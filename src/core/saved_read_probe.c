/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/saved_read_probe.h"
#include "kui/hash.h"
#include "kui/recovery_checks.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_BYTES 4096u
#define RAW_BYTES 2352u
#define PAYLOAD_OFFSET (64u+RAW_BYTES)
struct saved_read_probe {
    const struct kui_saved_read_probe_ops *ops;
    struct kui_saved_read_probe_status *out;
    FATFS fs;
    uint8_t header[HEADER_BYTES],storage[KUI_SAVED_READ_RECORD_BYTES+32u];
    uint8_t identity[32],reference_digest[32];
    struct {uint32_t control,session,start,end,toc_end;} tracks[99];
    char folder[KUI_DEST_JOB_CAP],path[KUI_DEST_PATH_CAP];
    uint64_t started,last_progress,total_sectors;
    unsigned count,version;
    bool reference_valid;
};
_Static_assert(sizeof(struct saved_read_probe)<32u*1024u,"Saved-read probe allocation must stay bounded");
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t clock_ms(struct saved_read_probe *s) {return s->ops->now_ms(s->ops->ctx);}
static void update(struct saved_read_probe *s,bool force) {
    uint64_t now=clock_ms(s);s->out->elapsed_ms=now>=s->started?now-s->started:0;
    if(s->ops->progress && (force || now-s->last_progress>=100u)) {
        s->last_progress=now;s->ops->progress(s->ops->ctx,s->out);
    }
}
static bool fail(struct saved_read_probe *s,const char *message) {
    bool secondary=s->out->failed;
    if(!secondary)snprintf(s->out->message,sizeof(s->out->message),"%s",message);
    s->out->failed=true;
    s->ops->log("SAVED READ %s: %s",secondary?"secondary failure":"probe",message);return false;
}
static bool interrupted(struct saved_read_probe *s) {
    if(s->ops->cancelled && s->ops->cancelled(s->ops->ctx)) {
        s->out->stopped=true;return true;
    }
    uint64_t now=clock_ms(s);
    if(now>=s->started && now-s->started>=KUI_SAVED_READ_BUDGET_MS) {
        s->out->budget_expired=true;return true;
    }
    return false;
}
static void path(struct saved_read_probe *s,const char *name) {
    snprintf(s->path,sizeof(s->path),"0:%s/%s",s->folder,name);
}
static bool close_file(struct saved_read_probe *s,FIL *f) {
    uint64_t offset=f_tell(f);FRESULT result=f_close(f);
    if(result==FR_OK)return true;
    s->ops->log("SAVED READ close: profile=%u slot=%" PRIu32 " offset=%" PRIu64 " FatFs=%u secondary=%u",
        s->out->profile,s->out->current_slot,offset,(unsigned)result,s->out->failed?1u:0u);
    return fail(s,"Saved file close failed; comparison incomplete");
}
static bool exact_read(struct saved_read_probe *s,FIL *f,void *data,UINT bytes) {
    uint64_t offset=f_tell(f);UINT got=0;FRESULT result=f_read(f,data,bytes,&got);
    if(result!=FR_OK || got!=bytes) {
        s->ops->log("SAVED READ I/O: profile=%u slot=%" PRIu32 " offset=%" PRIu64 " requested=%u returned=%u FatFs=%u",
            s->out->profile,s->out->current_slot,offset,bytes,got,(unsigned)result);
        return fail(s,"Saved file read failed or was short; comparison stopped");
    }
    return true;
}
static bool read_header(struct saved_read_probe *s,bool initial) {
    path(s,"header.bin");FIL file;
    if(f_open(&file,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot open saved salvage header read-only");
    bool ok=f_size(&file)==HEADER_BYTES;
    if(!ok)fail(s,"Saved salvage header has an invalid length");
    if(ok)ok=exact_read(s,&file,s->header,HEADER_BYTES);
    if(!close_file(s,&file))ok=false;
    if(!ok)return false;
    uint8_t digest[32];struct kui_sha256 hash;kui_sha256_init(&hash);
    kui_sha256_update(&hash,s->header,HEADER_BYTES);kui_sha256_digest(&hash,digest);
    if(!initial) {
        if(memcmp(digest,s->identity,32)) {
            s->out->inconsistent=true;return fail(s,"Saved header observations changed; repair remains blocked");
        }
        return true;
    }
    const uint8_t *h=s->header;s->version=get32(h+8);s->count=get32(h+16);
    if((s->version!=2 && s->version!=3) ||
       memcmp(h,s->version==3?"KUISALV3":"KUISALV2",8) ||
       get32(h+12)!=HEADER_BYTES || !s->count || s->count>99 || get32(h+20)>1 ||
       get32(h+HEADER_BYTES-4)!=kui_crc32(0,h,HEADER_BYTES-4))
        return fail(s,"Invalid or unsupported packed salvage header");
    if(s->version==3 && get32(h+4064)!=1u)return fail(s,"Unsupported saved CD capture profile");
    for(unsigned at=96+s->count*24;at<HEADER_BYTES-4;++at)
        if(!(s->version==3 && at>=4064 && at<4068) && h[at])
            return fail(s,"Unknown saved salvage header fields");
    for(unsigned i=0;i<s->count;++i) {
        const uint8_t *t=h+96+i*24;
        s->tracks[i].control=get32(t+4);s->tracks[i].session=get32(t+8);
        s->tracks[i].start=get32(t+12);s->tracks[i].end=get32(t+16);s->tracks[i].toc_end=get32(t+20);
        uint32_t begin=s->tracks[i].start,end=s->tracks[i].end;
        if(get32(t)!=i+1u || (s->tracks[i].control!=0 && s->tracks[i].control!=4) ||
           s->tracks[i].session>1 || begin<150 || begin>=end || end>s->tracks[i].toc_end ||
           s->tracks[i].toc_end>KUI_RECOVERY_FAD_MAX+1u ||
           (i && begin<s->tracks[i-1].end) ||
           (s->version==3 && (s->tracks[i].session!=0 || end!=s->tracks[i].toc_end ||
                             (i && begin!=s->tracks[i-1].end))))
            return fail(s,"Saved salvage track bounds are invalid");
        s->total_sectors+=end-begin;
    }
    if(get32(h+24)==get32(h+28) || (s->version==3 && !memcmp(h+32,h+64,32)))
        return fail(s,"Saved identity anchors are not distinct");
    for(unsigned a=0;a<2;++a) {
        uint32_t fad=get32(h+24+a*4);bool found=false;
        for(unsigned i=0;i<s->count;++i)
            if((s->version==3 || (s->tracks[i].control==4 && s->tracks[i].session==1)) &&
               fad>=s->tracks[i].start && fad<s->tracks[i].end)found=true;
        if(!found)return fail(s,"Saved identity anchor is outside its track layout");
    }
    memcpy(s->identity,digest,32);return true;
}
static const char *record_problem(struct saved_read_probe *s,const uint8_t *p) {
    if(memcmp(p,"KUISPAT2",8))return "magic/version";
    uint32_t index=get32(p+8),track=get32(p+12),sector=get32(p+16);
    if(index>=s->total_sectors)return "target index";
    if(track>=s->count)return "track";
    if(sector>=s->tracks[track].end-s->tracks[track].start)return "sector";
    if(get32(p+20)!=(s->tracks[track].control==4?1u:2u))return "evidence type";
    if(memcmp(p+32,s->identity,32))return "job identity";
    if(get32(p+KUI_SAVED_READ_RECORD_BYTES-4)!=kui_crc32(0,p,KUI_SAVED_READ_RECORD_BYTES-4))return "record CRC";
    for(unsigned i=64;i<PAYLOAD_OFFSET;++i)if(p[i])return "nonzero baseline";
    if(get32(p+24)!=kui_crc32(0,p+64,RAW_BYTES))return "baseline CRC";
    if(get32(p+28)!=kui_crc32(0,p+PAYLOAD_OFFSET,RAW_BYTES))return "payload CRC";
    for(unsigned i=PAYLOAD_OFFSET+RAW_BYTES;i<KUI_SAVED_READ_RECORD_BYTES-4;++i)
        if(p[i])return "reserved bytes";
    if(s->tracks[track].control==4) {
        uint32_t fad=s->tracks[track].start+sector;
        unsigned bad=s->version==3?kui_recovery_cd_sector_check(p+PAYLOAD_OFFSET,RAW_BYTES,fad):
            kui_recovery_sector_check(p+PAYLOAD_OFFSET,RAW_BYTES,fad);
        if(bad)return "data sector validation";
    }
    return NULL;
}
static bool observation(struct saved_read_probe *s,uint32_t slot,const uint8_t *p,const char *problem) {
    unsigned profile=s->out->profile;
    if(!s->out->first_bad_seen) {
        s->out->first_bad_seen=true;s->out->first_bad_profile=profile;s->out->first_bad_slot=slot;
        snprintf(s->out->first_problem,sizeof(s->out->first_problem),"%s",problem);
    }
    s->ops->log("SAVED READ invalid: profile=%u slot=%" PRIu32 " offset=%" PRIu64 " alignment=%u problem=%s",
        profile,slot,(uint64_t)slot*KUI_SAVED_READ_RECORD_BYTES,(unsigned)((uintptr_t)p&31u),problem);
    s->ops->log("SAVED READ fields: index=%" PRIu32 " track=%" PRIu32 " sector=%" PRIu32 " type=%" PRIu32,
        get32(p+8),get32(p+12),get32(p+16),get32(p+20));
    s->ops->log("SAVED READ CRC record stored=%08" PRIx32 " observed=%08" PRIx32 " payload stored=%08" PRIx32 " observed=%08" PRIx32,
        get32(p+KUI_SAVED_READ_RECORD_BYTES-4),kui_crc32(0,p,KUI_SAVED_READ_RECORD_BYTES-4),
        get32(p+28),kui_crc32(0,p+PAYLOAD_OFFSET,RAW_BYTES));
    for(unsigned at=0;at<KUI_SAVED_READ_RECORD_BYTES;at+=512u)
        s->ops->log("SAVED READ block=%u CRC32=%08" PRIx32,at/512u,kui_crc32(0,p+at,512));
    if(s->ops->observe) {
        struct kui_sha256 hash;uint8_t before[32],after[32];
        kui_sha256_init(&hash);kui_sha256_update(&hash,p,KUI_SAVED_READ_RECORD_BYTES);kui_sha256_digest(&hash,before);
        uint32_t crc=kui_crc32(0,p,KUI_SAVED_READ_RECORD_BYTES);
        s->ops->observe(s->ops->ctx,profile,slot,problem,p);
        kui_sha256_init(&hash);kui_sha256_update(&hash,p,KUI_SAVED_READ_RECORD_BYTES);kui_sha256_digest(&hash,after);
        if(crc!=kui_crc32(0,p,KUI_SAVED_READ_RECORD_BYTES) || memcmp(before,after,32))
            return fail(s,"Invalid-record buffer changed during evidence capture; comparison stopped");
    }
    return true;
}
const char *kui_saved_read_probe_profile_name(unsigned profile) {
    static const char *const names[]={"Bulk aligned","512-byte aligned","Bulk unaligned","512-byte unaligned"};
    return profile<KUI_SAVED_READ_PROFILES?names[profile]:"Invalid";
}
unsigned kui_saved_read_probe_profile_at_step(unsigned step) {
    static const unsigned order[KUI_SAVED_READ_PROFILES]={2u,3u,0u,1u};
    return step<KUI_SAVED_READ_PROFILES?order[step]:KUI_SAVED_READ_PROFILES;
}
static bool compare_profile(struct saved_read_probe *s,unsigned profile) {
    s->out->profile=profile;s->out->current_slot=0;s->out->tried[profile]=true;
    snprintf(s->out->message,sizeof(s->out->message),"Comparing saved repairs: %s",kui_saved_read_probe_profile_name(profile));
    update(s,true);
    if(interrupted(s) || !read_header(s,false))return false;
    path(s,"patches.bin");FIL file;
    if(f_open(&file,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot open packed candidates read-only");
    bool ok=true,invalid=false;
    if(f_size(&file)!=(uint64_t)s->out->records*KUI_SAVED_READ_RECORD_BYTES) {
        s->out->inconsistent=true;ok=fail(s,"Packed candidate length changed between observations");
    }
    uint8_t *data=(uint8_t *)(((uintptr_t)s->storage+31u)&~(uintptr_t)31u);
    if(profile>=2u)++data;
    const unsigned request=(profile&1u)?512u:KUI_SAVED_READ_RECORD_BYTES;
    struct kui_sha256 hash;kui_sha256_init(&hash);
    s->ops->log("SAVED READ profile=%u label=%s request_bytes=%u alignment=%u; fresh read-only handle",
        profile,kui_saved_read_probe_profile_name(profile),request,(unsigned)((uintptr_t)data&31u));
    for(uint32_t slot=0;ok && slot<s->out->records;++slot) {
        if(interrupted(s)) {ok=false;break;}
        s->out->current_slot=slot;update(s,false);
        for(unsigned at=0;at<KUI_SAVED_READ_RECORD_BYTES;at+=request) {
            if(interrupted(s)) {ok=false;break;}
            uint64_t begin=clock_ms(s);
            ok=exact_read(s,&file,data+at,request);
            uint64_t end=clock_ms(s);
            if(end>=begin)s->out->read_ms[profile]+=end-begin;
            if(!ok || interrupted(s)) {ok=false;break;}
        }
        if(!ok)break;
        ++s->out->processed[profile];++s->out->done;
        const char *problem=record_problem(s,data);
        if(problem) {
            ++s->out->invalids[profile];s->out->inconsistent=true;invalid=true;
            if(!observation(s,slot,data,problem))ok=false;
            break;
        }
        kui_sha256_update(&hash,data,KUI_SAVED_READ_RECORD_BYTES);
    }
    if(!close_file(s,&file))ok=false;
    if(!ok)return false;
    if(!invalid) {
        s->out->full[profile]=true;kui_sha256_digest(&hash,s->out->digest[profile]);
        char hex[65];kui_hex(s->out->digest[profile],32,hex);
        s->ops->log("SAVED READ profile=%u records=%" PRIu32 " read_ms=%" PRIu64 " SHA256=%s",
            profile,s->out->processed[profile],s->out->read_ms[profile],hex);
        if(!s->reference_valid) {
            memcpy(s->reference_digest,s->out->digest[profile],32);s->reference_valid=true;
        } else if(memcmp(s->reference_digest,s->out->digest[profile],32)) {
            s->out->inconsistent=true;
            s->ops->log("SAVED READ full-file hashes differ; self-valid candidate observations changed; repair remains blocked");
        }
    }
    update(s,true);return !interrupted(s);
}
bool kui_saved_read_probe_run(const char *job,const struct kui_saved_read_probe_ops *ops,
    struct kui_saved_read_probe_status *out) {
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!job || !ops || !ops->now_ms || !ops->log) {
        out->failed=true;snprintf(out->message,sizeof(out->message),"Invalid saved-read probe request");return false;
    }
    struct saved_read_probe *s=calloc(1,sizeof(*s));
    if(!s) {out->failed=true;snprintf(out->message,sizeof(out->message),"Not enough memory for saved-read probe");return false;}
    s->ops=ops;s->out=out;s->started=clock_ms(s);
    bool mounted=false,finished=false;
    if(!kui_destination_job_normalize(s->folder,job)) {fail(s,"Invalid saved salvage folder");goto done;}
    snprintf(out->job,sizeof(out->job),"%s",s->folder);
    if(interrupted(s))goto done;
    snprintf(out->message,sizeof(out->message),"Preparing read-only saved repair comparison");update(s,true);
    if(!kui_mount(&s->fs,ops->log)) {fail(s,"Cannot mount card for saved-read probe");goto done;}
    mounted=true;
    if(interrupted(s) || !read_header(s,true))goto done;
    path(s,"patches.bin");FIL file;
    if(f_open(&file,s->path,FA_READ)!=FR_OK) {fail(s,"Cannot open saved packed candidates");goto done;}
    uint64_t bytes=f_size(&file);
    bool valid=bytes && !(bytes%KUI_SAVED_READ_RECORD_BYTES) &&
        bytes/KUI_SAVED_READ_RECORD_BYTES<=s->total_sectors;
    if(!valid)fail(s,"Packed candidates are empty, truncated or exceed the saved layout");
    if(!close_file(s,&file) || !valid)goto done;
    out->records=(uint32_t)(bytes/KUI_SAVED_READ_RECORD_BYTES);out->total=(uint64_t)out->records*KUI_SAVED_READ_PROFILES;
    ops->log("SAVED READ start folder=%s records=%" PRIu32 " budget_ms=%u; source files read-only; quarantine unchanged",
        s->folder,out->records,KUI_SAVED_READ_BUDGET_MS);
    ops->log("SAVED READ scope: candidate self-checks and read consistency only; journal/tracks not verified; no repair authority");
    for(unsigned step=0;step<KUI_SAVED_READ_PROFILES;++step)
        if(!compare_profile(s,kui_saved_read_probe_profile_at_step(step)))goto done;
    finished=true;
done:
    if(mounted && f_mount(NULL,"0:",0)!=FR_OK) {fail(s,"Card unmount failed; saved-read comparison incomplete");finished=false;}
    if(out->failed || out->stopped || out->budget_expired)finished=false;
    out->complete=finished;
    if(!out->failed && out->stopped)snprintf(out->message,sizeof(out->message),"Saved-read probe stopped; job and quarantine unchanged");
    else if(!out->failed && out->budget_expired)snprintf(out->message,sizeof(out->message),"Saved-read probe budget reached; comparison incomplete");
    else if(finished)snprintf(out->message,sizeof(out->message),"%s",out->inconsistent?
        "Saved-read differences detected; repair remains blocked":
        "Four candidate observations agree; repair quarantine unchanged");
    ops->log("SAVED READ end complete=%u inconsistent=%u failed=%u stopped=%u budget=%u; no repair or quarantine changes",
        out->complete?1u:0u,out->inconsistent?1u:0u,out->failed?1u:0u,out->stopped?1u:0u,out->budget_expired?1u:0u);
    update(s,true);bool ok=finished && !out->inconsistent;free(s);return ok;
}
