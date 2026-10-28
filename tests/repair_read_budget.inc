/* SPDX-License-Identifier: GPL-3.0-only */
/* Measure FatFs payload bytes by actual source-file object and file offset.
 * Verification below runs after measurement ends, so a full-fixture check
 * cannot accidentally hide an extra engine scan in the reported budget. */
static bool repair_budget_measuring;
static uint64_t repair_budget_bytes[3],repair_budget_sector_bytes[3][40];

static void repair_read_budget_record(DWORD cluster,FSIZE_t offset,UINT bytes) {
    if(!repair_budget_measuring || !test.active || !bytes)return;
    for(unsigned ti=0;ti<3u;++ti) {
        if(!test.source_clusters[ti] || cluster!=test.source_clusters[ti])continue;
        uint64_t start=(uint64_t)offset,end=start+bytes;
        uint32_t count=plan.tracks[ti].end-plan.tracks[ti].start;
        assert(count<=40u && end>=start && end<=(uint64_t)count*KUI_RAW_BYTES);
        repair_budget_bytes[ti]+=bytes;
        for(uint32_t sector=0;sector<count;++sector) {
            uint64_t left=(uint64_t)sector*KUI_RAW_BYTES,right=left+KUI_RAW_BYTES;
            if(left<start)left=start;
            if(right>end)right=end;
            if(left<right)repair_budget_sector_bytes[ti][sector]+=right-left;
        }
    }
}
static void repair_read_budget_begin(void) {
    memset(repair_budget_bytes,0,sizeof(repair_budget_bytes));
    memset(repair_budget_sector_bytes,0,sizeof(repair_budget_sector_bytes));
    repair_budget_measuring=true;
}
static void repair_read_budget_check(const char *name,bool clean) {
    uint64_t bytes=0;
    for(unsigned ti=0;ti<3u;++ti) {
        uint32_t count=plan.tracks[ti].end-plan.tracks[ti].start;
        for(uint32_t sector=0;sector<count;++sector) {
            bool target=!clean && ((ti==0u && sector==0u) || (ti==2u && sector==2u));
            uint64_t actual=repair_budget_sector_bytes[ti][sector];
            if(!target && actual!=2u*KUI_RAW_BYTES)
                fprintf(stderr,"READ BUDGET %s: healthy track %u sector %u read %llu bytes; expected %u\n",
                    name,ti+1u,sector,(unsigned long long)actual,2u*KUI_RAW_BYTES);
            /* Target rereads and exact patch readback are useful work. Every
             * other byte gets one admitted baseline read and one final read. */
            assert(target?(actual>=2u*KUI_RAW_BYTES && actual<=10u*KUI_RAW_BYTES):actual==2u*KUI_RAW_BYTES);
        }
        bytes+=repair_budget_bytes[ti];
    }
    assert(bytes>=2u*plan.bytes && bytes<=2u*plan.bytes+(clean?0u:16u*KUI_RAW_BYTES));
    printf("READ_BUDGET %s source_bytes=%llu baseline_plus_final=%llu target_extra=%llu healthy_full_reads=2\n",
        name,(unsigned long long)bytes,(unsigned long long)(2u*plan.bytes),
        (unsigned long long)(bytes-2u*plan.bytes));
}
static void repair_read_budget_verify(const struct kui_repair_status *status) {
    assert(!repair_budget_measuring && !test.active);
    uint8_t bytes[100000];char path[KUI_DEST_PATH_CAP];FILINFO info;
    for(unsigned ti=0;ti<3u;++ti) {
        source_path(path,files[4u+ti]);size_t n=load(path,bytes,sizeof(bytes));
        assert(n==(size_t)(plan.tracks[ti].end-plan.tracks[ti].start)*KUI_RAW_BYTES);
        assert(!memcmp(bytes,test.optical[ti],n));
    }
    source_path(path,".kui-inplace-active");assert(f_stat(path,&info)==FR_NO_FILE);
    source_path(path,".kui-inplace-complete");assert(load(path,bytes,sizeof(bytes))==512u);
    assert(!memcmp(bytes,"KUIIPD1",8) && get32(bytes+508)==kui_crc32(0,bytes,508));
    source_path(path,"manifest.json");size_t n=load(path,bytes,sizeof(bytes));
    struct kui_scan_manifest manifest;assert(kui_recovery_manifest_parse(bytes,n,&manifest));
    for(unsigned ti=0;ti<3u;++ti)
        assert(manifest.track[ti].crc32==kui_crc32(0,test.optical[ti],(plan.tracks[ti].end-plan.tracks[ti].start)*KUI_RAW_BYTES));
    source_path(path,"checkpoint-a.bin");assert(load(path,bytes,sizeof(bytes))==KUI_CHECKPOINT_BYTES);
    struct kui_checkpoint checkpoint;assert(kui_checkpoint_decode(bytes,&plan,manifest.identity,&checkpoint));
    for(unsigned ti=0;ti<3u;++ti)
        assert(checkpoint.track[ti].crc32==manifest.track[ti].crc32 && !memcmp(checkpoint.track[ti].sha256,manifest.track[ti].sha256,32));
    snprintf(path,sizeof(path),"0:%s/samples.bin",status->job);
    assert(check_evidence(path)==status->repaired*2u);
    snprintf(path,sizeof(path),"0:%s/track01.bin",status->job);assert(f_stat(path,&info)==FR_NO_FILE);
}
static void repair_read_budget_run(const char *name) {
    bool clean=strstr(name,"-clean")!=NULL,resume=strstr(name,"-resume")!=NULL;
    const struct kui_capture_ops ops={.read=optical,.cancelled=cancel,.now_ms=now_ms,.log=log_line,.build="000000000000"};
    const struct kui_repair_options options={.attempts=1,.progress=repair_progress,.continuous=true,.in_place=true};
    struct kui_repair_status status;test.repair=&status;
    if(resume) {
        test.fault="inplace-stop";test.active=true;
        enum kui_repair_result stopped=kui_repair_copy(folder,&plan,&ops,&options,&status);
        test.active=false;remount();
        assert(stopped==KUI_REPAIR_STOPPED && !status.complete && status.repaired==1u);
        test.fault="none";assert(f_mount(NULL,"0:",0)==FR_OK);
    }
    test.active=true;repair_read_budget_begin();
    enum kui_repair_result result=kui_repair_copy(folder,&plan,&ops,&options,&status);
    repair_budget_measuring=false;test.active=false;remount();
    if(result!=KUI_REPAIR_COMPLETE)
        fprintf(stderr,"READ BUDGET %s failed: %s\n",name,status.message);
    assert(result==KUI_REPAIR_COMPLETE && status.complete && status.in_place && status.catalogue_checked);
    assert(status.resumed==resume && status.repaired==(clean?0u:2u));
    repair_read_budget_check(name,clean);
    repair_read_budget_verify(&status);
}
