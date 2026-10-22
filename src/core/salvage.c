/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/salvage.h"
#include "kui/recovery_checks.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every journal record occupies its own 512-byte sector. A torn final record
 * can be discarded: data commits follow track sync; repairs follow immutable
 * candidate publication plus exact track readback. Earlier corruption fails
 * closed. FatFs/card firmware cannot promise physical power-failure atomicity. */
#define HEADER_BYTES 4096u
#define EVENT_BYTES 512u
#define PATCH_BYTES 8192u
#define CHUNK 32u
#define JOURNAL_LIMIT (64u*1024u*1024u)
enum event_type { EV_TARGET=1,EV_CHUNK,EV_BASELINE,EV_READY,EV_REPAIRED,EV_COMPLETE };
struct target { uint32_t sector, crc; uint16_t track; bool repaired; };
struct salvage {
    const struct kui_capture_plan *plan;
    const struct kui_capture_ops *ops;
    const struct kui_salvage_options *options;
    struct kui_salvage_status *status;
    struct target target[KUI_SALVAGE_TARGET_LIMIT];
    uint32_t sectors[99], crc[99], baseline[99];
    bool baselined[99], zero_fill, failed, journal_open, ready, completed;
    unsigned target_count,pending_count;
    uint32_t pending[CHUNK];
    uint64_t sequence,started,last_progress,pending_offset;
    FIL journal;
    uint8_t header[HEADER_BYTES], event[EVENT_BYTES], patch[PATCH_BYTES];
    uint8_t data[CHUNK*KUI_RAW_BYTES], scratch[KUI_RAW_BYTES], identity[32];
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
static void update(struct salvage *s,bool force) {
    struct kui_salvage_status *o=s->status;
    o->targets=s->target_count;o->recovered=0;o->done=0;
    for(unsigned i=0;i<s->target_count;++i)o->recovered+=s->target[i].repaired;
    for(unsigned i=0;i<s->plan->count;++i)o->done+=(uint64_t)s->sectors[i]*KUI_RAW_BYTES;
    o->remaining=o->targets-o->recovered;o->first_pass_complete=s->ready;
    uint64_t now=s->ops->now_ms?s->ops->now_ms(s->ops->ctx):0;
    o->elapsed_ms=now>=s->started?now-s->started:0;
    if(s->options->progress && (force || now-s->last_progress>=100 || !s->ops->now_ms)) {
        s->last_progress=now;s->options->progress(s->ops->ctx,o);
    }
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
    if(!p || !p->count || p->count>99)return false;
    uint64_t bytes=0;
    for(unsigned i=0;i<p->count;++i) {
        const struct kui_capture_track *t=&p->tracks[i];
        if(t->number!=i+1 || (t->control!=0 && t->control!=4) || t->session>1 || t->start<150 || t->end<=t->start ||
           t->toc_end<t->end || t->toc_end>KUI_RECOVERY_FAD_MAX+1u ||
           t->end>KUI_RECOVERY_FAD_MAX+1u || (i && t->start<p->tracks[i-1].end))return false;
        bytes+=(uint64_t)(t->end-t->start)*KUI_RAW_BYTES;
    }
    return bytes==p->bytes;
}
static enum kui_read_result optical(struct salvage *s,uint32_t fad,unsigned count,uint8_t *data) {
    if(stop(s))return KUI_READ_RETRY;
    ++s->status->attempts;s->status->fad=fad;
    enum kui_read_result r=s->ops->read(s->ops->ctx,fad,count,data);
    if(r==KUI_READ_FATAL)fail(s,"Drive fatal/reset-required result; no placeholder or further reads");
    return r;
}
static bool candidate(struct salvage *s,unsigned track,uint32_t sector,bool audio_twice) {
    uint32_t fad=s->plan->tracks[track].start+sector;
    enum kui_read_result r=optical(s,fad,1,s->data);
    if(r!=KUI_READ_OK || stop(s))return false;
    if(s->plan->tracks[track].control==4)return kui_recovery_sector_check(s->data,KUI_RAW_BYTES,fad)==0;
    if(!audio_twice)return true;
    r=optical(s,fad,1,s->scratch);
    return r==KUI_READ_OK && !stop(s) && !memcmp(s->data,s->scratch,KUI_RAW_BYTES);
}
static bool make_header(struct salvage *s) {
    memset(s->header,0,HEADER_BYTES);memcpy(s->header,"KUISALV1",8);
    put32(s->header+8,1);put32(s->header+12,HEADER_BYTES);put32(s->header+16,s->plan->count);
    put32(s->header+20,s->options->zero_fill?1:0);s->zero_fill=s->options->zero_fill;
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
            const struct kui_capture_track *t=&s->plan->tracks[i];if(t->control!=4 || t->session!=1)continue;
            uint32_t count=t->end-t->start;
            for(unsigned n=0;n<16 && n<count;++n) {
                uint32_t sector=a?count-1-n:n,fad=t->start+sector;
                if(fad==chosen[0])continue;
                if(stop(s) || s->failed)return false;
                if(!candidate(s,i,sector,false))continue;
                struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,s->data,KUI_RAW_BYTES);
                kui_sha256_digest(&hash,s->header+32+a*32);put32(s->header+24+a*4,fad);
                chosen[a]=fad;++found;accepted=true;break;
            }
        }
    }
    if(found!=2)return fail(s,"Cannot establish two healthy data anchors; no salvage files created");
    put32(s->header+HEADER_BYTES-4,kui_crc32(0,s->header,HEADER_BYTES-4));
    return true;
}
static bool validate_header(struct salvage *s) {
    const uint8_t *b=s->header;
    if(memcmp(b,"KUISALV1",8) || get32(b+8)!=1 || get32(b+12)!=HEADER_BYTES || get32(b+16)!=s->plan->count ||
       get32(b+20)>1 || get32(b+HEADER_BYTES-4)!=kui_crc32(0,b,HEADER_BYTES-4))return fail(s,"Invalid salvage header; original metadata retained");
    for(unsigned i=0;i<s->plan->count;++i) {
        const struct kui_capture_track *t=&s->plan->tracks[i];const uint8_t *p=b+96+i*24;
        if(get32(p)!=t->number || get32(p+4)!=t->control || get32(p+8)!=t->session || get32(p+12)!=t->start ||
           get32(p+16)!=t->end || get32(p+20)!=t->toc_end)return fail(s,"Inserted disc TOC does not match this salvage job");
    }
    for(unsigned i=96+s->plan->count*24;i<HEADER_BYTES-4;++i)if(b[i])return fail(s,"Unknown salvage header fields");
    if(get32(b+24)==get32(b+28))return fail(s,"Salvage identity anchors are not distinct");
    for(unsigned a=0;a<2;++a) {
        uint32_t fad=get32(b+24+a*4);bool in_data=false;
        for(unsigned i=0;i<s->plan->count;++i)if(s->plan->tracks[i].control==4 && s->plan->tracks[i].session==1 && fad>=s->plan->tracks[i].start && fad<s->plan->tracks[i].end)in_data=true;
        if(!in_data)return fail(s,"Invalid salvage identity anchor");
        if(stop(s))return false;
        enum kui_read_result r=optical(s,fad,1,s->data);
        if(stop(s))return false;
        if(r!=KUI_READ_OK || kui_recovery_sector_check(s->data,KUI_RAW_BYTES,fad))return fail(s,"Disc identity anchor is unreadable; no saved data changed");
        uint8_t digest[32];struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,s->data,KUI_RAW_BYTES);kui_sha256_digest(&hash,digest);
        if(memcmp(digest,b+32+a*32,32))return fail(s,"Inserted disc bytes do not match this salvage job");
    }
    s->zero_fill=get32(b+20)!=0;return true;
}
static void identity(struct salvage *s) {struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,s->header,HEADER_BYTES);kui_sha256_digest(&hash,s->identity);}
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
    if(s->sequence>=JOURNAL_LIMIT/EVENT_BYTES)return fail(s,"Salvage journal limit reached");
    memset(s->event,0,EVENT_BYTES);memcpy(s->event,"KUISJNL1",8);
    put64(s->event+8,s->sequence+1);put32(s->event+16,type);put32(s->event+20,track);
    put32(s->event+24,sector);put32(s->event+28,count);put32(s->event+32,crc);
    memcpy(s->event+40,s->identity,32);put32(s->event+508,kui_crc32(0,s->event,508));
    if(!write_exact(s,&s->journal,s->event,EVENT_BYTES) || !sync_file(s,&s->journal) ||
       !seek(s,&s->journal,s->sequence*EVENT_BYTES) || !read_exact(s,&s->journal,s->scratch,EVENT_BYTES))return false;
    if(memcmp(s->scratch,s->event,EVENT_BYTES))return fail(s,"Salvage journal readback mismatch");
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
        if(s->ready || !s->zero_fill || track!=active || track>=s->plan->count || count || crc ||
           s->pending_count==CHUNK || s->target_count+s->pending_count>=KUI_SALVAGE_TARGET_LIMIT ||
           sector<s->sectors[track] || sector>=s->sectors[track]+CHUNK || sector>=s->plan->tracks[track].end-s->plan->tracks[track].start ||
           (s->pending_count && sector<=s->pending[s->pending_count-1]))return false;
        if(!s->pending_count)s->pending_offset=s->sequence*EVENT_BYTES;
        s->pending[s->pending_count++]=sector;
    } else if(type==EV_CHUNK) {
        if(s->ready || track!=active || track>=s->plan->count || sector!=s->sectors[track] || !count || count>CHUNK ||
           count>s->plan->tracks[track].end-s->plan->tracks[track].start-sector)return false;
        for(unsigned i=0;i<s->pending_count;++i) {
            if(s->pending[i]>=sector+count)return false;
            s->target[s->target_count++]=(struct target){s->pending[i],0,(uint16_t)track,false};
        }
        s->pending_count=0;s->sectors[track]+=count;s->crc[track]=crc;
    } else if(type==EV_BASELINE) {
        if(s->ready || s->pending_count || track!=active || track>=s->plan->count || sector || count ||
           s->sectors[track]!=s->plan->tracks[track].end-s->plan->tracks[track].start || crc!=s->crc[track])return false;
        s->baseline[track]=crc;s->baselined[track]=true;
    } else if(type==EV_READY) {
        if(s->ready || s->pending_count || active!=s->plan->count || track || sector || count!=s->target_count || crc)return false;
        s->ready=true;
    } else if(type==EV_REPAIRED) {
        if(!s->ready || track>=s->target_count || sector || count || s->target[track].repaired)return false;
        s->target[track].repaired=true;s->target[track].crc=crc;
    } else if(type==EV_COMPLETE) {
        if(!s->ready || track || sector || count!=s->target_count || crc)return false;
        for(unsigned i=0;i<s->target_count;++i)if(!s->target[i].repaired)return false;
        s->completed=true;
    } else return false;
    ++s->sequence;return true;
}
static bool replay(struct salvage *s) {
    FSIZE_t length=f_size(&s->journal);
    if(length>JOURNAL_LIMIT)return fail(s,"Salvage journal is oversized");
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
        valid+=EVENT_BYTES;
    }
    if(s->pending_count) {valid=s->pending_offset;s->sequence=valid/EVENT_BYTES;s->pending_count=0;}
    /* No committed progress depends on bytes after this boundary. All optical
     * anchors were checked before journal truncation or any track mutation. */
    if(!seek(s,&s->journal,valid))return false;
    if(valid!=length && (f_truncate(&s->journal)!=FR_OK || !sync_file(s,&s->journal)))return fail(s,"Cannot discard uncommitted journal tail");
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
    s->journal_open=true;return fresh?sync_file(s,&s->journal):replay(s);
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
static bool first_pass(struct salvage *s) {
    for(unsigned ti=current_track(s);ti<s->plan->count;++ti) {
        const struct kui_capture_track *t=&s->plan->tracks[ti];uint32_t total=t->end-t->start;
        s->status->track=ti+1;track_path(s,ti);FIL file;
        if(f_open(&file,s->path,FA_READ|FA_WRITE|FA_OPEN_ALWAYS)!=FR_OK)return fail(s,"Cannot open salvage track for first pass");
        bool ok=seek(s,&file,(uint64_t)s->sectors[ti]*KUI_RAW_BYTES);
        if(ok && f_truncate(&file)!=FR_OK)ok=fail(s,"Cannot discard uncommitted track tail");
        while(ok && s->sectors[ti]<total) {
            if(stop(s)) {ok=false;break;}
            uint32_t begin=s->sectors[ti];unsigned count=total-begin;if(count>CHUNK)count=CHUNK;
            bool bad[CHUNK]={false};enum kui_read_result r=optical(s,t->start+begin,count,s->data);
            if(s->failed || stop(s)) {ok=false;break;}
            for(unsigned n=0;n<count;++n)bad[n]=r!=KUI_READ_OK || (t->control==4 && kui_recovery_sector_check(s->data+n*KUI_RAW_BYTES,KUI_RAW_BYTES,t->start+begin+n));
            for(unsigned n=0;n<count && ok;++n)if(bad[n]) {
                bool good=false;
                /* Retry only failed sectors. Scratch output preserves the
                 * rest of a successfully returned batch. */
                for(unsigned attempt=0;attempt<2 && !good && !s->failed && !stop(s);++attempt) {
                    r=optical(s,t->start+begin+n,1,s->scratch);
                    good=r==KUI_READ_OK && (t->control!=4 || kui_recovery_sector_check(s->scratch,KUI_RAW_BYTES,t->start+begin+n)==0);
                }
                if(s->failed || stop(s)) {ok=false;break;}
                if(good) {memcpy(s->data+n*KUI_RAW_BYTES,s->scratch,KUI_RAW_BYTES);bad[n]=false;}
                else if(!s->zero_fill)ok=fail(s,"Unreadable sector; zero-fill is OFF, so this chunk was not committed");
                else memset(s->data+n*KUI_RAW_BYTES,0,KUI_RAW_BYTES);
            }
            if(!ok)break;
            unsigned holes=0;for(unsigned n=0;n<count;++n)holes+=bad[n];
            if(holes>KUI_SALVAGE_TARGET_LIMIT-s->target_count) {ok=fail(s,"Target limit reached; committed data preserved");break;}
            for(unsigned n=0;n<count && ok;++n)if(bad[n])ok=append(s,EV_TARGET,ti,begin+n,0,0);
            if(!ok)break;
            uint32_t crc=kui_crc32(s->crc[ti],s->data,count*KUI_RAW_BYTES);
            ok=write_exact(s,&file,s->data,count*KUI_RAW_BYTES) && sync_file(s,&file) && append(s,EV_CHUNK,ti,begin,count,crc);
            if(!ok)break;
            for(unsigned n=0;n<count;++n)if(bad[n])s->target[s->target_count++]=(struct target){begin+n,0,(uint16_t)ti,false};
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
static bool patch_valid(struct salvage *s,unsigned index) {
    const struct target *t=&s->target[index];const uint8_t *p=s->patch;
    if(memcmp(p,"KUISPAT1",8) || get32(p+8)!=index || get32(p+12)!=t->track || get32(p+16)!=t->sector ||
       get32(p+20)!=(s->plan->tracks[t->track].control==4?1u:2u) || memcmp(p+32,s->identity,32) ||
       get32(p+PATCH_BYTES-4)!=kui_crc32(0,p,PATCH_BYTES-4))return fail(s,"Invalid published sector backup/candidate; refusing repair");
    /* First-pass targets always contain zeros; immutable original bytes and
     * validated candidate are retained together, never regenerated later. */
    for(unsigned i=64;i<64+KUI_RAW_BYTES;++i)if(p[i])return fail(s,"Sector backup disagrees with the immutable zero baseline");
    if(get32(p+24)!=kui_crc32(0,p+64,KUI_RAW_BYTES) || get32(p+28)!=kui_crc32(0,p+64+KUI_RAW_BYTES,KUI_RAW_BYTES))return fail(s,"Sector backup CRC mismatch");
    for(unsigned i=64+2*KUI_RAW_BYTES;i<PATCH_BYTES-4;++i)if(p[i])return fail(s,"Unknown sector backup fields");
    if(s->plan->tracks[t->track].control==4 && kui_recovery_sector_check(p+64+KUI_RAW_BYTES,KUI_RAW_BYTES,s->plan->tracks[t->track].start+t->sector))return fail(s,"Stored repair candidate no longer passes Mode 1 checks");
    return true;
}
static bool load_patch(struct salvage *s,unsigned index,bool *exists) {
    char name[32];snprintf(name,sizeof(name),"patch-%04u.bin",index);path(s,name);
    FILINFO info;FRESULT r=f_stat(s->path,&info);*exists=r==FR_OK;
    if(r==FR_NO_FILE)return true;
    if(r!=FR_OK)return fail(s,"Cannot inspect repair candidate");
    return exact_file(s,name,s->patch,PATCH_BYTES) && patch_valid(s,index);
}
static bool publish_patch(struct salvage *s,unsigned index) {
    struct target *t=&s->target[index];char temporary[32],name[32],oldpath[KUI_DEST_PATH_CAP];
    snprintf(temporary,sizeof(temporary),"patch-%04u.part",index);snprintf(name,sizeof(name),"patch-%04u.bin",index);
    memset(s->patch,0,PATCH_BYTES);memcpy(s->patch,"KUISPAT1",8);put32(s->patch+8,index);put32(s->patch+12,t->track);
    put32(s->patch+16,t->sector);put32(s->patch+20,s->plan->tracks[t->track].control==4?1:2);memcpy(s->patch+32,s->identity,32);
    /* Caller has checked the on-card original bytes are zero before accepting
     * any optical candidate; these exact original bytes are preserved. */
    memcpy(s->patch+64,s->scratch,KUI_RAW_BYTES);memcpy(s->patch+64+KUI_RAW_BYTES,s->data,KUI_RAW_BYTES);
    put32(s->patch+24,kui_crc32(0,s->patch+64,KUI_RAW_BYTES));put32(s->patch+28,kui_crc32(0,s->data,KUI_RAW_BYTES));
    put32(s->patch+PATCH_BYTES-4,kui_crc32(0,s->patch,PATCH_BYTES-4));
    path(s,temporary);FRESULT r=f_unlink(s->path);if(r!=FR_OK && r!=FR_NO_FILE)return fail(s,"Cannot discard unpublished repair candidate");
    if(!write_new(s,temporary,s->patch,PATCH_BYTES) || !exact_file(s,temporary,s->patch,PATCH_BYTES) || !patch_valid(s,index))return false;
    if(memcmp(s->patch+64+KUI_RAW_BYTES,s->data,KUI_RAW_BYTES))return fail(s,"Stored repair candidate differs from the optical candidate");
    path(s,temporary);snprintf(oldpath,sizeof(oldpath),"%s",s->path);path(s,name);
    if(f_rename(oldpath,s->path)!=FR_OK)return fail(s,"Cannot publish verified repair candidate");
    if(!exact_file(s,name,s->patch,PATCH_BYTES) || !patch_valid(s,index))return false;
    if(memcmp(s->patch+64+KUI_RAW_BYTES,s->data,KUI_RAW_BYTES))return fail(s,"Published repair candidate changed");
    return true;
}
static bool read_target(struct salvage *s,unsigned index) {
    struct target *t=&s->target[index];track_path(s,t->track);FIL file;
    if(f_open(&file,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot read target sector from saved track");
    bool ok=seek(s,&file,(uint64_t)t->sector*KUI_RAW_BYTES) && read_exact(s,&file,s->scratch,KUI_RAW_BYTES);
    return io_close(s,&file,ok);
}
static bool apply_patch(struct salvage *s,unsigned index) {
    struct target *t=&s->target[index];track_path(s,t->track);FIL file;
    if(f_open(&file,s->path,FA_READ|FA_WRITE)!=FR_OK)return fail(s,"Cannot open target track for repair");
    uint64_t offset=(uint64_t)t->sector*KUI_RAW_BYTES;const uint8_t *replacement=s->patch+64+KUI_RAW_BYTES;
    bool ok=seek(s,&file,offset) && write_exact(s,&file,replacement,KUI_RAW_BYTES) && sync_file(s,&file) &&
        seek(s,&file,offset) && read_exact(s,&file,s->scratch,KUI_RAW_BYTES);
    if(ok && memcmp(s->scratch,replacement,KUI_RAW_BYTES))ok=fail(s,"Repair readback mismatch; durable candidate retained");
    ok=io_close(s,&file,ok);if(!ok)return false;
    uint32_t crc=get32(s->patch+28);if(!append(s,EV_REPAIRED,index,0,0,crc))return false;
    t->crc=crc;t->repaired=true;update(s,true);return true;
}
static bool reconcile(struct salvage *s) {
    if(!s->ready)return true;
    snprintf(s->status->message,sizeof(s->status->message),"Checking durable targets and interrupted patches");
    for(unsigned i=0;i<s->target_count;++i) {
        if(stop(s))return false;
        struct target *t=&s->target[i];bool exists;
        if(!load_patch(s,i,&exists) || !read_target(s,i))return false;
        if(t->repaired) {
            if(!exists || t->crc!=get32(s->patch+28) || memcmp(s->scratch,s->patch+64+KUI_RAW_BYTES,KUI_RAW_BYTES))return fail(s,"Previously repaired sector or its backup changed; no complete result");
        } else if(exists) {
            /* The saved, validated candidate makes an interrupted track write
             * idempotent, including a torn sector. Never count it twice. */
            if(!apply_patch(s,i))return false;
        } else {
            for(unsigned n=0;n<KUI_RAW_BYTES;++n)if(s->scratch[n])return fail(s,"Unresolved placeholder changed without a durable repair candidate");
        }
        update(s,false);
    }
    return true;
}
static bool repair(struct salvage *s) {
    for(unsigned pass=1;pass<=s->options->passes;++pass) {
        s->status->pass=pass;unsigned unresolved=0;
        for(unsigned n=0;n<s->target_count;++n) {
            unsigned i=(pass&1)?n:s->target_count-1-n;struct target *t=&s->target[i];
            if(t->repaired)continue;
            ++unresolved;if(stop(s))return false;
            s->status->track=t->track+1;s->status->fad=s->plan->tracks[t->track].start+t->sector;
            snprintf(s->status->message,sizeof(s->status->message),"Recovery pass %u/%u, target %u",pass,s->options->passes,i+1);update(s,true);
            if(!candidate(s,t->track,t->sector,true)) {if(s->failed || stop(s))return false;continue;}
            /* candidate() uses scratch for audio agreement. Load the original
             * saved sector only after candidate validation and keep its bytes. */
            if(!read_target(s,i))return false;
            for(unsigned j=0;j<KUI_RAW_BYTES;++j)if(s->scratch[j])return fail(s,"Placeholder changed before patch publication");
            if(!publish_patch(s,i) || !apply_patch(s,i))return false;
        }
        if(!unresolved)break;
        update(s,true);if(!s->status->remaining)break;
    }
    return true;
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
    size_t used=(size_t)snprintf(text,32768,"%u\n",s->plan->count);
    for(unsigned i=0;i<s->plan->count;++i) {
        const struct kui_capture_track *t=&s->plan->tracks[i];
        int n=snprintf(text+used,32768-used,"%u %" PRIu32 " %u 2352 track%02u.%s 0\n",i+1,t->start-150,(unsigned)t->control,i+1,t->control==4?"bin":"raw");
        if(n<0 || (size_t)n>=32768-used) {free(text);return fail(s,"Final descriptor exceeds its bound");}used+=(size_t)n;
    }
    bool ok=publish_text(s,"salvage.gdi",text,(unsigned)used);
    char hex[65];kui_hex(s->identity,32,hex);
    used=(size_t)snprintf(text,32768,"{\n  \"salvage_schema\":1,\n  \"identity\":\"%s\",\n  \"requires_complete_journal\":true,\n  \"full_saved_readback\":false,\n  \"original_targets\":%u,\n  \"unresolved\":0,\n  \"tracks\":[\n",hex,s->target_count);
    memset(s->scratch,0,KUI_RAW_BYTES);uint32_t zero_crc=kui_crc32(0,s->scratch,KUI_RAW_BYTES);
    for(unsigned i=0;i<s->plan->count;++i) {
        uint32_t crc=s->baseline[i];const struct kui_capture_track *t=&s->plan->tracks[i];
        for(unsigned n=0;n<s->target_count;++n)if(s->target[n].track==i) {
            uint64_t suffix=(uint64_t)(t->end-t->start-s->target[n].sector-1)*KUI_RAW_BYTES;
            crc=kui_recovery_crc_replace(crc,zero_crc,s->target[n].crc,suffix);
        }
        int n=snprintf(text+used,32768-used,"    {\"number\":%u,\"bytes\":%" PRIu64 ",\"crc32\":\"%08" PRIx32 "\"}%s\n",i+1,(uint64_t)(t->end-t->start)*KUI_RAW_BYTES,crc,i+1==s->plan->count?"":",");
        if(n<0 || (size_t)n>=32768-used) {free(text);return fail(s,"Final manifest exceeds its bound");}used+=(size_t)n;
    }
    memcpy(text+used,"  ]\n}\n",6);used+=6;
    if(ok)ok=publish_text(s,"salvage.json",text,(unsigned)used);
    free(text);
    if(ok && !s->completed) {ok=append(s,EV_COMPLETE,0,0,s->target_count,0);if(ok)s->completed=true;}
    return ok;
}
enum kui_salvage_result kui_salvage_run(const struct kui_capture_plan *plan,const struct kui_capture_ops *ops,
    const struct kui_salvage_options *options,enum kui_salvage_action action,struct kui_salvage_status *status) {
    if(!status)return KUI_SALVAGE_FAILED;
    memset(status,0,sizeof(*status));
    if(!plan_valid(plan) || !ops || !ops->read || !ops->log || !options || action<KUI_SALVAGE_NEW || action>KUI_SALVAGE_RECOVER ||
       (options->passes!=1 && options->passes!=5 && options->passes!=10 && options->passes!=20 && options->passes!=50)) {
        snprintf(status->message,sizeof(status->message),"Invalid salvage request or pass limit");return status->result;
    }
    struct salvage *s=calloc(1,sizeof(*s));if(!s) {snprintf(status->message,sizeof(status->message),"Not enough memory for bounded salvage state");return status->result;}
    s->plan=plan;s->ops=ops;s->options=options;s->status=status;status->tracks=plan->count;status->total=plan->bytes;status->pass_limit=options->passes;
    s->started=ops->now_ms?ops->now_ms(ops->ctx):0;
    FATFS fs;bool mounted=false,ok=false;
    if(stop(s))goto done;
    if(action==KUI_SALVAGE_NEW && !make_header(s))goto done;
    if(!kui_mount(&fs,ops->log)) {fail(s,"Cannot mount SD card for salvage");goto done;}mounted=true;
    if(action==KUI_SALVAGE_NEW) {if(!make_job(s))goto done;identity(s);}
    else {if(!job_path(s,options->job) || !exact_file(s,"header.bin",s->header,HEADER_BYTES) || !validate_header(s))goto done;identity(s);}
    if(stop(s) || !open_journal(s,action==KUI_SALVAGE_NEW) || !check_lengths(s))goto done;
    ops->log("Salvage job: %s; zero placeholders %s; normal captures are untouched",status->job,s->zero_fill?"explicitly enabled":"OFF");
    if(action==KUI_SALVAGE_RECOVER && !s->ready) {fail(s,"First pass is incomplete; Resume it before targeted recovery");goto done;}
    if(!reconcile(s))goto done;
    if(!s->ready && !first_pass(s))goto done;
    if(action==KUI_SALVAGE_RECOVER && !repair(s))goto done;
    if(stop(s) || !finish(s))goto done;
    ok=true;
done:
    if(s->journal_open && !io_close(s,&s->journal,true))ok=false;
    if(mounted && f_mount(NULL,"0:",0)!=FR_OK) {fail(s,"Card unmount failed; operation is not successful");ok=false;}
    update(s,true);
    if(s->failed)status->result=KUI_SALVAGE_FAILED;
    else if(!ok)status->result=stop(s)?KUI_SALVAGE_STOPPED:KUI_SALVAGE_FAILED;
    else status->result=s->completed?KUI_SALVAGE_RESOLVED:KUI_SALVAGE_UNRESOLVED;
    status->complete=ok && !s->failed && s->completed;
    if(status->result==KUI_SALVAGE_STOPPED)snprintf(status->message,sizeof(status->message),"Stopped; committed good data and repair candidates retained");
    else if(status->result==KUI_SALVAGE_RESOLVED)snprintf(status->message,sizeof(status->message),"All targets resolved; PC saved-file verification is still required");
    else if(status->result==KUI_SALVAGE_UNRESOLVED)snprintf(status->message,sizeof(status->message),"%" PRIu32 " unresolved sectors; choose Recover for targeted passes",status->remaining);
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
            valid=valid && !memcmp(b,"KUISALV1",8) && get32(b+8)==1 && get32(b+12)==HEADER_BYTES && get32(b+16)==plan->count &&
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
        /* Discovery skips a structurally matching but different disc. Read
         * failures are not retried across other jobs: the drive may be fatal. */
        bool same=true;
        for(unsigned a=0;a<2;++a) {
            uint32_t fad=get32(s->header+24+a*4);bool in_data=false;
            for(unsigned i=0;i<plan->count;++i)if(plan->tracks[i].control==4 && plan->tracks[i].session==1 && fad>=plan->tracks[i].start && fad<plan->tracks[i].end)in_data=true;
            if(!in_data || fad==get32(s->header+24+(1-a)*4)) {same=false;break;}
            enum kui_read_result read=optical(s,fad,1,s->data);
            if(stop(s)) {same=false;break;}
            if(read!=KUI_READ_OK) {fail(s,"Cannot read salvage identity anchor during discovery");same=false;break;}
            uint8_t digest[32];struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,s->data,KUI_RAW_BYTES);kui_sha256_digest(&hash,digest);
            if(kui_recovery_sector_check(s->data,KUI_RAW_BYTES,fad) || memcmp(digest,s->header+32+a*32,32)) {same=false;break;}
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
