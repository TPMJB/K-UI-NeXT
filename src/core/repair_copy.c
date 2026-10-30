/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/repair_copy.h"
#include "kui/recovery_checks.h"
#include "kui/recovery_guard.h"
#include "kui/legacy_import.h"
#include "kui/raw_cue.h"
#include "kui/saved_identity.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COPY_SECTORS 16u
#define EVIDENCE_HEADER 64u
#define REPAIR_STATE_BYTES 8192u
struct recovery_copy {
    struct kui_scan_manifest manifest;
    struct kui_scan_gdi source;
    struct kui_legacy_job *legacy;
    struct kui_saved_identity saved_identity;
    bool saved_identity_active,identity_unreadable;
    uint32_t trusted_crc[99];uint8_t trusted_sha[99][32];
    const struct kui_capture_plan *optical_plan;
    const struct kui_capture_ops *optical_ops;
    const struct kui_repair_options *repair_options;
    const struct kui_forensic_ops *forensic_ops;
    struct kui_repair_status *repair;
    struct kui_forensic_status *forensic;
    char directory[KUI_DEST_JOB_CAP],job[KUI_DEST_JOB_CAP],path[KUI_DEST_PATH_CAP];
    char metadata[KUI_SCAN_MANIFEST_LIMIT+1u];
    uint8_t data[COPY_SECTORS*KUI_RAW_BYTES],samples[3][KUI_RAW_BYTES];
    uint8_t candidate[KUI_RAW_BYTES],header[EVIDENCE_HEADER];
    uint32_t crc[99];uint8_t sha[99][32];
    uint32_t committed[99],available[99],initial_crc[99];
    uint8_t initial_sha[99][32],binding[32],state[REPAIR_STATE_BYTES];
    uint64_t state_sequence,committed_evidence,committed_records;
    bool partial_source,resume_enabled,resuming,log_final,sha_valid[99];
    bool in_place;
    unsigned inplace_marker_version;
    uint8_t hole_record[32];
    uint8_t *baseline_targets;uint32_t baseline_offsets[100];
    bool baseline_collect,baseline_ready,baseline_partial_valid;
    unsigned baseline_partial_track;uint32_t baseline_partial_sectors,baseline_partial_crc;
    struct kui_sha256 baseline_partial_hash;
    uint32_t verified_prefix_crc[99];uint8_t verified_prefix_sha[99][32];bool verified_prefix[99];
    bool pending_loaded,pending_committed;
    unsigned pending_track;uint32_t pending_sector;
    bool chunk_loaded,chunk_committed;
    unsigned chunk_track;uint32_t chunk_sector,chunk_count;
    FIL log,evidence;
    bool log_open,evidence_open,failed,unresolved,manifest_present;
    uint64_t started,last_progress,evidence_records;
};
_Static_assert(sizeof(struct recovery_copy)<160u*1024u,"Repair memory is bounded");
static bool load_partial_source(struct recovery_copy *s);
static bool repair_job(struct recovery_copy *s);
static bool repair_commit(struct recovery_copy *s);
static bool repair_prefix(struct recovery_copy *s,unsigned ti,struct kui_sha256 *hash,uint32_t *crc);
static bool repair_pending(struct recovery_copy *s);
static bool repair_unresolved(struct recovery_copy *s,unsigned ti);
static bool inplace_binding(struct recovery_copy *s);
static bool inplace_ready(struct recovery_copy *s);
static bool inplace_publish(struct recovery_copy *s);
static bool inplace_start_fresh(struct recovery_copy *s);
static bool inplace_original_path(struct recovery_copy *s,unsigned index,bool *present);
static kui_log_fn logger(struct recovery_copy *s) {return s->repair?s->optical_ops->log:s->forensic_ops->log;}
static bool cancelled(struct recovery_copy *s) {
    return s->repair?(s->optical_ops->cancelled && s->optical_ops->cancelled(s->optical_ops->ctx)):
        (s->forensic_ops->cancelled && s->forensic_ops->cancelled(s->forensic_ops->ctx));
}
static uint64_t now_ms(struct recovery_copy *s) {
    return s->repair?(s->optical_ops->now_ms?s->optical_ops->now_ms(s->optical_ops->ctx):0):
        (s->forensic_ops->now_ms?s->forensic_ops->now_ms(s->forensic_ops->ctx):0);
}
static void update(struct recovery_copy *s,bool force) {
    uint64_t now=now_ms(s),elapsed=now>=s->started?now-s->started:0;
    if(s->repair)s->repair->elapsed_ms=elapsed;else s->forensic->elapsed_ms=elapsed;
    if(!force && now && now-s->last_progress<100)return;
    s->last_progress=now;
    if(s->repair) {if(s->repair_options->progress)s->repair_options->progress(s->optical_ops->ctx,s->repair);}
    else if(s->forensic_ops->progress)s->forensic_ops->progress(s->forensic_ops->ctx,s->forensic);
}
static bool fail(struct recovery_copy *s,const char *message) {
    s->failed=true;
    snprintf(s->repair?s->repair->message:s->forensic->message,128,"%s",message);
    logger(s)("%s: %s",s->repair?"Repair copy":"Storage rereads",message);return false;
}
static bool line(struct recovery_copy *s,const char *format,...) {
    char text[768];va_list args;va_start(args,format);int n=vsnprintf(text,sizeof(text),format,args);va_end(args);
    if(n<0 || (size_t)n>=sizeof(text))return fail(s,"Evidence line is too long");
    UINT put=0;if(f_write(&s->log,text,(UINT)n,&put)!=FR_OK || put!=(UINT)n)return fail(s,"Evidence log write failed");
    return true;
}
static bool close_file(struct recovery_copy *s,FIL *f,bool ok) {
    if(f_close(f)!=FR_OK)return fail(s,"Storage close failed; output is incomplete");
    return ok;
}
static bool exact_read(struct recovery_copy *s,FIL *f,void *data,unsigned bytes) {
    UINT got=0;if(f_read(f,data,bytes,&got)!=FR_OK || got!=bytes)return fail(s,"Storage read failed or track is truncated");return true;
}
static bool exact_write(struct recovery_copy *s,FIL *f,const void *data,unsigned bytes) {
    UINT put=0;if(f_write(f,data,bytes,&put)!=FR_OK || put!=bytes)return fail(s,"Storage write failed or volume is full");return true;
}
static bool sync_file(struct recovery_copy *s,FIL *f) {
    if(f_sync(f)!=FR_OK)return fail(s,"Storage sync failed; partial evidence retained");
    return true;
}
static void source_path(struct recovery_copy *s,const char *name) {
    size_t a=strlen(s->directory),b=strlen(name);
    if(a+b+4u>sizeof(s->path)) {s->path[0]=0;(void)fail(s,"Source path exceeds repair bound");return;}
    memcpy(s->path,"0:",2);memcpy(s->path+2,s->directory,a);s->path[a+2]='/';memcpy(s->path+a+3,name,b+1);
}
static void job_path(struct recovery_copy *s,const char *name) {
    if(s->in_place)for(unsigned i=0;i<s->source.plan.count;++i) {
        char canonical[32];snprintf(canonical,sizeof(canonical),"track%02u.%s",i+1,s->source.plan.tracks[i].control==4?"bin":"raw");
        if(!strcmp(name,canonical)) {source_path(s,s->source.files[i]);return;}
    }
    snprintf(s->path,sizeof(s->path),"0:%s/%s",s->job,name);
}
static void track_name(char name[32],unsigned i,const struct kui_capture_track *t) {snprintf(name,32,"track%02u.%s",i+1,t->control==4?"bin":"raw");}
static bool metadata_read(struct recovery_copy *s,const char *name,size_t *size) {
    if(name)source_path(s,name);
    FIL f;if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot open saved dump metadata");
    uint64_t n=f_size(&f);bool ok=n && n<=KUI_SCAN_MANIFEST_LIMIT;
    if(!ok)fail(s,"Saved dump metadata has an invalid length");
    if(ok)ok=exact_read(s,&f,s->metadata,(unsigned)n);
    if(!close_file(s,&f,ok))return false;
    s->metadata[n]=0;*size=(size_t)n;return true;
}
static bool gdi_extension(const char *name) {
    size_t n=strlen(name);if(n<5)return false;name+=n-4;
    return name[0]=='.' && (name[1]=='g'||name[1]=='G') && (name[2]=='d'||name[2]=='D') && (name[3]=='i'||name[3]=='I');
}
static bool cue_extension(const char *name) {
    size_t n=strlen(name);if(n<5)return false;name+=n-4;
    return name[0]=='.' && (name[1]=='c'||name[1]=='C') && (name[2]=='u'||name[2]=='U') && (name[3]=='e'||name[3]=='E');
}
static bool load_source(struct recovery_copy *s,const char *directory) {
    if(!kui_destination_job_normalize(s->directory,directory))return fail(s,"Select the actual saved game folder");
    source_path(s,"manifest.json");FILINFO info;FRESULT r=f_stat(s->path,&info);size_t n=0;
    const char *manifest_name="manifest.json";
    if(s->in_place) {
        source_path(s,".kui-inplace-active");FIL marker;FRESULT guard=f_open(&marker,s->path,FA_READ);
        if(guard==FR_OK) {
            uint8_t record[512];bool valid=f_size(&marker)==512 && exact_read(s,&marker,record,512) &&
                ((!memcmp(record,"KUIIPG1",8) && record[8]==1) || (!memcmp(record,"KUIIPG2",8) && record[8]==2)) &&
                kui_crc32(0,record,508)==((uint32_t)record[508]|(uint32_t)record[509]<<8|(uint32_t)record[510]<<16|(uint32_t)record[511]<<24);
            if(!close_file(s,&marker,valid))return false;
            if(!valid)return fail(s,"In-place active guard is invalid");
            s->inplace_marker_version=record[8];
            if(record[8]==2) {
                const char *job=(const char *)record+64;size_t cap=256;
                const char *end=memchr(job,0,cap);
                if(!end || strncmp(job,"/KUI/repair/job-",16) || (size_t)(end-job)!=20u ||
                   job[16]<'0' || job[16]>'9' || job[17]<'0' || job[17]>'9' || job[18]<'0' || job[18]>'9' || job[19]<'0' || job[19]>'9')return fail(s,"In-place guard job path is invalid");
                memcpy(s->job,job,21);memcpy(s->binding,record+16,32);bool present=false;
                if(!inplace_original_path(s,0,&present))return false;
                r=present?FR_OK:FR_NO_FILE;manifest_name=present?NULL:"manifest.json";
                if(present && record[12])return fail(s,"Partial-origin guard conflicts with original manifest");
            } else if(record[12])r=FR_NO_FILE;
            else {source_path(s,"manifest.json.kui-repair-original");FRESULT backup=f_stat(s->path,&info);if(backup==FR_OK) {r=FR_OK;manifest_name="manifest.json.kui-repair-original";}else if(backup!=FR_NO_FILE)return fail(s,"Cannot inspect original in-place metadata");}
        } else if(guard!=FR_NO_FILE)return fail(s,"Cannot inspect in-place recovery guard");
    }
    if(r==FR_OK) {
        s->manifest_present=true;
        if(!metadata_read(s,manifest_name,&n))return false;
        if(!kui_recovery_manifest_parse(s->metadata,n,&s->manifest))return fail(s,"Not a supported completed raw-track manifest");
        if(!metadata_read(s,s->manifest.gdi,&n) || !kui_recovery_gdi_parse(s->metadata,n,&s->source))
            return fail(s,"Completed capture GDI is missing or unsupported");
        if(s->source.plan.count!=s->manifest.plan.count)return fail(s,"GDI differs from capture manifest");
        for(unsigned i=0;i<s->source.plan.count;++i) {
            const struct kui_capture_track *a=&s->source.plan.tracks[i],*b=&s->manifest.plan.tracks[i];char name[32];track_name(name,i,b);
            if(a->number!=b->number || a->control!=b->control || a->start!=b->start || strcmp(name,s->source.files[i]))
                return fail(s,"GDI track names/layout differ from capture manifest");
        }
        s->source.plan=s->manifest.plan;
    } else if(r==FR_NO_FILE) {
        if(s->repair) {
            s->legacy=calloc(1,sizeof(*s->legacy));if(!s->legacy)return fail(s,"Not enough memory to admit legacy source");
            enum kui_legacy_result legacy=kui_legacy_import(s->directory,s->optical_plan,s->optical_ops,s->legacy);
            if(legacy==KUI_LEGACY_READY) {
                s->source=s->legacy->source;s->partial_source=s->legacy->partial;
                memcpy(s->available,s->legacy->available,sizeof(s->available));
                if(s->in_place)return fail(s,"Legacy jobs require separate-copy import before independent in-place repair");
                s->repair->tracks=s->source.plan.count;s->repair->total=s->source.plan.bytes;s->repair->partial_source=s->partial_source;s->repair->imported=true;
                return true;
            }
            if(legacy!=KUI_LEGACY_ABSENT)return legacy==KUI_LEGACY_STOPPED?false:fail(s,s->legacy->message);
            free(s->legacy);s->legacy=NULL;
            source_path(s,"checkpoint-a.bin");FRESULT a=f_stat(s->path,&info);
            source_path(s,"checkpoint-b.bin");FRESULT b=f_stat(s->path,&info);
            if((a!=FR_OK && a!=FR_NO_FILE) || (b!=FR_OK && b!=FR_NO_FILE))return fail(s,"Cannot inspect partial capture checkpoints");
            if(a==FR_OK || b==FR_OK)return load_partial_source(s);
        }
        DIR d;source_path(s,"");if(f_opendir(&d,s->path)!=FR_OK)return fail(s,"Cannot open selected dump folder");
        char selected[KUI_DEST_NAME_CAP]={0};unsigned found=0;bool ok=true;
        for(;;) {
            if(cancelled(s)) {ok=false;break;}
            r=f_readdir(&d,&info);if(r!=FR_OK) {ok=fail(s,"Cannot read selected dump folder");break;}
            if(!info.fname[0])break;
            if(!(info.fattrib&AM_DIR) && (gdi_extension(info.fname) || (s->repair && cue_extension(info.fname)))) {
                ++found;if(strlen(info.fname)>=sizeof(selected)) {ok=fail(s,"Imported GDI name is too long");break;}
                memcpy(selected,info.fname,strlen(info.fname)+1);
            }
        }
        if(f_closedir(&d)!=FR_OK)ok=fail(s,"Cannot close selected dump folder");
        if(!ok)return false;
        if(found!=1)return fail(s,"Select a dump folder containing exactly one GDI");
        if(!metadata_read(s,selected,&n) || !(gdi_extension(selected)?kui_recovery_gdi_parse(s->metadata,n,&s->source):kui_raw_cue_parse(s->metadata,n,s->optical_plan,&s->source)))
            return fail(s,"Imported GDI requires raw 2352 tracks, zero offsets and safe names");
        if(gdi_extension(selected)) {
            if(strlen(selected)>=sizeof(s->manifest.gdi)) {if(s->in_place)return fail(s,"Imported descriptor name is too long for in-place evidence; use Copy");}
            else memcpy(s->manifest.gdi,selected,strlen(selected)+1);
        } else {
            if(strlen(selected)>=sizeof(s->manifest.cue)) {if(s->in_place)return fail(s,"Imported CUE name is too long for in-place evidence; use Copy");}
            else memcpy(s->manifest.cue,selected,strlen(selected)+1);
        }
        s->source.plan.bytes=0;
        for(unsigned i=0;i<s->source.plan.count;++i) {
            source_path(s,s->source.files[i]);r=f_stat(s->path,&info);
            if(r!=FR_OK)return fail(s,"Cannot inspect imported raw track");
            struct kui_capture_track *t=&s->source.plan.tracks[i];uint64_t sectors=(uint64_t)info.fsize/KUI_RAW_BYTES;
            if((info.fattrib&AM_DIR) || !sectors || info.fsize%KUI_RAW_BYTES ||
               sectors>KUI_RECOVERY_FAD_MAX+1u-t->start ||
               (i+1<s->source.plan.count && t->start+sectors>s->source.plan.tracks[i+1].start))
                return fail(s,"Imported raw track length is invalid or overlaps another track");
            t->end=t->toc_end=t->start+(uint32_t)sectors;s->source.plan.bytes+=info.fsize;
        }
    } else return fail(s,"Cannot inspect saved dump manifest");
    for(unsigned i=0;i<s->source.plan.count;++i) {
        if(cancelled(s))return false;
        source_path(s,s->source.files[i]);r=f_stat(s->path,&info);
        const struct kui_capture_track *t=&s->source.plan.tracks[i];
        if(r!=FR_OK || (info.fattrib&AM_DIR) || (uint64_t)info.fsize!=(uint64_t)(t->end-t->start)*KUI_RAW_BYTES)
            return fail(s,"Raw track size differs from the saved layout");
        s->available[i]=t->end-t->start;
    }
    if(s->repair) {s->repair->tracks=s->source.plan.count;s->repair->total=s->source.plan.bytes;s->repair->imported=!s->manifest_present;}
    else {s->forensic->tracks=s->source.plan.count;s->forensic->total=s->source.plan.bytes;}
    return true;
}
static unsigned check_sector(struct recovery_copy *s,const uint8_t *raw,uint32_t fad) {
    return s->optical_plan && s->optical_plan->profile==KUI_CAPTURE_PROFILE_CD_TOC?
        kui_recovery_cd_sector_check(raw,KUI_RAW_BYTES,fad):kui_recovery_sector_check(raw,KUI_RAW_BYTES,fad);
}
#include "repair_baseline.inc"
static bool read_saved_sector(struct recovery_copy *s,unsigned ti,uint32_t sector,uint8_t *out) {
    source_path(s,s->source.files[ti]);FIL f;if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot reopen saved suspect sector");
    uint64_t offset=(uint64_t)sector*KUI_RAW_BYTES;
    bool ok=f_lseek(&f,(FSIZE_t)offset)==FR_OK && f_tell(&f)==offset;
    if(!ok)fail(s,"Cannot seek saved suspect sector");
    if(ok)ok=exact_read(s,&f,out,KUI_RAW_BYTES);
    return close_file(s,&f,ok);
}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(8*i));}
static void hash32(struct kui_sha256 *hash,uint32_t n) {uint8_t b[4];put32(b,n);kui_sha256_update(hash,b,4);}
static bool optical_sector(struct recovery_copy *s,uint32_t fad,uint8_t *out) {
    if(cancelled(s))return false;
    ++s->repair->attempts;s->repair->fad=fad;
    enum kui_read_result r=s->optical_ops->read(s->optical_ops->ctx,fad,1,out);
    if(r==KUI_READ_FATAL)return fail(s,"Optical fatal/reset-required result; no further reads");
    return r==KUI_READ_OK && !cancelled(s);
}
#include "repair_saved_identity.inc"
#include "repair_identity.inc"
static bool new_job(struct recovery_copy *s) {
    const char *parent=s->repair?"0:/KUI/repair":"0:/KUI/recovery";
    FRESULT r=f_mkdir("0:/KUI");if(r!=FR_OK && r!=FR_EXIST)return fail(s,"Cannot create evidence parent");
    r=f_mkdir(parent);if(r!=FR_OK && r!=FR_EXIST)return fail(s,"Cannot create evidence folder");
    for(unsigned i=1;i<=9999;++i) {
        if(cancelled(s))return false;
        snprintf(s->job,sizeof(s->job),s->repair?"/KUI/repair/job-%04u":"/KUI/recovery/readback-%04u",i);
        snprintf(s->path,sizeof(s->path),"0:%s",s->job);r=f_mkdir(s->path);
        if(r==FR_EXIST)continue;
        if(r!=FR_OK)return fail(s,"Cannot create unique evidence job");
        if(s->repair)snprintf(s->repair->job,sizeof(s->repair->job),"%s",s->job);
        job_path(s,"evidence.part");
        if(f_open(&s->log,s->path,FA_WRITE|FA_CREATE_NEW)!=FR_OK)return fail(s,"Cannot create new evidence log");
        s->log_open=true;
        if(s->forensic)snprintf(s->forensic->report,sizeof(s->forensic->report),"%s",s->path);
        job_path(s,"samples.bin");
        if(f_open(&s->evidence,s->path,FA_READ|FA_WRITE|FA_CREATE_NEW)!=FR_OK)return fail(s,"Cannot create binary sample evidence");
        s->evidence_open=true;
        if(s->forensic)snprintf(s->forensic->evidence,sizeof(s->forensic->evidence),"%s",s->path);
        if(!line(s,"K-UI %s v1\nSOURCE 0:%s\n%s\n"
            "samples.bin: 64-byte KUIRDB1/KUIRPT1 headers, then three raw2352 samples; header/data CRC32.\n"
            ".part is incomplete; no successful result without COMPLETE.\n",
            s->repair?(s->in_place?"in-place target repair":"repair-to-copy"):"saved-sector reopen evidence",s->directory,s->in_place?"Explicit in-place action; original/candidate evidence precedes replacements.":"Original files are READ ONLY."))return false;
        if(s->saved_identity_active && !line(s,"IDENTITY alternate saved-anchor recovery binding; original capture identity unreadable; trusted source prefixes verified; immutable recovery anchors retained\n"))return false;
        if(!line(s,"TRACKS %u bytes=%" PRIu64 " reference_hashes=%s\n",
            s->source.plan.count,s->source.plan.bytes,s->manifest_present?"capture manifest":"none"))return false;
        return sync_file(s,&s->log) && sync_file(s,&s->evidence);
    }
    return fail(s,"All evidence job numbers are occupied");
}
static uint32_t differences(const uint8_t *a,const uint8_t *b,uint32_t *first) {
    uint32_t count=0;*first=UINT32_MAX;
    for(uint32_t i=0;i<KUI_RAW_BYTES;++i)if(a[i]!=b[i]) {if(!count)*first=i;++count;}
    return count;
}
static bool evidence_record(struct recovery_copy *s,const char magic[8],unsigned ti,uint32_t sector,unsigned flags) {
    memset(s->header,0,sizeof(s->header));memcpy(s->header,magic,8);
    put32(s->header+8,1);put32(s->header+12,(uint32_t)++s->evidence_records);
    put32(s->header+16,ti+1);put32(s->header+20,s->source.plan.tracks[ti].start+sector);
    put32(s->header+24,sector);put32(s->header+28,flags);put32(s->header+32,3);
    for(unsigned i=0;i<3;++i)put32(s->header+36+i*4,kui_crc32(0,s->samples[i],KUI_RAW_BYTES));
    put32(s->header+48,kui_crc32(0,s->samples,sizeof(s->samples)));
    put32(s->header+60,kui_crc32(0,s->header,60));
    FSIZE_t offset=f_tell(&s->evidence);
    if(!exact_write(s,&s->evidence,s->header,sizeof(s->header)) ||
       !exact_write(s,&s->evidence,s->samples,sizeof(s->samples)) || !sync_file(s,&s->evidence))return false;
    if(f_lseek(&s->evidence,offset)!=FR_OK || f_tell(&s->evidence)!=offset ||
       !exact_read(s,&s->evidence,s->metadata,sizeof(s->header)+sizeof(s->samples)))return fail(s,"Cannot read back durable binary evidence");
    if(memcmp(s->metadata,s->header,sizeof(s->header)) ||
       memcmp(s->metadata+sizeof(s->header),s->samples,sizeof(s->samples)))return fail(s,"Binary evidence readback mismatch; repair blocked");
    if(kui_crc32(0,s->samples,sizeof(s->samples))!=((uint32_t)s->header[48]|(uint32_t)s->header[49]<<8|(uint32_t)s->header[50]<<16|(uint32_t)s->header[51]<<24))return fail(s,"Binary evidence source changed during write");
    return true;
}
static bool reopen_evidence(struct recovery_copy *s,unsigned ti,uint32_t sector,const uint8_t *first,unsigned flags,bool *stable) {
    memcpy(s->samples[0],first,KUI_RAW_BYTES);*stable=true;
    for(unsigned i=1;i<3;++i) {
        if(cancelled(s) || !read_saved_sector(s,ti,sector,s->samples[i]))return false;
        uint32_t at=0,diff=differences(s->samples[0],s->samples[i],&at);
        if(diff)*stable=false;
        if(!line(s,"REREAD track=%02u fad=%" PRIu32 " pass=%u different_bytes=%" PRIu32 " first_offset=%" PRIu32
            " initial_crc32=%08" PRIx32 " reread_crc32=%08" PRIx32 "\n",ti+1,s->source.plan.tracks[ti].start+sector,i,diff,at,
            kui_crc32(0,s->samples[0],KUI_RAW_BYTES),kui_crc32(0,s->samples[i],KUI_RAW_BYTES)))return false;
    }
    return evidence_record(s,"KUIRDB1",ti,sector,flags) && sync_file(s,&s->log);
}
static bool read_candidate(struct recovery_copy *s,unsigned ti,uint32_t sector) {
    uint32_t fad=s->source.plan.tracks[ti].start+sector;
    for(unsigned attempt=0;attempt<s->repair_options->attempts;++attempt) {
        if(cancelled(s) || s->failed)return false;
        if(!optical_sector(s,fad,s->candidate))continue;
        if(s->source.plan.tracks[ti].control==4 && check_sector(s,s->candidate,fad))continue;
        if(!optical_sector(s,fad,s->samples[2]))continue;
        if((s->source.plan.tracks[ti].control==4 && check_sector(s,s->samples[2],fad)) || memcmp(s->candidate,s->samples[2],KUI_RAW_BYTES))continue;
        /* Preserve one immutable original and both agreed optical reads before
         * any candidate bytes reach the new track file. */
        memcpy(s->samples[1],s->candidate,KUI_RAW_BYTES);
        if(!evidence_record(s,"KUIRPT1",ti,sector,0) || !line(s,"CANDIDATE track=%02u fad=%" PRIu32
            " two_valid_identical_reads original_crc32=%08" PRIx32 " candidate_crc32=%08" PRIx32 "\n",ti+1,fad,
            kui_crc32(0,s->samples[0],KUI_RAW_BYTES),kui_crc32(0,s->candidate,KUI_RAW_BYTES)) || !sync_file(s,&s->log))return false;
        return true;
    }
    return false;
}
static bool full_readback(struct recovery_copy *s,unsigned ti,const char *name,uint64_t wanted,uint32_t crc,const uint8_t digest[32]) {
    s->repair->phase=KUI_REPAIR_READBACK;update(s,true);job_path(s,name);FIL f;
    if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot reopen copied track for full readback");
    bool ok=f_size(&f)==wanted;if(!ok)fail(s,"Copied track length differs before readback");
    uint32_t actual=0,prefix_crc=0;uint64_t done=0;unsigned cd_mode=0;
    uint64_t prefix_bytes=(uint64_t)s->available[ti]*KUI_RAW_BYTES;
    struct kui_sha256 sha,prefix_sha;kui_sha256_init(&sha);kui_sha256_init(&prefix_sha);s->verified_prefix[ti]=false;
    while(ok && done<wanted) {
        if(cancelled(s)) {ok=false;break;}
        unsigned bytes=(unsigned)(wanted-done>sizeof(s->data)?sizeof(s->data):wanted-done);
        if(!exact_read(s,&f,s->data,bytes)) {ok=false;break;}
        if(s->source.plan.profile==KUI_CAPTURE_PROFILE_CD_TOC && s->source.plan.tracks[ti].control==4) {
            for(unsigned at=0;at<bytes;at+=KUI_RAW_BYTES) {
                const uint8_t *raw=s->data+at;uint32_t fad=s->source.plan.tracks[ti].start+(uint32_t)((done+at)/KUI_RAW_BYTES);
                if(check_sector(s,raw,fad))continue; /* tracked unresolved placeholders are not a CUE mode */
                unsigned mode=raw[15];if(cd_mode && cd_mode!=mode) {ok=fail(s,"Mixed CD data-sector modes cannot be described by one CUE track");break;}cd_mode=mode;
            }
            if(!ok)break;
        }
        if(done<prefix_bytes) {unsigned n=(unsigned)(prefix_bytes-done>bytes?bytes:prefix_bytes-done);
            prefix_crc=kui_crc32(prefix_crc,s->data,n);kui_sha256_update(&prefix_sha,s->data,n);}
        actual=kui_crc32(actual,s->data,bytes);kui_sha256_update(&sha,s->data,bytes);done+=bytes;update(s,false);
    }
    if(!close_file(s,&f,ok))return false;
    if(cd_mode) {s->manifest.track[ti].sector_mode=(uint8_t)cd_mode;s->manifest.track[ti].mode_recorded=true;}
    uint8_t saved[32];kui_sha256_digest(&sha,saved);
    if(actual!=crc || (digest && memcmp(saved,digest,32)))return fail(s,"Copied track saved-byte readback mismatch");
    s->verified_prefix_crc[ti]=prefix_crc;kui_sha256_digest(&prefix_sha,s->verified_prefix_sha[ti]);s->verified_prefix[ti]=true;
    memcpy(s->sha[ti],saved,32);s->sha_valid[ti]=true;
    return line(s,"READBACK track=%02u bytes=%" PRIu64 " CRC32=%08" PRIx32 " SHA256=%s\n",ti+1,wanted,crc,digest?"MATCH":"COMPUTED") && sync_file(s,&s->log);
}
#include "repair_resume.inc"
#include "repair_holes.inc"
#include "repair_catalogue_report.inc"
#include "repair_catalogue.inc"
#include "repair_inplace.inc"

