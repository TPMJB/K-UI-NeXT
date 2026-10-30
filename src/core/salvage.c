/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/salvage.h"
#include "kui/recovery_checks.h"
#include "kui/recovery_guard.h"
#include "kui/media.h"
#include "diskio.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every journal record occupies its own 512-byte sector. A torn final record
 * can be discarded: data commits follow track sync; repairs follow immutable
 * candidate publication plus exact track readback. Earlier corruption fails
 * closed. FatFs/card firmware cannot promise physical power-failure atomicity. */
#define HEADER_BYTES 4096u
#define PROFILE_OFFSET 4064u
#define EVENT_BYTES 512u
#define PATCH_BYTES 8192u
#define PACKED_PATCH_BYTES 5120u
#define TARGET_BYTES 32u
#define NO_PATCH UINT32_MAX
#define CHUNK 32u
#define REPAIR_CHUNK 16u
enum event_type { EV_TARGET=1,EV_CHUNK,EV_BASELINE,EV_READY,EV_REPAIRED,EV_COMPLETE,EV_CANDIDATE };
struct target { uint32_t sector, crc, track, patch_slot; bool repaired; };
struct salvage {
    const struct kui_capture_plan *plan;
    const struct kui_capture_ops *ops;
    const struct kui_salvage_options *options;
    struct kui_salvage_status *status;
    uint32_t sectors[99], crc[99], baseline[99];
    bool baselined[99], failed, journal_open, index_open, ready, completed, packed, identified, recovering, legacy_indexed;
    bool repair_batching;
    unsigned read_failures,collection_bad_ranges,collection_health_at,collection_issue_ranges,zero_yield_batches;
    unsigned target_count,recovered_count,patch_count,pending_count;
    uint32_t pending[CHUNK];
    uint64_t sequence,started,last_progress,pending_offset,collection_base,collection_started;
    FIL journal,index,track_file,packed_file;
    bool track_open,packed_open,track_write,packed_write;
    unsigned open_track;
    DWORD track_map_small[32],*track_map_heap;
    uint8_t header[HEADER_BYTES], event[EVENT_BYTES];
    /* calloc need not return a 32-byte-aligned object. Align the candidate
     * buffer inside owned padding, including when heap placement changes. */
    uint8_t patch_storage[PATCH_BYTES+31u], *patch;
    /* Reconciliation stages a sector-aligned SD window into a 32-byte-aligned
     * address inside data; leading/trailing SD bytes and pointer rounding fit
     * in this bounded padding without a separate allocation. */
    uint8_t data[CHUNK*KUI_RAW_BYTES+512u+32u], scratch[KUI_RAW_BYTES], identity[32];
    uint8_t repair_data[REPAIR_CHUNK*KUI_RAW_BYTES];
    char path[KUI_DEST_PATH_CAP];
};
_Static_assert(sizeof(struct salvage)<=256u*1024u,"Salvage worker memory must remain bounded");
static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t get64(const uint8_t *p) {return get32(p)|(uint64_t)get32(p+4)<<32;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));}
static void put64(uint8_t *p,uint64_t n) {put32(p,(uint32_t)n);put32(p+4,(uint32_t)(n>>32));}
static bool stop(struct salvage *s) {return s->ops->cancelled && s->ops->cancelled(s->ops->ctx);}
static bool fail(struct salvage *s,const char *message) {
    s->failed=true;snprintf(s->status->message,sizeof(s->status->message),"%s",message);
    s->ops->log("Salvage: %s",message);return false;
}
static uint64_t committed_bytes(const struct salvage *s) {
    uint64_t bytes=0;for(unsigned i=0;i<s->plan->count;++i)bytes+=(uint64_t)s->sectors[i]*KUI_RAW_BYTES;
    return bytes;
}
static void update(struct salvage *s,bool force) {
    struct kui_salvage_status *o=s->status;
    o->targets=s->target_count;o->recovered=s->recovered_count;o->done=committed_bytes(s);
    o->remaining=o->targets-o->recovered;o->first_pass_complete=s->ready;
    uint64_t now=s->ops->now_ms?s->ops->now_ms(s->ops->ctx):0;
    o->elapsed_ms=now>=s->started?now-s->started:0;
    if(o->collection_active) {
        o->collected_this_run=o->done>=s->collection_base?o->done-s->collection_base:0;
        o->collection_elapsed_ms=now>=s->collection_started?now-s->collection_started:0;
    }
    if(s->options->progress && (force || now-s->last_progress>=100 || !s->ops->now_ms)) {
        s->last_progress=now;s->options->progress(s->ops->ctx,o);
    }
}
static void saved_check_start(struct salvage *s,enum kui_salvage_sd_phase phase,
    uint64_t total,const char *message) {
    struct kui_salvage_status *o=s->status;
    o->sd_phase=phase;o->sd_done=0;o->sd_total=total;
    o->track=0;o->fad=0;o->read_fad=0;o->read_count=0;
    o->read_attempt=0;o->read_limit=0;o->read_confirm=0;o->read_confirm_limit=0;
    o->read_state=KUI_SALVAGE_READ_NONE;
    snprintf(o->message,sizeof(o->message),"%s",message);update(s,true);
}
static void saved_check_end(struct salvage *s) {
    update(s,true);s->status->sd_phase=KUI_SALVAGE_SD_NONE;
}
static void read_outcome(struct salvage *s,enum kui_salvage_read_state state) {
    struct kui_salvage_status *o=s->status;o->read_state=state;
    bool issue=state==KUI_SALVAGE_READ_RETRY || state==KUI_SALVAGE_READ_SPLIT ||
        state==KUI_SALVAGE_READ_DEFERRED || state==KUI_SALVAGE_READ_FATAL;
    bool resolved=state==KUI_SALVAGE_READ_ACCEPTED && o->last_problem_state!=KUI_SALVAGE_READ_NONE &&
        o->read_fad==o->last_problem_fad && o->read_count==o->last_problem_count;
    if(issue || resolved) {
        o->last_problem_fad=o->read_fad;o->last_problem_count=o->read_count;
        o->last_problem_attempt=o->read_attempt;o->last_problem_limit=o->read_limit;
        o->last_problem_confirm=o->read_confirm;o->last_problem_confirm_limit=o->read_confirm_limit;
        o->last_problem_state=state;
    }
    update(s,false);
}
/* Drive revalidation has its own optical reads, but must not replace the
 * interrupted operation's current range/try or track when it succeeds. */
struct read_progress {
    uint32_t fad,read_fad,read_count;
    unsigned track,attempt,limit,confirm,confirm_limit;
    enum kui_salvage_read_state state;
};
static struct read_progress save_read_progress(const struct kui_salvage_status *o) {
    return (struct read_progress){o->fad,o->read_fad,o->read_count,o->track,
        o->read_attempt,o->read_limit,o->read_confirm,o->read_confirm_limit,o->read_state};
}
static void restore_read_progress(struct kui_salvage_status *o,const struct read_progress *p) {
    o->fad=p->fad;o->read_fad=p->read_fad;o->read_count=p->read_count;o->track=p->track;
    o->read_attempt=p->attempt;o->read_limit=p->limit;o->read_confirm=p->confirm;
    o->read_confirm_limit=p->confirm_limit;o->read_state=p->state;
}
static bool read_context(struct salvage *s,uint32_t fad,unsigned count,unsigned attempt,
    unsigned limit,unsigned confirm,unsigned confirm_limit) {
    struct kui_salvage_status *o=s->status;
    bool changed=o->read_state!=KUI_SALVAGE_READ_READING || o->read_fad!=fad || o->read_count!=count ||
        o->read_attempt!=attempt || o->read_limit!=limit || o->read_confirm!=confirm || o->read_confirm_limit!=confirm_limit;
    o->fad=fad;o->read_fad=fad;o->read_count=count;
    o->read_attempt=attempt;o->read_limit=limit;o->read_confirm=confirm;o->read_confirm_limit=confirm_limit;
    o->read_state=KUI_SALVAGE_READ_READING;
    return changed;
}
static void path(struct salvage *s,const char *name) {snprintf(s->path,sizeof(s->path),"0:%s/%s",s->status->job,name);}
static void track_path(struct salvage *s,unsigned i) {
    char name[32];snprintf(name,sizeof(name),"track%02u.%s",i+1,s->plan->tracks[i].control==4?"bin":"raw");path(s,name);
}
static bool io_close(struct salvage *s,FIL *file,bool ok) {
    if(f_close(file)!=FR_OK)return fail(s,"Storage close failed; no success was committed");
    return ok;
}
static bool read_exact(struct salvage *s,FIL *file,void *data,unsigned bytes) {
    UINT got=0;if(f_read(file,data,bytes,&got)!=FR_OK || got!=bytes)return fail(s,"Storage read failed or file is truncated");return true;
}
static bool write_exact(struct salvage *s,FIL *file,const void *data,unsigned bytes) {
    UINT put=0;if(f_write(file,data,bytes,&put)!=FR_OK || put!=bytes)return fail(s,"Storage write failed or card is full");return true;
}
static bool sync_file(struct salvage *s,FIL *file) {
    if(f_sync(file)!=FR_OK)return fail(s,"Storage sync failed; preserve this incomplete job");
    return true;
}
static bool seek(struct salvage *s,FIL *file,uint64_t offset) {
    if(f_lseek(file,(FSIZE_t)offset)!=FR_OK || f_tell(file)!=offset)return fail(s,"Storage seek failed");
    return true;
}
#include "salvage_files.inc"
_Static_assert(sizeof(struct salvage)+sizeof(((struct salvage *)0)->data)+
    sizeof(((struct salvage *)0)->scratch)+SALVAGE_TRACK_MAP_WORDS*sizeof(DWORD)<=256u*1024u,
    "Salvage worker, retained acquisition bytes and largest track map must remain bounded");
/* The target index is a disposable, bounded-RAM acceleration file. The
 * immutable header and ordered journal are the authority; cold resumes
 * rebuild this file. A same-boot exclusive session may reuse only the exact
 * index bound to its checked RAM receipt. Records carry their own checksums so a
 * bad cache read cannot authorize a repair of another sector. */
static bool target_put(struct salvage *s,unsigned index,const struct target *t) {
    uint8_t b[TARGET_BYTES]={0};memcpy(b,"KUITGT2",7);put32(b+8,index);
    put32(b+12,t->sector);put32(b+16,t->crc);
    put32(b+20,t->track|(t->repaired?0x80000000u:0));put32(b+24,t->patch_slot);
    put32(b+28,kui_crc32(0,b,28));
    return seek(s,&s->index,(uint64_t)index*TARGET_BYTES) && write_exact(s,&s->index,b,sizeof(b));
}
static bool target_get(struct salvage *s,unsigned index,struct target *t) {
    uint8_t b[TARGET_BYTES];
    if(index>=s->target_count || !seek(s,&s->index,(uint64_t)index*TARGET_BYTES) || !read_exact(s,&s->index,b,sizeof(b)))return false;
    uint32_t track=get32(b+20);
    if(memcmp(b,"KUITGT2",8) || get32(b+8)!=index || get32(b+28)!=kui_crc32(0,b,28) ||
       (track&0x7fffffffu)>=s->plan->count || get32(b+12)>=s->plan->tracks[track&0x7fffffffu].end-s->plan->tracks[track&0x7fffffffu].start)
        return fail(s,"Derived target index changed; resume rebuilds it from the journal");
    *t=(struct target){get32(b+12),get32(b+16),track&0x7fffffffu,get32(b+24),(track&0x80000000u)!=0};
    /* A v1 cache uses slot zero only as a presence marker from a completed
     * directory scan; the original candidate file remains the authority. */
    if(t->patch_slot!=NO_PATCH && (s->packed?t->patch_slot>=s->patch_count:t->patch_slot!=0u))
        return fail(s,"Derived candidate index is invalid");
    return true;
}
/* Writers use a minimum of four digits, not arbitrary zero padding. The
 * canonical name is too long for an SFN alias; compare its ASCII case variants
 * just as FatFs does when opening that generated path. */
static bool legacy_patch_number(const char *name,unsigned *index) {
    char folded[32],canonical[32];unsigned length=0;
    while(name[length]) {
        if(length>=sizeof(folded)-1u)return false;
        unsigned char c=(unsigned char)name[length];
        if(c>=128u)return false;
        folded[length]=(char)(c>='a' && c<='z'?c-'a'+'A':c);++length;
    }
    folded[length]=0;
    if(strncmp(folded,"PATCH-",6))return false;
    unsigned value=0,digits=0;const char *p=folded+6;
    while(*p>='0' && *p<='9') {
        unsigned digit=(unsigned)(*p++-'0');
        if(value>(UINT_MAX-digit)/10u)return false;
        value=value*10u+digit;++digits;
    }
    if(digits<4u || strcmp(p,".BIN"))return false;
    snprintf(canonical,sizeof(canonical),"PATCH-%04u.BIN",value);
    if(strcmp(folded,canonical))return false;
    *index=value;return true;
}
/* Legacy candidates are individually published files and have no CANDIDATE
 * journal event. Discover them once, then reuse a checksummed presence marker
 * in the disposable target cache. No absence is trusted until the entire
 * directory and close have succeeded and the derived index has synced. */
