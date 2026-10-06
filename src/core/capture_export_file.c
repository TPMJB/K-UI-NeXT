/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/capture_export.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

struct export_files {
    const struct kui_capture_plan *plan;
    const struct kui_capture_ops *ops;
    const char *dir;
    FIL source,output;
    unsigned track;
    bool source_open,create;
    uint64_t started;
};
static bool read_exact(FIL *file,uint64_t offset,void *data,size_t bytes) {
    if(offset>(uint64_t)(FSIZE_t)-1 || bytes>UINT32_MAX ||
       offset>f_size(file) || bytes>f_size(file)-offset || f_lseek(file,(FSIZE_t)offset)!=FR_OK) return false;
    UINT got=0;return f_read(file,data,(UINT)bytes,&got)==FR_OK && got==bytes;
}
static bool track_read(void *arg,unsigned track,uint64_t offset,void *data,size_t bytes) {
    struct export_files *files=arg;
    if(track>=files->plan->count) return false;
    if(!files->source_open || track!=files->track) {
        if(files->source_open && f_close(&files->source)!=FR_OK) {files->source_open=false;return false;}
        files->source_open=false;
        char path[KUI_DEST_PATH_CAP];
        int n=snprintf(path,sizeof(path),"%s/track%02" PRIu32 ".%s",files->dir,
            files->plan->tracks[track].number,files->plan->tracks[track].control==4u?"bin":"raw");
        if(n<0 || (size_t)n>=sizeof(path) || f_open(&files->source,path,FA_READ)!=FR_OK) return false;
        files->source_open=true;files->track=track;
        uint64_t expected=(uint64_t)(files->plan->tracks[track].end-files->plan->tracks[track].start)*KUI_RAW_BYTES;
        if(f_size(&files->source)!=expected) return false;
    }
    return read_exact(&files->source,offset,data,bytes);
}
static bool output_read(void *arg,uint64_t offset,void *data,size_t bytes) {
    struct export_files *files=arg;return read_exact(&files->output,offset,data,bytes);
}
static bool output_write(void *arg,uint64_t offset,const void *data,size_t bytes) {
    struct export_files *files=arg;
    /* Verify reconstructs the expected container without touching the card.
     * The core subsequently decodes and compares the actual on-card image. */
    if(!files->create) return true;
    if(offset>(uint64_t)(FSIZE_t)-1 || bytes>UINT32_MAX || f_lseek(&files->output,(FSIZE_t)offset)!=FR_OK) return false;
    UINT put=0;return f_write(&files->output,data,(UINT)bytes,&put)==FR_OK && put==bytes;
}
static bool output_sync(void *arg) {
    struct export_files *files=arg;return !files->create || f_sync(&files->output)==FR_OK;
}
static bool cancelled(void *arg) {
    struct export_files *files=arg;
    return files->ops->cancelled && files->ops->cancelled(files->ops->ctx);
}
static void export_progress(void *arg,uint64_t done,uint64_t total,bool verify) {
    struct export_files *files=arg;
    if(!files->ops->progress) return;
    struct kui_capture_progress p={.phase=verify?KUI_VERIFYING:KUI_EXPORTING,
        .tracks=files->plan->count,.done=done,.total=total,.committed=files->plan->bytes};
    if(files->ops->now_ms) p.elapsed_ms=files->ops->now_ms(files->ops->ctx)-files->started;
    files->ops->progress(files->ops->ctx,&p);
}
static bool safe_name(const char *name) {
    if(!name || !*name || strlen(name)>KUI_DEST_TITLE_CAP+4u || !strcmp(name,".") || !strcmp(name,"..")) return false;
    for(const unsigned char *p=(const unsigned char *)name;*p;p++)
        if(*p<32u || *p>126u || strchr("/\\:\"*?<>|",*p)) return false;
    return true;
}
static bool existing_matches(const char *path,const struct kui_capture_export_report *report,
    const struct kui_capture_ops *ops) {
    FIL file;if(f_open(&file,path,FA_READ)!=FR_OK) return false;
    bool ok=f_size(&file)==report->bytes;
    struct kui_sha256 hash;kui_sha256_init(&hash);uint8_t data[4096],digest[32];uint32_t crc=0;
    for(uint64_t offset=0;ok && offset<report->bytes;) {
        if(ops->cancelled && ops->cancelled(ops->ctx)) {ok=false;break;}
        size_t n=report->bytes-offset>sizeof(data)?sizeof(data):(size_t)(report->bytes-offset);
        ok=read_exact(&file,offset,data,n);
        if(ok) {kui_sha256_update(&hash,data,n);crc=kui_crc32(crc,data,n);offset+=n;}
    }
    if(f_close(&file)!=FR_OK) ok=false;
    kui_sha256_digest(&hash,digest);
    return ok && crc==report->crc32 && !memcmp(digest,report->sha256,32);
}
enum kui_capture_export_result kui_capture_export_file(const struct kui_capture_plan *plan,
    const struct kui_checkpoint *state,const struct kui_capture_ops *ops,const char *dir,
    const char *primary,bool create,struct kui_capture_export_report *report) {
    if(report) memset(report,0,sizeof(*report));
    if(!plan || !state || !ops || !dir || !report || !safe_name(primary)) return KUI_EXPORT_FAILED;
    char reason[192];
    if(!kui_capture_export_preflight(plan,state,state->format,reason,sizeof(reason))) {
        if(ops->log) ops->log("Export refused: %s",reason);
        return KUI_EXPORT_FAILED;
    }
    char final[KUI_DEST_PATH_CAP],temporary[KUI_DEST_PATH_CAP];
    int n=snprintf(final,sizeof(final),"%s/%s",dir,primary);
    int m=snprintf(temporary,sizeof(temporary),"%s/%s.kui-export.tmp",dir,primary);
    if(n<0 || m<0 || (size_t)n>=sizeof(final) || (size_t)m>=sizeof(temporary)) return KUI_EXPORT_FAILED;
    if(create) {
        FILINFO info;FRESULT existing=f_stat(final,&info);
        if(existing==FR_OK) {
            if(info.fattrib&AM_DIR) return KUI_EXPORT_FAILED;
            /* A crash after atomic rename but before manifest publication
             * resumes with no duplicate derivative storage allocation. */
            return kui_capture_export_file(plan,state,ops,dir,primary,false,report);
        }
        if(existing!=FR_NO_FILE) return KUI_EXPORT_FAILED;
    }
    if(ops->log) {
        ops->log(create?"Exporting %s; raw capture remains authoritative":"Verifying existing %s without writes",primary);
        if(state->format==KUI_CAPTURE_FORMAT_CSO || state->format==KUI_CAPTURE_FORMAT_ZSO)
            ops->log("Cooked high-density data only; raw framing, low-density tracks and CDDA remain in source files");
        if(state->format==KUI_CAPTURE_FORMAT_ZSO) ops->log("ZSO codec: DreamShell LZO1X (not LZ4)");
        if(state->format==KUI_CAPTURE_FORMAT_CHD)
            ops->log("CHDv4: captured mainchannel/audio, declared zero gap padding, absent subchannels; CUE extraction recommended");
    }
    struct export_files files={.plan=plan,.ops=ops,.dir=dir,.create=create};
    if(ops->now_ms) files.started=ops->now_ms(ops->ctx);
    if(create) {
        FRESULT removed=f_unlink(temporary);
        if(removed!=FR_OK && removed!=FR_NO_FILE) return KUI_EXPORT_FAILED;
    }
    if(f_open(&files.output,create?temporary:final,create?(FA_READ|FA_WRITE|FA_CREATE_NEW):FA_READ)!=FR_OK)
        return KUI_EXPORT_FAILED;
    struct kui_capture_export_io io={.ctx=&files,.track_read=track_read,.read=output_read,.write=output_write,
        .sync=output_sync,.cancelled=cancelled,.progress=export_progress};
    enum kui_capture_export_result result=kui_capture_export_write(plan,state,state->format,&io,report);
    if(result==KUI_EXPORT_COMPLETE && f_size(&files.output)!=report->bytes) result=KUI_EXPORT_FAILED;
    if(files.source_open && f_close(&files.source)!=FR_OK) result=KUI_EXPORT_FAILED;
    if(f_close(&files.output)!=FR_OK) result=KUI_EXPORT_FAILED;
    if(result==KUI_EXPORT_COMPLETE && create) {
        if(cancelled(&files)) result=KUI_EXPORT_STOPPED;
        else {
            FILINFO info;FRESULT exists=f_stat(final,&info);
            if(exists==FR_NO_FILE) {if(f_rename(temporary,final)!=FR_OK) result=KUI_EXPORT_FAILED;}
            else if(exists==FR_OK && !(info.fattrib&AM_DIR) && existing_matches(final,report,ops)) {
                if(f_unlink(temporary)!=FR_OK) result=KUI_EXPORT_FAILED;
            } else {
                if(ops->log) ops->log("Existing output differs; preserved %s",final);
                result=cancelled(&files)?KUI_EXPORT_STOPPED:KUI_EXPORT_FAILED;
            }
        }
    }
    if(result!=KUI_EXPORT_COMPLETE) {
        memset(report,0,sizeof(*report));
        if(ops->log) ops->log(result==KUI_EXPORT_STOPPED?
            "Export stopped; raw capture is safe, Resume restarts export":"Export failed; raw capture and existing final output preserved");
    } else if(ops->log) ops->log("Decoded output verified: %s (%" PRIu64 " bytes)",primary,report->bytes);
    return result;
}
