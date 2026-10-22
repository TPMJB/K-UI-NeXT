/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/repair_copy.h"
#include "kui/recovery_checks.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COPY_SECTORS 16u
#define EVIDENCE_HEADER 64u
struct recovery_copy {
    struct kui_scan_manifest manifest;
    struct kui_scan_gdi source;
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
    FIL log,evidence;
    bool log_open,evidence_open,failed,unresolved,manifest_present;
    uint64_t started,last_progress,evidence_records;
};
_Static_assert(sizeof(struct recovery_copy)<160u*1024u,"Repair memory is bounded");
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
static void source_path(struct recovery_copy *s,const char *name) {snprintf(s->path,sizeof(s->path),"0:%s/%s",s->directory,name);}
static void job_path(struct recovery_copy *s,const char *name) {snprintf(s->path,sizeof(s->path),"0:%s/%s",s->job,name);}
static void track_name(char name[32],unsigned i,const struct kui_capture_track *t) {snprintf(name,32,"track%02u.%s",i+1,t->control==4?"bin":"raw");}
static bool metadata_read(struct recovery_copy *s,const char *name,size_t *size) {
    source_path(s,name);FIL f;if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot open saved dump metadata");
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
static bool load_source(struct recovery_copy *s,const char *directory) {
    if(!kui_destination_job_normalize(s->directory,directory))return fail(s,"Select the actual saved game folder");
    source_path(s,"manifest.json");FILINFO info;FRESULT r=f_stat(s->path,&info);size_t n=0;
    if(r==FR_OK) {
        s->manifest_present=true;
        if(!metadata_read(s,"manifest.json",&n))return false;
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
        DIR d;source_path(s,"");if(f_opendir(&d,s->path)!=FR_OK)return fail(s,"Cannot open selected dump folder");
        char selected[KUI_DEST_NAME_CAP]={0};unsigned found=0;bool ok=true;
        for(;;) {
            if(cancelled(s)) {ok=false;break;}
            r=f_readdir(&d,&info);if(r!=FR_OK) {ok=fail(s,"Cannot read selected dump folder");break;}
            if(!info.fname[0])break;
            if(!(info.fattrib&AM_DIR) && gdi_extension(info.fname)) {
                ++found;if(strlen(info.fname)>=sizeof(selected)) {ok=fail(s,"Imported GDI name is too long");break;}
                memcpy(selected,info.fname,strlen(info.fname)+1);
            }
        }
        if(f_closedir(&d)!=FR_OK)ok=fail(s,"Cannot close selected dump folder");
        if(!ok)return false;
        if(found!=1)return fail(s,"Select a dump folder containing exactly one GDI");
        if(!metadata_read(s,selected,&n) || !kui_recovery_gdi_parse(s->metadata,n,&s->source))
            return fail(s,"Imported GDI requires raw 2352 tracks, zero offsets and safe names");
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
    }
    if(s->repair) {s->repair->tracks=s->source.plan.count;s->repair->total=s->source.plan.bytes;s->repair->imported=!s->manifest_present;}
    else {s->forensic->tracks=s->source.plan.count;s->forensic->total=s->source.plan.bytes;}
    return true;
}
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
static bool optical_identity(struct recovery_copy *s) {
    const struct kui_capture_plan *p=s->optical_plan,*saved=&s->source.plan;
    if(!p || !p->count || p->count>99 || p->count!=saved->count)return fail(s,"Inserted disc track count differs from saved dump");
    uint64_t bytes=0;
    for(unsigned i=0;i<p->count;++i) {
        const struct kui_capture_track *a=&p->tracks[i],*b=&saved->tracks[i];
        if(a->number!=i+1 || (a->control!=0 && a->control!=4) || a->session>1 || a->start<150 ||
           a->end<=a->start || a->end>KUI_RECOVERY_FAD_MAX+1u || a->toc_end<a->end ||
           a->toc_end>KUI_RECOVERY_FAD_MAX+1u || (i && a->start<p->tracks[i-1].end) ||
           a->number!=b->number || a->control!=b->control || a->start!=b->start || a->end!=b->end ||
           (s->manifest_present && (a->session!=b->session || a->toc_end!=b->toc_end)))
            return fail(s,"Inserted disc layout/ranges differ from saved dump");
        bytes+=(uint64_t)(a->end-a->start)*KUI_RAW_BYTES;
    }
    if(bytes!=p->bytes)return fail(s,"Prepared disc byte count is invalid");
    if(s->manifest_present) {
        struct kui_sha256 hash;kui_sha256_init(&hash);
        kui_sha256_update(&hash,KUI_CAPTURE_PROFILE,strlen(KUI_CAPTURE_PROFILE));hash32(&hash,p->count);
        for(unsigned i=0;i<p->count;++i) {
            const struct kui_capture_track *t=&p->tracks[i];
            hash32(&hash,t->number);hash32(&hash,t->control);hash32(&hash,t->session);hash32(&hash,t->start);hash32(&hash,t->end);hash32(&hash,t->toc_end);
            uint32_t points[3]={t->start,t->start+(t->end-t->start-1)/2,t->end-1};
            for(unsigned n=0;n<3;++n) {
                if(!optical_sector(s,points[n],s->candidate))return s->failed||cancelled(s)?false:fail(s,"Cannot read optical capture identity");
                if(t->control==4 && kui_recovery_sector_check(s->candidate,KUI_RAW_BYTES,points[n]))
                    return fail(s,"Optical identity data sector is not valid Mode 1");
                hash32(&hash,points[n]);kui_sha256_update(&hash,s->candidate,KUI_RAW_BYTES);
            }
        }
        uint8_t digest[32];kui_sha256_digest(&hash,digest);
        if(memcmp(digest,s->manifest.identity,32))return fail(s,"Inserted disc content identity differs from saved capture");
    } else {
        uint32_t chosen=0;unsigned found=0;
        /* Independent healthy anchors near both ends of saved data, bounded
         * to 16 alternatives per track/end. Corrupt saved anchors are skipped. */
        for(unsigned end=0;end<2;++end) {
            bool matched=false;
            for(unsigned ti=0;ti<p->count && !matched;++ti) {
                const struct kui_capture_track *t=&p->tracks[ti];
                /* GD low-density warning/data tracks can be shared by
                 * unrelated discs. Imported identity must use game data. */
                if(t->control!=4 || t->session!=1)continue;
                uint32_t count=t->end-t->start;
                for(unsigned n=0;n<16 && n<count;++n) {
                    uint32_t sector=end?count-1-n:n,fad=t->start+sector;if(fad==chosen)continue;
                    if(cancelled(s) || !read_saved_sector(s,ti,sector,s->samples[0]))return false;
                    if(kui_recovery_sector_check(s->samples[0],KUI_RAW_BYTES,fad))continue;
                    if(!optical_sector(s,fad,s->candidate))return s->failed||cancelled(s)?false:fail(s,"Cannot read imported dump identity anchor");
                    if(kui_recovery_sector_check(s->candidate,KUI_RAW_BYTES,fad) || memcmp(s->candidate,s->samples[0],KUI_RAW_BYTES))
                        return fail(s,"Inserted disc bytes differ from imported dump identity anchor");
                    chosen=fad;matched=true;++found;break;
                }
            }
        }
        if(found!=2)return fail(s,"Cannot establish two healthy distinct saved/optical data anchors");
    }
    return true;
}
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
        if(!line(s,"K-UI %s v1\nSOURCE 0:%s\nOriginal files are READ ONLY.\n"
            "samples.bin: 64-byte KUIRDB1/KUIRPT1 headers, then three raw2352 samples; header/data CRC32.\n"
            ".part is incomplete; no successful result without COMPLETE.\n",
            s->repair?"repair-to-copy":"saved-sector reopen evidence",s->directory))return false;
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
        if(kui_recovery_sector_check(s->candidate,KUI_RAW_BYTES,fad))continue;
        if(!optical_sector(s,fad,s->samples[2]))continue;
        if(kui_recovery_sector_check(s->samples[2],KUI_RAW_BYTES,fad) || memcmp(s->candidate,s->samples[2],KUI_RAW_BYTES))continue;
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
    uint32_t actual=0;uint64_t done=0;struct kui_sha256 sha;kui_sha256_init(&sha);
    while(ok && done<wanted) {
        if(cancelled(s)) {ok=false;break;}
        unsigned bytes=(unsigned)(wanted-done>sizeof(s->data)?sizeof(s->data):wanted-done);
        if(!exact_read(s,&f,s->data,bytes)) {ok=false;break;}
        actual=kui_crc32(actual,s->data,bytes);kui_sha256_update(&sha,s->data,bytes);done+=bytes;update(s,false);
    }
    if(!close_file(s,&f,ok))return false;
    uint8_t saved[32];kui_sha256_digest(&sha,saved);
    if(actual!=crc || memcmp(saved,digest,32))return fail(s,"Copied track saved-byte readback mismatch");
    return line(s,"READBACK track=%02u bytes=%" PRIu64 " CRC32=%08" PRIx32 " SHA256=MATCH\n",ti+1,wanted,crc) && sync_file(s,&s->log);
}
static bool copy_track(struct recovery_copy *s,unsigned ti) {
    const struct kui_capture_track *t=&s->source.plan.tracks[ti];uint32_t sectors=t->end-t->start;
    s->repair->track=ti+1;s->repair->phase=KUI_REPAIR_COPYING;
    char name[32];track_name(name,ti,t);source_path(s,s->source.files[ti]);FIL input,output;
    if(f_open(&input,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot open original track read-only");
    job_path(s,name);if(f_open(&output,s->path,FA_WRITE|FA_CREATE_NEW)!=FR_OK) {close_file(s,&input,true);return fail(s,"Cannot create unique copied track");}
    bool ok=true;uint32_t position=0,crc=0;struct kui_sha256 sha;kui_sha256_init(&sha);
    if(t->control==0 && !s->manifest_present)s->repair->audio_unverified=true;
    while(ok && position<sectors) {
        if(cancelled(s)) {ok=false;break;}
        unsigned count=sectors-position;if(count>COPY_SECTORS)count=COPY_SECTORS;
        if(!exact_read(s,&input,s->data,count*KUI_RAW_BYTES)) {ok=false;break;}
        for(unsigned i=0;i<count && ok;++i) {
            if(cancelled(s)) {ok=false;break;}
            uint8_t *raw=s->data+i*KUI_RAW_BYTES;uint32_t fad=t->start+position+i;s->repair->fad=fad;
            unsigned flags=t->control==4?kui_recovery_sector_check(raw,KUI_RAW_BYTES,fad):0;
            if(flags) {
                ++s->repair->suspects;
                if(s->repair->suspects>KUI_REPAIR_SECTOR_LIMIT) {ok=fail(s,"Repair target limit reached; copy remains incomplete");break;}
                bool stable=false;
                if(!reopen_evidence(s,ti,position+i,raw,flags,&stable)) {ok=false;break;}
                if(!stable) {s->repair->storage_unstable=true;ok=fail(s,"Saved-sector rereads disagree; optical repair blocked");break;}
                if((flags&KUI_RECOVERY_SECTOR_UNSUPPORTED) || !read_candidate(s,ti,position+i)) {
                    if(s->failed || cancelled(s)) {ok=false;break;}
                    ++s->repair->unresolved;s->unresolved=true;
                    if(!line(s,"UNRESOLVED track=%02u fad=%" PRIu32 " flags=%02x\n",ti+1,fad,flags)) {ok=false;break;}
                } else {memcpy(raw,s->candidate,KUI_RAW_BYTES);++s->repair->repaired;}
            }
            s->repair->done+=KUI_RAW_BYTES;
        }
        if(!ok)break;
        unsigned bytes=count*KUI_RAW_BYTES;
        if(!exact_write(s,&output,s->data,bytes)) {ok=false;break;}
        crc=kui_crc32(crc,s->data,bytes);kui_sha256_update(&sha,s->data,bytes);position+=count;update(s,false);
    }
    if(ok)ok=sync_file(s,&output);
    if(!close_file(s,&output,ok))ok=false;
    if(!close_file(s,&input,ok))ok=false;
    if(!ok)return false;
    uint8_t digest[32];kui_sha256_digest(&sha,digest);s->crc[ti]=crc;memcpy(s->sha[ti],digest,32);
    if(!full_readback(s,ti,name,(uint64_t)sectors*KUI_RAW_BYTES,crc,digest))return false;
    if(s->manifest_present && (crc!=s->manifest.track[ti].crc32 ||
       (!s->manifest.crc_only && memcmp(digest,s->manifest.track[ti].sha256,32)))) {
        ++s->repair->unresolved;s->unresolved=true;
        if(!line(s,"UNRESOLVED track=%02u source manifest hash differs; cannot localize %s from a track hash\n",ti+1,t->control==4?"remaining bytes":"audio"))return false;
    }
    return sync_file(s,&s->log);
}
static bool publish_new(struct recovery_copy *s,const char *temporary,const char *final,const void *data,unsigned bytes) {
    job_path(s,temporary);FIL f;if(f_open(&f,s->path,FA_WRITE|FA_CREATE_NEW)!=FR_OK)return fail(s,"Cannot create final copy metadata");
    bool ok=exact_write(s,&f,data,bytes) && sync_file(s,&f);
    if(!close_file(s,&f,ok))return false;
    job_path(s,temporary);char to[KUI_DEST_PATH_CAP];snprintf(to,sizeof(to),"0:%s/%s",s->job,final);
    if(f_rename(s->path,to)!=FR_OK)return fail(s,"Cannot publish final copy metadata");
    return true;
}
static bool publish_copy(struct recovery_copy *s) {
    if(cancelled(s))return false;
    s->repair->phase=KUI_REPAIR_PUBLISHING;update(s,true);
    if(cancelled(s))return false;
    size_t used=(size_t)snprintf(s->metadata,sizeof(s->metadata),"K-UI repair copy v1\nSOURCE 0:%s\n"
        "Completed readback of every saved copied byte. Original files unchanged.\n"
        "Imported audio verified=%s; catalogue verification not performed.\n",s->directory,s->repair->audio_unverified?"NO":"source hashes available");
    for(unsigned i=0;i<s->source.plan.count;++i) {
        const struct kui_capture_track *t=&s->source.plan.tracks[i];char digest[65];kui_hex(s->sha[i],32,digest);
        int n=snprintf(s->metadata+used,sizeof(s->metadata)-used,"TRACK %u bytes=%" PRIu64 " CRC32=%08" PRIx32 " SHA256=%s\n",
            i+1,(uint64_t)(t->end-t->start)*KUI_RAW_BYTES,s->crc[i],digest);
        if(n<0 || (size_t)n>=sizeof(s->metadata)-used)return fail(s,"Copy manifest exceeds buffer");
        used+=(size_t)n;
    }
    if(!publish_new(s,"copy.part","copy.txt",s->metadata,(unsigned)used))return false;
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
        if(success && !s->failed) {
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
    if(!directory || !prepared_plan || !ops || !ops->read || !ops->log || !options ||
       (options->attempts!=1 && options->attempts!=5 && options->attempts!=10 && options->attempts!=20 && options->attempts!=50)) {
        snprintf(out->message,sizeof(out->message),"Invalid repair-to-copy request");return out->result;
    }
    struct recovery_copy *s=calloc(1,sizeof(*s));if(!s) {snprintf(out->message,sizeof(out->message),"Not enough memory for repair copy");return out->result;}
    s->repair=out;s->optical_plan=prepared_plan;s->optical_ops=ops;s->repair_options=options;s->started=now_ms(s);
    FATFS fs;bool mounted=false,ok=false;out->phase=KUI_REPAIR_IDENTIFY;
    if(cancelled(s))goto done;
    if(!kui_mount(&fs,ops->log)) {fail(s,"Cannot mount volume for repair copy");goto done;}mounted=true;
    if(!load_source(s,directory) || cancelled(s) || !optical_identity(s) || cancelled(s) || !new_job(s))goto done;
    for(unsigned i=0;i<s->source.plan.count;++i)if(!copy_track(s,i))goto done;
    if(s->unresolved) {snprintf(out->message,sizeof(out->message),"Unresolved sectors/hash mismatches; incomplete copy retained");goto done;}
    if(!publish_copy(s))goto done;
    ok=true;
done:
    ok=finish_logs(s,ok);
    if(ok && cancelled(s))ok=false;
    if(ok) {
        job_path(s,"disc.part");char final[KUI_DEST_PATH_CAP];snprintf(final,sizeof(final),"0:%s/disc.gdi",s->job);
        if(f_rename(s->path,final)!=FR_OK) {fail(s,"Cannot publish completed copy descriptor");ok=false;}
    }
    if(mounted && f_mount(NULL,"0:",0)!=FR_OK) {fail(s,"Volume unmount failed; operation is not successful");ok=false;}
    if(s->failed)out->result=KUI_REPAIR_FAILED;
    else if(cancelled(s))out->result=KUI_REPAIR_STOPPED;
    else if(s->unresolved)out->result=KUI_REPAIR_UNRESOLVED;
    else out->result=ok?KUI_REPAIR_COMPLETE:KUI_REPAIR_FAILED;
    out->complete=ok && out->result==KUI_REPAIR_COMPLETE;
    if(out->complete)snprintf(out->message,sizeof(out->message),out->audio_unverified?
        "Repair copy readback passed; imported audio remains unverified":"Repair copy completed; all saved bytes read back");
    else if(out->result==KUI_REPAIR_STOPPED)snprintf(out->message,sizeof(out->message),"Stopped; originals unchanged, partial copy/evidence retained");
    update(s,true);ops->log("Repair copy: %s",out->message);enum kui_repair_result result=out->result;free(s);return result;
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
                    ++s->forensic->sampled;if(!stable)++s->forensic->unstable;
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
    update(s,true);ops->log("Storage rereads: %s",out->message);enum kui_forensic_result result=out->result;free(s);return result;
}