static bool index_legacy_patches(struct salvage *s) {
    if(s->packed || s->legacy_indexed)return true;
    saved_check_start(s,KUI_SALVAGE_SD_DIRECTORY,0,"Indexing published legacy repair candidates");
    if(stop(s))return false;
    snprintf(s->path,sizeof(s->path),"0:%s",s->status->job);
    DIR dir;FRESULT r=f_opendir(&dir,s->path);
    if(r!=FR_OK)return fail(s,"Cannot list legacy repair candidates");
    bool ok=true;unsigned candidates=0;
    for(;;) {
        if(stop(s)) {ok=false;break;}
        FILINFO info;r=f_readdir(&dir,&info);
        if(r!=FR_OK) {ok=fail(s,"Cannot finish legacy candidate directory scan");break;}
        if(!info.fname[0])break;
        ++s->status->sd_done;
        unsigned index;
        if(legacy_patch_number(info.fname,&index) && index<s->target_count) {
            if((info.fattrib&AM_DIR) || info.fsize!=PATCH_BYTES) {
                ok=fail(s,"Published legacy candidate has invalid type or length");break;
            }
            struct target t;
            if(!target_get(s,index,&t)) {ok=false;break;}
            if(t.patch_slot!=NO_PATCH) {
                ok=fail(s,"Duplicate published legacy candidate name");break;
            }
            t.patch_slot=0;
            if(!target_put(s,index,&t)) {ok=false;break;}
            ++candidates;
        }
        snprintf(s->status->message,sizeof(s->status->message),
            "Indexing legacy repair candidates; %u found",candidates);
        update(s,false);
    }
    if(f_closedir(&dir)!=FR_OK)ok=fail(s,"Legacy candidate directory close failed");
    if(!ok || stop(s))return false;
    if(!sync_file(s,&s->index))return false;
    s->legacy_indexed=true;
    s->ops->log("Salvage legacy index: entries=%" PRIu64 " candidates=%u; one directory scan",
        s->status->sd_done,candidates);
    saved_check_end(s);return !stop(s);
}
static uint64_t journal_limit(struct salvage *s) {
    /* At most TARGET+CANDIDATE+REPAIRED per sector, one CHUNK per sector
     * (legacy producers may use short chunks), and bounded track/final records.
     * This scales to every admitted disc sector, rather than a fixed hole cap. */
    return (s->plan->bytes/KUI_RAW_BYTES*4u+s->plan->count+4u)*EVENT_BYTES;
}
static bool exact_file(struct salvage *s,const char *name,void *data,unsigned bytes) {
    path(s,name);FIL file;if(f_open(&file,s->path,FA_READ)!=FR_OK)return fail(s,"Required salvage metadata is missing");
    bool ok=f_size(&file)==bytes;if(!ok)fail(s,"Salvage metadata length is invalid");
    if(ok)ok=read_exact(s,&file,data,bytes);
    return io_close(s,&file,ok);
}
static bool write_new(struct salvage *s,const char *name,const void *data,unsigned bytes) {
    path(s,name);FIL file;if(f_open(&file,s->path,FA_WRITE|FA_CREATE_NEW)!=FR_OK)return fail(s,"Cannot create unique salvage metadata");
    bool ok=write_exact(s,&file,data,bytes) && sync_file(s,&file);return io_close(s,&file,ok);
}
static bool plan_valid(const struct kui_capture_plan *p) {
    if(!p || !p->count || p->count>99 || (p->profile!=KUI_CAPTURE_PROFILE_GD && p->profile!=KUI_CAPTURE_PROFILE_CD_TOC))return false;
    uint64_t bytes=0;
    for(unsigned i=0;i<p->count;++i) {
        const struct kui_capture_track *t=&p->tracks[i];
        if(t->number!=i+1 || (t->control!=0 && t->control!=4) || t->session>1 || t->start<150 || t->end<=t->start ||
           t->toc_end<t->end || t->toc_end>KUI_RECOVERY_FAD_MAX+1u ||
           t->end>KUI_RECOVERY_FAD_MAX+1u || (i && t->start<p->tracks[i-1].end))return false;
        if(p->profile==KUI_CAPTURE_PROFILE_CD_TOC && (t->session || t->toc_end!=t->end || (i && t->start!=p->tracks[i-1].end)))return false;
        bytes+=(uint64_t)(t->end-t->start)*KUI_RAW_BYTES;
    }
    return bytes==p->bytes;
}
static bool validate_header(struct salvage *s);
static bool recover_drive(struct salvage *s) {
    if(!s->ops->recover || s->recovering || s->failed || stop(s))return !s->failed && !stop(s);
    s->recovering=true;struct read_progress interrupted=save_read_progress(s->status);
    snprintf(s->status->message,sizeof(s->status->message),"Safely reinitializing drive; checking saved disc identity");update(s,true);
    uint8_t *retained=NULL;
    if(s->identified) {retained=malloc(sizeof(s->data)+sizeof(s->scratch));if(!retained) {s->recovering=false;return fail(s,"Not enough memory to retain acquisition bytes during drive recovery");}
        memcpy(retained,s->data,sizeof(s->data));memcpy(retained+sizeof(s->data),s->scratch,sizeof(s->scratch));}
    bool ok=s->ops->recover(s->ops->ctx,s->plan);
    if(!ok && !stop(s))fail(s,"Drive recovery refused or layout changed; committed progress retained");
    if(ok && s->identified)ok=validate_header(s);
    if(retained) {memcpy(s->data,retained,sizeof(s->data));memcpy(s->scratch,retained+sizeof(s->data),sizeof(s->scratch));free(retained);}
    if(ok)restore_read_progress(s->status,&interrupted);
    else if(s->failed)s->status->read_state=KUI_SALVAGE_READ_FATAL;
    s->recovering=false;s->read_failures=0;update(s,false);return ok;
}
static unsigned sector_check(struct salvage *s,const uint8_t *raw,uint32_t fad) {
    return s->plan->profile==KUI_CAPTURE_PROFILE_CD_TOC?kui_recovery_cd_sector_check(raw,KUI_RAW_BYTES,fad):kui_recovery_sector_check(raw,KUI_RAW_BYTES,fad);
}
static bool retry_recovery(struct salvage *s) {
    if(s->status->trial_active)return true; /* Trial resets would confound profiles. */
    if(!s->status->collection_active && !s->recovering && ++s->read_failures>=8 && s->ops->recover && s->identified)
        return recover_drive(s);
    /* No automatic replay: the bounded caller retains its read budget. */
    return true;
}
static enum kui_read_result optical(struct salvage *s,uint32_t fad,unsigned count,uint8_t *data,
    unsigned attempt,unsigned limit,unsigned confirm,unsigned confirm_limit) {
    if(stop(s) || s->failed) {if(!s->failed)s->status->read_state=KUI_SALVAGE_READ_NONE;return KUI_READ_RETRY;}
    struct kui_salvage_status *o=s->status;
    ++o->attempts;bool changed=read_context(s,fad,count,attempt,limit,confirm,confirm_limit);
    /* Publish the exact blocking read before entering the adapter. Display
     * rendering has its own rate limit; this is only a progress snapshot. */
    update(s,changed);
    if(stop(s)) {--o->attempts;o->read_state=KUI_SALVAGE_READ_NONE;return KUI_READ_RETRY;}
    bool repairing=o->pass && !o->collection_active && !s->recovering;
    uint64_t before=repairing && s->ops->now_ms?s->ops->now_ms(s->ops->ctx):0;
    if(repairing) {if(count>1)++o->repair_batches;else ++o->repair_singles;}
    enum kui_read_result r=s->ops->read(s->ops->ctx,fad,count,data);
    if(repairing && s->ops->now_ms) {
        uint64_t after=s->ops->now_ms(s->ops->ctx);
        if(after>=before)o->repair_optical_ms+=after-before;
    }
    if(r==KUI_READ_FATAL) {fail(s,"Drive stopped; no placeholder or further reads");read_outcome(s,KUI_SALVAGE_READ_FATAL);}
    if(r==KUI_READ_OK)s->read_failures=0;
    else if(r==KUI_READ_RETRY) {
        read_outcome(s,KUI_SALVAGE_READ_RETRY);
        if(!retry_recovery(s))return KUI_READ_FATAL;
    }
    return r;
}
static bool candidate(struct salvage *s,unsigned track,uint32_t sector,bool audio_twice,unsigned attempt,unsigned limit) {
    uint32_t fad=s->plan->tracks[track].start+sector;
    bool audio=s->plan->tracks[track].control!=4 && audio_twice;
    s->status->track=track+1;
    enum kui_read_result r=optical(s,fad,1,s->data,attempt,limit,1,audio?2:1);
    bool good=(r==KUI_READ_OK || (r==KUI_READ_PARTIAL && s->plan->tracks[track].control==4)) &&
        !stop(s) && !s->failed;
    if(good && s->plan->tracks[track].control==4)good=sector_check(s,s->data,fad)==0;
    else if(good && audio) {
        r=optical(s,fad,1,s->scratch,attempt,limit,2,2);
        good=r==KUI_READ_OK && !stop(s) && !s->failed && !memcmp(s->data,s->scratch,KUI_RAW_BYTES);
    }
    if(good) {
        s->read_failures=0; /* Validated partial data is not a failed candidate. */
        if(s->status->pass && !s->recovering)++s->status->repair_single_accepted;
    }
    else if(r==KUI_READ_PARTIAL && !stop(s) && !s->failed && !retry_recovery(s))return false;
    if(!stop(s) && !s->failed)read_outcome(s,good?KUI_SALVAGE_READ_ACCEPTED:
        attempt==limit?KUI_SALVAGE_READ_DEFERRED:KUI_SALVAGE_READ_RETRY);
    return good;
}
/* Discovery probes one prospective location, then searches elsewhere. Saved
 * identity instead retries its exact location; never substitute another anchor
 * for an existing job. A valid different payload means a different disc.
 * An audio probe still needs its pair of matching reads. */
