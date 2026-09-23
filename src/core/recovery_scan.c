/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/recovery_scan.h"
#include "kui/recovery_checks.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCAN_CHUNK_SECTORS 16u
#define SCAN_REPORT_BAD_LIMIT 4096u
struct scan {
    struct kui_scan_manifest manifest;
    struct kui_checkpoint checkpoint;
    uint8_t record[KUI_CHECKPOINT_BYTES];
    uint8_t data[SCAN_CHUNK_SECTORS*KUI_RAW_BYTES];
    char text[KUI_SCAN_MANIFEST_LIMIT+1u];
    char directory[KUI_DEST_JOB_CAP],path[KUI_DEST_PATH_CAP];
    FIL report;
    bool report_open,failed;
    unsigned reported_bad;
    const struct kui_scan_ops *ops;
    struct kui_scan_status *status;
    uint64_t started,last_progress;
};
static bool cancelled(struct scan *s) {return s->ops->cancelled && s->ops->cancelled(s->ops->ctx);}
static void update(struct scan *s,bool force) {
    uint64_t now=s->ops->now_ms?s->ops->now_ms(s->ops->ctx):0;
    s->status->elapsed_ms=now>=s->started?now-s->started:0;
    if(s->ops->progress && (force || now-s->last_progress>=100 || !s->ops->now_ms)) {
        s->last_progress=now;s->ops->progress(s->ops->ctx,s->status);
    }
}
static bool fail(struct scan *s,const char *message) {
    s->failed=true;
    snprintf(s->status->message,sizeof(s->status->message),"%s",message);
    s->ops->log("Advanced CRC: %s",message);return false;
}
static bool line(struct scan *s,const char *format,...) {
    char text[768];va_list args;va_start(args,format);int count=vsnprintf(text,sizeof(text),format,args);va_end(args);
    if(count<0 || (size_t)count>=sizeof(text)) return fail(s,"Report line exceeds its bounded buffer");
    UINT written=0;
    if(f_write(&s->report,text,(UINT)count,&written)!=FR_OK || written!=(UINT)count)
        return fail(s,"Report write failed; scan result is incomplete");
    return true;
}
static void path(struct scan *s,const char *name) {
    snprintf(s->path,sizeof(s->path),"%s/%s",s->directory,name);
}
static bool small_file(struct scan *s,const char *name,void *data,size_t limit,size_t *size) {
    path(s,name);FIL file;FRESULT r=f_open(&file,s->path,FA_READ);
    if(r!=FR_OK) return fail(s,"Required metadata is missing or cannot be read");
    FSIZE_t bytes=f_size(&file);UINT got=0;
    bool ok=bytes>0 && bytes<=limit && f_read(&file,data,(UINT)bytes,&got)==FR_OK && got==bytes;
    if(f_close(&file)!=FR_OK) ok=false;
    if(!ok) return fail(s,"Metadata is oversized, empty, truncated or unreadable");
    *size=(size_t)bytes;return true;
}
static bool load_checkpoint(struct scan *s) {
    bool valid=false;
    for(unsigned i=0;i<2;++i) {
        if(cancelled(s)) return false;
        path(s,i?"checkpoint-b.bin":"checkpoint-a.bin");FIL file;
        FRESULT r=f_open(&file,s->path,FA_READ);
        if(r==FR_NO_FILE) continue;
        if(r!=FR_OK) return fail(s,"Checkpoint storage read failed");
        UINT got=0;bool sized=f_size(&file)==sizeof(s->record);
        bool read_ok=!sized || (f_read(&file,s->record,sizeof(s->record),&got)==FR_OK && got==sizeof(s->record));
        if(f_close(&file)!=FR_OK) read_ok=false;
        if(!read_ok) return fail(s,"Checkpoint storage read failed");
        struct kui_checkpoint candidate;
        if(!sized || !kui_checkpoint_decode(s->record,&s->manifest.plan,s->manifest.identity,&candidate)) {
            s->ops->log("Advanced CRC: ignoring invalid checkpoint %c",i?'B':'A');continue;
        }
        if(valid && candidate.sequence==s->checkpoint.sequence) {
            kui_checkpoint_encode(&candidate,s->record);
            kui_checkpoint_encode(&s->checkpoint,s->data);
            if(memcmp(s->record,s->data,KUI_CHECKPOINT_BYTES))
                return fail(s,"Conflicting checkpoint records; original files preserved");
        }
        if(!valid || candidate.sequence>s->checkpoint.sequence) {s->checkpoint=candidate;valid=true;}
    }
    if(!valid) return fail(s,"No valid checkpoint matches this manifest");
    if(s->checkpoint.crc_only!=s->manifest.crc_only) return fail(s,"Checkpoint and manifest hash modes differ");
    for(unsigned i=0;i<s->manifest.plan.count;++i) {
        const struct kui_capture_track *t=&s->manifest.plan.tracks[i];
        if(s->checkpoint.track[i].sectors!=t->end-t->start)
            return fail(s,"Job is incomplete; resume its capture before scanning");
        if(s->checkpoint.track[i].crc32!=s->manifest.track[i].crc32 ||
           memcmp(s->checkpoint.track[i].sha256,s->manifest.track[i].sha256,32))
            return fail(s,"Checkpoint and manifest hashes differ");
    }
    return true;
}
static bool load_metadata(struct scan *s) {
    size_t size;
    if(!small_file(s,"manifest.json",s->text,KUI_SCAN_MANIFEST_LIMIT,&size)) return false;
    if(!kui_recovery_manifest_parse(s->text,size,&s->manifest))
        return fail(s,"Not a supported completed K-UI raw-track manifest");
    if(cancelled(s) || !load_checkpoint(s)) return false;
    /* Descriptor comparison is exact because K-UI publishes a deterministic
     * GDI. Do not follow filenames supplied by arbitrary imported metadata. */
    if(!small_file(s,s->manifest.gdi,s->text,sizeof(s->text)-1,&size)) return false;
    size_t offset=0;char expected[128];
    int n=snprintf(expected,sizeof(expected),"%u\n",s->manifest.plan.count);
    if(n<0 || (size_t)n>size || memcmp(s->text,expected,(size_t)n))
        return fail(s,"GDI descriptor differs from capture metadata");
    offset=(size_t)n;
    for(unsigned i=0;i<s->manifest.plan.count;++i) {
        const struct kui_capture_track *t=&s->manifest.plan.tracks[i];
        n=snprintf(expected,sizeof(expected),"%u %" PRIu32 " %" PRIu32 " 2352 track%02u.%s 0\n",
            i+1,t->start-150,t->control,i+1,t->control==4?"bin":"raw");
        if(n<0 || (size_t)n>=sizeof(expected) || (size_t)n>size-offset || memcmp(s->text+offset,expected,(size_t)n))
            return fail(s,"GDI descriptor differs from capture metadata");
        offset+=(size_t)n;
    }
    if(offset!=size) return fail(s,"GDI descriptor has unexpected trailing content");
    s->status->tracks=s->manifest.plan.count;s->status->total=s->manifest.plan.bytes;
    return true;
}
static bool new_report(struct scan *s) {
    if(cancelled(s)) return false;
    const char *parents[]={"0:/KUI","0:/KUI/recovery"};
    for(unsigned i=0;i<2;++i) {
        FRESULT r=f_mkdir(parents[i]);
        if(r!=FR_OK && r!=FR_EXIST) return fail(s,"Cannot create Advanced CRC report folder");
    }
    for(unsigned i=1;i<=9999;++i) {
        if(cancelled(s)) return false;
        snprintf(s->status->report,sizeof(s->status->report),"0:/KUI/recovery/scan-%04u.txt",i);
        FILINFO info;FRESULT exists=f_stat(s->status->report,&info);
        if(exists==FR_OK) continue;
        if(exists!=FR_NO_FILE) return fail(s,"Cannot inspect existing Advanced CRC reports");
        snprintf(s->status->report,sizeof(s->status->report),"0:/KUI/recovery/scan-%04u.part",i);
        FRESULT r=f_open(&s->report,s->status->report,FA_WRITE|FA_CREATE_NEW);
        if(r==FR_EXIST) continue;
        if(r!=FR_OK) {s->status->report[0]=0;return fail(s,"Cannot create a new Advanced CRC report");}
        s->report_open=true;
        char identity[65];kui_hex(s->manifest.identity,32,identity);
        bool written=line(s,"K-UI Advanced saved-file scan v1\nJob: %s\nIdentity: %s\n"
            "Original tracks, checkpoints, manifest and GDI are read-only.\n"
            "Data: CRC32%s plus Mode 1 sync/address/EDC/reserved/P/Q.\n"
            "Audio: saved hash only; no parity or optical reread.\n"
            "No independent catalogue comparison or sector repair is performed.\n"
            "Flags: 0x01 sync, 0x02 address, 0x04 EDC, 0x08 P/Q, 0x10 unsupported mode, 0x20 input, 0x40 reserved.\n"
            "At most %u suspect-sector lines; totals still include every sector.\n"
            "Only .txt plus COMPLETE is finished; .part is always incomplete.\n",
            s->directory,identity,s->manifest.crc_only?"":"/SHA-256",SCAN_REPORT_BAD_LIMIT);
        if(!written) return false;
        if(f_sync(&s->report)!=FR_OK) return fail(s,"Initial report sync failed; scan not started");
        return true;
    }
    s->status->report[0]=0;return fail(s,"All Advanced CRC report numbers are occupied");
}
static bool scan_track(struct scan *s,unsigned index) {
    const struct kui_capture_track *t=&s->manifest.plan.tracks[index];
    char name[32];snprintf(name,sizeof(name),"track%02u.%s",index+1,t->control==4?"bin":"raw");path(s,name);
    FIL file;
    if(f_open(&file,s->path,FA_READ)!=FR_OK) return fail(s,"Track file is missing or cannot be opened");
    bool ok=true;uint64_t wanted=(uint64_t)(t->end-t->start)*KUI_RAW_BYTES;
    if(f_size(&file)!=wanted) {fail(s,"Track length differs from its complete checkpoint");ok=false;}
    struct kui_sha256 sha;kui_sha256_init(&sha);uint32_t crc=0,position=0;
    s->status->track=index+1;
    snprintf(s->status->message,sizeof(s->status->message),"Checking %s: %s",name,t->control==4?"CRC + Mode 1 EDC/PQ":"saved CRC (audio)");
    update(s,true);
    while(ok && position<t->end-t->start) {
        if(cancelled(s)) {ok=false;break;}
        unsigned count=t->end-t->start-position;if(count>SCAN_CHUNK_SECTORS) count=SCAN_CHUNK_SECTORS;
        UINT bytes=count*KUI_RAW_BYTES,got=0;
        if(f_read(&file,s->data,bytes,&got)!=FR_OK || got!=bytes) {fail(s,"Track read failed; scan is incomplete");ok=false;break;}
        crc=kui_crc32(crc,s->data,bytes);
        if(!s->manifest.crc_only) kui_sha256_update(&sha,s->data,bytes);
        for(unsigned i=0;i<count;++i) {
            if(cancelled(s)) {ok=false;break;}
            if(t->control==4) {
                uint32_t fad=t->start+position+i;
                unsigned flags=kui_recovery_sector_check(s->data+i*KUI_RAW_BYTES,KUI_RAW_BYTES,fad);
                ++s->status->data_sectors;
                if(flags) {
                    if(flags&KUI_RECOVERY_SECTOR_UNSUPPORTED) ++s->status->unsupported_sectors;
                    else ++s->status->bad_sectors;
                    if(s->reported_bad<SCAN_REPORT_BAD_LIMIT) {
                        if(!line(s,"SUSPECT track=%02u fad=%" PRIu32 " flags=0x%02x\n",index+1,fad,flags)) {ok=false;break;}
                        if(s->reported_bad<8) s->ops->log("Advanced CRC T%02u FAD=%" PRIu32 " flags=0x%02x",index+1,fad,flags);
                        ++s->reported_bad;
                    }
                }
            } else ++s->status->audio_sectors;
            s->status->done+=KUI_RAW_BYTES;
        }
        position+=count;update(s,false);
    }
    if(f_close(&file)!=FR_OK) {fail(s,"Track close failed; scan is incomplete");ok=false;}
    if(!ok) return false;
    bool crc_match=crc==s->manifest.track[index].crc32;
    if(!crc_match) ++s->status->crc_mismatches;
    if(!line(s,"TRACK %02u CRC32 actual=%08" PRIx32 " expected=%08" PRIx32 " %s\n",index+1,crc,s->manifest.track[index].crc32,crc_match?"MATCH":"MISMATCH")) return false;
    s->ops->log("Advanced CRC T%02u CRC32=%08" PRIx32 " %s",index+1,crc,crc_match?"MATCH":"MISMATCH");
    if(!s->manifest.crc_only) {
        uint8_t digest[32];char text[65];kui_sha256_digest(&sha,digest);kui_hex(digest,32,text);
        bool same=!memcmp(digest,s->manifest.track[index].sha256,32);
        if(!same) ++s->status->sha_mismatches;
        if(!line(s,"TRACK %02u SHA256 actual=%s %s\n",index+1,text,same?"MATCH":"MISMATCH")) return false;
    }
    if(f_sync(&s->report)!=FR_OK) return fail(s,"Report sync failed; scan result is incomplete");
    return true;
}
enum kui_scan_result kui_recovery_scan(const char *directory,const struct kui_scan_ops *ops,struct kui_scan_status *out) {
    if(!out) return KUI_SCAN_FAILED;
    memset(out,0,sizeof(*out));
    if(!directory || !ops || !ops->log) {snprintf(out->message,sizeof(out->message),"Invalid scan request");return out->result;}
    struct scan *s=calloc(1,sizeof(*s));
    if(!s) {snprintf(out->message,sizeof(out->message),"Not enough memory for Advanced CRC scan");return out->result;}
    s->ops=ops;s->status=out;s->started=ops->now_ms?ops->now_ms(ops->ctx):0;
    char normalized[KUI_DEST_ROOT_CAP];
    const char *root=!strncmp(directory,"0:",2)?directory+2:directory;
    if(!kui_destination_normalize(normalized,root) || !strcmp(normalized,"/")) {
        fail(s,"Select a game folder with its manifest and GDI");free(s);return out->result;
    }
    snprintf(s->directory,sizeof(s->directory),"0:%s",normalized);
    snprintf(out->message,sizeof(out->message),"Checking saved capture metadata");update(s,true);
    FATFS fs;bool mounted=false,finished=false;
    if(cancelled(s)) goto done;
    if(!kui_mount(&fs,ops->log)) {fail(s,"Cannot mount the card for Advanced CRC");goto done;}
    mounted=true;
    if(!load_metadata(s) || cancelled(s) || !new_report(s)) goto done;
    ops->log("Advanced CRC: saved files in %s; no optical rereads or repairs",normalized);
    for(unsigned i=0;i<s->manifest.plan.count;++i) if(!scan_track(s,i)) goto done;
    if(cancelled(s)) goto done;
    out->result=(out->bad_sectors || out->unsupported_sectors || out->crc_mismatches || out->sha_mismatches)?KUI_SCAN_ISSUES:KUI_SCAN_CLEAN;
    if(!line(s,"SUMMARY data=%" PRIu32 " audio=%" PRIu32 " bad=%" PRIu32 " unsupported=%" PRIu32
        " crc_mismatches=%" PRIu32 " sha_mismatches=%" PRIu32 " bytes=%" PRIu64 "\n"
        "COMPLETE %s\n",out->data_sectors,out->audio_sectors,out->bad_sectors,out->unsupported_sectors,
        out->crc_mismatches,out->sha_mismatches,out->done,out->result==KUI_SCAN_CLEAN?"CLEAN":"ISSUES")) goto done;
    if(f_sync(&s->report)!=FR_OK) {fail(s,"Report sync failed; scan result is incomplete");goto done;}
    finished=true;
done:
    if(!finished) {
        out->result=cancelled(s) && !s->failed?KUI_SCAN_STOPPED:KUI_SCAN_FAILED;
        if(out->result==KUI_SCAN_STOPPED) snprintf(out->message,sizeof(out->message),"Stopped; original files preserved");
        if(s->report_open) {
            /* Best-effort incomplete footer; failure never becomes success. */
            (void)line(s,"INCOMPLETE %s at saved byte %" PRIu64 "\n",out->result==KUI_SCAN_STOPPED?"STOPPED":"FAILED",out->done);
            (void)f_sync(&s->report);
        }
    }
    if(s->report_open && f_close(&s->report)!=FR_OK) {fail(s,"Report close failed; scan result is incomplete");finished=false;out->result=KUI_SCAN_FAILED;}
    if(finished) {
        char final[KUI_DEST_PATH_CAP];snprintf(final,sizeof(final),"%s",out->report);
        size_t length=strlen(final);memcpy(final+length-5,".txt",5);
        if(f_rename(out->report,final)!=FR_OK) {
            fail(s,"Report finalization failed; partial report retained");finished=false;out->result=KUI_SCAN_FAILED;
        } else snprintf(out->report,sizeof(out->report),"%s",final);
    }
    if(mounted && f_mount(NULL,"0:",0)!=FR_OK) {fail(s,"Card unmount failed; scan result is incomplete");finished=false;out->result=KUI_SCAN_FAILED;}
    if(s->failed) {finished=false;out->result=KUI_SCAN_FAILED;}
    out->complete=finished;
    if(finished) snprintf(out->message,sizeof(out->message),"%s",out->result==KUI_SCAN_CLEAN?
        "Saved hashes and Mode 1 checks passed; audio hash only":"Scan finished with issues; see the sector report");
    update(s,true);
    ops->log("Advanced CRC: %s",out->message);
    if(out->report[0]) ops->log("Advanced CRC report: %s",out->report+2);
    free(s);return out->result;
}
