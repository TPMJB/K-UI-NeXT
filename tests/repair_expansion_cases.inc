/* SPDX-License-Identifier: GPL-3.0-only */
static void expansion_verify_source(const struct kui_repair_status *status) {
    uint8_t data[100000];char path[KUI_DEST_PATH_CAP];FILINFO info;
    for(unsigned i=0;i<3;++i) {source_path(path,files[4+i]);size_t n=load(path,data,sizeof(data));assert(n==(size_t)(plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES && !memcmp(data,test.optical[i],n));}
    source_path(path,".kui-inplace-active");assert(f_stat(path,&info)==FR_NO_FILE);
    if(!status->imported) {
    source_path(path,"manifest.json");size_t n=load(path,data,sizeof(data));struct kui_scan_manifest m;assert(kui_recovery_manifest_parse(data,n,&m));
    for(unsigned i=0;i<3;++i)assert(m.track[i].crc32==kui_crc32(0,test.optical[i],(plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES));
    source_path(path,"checkpoint-a.bin");n=load(path,data,sizeof(data));struct kui_checkpoint checkpoint;
    assert(n==KUI_CHECKPOINT_BYTES && kui_checkpoint_decode(data,&plan,m.identity,&checkpoint));
    for(unsigned i=0;i<3;++i)assert(checkpoint.track[i].crc32==m.track[i].crc32 && !memcmp(checkpoint.track[i].sha256,m.track[i].sha256,32));
    }
    snprintf(path,sizeof(path),"0:%s/samples.bin",status->job);assert(check_evidence(path)>=2);
    snprintf(path,sizeof(path),"0:%s/track01.bin",status->job);assert(f_stat(path,&info)==FR_NO_FILE);
}
static void expansion_run(const char *name) {
    bool inplace=strstr(name,"inplace")!=NULL,resume=(!strstr(name,"second") && strstr(name,"stop")) || strstr(name,"torn") || strstr(name,"checkpoint") || strstr(name,"metadata") || strstr(name,"initial-state");
    const struct kui_capture_ops ops={.read=optical,.cancelled=cancel,.now_ms=now_ms,.log=log_line,.build="000000000000"};
    struct kui_repair_options options={.attempts=1,.progress=repair_progress,.continuous=true,.in_place=inplace,.allow_saved_identity=strstr(name,"saved-anchor")!=NULL};
    if(strstr(name,"finite"))options.passes=1;
    struct kui_repair_status status;test.repair=&status;test.active=true;
    enum kui_repair_result result=kui_repair_copy(folder,&plan,&ops,&options,&status);test.active=false;remount();
    if(!strcmp(name,"inplace-final-readback-mismatch")) {
        assert(result==KUI_REPAIR_FAILED && !status.complete && test.injected);char job[KUI_DEST_JOB_CAP];snprintf(job,sizeof(job),"%s",status.job);
        char guard[KUI_DEST_PATH_CAP];source_path(guard,".kui-inplace-active");FILINFO active;assert(f_stat(guard,&active)==FR_OK);
        test.fault="none";test.injected=false;test.active=true;assert(f_mount(NULL,"0:",0)==FR_OK);
        result=kui_repair_copy(folder,&plan,&ops,&options,&status);test.active=false;remount();assert(!strcmp(job,status.job) && status.resumed);
    }
    if(strstr(name,"saved-anchor-wrong") || strstr(name,"saved-anchor-readable") || strstr(name,"saved-anchor-corrupt")) {assert(result==KUI_REPAIR_FAILED && !status.complete && !status.job[0]);return;}
    if(resume) {
        assert(result==KUI_REPAIR_STOPPED || result==KUI_REPAIR_FAILED);
        assert(!status.complete && status.job[0]);char job[KUI_DEST_JOB_CAP];snprintf(job,sizeof(job),"%s",status.job);
        if(inplace) {char path[KUI_DEST_PATH_CAP];source_path(path,".kui-inplace-active");FILINFO info;if(!strstr(name,"initial-state"))assert(f_stat(path,&info)==FR_OK);}
        test.fault="none";test.active=true;assert(f_mount(NULL,"0:",0)==FR_OK);
        result=kui_repair_copy(folder,&plan,&ops,&options,&status);test.active=false;remount();
        assert(!strcmp(job,status.job) && status.resumed);
    }
    if(strstr(name,"saved-anchor"))assert(status.alternate_identity);
    if(strstr(name,"finite")) {assert(result==KUI_REPAIR_UNRESOLVED && !status.complete && status.unresolved);return;}
    if(result!=KUI_REPAIR_COMPLETE)fprintf(stderr,"EXPANSION FAILED %s: %d %s\n",name,result,status.message);
    assert(result==KUI_REPAIR_COMPLETE && status.complete && status.catalogue_checked);
    if(strstr(name,"catalogue"))assert((status.catalogue.result==KUI_KNOWN_FULL_MATCH)==!strstr(name,"mismatch"));
    if(inplace)expansion_verify_source(&status);else check_completed_copy(&status);
    if(plan.profile==KUI_CAPTURE_PROFILE_CD_TOC) {char cue[KUI_DEST_PATH_CAP];FILINFO info;snprintf(cue,sizeof(cue),"0:%s/disc.cue",inplace?"/Games/Repair Test":status.job);assert(f_stat(cue,&info)==FR_OK);}

    if(strstr(name,"second")) {
        char first[KUI_DEST_JOB_CAP];snprintf(first,sizeof(first),"%s",status.job);char path[KUI_DEST_PATH_CAP];uint8_t original[32768];
        snprintf(path,sizeof(path),"0:%s/original-metadata.bin",first);size_t bytes=load(path,original,sizeof(original));uint32_t original_crc=kui_crc32(0,original,bytes);
        mutate("track03.bin",7u*KUI_RAW_BYTES+37,1);
        test.fault=strstr(name,"stop")?"inplace-stop":"none";test.active=true;assert(f_mount(NULL,"0:",0)==FR_OK);
        result=kui_repair_copy(folder,&plan,&ops,&options,&status);test.active=false;remount();
        assert(strcmp(first,status.job));char second[KUI_DEST_JOB_CAP];snprintf(second,sizeof(second),"%s",status.job);
        if(strstr(name,"stop")) {
            assert(result==KUI_REPAIR_STOPPED && !status.complete);test.fault="none";test.active=true;assert(f_mount(NULL,"0:",0)==FR_OK);
            result=kui_repair_copy(folder,&plan,&ops,&options,&status);test.active=false;remount();assert(!strcmp(second,status.job) && status.resumed);
        }
        if(result!=KUI_REPAIR_COMPLETE)fprintf(stderr,"SECOND FAILED %s %s\n",name,status.message);
        assert(result==KUI_REPAIR_COMPLETE);expansion_verify_source(&status);
        snprintf(path,sizeof(path),"0:%s/original-metadata.bin",first);size_t after=load(path,original,sizeof(original));assert(after==bytes && kui_crc32(0,original,after)==original_crc);
    }
    if(strstr(name,"continuous") && !resume)assert(status.pass>=2);
}
