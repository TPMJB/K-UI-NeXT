/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/salvage_revalidate.h"
#include "kui/recovery_checks.h"
#include "kui/recovery_guard.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RV_HEADER 4096u
#define RV_EVENT 512u
#define RV_PATCH 5120u
#define RV_TARGET 96u
#define RV_CHUNK 32u
#define RV_DATA (RV_CHUNK*KUI_RAW_BYTES)
#define RV_NONE UINT32_MAX
enum { RV_EV_TARGET=1,RV_EV_CHUNK,RV_EV_BASELINE,RV_EV_READY,RV_EV_REPAIRED,RV_EV_COMPLETE,RV_EV_CANDIDATE };
struct rv_pair { FIL a,b; bool a_open,b_open; uint64_t bytes; };
struct rv_target { uint32_t track,sector,slot,crc; bool repaired; };
struct rv {
    const struct kui_capture_plan *plan;
    const struct kui_capture_ops *ops;
    void (*progress)(void *,const struct kui_salvage_revalidate_status *);
    struct kui_salvage_revalidate_status *out;
    FATFS fs;
    struct rv_pair source,journal,patches,track,index;
    FIL index_write; bool index_write_open,index_owned,mounted;
    bool ready,completed,baselined[99];
    uint32_t sectors[99],crc[99],baseline[99],pending[RV_CHUNK];
    uint32_t target_count,patch_count,recovered,pending_count;
    uint64_t sequence,journal_bytes,patch_bytes,last_progress;
    uint8_t header[RV_HEADER],identity[32],journal_hash[32],patch_hash[32];
    uint8_t event[RV_EVENT],event_again[RV_EVENT];
    uint8_t data_storage[RV_DATA+31u],again_storage[RV_DATA+31u];
    uint8_t patch_storage[RV_PATCH+31u],patch_again_storage[RV_PATCH+31u];
    uint8_t *data,*again,*patch,*patch_again;
    char path[KUI_DEST_PATH_CAP],index_path[KUI_DEST_PATH_CAP];
};
_Static_assert(sizeof(struct rv)<=256u*1024u,"Salvage revalidation memory is bounded");
static uint32_t rv_u32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t rv_u64(const uint8_t *p) {return rv_u32(p)|(uint64_t)rv_u32(p+4)<<32;}
static void rv_put(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));}
static bool rv_stop(struct rv *r) {return r->ops->cancelled && r->ops->cancelled(r->ops->ctx);}
static bool rv_fail(struct rv *r,const char *message) {
    if(!r->out->failed)snprintf(r->out->message,sizeof(r->out->message),"%s",message);
    r->out->failed=true;r->ops->log("Salvage recheck: %s",message);return false;
}
static void rv_progress(struct rv *r,bool force) {
    uint64_t now=r->ops->now_ms?r->ops->now_ms(r->ops->ctx):0;
    if(r->progress && (force || !r->ops->now_ms || now-r->last_progress>=100u)) {
        r->last_progress=now;r->progress(r->ops->ctx,r->out);
    }
}
static void rv_phase(struct rv *r,enum kui_salvage_revalidate_phase phase,const char *message) {
    r->out->phase=phase;snprintf(r->out->message,sizeof(r->out->message),"%s",message);rv_progress(r,true);
}
static void rv_path(struct rv *r,const char *name) {snprintf(r->path,sizeof(r->path),"0:%s/%s",r->out->job,name);}
static bool rv_close(struct rv *r,struct rv_pair *p) {
    bool ok=true;
    if(p->a_open) {p->a_open=false;if(f_close(&p->a)!=FR_OK)ok=rv_fail(r,"Saved source close failed; recheck incomplete");}
    if(p->b_open) {p->b_open=false;if(f_close(&p->b)!=FR_OK)ok=rv_fail(r,"Repeated source close failed; recheck incomplete");}
    return ok;
}
static bool rv_open_path(struct rv *r,struct rv_pair *p,const char *name,uint64_t expected) {
    if(rv_stop(r) || r->out->failed)return false;
    if(p->a_open || p->b_open)return rv_fail(r,"Internal source ownership error");
    if(f_open(&p->a,name,FA_READ)!=FR_OK)return rv_fail(r,"Cannot open saved source read-only");
    p->a_open=true;
    if(f_open(&p->b,name,FA_READ)!=FR_OK)return rv_fail(r,"Cannot open independent repeated saved source");
    p->b_open=true;
    p->bytes=f_size(&p->a);
    if(p->bytes!=f_size(&p->b) || (expected!=UINT64_MAX && p->bytes!=expected))
        return rv_fail(r,"Saved source length differs from the validated layout");
    return !rv_stop(r);
}
static bool rv_open(struct rv *r,struct rv_pair *p,const char *name,uint64_t expected) {
    rv_path(r,name);return rv_open_path(r,p,r->path,expected);
}
static bool rv_read(struct rv *r,struct rv_pair *p,void *a,void *b,unsigned count,bool source) {
    UINT got=0;
    if(rv_stop(r) || r->out->failed)return false;
    if(f_read(&p->a,a,count,&got)!=FR_OK || got!=count)return rv_fail(r,"Saved source read failed or was short");
    if(source)r->out->bytes_done+=count;
    if(rv_stop(r))return false;
    got=0;if(f_read(&p->b,b,count,&got)!=FR_OK || got!=count)return rv_fail(r,"Repeated saved source read failed or was short");
    if(source)r->out->bytes_done+=count;
    if(memcmp(a,b,count)) {
        /* A recheck may have started without a marker. Proven disagreement
         * must block a later ordinary Resume too, not just this invocation. */
        if(!kui_recovery_guard_mark(r->out->job,r->ops->log))
            kui_recovery_guard_reblock(r->out->job,r->ops->log);
        return rv_fail(r,"Saved source bytes disagree between independent reads");
    }
    rv_progress(r,false);return !rv_stop(r);
}
static bool rv_seek(struct rv *r,struct rv_pair *p,uint64_t offset) {
    if(offset>p->bytes || f_lseek(&p->a,(FSIZE_t)offset)!=FR_OK || f_tell(&p->a)!=offset ||
       f_lseek(&p->b,(FSIZE_t)offset)!=FR_OK || f_tell(&p->b)!=offset)return rv_fail(r,"Saved source seek failed");
    return !rv_stop(r);
}
static bool rv_plan(const struct kui_capture_plan *p) {
    if(!p || !p->count || p->count>99 || (p->profile!=KUI_CAPTURE_PROFILE_GD && p->profile!=KUI_CAPTURE_PROFILE_CD_TOC))return false;
    uint64_t bytes=0;
    for(unsigned i=0;i<p->count;++i) {
        const struct kui_capture_track *t=&p->tracks[i];
        if(t->number!=i+1u || (t->control!=0 && t->control!=4) || t->session>1 || t->start<150 ||
           t->end<=t->start || t->toc_end<t->end || t->toc_end>KUI_RECOVERY_FAD_MAX+1u ||
           (i && t->start<p->tracks[i-1].end))return false;
        if(p->profile==KUI_CAPTURE_PROFILE_CD_TOC && (t->session || t->toc_end!=t->end || (i && t->start!=p->tracks[i-1].end)))return false;
        bytes+=(uint64_t)(t->end-t->start)*KUI_RAW_BYTES;
    }
    return bytes==p->bytes;
}
static bool rv_header(struct rv *r) {
    rv_phase(r,KUI_SALVAGE_REVALIDATE_HEADER,"Reading and comparing the saved salvage header");
    if(!rv_open(r,&r->source,"header.bin",RV_HEADER) || !rv_read(r,&r->source,r->data,r->again,RV_HEADER,true))return false;
    memcpy(r->header,r->data,RV_HEADER);if(!rv_close(r,&r->source))return false;
    const uint8_t *h=r->header;unsigned version=rv_u32(h+8);
    bool cd=r->plan->profile==KUI_CAPTURE_PROFILE_CD_TOC;
    if(version!=(cd?3u:2u) || memcmp(h,cd?"KUISALV3":"KUISALV2",8) || rv_u32(h+12)!=RV_HEADER ||
       rv_u32(h+16)!=r->plan->count || rv_u32(h+20)>1 || rv_u32(h+RV_HEADER-4)!=kui_crc32(0,h,RV_HEADER-4) ||
       (cd && rv_u32(h+4064)!=KUI_CAPTURE_PROFILE_CD_TOC))return rv_fail(r,"Only valid packed v2/v3 salvage headers can be rechecked");
    for(unsigned i=0;i<r->plan->count;++i) {
        const struct kui_capture_track *t=&r->plan->tracks[i];const uint8_t *b=h+96+i*24;
        if(rv_u32(b)!=t->number || rv_u32(b+4)!=t->control || rv_u32(b+8)!=t->session ||
           rv_u32(b+12)!=t->start || rv_u32(b+16)!=t->end || rv_u32(b+20)!=t->toc_end)
            return rv_fail(r,"Saved header and admitted disc layout disagree");
    }
    for(unsigned i=96+r->plan->count*24;i<RV_HEADER-4;++i)
        if(!(cd && i>=4064 && i<4068) && h[i])return rv_fail(r,"Unknown saved header fields");
    if(rv_u32(h+24)==rv_u32(h+28) || (cd && !memcmp(h+32,h+64,32)))return rv_fail(r,"Saved identity anchors are not distinct");
    for(unsigned a=0;a<2;++a) {
        uint32_t fad=rv_u32(h+24+a*4);bool found=false;
        for(unsigned i=0;i<r->plan->count;++i) {
            const struct kui_capture_track *t=&r->plan->tracks[i];
            if(fad>=t->start && fad<t->end && (cd || (t->control==4 && t->session==1)))found=true;
        }
        if(!found)return rv_fail(r,"Saved identity anchor is outside the admitted layout");
    }
    struct kui_sha256 hsh;kui_sha256_init(&hsh);kui_sha256_update(&hsh,h,RV_HEADER);kui_sha256_digest(&hsh,r->identity);
    return true;
}
static bool rv_index_write(struct rv *r,unsigned index,const struct rv_target *t) {
    uint8_t b[RV_TARGET]={0};memcpy(b,"KUIRVT1",8);rv_put(b+8,index);rv_put(b+12,t->track);rv_put(b+16,t->sector);
    rv_put(b+20,t->slot);rv_put(b+24,t->crc);rv_put(b+28,t->repaired?1u:0u);memcpy(b+32,r->identity,32);
    rv_put(b+92,kui_crc32(0,b,92));UINT put=0;
    uint64_t off=(uint64_t)index*RV_TARGET;
    if(f_lseek(&r->index_write,(FSIZE_t)off)!=FR_OK || f_tell(&r->index_write)!=off ||
       f_write(&r->index_write,b,RV_TARGET,&put)!=FR_OK || put!=RV_TARGET)return rv_fail(r,"Cannot build disposable recheck index");
    return true;
}
static bool rv_index_decode(struct rv *r,unsigned index,const uint8_t *b,struct rv_target *t) {
    if(memcmp(b,"KUIRVT1",8) || rv_u32(b+8)!=index || memcmp(b+32,r->identity,32) ||
       rv_u32(b+92)!=kui_crc32(0,b,92) || rv_u32(b+12)>=r->plan->count || rv_u32(b+28)>1)
        return rv_fail(r,"Disposable recheck index failed its identity or CRC checks");
    for(unsigned i=64;i<92;++i)if(b[i])return rv_fail(r,"Unknown disposable recheck index fields");
    *t=(struct rv_target){rv_u32(b+12),rv_u32(b+16),rv_u32(b+20),rv_u32(b+24),rv_u32(b+28)!=0};
    if(t->sector>=r->plan->tracks[t->track].end-r->plan->tracks[t->track].start ||
       (t->slot!=RV_NONE && t->slot>=r->patch_count))return rv_fail(r,"Disposable recheck target is outside the saved layout");
    return true;
}
static bool rv_index_get(struct rv *r,unsigned index,struct rv_target *t) {
    uint8_t a[RV_TARGET],b[RV_TARGET];uint64_t off=(uint64_t)index*RV_TARGET;
    if(index>=r->target_count)return rv_fail(r,"Journal references an unknown salvage target");
    if(r->index_write_open) {
        UINT got=0;
        if(f_lseek(&r->index_write,(FSIZE_t)off)!=FR_OK || f_tell(&r->index_write)!=off ||
           f_read(&r->index_write,a,RV_TARGET,&got)!=FR_OK || got!=RV_TARGET)return rv_fail(r,"Cannot read disposable recheck index");
    } else if(!rv_seek(r,&r->index,off) || !rv_read(r,&r->index,a,b,RV_TARGET,false))return false;
    return rv_index_decode(r,index,a,t);
}
static bool rv_create_index(struct rv *r) {
    FRESULT result=f_mkdir("0:/KUI/probes");
    if(result!=FR_OK && result!=FR_EXIST)return rv_fail(r,"Cannot create recheck workspace");
    for(unsigned i=1;i<=9999;++i) {
        if(rv_stop(r))return false;
        snprintf(r->index_path,sizeof(r->index_path),"0:/KUI/probes/salvage-recheck-%04u.idx",i);
        result=f_open(&r->index_write,r->index_path,FA_READ|FA_WRITE|FA_CREATE_NEW);
        if(result==FR_EXIST)continue;
        if(result!=FR_OK)return rv_fail(r,"Cannot create a unique disposable recheck index");
        r->index_owned=r->index_write_open=true;return true;
    }
    return rv_fail(r,"All disposable recheck workspace names are occupied");
}
static unsigned rv_active(const struct rv *r) {unsigned i=0;while(i<r->plan->count && r->baselined[i])++i;return i;}
static bool rv_event(struct rv *r) {
    const uint8_t *e=r->event;unsigned type=rv_u32(e+16),track=rv_u32(e+20);
    uint32_t sector=rv_u32(e+24),count=rv_u32(e+28),crc=rv_u32(e+32);unsigned active=rv_active(r);
    if(memcmp(e,"KUISJNL1",8) || rv_u32(e+508)!=kui_crc32(0,e,508) || rv_u64(e+8)!=r->sequence+1u ||
       memcmp(e+40,r->identity,32) || rv_u32(e+36) || r->completed)return rv_fail(r,"Salvage journal record integrity or order is invalid");
    for(unsigned i=72;i<508;++i)if(e[i])return rv_fail(r,"Unknown salvage journal fields");
    if(type==RV_EV_TARGET) {
        if(r->ready || track!=active || track>=r->plan->count || count || crc || r->pending_count==RV_CHUNK ||
           (uint64_t)r->target_count+r->pending_count>=r->plan->bytes/KUI_RAW_BYTES || sector<r->sectors[track] ||
           sector>=r->sectors[track]+RV_CHUNK || sector>=r->plan->tracks[track].end-r->plan->tracks[track].start ||
           (r->pending_count && sector<=r->pending[r->pending_count-1]))return rv_fail(r,"Invalid journal target ordering or extent");
        r->pending[r->pending_count++]=sector;
    } else if(type==RV_EV_CHUNK) {
        if(r->ready || track!=active || track>=r->plan->count || sector!=r->sectors[track] || !count || count>RV_CHUNK ||
           count>r->plan->tracks[track].end-r->plan->tracks[track].start-sector)return rv_fail(r,"Invalid journal collection chunk");
        for(unsigned i=0;i<r->pending_count;++i) {
            if(r->pending[i]>=sector+count)return rv_fail(r,"Journal target extends beyond its committed chunk");
            const struct rv_target t={track,r->pending[i],RV_NONE,0,false};
            if(!rv_index_write(r,r->target_count,&t))return false;
            ++r->target_count;
        }
        r->pending_count=0;r->sectors[track]+=count;r->crc[track]=crc;
    } else if(type==RV_EV_BASELINE) {
        if(r->ready || r->pending_count || track!=active || track>=r->plan->count || sector || count ||
           r->sectors[track]!=r->plan->tracks[track].end-r->plan->tracks[track].start || crc!=r->crc[track])
            return rv_fail(r,"Invalid journal baseline transition");
        r->baseline[track]=crc;r->baselined[track]=true;
    } else if(type==RV_EV_READY) {
        if(r->ready || r->pending_count || active!=r->plan->count || track || sector || count!=r->target_count || crc)
            return rv_fail(r,"Invalid journal ready transition");
        r->ready=true;
    } else if(type==RV_EV_CANDIDATE) {
        struct rv_target t;
        if(!r->ready || track>=r->target_count || sector!=r->patch_count || count || !rv_index_get(r,track,&t))
            return rv_fail(r,"Invalid journal candidate reference");
        if(t.repaired || t.slot!=RV_NONE)return rv_fail(r,"Duplicate published candidate for a target");
        t.slot=r->patch_count++;t.crc=crc;if(!rv_index_write(r,track,&t))return false;
    } else if(type==RV_EV_REPAIRED) {
        struct rv_target t;
        if(!r->ready || track>=r->target_count || sector || count || !rv_index_get(r,track,&t))
            return rv_fail(r,"Invalid journal repaired reference");
        if(t.repaired || t.slot==RV_NONE || t.crc!=crc)return rv_fail(r,"Repaired target lacks its unique published candidate");
        t.repaired=true;++r->recovered;if(!rv_index_write(r,track,&t))return false;
    } else if(type==RV_EV_COMPLETE) {
        if(!r->ready || track || sector || count!=r->target_count || crc || r->recovered!=r->target_count)
            return rv_fail(r,"Invalid journal complete transition");
        r->completed=true;
    } else return rv_fail(r,"Unknown salvage journal event");
    ++r->sequence;return true;
}
static bool rv_replay(struct rv *r) {
    rv_phase(r,KUI_SALVAGE_REVALIDATE_JOURNAL,"Comparing and validating the complete salvage journal");
    if(!rv_create_index(r) || !rv_open(r,&r->source,"journal.bin",UINT64_MAX))return false;
    r->journal_bytes=r->source.bytes;
    uint64_t max=(r->plan->bytes/KUI_RAW_BYTES*4u+r->plan->count+4u)*RV_EVENT;
    if(!r->journal_bytes || r->journal_bytes%RV_EVENT || r->journal_bytes>max)
        return rv_fail(r,"Journal has a torn tail or unsupported length; no files were repaired");
    struct kui_sha256 hash;kui_sha256_init(&hash);
    for(uint64_t pos=0;pos<r->journal_bytes;pos+=RV_EVENT) {
        if(!rv_read(r,&r->source,r->event,r->event_again,RV_EVENT,true))return false;
        kui_sha256_update(&hash,r->event,RV_EVENT);if(!rv_event(r))return false;
    }
    kui_sha256_digest(&hash,r->journal_hash);
    if(!rv_close(r,&r->source))return false;
    if(!r->ready || r->pending_count)return rv_fail(r,"Only fully collected jobs with no pending journal tail can be rechecked");
    if(r->patch_count!=r->recovered)return rv_fail(r,"A published candidate has no committed repair; recheck leaves it untouched");
    if(f_sync(&r->index_write)!=FR_OK)return rv_fail(r,"Disposable index sync failed");
    r->index_write_open=false;if(f_close(&r->index_write)!=FR_OK)return rv_fail(r,"Disposable index close failed");
    if(!rv_open_path(r,&r->index,r->index_path,(uint64_t)r->target_count*RV_TARGET))return false;
    r->out->targets=r->target_count;r->out->recovered=r->recovered;r->out->candidates=r->patch_count;
    return true;
}
static unsigned rv_sector_check(struct rv *r,const uint8_t *raw,uint32_t fad) {
    return r->plan->profile==KUI_CAPTURE_PROFILE_CD_TOC?kui_recovery_cd_sector_check(raw,KUI_RAW_BYTES,fad):kui_recovery_sector_check(raw,KUI_RAW_BYTES,fad);
}
static bool rv_patch_valid(struct rv *r,const uint8_t *p,unsigned slot,struct rv_target *target) {
    unsigned index=rv_u32(p+8);struct rv_target t;
    if(!rv_index_get(r,index,&t))return false;
    if(memcmp(p,"KUISPAT2",8) || t.slot!=slot || !t.repaired || rv_u32(p+12)!=t.track || rv_u32(p+16)!=t.sector ||
       rv_u32(p+20)!=(r->plan->tracks[t.track].control==4?1u:2u) || memcmp(p+32,r->identity,32) ||
       rv_u32(p+RV_PATCH-4)!=kui_crc32(0,p,RV_PATCH-4) || rv_u32(p+28)!=t.crc)
        return rv_fail(r,"Published candidate and journal target mapping disagree");
    for(unsigned i=64;i<64+KUI_RAW_BYTES;++i)if(p[i])return rv_fail(r,"Candidate original backup is not a zero placeholder");
    if(rv_u32(p+24)!=kui_crc32(0,p+64,KUI_RAW_BYTES) || rv_u32(p+28)!=kui_crc32(0,p+64+KUI_RAW_BYTES,KUI_RAW_BYTES))
        return rv_fail(r,"Candidate backup or payload CRC failed");
    for(unsigned i=64+2*KUI_RAW_BYTES;i<RV_PATCH-4;++i)if(p[i])return rv_fail(r,"Unknown published candidate fields");
    if(r->plan->tracks[t.track].control==4 && rv_sector_check(r,p+64+KUI_RAW_BYTES,r->plan->tracks[t.track].start+t.sector))
        return rv_fail(r,"Published data candidate failed FAD, EDC or parity checks");
    if(target)*target=t;
    return true;
}
static bool rv_candidates(struct rv *r) {
    rv_phase(r,KUI_SALVAGE_REVALIDATE_CANDIDATES,"Comparing published candidates against journal targets");
    r->patch_bytes=(uint64_t)r->patch_count*RV_PATCH;
    if(!rv_open(r,&r->source,"patches.bin",r->patch_bytes))return false;
    r->out->bytes_total=4u*RV_HEADER+4u*r->journal_bytes+6u*r->patch_bytes+2u*r->plan->bytes;
    struct kui_sha256 hash;kui_sha256_init(&hash);
    for(unsigned slot=0;slot<r->patch_count;++slot) {
        if(!rv_read(r,&r->source,r->patch,r->patch_again,RV_PATCH,true) || !rv_patch_valid(r,r->patch,slot,NULL))return false;
        kui_sha256_update(&hash,r->patch,RV_PATCH);
    }
    kui_sha256_digest(&hash,r->patch_hash);return rv_close(r,&r->source);
}
struct rv_checkpoint {uint32_t track,end,crc;bool baseline;};
static bool rv_checkpoint(struct rv *r,struct kui_sha256 *hash,struct rv_checkpoint *checkpoint) {
    while(f_tell(&r->journal.a)<r->journal_bytes) {
        if(!rv_read(r,&r->journal,r->event,r->event_again,RV_EVENT,true))return false;
        kui_sha256_update(hash,r->event,RV_EVENT);unsigned type=rv_u32(r->event+16);
        if(type!=RV_EV_TARGET && type!=RV_EV_CHUNK && type!=RV_EV_BASELINE)
            return rv_fail(r,"Collection checkpoint ended before all saved tracks");
        if(type==RV_EV_TARGET)continue;
        *checkpoint=(struct rv_checkpoint){rv_u32(r->event+20),
            type==RV_EV_CHUNK?rv_u32(r->event+24)+rv_u32(r->event+28):0,rv_u32(r->event+32),type==RV_EV_BASELINE};
        return true;
    }
    return rv_fail(r,"Collection checkpoint is missing");
}
static bool rv_saved_anchor(struct rv *r,const uint8_t *raw,uint32_t fad,bool seen[2]) {
    for(unsigned i=0;i<2;++i)if(fad==rv_u32(r->header+24+i*4)) {
        struct kui_sha256 hash;uint8_t digest[32];kui_sha256_init(&hash);kui_sha256_update(&hash,raw,KUI_RAW_BYTES);kui_sha256_digest(&hash,digest);
        if(memcmp(digest,r->header+32+i*32,32))return rv_fail(r,"Saved identity anchor differs or is unresolved; quarantine retained");
        seen[i]=true;
    }
    return true;
}
static bool rv_tracks(struct rv *r) {
    rv_phase(r,KUI_SALVAGE_REVALIDATE_TRACKS,"Comparing every saved track byte and validating baseline checkpoints");
    if(!rv_open(r,&r->journal,"journal.bin",r->journal_bytes) || !rv_open(r,&r->patches,"patches.bin",r->patch_bytes))return false;
    struct kui_sha256 journal_hash;kui_sha256_init(&journal_hash);bool anchors[2]={false,false};
    unsigned next=0;struct rv_target target={0};bool have=next<r->target_count;
    if(have && !rv_index_get(r,next,&target))return false;
    static const uint8_t zero[KUI_RAW_BYTES]={0};
    for(unsigned ti=0;ti<r->plan->count;++ti) {
        r->out->track=ti+1u;const struct kui_capture_track *track=&r->plan->tracks[ti];
        char name[32];snprintf(name,sizeof(name),"track%02u.%s",ti+1u,track->control==4?"bin":"raw");
        uint32_t sectors=track->end-track->start,crc=0;struct rv_checkpoint checkpoint;
        if(!rv_open(r,&r->track,name,(uint64_t)sectors*KUI_RAW_BYTES) || !rv_checkpoint(r,&journal_hash,&checkpoint))return false;
        for(uint32_t start=0;start<sectors;) {
            unsigned count=sectors-start>RV_CHUNK?RV_CHUNK:sectors-start;
            if(!rv_read(r,&r->track,r->data,r->again,count*KUI_RAW_BYTES,true))return false;
            for(unsigned j=0;j<count;++j) {
                if(rv_stop(r))return false;
                uint32_t sector=start+j,fad=track->start+sector;const uint8_t *raw=r->data+j*KUI_RAW_BYTES;
                if(have && (target.track<ti || (target.track==ti && target.sector<sector)))return rv_fail(r,"Derived target order changed during full track scan");
                bool is_target=have && target.track==ti && target.sector==sector;
                if(is_target) {
                    if(target.repaired) {
                        if(target.slot==RV_NONE || !rv_seek(r,&r->patches,(uint64_t)target.slot*RV_PATCH) ||
                           !rv_read(r,&r->patches,r->patch,r->patch_again,RV_PATCH,true) || !rv_patch_valid(r,r->patch,target.slot,NULL))return false;
                        if(rv_u32(r->patch+8)!=next || memcmp(raw,r->patch+64+KUI_RAW_BYTES,KUI_RAW_BYTES))
                            return rv_fail(r,"Repaired track bytes differ from their published candidate");
                    } else if(target.slot!=RV_NONE || target.crc || memcmp(raw,zero,KUI_RAW_BYTES))
                        return rv_fail(r,"Unresolved target is not its original zero placeholder");
                    crc=kui_crc32_capture(crc,zero,KUI_RAW_BYTES);
                    ++next;have=next<r->target_count;if(have && !rv_index_get(r,next,&target))return false;
                } else {
                    if(track->control==4 && rv_sector_check(r,raw,fad))return rv_fail(r,"Non-target data sector failed FAD, EDC or parity checks");
                    crc=kui_crc32_capture(crc,raw,KUI_RAW_BYTES);
                }
                if(!rv_saved_anchor(r,raw,fad,anchors))return false;
                if(checkpoint.baseline || checkpoint.track!=ti || checkpoint.end<sector+1u)
                    return rv_fail(r,"Journal collection checkpoint does not match the track prefix");
                if(checkpoint.end==sector+1u) {
                    if(checkpoint.crc!=crc)return rv_fail(r,"Original normalized track prefix CRC differs from the journal");
                    if(!rv_checkpoint(r,&journal_hash,&checkpoint))return false;
                    if(sector+1u==sectors) {
                        if(!checkpoint.baseline || checkpoint.track!=ti || checkpoint.crc!=crc || crc!=r->baseline[ti])
                            return rv_fail(r,"Original normalized track baseline CRC differs from the journal");
                    } else if(checkpoint.baseline)return rv_fail(r,"Track baseline appears before its complete saved extent");
                }
            }
            start+=count;rv_progress(r,false);
        }
        if(!rv_close(r,&r->track))return false;
    }
    if(have || next!=r->target_count || !anchors[0] || !anchors[1])return rv_fail(r,"Not all journal targets and saved identity anchors were checked");
    while(f_tell(&r->journal.a)<r->journal_bytes) {
        if(!rv_read(r,&r->journal,r->event,r->event_again,RV_EVENT,true))return false;
        kui_sha256_update(&journal_hash,r->event,RV_EVENT);
    }
    uint8_t digest[32];kui_sha256_digest(&journal_hash,digest);
    if(memcmp(digest,r->journal_hash,32))return rv_fail(r,"Journal changed after its semantic validation");
    return rv_close(r,&r->journal) && rv_close(r,&r->patches);
}
static bool rv_repeat_hash(struct rv *r,const char *name,uint64_t bytes,const uint8_t expected[32]) {
    if(!rv_open(r,&r->source,name,bytes))return false;
    struct kui_sha256 hash;kui_sha256_init(&hash);
    for(uint64_t pos=0;pos<bytes;) {
        unsigned n=bytes-pos>RV_DATA?RV_DATA:(unsigned)(bytes-pos);
        if(!rv_read(r,&r->source,r->data,r->again,n,true))return false;
        kui_sha256_update(&hash,r->data,n);pos+=n;
    }
    uint8_t digest[32];kui_sha256_digest(&hash,digest);
    if(memcmp(digest,expected,32))return rv_fail(r,"Saved metadata changed during full track validation");
    return rv_close(r,&r->source);
}
static bool rv_remove_index(struct rv *r) {
    if(r->index_write_open) {r->index_write_open=false;if(f_close(&r->index_write)!=FR_OK)return rv_fail(r,"Disposable index close failed");}
    if(!rv_close(r,&r->index))return false;
    if(r->index_owned) {
        if(f_unlink(r->index_path)!=FR_OK)return rv_fail(r,"Cannot remove the owned disposable recheck index");
        r->index_owned=false;
    }
    return true;
}
bool kui_salvage_revalidate(const struct kui_capture_plan *plan,const struct kui_capture_ops *ops,const char *job,
    void (*progress)(void *,const struct kui_salvage_revalidate_status *),struct kui_salvage_revalidate_status *out) {
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!rv_plan(plan) || !ops || !ops->log || !job) {out->failed=true;snprintf(out->message,sizeof(out->message),"Invalid saved salvage recheck request");return false;}
    if(!strncmp(job,"0:",2))job+=2;
    bool name_ok=strlen(job)==21 && !strncmp(job,"/KUI/salvage/job-",17) && strcmp(job+17,"0000");
    if(name_ok)for(unsigned i=17;i<21;++i)if(job[i]<'0' || job[i]>'9')name_ok=false;
    if(!name_ok) {out->failed=true;snprintf(out->message,sizeof(out->message),"Select an exact existing salvage job folder");return false;}
    struct rv *r=calloc(1,sizeof(*r));
    if(!r) {out->failed=true;snprintf(out->message,sizeof(out->message),"Not enough memory for bounded salvage recheck");return false;}
    r->plan=plan;r->ops=ops;r->progress=progress;r->out=out;out->tracks=plan->count;snprintf(out->job,sizeof(out->job),"%s",job);
    r->data=(uint8_t *)(((uintptr_t)r->data_storage+31u)&~(uintptr_t)31u);r->again=(uint8_t *)(((uintptr_t)r->again_storage+31u)&~(uintptr_t)31u);
    r->patch=(uint8_t *)(((uintptr_t)r->patch_storage+31u)&~(uintptr_t)31u);r->patch_again=(uint8_t *)(((uintptr_t)r->patch_again_storage+31u)&~(uintptr_t)31u);
    bool ok=false;
    if(rv_stop(r))goto done;
    if(!kui_mount(&r->fs,ops->log)) {rv_fail(r,"Cannot mount SD for saved salvage recheck");goto done;}r->mounted=true;
    if(!kui_recovery_guard_validate(out->job,ops->log)) {rv_fail(r,"Quarantine identity or CRC cannot be validated");goto done;}
    ops->log("SALVAGE RECHECK start folder=%s; strict packed collected jobs; sources read-only",out->job);
    if(!rv_header(r) || !rv_replay(r) || !rv_candidates(r) || !rv_tracks(r))goto done;
    rv_phase(r,KUI_SALVAGE_REVALIDATE_FINAL,"Rechecking metadata stability before removing the recovery block");
    if(!rv_repeat_hash(r,"header.bin",RV_HEADER,r->identity) || !rv_repeat_hash(r,"patches.bin",r->patch_bytes,r->patch_hash) ||
       !rv_remove_index(r) || rv_stop(r))goto done;
    if(!kui_recovery_guard_clear(out->job,ops->log)) {rv_fail(r,"Full recheck passed but quarantine clearing was not confirmed");goto done;}
    out->guard_cleared=true;ok=true;
