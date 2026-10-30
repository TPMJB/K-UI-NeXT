/* SPDX-License-Identifier: GPL-3.0-only */
/* Targeted in-place repair reuses the copy worker's immutable original/candidate
 * evidence, alternating checkpoints and replayable single-sector transaction.
 * Only track path resolution differs. No original opens for writing until the
 * source identity, checkpoint, retained prefix and durable active guard agree. */
static bool inplace_binding(struct recovery_copy *s) {
    struct kui_sha256 hash;kui_sha256_init(&hash);
    kui_sha256_update(&hash,"KUI targeted in-place v1",24);
    kui_sha256_update(&hash,s->directory,strlen(s->directory)+1);
    const char *profile=kui_capture_profile_name(s->optical_plan);
    if(!profile)return fail(s,"Unsupported in-place profile");
    kui_sha256_update(&hash,profile,strlen(profile));hash32(&hash,s->source.plan.count);
    hash32(&hash,s->manifest_present);if(s->manifest_present)kui_sha256_update(&hash,s->manifest.identity,32);
    for(unsigned i=0;i<s->source.plan.count;++i) {
        const struct kui_capture_track *t=&s->source.plan.tracks[i];
        hash32(&hash,t->number);hash32(&hash,t->control);hash32(&hash,t->session);
        hash32(&hash,t->start);hash32(&hash,t->end);hash32(&hash,t->toc_end);
        kui_sha256_update(&hash,s->source.files[i],strlen(s->source.files[i])+1);
    }
    kui_sha256_digest(&hash,s->binding);return true;
}
static bool inplace_source_store(struct recovery_copy *s,const char *name,const void *data,unsigned bytes) {
    uint32_t expected=kui_crc32(0,data,bytes);
    source_path(s,name);FIL f;if(f_open(&f,s->path,FA_READ|FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)return fail(s,"Cannot stage repaired source metadata");
    bool ok=exact_write(s,&f,data,bytes) && sync_file(s,&f) && repair_seek(s,&f,0);
    uint8_t check[512];unsigned at=0;
    while(ok && at<bytes) {unsigned n=bytes-at>sizeof(check)?sizeof(check):bytes-at;ok=exact_read(s,&f,check,n) && !memcmp(check,(const uint8_t *)data+at,n);at+=n;}
    ok=ok && kui_crc32(0,data,bytes)==expected;
    return close_file(s,&f,ok) || fail(s,"Repaired source metadata readback or source-buffer check failed");
}
/* Metadata belongs to one transaction. A later repair retains its own
 * baseline, without reusing or replacing an earlier transaction's originals. */
static const char *const inplace_original_names[]={"original-manifest.json","original-checkpoint-a.bin",
    "original-checkpoint-b.bin","original-gdi.bin","original-cue.bin"};
static const char *inplace_source_name(struct recovery_copy *s,unsigned index) {
    static const char *const names[]={"manifest.json","checkpoint-a.bin","checkpoint-b.bin"};
    return index<3u?names[index]:index==3u?s->manifest.gdi:s->manifest.cue;
}
static bool inplace_job_store_once(struct recovery_copy *s,const char *name,const void *data,unsigned bytes) {
    uint32_t expected=kui_crc32(0,data,bytes);
    job_path(s,name);FIL f;FRESULT r=f_open(&f,s->path,FA_READ);
    if(r==FR_OK) {
        bool same=f_size(&f)==bytes;uint8_t check[512];unsigned at=0;
        while(same && at<bytes) {unsigned n=bytes-at>sizeof(check)?sizeof(check):bytes-at;
            same=exact_read(s,&f,check,n) && !memcmp(check,(const uint8_t *)data+at,n);at+=n;}
        same=same && kui_crc32(0,data,bytes)==expected;
        if(!same && !s->failed)fail(s,"Transaction metadata backup differs; refusing replacement");
        return close_file(s,&f,same);
    }
    if(r!=FR_NO_FILE)return fail(s,"Cannot inspect transaction metadata backup");
    if(f_open(&f,s->path,FA_READ|FA_WRITE|FA_CREATE_NEW)!=FR_OK)return fail(s,"Cannot retain transaction metadata backup");
    bool ok=exact_write(s,&f,data,bytes) && sync_file(s,&f) && repair_seek(s,&f,0);
    uint8_t check[512];unsigned at=0;
    while(ok && at<bytes) {unsigned n=bytes-at>sizeof(check)?sizeof(check):bytes-at;
        ok=exact_read(s,&f,check,n) && !memcmp(check,(const uint8_t *)data+at,n);at+=n;}
    ok=ok && kui_crc32(0,data,bytes)==expected;
    if(!ok && !s->failed)fail(s,"Transaction metadata backup readback or source buffer differs");
    return close_file(s,&f,ok);
}
static bool inplace_backup_metadata(struct recovery_copy *s) {
    uint8_t table[512]={0};memcpy(table,"KUIIPB2",8);put32(table+8,2);put32(table+12,512);put32(table+20,5);
    memcpy(table+32,s->binding,32);struct kui_sha256 hash;kui_sha256_init(&hash);
    kui_sha256_update(&hash,s->directory,strlen(s->directory)+1);kui_sha256_digest(&hash,table+64);
    uint32_t present=0;
    for(unsigned index=0;index<5u;++index) {
        const char *name=inplace_source_name(s,index);if(!name[0])continue;
        source_path(s,name);FIL f;FRESULT r=f_open(&f,s->path,FA_READ);
        if(r==FR_NO_FILE) {job_path(s,inplace_original_names[index]);FILINFO info;
            if(f_stat(s->path,&info)!=FR_NO_FILE)return fail(s,"Absent source metadata has a retained transaction backup");
            continue;}
        if(r!=FR_OK)return fail(s,"Cannot retain source metadata before in-place edits");
        uint64_t bytes=f_size(&f);bool ok=bytes && bytes<=KUI_SCAN_MANIFEST_LIMIT;
        if(ok)ok=exact_read(s,&f,s->metadata,(unsigned)bytes);
        if(!close_file(s,&f,ok))return false;
        if(!ok)return fail(s,"Source metadata backup exceeds its bound");
        if(!inplace_job_store_once(s,inplace_original_names[index],s->metadata,(unsigned)bytes))return false;
        present|=1u<<index;repair_put64(table+128u+index*40u,bytes);
        kui_sha256_init(&hash);kui_sha256_update(&hash,s->metadata,(size_t)bytes);kui_sha256_digest(&hash,table+136u+index*40u);
    }
    put32(table+16,present);put32(table+508,kui_crc32(0,table,508));
    return inplace_job_store_once(s,"original-metadata.bin",table,sizeof(table));
}
/* Admission helper. Caller has validated the v2 active guard and provisionally
 * copied its binding/job path into s. The exact backup is checked independently
 * before its path can be used to admit a manifest or partial checkpoint. */
static bool inplace_original_path(struct recovery_copy *s,unsigned index,bool *present) {
    *present=false;if(index>=5u)return fail(s,"Invalid transaction metadata index");
    job_path(s,"original-metadata.bin");FIL f;uint8_t table[512];
    if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"In-place original metadata receipt is missing");
    bool ok=f_size(&f)==512 && exact_read(s,&f,table,512);
    if(!close_file(s,&f,ok))return false;
    uint8_t directory[32];struct kui_sha256 hash;kui_sha256_init(&hash);
    kui_sha256_update(&hash,s->directory,strlen(s->directory)+1);kui_sha256_digest(&hash,directory);
    if(!ok || memcmp(table,"KUIIPB2",8) || repair_get32(table+8)!=2 || repair_get32(table+12)!=512 ||
       repair_get32(table+20)!=5 || (repair_get32(table+16)&~31u) || memcmp(table+32,s->binding,32) ||
       memcmp(table+64,directory,32) || repair_get32(table+508)!=kui_crc32(0,table,508))
        return fail(s,"In-place original metadata receipt is damaged or belongs elsewhere");
    job_path(s,inplace_original_names[index]);
    if(!(repair_get32(table+16)&(1u<<index))) {
        FILINFO info;if(f_stat(s->path,&info)!=FR_NO_FILE)return fail(s,"Unexpected metadata backup for an absent original");
        return true;
    }
    uint64_t bytes=repair_get64(table+128u+index*40u);
    if(!bytes || bytes>KUI_SCAN_MANIFEST_LIMIT || f_open(&f,s->path,FA_READ)!=FR_OK)
        return fail(s,"Original transaction metadata is missing or too large");
    ok=f_size(&f)==bytes;uint8_t buffer[512];kui_sha256_init(&hash);uint64_t at=0;
    while(ok && at<bytes) {unsigned n=(unsigned)(bytes-at>sizeof(buffer)?sizeof(buffer):bytes-at);
        ok=exact_read(s,&f,buffer,n);if(ok)kui_sha256_update(&hash,buffer,n);at+=n;}
    if(!close_file(s,&f,ok))return false;
    uint8_t digest[32];kui_sha256_digest(&hash,digest);
    if(!ok || memcmp(digest,table+136u+index*40u,32))return fail(s,"Original transaction metadata checksum differs");
    *present=true;return true;
}
static bool inplace_metadata_digest(struct recovery_copy *s,const char *name,uint8_t digest[32]) {
    source_path(s,name);FIL f;if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot read completed in-place metadata");
    uint64_t bytes=f_size(&f);bool ok=bytes && bytes<=KUI_SCAN_MANIFEST_LIMIT;struct kui_sha256 hash;kui_sha256_init(&hash);
    uint8_t data[512];while(ok && bytes) {unsigned n=(unsigned)(bytes>sizeof(data)?sizeof(data):bytes);
        ok=exact_read(s,&f,data,n);if(ok)kui_sha256_update(&hash,data,n);bytes-=n;}
    if(!close_file(s,&f,ok))return false;
    if(!ok)return fail(s,"Completed in-place metadata length is invalid");
    kui_sha256_digest(&hash,digest);return true;
}
static bool inplace_job_complete(struct recovery_copy *s) {
    if(!s->state_sequence)return fail(s,"Cannot complete in-place repair without a checkpoint");
    job_path(s,(s->state_sequence&1u)?"resume-a.bin":"resume-b.bin");FIL f;
    if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Final in-place repair checkpoint is missing");
    bool ok=f_size(&f)==REPAIR_STATE_BYTES && exact_read(s,&f,s->state,REPAIR_STATE_BYTES);
    if(!close_file(s,&f,ok))return false;
    if(!ok || memcmp(s->state,"KUIRPC2",8) || repair_get32(s->state+8)!=2 ||
       repair_get64(s->state+16)!=s->state_sequence || memcmp(s->state+24,s->binding,32) ||
       repair_get64(s->state+64)!=s->source.plan.bytes ||
       repair_get32(s->state+REPAIR_STATE_BYTES-4)!=kui_crc32(0,s->state,REPAIR_STATE_BYTES-4))
        return fail(s,"Final in-place repair checkpoint differs from transaction");
    for(unsigned i=0;i<s->source.plan.count;++i) {
        const uint8_t *track=s->state+128+i*80;const struct kui_capture_track *t=&s->source.plan.tracks[i];
        if(repair_get32(track)!=t->end-t->start || repair_get32(track+4)!=s->crc[i] ||
           !(s->state[8064+i/8u]&(1u<<(i%8u))) || memcmp(track+8,s->sha[i],32))
            return fail(s,"Final in-place track checkpoint differs from saved-byte readback");
    }
    uint8_t record[512]={0};memcpy(record,"KUIIPC2",8);put32(record+8,2);put32(record+12,512);
    repair_put64(record+16,s->state_sequence);memcpy(record+24,s->binding,32);
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,s->directory,strlen(s->directory)+1);kui_sha256_digest(&hash,record+56);
    kui_sha256_init(&hash);kui_sha256_update(&hash,s->state,REPAIR_STATE_BYTES);kui_sha256_digest(&hash,record+88);
    put32(record+120,s->source.plan.count);put32(record+124,(uint32_t)s->source.plan.profile);
    put32(record+508,kui_crc32(0,record,508));
    if(!repair_store(s,"inplace-complete.part",record,512))return false;
    /* This record may be regenerated while the active guard still owns the
     * source after a stopped metadata publication. No unguarded edits depend
     * on replacing an earlier completed transaction's job record. */
    job_path(s,"inplace-complete.bin");char final[KUI_DEST_PATH_CAP];snprintf(final,sizeof(final),"%s",s->path);
    FRESULT r=f_unlink(s->path);if(r!=FR_OK && r!=FR_NO_FILE)return fail(s,"Cannot update guarded in-place completion record");
    job_path(s,"inplace-complete.part");if(f_rename(s->path,final)!=FR_OK)return fail(s,"Cannot publish in-place transaction completion record");
    job_path(s,"inplace-complete.bin");uint8_t saved[512];
    if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot reopen in-place transaction completion record");
    ok=f_size(&f)==512 && exact_read(s,&f,saved,512) && !memcmp(saved,record,512);
    if(!ok && !s->failed)fail(s,"In-place completion record readback differs");
    return close_file(s,&f,ok);
}
/* A finished transaction may seed a new repair after later corruption, but
 * only if its metadata receipt and durable completion still agree. Track data
 * is deliberately not checked against the old prefix: that is what the new
 * repair is being asked to diagnose. Current source metadata remains trusted. */