static bool anchor_read(struct salvage *s,unsigned track,uint32_t fad,const uint8_t *expected,bool *different) {
    unsigned retries=0;
    if(expected) {
        retries=s->ops->options && s->ops->options->retry_limit?s->ops->options->retry_limit:KUI_CAPTURE_RETRIES;
        if(retries>50)retries=50; /* Foreign options cannot create unbounded identity reads. */
    }
    *different=false;
    for(unsigned attempt=0;attempt<=retries && !stop(s) && !s->failed;++attempt) {
        if(!candidate(s,track,fad-s->plan->tracks[track].start,true,attempt+1,retries+1))continue;
        uint8_t digest[32];struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,s->data,KUI_RAW_BYTES);kui_sha256_digest(&hash,digest);
        if(expected && memcmp(digest,expected,32)) {*different=true;return false;}
        return true;
    }
    return false;
}
static unsigned header_version(struct salvage *s) {
    const uint8_t *b=s->header;unsigned v=get32(b+8);
    if(v<1 || v>3 || memcmp(b,v==3?"KUISALV3":v==2?"KUISALV2":"KUISALV1",8))return 0;
    if(v==3) {if(s->plan->profile!=KUI_CAPTURE_PROFILE_CD_TOC || get32(b+PROFILE_OFFSET)!=KUI_CAPTURE_PROFILE_CD_TOC)return 0;}
    else if(s->plan->profile!=KUI_CAPTURE_PROFILE_GD)return 0;
    return v;
}
static int anchor_track(struct salvage *s,uint32_t fad) {
    for(unsigned i=0;i<s->plan->count;++i) {
        const struct kui_capture_track *t=&s->plan->tracks[i];
        if((s->plan->profile==KUI_CAPTURE_PROFILE_CD_TOC || (t->control==4 && t->session==1)) && fad>=t->start && fad<t->end)return (int)i;
    }
    return -1;
}
static bool make_header(struct salvage *s) {
    memset(s->header,0,HEADER_BYTES);bool cd=s->plan->profile==KUI_CAPTURE_PROFILE_CD_TOC;
    memcpy(s->header,cd?"KUISALV3":"KUISALV2",8);s->packed=true;
    snprintf(s->status->message,sizeof(s->status->message),"Finding healthy disc identity sectors");update(s,true);
    put32(s->header+8,cd?3u:2u);if(cd)put32(s->header+PROFILE_OFFSET,KUI_CAPTURE_PROFILE_CD_TOC);put32(s->header+12,HEADER_BYTES);put32(s->header+16,s->plan->count);
    /* Salvage always records unresolved placeholders. Keep the original
     * header layout so existing jobs retain their immutable identity. */
    put32(s->header+20,1);
    for(unsigned i=0;i<s->plan->count;++i) {
        const struct kui_capture_track *t=&s->plan->tracks[i];uint8_t *b=s->header+96+i*24;
        put32(b,t->number);put32(b+4,t->control);put32(b+8,t->session);put32(b+12,t->start);put32(b+16,t->end);put32(b+20,t->toc_end);
    }
    /* Two distinct healthy high-density data anchors bind the complete TOC to disc bytes.
     * Find bounded alternatives when a starting sector itself is damaged. */
    uint32_t chosen[2]={0,0};unsigned found=0;
    for(unsigned a=0;a<2;++a) {
        bool accepted=false;
        for(unsigned i=0;i<s->plan->count && !accepted;++i) {
            const struct kui_capture_track *t=&s->plan->tracks[i];if(!cd && (t->control!=4 || t->session!=1))continue;
            uint32_t count=t->end-t->start;
            for(unsigned n=0;n<128 && n<count;++n) {
                uint32_t sector;
                if(n<16)sector=a?count-1-n:n;
                else if(n<48) {sector=(uint32_t)((uint64_t)(n-16)*count/32u);if(a)sector=count-1-sector;}
                else if(n<80)sector=(count/2u+(n-48))%count;
                else sector=(uint32_t)((uint64_t)n*2654435761u%count);
                uint32_t fad=t->start+sector;
                if(fad==chosen[0])continue;
                if(stop(s) || s->failed)return false;
                bool different;if(!anchor_read(s,i,fad,NULL,&different))continue;
                struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,s->data,KUI_RAW_BYTES);
                uint8_t digest[32];kui_sha256_digest(&hash,digest);
                if(cd && t->control==0) {
                    bool varying=false;for(unsigned at=2;at<KUI_RAW_BYTES;++at)if(s->data[at]!=s->data[at%2]) {varying=true;break;}
                    if(!varying || (a && !memcmp(digest,s->header+32,32)))continue;
                }
                memcpy(s->header+32+a*32,digest,32);put32(s->header+24+a*4,fad);
                chosen[a]=fad;++found;accepted=true;break;
            }
        }
        if(!accepted)break; /* No first anchor means no usable two-anchor header. */
    }
    if(found!=2)return fail(s,"Cannot establish two stable disc identity anchors; no salvage files created");
    put32(s->header+HEADER_BYTES-4,kui_crc32(0,s->header,HEADER_BYTES-4));
    return true;
}
static bool validate_header(struct salvage *s) {
    const uint8_t *b=s->header;
    unsigned version=header_version(s);s->packed=version>=2;
    if(!version || get32(b+12)!=HEADER_BYTES || get32(b+16)!=s->plan->count ||
       get32(b+20)>1 || get32(b+HEADER_BYTES-4)!=kui_crc32(0,b,HEADER_BYTES-4))return fail(s,"Invalid salvage header/profile; original metadata retained");
    for(unsigned i=0;i<s->plan->count;++i) {
        const uint8_t *p=b+96+i*24;const struct kui_capture_track *t=&s->plan->tracks[i];
        if(get32(p)!=t->number || get32(p+4)!=t->control || get32(p+8)!=t->session || get32(p+12)!=t->start ||
           get32(p+16)!=t->end || get32(p+20)!=t->toc_end)return fail(s,"Inserted disc TOC does not match this salvage job");
    }
    for(unsigned i=96+s->plan->count*24;i<HEADER_BYTES-4;++i)
        if(!(version==3 && i>=PROFILE_OFFSET && i<PROFILE_OFFSET+4) && b[i])return fail(s,"Unknown salvage header fields");
    if(get32(b+24)==get32(b+28) || (version==3 && !memcmp(b+32,b+64,32)))return fail(s,"Salvage identity anchors are not distinct");
    for(unsigned a=0;a<2;++a) {
        uint32_t fad=get32(b+24+a*4);int track=anchor_track(s,fad);bool different;
        if(track<0)return fail(s,"Invalid salvage identity anchor");
        if(!anchor_read(s,(unsigned)track,fad,b+32+a*32,&different)) {
            if(stop(s) || s->failed)return false;
            return fail(s,different?"Inserted disc bytes do not match this salvage job":"Disc identity anchor remains unreadable after bounded retries; no saved data changed");
        }
    }
    return true;
}
static void identity(struct salvage *s) {struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,s->header,HEADER_BYTES);kui_sha256_digest(&hash,s->identity);s->identified=true;}
static bool make_job(struct salvage *s) {
    const char *parents[]={"0:/KUI","0:/KUI/salvage"};
    for(unsigned i=0;i<2;++i) {FRESULT r=f_mkdir(parents[i]);if(r!=FR_OK && r!=FR_EXIST)return fail(s,"Cannot create salvage parent folder");}
    for(unsigned i=1;i<=9999;++i) {
        if(stop(s))return false;
        snprintf(s->status->job,sizeof(s->status->job),"/KUI/salvage/job-%04u",i);
        snprintf(s->path,sizeof(s->path),"0:%s",s->status->job);
        FRESULT r=f_mkdir(s->path);if(r==FR_EXIST)continue;
        if(r!=FR_OK)return fail(s,"Cannot create a new salvage job");
        if(!write_new(s,"header.bin",s->header,HEADER_BYTES) || !exact_file(s,"header.bin",s->data,HEADER_BYTES))return false;
        if(memcmp(s->header,s->data,HEADER_BYTES))return fail(s,"Salvage identity header readback mismatch");
        return true;
    }
    return fail(s,"All salvage job numbers are occupied");
}
static bool job_path(struct salvage *s,const char *job) {
    if(!job)return fail(s,"Select an existing salvage job");
    if(!strncmp(job,"0:",2))job+=2;
    if(strlen(job)!=21 || strncmp(job,"/KUI/salvage/job-",17))return fail(s,"Select an exact /KUI/salvage/job-NNNN folder");
    for(unsigned i=17;i<21;++i)if(job[i]<'0'||job[i]>'9')return fail(s,"Invalid salvage job number");
    if(!strcmp(job+17,"0000"))return fail(s,"Invalid salvage job number");
    snprintf(s->status->job,sizeof(s->status->job),"%s",job);return true;
}
static bool append(struct salvage *s,unsigned type,uint32_t track,uint32_t sector,uint32_t count,uint32_t crc) {
    if(s->sequence>=journal_limit(s)/EVENT_BYTES)return fail(s,"Salvage journal limit reached");
    memset(s->event,0,EVENT_BYTES);memcpy(s->event,"KUISJNL1",8);
    put64(s->event+8,s->sequence+1);put32(s->event+16,type);put32(s->event+20,track);
    put32(s->event+24,sector);put32(s->event+28,count);put32(s->event+32,crc);
    memcpy(s->event+40,s->identity,32);put32(s->event+508,kui_crc32(0,s->event,508));
    uint32_t expected=get32(s->event+508);
    if(!write_exact(s,&s->journal,s->event,EVENT_BYTES) || !sync_file(s,&s->journal) ||
       !seek(s,&s->journal,s->sequence*EVENT_BYTES) || !read_exact(s,&s->journal,s->scratch,EVENT_BYTES))return false;
    if(memcmp(s->scratch,s->event,EVENT_BYTES) || get32(s->scratch+508)!=expected ||
       kui_crc32(0,s->scratch,508)!=expected)return fail(s,"Salvage journal readback mismatch");
    ++s->sequence;return true;
}
static unsigned current_track(struct salvage *s) {unsigned i=0;while(i<s->plan->count && s->baselined[i])++i;return i;}
static bool replay_event(struct salvage *s) {
    uint8_t *e=s->event;unsigned type=get32(e+16),track=get32(e+20);uint32_t sector=get32(e+24),count=get32(e+28),crc=get32(e+32);
    if(get64(e+8)!=s->sequence+1 || memcmp(e+40,s->identity,32) || get32(e+36))return false;
    for(unsigned i=72;i<508;++i)if(e[i])return false;
    if(s->completed)return false;
    unsigned active=current_track(s);
    if(type==EV_TARGET) {
        /* The old header flag described an abort-on-error preference, not
         * sector evidence. Resuming that job now permits durable holes too. */
        if(s->ready || track!=active || track>=s->plan->count || count || crc ||
           s->pending_count==CHUNK || s->target_count+s->pending_count>=s->plan->bytes/KUI_RAW_BYTES ||
           sector<s->sectors[track] || sector>=s->sectors[track]+CHUNK || sector>=s->plan->tracks[track].end-s->plan->tracks[track].start ||
           (s->pending_count && sector<=s->pending[s->pending_count-1]))return false;
        if(!s->pending_count)s->pending_offset=s->sequence*EVENT_BYTES;
        s->pending[s->pending_count++]=sector;
    } else if(type==EV_CHUNK) {
        if(s->ready || track!=active || track>=s->plan->count || sector!=s->sectors[track] || !count || count>CHUNK ||
           count>s->plan->tracks[track].end-s->plan->tracks[track].start-sector)return false;
        for(unsigned i=0;i<s->pending_count;++i) {
            if(s->pending[i]>=sector+count)return false;
            struct target t={s->pending[i],0,track,NO_PATCH,false};
            if(!target_put(s,s->target_count,&t))return false;
            ++s->target_count;
        }
        s->pending_count=0;s->sectors[track]+=count;s->crc[track]=crc;
    } else if(type==EV_BASELINE) {
        if(s->ready || s->pending_count || track!=active || track>=s->plan->count || sector || count ||
           s->sectors[track]!=s->plan->tracks[track].end-s->plan->tracks[track].start || crc!=s->crc[track])return false;
        s->baseline[track]=crc;s->baselined[track]=true;
    } else if(type==EV_READY) {
        if(s->ready || s->pending_count || active!=s->plan->count || track || sector || count!=s->target_count || crc)return false;
        s->ready=true;
    } else if(type==EV_CANDIDATE) {
        struct target t;
        if(!s->packed || !s->ready || track>=s->target_count || sector!=s->patch_count || count || !target_get(s,track,&t) ||
           t.repaired || t.patch_slot!=NO_PATCH)return false;
        t.patch_slot=s->patch_count++;t.crc=crc;
        if(!target_put(s,track,&t))return false;
    } else if(type==EV_REPAIRED) {
        struct target t;
        if(!s->ready || track>=s->target_count || sector || count || !target_get(s,track,&t) || t.repaired ||
           (s->packed && (t.patch_slot==NO_PATCH || t.crc!=crc)))return false;
        t.repaired=true;t.crc=crc;++s->recovered_count;
        if(!target_put(s,track,&t))return false;
    } else if(type==EV_COMPLETE) {
        if(!s->ready || track || sector || count!=s->target_count || crc)return false;
        if(s->recovered_count!=s->target_count)return false;
        s->completed=true;
    } else return false;
    ++s->sequence;return true;
}
static bool replay(struct salvage *s) {
    FSIZE_t length=f_size(&s->journal);
    if(length>journal_limit(s))return fail(s,"Salvage journal is oversized");
    saved_check_start(s,KUI_SALVAGE_SD_JOURNAL,
        (uint64_t)length/EVENT_BYTES+(length%EVENT_BYTES!=0),"Rebuilding saved progress from journal");
    uint64_t valid=0;
    while(length-valid>=EVENT_BYTES) {
        if(stop(s))return false;
        if(!read_exact(s,&s->journal,s->event,EVENT_BYTES))return false;
        bool intact=!memcmp(s->event,"KUISJNL1",8) && get32(s->event+508)==kui_crc32(0,s->event,508);
        if(!intact) {
            if(length-valid>EVENT_BYTES)return fail(s,"Salvage journal corruption before its final record");
            break;
        }
        if(!replay_event(s))return fail(s,"Salvage journal has invalid state transitions");
        valid+=EVENT_BYTES;s->status->sd_done=valid/EVENT_BYTES;update(s,false);
    }
    if(s->pending_count) {valid=s->pending_offset;s->sequence=valid/EVENT_BYTES;s->pending_count=0;}
    /* No committed progress depends on bytes after this boundary. All optical
     * anchors were checked before journal truncation or any track mutation. */
    if(!seek(s,&s->journal,valid))return false;
    if(valid!=length && (f_truncate(&s->journal)!=FR_OK || !sync_file(s,&s->journal)))return fail(s,"Cannot discard uncommitted journal tail");
    s->status->sd_done=s->status->sd_total;saved_check_end(s);
    return true;
}
static bool open_journal(struct salvage *s,bool fresh) {
    path(s,"journal.bin");FRESULT opened=f_open(&s->journal,s->path,FA_READ|FA_WRITE|(fresh?FA_CREATE_NEW:FA_OPEN_EXISTING));
    if(!fresh && opened==FR_NO_FILE) {
        /* A stop after durable header publication but before journal creation
         * has no committed payload. Recreate ONLY when no track exists. */
        for(unsigned i=0;i<s->plan->count;++i) {
            track_path(s,i);FILINFO info;FRESULT r=f_stat(s->path,&info);
            if(r!=FR_NO_FILE)return fail(s,"Missing journal beside track data; refusing to invent progress");
        }
        path(s,"journal.bin");opened=f_open(&s->journal,s->path,FA_READ|FA_WRITE|FA_CREATE_NEW);fresh=true;
    }
    if(opened!=FR_OK)return fail(s,"Cannot open salvage journal");
    s->journal_open=true;
    path(s,"targets.cache");
    if(f_open(&s->index,s->path,FA_READ|FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)return fail(s,"Cannot rebuild derived target index");
    s->index_open=true;
    return fresh?sync_file(s,&s->journal):replay(s);
}
static bool check_lengths(struct salvage *s) {
    for(unsigned i=0;i<s->plan->count;++i) {
        track_path(s,i);FILINFO info;FRESULT r=f_stat(s->path,&info);
        if(!s->sectors[i] && r==FR_NO_FILE)continue;
        if(r!=FR_OK || (info.fattrib&AM_DIR) || info.fsize<(uint64_t)s->sectors[i]*KUI_RAW_BYTES ||
           (s->baselined[i] && info.fsize!=(uint64_t)s->sectors[i]*KUI_RAW_BYTES))return fail(s,"Saved salvage track length disagrees with committed data");
    }
    return true;
}
/* Collection maps each range with one probe. Released partial data buffers are
 * untrusted until each sector passes the complete data checks; partial audio
 * has no such independent acceptance test. No subdivision or local retries.
 * Individual recovery starts after all planned ranges are collected. */
static bool collect_region(struct salvage *s,unsigned ti,uint32_t begin,unsigned count,bool bad[CHUNK]) {
    const struct kui_capture_track *t=&s->plan->tracks[ti];
    enum kui_read_result r=optical(s,t->start+begin,count,s->data,1,1,1,1);
    if(s->failed || stop(s))return false;
    unsigned accepted=0,flags=0;uint32_t invalid_fad=0;uint8_t invalid_header[4]={0};
    bool eligible=r==KUI_READ_OK || (r==KUI_READ_PARTIAL && t->control==4);
    for(unsigned n=0;n<count;++n) {
        if(stop(s))return false;
        uint8_t *raw=s->data+n*KUI_RAW_BYTES;uint32_t fad=t->start+begin+n;
        unsigned check=eligible && t->control==4?sector_check(s,raw,fad):0;
        bool good=eligible && !check;
        bad[n]=!good;
        if(good)++accepted;
        else {
            if(check && !invalid_fad) {invalid_fad=fad;memcpy(invalid_header,raw+12,sizeof(invalid_header));}
            flags|=check;memset(raw,0,KUI_RAW_BYTES);
        }
    }
    if(r!=KUI_READ_OK || accepted!=count) {
        ++s->collection_issue_ranges;
        if(s->collection_issue_ranges==1 || !(s->collection_issue_ranges%64u)) {
            s->ops->log("Salvage collection: FAD=%" PRIu32 " sectors=%u transport=%s accepted=%u unverified=%u flags=%02x",
                t->start+begin,count,r==KUI_READ_OK?"ok":r==KUI_READ_PARTIAL?"partial":r==KUI_READ_RETRY?"retry":"unknown",
                accepted,count-accepted,flags);
            if(invalid_fad)s->ops->log("  first invalid FAD=%" PRIu32 " raw_header=%02x:%02x:%02x mode=%02x",
                invalid_fad,(unsigned)invalid_header[0],(unsigned)invalid_header[1],(unsigned)invalid_header[2],(unsigned)invalid_header[3]);
        }
    }
    if(accepted) {s->collection_bad_ranges=0;s->collection_health_at=8;}
    else ++s->collection_bad_ranges;
    read_outcome(s,accepted!=count?KUI_SALVAGE_READ_DEFERRED:KUI_SALVAGE_READ_ACCEPTED);
    return true;
}
/* Called only between committed collection ranges. Anchor reads may overwrite
 * the acquisition buffers here: no accepted bytes are awaiting publication.
 * An exact saved anchor demonstrates that the drive can still return checked
 * data. A valid different payload fails closed; unreadable probes alone do not
 * prove the remaining disc is damaged and get one guarded recovery attempt. */
static bool collection_health(struct salvage *s) {
    struct read_progress interrupted=save_read_progress(s->status);unsigned matched=0;
    snprintf(s->status->message,sizeof(s->status->message),"Collection health check; verifying saved disc anchors");update(s,true);
    if(stop(s) || s->failed)return false;
    s->ops->log("Salvage collection: health check after %u wholly unverified ranges",s->collection_bad_ranges);
    s->recovering=true;
    for(unsigned a=0;a<2;++a) {
        uint32_t fad=get32(s->header+24+a*4);int track=anchor_track(s,fad);
        if(track<0) {s->recovering=false;return fail(s,"Invalid salvage identity anchor during collection health check");}
        if(!candidate(s,(unsigned)track,fad-s->plan->tracks[track].start,true,1,1)) {
            if(stop(s) || s->failed) {s->recovering=false;return false;}
            continue;
        }
        struct kui_sha256 hash;uint8_t digest[32];
        kui_sha256_init(&hash);kui_sha256_update(&hash,s->data,KUI_RAW_BYTES);kui_sha256_digest(&hash,digest);
        if(memcmp(digest,s->header+32+a*32,sizeof(digest))) {
            s->recovering=false;return fail(s,"Inserted disc bytes changed during collection; committed data retained");
        }
        ++matched;
    }
    s->recovering=false;
    if(!matched) {
        s->ops->log("Salvage collection: no saved anchor verified; attempting guarded drive recovery once");
        if(!s->ops->recover)return fail(s,"Collection stopped: saved anchors unverified and drive recovery unavailable");
        if(!recover_drive(s))return false;
    } else s->ops->log("Salvage collection: %u saved anchors verified; continuing scan without reinitialization",matched);
    if(stop(s) || s->failed)return false;
    restore_read_progress(s->status,&interrupted);
    s->collection_health_at=s->collection_bad_ranges+64u;
    update(s,false);return true;
}
static bool first_pass(struct salvage *s) {
    bool defer=s->options->defer_next_range;
    s->collection_health_at=8;
    for(unsigned ti=current_track(s);ti<s->plan->count;++ti) {
        const struct kui_capture_track *t=&s->plan->tracks[ti];uint32_t total=t->end-t->start;
        s->status->track=ti+1;track_path(s,ti);FIL file;
        if(f_open(&file,s->path,FA_READ|FA_WRITE|FA_OPEN_ALWAYS)!=FR_OK)return fail(s,"Cannot open salvage track for first pass");
        bool ok=seek(s,&file,(uint64_t)s->sectors[ti]*KUI_RAW_BYTES);
        if(ok && f_truncate(&file)!=FR_OK)ok=fail(s,"Cannot discard uncommitted track tail");
        while(ok && s->sectors[ti]<total) {
            if(stop(s)) {ok=false;break;}
            if(!defer && s->collection_bad_ranges>=s->collection_health_at && !collection_health(s)) {ok=false;break;}
            uint32_t begin=s->sectors[ti];unsigned count=total-begin;if(count>CHUNK)count=CHUNK;
            bool bad[CHUNK]={false};
            if(defer) {
                defer=false;memset(s->data,0,(size_t)count*KUI_RAW_BYTES);
                for(unsigned n=0;n<count;++n)bad[n]=true;
                read_context(s,t->start+begin,count,0,0,0,0);
                snprintf(s->status->message,sizeof(s->status->message),"Deferring next range by request; not recovered");
                s->ops->log("Salvage: explicitly deferring FAD=%" PRIu32 " sectors=%u without reading; remains unresolved",t->start+begin,count);
                read_outcome(s,KUI_SALVAGE_READ_DEFERRED);
            } else {
                snprintf(s->status->message,sizeof(s->status->message),"Collection scan; unverified sectors deferred to repair");
                if(!collect_region(s,ti,begin,count,bad)) {ok=false;break;}
            }
            if(stop(s)) {ok=false;break;}
            for(unsigned n=0;n<count && ok;++n)if(bad[n])ok=append(s,EV_TARGET,ti,begin+n,0,0);
            if(!ok)break;
            uint32_t crc=kui_crc32(s->crc[ti],s->data,count*KUI_RAW_BYTES);
            ok=write_exact(s,&file,s->data,count*KUI_RAW_BYTES) && sync_file(s,&file);
            if(ok && crc!=kui_crc32(s->crc[ti],s->data,count*KUI_RAW_BYTES))ok=fail(s,"Source buffer changed during salvage write; chunk not committed");
            if(ok)ok=append(s,EV_CHUNK,ti,begin,count,crc);
            if(!ok)break;
            for(unsigned n=0;n<count && ok;++n)if(bad[n]) {
                struct target target={begin+n,0,ti,NO_PATCH,false};
                ok=target_put(s,s->target_count,&target);if(ok)++s->target_count;
            }
            if(!ok)break;
            s->sectors[ti]+=count;s->crc[ti]=crc;
            snprintf(s->status->message,sizeof(s->status->message),"Collecting good data; %u unresolved sectors",s->target_count);update(s,false);
        }
        ok=io_close(s,&file,ok);if(!ok)return false;
        if(!append(s,EV_BASELINE,ti,0,0,s->crc[ti]))return false;
        s->baseline[ti]=s->crc[ti];s->baselined[ti]=true;update(s,true);
    }
    if(!append(s,EV_READY,0,0,s->target_count,0))return false;
    s->ready=true;update(s,true);return true;
}
static unsigned patch_bytes(struct salvage *s) {return s->packed?PACKED_PATCH_BYTES:PATCH_BYTES;}
static const char *patch_problem(struct salvage *s,unsigned index,const struct target *t) {
    const uint8_t *p=s->patch;unsigned bytes=patch_bytes(s);
    if(memcmp(p,s->packed?"KUISPAT2":"KUISPAT1",8))return "magic/version";
    if(get32(p+8)!=index)return "target index";
    if(get32(p+12)!=t->track)return "track";
    if(get32(p+16)!=t->sector)return "sector";
    if(get32(p+20)!=(s->plan->tracks[t->track].control==4?1u:2u))return "evidence type";
    if(memcmp(p+32,s->identity,32))return "job identity";
    if(get32(p+bytes-4)!=kui_crc32(0,p,bytes-4))return "record CRC";
    for(unsigned i=64;i<64+KUI_RAW_BYTES;++i)if(p[i])return "nonzero baseline";
    if(get32(p+24)!=kui_crc32(0,p+64,KUI_RAW_BYTES))return "baseline CRC";
    if(get32(p+28)!=kui_crc32(0,p+64+KUI_RAW_BYTES,KUI_RAW_BYTES))return "payload CRC";
    for(unsigned i=64+2*KUI_RAW_BYTES;i<bytes-4;++i)if(p[i])return "reserved bytes";
    if(s->plan->tracks[t->track].control==4 && sector_check(s,p+64+KUI_RAW_BYTES,s->plan->tracks[t->track].start+t->sector))return "sector validation";
    return NULL;
}
static void patch_report(struct salvage *s,unsigned index,const struct target *t,const char *phase,const char *problem) {
    const uint8_t *p=s->patch;unsigned bytes=patch_bytes(s);char expected[65],observed[65];
    kui_hex(s->identity,32,expected);kui_hex(p+32,32,observed);
    s->ops->log("Salvage candidate %s: target=%u slot=%" PRIu32 " offset=%" PRIu64 " FAD=%" PRIu32 " problem=%s",
        phase,index,t->patch_slot,(uint64_t)t->patch_slot*PACKED_PATCH_BYTES,
        s->plan->tracks[t->track].start+t->sector,problem?problem:"none");
    s->ops->log("Candidate fields expected index/track/sector/type=%u/%" PRIu32 "/%" PRIu32 "/%u got=%" PRIu32 "/%" PRIu32 "/%" PRIu32 "/%" PRIu32,
        index,t->track,t->sector,s->plan->tracks[t->track].control==4?1u:2u,
        get32(p+8),get32(p+12),get32(p+16),get32(p+20));
    s->ops->log("Candidate CRC record stored=%08" PRIx32 " read=%08" PRIx32 " payload stored=%08" PRIx32 " read=%08" PRIx32 " journal=%08" PRIx32,
        get32(p+bytes-4),kui_crc32(0,p,bytes-4),get32(p+28),kui_crc32(0,p+64+KUI_RAW_BYTES,KUI_RAW_BYTES),t->crc);
    s->ops->log("Candidate identity expected=%s got=%s; buffer alignment=%u",
        expected,observed,(unsigned)((uintptr_t)p&31u));
}
static bool patch_valid(struct salvage *s,unsigned index) {
    struct target target;if(!target_get(s,index,&target))return false;
    const char *problem=patch_problem(s,index,&target);
    if(problem) {
        patch_report(s,index,&target,"validation",problem);
        return fail(s,"Invalid published sector backup/candidate; refusing repair");
    }
    return true;
}
/* A successful I/O with invalid evidence is diagnosed once through a fresh
 * read-only handle and sector-sized, aligned reads. This is never an alternate
 * admission route: even a fully valid reread stops this invocation. Differing
 * saved bytes retain the existing full-repeat-scan quarantine requirement. */
static bool diagnose_packed_candidate(struct salvage *s,unsigned index,const struct target *t,const char *problem) {
    patch_report(s,index,t,"initial",problem);
    memcpy(s->repair_data,s->patch,PACKED_PATCH_BYTES);
    s->packed_open=false;s->packed_write=false;
    if(!io_close(s,&s->packed_file,true) || stop(s))return false;
    FIL *f=cached_packed(s,false);if(!f)return false;
    uint64_t offset=(uint64_t)t->patch_slot*PACKED_PATCH_BYTES;
    if(f_size(f)<offset+PACKED_PATCH_BYTES)return fail(s,"Published candidate file shortened during diagnostic reread");
    if(!seek(s,f,offset))return false;
    for(unsigned at=0;at<PACKED_PATCH_BYTES;at+=512u) {
        if(stop(s) || !read_exact(s,f,s->patch+at,512u))return false;
    }
    if(stop(s))return false;
    const char *again=patch_problem(s,index,t);
    if(!again && get32(s->patch+28)!=t->crc)again="journal candidate CRC";
    patch_report(s,index,t,"fresh sector reread",again);
    unsigned changed=0;
    while(changed<PACKED_PATCH_BYTES && s->patch[changed]==s->repair_data[changed])++changed;
    if(changed<PACKED_PATCH_BYTES) {
        s->ops->log("Saved candidate reads differ at record byte=%u first=%02x reread=%02x; repair remains stopped",
            changed,(unsigned)s->repair_data[changed],(unsigned)s->patch[changed]);
        if(!kui_recovery_guard_mark(s->status->job,s->ops->log))
            return fail(s,"Saved candidate reads differ; cannot save quarantine; preserve this job");
        return fail(s,"Saved candidate reads differ; repair blocked; preserve job and see Diagnostics");
    }
    return fail(s,"Invalid published sector backup/candidate; fresh reread agrees; refusing repair");
}
static bool reconcile_patch_tail(struct salvage *s) {
    if(!s->packed)return true;
    path(s,"patches.bin");FIL f;FRESULT r=f_open(&f,s->path,FA_READ|FA_WRITE);
    if(r==FR_NO_FILE)return s->patch_count?fail(s,"Published packed candidates are missing"):true;
    if(r!=FR_OK)return fail(s,"Cannot open packed candidates");
    uint64_t wanted=(uint64_t)s->patch_count*PACKED_PATCH_BYTES;
    bool ok=f_size(&f)>=wanted;
    if(!ok)fail(s,"Packed candidate file is shorter than its published journal");
    if(ok && f_size(&f)!=wanted)ok=seek(s,&f,wanted) && f_truncate(&f)==FR_OK && sync_file(s,&f);
    if(!ok && !s->failed)fail(s,"Cannot discard unpublished packed candidate tail");
    return io_close(s,&f,ok);
}
static bool load_patch(struct salvage *s,unsigned index,bool *exists) {
    struct target t;if(!target_get(s,index,&t))return false;
    if(s->packed) {
        *exists=t.patch_slot!=NO_PATCH;if(!*exists)return true;
        FIL *f=cached_packed(s,false);if(!f)return false;
        bool ok=seek(s,f,(uint64_t)t.patch_slot*PACKED_PATCH_BYTES) && read_exact(s,f,s->patch,PACKED_PATCH_BYTES);
        if(ok) {
            struct target checked;if(!target_get(s,index,&checked))return false;
            if(checked.track!=t.track || checked.sector!=t.sector || checked.patch_slot!=t.patch_slot ||
               checked.crc!=t.crc || checked.repaired!=t.repaired)
                return fail(s,"Derived candidate target changed during saved read");
            const char *problem=patch_problem(s,index,&checked);
            if(!problem && get32(s->patch+28)!=checked.crc)problem="journal candidate CRC";
            if(problem)return diagnose_packed_candidate(s,index,&checked,problem);
        }
        if(!ok && !s->failed)fail(s,"Packed candidate differs from its durable publication");
        return ok;
    }
    char name[32];snprintf(name,sizeof(name),"patch-%04u.bin",index);path(s,name);
    if(s->legacy_indexed) {
        *exists=t.patch_slot!=NO_PATCH;
        if(!*exists)return true;
        return exact_file(s,name,s->patch,PATCH_BYTES) && patch_valid(s,index);
    }
    FILINFO info;FRESULT r=f_stat(s->path,&info);*exists=r==FR_OK;
    if(r==FR_NO_FILE)return true;
    if(r!=FR_OK)return fail(s,"Cannot inspect repair candidate");
    return exact_file(s,name,s->patch,PATCH_BYTES) && patch_valid(s,index);
}
static bool publish_patch(struct salvage *s,unsigned index) {
    struct target target;if(!target_get(s,index,&target))return false;
    struct target *t=&target;unsigned bytes=patch_bytes(s);
    memset(s->patch,0,bytes);memcpy(s->patch,s->packed?"KUISPAT2":"KUISPAT1",8);put32(s->patch+8,index);put32(s->patch+12,t->track);
    put32(s->patch+16,t->sector);put32(s->patch+20,s->plan->tracks[t->track].control==4?1:2);memcpy(s->patch+32,s->identity,32);
    memcpy(s->patch+64,s->scratch,KUI_RAW_BYTES);memcpy(s->patch+64+KUI_RAW_BYTES,s->data,KUI_RAW_BYTES);
    put32(s->patch+24,kui_crc32(0,s->patch+64,KUI_RAW_BYTES));put32(s->patch+28,kui_crc32(0,s->data,KUI_RAW_BYTES));
    put32(s->patch+bytes-4,kui_crc32(0,s->patch,bytes-4));
    if(s->packed) {
        FIL *f=cached_packed(s,true);if(!f)return false;
        uint64_t offset=(uint64_t)s->patch_count*PACKED_PATCH_BYTES;
        bool ok=f_size(f)==offset;
        if(!ok)fail(s,"Unpublished packed candidate tail was not reconciled");
        if(ok)ok=seek(s,f,offset) && write_exact(s,f,s->patch,bytes) && sync_file(s,f) && seek(s,f,offset) && read_exact(s,f,s->patch,bytes) && patch_valid(s,index);
        if(ok && memcmp(s->patch+64+KUI_RAW_BYTES,s->data,KUI_RAW_BYTES))ok=fail(s,"Stored packed candidate differs from optical bytes");
        if(!ok)return false;
        uint32_t crc=get32(s->patch+28);
        if(!append(s,EV_CANDIDATE,index,s->patch_count,0,crc))return false;
        t->patch_slot=s->patch_count++;t->crc=crc;
        return target_put(s,index,t);
    }
    char temporary[32],name[32],oldpath[KUI_DEST_PATH_CAP];
    snprintf(temporary,sizeof(temporary),"patch-%04u.part",index);snprintf(name,sizeof(name),"patch-%04u.bin",index);
    path(s,temporary);FRESULT r=f_unlink(s->path);if(r!=FR_OK && r!=FR_NO_FILE)return fail(s,"Cannot discard unpublished repair candidate");
    if(!write_new(s,temporary,s->patch,PATCH_BYTES) || !exact_file(s,temporary,s->patch,PATCH_BYTES) || !patch_valid(s,index))return false;
    if(memcmp(s->patch+64+KUI_RAW_BYTES,s->data,KUI_RAW_BYTES))return fail(s,"Stored repair candidate differs from the optical candidate");
    path(s,temporary);snprintf(oldpath,sizeof(oldpath),"%s",s->path);path(s,name);
    if(f_rename(oldpath,s->path)!=FR_OK)return fail(s,"Cannot publish verified repair candidate");
    if(!exact_file(s,name,s->patch,PATCH_BYTES) || !patch_valid(s,index))return false;
    if(memcmp(s->patch+64+KUI_RAW_BYTES,s->data,KUI_RAW_BYTES))return fail(s,"Published repair candidate changed");
    if(s->legacy_indexed) {t->patch_slot=0;if(!target_put(s,index,t))return false;}
    return true;
}
static bool read_target(struct salvage *s,unsigned index) {
    struct target t;if(!target_get(s,index,&t))return false;FIL *file=cached_track(s,t.track,false);
    return file && seek(s,file,(uint64_t)t.sector*KUI_RAW_BYTES) && read_exact(s,file,s->scratch,KUI_RAW_BYTES);
}
static bool apply_patch(struct salvage *s,unsigned index) {
    struct target t;if(!target_get(s,index,&t))return false;FIL *file=cached_track(s,t.track,true);if(!file)return false;
    uint64_t offset=(uint64_t)t.sector*KUI_RAW_BYTES;const uint8_t *replacement=s->patch+64+KUI_RAW_BYTES;
    uint32_t expected=get32(s->patch+28);
    if(kui_crc32(0,replacement,KUI_RAW_BYTES)!=expected)return fail(s,"Repair buffer changed before target write");
    bool ok=seek(s,file,offset) && write_exact(s,file,replacement,KUI_RAW_BYTES) && sync_file(s,file) &&
        seek(s,file,offset) && read_exact(s,file,s->scratch,KUI_RAW_BYTES);
    if(ok && (memcmp(s->scratch,replacement,KUI_RAW_BYTES) || kui_crc32(0,replacement,KUI_RAW_BYTES)!=expected ||
       kui_crc32(0,s->scratch,KUI_RAW_BYTES)!=expected))ok=fail(s,"Repair readback/buffer mismatch; durable candidate retained");
    if(!ok)return false;
    uint32_t crc=expected;if(!append(s,EV_REPAIRED,index,0,0,crc))return false;
    t.crc=crc;t.repaired=true;++s->recovered_count;
    if(!target_put(s,index,&t))return false;
    update(s,true);return true;
}
/* Adjacent saved targets share one bounded track read. The existing acquisition
 * buffer is idle until reconciliation finishes; patch loading/application uses
 * separate patch/scratch storage. Per-slot checksums detect later callbacks or
 * candidate replay changing a still-unchecked saved slot in that buffer. */
static bool reconcile(struct salvage *s) {
    if(!s->ready)return true;
    /* Commit the disposable index allocation before a read fault can poison
     * storage and prevent normal close from publishing its length. */
    if(!sync_file(s,&s->index) || !index_legacy_patches(s))return false;
    saved_check_start(s,KUI_SALVAGE_SD_TARGETS,s->target_count,
        "Checking durable targets and interrupted patches");
    for(unsigned i=0;i<s->target_count;) {
        if(stop(s))return false;
        struct target targets[CHUNK];uint32_t staged_crc[CHUNK];unsigned count=1;
        if(!target_get(s,i,&targets[0]))return false;
        while(count<CHUNK && count<s->target_count-i) {
            if(stop(s))return false;
            struct target next;
            if(!target_get(s,i+count,&next))return false;
            const struct target *previous=&targets[count-1];
            if(next.track!=previous->track || next.sector!=previous->sector+1u)break;
            targets[count++]=next;
        }
        s->status->track=targets[0].track+1;
        s->status->fad=s->plan->tracks[targets[0].track].start+targets[0].sector;
        update(s,false);
        if(stop(s))return false;
        FIL *file=cached_track(s,targets[0].track,false);if(!file)return false;
        uint8_t *window=(uint8_t *)(((uintptr_t)s->data+31u)&~(uintptr_t)31u);
        uint64_t offset=(uint64_t)targets[0].sector*KUI_RAW_BYTES;
        uint64_t begin=offset&~(uint64_t)511u,length=f_size(file);
        unsigned head=(unsigned)(offset-begin),raw_bytes=count*KUI_RAW_BYTES;
        unsigned bytes=(head+raw_bytes+511u)&~511u;
        if(begin>length || length-begin<(uint64_t)head+raw_bytes)
            return fail(s,"Saved target window exceeds the committed track");
        if(length-begin<bytes)bytes=(unsigned)(length-begin);
        /* Starting at a physical SD boundary keeps FatFs' large direct reads
         * aligned for SCI DMA even when the raw 2352-byte slot starts midway
         * through a 512-byte sector. The final partial file tail is bounded. */
        if(!seek(s,file,begin) || !read_exact(s,file,window,bytes))return false;
        if(stop(s))return false;
        const uint8_t *saved_data=window+head;
        for(unsigned n=0;n<count;++n)
            staged_crc[n]=kui_crc32_capture(0,saved_data+n*KUI_RAW_BYTES,KUI_RAW_BYTES);
        for(unsigned n=0;n<count;++n) {
            if(stop(s))return false;
            unsigned index=i+n;const struct target *t=&targets[n];bool exists;
            const uint8_t *saved=saved_data+n*KUI_RAW_BYTES;
            s->status->track=t->track+1;s->status->fad=s->plan->tracks[t->track].start+t->sector;
            update(s,false);
            if(stop(s))return false;
            /* Packed slots come from the journal; legacy presence comes from
             * the completed directory scan. Both are checksummed snapshots. */
            exists=false;
            if(t->patch_slot!=NO_PATCH && !load_patch(s,index,&exists))return false;
            if(kui_crc32_capture(0,saved,KUI_RAW_BYTES)!=staged_crc[n])
                return fail(s,"Staged saved-target data changed during reconciliation");
            if(t->repaired) {
                if(!exists || t->crc!=get32(s->patch+28) || memcmp(saved,s->patch+64+KUI_RAW_BYTES,KUI_RAW_BYTES))
                    return fail(s,"Previously repaired sector or its backup changed; no complete result");
            } else if(exists) {
                if(stop(s) || !apply_patch(s,index))return false;
            } else {
                for(unsigned b=0;b<KUI_RAW_BYTES;++b)if(saved[b])
                    return fail(s,"Unresolved placeholder changed without a durable repair candidate");
            }
            s->status->sd_done=index+1;update(s,false);
        }
        i+=count;
    }
    saved_check_end(s);
    snprintf(s->status->message,sizeof(s->status->message),"Saved-data checks complete; preparing disc repair");
    return true;
}

/* Publication remains one durable target at a time. A checked optical
 * candidate must survive the placeholder read before it becomes evidence. */
static bool store_repair(struct salvage *s,unsigned index,const struct target *t,uint32_t expected) {
    if(stop(s) || s->failed)return false;
    if(!read_target(s,index))return false;
    for(unsigned j=0;j<KUI_RAW_BYTES;++j)if(s->scratch[j])return fail(s,"Placeholder changed before patch publication");
    if(stop(s) || s->failed)return false;
    uint32_t fad=s->plan->tracks[t->track].start+t->sector;
    if(kui_crc32(0,s->data,KUI_RAW_BYTES)!=expected ||
       (s->plan->tracks[t->track].control==4 && sector_check(s,s->data,fad)))
        return fail(s,"Accepted repair candidate changed before publication");
    if(!publish_patch(s,index) || stop(s) || s->failed)return false;
    return apply_patch(s,index);
}
#include "salvage_control_trial.inc"
static void repair_message(struct salvage *s,unsigned index,const struct target *t) {
    s->status->track=t->track+1;s->status->fad=s->plan->tracks[t->track].start+t->sector;
    if(s->options->passes)
        snprintf(s->status->message,sizeof(s->status->message),"Recovery pass %u/%u, target %u",s->status->pass,s->options->passes,index+1);
    else
        snprintf(s->status->message,sizeof(s->status->message),"Recovery pass %u until stopped, target %u",s->status->pass,index+1);
    update(s,true);
}
/* A batch contains only adjacent unresolved DATA targets. Optical order is
 * ascending even on a reverse pass; selected targets are published in pass
 * order. Dedicated storage prevents singleton reads, anchor recovery and patch
 * publication from replacing the later checked batch slots. */
static bool repair_group(struct salvage *s,const unsigned *indices,const struct target *targets,unsigned count,bool forward) {
    bool valid[REPAIR_CHUNK]={false};uint32_t crc[REPAIR_CHUNK]={0};
    if(count>1) {
        uint32_t begin=targets[forward?0:count-1].sector;
        uint32_t fad=s->plan->tracks[targets[0].track].start+begin;
        repair_message(s,indices[0],&targets[0]);
        memset(s->repair_data,0xa5,(size_t)count*KUI_RAW_BYTES);
        enum kui_read_result r=optical(s,fad,count,s->repair_data,1,1,1,1);
        if(s->failed || stop(s))return false;
        unsigned accepted=0;
        if(r==KUI_READ_OK || r==KUI_READ_PARTIAL)for(unsigned slot=0;slot<count;++slot) {
            if(stop(s))return false;
            const uint8_t *raw=s->repair_data+slot*KUI_RAW_BYTES;
            if(!sector_check(s,raw,fad+slot)) {valid[slot]=true;crc[slot]=kui_crc32(0,raw,KUI_RAW_BYTES);++accepted;}
        }
        s->status->repair_batch_accepted+=accepted;
        if(accepted) {s->read_failures=0;s->zero_yield_batches=0;}
        else if(r==KUI_READ_PARTIAL && !retry_recovery(s))return false;
        if(s->failed || stop(s))return false;
        if(!accepted && s->repair_batching && ++s->zero_yield_batches>=2u) {
            s->repair_batching=false;
            s->ops->log("Salvage repair policy: 2 zero-yield batches; singles for rest of run");
        }
        read_outcome(s,accepted==count?KUI_SALVAGE_READ_ACCEPTED:KUI_SALVAGE_READ_DEFERRED);
    }
    for(unsigned j=0;j<count;++j) {
        if(stop(s) || s->failed)return false;
        unsigned slot=forward?j:count-1-j;const struct target *t=&targets[j];
        uint32_t fad=s->plan->tracks[t->track].start+t->sector,expected;
        if(count>1 && valid[slot]) {
            const uint8_t *raw=s->repair_data+slot*KUI_RAW_BYTES;
            if(kui_crc32(0,raw,KUI_RAW_BYTES)!=crc[slot] || sector_check(s,raw,fad))
                return fail(s,"Staged repair batch changed before publication");
            memcpy(s->data,raw,KUI_RAW_BYTES);expected=crc[slot];
        } else {
            repair_message(s,indices[j],t);
            if(!candidate(s,t->track,t->sector,true,1,1)) {
                if(s->failed || stop(s))return false;
                continue;
            }
            expected=kui_crc32(0,s->data,KUI_RAW_BYTES);
        }
        if(!store_repair(s,indices[j],t,expected))return false;
    }
    return true;
}
static bool repair(struct salvage *s) {
    bool ok=false;update(s,true);
    s->ops->log("Salvage repair policy: %s",s->repair_batching?
        "initial batches; stop after 2 zero-yield batches":
        "singleton targets (saved collection complete)");
    if(!s->status->remaining) {ok=true;goto done;}
    for(unsigned pass=1;;++pass) {
        s->status->pass=pass;unsigned unresolved=0;bool forward=(pass&1)!=0;
        if(pass>1 && s->ops->recover && !recover_drive(s))goto done;
        for(unsigned n=0;n<s->target_count;) {
            unsigned indices[REPAIR_CHUNK],count=1;
            struct target targets[REPAIR_CHUNK];
            indices[0]=forward?n:s->target_count-1-n;
            if(!target_get(s,indices[0],&targets[0]))goto done;
            if(targets[0].repaired) {++n;continue;}
            if(stop(s))goto done;
            if(s->repair_batching && s->plan->tracks[targets[0].track].control==4) {
                while(count<REPAIR_CHUNK && n+count<s->target_count) {
                    unsigned next=forward?n+count:s->target_count-1-(n+count);struct target t;
                    if(!target_get(s,next,&t))goto done;
                    const struct target *previous=&targets[count-1];
                    if(t.repaired || t.track!=previous->track ||
                       (forward?t.sector!=previous->sector+1:previous->sector!=t.sector+1))break;
                    indices[count]=next;targets[count++]=t;
                }
            }
            unresolved+=count;
            if(!repair_group(s,indices,targets,count,forward))goto done;
            n+=count;
        }
        if(!unresolved)break;
        update(s,true);if(!s->status->remaining)break;
        if(s->options->passes && pass>=s->options->passes)break;
        if(pass==UINT_MAX) {fail(s,"Recovery pass counter exhausted; committed progress retained");goto done;}
    }
    ok=true;
 done:
    s->ops->log("Salvage repair: batch_calls=%" PRIu32 " single_calls=%" PRIu32 " checked_batch=%" PRIu32 " checked_single=%" PRIu32,
        s->status->repair_batches,s->status->repair_singles,s->status->repair_batch_accepted,s->status->repair_single_accepted);
    s->ops->log("Salvage repair: optical_ms=%" PRIu64 "; checked candidates are not a durable-repair count",s->status->repair_optical_ms);
    return ok;
}
static bool publish_text(struct salvage *s,const char *name,const void *data,unsigned size) {
    char temporary[64],original[KUI_DEST_PATH_CAP];snprintf(temporary,sizeof(temporary),"%s.part",name);
    path(s,name);FILINFO info;FRESULT r=f_stat(s->path,&info);
    if(r==FR_OK) {
        FIL file;if(f_open(&file,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot verify existing final metadata");
        bool ok=f_size(&file)==size && size<=sizeof(s->data);if(ok)ok=read_exact(s,&file,s->data,size);
        if(ok)ok=!memcmp(data,s->data,size);
        if(!ok)fail(s,"Existing final metadata differs from salvage journal");
        return io_close(s,&file,ok);
    }
    if(r!=FR_NO_FILE)return fail(s,"Cannot inspect final metadata");
    path(s,temporary);r=f_unlink(s->path);if(r!=FR_OK && r!=FR_NO_FILE)return fail(s,"Cannot discard incomplete final metadata");
    if(!write_new(s,temporary,data,size))return false;
    /* Final metadata readback does not alias its source buffer. */
    path(s,temporary);FIL file;if(f_open(&file,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot read back final metadata");
    bool ok=f_size(&file)==size && size<=sizeof(s->data);if(ok)ok=read_exact(s,&file,s->data,size);
    if(ok)ok=!memcmp(data,s->data,size);
    if(!ok)fail(s,"Final metadata readback mismatch");
    if(!io_close(s,&file,ok))return false;
    path(s,temporary);snprintf(original,sizeof(original),"%s",s->path);path(s,name);
    if(f_rename(original,s->path)!=FR_OK)return fail(s,"Final metadata publication failed");
    return true;
}
static bool finish(struct salvage *s) {
    update(s,true);if(!s->ready || s->status->remaining)return true;
    char *text=malloc(32768);if(!text)return fail(s,"Not enough memory for final salvage metadata");
    uint32_t final[99];memcpy(final,s->baseline,sizeof(final));
    memset(s->scratch,0,KUI_RAW_BYTES);uint32_t zero_crc=kui_crc32(0,s->scratch,KUI_RAW_BYTES);
    snprintf(s->status->message,sizeof(s->status->message),"Finalizing recovered track CRCs from durable evidence");update(s,true);
    for(unsigned n=0;n<s->target_count;++n) {
        if(stop(s)) {free(text);return false;}
        struct target target;if(!target_get(s,n,&target)) {free(text);return false;}
        const struct kui_capture_track *t=&s->plan->tracks[target.track];
        uint64_t suffix=(uint64_t)(t->end-t->start-target.sector-1)*KUI_RAW_BYTES;
        final[target.track]=kui_recovery_crc_replace(final[target.track],zero_crc,target.crc,suffix);
        update(s,false);
    }
    if(stop(s)) {free(text);return false;}
    bool cd=s->plan->profile==KUI_CAPTURE_PROFILE_CD_TOC;
    size_t used=cd?(size_t)snprintf(text,32768,"REM CAPTURE_PROFILE %s\nREM TOC INDEX01 ONLY; INDEX00 AND SUBCHANNELS NOT CAPTURED\n",KUI_CAPTURE_CD_PROFILE):(size_t)snprintf(text,32768,"%u\n",s->plan->count);
    for(unsigned i=0;i<s->plan->count;++i) {
        const struct kui_capture_track *t=&s->plan->tracks[i];int n;
        if(cd) {
            unsigned mode=0;if(t->control==4) {
                track_path(s,i);FIL track;if(f_open(&track,s->path,FA_READ)!=FR_OK) {free(text);return fail(s,"Cannot inspect CD track mode");}
                bool ok=true;
                for(uint32_t sector=0;ok && sector<t->end-t->start;++sector) {
                    if(stop(s)) {ok=false;break;}
                    ok=read_exact(s,&track,s->scratch,KUI_RAW_BYTES) && !sector_check(s,s->scratch,t->start+sector);
                    if(!sector)mode=s->scratch[15];else if(s->scratch[15]!=mode)ok=false;
                    if(!(sector%128))update(s,false);
                }
                if(!io_close(s,&track,ok) || !ok) {free(text);return stop(s)?false:fail(s,"CD track data checks or uniform sector mode failed; no descriptor published");}
            }
            char type[24];if(t->control==4)snprintf(type,sizeof(type),"MODE%u/2352",mode);else snprintf(type,sizeof(type),"AUDIO");
            n=snprintf(text+used,32768-used,"FILE \"track%02u.%s\" BINARY\n  TRACK %02u %s\n",i+1,t->control==4?"bin":"raw",i+1,type);
            if(n<0 || (size_t)n>=32768-used) {free(text);return fail(s,"Final descriptor exceeds its bound");}used+=(size_t)n;
            if(!i && t->start>150u) {uint32_t gap=t->start-150u;n=snprintf(text+used,32768-used,"    PREGAP %02u:%02u:%02u\n",(unsigned)(gap/4500u),(unsigned)((gap/75u)%60u),(unsigned)(gap%75u));if(n<0 || (size_t)n>=32768-used) {free(text);return fail(s,"Final descriptor exceeds its bound");}used+=(size_t)n;}
            n=snprintf(text+used,32768-used,"    INDEX 01 00:00:00\n");
        } else n=snprintf(text+used,32768-used,"%u %" PRIu32 " %u 2352 track%02u.%s 0\n",i+1,t->start-150,(unsigned)t->control,i+1,t->control==4?"bin":"raw");
        if(n<0 || (size_t)n>=32768-used) {free(text);return fail(s,"Final descriptor exceeds its bound");}used+=(size_t)n;
    }
    bool ok=publish_text(s,cd?"salvage.cue":"salvage.gdi",text,(unsigned)used);
    char hex[65];kui_hex(s->identity,32,hex);
    used=(size_t)snprintf(text,32768,"{\n  \"salvage_schema\":%u,\n  \"identity\":\"%s\",\n  \"requires_complete_journal\":true,\n  \"full_saved_readback\":false,\n  \"original_targets\":%u,\n  \"unresolved\":0,\n  \"tracks\":[\n",(unsigned)get32(s->header+8),hex,s->target_count);
    for(unsigned i=0;i<s->plan->count;++i) {
        uint32_t crc=final[i];const struct kui_capture_track *t=&s->plan->tracks[i];
        int n=snprintf(text+used,32768-used,"    {\"number\":%u,\"bytes\":%" PRIu64 ",\"crc32\":\"%08" PRIx32 "\"}%s\n",i+1,(uint64_t)(t->end-t->start)*KUI_RAW_BYTES,crc,i+1==s->plan->count?"":",");
        if(n<0 || (size_t)n>=32768-used) {free(text);return fail(s,"Final manifest exceeds its bound");}used+=(size_t)n;
    }
    memcpy(text+used,"  ]\n}\n",6);used+=6;
    if(ok)ok=!stop(s) && publish_text(s,"salvage.json",text,(unsigned)used);
    free(text);
    if(ok && stop(s))ok=false;
    if(ok && !s->completed) {ok=append(s,EV_COMPLETE,0,0,s->target_count,0);if(ok)s->completed=true;}
    if(ok && s->completed) {
        memcpy(s->status->final_crc,final,sizeof(final));
        memcpy(s->status->identity,s->identity,sizeof(s->identity));
    }
    return ok;
}
/* This private receipt is an exclusive-worker, same-boot acceleration only.
 * It is never serialized. Ordinary callers retain the historical cold path.
 * A full small-index digest binds derived records to the last checked state;
 * file fences, the fresh header/anchors/guard and caller ownership lease must
 * all match before the journal and durable-target scans can be omitted. */
#define SESSION_INDEX_LIMIT (1024u*1024u)
#define SESSION_HASH_BYTES 32768u
#define SESSION_FILES 103u
struct session_file {
    uint64_t bytes;
    uint32_t cluster;
    uint16_t date,time;
    uint8_t attributes,present;
};
struct salvage_session {
    uint64_t epoch,sequence;
    uint32_t target_count,recovered_count,patch_count;
    uint32_t sectors[99],crc[99],baseline[99];
    bool baselined[99];
    uint8_t card[32],volume[32],identity[32],index_hash[32],tail_hash[32];
    char job[KUI_DEST_JOB_CAP];
    struct session_file files[SESSION_FILES];
    uint8_t seal[32];
    bool valid;
};
static struct salvage_session session;
_Static_assert(sizeof(session)<=8192u,"A stopped recovery retains only a compact RAM receipt");
void kui_salvage_session_invalidate(void) {memset(&session,0,sizeof(session));}
static void session_seal(uint8_t digest[32]) {
    struct kui_sha256 hash;kui_sha256_init(&hash);
    kui_sha256_update(&hash,&session,offsetof(struct salvage_session,seal));
    kui_sha256_digest(&hash,digest);
}
static bool session_volume(struct salvage *s,uint8_t digest[32]) {
    const struct kui_volume *v=kui_media_volume();uint8_t extent[8];
    if(!v || !v->count || disk_read(0,s->scratch,0,1)!=RES_OK)
        return fail(s,"Cannot confirm the saved session volume");
    if(s->scratch[510]!=0x55 || s->scratch[511]!=0xaa)return false;
    put32(extent,v->start);put32(extent+4,v->count);
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,extent,sizeof(extent));
    if(!memcmp(s->scratch+3,"EXFAT   ",8)) {
        kui_sha256_update(&hash,s->scratch+3,8);kui_sha256_update(&hash,s->scratch+64,42);
        uint8_t active_fat=s->scratch[106]&1u;kui_sha256_update(&hash,&active_fat,1);
        kui_sha256_update(&hash,s->scratch+108,4); /* dirty/usage are mutable */
    } else {
        kui_sha256_update(&hash,"FAT32",5);kui_sha256_update(&hash,s->scratch+11,53);
        kui_sha256_update(&hash,s->scratch+67,4);
    }
    kui_sha256_digest(&hash,digest);return true;
}
/* A clean metadata mismatch is a cache miss. Transport/short/close failures
 * remain operational failures and must never trigger a silent read retry. */
static int session_file(struct salvage *s,unsigned index,struct session_file *out) {
    char name[32];uint64_t expected=0;bool optional=false;
    if(index==0) {snprintf(name,sizeof(name),"header.bin");expected=HEADER_BYTES;}
    else if(index==1) {snprintf(name,sizeof(name),"journal.bin");expected=s->sequence*EVENT_BYTES;}
    else if(index==2) {snprintf(name,sizeof(name),"targets.cache");expected=(uint64_t)s->target_count*TARGET_BYTES;}
    else if(index==3) {snprintf(name,sizeof(name),"patches.bin");expected=(uint64_t)s->patch_count*PACKED_PATCH_BYTES;optional=!expected;}
    else {
        unsigned ti=index-4u;const struct kui_capture_track *t=&s->plan->tracks[ti];
        snprintf(name,sizeof(name),"track%02u.%s",ti+1,t->control==4?"bin":"raw");
        expected=(uint64_t)(t->end-t->start)*KUI_RAW_BYTES;
    }
    memset(out,0,sizeof(*out));path(s,name);FILINFO info;FRESULT result=f_stat(s->path,&info);
    if(result==FR_NO_FILE)return optional?1:0;
    if(result!=FR_OK) {fail(s,"Cannot inspect a saved session source");return -1;}
    if((info.fattrib&AM_DIR) || info.fsize!=expected)return 0;
    FIL file;result=f_open(&file,s->path,FA_READ);
    if(result==FR_NO_FILE)return 0;
    if(result!=FR_OK) {fail(s,"Cannot open a saved session source");return -1;}
    bool same=f_size(&file)==expected;
    if(same) {
        out->bytes=expected;out->cluster=file.obj.sclust;
        out->date=info.fdate;out->time=info.ftime;out->attributes=info.fattrib;out->present=1;
    }
    if(!io_close(s,&file,true))return -1;
    return same?1:0;
}
static int session_tail(struct salvage *s,uint8_t digest[32]) {
    if(!s->sequence)return 0;
    path(s,"journal.bin");FIL file;FRESULT result=f_open(&file,s->path,FA_READ);
    if(result==FR_NO_FILE)return 0;
    if(result!=FR_OK) {fail(s,"Cannot check the saved session journal tail");return -1;}
    bool ok=f_size(&file)==s->sequence*EVENT_BYTES;
    if(ok)ok=seek(s,&file,(s->sequence-1u)*EVENT_BYTES) && read_exact(s,&file,s->event,EVENT_BYTES);
    if(!io_close(s,&file,true))return -1;
    if(s->failed)return -1;
    if(!ok || memcmp(s->event,"KUISJNL1",8) || get64(s->event+8)!=s->sequence ||
       get32(s->event+508)!=kui_crc32(0,s->event,508) || memcmp(s->event+40,s->identity,32))return 0;
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,s->event,EVENT_BYTES);kui_sha256_digest(&hash,digest);
    return 1;
}
static int session_index(struct salvage *s,uint8_t digest[32],bool cancelling) {
    uint64_t bytes=(uint64_t)s->target_count*TARGET_BYTES;
    if(!bytes || bytes>SESSION_INDEX_LIMIT)return 0;
    path(s,"targets.cache");FIL file;FRESULT result=f_open(&file,s->path,FA_READ);
    if(result==FR_NO_FILE)return 0;
    if(result!=FR_OK) {fail(s,"Cannot check the saved session index");return -1;}
    uint8_t *window=(uint8_t *)(((uintptr_t)s->data+31u)&~(uintptr_t)31u);
    uint8_t *slots=window+SESSION_HASH_BYTES;size_t slot_bytes=(s->patch_count+7u)/8u;
    if(slot_bytes>SESSION_INDEX_LIMIT/TARGET_BYTES/8u) {io_close(s,&file,true);return 0;}
    memset(slots,0,slot_bytes);
    bool ok=f_size(&file)==bytes;unsigned index=0,repaired=0,previous_track=0;uint32_t previous_sector=0;
    struct kui_sha256 hash;kui_sha256_init(&hash);
    for(uint64_t at=0;ok && at<bytes;) {
        if(!cancelling && stop(s)) {ok=false;break;}
        unsigned n=bytes-at>SESSION_HASH_BYTES?SESSION_HASH_BYTES:(unsigned)(bytes-at);
        if(!read_exact(s,&file,window,n)) {ok=false;break;}
        kui_sha256_update(&hash,window,n);
        for(unsigned off=0;off<n;off+=TARGET_BYTES,++index) {
            const uint8_t *b=window+off;uint32_t flags=get32(b+20),track=flags&0x7fffffffu,sector=get32(b+12),slot=get32(b+24);
            bool done=(flags&0x80000000u)!=0;
            if(memcmp(b,"KUITGT2",8) || get32(b+8)!=index || get32(b+28)!=kui_crc32(0,b,28) ||
               track>=s->plan->count || sector>=s->plan->tracks[track].end-s->plan->tracks[track].start ||
               (index && (track<previous_track || (track==previous_track && sector<=previous_sector))) ||
               (done?(slot>=s->patch_count):(slot!=NO_PATCH || get32(b+16)))) {ok=false;break;}
            if(done) {
                uint8_t bit=(uint8_t)(1u<<(slot&7u));if(slots[slot>>3]&bit) {ok=false;break;}
                slots[slot>>3]|=bit;++repaired;
            }
            previous_track=track;previous_sector=sector;
        }
        at+=n;
    }
    if(!io_close(s,&file,true) || s->failed)return -1;
    if(!ok || index!=s->target_count || repaired!=s->recovered_count || repaired!=s->patch_count)return 0;
    kui_sha256_digest(&hash,digest);return 1;
}
static void session_restore(struct salvage *s) {
    memcpy(s->sectors,session.sectors,sizeof(s->sectors));memcpy(s->crc,session.crc,sizeof(s->crc));
    memcpy(s->baseline,session.baseline,sizeof(s->baseline));memcpy(s->baselined,session.baselined,sizeof(s->baselined));
    s->sequence=session.sequence;s->target_count=session.target_count;s->recovered_count=session.recovered_count;
    s->patch_count=session.patch_count;s->ready=true;s->completed=false;
}
static void session_state_clear(struct salvage *s) {
    memset(s->sectors,0,sizeof(s->sectors));memset(s->crc,0,sizeof(s->crc));memset(s->baseline,0,sizeof(s->baseline));
    memset(s->baselined,0,sizeof(s->baselined));s->sequence=0;s->target_count=0;s->recovered_count=0;s->patch_count=0;
    s->ready=s->completed=false;
}
static bool session_try(struct salvage *s,bool available,enum kui_salvage_action action) {
    const struct kui_salvage_options *o=s->options;uint8_t digest[32];
    if(!available || !o->session_epoch || !o->session_card || action!=KUI_SALVAGE_RESUME || o->defer_next_range || o->control_trial)return false;
    session_seal(digest);
    if(memcmp(digest,session.seal,32) || o->session_epoch!=session.epoch || memcmp(o->session_card,session.card,32) ||
       strcmp(s->status->job,session.job) || memcmp(s->identity,session.identity,32)) {
        s->ops->log("Salvage session miss: ownership, card, job, identity or RAM receipt changed");return false;
    }
    if(!session_volume(s,digest) || memcmp(digest,session.volume,32)) {
        s->ops->log("Salvage session miss: volume fingerprint changed or could not be read");return false;
    }
    session_restore(s);bool matched=true;
    for(unsigned i=0;i<4u+s->plan->count && matched;++i) {
        struct session_file current;int result=session_file(s,i,&current);
        matched=result==1 && !memcmp(&current,&session.files[i],sizeof(current));
        if(!matched)s->ops->log("Salvage session miss: saved source %u metadata/length changed or read failed",i);
    }
    if(matched) {
        matched=session_tail(s,digest)==1 && !memcmp(digest,session.tail_hash,32);
        if(!matched)s->ops->log("Salvage session miss: journal tail changed or could not be read");
    }
    if(matched) {
        matched=session_index(s,digest,false)==1 && !memcmp(digest,session.index_hash,32);
        if(!matched)s->ops->log("Salvage session miss: checked target index changed or could not be read");
    }
    if(!matched || stop(s) || s->failed) {session_state_clear(s);return false;}
    path(s,"journal.bin");
    if(f_open(&s->journal,s->path,FA_READ|FA_WRITE)!=FR_OK)return fail(s,"Cannot reopen the checked session journal");
    s->journal_open=true;
    if(f_size(&s->journal)!=s->sequence*EVENT_BYTES || s->journal.obj.sclust!=session.files[1].cluster)
        return fail(s,"Checked journal changed before session continuation");
    path(s,"targets.cache");
    if(f_open(&s->index,s->path,FA_READ|FA_WRITE)!=FR_OK)return fail(s,"Cannot reopen the checked session index");
    s->index_open=true;
    if(f_size(&s->index)!=(uint64_t)s->target_count*TARGET_BYTES || s->index.obj.sclust!=session.files[2].cluster ||
       !seek(s,&s->journal,s->sequence*EVENT_BYTES))return fail(s,"Checked index changed before session continuation");
    s->status->session_reused=true;
    snprintf(s->status->message,sizeof(s->status->message),"Reusing checked saved progress from this session");update(s,true);
    s->ops->log("Salvage session reused: checked journal=%" PRIu64 " records targets=%u recovered=%u; no replay or durable-target rescan",
        s->sequence,s->target_count,s->recovered_count);
    return true;
}
static bool session_capture(struct salvage *s) {
    memset(&session,0,sizeof(session));const struct kui_salvage_options *o=s->options;
    session.epoch=o->session_epoch;session.sequence=s->sequence;session.target_count=s->target_count;
    session.recovered_count=s->recovered_count;session.patch_count=s->patch_count;
    memcpy(session.card,o->session_card,32);memcpy(session.identity,s->identity,32);
    snprintf(session.job,sizeof(session.job),"%s",s->status->job);
    memcpy(session.sectors,s->sectors,sizeof(session.sectors));memcpy(session.crc,s->crc,sizeof(session.crc));
    memcpy(session.baseline,s->baseline,sizeof(session.baseline));memcpy(session.baselined,s->baselined,sizeof(session.baselined));
    if(!session_volume(s,session.volume))return false;
    for(unsigned i=0;i<4u+s->plan->count;++i)if(session_file(s,i,&session.files[i])!=1)return false;
    if(session_tail(s,session.tail_hash)!=1 || session_index(s,session.index_hash,true)!=1)return false;
    session_seal(session.seal);return true;
}
enum kui_salvage_result kui_salvage_run(const struct kui_capture_plan *plan,const struct kui_capture_ops *ops,
    const struct kui_salvage_options *options,enum kui_salvage_action action,struct kui_salvage_status *status) {
    bool session_available=session.valid;session.valid=false;
    if(!status)return KUI_SALVAGE_FAILED;
    memset(status,0,sizeof(*status));
    if(!plan_valid(plan) || !ops || !ops->read || !ops->log || !options || action<KUI_SALVAGE_NEW || action>KUI_SALVAGE_RECOVER ||
       (options->control_trial && (action!=KUI_SALVAGE_RECOVER || !options->trial_select || !ops->now_ms)) ||
       (options->defer_next_range && action!=KUI_SALVAGE_RESUME) ||
       (options->passes!=0 && options->passes!=1 && options->passes!=5 && options->passes!=10 && options->passes!=20 && options->passes!=50)) {
        snprintf(status->message,sizeof(status->message),"Invalid salvage request or pass limit");return status->result;
    }
    struct salvage *s=calloc(1,sizeof(*s));if(!s) {snprintf(status->message,sizeof(status->message),"Not enough memory for bounded salvage state");return status->result;}
    s->patch=(uint8_t *)(((uintptr_t)s->patch_storage+31u)&~(uintptr_t)31u);
    s->plan=plan;s->ops=ops;s->options=options;s->status=status;status->tracks=plan->count;status->total=plan->bytes;status->pass_limit=options->passes;
    s->started=ops->now_ms?ops->now_ms(ops->ctx):0;
    FATFS fs;bool mounted=false,ok=false,ready_at_admission=false,warm=false,checked=false,repair_entered=false,retained=false;
    if(stop(s))goto done;
    if(action==KUI_SALVAGE_NEW && !make_header(s))goto done;
    if(!kui_mount(&fs,ops->log)) {fail(s,"Cannot mount SD card for salvage");goto done;}mounted=true;
    if(action==KUI_SALVAGE_NEW) {if(!make_job(s))goto done;identity(s);}
    else {if(!job_path(s,options->job) || !exact_file(s,"header.bin",s->header,HEADER_BYTES) || !validate_header(s))goto done;identity(s);}
    if(action!=KUI_SALVAGE_NEW && !kui_recovery_guard_check(status->job,ops->log)) {
        fail(s,"Saved storage reads were unstable; salvage blocked; preserve job and see Diagnostics");goto done;
    }
    if(stop(s))goto done;
    warm=session_try(s,session_available,action);
    if(s->failed || stop(s))goto done;
    if(!warm && !open_journal(s,action==KUI_SALVAGE_NEW))goto done;
    if(!check_lengths(s))goto done;
    /* An already collected job resumes with targeted singleton reads. An
     * invocation that still needs collection keeps the initial opportunity to
     * rescue neighboring sectors together before harder singleton work. */
    ready_at_admission=s->ready;s->repair_batching=!ready_at_admission;
    if(options->defer_next_range) {
        bool remaining=false;
        for(unsigned i=0;i<plan->count;++i)if(s->sectors[i]<plan->tracks[i].end-plan->tracks[i].start)remaining=true;
        if(s->ready || !remaining) {fail(s,"Collection complete; no unread range to defer");goto done;}
    }
    if(!reconcile_patch_tail(s))goto done;
    ops->log("Salvage job: %s; unverified sectors are recorded as unresolved placeholders",status->job);
    ops->log("Salvage format: version=%u; candidates=%s",header_version(s),s->packed?"packed":"legacy files");
    if(options->passes)ops->log("Salvage: automatic recovery, up to %u alternating passes",options->passes);
    else ops->log("Salvage: automatic alternating recovery passes until resolved or stopped");
    if(action==KUI_SALVAGE_RECOVER && !s->ready) {fail(s,"First pass is incomplete; use Continue recovery before targeted trials");goto done;}
    if(!warm && !reconcile(s))goto done;
    checked=ready_at_admission;
    if(!s->ready) {
        s->collection_base=committed_bytes(s);
        s->collection_started=ops->now_ms?ops->now_ms(ops->ctx):0;status->collection_active=true;
        bool collected=first_pass(s);update(s,false);status->collection_active=false;
        s->read_failures=0; /* Collection failures do not seed repair reinitialization. */
        if(!collected || !sync_file(s,&s->index))goto done;
    }
    repair_entered=true;
    if(!(options->control_trial?control_trial(s):repair(s)) || !close_salvage_files(s))goto done;
    if(stop(s) || !finish(s))goto done;
    ok=true;
done:
    if(!close_salvage_files(s))ok=false;
    if(s->index_open && !io_close(s,&s->index,true))ok=false;
    if(s->journal_open && !io_close(s,&s->journal,true))ok=false;
    if(mounted && checked && repair_entered && action==KUI_SALVAGE_RESUME && s->packed && s->ready && !s->completed && !s->failed &&
       !options->control_trial && !options->defer_next_range && options->session_epoch && options->session_card &&
       !s->pending_count && s->patch_count==s->recovered_count && s->target_count>s->recovered_count &&
       (uint64_t)s->target_count*TARGET_BYTES<=SESSION_INDEX_LIMIT)
        retained=session_capture(s);
    if(mounted && f_mount(NULL,"0:",0)!=FR_OK) {fail(s,"Card unmount failed; operation is not successful");ok=false;}
    session.valid=retained && !s->failed;
    status->session_retained=session.valid;
    if(session.valid)s->ops->log("Salvage session retained: journal=%" PRIu64 " records targets=%u recovered=%u; same-boot Continue may reuse checked state",
        s->sequence,s->target_count,s->recovered_count);
    else s->ops->log("Salvage session not retained: %s",
        s->failed?"operation or cleanup failed":!options->session_epoch || !options->session_card?"no physical-card session lease":
        !checked?"full ready-job reconciliation was not completed":!repair_entered?"recovery did not begin":
        action!=KUI_SALVAGE_RESUME || options->control_trial || options->defer_next_range?"not ordinary Continue recovery":
        !s->packed || !s->ready?"job is not a collected packed job":s->completed || s->target_count==s->recovered_count?"no unresolved recovery remains":
        s->pending_count || s->patch_count!=s->recovered_count?"an interrupted publication needs cold reconciliation":
        (uint64_t)s->target_count*TARGET_BYTES>SESSION_INDEX_LIMIT?"derived index exceeds bounded session limit":
        "saved source fences were not confirmed");
    update(s,true);
    if(s->failed)status->result=KUI_SALVAGE_FAILED;
    else if(!ok)status->result=stop(s)?KUI_SALVAGE_STOPPED:KUI_SALVAGE_FAILED;
    else status->result=s->completed?KUI_SALVAGE_RESOLVED:KUI_SALVAGE_UNRESOLVED;
    status->complete=ok && !s->failed && s->completed;
    if(status->result==KUI_SALVAGE_STOPPED)snprintf(status->message,sizeof(status->message),"Stopped; committed good data and repair candidates retained");
    else if(status->result==KUI_SALVAGE_RESOLVED)snprintf(status->message,sizeof(status->message),"All targets resolved; PC saved-file verification is still required");
    else if(status->result==KUI_SALVAGE_UNRESOLVED && (status->trial_complete || status->trial_budget_expired))
        snprintf(status->message,sizeof(status->message),"Drive trial %s; %" PRIu32 " unresolved sectors; saved repairs retained",
            status->trial_budget_expired?"budget reached":"finished",status->remaining);
    else if(status->result==KUI_SALVAGE_UNRESOLVED)snprintf(status->message,sizeof(status->message),"%" PRIu32 " unresolved sectors after %u passes; Continue recovery to retry",status->remaining,status->pass);
    ops->log("Salvage: %s; targets=%u recovered=%u remaining=%u",status->message,status->targets,status->recovered,status->remaining);
    update(s,true);enum kui_salvage_result result=status->result;free(s);return result;
}

bool kui_salvage_latest(const struct kui_capture_plan *plan,const struct kui_capture_ops *ops,char job[KUI_DEST_JOB_CAP]) {
    if(job)job[0]=0;
    if(!job || !plan_valid(plan) || !ops || !ops->read || !ops->log)return false;
    struct salvage *s=calloc(1,sizeof(*s));if(!s)return false;
    struct kui_salvage_status status={0};const struct kui_salvage_options options={.passes=1};
    s->plan=plan;s->ops=ops;s->status=&status;s->options=&options;
    FATFS fs;bool found=false,mounted=false;
    if(stop(s) || !kui_mount(&fs,ops->log))goto end;
    mounted=true;
    unsigned ceiling=10000;
    while(!stop(s)) {
        DIR dir;FRESULT r=f_opendir(&dir,"0:/KUI/salvage");
        if(r==FR_NO_PATH)break;
        if(r!=FR_OK) {fail(s,"Cannot list salvage jobs");break;}
        unsigned selected=0;FILINFO info;
        while((r=f_readdir(&dir,&info))==FR_OK && info.fname[0] && !stop(s)) {
            if(!(info.fattrib&AM_DIR) || strlen(info.fname)!=8 || strncmp(info.fname,"job-",4))continue;
            unsigned number=0;bool digits=true;
            for(unsigned i=4;i<8;++i) {if(info.fname[i]<'0'||info.fname[i]>'9')digits=false;number=number*10+(unsigned)(info.fname[i]-'0');}
            if(!digits || number<=selected || number>=ceiling)continue;
            snprintf(status.job,sizeof(status.job),"/KUI/salvage/%.8s",info.fname);
            path(s,"header.bin");FIL file;FRESULT opened=f_open(&file,s->path,FA_READ);
            if(opened==FR_NO_FILE)continue;
            if(opened!=FR_OK) {fail(s,"Cannot read salvage header during discovery");break;}
            bool valid=f_size(&file)==HEADER_BYTES;
            if(valid)valid=read_exact(s,&file,s->header,HEADER_BYTES);
            if(!io_close(s,&file,true))break;
            const uint8_t *b=s->header;
            valid=valid && header_version(s) && get32(b+12)==HEADER_BYTES && get32(b+16)==plan->count &&
                get32(b+20)<=1 && get32(b+HEADER_BYTES-4)==kui_crc32(0,b,HEADER_BYTES-4);
            for(unsigned i=0;valid && i<plan->count;++i) {
                const struct kui_capture_track *t=&plan->tracks[i];const uint8_t *p=b+96+i*24;
                valid=get32(p)==t->number && get32(p+4)==t->control && get32(p+8)==t->session && get32(p+12)==t->start && get32(p+16)==t->end && get32(p+20)==t->toc_end;
            }
            if(valid)selected=number;
        }
        if(f_closedir(&dir)!=FR_OK || r!=FR_OK)fail(s,"Salvage directory read failed");
        if(s->failed || !selected || stop(s))break;
        snprintf(status.job,sizeof(status.job),"/KUI/salvage/job-%04u",selected);
        if(!exact_file(s,"header.bin",s->header,HEADER_BYTES))break;
        /* A valid different payload skips this job; transient reads retry the
         * exact saved location. Fatal/Stop never probes older jobs. */
        bool same=true;
        for(unsigned a=0;a<2;++a) {
            uint32_t fad=get32(s->header+24+a*4);int track=anchor_track(s,fad);bool different;
            if(track<0 || fad==get32(s->header+24+(1-a)*4)) {same=false;break;}
            if(!anchor_read(s,(unsigned)track,fad,s->header+32+a*32,&different)) {
                same=false;if(!different && !stop(s) && !s->failed)fail(s,"Cannot read salvage identity anchor after bounded discovery retries");break;
            }
        }
        if(s->failed)break;
        if(same) {snprintf(job,KUI_DEST_JOB_CAP,"%s",status.job);found=true;break;}
        ceiling=selected;
    }
end:
    if(mounted && f_mount(NULL,"0:",0)!=FR_OK)found=false;
    if(!found)job[0]=0;
    free(s);return found;
}
