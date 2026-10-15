/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/capture_export.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* A post-capture derivative. The raw tracks and checkpoint remain authoritative;
 * the public descriptor is published only after all cooked files are verified.
 * One serialized worker owns these buffers, as for the other export codecs. */
static uint8_t raw[KUI_RAW_BYTES],saved[KUI_DATA_BYTES];
static char descriptor[8192];
static bool stopped(const struct kui_capture_ops *ops) {
    return ops->cancelled && ops->cancelled(ops->ctx);
}
static bool exact_read(FIL *f,void *p,UINT n) {
    UINT got=0;return f_read(f,p,n,&got)==FR_OK && got==n;
}
static bool exact_write(FIL *f,const void *p,UINT n) {
    UINT put=0;return f_write(f,p,n,&put)==FR_OK && put==n;
}
static void progress(const struct kui_capture_plan *plan,const struct kui_capture_ops *ops,
    unsigned track,uint64_t done,uint64_t total) {
    if(ops->progress) {
        const struct kui_capture_progress p={.phase=KUI_EXPORTING,.track=track+1u,
            .tracks=plan->count,.done=done,.total=total,.committed=plan->bytes};
        ops->progress(ops->ctx,&p);
    }
}
static bool path(char out[KUI_DEST_PATH_CAP],const char *dir,unsigned track,const char *suffix) {
    int n=snprintf(out,KUI_DEST_PATH_CAP,"%s/track%02u.%s",dir,track,suffix);
    return n>0 && (size_t)n<KUI_DEST_PATH_CAP;
}
static bool convert_track(const struct kui_capture_plan *plan,const struct kui_checkpoint *state,
    const struct kui_capture_ops *ops,const char *dir,unsigned i,bool create,uint64_t *done,uint64_t total) {
    const struct kui_capture_track *t=&plan->tracks[i];
    char source[KUI_DEST_PATH_CAP],final[KUI_DEST_PATH_CAP],temporary[KUI_DEST_PATH_CAP];
    if(!path(source,dir,t->number,"bin") || !path(final,dir,t->number,"iso") ||
       !path(temporary,dir,t->number,"kui-cooked.tmp"))return false;
    FIL input,output;FILINFO info;bool writing=false;
    FRESULT exists=f_stat(final,&info);
    if(exists==FR_OK && (info.fattrib&AM_DIR))return false;
    if(exists!=FR_OK && exists!=FR_NO_FILE)return false;
    if(!create && exists!=FR_OK)return false;
    if(f_open(&input,source,FA_READ)!=FR_OK)return false;
    bool ok=f_size(&input)==(uint64_t)state->track[i].sectors*KUI_RAW_BYTES &&
        state->track[i].sectors==t->end-t->start;
    if(ok && exists==FR_NO_FILE) {
        FRESULT removed=f_unlink(temporary);
        ok=removed==FR_OK || removed==FR_NO_FILE;
        writing=ok;
    }
    bool opened=ok && f_open(&output,writing?temporary:final,
        writing?(FA_READ|FA_WRITE|FA_CREATE_NEW):FA_READ)==FR_OK;
    ok=opened && (writing || f_size(&output)==(uint64_t)state->track[i].sectors*KUI_DATA_BYTES);
    uint32_t crc=0;struct kui_sha256 hash,cooked_hash;kui_sha256_init(&hash);kui_sha256_init(&cooked_hash);
    for(uint32_t sector=0;ok && sector<state->track[i].sectors;sector++) {
        if(stopped(ops)) {ok=false;break;}
        ok=exact_read(&input,raw,sizeof(raw));
        int offset=ok?kui_data_offset(raw):-1;
        ok=ok && offset>=0 && kui_sector_edc_valid(raw);
        if(!ok)break;
        crc=kui_crc32(crc,raw,sizeof(raw));kui_sha256_update(&hash,raw,sizeof(raw));
        kui_sha256_update(&cooked_hash,raw+(unsigned)offset,KUI_DATA_BYTES);
        if(writing)ok=exact_write(&output,raw+(unsigned)offset,KUI_DATA_BYTES);
        else ok=exact_read(&output,saved,sizeof(saved)) && !memcmp(saved,raw+(unsigned)offset,sizeof(saved));
        *done+=KUI_DATA_BYTES;
        if(!(sector%32u))progress(plan,ops,i,*done,total);
    }
    uint8_t digest[32],expected_cooked[32];kui_sha256_digest(&hash,digest);kui_sha256_digest(&cooked_hash,expected_cooked);
    ok=ok && crc==state->track[i].crc32 && (state->crc_only || !memcmp(digest,state->track[i].sha256,32));
    if(ok && writing) {
        ok=f_sync(&output)==FR_OK && f_lseek(&output,0)==FR_OK && f_lseek(&input,0)==FR_OK;
        kui_sha256_init(&hash);kui_sha256_init(&cooked_hash);crc=0;
        /* Read every new byte back and compare with the authoritative raw file. */
        for(uint32_t sector=0;ok && sector<state->track[i].sectors;sector++) {
            if(stopped(ops)) {ok=false;break;}
            ok=exact_read(&input,raw,sizeof(raw)) && exact_read(&output,saved,sizeof(saved));
            int offset=ok?kui_data_offset(raw):-1;
            ok=ok && offset>=0 && kui_sector_edc_valid(raw) && !memcmp(saved,raw+(unsigned)offset,sizeof(saved));
            if(ok) {crc=kui_crc32(crc,raw,sizeof(raw));kui_sha256_update(&hash,raw,sizeof(raw));
                kui_sha256_update(&cooked_hash,saved,sizeof(saved));}
        }
        uint8_t actual_cooked[32];kui_sha256_digest(&hash,digest);kui_sha256_digest(&cooked_hash,actual_cooked);
        ok=ok && crc==state->track[i].crc32 && (state->crc_only || !memcmp(digest,state->track[i].sha256,32)) &&
            !memcmp(expected_cooked,actual_cooked,32);
    }
    if(opened && f_close(&output)!=FR_OK)ok=false;
    if(f_close(&input)!=FR_OK)ok=false;
    if(ok && writing)ok=!stopped(ops) && f_rename(temporary,final)==FR_OK;
    return ok;
}
enum kui_capture_export_result kui_capture_cooked_file(const struct kui_capture_plan *plan,
    const struct kui_checkpoint *state,const struct kui_capture_ops *ops,const char *dir,
    const char *primary,bool create,struct kui_capture_export_report *report) {
    if(!plan || !state || !ops || !dir || !primary || !report)return KUI_EXPORT_FAILED;
    memset(report,0,sizeof(*report));
    char why[128];
    if(state->format!=KUI_CAPTURE_FORMAT_GDI_2048 ||
       !kui_capture_export_preflight(plan,state,state->format,why,sizeof(why)) ||
       !primary[0] || strlen(primary)>KUI_DEST_TITLE_CAP+4u || strpbrk(primary,"/\\:\"*?<>|"))return KUI_EXPORT_FAILED;
    for(unsigned i=0;i<plan->count;i++)
        if(state->track[i].sectors!=plan->tracks[i].end-plan->tracks[i].start)return KUI_EXPORT_FAILED;
    size_t used=0;int n=snprintf(descriptor,sizeof(descriptor),"%u\n",plan->count);
    if(n<0 || (size_t)n>=sizeof(descriptor))return KUI_EXPORT_FAILED;
    used=(size_t)n;uint64_t total=0,done=0;
    for(unsigned i=0;i<plan->count;i++) {
        const struct kui_capture_track *t=&plan->tracks[i];bool data=t->control==4u;
        n=snprintf(descriptor+used,sizeof(descriptor)-used,"%" PRIu32 " %" PRIu32 " %" PRIu32 " %u track%02" PRIu32 ".%s 0\n",
            t->number,t->start-150u,t->control,data?2048u:2352u,t->number,data?"iso":"raw");
        if(n<0 || (size_t)n>=sizeof(descriptor)-used)return KUI_EXPORT_FAILED;
        used+=(size_t)n;
        if(data)total+=(uint64_t)state->track[i].sectors*KUI_DATA_BYTES;
    }
    for(unsigned i=0;i<plan->count;i++)if(plan->tracks[i].control==4u &&
        !convert_track(plan,state,ops,dir,i,create,&done,total))
        return stopped(ops)?KUI_EXPORT_STOPPED:KUI_EXPORT_FAILED;
    char final[KUI_DEST_PATH_CAP],temporary[KUI_DEST_PATH_CAP];
    n=snprintf(final,sizeof(final),"%s/%s",dir,primary);
    int m=snprintf(temporary,sizeof(temporary),"%s/%s.kui-export.tmp",dir,primary);
    if(n<0 || (size_t)n>=sizeof(final) || m<0 || (size_t)m>=sizeof(temporary))return KUI_EXPORT_FAILED;
    FILINFO info;FRESULT exists=f_stat(final,&info);bool writing=create && exists==FR_NO_FILE;
    if((exists!=FR_OK && exists!=FR_NO_FILE) || (!create && exists!=FR_OK) ||
       (exists==FR_OK && (info.fattrib&AM_DIR)))return KUI_EXPORT_FAILED;
    if(writing) {FRESULT r=f_unlink(temporary);if(r!=FR_OK && r!=FR_NO_FILE)return KUI_EXPORT_FAILED;}
    FIL file;if(f_open(&file,writing?temporary:final,writing?(FA_READ|FA_WRITE|FA_CREATE_NEW):FA_READ)!=FR_OK)return KUI_EXPORT_FAILED;
    bool ok=!stopped(ops);
    if(writing)ok=ok && exact_write(&file,descriptor,(UINT)used) && f_sync(&file)==FR_OK && f_lseek(&file,0)==FR_OK;
    ok=ok && f_size(&file)==used;
    for(size_t offset=0;ok && offset<used;) {
        UINT bytes=used-offset>sizeof(saved)?sizeof(saved):(UINT)(used-offset);
        ok=exact_read(&file,saved,bytes) && !memcmp(saved,descriptor+offset,bytes);offset+=bytes;
    }
    if(f_close(&file)!=FR_OK)ok=false;
    if(ok && writing)ok=!stopped(ops) && f_rename(temporary,final)==FR_OK;
    if(!ok)return stopped(ops)?KUI_EXPORT_STOPPED:KUI_EXPORT_FAILED;
    report->bytes=used;report->logical_bytes=total;report->crc32=kui_crc32(0,descriptor,used);
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,descriptor,used);kui_sha256_digest(&hash,report->sha256);
    progress(plan,ops,plan->count-1u,total,total);
    if(ops->log)ops->log("Cooked GDI verified; all raw tracks retained: %s",primary);
    return KUI_EXPORT_COMPLETE;
}