static bool repair_expected(struct recovery_copy *s,unsigned ti) {
    if(!s->manifest_present || !s->available[ti])return true;
    bool initially_matched=s->initial_crc[ti]==s->manifest.track[ti].crc32 &&
        (s->manifest.crc_only || !memcmp(s->initial_sha[ti],s->manifest.track[ti].sha256,32));
    /* A capture can have faithfully recorded structurally bad bytes. Their
     * validated replacements get new hashes; preserve capture provenance.
     * Storage corruption of a formerly trusted prefix must restore its hash. */
    if(initially_matched)return true;
    if(!s->verified_prefix[ti])return fail(s,"Trusted source-prefix comparison requires completed saved-byte verification");
    uint32_t crc=s->verified_prefix_crc[ti];const uint8_t *digest=s->verified_prefix_sha[ti];
    if(crc!=s->manifest.track[ti].crc32 || (!s->manifest.crc_only && memcmp(digest,s->manifest.track[ti].sha256,32))) {
        ++s->repair->unresolved;s->unresolved=true;
        return line(s,"UNRESOLVED track=%02u trusted source-prefix hash not restored; cannot localize remaining bytes\n",ti+1);
    }
    return true;
}
static bool copy_track(struct recovery_copy *s,unsigned ti) {
    const struct kui_capture_track *t=&s->source.plan.tracks[ti];uint32_t sectors=t->end-t->start;
    s->repair->track=ti+1;s->repair->phase=KUI_REPAIR_COPYING;
    if(!repair_unresolved(s,ti))return false;
    char name[32];track_name(name,ti,t);
    if(s->in_place && s->committed[ti]==sectors)return true;
    struct kui_sha256 sha;uint32_t crc;bool hash_known=true;
    if(!(s->in_place?repair_baseline_load(s,ti,&sha,&crc,&hash_known):repair_prefix(s,ti,&sha,&crc)))return false;
    FIL input,output;bool input_open=false;
    uint32_t position=s->committed[ti];
    if(position<s->available[ti]) {
        source_path(s,s->source.files[ti]);
        if(f_open(&input,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot open original track read-only");
        input_open=true;if(!repair_seek(s,&input,(uint64_t)position*KUI_RAW_BYTES)) {close_file(s,&input,false);return false;}
    }
    job_path(s,name);if(f_open(&output,s->path,FA_READ|FA_WRITE|FA_OPEN_ALWAYS)!=FR_OK) {
        if(input_open)close_file(s,&input,true);
        return fail(s,"Cannot open retained copied track");
    }
    bool ok=repair_seek(s,&output,(uint64_t)position*KUI_RAW_BYTES);
    if(ok && f_truncate(&output)!=FR_OK)ok=fail(s,"Cannot discard uncommitted copy tail");
    if(t->control==0 && (!s->manifest_present || s->available[ti]<sectors))s->repair->audio_unverified=true;
    while(ok && position<sectors) {
        if(cancelled(s)) {ok=false;break;}
        unsigned count=sectors-position;if(count>COPY_SECTORS)count=COPY_SECTORS;
        bool saved=position<s->available[ti];
        if(saved && count>s->available[ti]-position)count=s->available[ti]-position;
        if(saved && !exact_read(s,&input,s->data,count*KUI_RAW_BYTES)) {ok=false;break;}
        for(unsigned i=0;i<count && ok;++i) {
            if(cancelled(s)) {ok=false;break;}
            uint8_t *raw=s->data+i*KUI_RAW_BYTES;uint32_t fad=t->start+position+i;s->repair->fad=fad;
            if(!saved) {
                bool good=false;
                for(unsigned attempt=0;attempt<s->repair_options->attempts && !good && !s->failed && !cancelled(s);++attempt) {
                    if(!optical_sector(s,fad,raw))continue;
                    if(t->control==4 && check_sector(s,raw,fad))continue;
                    if(!optical_sector(s,fad,s->candidate))continue;
                    good=!memcmp(raw,s->candidate,KUI_RAW_BYTES) && (t->control!=4 || !check_sector(s,s->candidate,fad));
                }
                if(!good) {
                    if(s->failed || cancelled(s)) {ok=false;break;}
                    if(!s->repair_options->continuous) {ok=fail(s,"Missing source sector is still unreadable; resume retained copy");break;}
                    if(!repair_hole_declare(s,ti,position+i)) {ok=false;break;}
                    memset(raw,0,KUI_RAW_BYTES);++s->repair->suspects;++s->repair->unresolved;s->unresolved=true;
                }
            } else {
                unsigned flags=t->control==4?check_sector(s,raw,fad):0;
                if(s->legacy && kui_legacy_target_is_marked(s->legacy,ti,position+i))flags|=0x100u;
                if(flags) {
                    ++s->repair->suspects;bool stable=false;
                    if(!reopen_evidence(s,ti,position+i,raw,flags,&stable)) {ok=false;break;}
                    if(!stable) {s->repair->storage_unstable=true;(void)kui_recovery_guard_mark(s->directory,logger(s));ok=fail(s,"Saved-sector rereads disagree; optical repair blocked");break;}
                    if((flags&KUI_RECOVERY_SECTOR_UNSUPPORTED) || !read_candidate(s,ti,position+i)) {
                        if(s->failed || cancelled(s)) {ok=false;break;}
                        ++s->repair->unresolved;s->unresolved=true;
                        if(!line(s,"UNRESOLVED track=%02u fad=%" PRIu32 " flags=%02x\n",ti+1,fad,flags)) {ok=false;break;}
                    } else {memcpy(raw,s->candidate,KUI_RAW_BYTES);++s->repair->repaired;}
                }
            }
            if(t->control==4 && raw[15]>=1 && raw[15]<=2) {s->manifest.track[ti].sector_mode=raw[15];s->manifest.track[ti].mode_recorded=true;}
            s->repair->done+=KUI_RAW_BYTES;
        }
        if(!ok)break;
        unsigned bytes=count*KUI_RAW_BYTES;uint32_t next=kui_crc32(crc,s->data,bytes);
        if(!repair_chunk_publish(s,ti,position,count,next)) {ok=false;break;}
        if(!exact_write(s,&output,s->data,bytes) || !sync_file(s,&output)) {ok=false;break;}
        if(kui_crc32(crc,s->data,bytes)!=next) {ok=fail(s,"Repair write source buffer changed; committed prefix retained");break;}
        uint8_t check[4096];unsigned at=0;
        if(!repair_seek(s,&output,(uint64_t)position*KUI_RAW_BYTES)) {ok=false;break;}
        while(ok && at<bytes) {
            unsigned n=bytes-at>sizeof(check)?sizeof(check):bytes-at;
            if(!exact_read(s,&output,check,n)) {ok=false;break;}
            if(memcmp(check,s->data+at,n)) {ok=fail(s,"Copied chunk saved-byte readback mismatch");break;}
            at+=n;
        }
        if(!ok)break;
        crc=next;if(hash_known)kui_sha256_update(&sha,s->data,bytes);position+=count;
        s->committed[ti]=position;s->crc[ti]=crc;
        if(hash_known)kui_sha256_digest(&sha,s->sha[ti]);else memset(s->sha[ti],0,32);s->sha_valid[ti]=hash_known;
        if(!repair_commit(s) || !repair_chunk_retire(s)) {ok=false;break;}
        update(s,false);
    }
    if(ok)ok=sync_file(s,&output);
    if(!close_file(s,&output,ok))ok=false;
    if(input_open && !close_file(s,&input,ok))ok=false;
    if(!ok)return false;
    uint8_t digest[32]={0};if(hash_known)kui_sha256_digest(&sha,digest);s->crc[ti]=crc;memcpy(s->sha[ti],digest,32);s->sha_valid[ti]=hash_known;
    if(!s->in_place && (!full_readback(s,ti,name,(uint64_t)sectors*KUI_RAW_BYTES,crc,digest) || !repair_expected(s,ti)))return false;
    return repair_commit(s) && sync_file(s,&s->log);
}
static bool publish_new(struct recovery_copy *s,const char *temporary,const char *final,const void *data,unsigned bytes) {
    job_path(s,final);FILINFO info;FRESULT r=f_stat(s->path,&info);
    if(r==FR_OK) {
        FIL existing;if(f_open(&existing,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot inspect retained final metadata");
        bool same=f_size(&existing)==bytes;uint8_t check[512];unsigned at=0;
        while(same && at<bytes) {unsigned n=bytes-at>sizeof(check)?sizeof(check):bytes-at;same=exact_read(s,&existing,check,n) && !memcmp(check,(const uint8_t *)data+at,n);at+=n;}
        if(!same && !s->failed)fail(s,"Retained final metadata differs from repair state");
        return close_file(s,&existing,same);
    }
    if(r!=FR_NO_FILE)return fail(s,"Cannot inspect final repair metadata");
    job_path(s,temporary);r=f_unlink(s->path);if(r!=FR_OK && r!=FR_NO_FILE)return fail(s,"Cannot discard unpublished repair metadata");
    job_path(s,temporary);FIL f;if(f_open(&f,s->path,FA_WRITE|FA_CREATE_NEW)!=FR_OK)return fail(s,"Cannot create final copy metadata");
    bool ok=exact_write(s,&f,data,bytes) && sync_file(s,&f);
    if(!close_file(s,&f,ok))return false;
    job_path(s,temporary);char to[KUI_DEST_PATH_CAP];snprintf(to,sizeof(to),"0:%s/%s",s->job,final);
    if(f_rename(s->path,to)!=FR_OK)return fail(s,"Cannot publish final copy metadata");
    return true;
}
/* copy.txt binds saved bytes, not the mutable reference catalogue. Old v2
 * receipts retain their original reference observation verbatim; every other
 * byte must still match the regenerated immutable receipt. */
static bool copy_receipt_prefix(struct recovery_copy *s,size_t *used) {
    char expected[1024];const char *audio=s->repair->audio_unverified?"NO":"source hashes available";
    int n=snprintf(expected,sizeof(expected),"K-UI repair copy v3\nSOURCE 0:%s\n"
        "Completed readback of every saved copied byte. Original files unchanged.\n"
        "Imported audio verified=%s; catalogue observations are separate reports.\n",s->directory,audio);
    if(n<0 || (size_t)n>=sizeof(expected))return fail(s,"Copy receipt prefix exceeds bound");
    job_path(s,"copy.txt");FIL f;FRESULT r=f_open(&f,s->path,FA_READ);
    if(r==FR_NO_FILE) {memcpy(s->metadata,expected,(size_t)n);*used=(size_t)n;return true;}
    if(r!=FR_OK)return fail(s,"Cannot inspect immutable copy receipt");
    char prior[1024];unsigned bytes=(unsigned)(f_size(&f)<sizeof(prior)-1u?f_size(&f):sizeof(prior)-1u);
    bool ok=bytes && exact_read(s,&f,prior,bytes);if(!close_file(s,&f,ok))return false;prior[bytes]=0;
    char *tail=prior;for(unsigned line=0;line<4;++line) {tail=strchr(tail,'\n');if(!tail)return fail(s,"Immutable copy receipt prefix is truncated");++tail;}
    size_t prefix=(size_t)(tail-prior);bool accepted=prefix==(size_t)n && !memcmp(prior,expected,prefix);
    if(!accepted) {
        int head=snprintf(expected,sizeof(expected),"K-UI repair copy v2\nSOURCE 0:%s\n"
            "Completed readback of every saved copied byte. Original files unchanged.\n"
            "Imported audio verified=%s; ",s->directory,audio);
        if(head<0 || (size_t)head>=sizeof(expected))return fail(s,"Legacy receipt prefix exceeds bound");
        if(prefix>(size_t)head && !memcmp(prior,expected,(size_t)head)) {
            const char *suffix=prior+head;size_t rest=prefix-(size_t)head;
            const char *old="catalogue verification not performed.\n";
            accepted=rest==strlen(old) && !memcmp(suffix,old,rest);
            for(int result=KUI_KNOWN_ERROR;!accepted && result<=KUI_KNOWN_FULL_MATCH+1;++result) {
                const char *grade=result>KUI_KNOWN_FULL_MATCH?"not compared":kui_known_text((enum kui_known_result)result);
                int end=snprintf(expected,sizeof(expected),"independent catalogue result=%s.\n",grade);
                accepted=end>0 && rest==(size_t)end && !memcmp(suffix,expected,rest);
            }
        }
    }
    if(!accepted)return fail(s,"Immutable copy receipt header differs from repair state");
    memcpy(s->metadata,prior,prefix);*used=prefix;return true;
}
static bool publish_copy(struct recovery_copy *s) {
    if(cancelled(s))return false;
    s->repair->phase=KUI_REPAIR_PUBLISHING;update(s,true);
    if(cancelled(s))return false;
    size_t used=0;if(!copy_receipt_prefix(s,&used))return false;
    for(unsigned i=0;i<s->source.plan.count;++i) {
        const struct kui_capture_track *t=&s->source.plan.tracks[i];char digest[65];kui_hex(s->sha[i],32,digest);
        int n=snprintf(s->metadata+used,sizeof(s->metadata)-used,"TRACK %u bytes=%" PRIu64 " CRC32=%08" PRIx32 " SHA256=%s\n",
            i+1,(uint64_t)(t->end-t->start)*KUI_RAW_BYTES,s->crc[i],digest);
        if(n<0 || (size_t)n>=sizeof(s->metadata)-used)return fail(s,"Copy manifest exceeds buffer");
        used+=(size_t)n;
        if(s->manifest_present) {
            n=snprintf(s->metadata+used,sizeof(s->metadata)-used,"SOURCE_PREFIX %u sectors=%" PRIu32 " original_crc32=%08" PRIx32 " captured_crc32=%08" PRIx32 "\n",
                i+1,s->available[i],s->initial_crc[i],s->manifest.track[i].crc32);
            if(n<0 || (size_t)n>=sizeof(s->metadata)-used)return fail(s,"Source provenance exceeds copy manifest");
            used+=(size_t)n;
        }
    }
    if(!publish_new(s,"copy.part","copy.txt",s->metadata,(unsigned)used))return false;
    if(s->source.plan.profile==KUI_CAPTURE_PROFILE_CD_TOC) {
        uint8_t modes[99]={0};for(unsigned i=0;i<s->source.plan.count;++i)modes[i]=s->manifest.track[i].sector_mode;
        if(!kui_capture_cue_encode(&s->source.plan,modes,s->metadata,sizeof(s->metadata),&used))return fail(s,"Cannot encode repaired CD CUE");
        job_path(s,"cue.part");FRESULT old=f_unlink(s->path);if(old!=FR_OK && old!=FR_NO_FILE)return fail(s,"Cannot discard unfinished repaired CUE");
        FIL cue;if(f_open(&cue,s->path,FA_WRITE|FA_CREATE_NEW)!=FR_OK)return fail(s,"Cannot stage repaired CD CUE");
        bool saved=exact_write(s,&cue,s->metadata,(unsigned)used) && sync_file(s,&cue);if(!close_file(s,&cue,saved))return false;
    }
    used=(size_t)snprintf(s->metadata,sizeof(s->metadata),"%u\n",s->source.plan.count);
    for(unsigned i=0;i<s->source.plan.count;++i) {
        const struct kui_capture_track *t=&s->source.plan.tracks[i];char name[32];track_name(name,i,t);
        int n=snprintf(s->metadata+used,sizeof(s->metadata)-used,"%u %" PRIu32 " %" PRIu32 " 2352 %s 0\n",i+1,t->start-150,t->control,name);
        if(n<0 || (size_t)n>=sizeof(s->metadata)-used)return fail(s,"Copy GDI exceeds buffer");
        used+=(size_t)n;
    }
    if(!line(s,"COMPLETE repaired=%" PRIu32 " bytes=%" PRIu64 " audio_unverified=%s\n",s->repair->repaired,s->repair->done,s->repair->audio_unverified?"YES":"NO") || !sync_file(s,&s->log))return false;
    /* Keep the GDI staged until the binary evidence and log have both closed
     * successfully. A final rename below is the publication step. */
    job_path(s,"disc.part");FIL f;
    FRESULT removed=f_unlink(s->path);if(removed!=FR_OK && removed!=FR_NO_FILE)return fail(s,"Cannot discard unpublished copy descriptor");
    if(f_open(&f,s->path,FA_WRITE|FA_CREATE_NEW)!=FR_OK)return fail(s,"Cannot stage copy descriptor");
    bool ok=exact_write(s,&f,s->metadata,(unsigned)used) && sync_file(s,&f);
    return close_file(s,&f,ok);
}
static bool finish_logs(struct recovery_copy *s,bool success) {
    if(s->evidence_open) {bool ok=sync_file(s,&s->evidence);if(!close_file(s,&s->evidence,ok))success=false;s->evidence_open=false;}
    if(s->log_open) {
        if(!success) { (void)line(s,"INCOMPLETE %s\n",cancelled(s)?"STOPPED":s->unresolved?"UNRESOLVED":"FAILED");(void)f_sync(&s->log); }
        if(!close_file(s,&s->log,true))success=false;
        s->log_open=false;
        if(success && !s->failed && !s->log_final) {
            job_path(s,"evidence.part");char final[KUI_DEST_PATH_CAP];snprintf(final,sizeof(final),"0:%s/evidence.txt",s->job);
            if(f_rename(s->path,final)!=FR_OK) {fail(s,"Cannot finalize evidence report");success=false;}
            else if(s->forensic)snprintf(s->forensic->report,sizeof(s->forensic->report),"%s",final);
        }
    }
    return success && !s->failed;
}
enum kui_repair_result kui_repair_copy(const char *directory,const struct kui_capture_plan *prepared_plan,
    const struct kui_capture_ops *ops,const struct kui_repair_options *options,struct kui_repair_status *out) {
    if(!out)return KUI_REPAIR_FAILED;
    memset(out,0,sizeof(*out));
    if(!directory || !prepared_plan || !ops || !ops->read || !ops->log || !options || (options->new_copy && options->in_place) ||
       (options->attempts!=1 && options->attempts!=5 && options->attempts!=10 && options->attempts!=20 && options->attempts!=50) ||
       (options->continuous && options->passes!=0 && options->passes!=1 && options->passes!=5 && options->passes!=10 && options->passes!=20 && options->passes!=50)) {
        snprintf(out->message,sizeof(out->message),"Invalid repair-to-copy request");return out->result;
    }
    struct recovery_copy *s=calloc(1,sizeof(*s));if(!s) {snprintf(out->message,sizeof(out->message),"Not enough memory for repair copy");return out->result;}
    s->repair=out;s->in_place=options->in_place;out->in_place=s->in_place;s->optical_plan=prepared_plan;s->optical_ops=ops;s->repair_options=options;s->started=now_ms(s);
    FATFS fs;bool mounted=false,ok=false;out->phase=KUI_REPAIR_IDENTIFY;
    if(cancelled(s))goto done;
    if(!kui_mount(&fs,ops->log)) {fail(s,"Cannot mount volume for repair copy");goto done;}mounted=true;
    if(!load_source(s,directory) || cancelled(s) || !repair_saved_identity_prepare(s) || !optical_identity(s) || cancelled(s))goto done;
    {DWORD clusters[99];unsigned count=0;
    for(unsigned i=0;i<s->source.plan.count;++i)if(s->available[i]) {
        source_path(s,s->source.files[i]);FIL f;if(f_open(&f,s->path,FA_READ)!=FR_OK) {fail(s,"Cannot validate distinct source-track objects");goto done;}
        DWORD cluster=f.obj.sclust;if(!close_file(s,&f,true))goto done;
        for(unsigned n=0;n<count;++n)if(clusters[n]==cluster) {fail(s,"Source track names alias the same saved file");goto done;}
        clusters[count++]=cluster;
    }}
    if(!repair_job(s))goto done;
    out->resume_ready=true;
    if(s->in_place && !inplace_ready(s))goto done;
    s->unresolved=false;s->repair->unresolved=0;
    for(unsigned i=0;i<s->source.plan.count;++i)if(!copy_track(s,i))goto done;
    if(!options->continuous && s->legacy) {
        bool pending=repair_holes_pending(s);if(s->failed || cancelled(s))goto done;
        if(pending && s->resuming && !repair_holes(s,false))goto done;
        pending=repair_holes_pending(s);if(s->failed || cancelled(s))goto done;
        s->unresolved=pending;
    }
    if(options->continuous) {
        unsigned pass=0;
        for(;;) {
            bool pending=repair_holes_pending(s);
            if(s->failed || cancelled(s))goto done;
            if(!pending)break;
            if(options->passes && pass>=options->passes) {s->unresolved=true;goto unresolved;}
            out->pass=++pass;s->resuming=true;out->phase=KUI_REPAIR_COPYING;
            snprintf(out->message,sizeof(out->message),"Repair recovery pass %u; %s",pass,(pass&1u)?"forward":"reverse");update(s,true);
            if(ops->recover && (!ops->recover(ops->ctx,prepared_plan) || !optical_identity(s))) {if(!s->failed && !cancelled(s))fail(s,"Drive recovery or exact disc identity failed");goto done;}
            if(!repair_holes(s,(pass&1u)==0))goto done;
        }
        s->unresolved=false;out->unresolved=0;
    }
    if(s->in_place && (s->unresolved || out->unresolved))goto unresolved;
    if(s->in_place || options->continuous) {
        /* Baseline/pending-prefix checks establish expected CRCs. Target-only
         * repairs update them, so one final saved read validates current bytes
         * and computes SHA plus trusted-prefix hashes without a duplicate pass. */
        for(unsigned i=0;i<s->source.plan.count;++i) {
            char name[32];track_name(name,i,&s->source.plan.tracks[i]);
            if(s->in_place) {
                const uint8_t *expected=s->sha_valid[i]?s->sha[i]:NULL;
                if(!full_readback(s,i,name,(uint64_t)s->committed[i]*KUI_RAW_BYTES,s->crc[i],expected) || !repair_expected(s,i))goto done;
            } else {
                struct kui_sha256 hash;uint32_t crc;if(!repair_prefix(s,i,&hash,&crc))goto done;
                s->crc[i]=crc;kui_sha256_digest(&hash,s->sha[i]);s->sha_valid[i]=true;
                if(!full_readback(s,i,name,(uint64_t)s->committed[i]*KUI_RAW_BYTES,crc,s->sha[i]) || !repair_expected(s,i))goto done;
            }
        }
        if(!repair_commit(s))goto done;
    }
    if(s->unresolved || s->repair->unresolved) {
    unresolved: s->unresolved=true;snprintf(out->message,sizeof(out->message),"Unresolved sectors/hash mismatches; incomplete repair retained");goto done;}
    if(!repair_catalogue(s) || !(s->in_place?inplace_publish(s):publish_copy(s)))goto done;
    ok=true;
done:
    ok=finish_logs(s,ok);
    if(ok && cancelled(s))ok=false;
    if(ok && !s->in_place && s->source.plan.profile==KUI_CAPTURE_PROFILE_CD_TOC) {
        job_path(s,"cue.part");char final[KUI_DEST_PATH_CAP];snprintf(final,sizeof(final),"0:%s/disc.cue",s->job);FILINFO info;FRESULT exists=f_stat(final,&info);
        if(exists==FR_NO_FILE) {if(f_rename(s->path,final)!=FR_OK) {fail(s,"Cannot publish repaired CD CUE");ok=false;}}
        else if(exists==FR_OK) {if(f_unlink(s->path)!=FR_OK) {fail(s,"Cannot retire duplicate staged CUE");ok=false;}}
        else {fail(s,"Cannot inspect repaired CD CUE");ok=false;}
    }
    if(ok && !s->in_place) {
        job_path(s,"disc.part");char final[KUI_DEST_PATH_CAP];snprintf(final,sizeof(final),"0:%s/disc.gdi",s->job);
        FILINFO info;FRESULT existing=f_stat(final,&info);
        if(existing==FR_OK) {
            /* The descriptor is deterministic. Never replace a published one. */
            FIL a,b;bool opened=f_open(&a,s->path,FA_READ)==FR_OK;
            if(!opened) {fail(s,"Cannot inspect staged repair descriptor");ok=false;}
            else if(f_open(&b,final,FA_READ)!=FR_OK) {close_file(s,&a,true);fail(s,"Cannot inspect retained repair descriptor");ok=false;}
            else {
                ok=f_size(&a)==f_size(&b);uint8_t x[256],y[256];uint64_t left=f_size(&a);
                while(ok && left) {unsigned n=(unsigned)(left>sizeof(x)?sizeof(x):left);ok=exact_read(s,&a,x,n) && exact_read(s,&b,y,n) && !memcmp(x,y,n);left-=n;}
                if(!close_file(s,&a,ok))ok=false;
                if(!close_file(s,&b,ok))ok=false;
                if(!ok && !s->failed)fail(s,"Published repair descriptor differs from retained state");
                if(ok && f_unlink(s->path)!=FR_OK) {fail(s,"Cannot retire duplicate staged descriptor");ok=false;}
            }
        } else if(existing!=FR_NO_FILE || f_rename(s->path,final)!=FR_OK) {fail(s,"Cannot publish completed copy descriptor");ok=false;}
    }
    if(mounted && f_mount(NULL,"0:",0)!=FR_OK) {fail(s,"Volume unmount failed; operation is not successful");ok=false;}
    if(s->failed)out->result=KUI_REPAIR_FAILED;
    else if(cancelled(s))out->result=KUI_REPAIR_STOPPED;
    else if(s->unresolved)out->result=KUI_REPAIR_UNRESOLVED;
    else out->result=ok?KUI_REPAIR_COMPLETE:KUI_REPAIR_FAILED;
    out->complete=ok && out->result==KUI_REPAIR_COMPLETE;
    if(out->complete)snprintf(out->message,sizeof(out->message),"%s%s",s->in_place?"In-place repair completed; all saved bytes read back":"Repair copy completed; all saved bytes read back",out->audio_unverified?"; audio unverified":"");
    else if(out->result==KUI_REPAIR_STOPPED)snprintf(out->message,sizeof(out->message),"%s",s->in_place?"Stopped; committed edits/evidence retained; resume this in-place transaction":"Stopped; originals unchanged, partial copy/evidence retained");
    update(s,true);ops->log("Repair copy: %s",out->message);enum kui_repair_result result=out->result;free(s->baseline_targets);free(s->legacy);free(s);return result;
}
static bool forensic_track(struct recovery_copy *s,unsigned ti) {
    const struct kui_capture_track *t=&s->source.plan.tracks[ti];s->forensic->track=ti+1;
    source_path(s,s->source.files[ti]);FIL f;if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot open original track read-only");
    bool ok=true;uint32_t position=0,sectors=t->end-t->start;
    while(ok && position<sectors) {
        if(cancelled(s)) {ok=false;break;}
        unsigned count=sectors-position;if(count>COPY_SECTORS)count=COPY_SECTORS;
        if(!exact_read(s,&f,s->data,count*KUI_RAW_BYTES)) {ok=false;break;}
        for(unsigned i=0;i<count;++i) {
            if(cancelled(s)) {ok=false;break;}
            uint32_t fad=t->start+position+i;unsigned flags=t->control==4?kui_recovery_sector_check(s->data+i*KUI_RAW_BYTES,KUI_RAW_BYTES,fad):0;
            if(flags) {
                ++s->forensic->suspects;if(flags&KUI_RECOVERY_SECTOR_UNSUPPORTED)++s->forensic->unsupported;
                if(s->forensic->sampled<KUI_FORENSIC_SECTOR_LIMIT) {
                    bool stable=false;
                    if(!reopen_evidence(s,ti,position+i,s->data+i*KUI_RAW_BYTES,flags,&stable)) {ok=false;break;}
                    ++s->forensic->sampled;if(!stable) {++s->forensic->unstable;if(!kui_recovery_guard_mark(s->directory,logger(s))) {ok=fail(s,"Cannot retain storage instability guard");break;}}
                } else s->forensic->limit_reached=true;
            }
            s->forensic->done+=KUI_RAW_BYTES;
        }
        position+=count;update(s,false);
    }
    return close_file(s,&f,ok);
}
enum kui_forensic_result kui_recovery_forensic(const char *directory,const struct kui_forensic_ops *ops,struct kui_forensic_status *out) {
    if(!out)return KUI_FORENSIC_FAILED;
    memset(out,0,sizeof(*out));
    if(!directory || !ops || !ops->log) {snprintf(out->message,sizeof(out->message),"Invalid saved-sector reread request");return out->result;}
    struct recovery_copy *s=calloc(1,sizeof(*s));if(!s) {snprintf(out->message,sizeof(out->message),"Not enough memory for bounded rereads");return out->result;}
    s->forensic=out;s->forensic_ops=ops;s->started=now_ms(s);FATFS fs;bool mounted=false,ok=false;
    if(cancelled(s))goto done;
    if(!kui_mount(&fs,ops->log)) {fail(s,"Cannot mount volume for saved-sector rereads");goto done;}mounted=true;
    if(!load_source(s,directory) || cancelled(s) || !new_job(s))goto done;
    if(!line(s,"At most %u suspect sectors get two independent reopen/read/close passes.\n"
        "Audio/track hashes are not localized by this diagnostic. Stable samples do not verify the whole storage device.\n",KUI_FORENSIC_SECTOR_LIMIT))goto done;
    for(unsigned i=0;i<s->source.plan.count;++i)if(!forensic_track(s,i))goto done;
    if(!line(s,"COMPLETE suspects=%" PRIu32 " sampled=%" PRIu32 " unstable=%" PRIu32 " limit_reached=%s\n",
        out->suspects,out->sampled,out->unstable,out->limit_reached?"YES":"NO") || !sync_file(s,&s->log))goto done;
    ok=true;
done:
    ok=finish_logs(s,ok);
    if(mounted && f_mount(NULL,"0:",0)!=FR_OK) {fail(s,"Volume unmount failed; diagnostic incomplete");ok=false;}
    out->complete=ok;
    out->result=s->failed?KUI_FORENSIC_FAILED:cancelled(s)?KUI_FORENSIC_STOPPED:!ok?KUI_FORENSIC_FAILED:
        out->unstable?KUI_FORENSIC_UNSTABLE:KUI_FORENSIC_STABLE;
    if(ok)snprintf(out->message,sizeof(out->message),out->unstable?"Saved-sector rereads disagree; see log and binary samples":
        out->sampled?"Sampled suspect-sector rereads agree; original defects remain":"No structurally suspect data sectors found; audio untested");
    else if(out->result==KUI_FORENSIC_STOPPED)snprintf(out->message,sizeof(out->message),"Stopped; originals unchanged, partial evidence retained");
    update(s,true);ops->log("Storage rereads: %s",out->message);enum kui_forensic_result result=out->result;free(s->baseline_targets);free(s->legacy);free(s);return result;
}
