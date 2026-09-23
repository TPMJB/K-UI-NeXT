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
    struct kui_scan_gdi gdi;
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
static bool file_error(struct scan *s,const char *operation,const char *name,FRESULT result) {
    char message[128];
    s->ops->log("Advanced CRC folder: %s",s->directory+2);
    s->ops->log("Advanced CRC file: %s; %s; FatFs=%u",name,operation,(unsigned)result);
    if(result==FR_NO_FILE || result==FR_NO_PATH)
        snprintf(message,sizeof(message),"Missing %.76s; open the actual game folder",name);
    else snprintf(message,sizeof(message),"%.32s: %.64s (FatFs %u)",operation,name,(unsigned)result);
    return fail(s,message);
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
    if(r!=FR_OK) return file_error(s,"Open failed",name,r);
    FSIZE_t bytes=f_size(&file);UINT got=0;
    bool ok=bytes>0 && bytes<=limit;
    if(!ok) file_error(s,"Empty or oversized metadata",name,FR_INVALID_PARAMETER);
    else {
        r=f_read(&file,data,(UINT)bytes,&got);
        if(r!=FR_OK || got!=bytes) {file_error(s,"Metadata read failed",name,r==FR_OK?FR_INT_ERR:r);ok=false;}
    }
    r=f_close(&file);
    if(r!=FR_OK) {file_error(s,"Metadata close failed",name,r);ok=false;}
    if(!ok) return false;
    *size=(size_t)bytes;return true;
}
static bool load_checkpoint(struct scan *s) {
    bool valid=false,present=false;
    for(unsigned i=0;i<2;++i) {
        if(cancelled(s)) return false;
        path(s,i?"checkpoint-b.bin":"checkpoint-a.bin");FIL file;
        FRESULT r=f_open(&file,s->path,FA_READ);
        if(r==FR_NO_FILE) continue;
        if(r!=FR_OK) return file_error(s,"Checkpoint open failed",i?"checkpoint-b.bin":"checkpoint-a.bin",r);
        present=true;
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
    if(!valid && !present) {
        s->ops->log("Advanced CRC: no checkpoints; hashes use the completed manifest only");
        return true;
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
    s->status->checkpoint_checked=true;return true;
}
static bool gdi_extension(const char *name) {
    size_t n=strlen(name);if(n<5) return false;name+=n-4;
    return name[0]=='.' && (name[1]=='g' || name[1]=='G') &&
        (name[2]=='d' || name[2]=='D') && (name[3]=='i' || name[3]=='I');
}
static bool load_imported(struct scan *s) {
    DIR directory;FRESULT r=f_opendir(&directory,s->directory);
    if(r!=FR_OK) return file_error(s,"Folder open failed","selected folder",r);
    FILINFO info;unsigned found=0;bool ok=true;
    for(;;) {
        if(cancelled(s)) {ok=false;break;}
        r=f_readdir(&directory,&info);
        if(r!=FR_OK) {file_error(s,"Folder read failed","selected folder",r);ok=false;break;}
        if(!info.fname[0]) break;
        if(!(info.fattrib&AM_DIR) && gdi_extension(info.fname)) {
            ++found;
            if(strlen(info.fname)>=sizeof(s->manifest.gdi)) {fail(s,"GDI filename is too long; original files preserved");ok=false;break;}
            memcpy(s->manifest.gdi,info.fname,strlen(info.fname)+1);
        }
    }
    if(f_closedir(&directory)!=FR_OK) {fail(s,"Cannot close selected folder");ok=false;}
    if(!ok) return false;
    if(!found) return fail(s,"No manifest.json or GDI here; open the actual game folder");
    if(found!=1) return fail(s,"Multiple GDI files: select a folder containing one dump");
    size_t size;
    if(!small_file(s,s->manifest.gdi,s->text,KUI_SCAN_MANIFEST_LIMIT,&size)) return false;
    if(!kui_recovery_gdi_parse(s->text,size,&s->gdi))
        return fail(s,"Unsupported GDI: needs raw 2352-byte tracks, safe filenames and zero offsets");
    s->manifest.plan=s->gdi.plan;s->manifest.crc_only=true;
    for(unsigned i=0;i<s->manifest.plan.count;++i) {
        if(cancelled(s)) return false;
        const char *name=s->gdi.files[i];path(s,name);r=f_stat(s->path,&info);
        if(r!=FR_OK) return file_error(s,"Track stat failed",name,r);
        uint64_t sectors=(uint64_t)info.fsize/KUI_RAW_BYTES;
        struct kui_capture_track *track=&s->manifest.plan.tracks[i];
        if((info.fattrib&AM_DIR) || !sectors || info.fsize%KUI_RAW_BYTES ||
           sectors>KUI_RECOVERY_FAD_MAX+1u-track->start ||
           (i+1<s->manifest.plan.count && track->start+sectors>s->manifest.plan.tracks[i+1].start))
            return file_error(s,"Invalid raw track length",name,FR_INVALID_PARAMETER);
        track->end=track->toc_end=track->start+(uint32_t)sectors;
        s->manifest.plan.bytes+=info.fsize;
    }
    s->ops->log("Advanced CRC: no manifest; STRUCTURAL ONLY, no expected hashes; audio is unverified");
    return true;
}
static bool load_metadata(struct scan *s) {
    size_t size;
    path(s,"manifest.json");FILINFO info;FRESULT r=f_stat(s->path,&info);
    if(r==FR_NO_FILE) {
        if(!load_imported(s)) return false;
        s->status->tracks=s->manifest.plan.count;s->status->total=s->manifest.plan.bytes;
        return true;
    }
    if(r!=FR_OK) return file_error(s,"Manifest stat failed","manifest.json",r);
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
    s->status->reference_hashes=true;
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
        if(!line(s,"K-UI Advanced saved-file scan v1\nJob: %s\nIdentity: %s\n",s->directory,
            s->status->reference_hashes?identity:"not available (GDI-only)")) return false;
        bool written=line(s,"Original tracks, checkpoints, manifest and GDI are read-only.\n"
            "Data: %s plus Mode 1 sync/address/EDC/reserved/P/Q.\n"
            "Audio: %s; no parity or optical reread.\n"
            "No independent catalogue comparison or sector repair is performed.\n"
            "Flags: 0x01 sync, 0x02 address, 0x04 EDC, 0x08 P/Q, 0x10 unsupported mode, 0x20 input, 0x40 reserved.\n"
            "At most %u suspect-sector lines; totals still include every sector.\n"
            "Only .txt plus COMPLETE is finished; .part is always incomplete.\n",
            s->status->reference_hashes?(s->manifest.crc_only?"saved CRC32":"saved CRC32/SHA-256"):"CRC32 recorded, NO expected hash",
            s->status->reference_hashes?"saved hash only":"UNVERIFIED; CRC recorded only",SCAN_REPORT_BAD_LIMIT);
        if(!written) return false;
        if(!line(s,"Metadata: %s; checkpoint: %s.\n",
            s->status->reference_hashes?"completed K-UI manifest":"STRUCTURAL ONLY, GDI file lengths",
            s->status->checkpoint_checked?"matched":s->status->reference_hashes?"absent; manifest only":"not used for GDI-only scan")) return false;
        if(f_sync(&s->report)!=FR_OK) return fail(s,"Initial report sync failed; scan not started");
        return true;
    }
    s->status->report[0]=0;return fail(s,"All Advanced CRC report numbers are occupied");
}
static bool scan_track(struct scan *s,unsigned index) {
    const struct kui_capture_track *t=&s->manifest.plan.tracks[index];
    char name[KUI_DEST_NAME_CAP];
    if(s->status->reference_hashes) snprintf(name,sizeof(name),"track%02u.%s",index+1,t->control==4?"bin":"raw");
    else snprintf(name,sizeof(name),"%s",s->gdi.files[index]);
    path(s,name);
    FIL file;
    FRESULT r=f_open(&file,s->path,FA_READ);
    if(r!=FR_OK) return file_error(s,"Track open failed",name,r);
    bool ok=true;uint64_t wanted=(uint64_t)(t->end-t->start)*KUI_RAW_BYTES;
    if(f_size(&file)!=wanted) {file_error(s,"Track length differs from metadata",name,FR_INVALID_PARAMETER);ok=false;}
    struct kui_sha256 sha;kui_sha256_init(&sha);uint32_t crc=0,position=0;
    s->status->track=index+1;
    snprintf(s->status->message,sizeof(s->status->message),"Checking %.70s: %s",name,t->control==4?"CRC + Mode 1 EDC/PQ":
        s->status->reference_hashes?"saved CRC (audio)":"recording CRC; audio unverified");
    update(s,true);
    while(ok && position<t->end-t->start) {
        if(cancelled(s)) {ok=false;break;}
        unsigned count=t->end-t->start-position;if(count>SCAN_CHUNK_SECTORS) count=SCAN_CHUNK_SECTORS;
        UINT bytes=count*KUI_RAW_BYTES,got=0;
        r=f_read(&file,s->data,bytes,&got);
        if(r!=FR_OK || got!=bytes) {file_error(s,"Track read failed",name,r==FR_OK?FR_INT_ERR:r);ok=false;break;}
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
    if(s->status->reference_hashes) {
        bool crc_match=crc==s->manifest.track[index].crc32;
        if(!crc_match) ++s->status->crc_mismatches;
        if(!line(s,"TRACK %02u CRC32 actual=%08" PRIx32 " expected=%08" PRIx32 " %s\n",index+1,crc,s->manifest.track[index].crc32,crc_match?"MATCH":"MISMATCH")) return false;
        s->ops->log("Advanced CRC T%02u CRC32=%08" PRIx32 " %s",index+1,crc,crc_match?"MATCH":"MISMATCH");
    } else {
        if(!line(s,"TRACK %02u CRC32 actual=%08" PRIx32 " NO EXPECTED HASH; file=%s\n",index+1,crc,name)) return false;
        s->ops->log("Advanced CRC T%02u CRC32=%08" PRIx32 " recorded only; no expected hash",index+1,crc);
    }
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
        fail(s,"Select the game folder containing its GDI and track files");free(s);return out->result;
    }
    snprintf(s->directory,sizeof(s->directory),"0:%s",normalized);
    snprintf(out->message,sizeof(out->message),"Checking saved capture metadata");update(s,true);
    FATFS fs;bool mounted=false,finished=false;
    if(cancelled(s)) goto done;
    if(!kui_mount(&fs,ops->log)) {fail(s,"Cannot mount the card for Advanced CRC");goto done;}
    mounted=true;
    ops->log("Advanced CRC selected folder: %s",normalized);
    if(!load_metadata(s) || cancelled(s) || !new_report(s)) goto done;
    ops->log("Advanced CRC: saved files in %s; no optical rereads or repairs",normalized);
    for(unsigned i=0;i<s->manifest.plan.count;++i) if(!scan_track(s,i)) goto done;
    if(cancelled(s)) goto done;
    out->result=(out->bad_sectors || out->unsupported_sectors || out->crc_mismatches || out->sha_mismatches)?KUI_SCAN_ISSUES:
        out->reference_hashes?KUI_SCAN_CLEAN:KUI_SCAN_STRUCTURAL;
    if(!line(s,"SUMMARY data=%" PRIu32 " audio=%" PRIu32 " bad=%" PRIu32 " unsupported=%" PRIu32
        " crc_mismatches=%" PRIu32 " sha_mismatches=%" PRIu32 " bytes=%" PRIu64 "\n"
        "COMPLETE %s\n",out->data_sectors,out->audio_sectors,out->bad_sectors,out->unsupported_sectors,
        out->crc_mismatches,out->sha_mismatches,out->done,out->result==KUI_SCAN_CLEAN?"CLEAN":
        out->result==KUI_SCAN_STRUCTURAL?"STRUCTURAL ONLY; NO EXPECTED HASHES; AUDIO UNVERIFIED":"ISSUES")) goto done;
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
        "Saved hashes and Mode 1 checks passed; audio hash only":out->result==KUI_SCAN_STRUCTURAL?
        (out->data_sectors?"Mode 1 structure passed; no expected hashes; audio unverified":
        "CRC values recorded only; no expected hashes; audio unverified"):"Scan finished with issues; see the sector report");
    update(s,true);
    ops->log("Advanced CRC: %s",out->message);
    if(out->report[0]) ops->log("Advanced CRC report: %s",out->report+2);
    free(s);return out->result;
}