done:
    if(!rv_close(r,&r->source))ok=false;
    if(!rv_close(r,&r->journal))ok=false;
    if(!rv_close(r,&r->patches))ok=false;
    if(!rv_close(r,&r->track))ok=false;
    if(!rv_remove_index(r))ok=false;
    if(r->mounted && f_mount(NULL,"0:",0)!=FR_OK) {
        if(out->guard_cleared) {
            out->guard_cleared=false;
            kui_recovery_guard_reblock(out->job,ops->log);
        }
        rv_fail(r,"Saved recheck unmount failed; recovery authorization withdrawn");ok=false;
    }
    if(!ok && !out->failed)out->stopped=rv_stop(r);
    out->complete=ok && !out->failed && !out->stopped && out->guard_cleared;
    if(out->complete)snprintf(out->message,sizeof(out->message),"Saved salvage data verified; recovery block cleared; repairs remain paused");
    else if(out->stopped)snprintf(out->message,sizeof(out->message),"Stopped; full validation incomplete; recovery remains blocked");
    else if(!out->failed)rv_fail(r,"Saved salvage recheck did not complete");
    ops->log("SALVAGE RECHECK end complete=%u failed=%u stopped=%u guard_cleared=%u bytes=%" PRIu64 "/%" PRIu64 " targets=%u recovered=%u candidates=%u",
        out->complete?1u:0u,out->failed?1u:0u,out->stopped?1u:0u,out->guard_cleared?1u:0u,
        out->bytes_done,out->bytes_total,out->targets,out->recovered,out->candidates);
    rv_progress(r,true);bool result=out->complete;free(r);return result;
}
