/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/salvage_completion.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPORT_CAPACITY 32768u
struct check_report {
    FIL file;
    bool open;
    unsigned used, written;
    char *text;
    char partial[KUI_DEST_PATH_CAP], final[KUI_DEST_PATH_CAP];
};

static bool stopped(const struct kui_salvage_check_ops *ops) {
    return ops->cancelled && ops->cancelled(ops->ctx);
}
static void progress(const struct kui_salvage_check_ops *ops,
        const struct kui_salvage_check_status *out) {
    if(ops->progress)ops->progress(ops->ctx,out);
}
static bool valid_receipt(const struct kui_capture_plan *plan,
        const struct kui_salvage_status *receipt) {
    if(!plan || !receipt || !plan->count || plan->count>99 ||
       (plan->profile!=KUI_CAPTURE_PROFILE_GD && plan->profile!=KUI_CAPTURE_PROFILE_CD_TOC) ||
       receipt->result!=KUI_SALVAGE_RESOLVED || !receipt->complete ||
       !receipt->first_pass_complete || receipt->remaining ||
       receipt->targets!=receipt->recovered || receipt->tracks!=plan->count ||
       receipt->total!=plan->bytes || receipt->done!=plan->bytes ||
       receipt->job[21] || memcmp(receipt->job,"/KUI/salvage/job-",17) ||
       !strcmp(receipt->job+17,"0000"))return false;
    for(unsigned i=17;i<21;++i)if(receipt->job[i]<'0'||receipt->job[i]>'9')return false;
    uint64_t total=0;
    for(unsigned i=0;i<plan->count;++i) {
        const struct kui_capture_track *t=&plan->tracks[i];
        if(t->number!=i+1 || (t->control!=0 && t->control!=4) || t->end<=t->start)return false;
        total+=(uint64_t)(t->end-t->start)*KUI_RAW_BYTES;
    }
    return total==plan->bytes;
}
static uint32_t header_word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static bool identity_matches(const struct kui_capture_plan *plan,const struct kui_salvage_status *receipt,
        const struct kui_salvage_check_ops *ops,struct kui_salvage_check_status *out) {
    char path[KUI_DEST_PATH_CAP];snprintf(path,sizeof(path),"0:%s/header.bin",receipt->job);
    FIL file;if(f_open(&file,path,FA_READ)!=FR_OK) {
        snprintf(out->message,sizeof(out->message),"Cannot reopen salvage identity header");return false;
    }
    uint8_t bytes[512],digest[32];struct kui_sha256 hash;kui_sha256_init(&hash);
    bool ok=f_size(&file)==4096;
    for(unsigned at=0;ok && at<4096;at+=sizeof(bytes)) {
        if(stopped(ops)) {ok=false;break;}
        UINT got=0;ok=f_read(&file,bytes,sizeof(bytes),&got)==FR_OK && got==sizeof(bytes);
        if(ok && at==0 && header_word(bytes+16)!=plan->count)ok=false;
        if(ok && at==0 && (header_word(bytes+8)==3)!=(plan->profile==KUI_CAPTURE_PROFILE_CD_TOC))ok=false;
        if(ok && at==3584 && plan->profile==KUI_CAPTURE_PROFILE_CD_TOC && header_word(bytes+480)!=KUI_CAPTURE_PROFILE_CD_TOC)ok=false;
        for(unsigned n=0;ok && n<sizeof(bytes);n+=4) {
            unsigned offset=at+n;
            if(offset<96 || offset>=96+plan->count*24)continue;
            unsigned index=(offset-96)/24,field=((offset-96)%24)/4;
            const struct kui_capture_track *t=&plan->tracks[index];
            const uint32_t fields[]={t->number,t->control,t->session,t->start,t->end,t->toc_end};
            if(header_word(bytes+n)!=fields[field])ok=false;
        }
        if(ok)kui_sha256_update(&hash,bytes,sizeof(bytes));
    }
    if(f_close(&file)!=FR_OK)ok=false;
    kui_sha256_digest(&hash,digest);
    if(!ok || memcmp(digest,receipt->identity,sizeof(digest))) {
        snprintf(out->message,sizeof(out->message),"Salvage identity or track plan changed; checks refused");return false;
    }
    return true;
}
static bool report_append(struct check_report *report,const char *format,...) {
    va_list args;va_start(args,format);
    int n=vsnprintf(report->text+report->used,REPORT_CAPACITY-report->used,format,args);
    va_end(args);
    if(n<0 || (unsigned)n>=REPORT_CAPACITY-report->used)return false;
    report->used+=(unsigned)n;return true;
}
static bool report_flush(struct check_report *report,struct kui_salvage_check_status *out) {
    UINT put=0;unsigned pending=report->used-report->written;
    if(f_write(&report->file,report->text+report->written,pending,&put)!=FR_OK || put!=pending ||
       f_sync(&report->file)!=FR_OK) {
        snprintf(out->message,sizeof(out->message),"Recovery-check report write/sync failed; recovery retained");return false;
    }
    report->written=report->used;return true;
}
static bool report_begin(struct check_report *report,const struct kui_capture_plan *plan,
        const struct kui_salvage_status *receipt,const struct kui_salvage_check_ops *ops,
        struct kui_salvage_check_status *out) {
    report->text=malloc(REPORT_CAPACITY);
    if(!report->text) {snprintf(out->message,sizeof(out->message),"Not enough memory for recovery-check evidence");return false;}
    for(unsigned n=1;n<=9999;++n) {
        if(stopped(ops))return false;
        snprintf(report->partial,sizeof(report->partial),"0:%s/completion-check-%04u.part",receipt->job,n);
        snprintf(report->final,sizeof(report->final),"0:%s/completion-check-%04u.txt",receipt->job,n);
        FILINFO info;FRESULT a=f_stat(report->partial,&info),b=f_stat(report->final,&info);
        if((a!=FR_OK && a!=FR_NO_FILE) || (b!=FR_OK && b!=FR_NO_FILE)) {
            snprintf(out->message,sizeof(out->message),"Cannot inspect recovery-check report destination");return false;
        }
        if(a==FR_OK || b==FR_OK)continue;
        if(f_open(&report->file,report->partial,FA_WRITE|FA_CREATE_NEW)!=FR_OK) {
            snprintf(out->message,sizeof(out->message),"Cannot create unique recovery-check report");return false;
        }
        report->open=true;snprintf(out->report,sizeof(out->report),"%s",report->partial+2);break;
    }
    if(!report->open) {snprintf(out->message,sizeof(out->message),"Recovery-check report names exhausted");return false;}
    char identity[65];kui_hex(receipt->identity,32,identity);
    bool ok=report_append(report,"KUI_SALVAGE_COMPLETION_V1\njob %s\nheader_identity_sha256 %s\n"
        "original_targets %" PRIu32 "\nrecovered %" PRIu32 "\nremaining 0\nreadback_requested %s\n",
        receipt->job,identity,receipt->targets,receipt->recovered,out->readback_requested?"YES":"NO");
    for(unsigned i=0;i<plan->count && ok;++i) {
        const struct kui_capture_track *t=&plan->tracks[i];
        ok=report_append(report,"TRACK %02u control=%u session=%u start=%" PRIu32 " end=%" PRIu32
            " toc_end=%" PRIu32 " bytes=%" PRIu64 " expected_crc32=%08" PRIx32 "\n",
            i+1,t->control,t->session,t->start,t->end,t->toc_end,(uint64_t)(t->end-t->start)*KUI_RAW_BYTES,receipt->final_crc[i]);
    }
    if(!ok) {snprintf(out->message,sizeof(out->message),"Recovery-check report exceeds its bound");return false;}
    return report_flush(report,out);
}
static bool report_compare(const char *path,const struct check_report *report,
        const struct kui_salvage_check_ops *ops,struct kui_salvage_check_status *out) {
    FIL file;if(f_open(&file,path,FA_READ)!=FR_OK) {
        snprintf(out->message,sizeof(out->message),"Cannot read back recovery-check report");return false;
    }
    uint8_t bytes[1024];unsigned at=0;bool ok=f_size(&file)==report->used;
    while(ok && at<report->used) {
        if(stopped(ops)) {ok=false;break;}
        UINT want=report->used-at>sizeof(bytes)?sizeof(bytes):report->used-at,got=0;
        ok=f_read(&file,bytes,want,&got)==FR_OK && got==want && !memcmp(bytes,report->text+at,want);at+=want;
    }
    if(f_close(&file)!=FR_OK)ok=false;
    if(!ok)snprintf(out->message,sizeof(out->message),"Recovery-check report readback failed; no check success");
    return ok;
}
static bool report_publish(struct check_report *report,const struct kui_salvage_check_ops *ops,
        struct kui_salvage_check_status *out) {
    if(!report_append(report,"saved_track_bytes_verified %s\ncatalogue_crc_source %s\n"
        "catalogue %s\ncatalogue_result %s\nreference_title %s\nCOMPLETE\n",
        out->saved_verified?"YES":"NO",out->saved_verified?"SAVED_TRACK_READBACK":"VALIDATED_RECOVERY_RECEIPT",
        out->catalogue.catalog,kui_known_text(out->catalogue.result),out->catalogue.name)) {
        snprintf(out->message,sizeof(out->message),"Recovery-check report exceeds its bound");return false;
    }
    if(stopped(ops) || !report_flush(report,out))return false;
    FRESULT closed=f_close(&report->file);report->open=false;
    if(closed!=FR_OK) {snprintf(out->message,sizeof(out->message),"Recovery-check report close failed");return false;}
    if(!report_compare(report->partial,report,ops,out) || stopped(ops))return false;
    if(f_rename(report->partial,report->final)!=FR_OK) {
        snprintf(out->message,sizeof(out->message),"Recovery-check report publication failed; recovery retained");return false;
    }
    snprintf(out->report,sizeof(out->report),"%s",report->final+2);
    if(!report_compare(report->final,report,ops,out))return false;
    out->report_written=true;ops->log("Salvage completion report: %s",out->report);return true;
}
static bool read_saved(const struct kui_capture_plan *plan,
        const struct kui_salvage_status *receipt,const struct kui_salvage_check_ops *ops,
        struct kui_salvage_check_status *out) {
    uint8_t *buffer=malloc(KUI_CAPTURE_CHUNK*KUI_RAW_BYTES);
    if(!buffer) {snprintf(out->message,sizeof(out->message),"Not enough memory for saved-file verification");return false;}
    bool ok=true;
    for(unsigned i=0;i<plan->count && ok;++i) {
        if(stopped(ops)) {ok=false;break;}
        char path[KUI_DEST_PATH_CAP];
        snprintf(path,sizeof(path),"0:%s/track%02u.%s",receipt->job,i+1,
            plan->tracks[i].control==4?"bin":"raw");
        FIL file;out->track=i+1;
        snprintf(out->message,sizeof(out->message),"Reading saved track %u/%u",i+1,plan->count);
        progress(ops,out);
        if(stopped(ops)) {ok=false;break;}
        if(f_open(&file,path,FA_READ)!=FR_OK) {
            snprintf(out->message,sizeof(out->message),"Cannot reopen saved salvage track %u",i+1);ok=false;break;
        }
        uint64_t size=(uint64_t)(plan->tracks[i].end-plan->tracks[i].start)*KUI_RAW_BYTES,at=0;
        uint32_t crc=0;
        if(f_size(&file)!=size) {snprintf(out->message,sizeof(out->message),"Saved salvage track %u length changed",i+1);ok=false;}
        while(ok && at<size) {
            if(stopped(ops)) {ok=false;break;}
            UINT bytes=size-at>KUI_CAPTURE_CHUNK*KUI_RAW_BYTES?
                KUI_CAPTURE_CHUNK*KUI_RAW_BYTES:(UINT)(size-at),got=0;
            if(f_read(&file,buffer,bytes,&got)!=FR_OK || got!=bytes) {
                snprintf(out->message,sizeof(out->message),"Saved salvage track %u read failed",i+1);ok=false;break;
            }
            crc=kui_crc32(crc,buffer,bytes);at+=bytes;out->done+=bytes;
            progress(ops,out);
        }
        if(f_close(&file)!=FR_OK) {snprintf(out->message,sizeof(out->message),"Saved salvage track close failed");ok=false;}
        if(ok && !stopped(ops) && crc!=receipt->final_crc[i]) {
            snprintf(out->message,sizeof(out->message),"Saved salvage track %u CRC changed; not verified",i+1);ok=false;
        }
        if(ok && !stopped(ops))ops->log("Salvage saved T%02u: %" PRIu64 " bytes CRC32=%08" PRIx32 " MATCH",i+1,size,crc);
    }
    free(buffer);return ok && !stopped(ops);
}
enum kui_salvage_check_result kui_salvage_check_complete(
        const struct kui_capture_plan *plan,const struct kui_salvage_status *receipt,
        bool verify_files,const struct kui_salvage_check_ops *ops,
        struct kui_salvage_check_status *out) {
    if(!out)return KUI_SALVAGE_CHECK_FAILED;
    memset(out,0,sizeof(*out));
    if(!ops || !ops->log || !valid_receipt(plan,receipt)) {
        snprintf(out->message,sizeof(out->message),"Resolved salvage receipt required; no files checked");return out->result;
    }
    out->tracks=plan->count;out->total=verify_files?plan->bytes:0;out->readback_requested=verify_files;
    FATFS fs;bool mounted=false,ok=false;struct check_report report={0};
    if(stopped(ops))goto done;
    if(!kui_mount(&fs,ops->log)) {snprintf(out->message,sizeof(out->message),"Cannot mount storage for recovery checks");goto done;}
    mounted=true;
    if(!identity_matches(plan,receipt,ops,out) || !report_begin(&report,plan,receipt,ops,out))goto done;
    if(verify_files) {
        out->verifying_files=true;
        if(!read_saved(plan,receipt,ops,out))goto done;
        out->saved_verified=true;out->verifying_files=false;
    }
    if(stopped(ops))goto done;
    snprintf(out->message,sizeof(out->message),"Comparing recovered tracks with Redump / TOSEC");progress(ops,out);
    if(stopped(ops))goto done;
    struct kui_known_track known[99];
    for(unsigned i=0;i<plan->count;++i)known[i]=(struct kui_known_track){
        i+1,plan->tracks[i].control==4,(uint64_t)(plan->tracks[i].end-plan->tracks[i].start)*KUI_RAW_BYTES,receipt->final_crc[i]};
    kui_known_check("0:/KUI/redump.db","0:/KUI/tosec.db",known,plan->count,
        &out->catalogue,ops->cancelled,ops->ctx);
    if(stopped(ops) || out->catalogue.result==KUI_KNOWN_CANCELLED)goto done;
    out->catalogue_checked=true;
    ops->log("Salvage %s CRC catalogue: %s: %s",out->saved_verified?"saved-file":"recovery",
        out->catalogue.catalog,kui_known_text(out->catalogue.result));
    if(out->catalogue.name[0])ops->log("Salvage reference: %s",out->catalogue.name);
    ok=report_publish(&report,ops,out);
done:
    out->verifying_files=false;
    if(report.open && f_close(&report.file)!=FR_OK) {
        snprintf(out->message,sizeof(out->message),"Recovery-check report close failed; no check success");ok=false;
    }
    free(report.text);
    if(mounted && f_mount(NULL,"0:",0)!=FR_OK) {
        snprintf(out->message,sizeof(out->message),"Recovery check unmount failed; no verification success");ok=false;
        out->report_written=false;
    }
    if(!ok) {out->saved_verified=false;out->catalogue_checked=false;}
    if(stopped(ops)) {
        out->result=KUI_SALVAGE_CHECK_STOPPED;
        snprintf(out->message,sizeof(out->message),"Recovery checks stopped; recovered data retained");
    } else if(ok) {
        out->result=KUI_SALVAGE_CHECK_COMPLETE;out->complete=true;
        snprintf(out->message,sizeof(out->message),out->saved_verified?
            "All targets resolved; every saved track byte verified":
            "All targets resolved; saved tracks not read back");
    }
    progress(ops,out);return out->result;
}