static bool inplace_start_fresh(struct recovery_copy *s) {
    FILINFO info;source_path(s,".kui-inplace-active");FRESULT r=f_stat(s->path,&info);
    if(r==FR_OK)return false;
    if(r!=FR_NO_FILE)return fail(s,"Cannot inspect prior in-place ownership");
    source_path(s,".kui-inplace-active.part");r=f_stat(s->path,&info);
    if(r==FR_OK)return false;
    if(r!=FR_NO_FILE)return fail(s,"Cannot inspect interrupted in-place ownership");
    /* A new job can be durably initialized before its active guard is
     * published. The previous receipt then still exists in the source; it
     * cannot turn this unfinished latest job into another fresh transaction. */
    job_path(s,"evidence.txt");r=f_stat(s->path,&info);
    if(r==FR_NO_FILE)return false;
    if(r!=FR_OK)return fail(s,"Cannot inspect prior finalized repair evidence");
    FIL f;bool ok=false;uint8_t expected[32];struct kui_sha256 hash;
    if(s->manifest_present) {
    source_path(s,".kui-inplace-complete");r=f_open(&f,s->path,FA_READ);
    if(r==FR_NO_FILE)return false;
    if(r!=FR_OK)return fail(s,"Cannot inspect prior in-place completion");
    uint8_t receipt[512];ok=f_size(&f)==512 && exact_read(s,&f,receipt,512);
    if(!close_file(s,&f,ok))return false;
    kui_sha256_init(&hash);
    kui_sha256_update(&hash,s->directory,strlen(s->directory)+1);kui_sha256_digest(&hash,expected);
    if(!ok || memcmp(receipt,"KUIIPD1",8) || repair_get32(receipt+8)!=1 || repair_get32(receipt+12)!=512 ||
       memcmp(receipt+80,expected,32) || repair_get32(receipt+508)!=kui_crc32(0,receipt,508))
        return fail(s,"Prior in-place completion receipt is damaged or belongs elsewhere");
    for(unsigned i=176;i<508;++i)if(receipt[i])return fail(s,"Unknown prior in-place completion fields");
    if(!inplace_metadata_digest(s,"manifest.json",expected) || memcmp(expected,receipt+16,32) ||
       !inplace_metadata_digest(s,"checkpoint-a.bin",expected) || memcmp(expected,receipt+48,32) ||
       !inplace_metadata_digest(s,"checkpoint-b.bin",expected) || memcmp(expected,receipt+48,32) ||
       !inplace_metadata_digest(s,s->manifest.gdi,expected) || memcmp(expected,receipt+112,32) ||
       (s->manifest.format==KUI_CAPTURE_FORMAT_BIN_CUE &&
        (!inplace_metadata_digest(s,s->manifest.cue,expected) || memcmp(expected,receipt+144,32))))
        return s->failed?false:fail(s,"Prior completion does not match current source metadata");
    } else {
        /* Imports have no source manifest/checkpoints. Their exact original
         * descriptor and the completed job state form the metadata receipt. */
        unsigned descriptor=s->manifest.gdi[0]?3u:4u;
        const char *name=descriptor==3u?s->manifest.gdi:s->manifest.cue;
        if(!name[0])return fail(s,"Imported repair descriptor name was not retained");
        bool present=false;if(!inplace_original_path(s,descriptor,&present) || !present)return s->failed?false:fail(s,"Imported descriptor transaction backup is missing");
        job_path(s,"original-metadata.bin");uint8_t table[512];
        if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Imported metadata receipt is missing");
        ok=f_size(&f)==512 && exact_read(s,&f,table,512);if(!close_file(s,&f,ok))return false;
        if(!ok || !inplace_metadata_digest(s,name,expected) || memcmp(expected,table+136u+descriptor*40u,32))
            return s->failed?false:fail(s,"Imported source descriptor changed after completed repair");
    }
    job_path(s,"resume.bin");uint8_t header[512];
    if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Prior completed repair header is missing");
    ok=f_size(&f)==512 && exact_read(s,&f,header,512);if(!close_file(s,&f,ok))return false;
    if(!ok || memcmp(header,"KUIRPH2",8) || repair_get32(header+8)!=2 || repair_get32(header+12)!=1 ||
       memcmp(header+32,s->binding,32) || !memchr(header+128,0,KUI_DEST_JOB_CAP) ||
       strcmp((const char *)header+128,s->directory) || repair_get32(header+508)!=kui_crc32(0,header,508))
        return fail(s,"Prior repair header does not bind this completed source");
    uint64_t sequence=0,fence=0,records=0;
    for(unsigned slot=0;slot<2u;++slot) {
        job_path(s,slot?"resume-b.bin":"resume-a.bin");r=f_open(&f,s->path,FA_READ);if(r==FR_NO_FILE)continue;
        if(r!=FR_OK)return fail(s,"Cannot inspect completed repair checkpoints");
        ok=f_size(&f)==REPAIR_STATE_BYTES && exact_read(s,&f,s->state,REPAIR_STATE_BYTES);
        if(!close_file(s,&f,true))return false;
        if(s->failed)return false;
        const uint8_t *state=s->state;
        bool valid=ok && !memcmp(state,"KUIRPC2",8) && repair_get32(state+8)==2 && repair_get32(state+12)==REPAIR_STATE_BYTES &&
            !memcmp(state+24,s->binding,32) && (repair_get32(state+56)&2u) && repair_get32(state+60)==s->source.plan.count &&
            repair_get32(state+REPAIR_STATE_BYTES-4)==kui_crc32(0,state,REPAIR_STATE_BYTES-4);
        uint64_t current=valid?repair_get64(state+16):0;
        if(current>sequence) {sequence=current;memcpy(s->metadata,state,REPAIR_STATE_BYTES);}
    }
    if(!sequence)return fail(s,"No valid completed repair evidence checkpoint");
    memcpy(s->state,s->metadata,REPAIR_STATE_BYTES);const uint8_t *state=s->state;
    bool final=repair_get64(state+64)==s->source.plan.bytes && repair_get32(state+76)<=repair_get32(state+72);
    for(unsigned i=0;i<s->source.plan.count && final;++i) {
        const uint8_t *track=state+128+i*80;const struct kui_capture_track *t=&s->source.plan.tracks[i];
        final=repair_get32(track)==t->end-t->start && (state[8064+i/8u]&(1u<<(i%8u))) &&
            (!s->manifest_present || (repair_get32(track+4)==s->manifest.track[i].crc32 && !memcmp(track+8,s->manifest.track[i].sha256,32)));
    }
    if(!final)return fail(s,"Prior completed repair state differs from published metadata");
    job_path(s,"inplace-complete.bin");uint8_t completed[512];
    if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Prior in-place transaction completion record is missing");
    ok=f_size(&f)==512 && exact_read(s,&f,completed,512);if(!close_file(s,&f,ok))return false;
    kui_sha256_init(&hash);kui_sha256_update(&hash,s->directory,strlen(s->directory)+1);kui_sha256_digest(&hash,expected);
    if(!ok || memcmp(completed,"KUIIPC2",8) || repair_get32(completed+8)!=2 || repair_get32(completed+12)!=512 ||
       repair_get64(completed+16)!=sequence || memcmp(completed+24,s->binding,32) || memcmp(completed+56,expected,32) ||
       repair_get32(completed+120)!=s->source.plan.count || repair_get32(completed+124)!=(uint32_t)s->source.plan.profile ||
       repair_get32(completed+508)!=kui_crc32(0,completed,508))return fail(s,"Prior in-place completion does not bind the latest completed state");
    for(unsigned i=128;i<508;++i)if(completed[i])return fail(s,"Unknown in-place completion record fields");
    kui_sha256_init(&hash);kui_sha256_update(&hash,state,REPAIR_STATE_BYTES);kui_sha256_digest(&hash,expected);
    if(memcmp(completed+88,expected,32))return fail(s,"Prior in-place checkpoint checksum differs from completion record");
    records=repair_get64(state+88);fence=repair_get64(state+96);
    if(!sequence || records>UINT64_MAX/REPAIR_EVIDENCE_BYTES || fence!=records*REPAIR_EVIDENCE_BYTES)
        return fail(s,"No valid completed repair evidence checkpoint");
    job_path(s,"samples.bin");if(f_stat(s->path,&info)!=FR_OK || info.fsize!=fence)return fail(s,"Prior completed repair evidence length differs");
    job_path(s,"pending.bin");if(f_stat(s->path,&info)!=FR_NO_FILE)return fail(s,"Prior repair still has a pending patch");
    job_path(s,"chunk.bin");if(f_stat(s->path,&info)!=FR_NO_FILE)return fail(s,"Prior repair still has a pending copied chunk");
    job_path(s,"evidence.txt");if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Prior repair evidence was not finalized");
    uint64_t bytes=f_size(&f),start=bytes>4096u?bytes-4096u:0u;unsigned n=(unsigned)(bytes-start);
    ok=n && repair_seek(s,&f,start) && exact_read(s,&f,s->metadata,n);if(!close_file(s,&f,ok))return false;
    s->metadata[n]=0;
    if(!ok || !strstr(s->metadata,"COMPLETE IN_PLACE all changed sectors backed up"))return fail(s,"Prior repair has no completed evidence footer");
    return true;
}
static bool inplace_forget_complete(struct recovery_copy *s) {
    source_path(s,".kui-inplace-complete");FRESULT r=f_unlink(s->path);
    if(r!=FR_OK && r!=FR_NO_FILE)return fail(s,"Cannot retire old in-place completion receipt");
    source_path(s,".kui-inplace-complete.part");r=f_unlink(s->path);
    return (r==FR_OK || r==FR_NO_FILE) || fail(s,"Cannot retire interrupted in-place completion receipt");
}
static bool inplace_history(struct recovery_copy *s) {
    uint8_t history[512]={0};memcpy(history,"KUIIPH2",8);put32(history+8,2);put32(history+12,512);
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,s->directory,strlen(s->directory)+1);
    kui_sha256_digest(&hash,history+16);put32(history+508,kui_crc32(0,history,508));
    source_path(s,".kui-inplace-history");FIL f;FRESULT r=f_open(&f,s->path,FA_READ);
    if(r==FR_OK) {
        uint8_t saved[512];bool same=f_size(&f)==512 && exact_read(s,&f,saved,512) && !memcmp(saved,history,512);
        if(!same && !s->failed)fail(s,"In-place history marker differs or belongs elsewhere");
        return close_file(s,&f,same);
    }
    if(r!=FR_NO_FILE)return fail(s,"Cannot inspect permanent in-place history marker");
    if(!inplace_source_store(s,".kui-inplace-history.part",history,512))return false;
    char final[KUI_DEST_PATH_CAP];snprintf(final,sizeof(final),"0:%s/.kui-inplace-history",s->directory);
    source_path(s,".kui-inplace-history.part");
    if(f_rename(s->path,final)!=FR_OK)return fail(s,"Cannot retain permanent in-place history marker");
    source_path(s,".kui-inplace-history");
    if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot read back permanent in-place history marker");
    uint8_t saved[512];bool same=f_size(&f)==512 && exact_read(s,&f,saved,512) && !memcmp(saved,history,512);
    if(!same && !s->failed)fail(s,"Permanent in-place history marker readback differs");
    return close_file(s,&f,same);
}
static bool inplace_ready(struct recovery_copy *s) {
    if(s->manifest_present && s->manifest.format>=KUI_CAPTURE_FORMAT_CSO)
        return fail(s,"In-place raw edits would invalidate derived containers; use a separate raw repair copy");
    uint8_t marker[512]={0};memcpy(marker,"KUIIPG2",8);put32(marker+8,2);put32(marker+12,s->partial_source?1u:0u);memcpy(marker+16,s->binding,32);
    memcpy(marker+64,s->job,strlen(s->job)+1);put32(marker+508,kui_crc32(0,marker,508));
    source_path(s,".kui-inplace-active");FIL f;FRESULT r=f_open(&f,s->path,FA_READ);
    if(r==FR_OK) {
        uint8_t saved[512];bool same=f_size(&f)==sizeof(saved) && exact_read(s,&f,saved,sizeof(saved));
        if(same && !memcmp(saved,"KUIIPG1",8) && repair_get32(saved+8)==1u) {
            memcpy(marker,"KUIIPG1",8);put32(marker+8,1);put32(marker+508,kui_crc32(0,marker,508));
        }
        same=same && !memcmp(saved,marker,sizeof(saved));
        if(!close_file(s,&f,same))return false;
        if(!same)return fail(s,"Another or damaged in-place transaction owns this source");
        if(repair_get32(marker+8)==2u)for(unsigned index=0;index<5u;++index) {
            bool present;if(!inplace_original_path(s,index,&present))return false;
        }
        return inplace_forget_complete(s);
    }
    if(r!=FR_NO_FILE)return fail(s,"Cannot inspect in-place active guard");
    if(!inplace_backup_metadata(s) || !inplace_history(s) ||
       !inplace_source_store(s,".kui-inplace-active.part",marker,sizeof(marker)))return false;
    source_path(s,".kui-inplace-active.part");char final[KUI_DEST_PATH_CAP];snprintf(final,sizeof(final),"0:%s/.kui-inplace-active",s->directory);
    if(f_rename(s->path,final)!=FR_OK)return fail(s,"Cannot publish in-place active guard");
    return inplace_forget_complete(s) && line(s,"IN_PLACE explicit target repair; originals and source metadata retained per transaction; normal Resume/Verify blocked until metadata finalization\n") && sync_file(s,&s->log);
}
static bool inplace_append(struct recovery_copy *s,size_t *used,const char *format,...) {
    va_list a;va_start(a,format);int n=vsnprintf(s->metadata+*used,sizeof(s->metadata)-*used,format,a);va_end(a);
    if(n<0 || (size_t)n>=sizeof(s->metadata)-*used)return fail(s,"Repaired manifest exceeds buffer");
    *used+=(size_t)n;return true;
}
static bool inplace_replace(struct recovery_copy *s,const char *name,const void *data,unsigned bytes) {
    char temp[KUI_DEST_NAME_CAP+32];int n=snprintf(temp,sizeof(temp),"%s.kui-repair.part",name);
    if(n<0 || (size_t)n>=sizeof(temp))return fail(s,"Repair metadata path exceeds bound");
    if(!inplace_source_store(s,temp,data,bytes))return false;
    /* V2 publishes metadata only after complete per-transaction backups. The
     * old fixed .kui-repair-original files are never replaced or removed. */
    source_path(s,name);FRESULT r=f_unlink(s->path);
    if(r!=FR_OK && r!=FR_NO_FILE)return fail(s,"Cannot replace guarded source metadata");
    char final[KUI_DEST_PATH_CAP];snprintf(final,sizeof(final),"0:%s/%s",s->directory,name);source_path(s,temp);
    if(f_rename(s->path,final)!=FR_OK)return fail(s,"Cannot publish repaired source metadata");
    return true;
}
static bool inplace_digest_source(struct recovery_copy *s,const char *name,uint8_t digest[32]) {
    source_path(s,name);FIL f;if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot bind repaired descriptor bytes");
    struct kui_sha256 hash;kui_sha256_init(&hash);uint64_t left=f_size(&f);bool ok=left!=0;
    while(ok && left) {if(cancelled(s)) {ok=false;break;}unsigned n=(unsigned)(left>sizeof(s->data)?sizeof(s->data):left);ok=exact_read(s,&f,s->data,n);if(ok) {kui_sha256_update(&hash,s->data,n);left-=n;}}
    if(!close_file(s,&f,ok))return false;
    kui_sha256_digest(&hash,digest);return true;
}
static bool inplace_publish(struct recovery_copy *s) {
    if(cancelled(s))return false;
    uint8_t manifest_digest[32]={0},checkpoint_digest[32]={0};
    if(s->manifest_present) {
        if(s->partial_source) {
            size_t used=0;if(!inplace_append(s,&used,"%u\n",s->source.plan.count))return false;
            for(unsigned i=0;i<s->source.plan.count;++i) {
                const struct kui_capture_track *t=&s->source.plan.tracks[i];
                if(!inplace_append(s,&used,"%u %u %u 2352 %s 0\n",i+1,t->start-150u,t->control,s->source.files[i]))return false;
            }
            if(!inplace_replace(s,s->manifest.gdi,s->metadata,(unsigned)used))return false;
            if(s->manifest.format==KUI_CAPTURE_FORMAT_BIN_CUE) {
                uint8_t modes[99]={0};for(unsigned i=0;i<s->source.plan.count;++i)modes[i]=s->manifest.track[i].sector_mode;
                if(!kui_capture_cue_encode(s->optical_plan,modes,s->metadata,sizeof(s->metadata),&used) || !inplace_replace(s,s->manifest.cue,s->metadata,(unsigned)used))return false;
            }
        }
        char identity[65];kui_hex(s->manifest.identity,32,identity);size_t used=0;
        if(!inplace_append(s,&used,"{\n\"schema\":1,\"complete\":true,\"saved_data_verified\":true,\n"
            "\"profile\":\"%s\",\"identity\":\"%s\",\"capture_build\":\"%.12s\",\n"
            "\"title\":\"Repaired K-UI dump\",\"sector_bytes\":2352,\"audio\":\"raw drive bytes; no offset/subchannel correction\",\n",
            kui_capture_profile_name(s->optical_plan),identity,s->optical_ops->build?s->optical_ops->build:"000000000000"))return false;
        if(s->manifest.format==KUI_CAPTURE_FORMAT_BIN_CUE) {
            if(!inplace_append(s,&used,"\"output_format\":\"bin_cue\",\"gdi_file\":\"%s\",\"cue_file\":\"%s\",\n",s->manifest.gdi,s->manifest.cue))return false;
        } else if(!inplace_append(s,&used,"\"gdi_file\":\"%s\",\n",s->manifest.gdi))return false;
        if(!inplace_append(s,&used,"\"tracks\":[\n"))return false;
        for(unsigned i=0;i<s->source.plan.count;++i) {
            const struct kui_capture_track *t=&s->source.plan.tracks[i];char sha[65];kui_hex(s->sha[i],32,sha);
            if(!inplace_append(s,&used,"{\"number\":%u,\"session\":%u,\"control\":%u,\"start_fad\":%u,\"end_fad\":%u,\"toc_end_fad\":%u,\"excluded_tail_sectors\":%u,\"file\":\"%s\",\"bytes\":%" PRIu64 ",\"crc32\":\"%08" PRIx32 "\",\"sha256\":\"%s\"",
                i+1,t->session,t->control,t->start,t->end,t->toc_end,t->toc_end-t->end,s->source.files[i],(uint64_t)(t->end-t->start)*KUI_RAW_BYTES,s->crc[i],sha))return false;
            if(s->manifest.format==KUI_CAPTURE_FORMAT_BIN_CUE && !inplace_append(s,&used,",\"sector_mode\":%u",s->manifest.track[i].sector_mode))return false;
            if(!inplace_append(s,&used,"}%s\n",i+1==s->source.plan.count?"":","))return false;
        }
        if(!inplace_append(s,&used,"]}\n"))return false;
        struct kui_scan_manifest parsed;
        if(!kui_recovery_manifest_parse(s->metadata,used,&parsed))return fail(s,"Regenerated repaired manifest failed its independent parser");
        struct kui_sha256 receipt_hash;kui_sha256_init(&receipt_hash);kui_sha256_update(&receipt_hash,s->metadata,used);kui_sha256_digest(&receipt_hash,manifest_digest);
        if(!inplace_replace(s,"manifest.json",s->metadata,(unsigned)used))return false;
        struct kui_checkpoint checkpoint={0};checkpoint.sequence=s->state_sequence+1;checkpoint.count=s->source.plan.count;
        memcpy(checkpoint.identity,s->manifest.identity,32);memcpy(checkpoint.build,s->optical_ops->build?s->optical_ops->build:"000000000000",12);checkpoint.format=s->manifest.format;
        for(unsigned i=0;i<checkpoint.count;++i) {checkpoint.track[i].sectors=s->committed[i];checkpoint.track[i].crc32=s->crc[i];memcpy(checkpoint.track[i].sha256,s->sha[i],32);checkpoint.track[i].sector_mode=s->manifest.track[i].sector_mode;}
        kui_checkpoint_encode(&checkpoint,s->state);
        kui_sha256_init(&receipt_hash);kui_sha256_update(&receipt_hash,s->state,KUI_CHECKPOINT_BYTES);kui_sha256_digest(&receipt_hash,checkpoint_digest);
        if(!inplace_replace(s,"checkpoint-a.bin",s->state,KUI_CHECKPOINT_BYTES) || !inplace_replace(s,"checkpoint-b.bin",s->state,KUI_CHECKPOINT_BYTES))return false;
    }
    if(s->manifest_present) {
        uint8_t receipt[512]={0};memcpy(receipt,"KUIIPD1",8);put32(receipt+8,1);put32(receipt+12,512);
        memcpy(receipt+16,manifest_digest,32);memcpy(receipt+48,checkpoint_digest,32);
        struct kui_sha256 path_hash;kui_sha256_init(&path_hash);kui_sha256_update(&path_hash,s->directory,strlen(s->directory)+1);kui_sha256_digest(&path_hash,receipt+80);
        if(!inplace_digest_source(s,s->manifest.gdi,receipt+112) ||
           (s->manifest.format==KUI_CAPTURE_FORMAT_BIN_CUE && !inplace_digest_source(s,s->manifest.cue,receipt+144)))return false;
        put32(receipt+508,kui_crc32(0,receipt,508));
        if(!inplace_source_store(s,".kui-inplace-complete.part",receipt,sizeof(receipt)))return false;
        char final[KUI_DEST_PATH_CAP];snprintf(final,sizeof(final),"0:%s/.kui-inplace-complete",s->directory);source_path(s,".kui-inplace-complete.part");
        if(f_rename(s->path,final)!=FR_OK)return fail(s,"Cannot publish in-place completion receipt");
        source_path(s,".kui-inplace-complete");FIL checked;uint8_t saved[512];
        if(f_open(&checked,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot reopen in-place completion receipt");
        bool same=f_size(&checked)==512 && exact_read(s,&checked,saved,512) && !memcmp(saved,receipt,512);
        if(!close_file(s,&checked,same))return false;
        if(!same)return fail(s,"Published in-place completion receipt differs");
    }
    if(!line(s,"COMPLETE IN_PLACE all changed sectors backed up, all saved bytes read back; source metadata finalized\n") || !sync_file(s,&s->evidence) || !sync_file(s,&s->log))return false;
    if(!inplace_job_complete(s))return false;
    source_path(s,".kui-inplace-active.part");FRESULT r=f_unlink(s->path);if(r!=FR_OK && r!=FR_NO_FILE)return fail(s,"Cannot retire interrupted in-place guard");
    source_path(s,".kui-inplace-active");if(f_unlink(s->path)!=FR_OK)return fail(s,"Cannot retire in-place guard");
    return true;
}
