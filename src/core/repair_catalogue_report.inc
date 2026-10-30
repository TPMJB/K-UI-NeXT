/* SPDX-License-Identifier: GPL-3.0-only */
/* Every comparison is a new observation. A later database can improve or
 * change its grade without changing the immutable saved-byte copy receipt. */
static bool repair_catalogue_report(struct recovery_copy *s) {
    unsigned number;char final[32],temporary[32];FILINFO info;
    for(number=1;number<=9999;++number) {
        if(cancelled(s))return false;
        snprintf(final,sizeof(final),"catalogue-%04u.txt",number);job_path(s,final);FRESULT r=f_stat(s->path,&info);
        if(r!=FR_OK && r!=FR_NO_FILE)return fail(s,"Cannot inspect catalogue check reports");
        if(r==FR_OK)continue;
        snprintf(temporary,sizeof(temporary),"catalogue-%04u.part",number);job_path(s,temporary);r=f_stat(s->path,&info);
        if(r==FR_NO_FILE)break;
        if(r!=FR_OK)return fail(s,"Cannot inspect interrupted catalogue report");
    }
    if(number>9999)return fail(s,"Catalogue report namespace exhausted; saved copy retained");
    char binding[65];kui_hex(s->binding,32,binding);
    int n=snprintf(s->metadata,sizeof(s->metadata),"K-UI repair catalogue observation v1\nSOURCE 0:%s\nJOB %s\nBINDING %s\n"
        "REFERENCE catalogue=%s result=%d grade=%s\nREFERENCE_TITLE %s\n",s->directory,s->job,binding,
        s->repair->catalogue.catalog,(int)s->repair->catalogue.result,kui_known_text(s->repair->catalogue.result),s->repair->catalogue.name);
    if(n<0 || (size_t)n>=sizeof(s->metadata))return fail(s,"Catalogue report exceeds buffer");
    size_t used=(size_t)n;
    for(unsigned i=0;i<s->source.plan.count;++i) {
        const struct kui_capture_track *t=&s->source.plan.tracks[i];char digest[65];kui_hex(s->sha[i],32,digest);
        n=snprintf(s->metadata+used,sizeof(s->metadata)-used,"TRACK %u bytes=%" PRIu64 " CRC32=%08" PRIx32 " SHA256=%s\n",i+1,
            (uint64_t)(t->end-t->start)*KUI_RAW_BYTES,s->crc[i],digest);
        if(n<0 || (size_t)n>=sizeof(s->metadata)-used)return fail(s,"Catalogue track receipt exceeds buffer");
        used+=(size_t)n;
    }
    if(sizeof(s->metadata)-used<10u)return fail(s,"Catalogue footer exceeds buffer");
    memcpy(s->metadata+used,"COMPLETE\n",9);used+=9;
    uint32_t expected=kui_crc32(0,s->metadata,used);FIL f;job_path(s,temporary);
    if(f_open(&f,s->path,FA_READ|FA_WRITE|FA_CREATE_NEW)!=FR_OK)return fail(s,"Cannot create catalogue check report");
    bool ok=exact_write(s,&f,s->metadata,(unsigned)used) && sync_file(s,&f) && repair_seek(s,&f,0);
    uint8_t check[512];size_t at=0;uint32_t crc=0;
    while(ok && at<used) {unsigned bytes=(unsigned)(used-at>sizeof(check)?sizeof(check):used-at);ok=exact_read(s,&f,check,bytes) && !memcmp(check,s->metadata+at,bytes);if(ok) {crc=kui_crc32(crc,check,bytes);at+=bytes;}}
    if(ok && (crc!=expected || kui_crc32(0,s->metadata,used)!=expected))ok=false;
    if(!ok && !s->failed)fail(s,"Catalogue report source/readback mismatch");
    if(!close_file(s,&f,ok))return false;
    char destination[KUI_DEST_PATH_CAP];snprintf(destination,sizeof(destination),"0:%s/%s",s->job,final);job_path(s,temporary);
    if(f_rename(s->path,destination)!=FR_OK)return fail(s,"Cannot publish catalogue check report");
    if(f_open(&f,destination,FA_READ)!=FR_OK)return fail(s,"Cannot verify published catalogue check report");
    ok=f_size(&f)==used;at=0;crc=0;
    while(ok && at<used) {unsigned bytes=(unsigned)(used-at>sizeof(check)?sizeof(check):used-at);ok=exact_read(s,&f,check,bytes) && !memcmp(check,s->metadata+at,bytes);if(ok) {crc=kui_crc32(crc,check,bytes);at+=bytes;}}
    if(ok && (crc!=expected || kui_crc32(0,s->metadata,used)!=expected))ok=false;
    if(!ok && !s->failed)fail(s,"Published catalogue report differs from its receipt");
    return close_file(s,&f,ok);
}
