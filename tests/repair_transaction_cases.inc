/* SPDX-License-Identifier: GPL-3.0-only */
static void transaction_hash(const char *job,uint8_t digest[32]) {
    char path[KUI_DEST_PATH_CAP];snprintf(path,sizeof(path),"0:%s",job);DIR directory;assert(f_opendir(&directory,path)==FR_OK);
    struct kui_sha256 hash;kui_sha256_init(&hash);FILINFO info;
    for(;;) {
        assert(f_readdir(&directory,&info)==FR_OK);if(!info.fname[0])break;
        assert(!(info.fattrib&AM_DIR));kui_sha256_update(&hash,info.fname,strlen(info.fname)+1u);
        size_t a=strlen(job),b=strlen(info.fname);assert(a+b+4u<=sizeof(path));memcpy(path,"0:",2);memcpy(path+2,job,a);path[a+2]='/';memcpy(path+a+3,info.fname,b+1u);FIL f;assert(f_open(&f,path,FA_READ)==FR_OK);uint64_t left=f_size(&f);
        kui_sha256_update(&hash,&left,sizeof(left));uint8_t data[4096];
        while(left) {UINT n=(UINT)(left>sizeof(data)?sizeof(data):left),got=0;assert(f_read(&f,data,n,&got)==FR_OK && got==n);kui_sha256_update(&hash,data,n);left-=n;}
        assert(f_close(&f)==FR_OK);
    }
    assert(f_closedir(&directory)==FR_OK);kui_sha256_digest(&hash,digest);
}
static void transaction_database(void) {
    char db[1024];size_t used=(size_t)snprintf(db,sizeof(db),"DREAMSHELL_REDUMP_CRC_V1\nG\t3\tRefreshed repaired fixture\n");
    for(unsigned i=0;i<3;++i)used+=(size_t)snprintf(db+used,sizeof(db)-used,"T\t%u\t%u\t%08x\n",i+1,
        (unsigned)((plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES),kui_crc32(0,test.optical[i],(plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES));
    write_file("0:/KUI/redump.db",db,(UINT)used);
}
static void transaction_torn_job(bool relevant) {
    assert(f_mkdir("0:/KUI")==FR_OK);assert(f_mkdir("0:/KUI/repair")==FR_OK);assert(f_mkdir("0:/KUI/repair/job-9999")==FR_OK);
    uint8_t bytes[220]={0};memcpy(bytes,"KUIRPH2",8);memcpy(bytes+128,relevant?folder+2:"/Games/Unrelated",relevant?strlen(folder+2)+1u:17u);
    write_file("0:/KUI/repair/job-9999/resume.bin",bytes,relevant?sizeof(bytes):8u);
}
static void transaction_run(const char *name) {
    remount();
    const struct kui_capture_ops ops={.read=optical,.cancelled=cancel,.now_ms=now_ms,.log=log_line,.build="000000000000"};
    struct kui_repair_options options={.attempts=5,.progress=repair_progress};struct kui_repair_status status;test.repair=&status;
    uint32_t before[7],after[7];size_t bsize[7],asize[7];snapshot(before,bsize);
    if(strstr(name,"new-copy-init-fail")) {
        options.new_copy=true;test.fault="resume-initial-state";assert(f_mount(NULL,"0:",0)==FR_OK);test.active=true;
        assert(kui_repair_copy(folder,&plan,&ops,&options,&status)==KUI_REPAIR_FAILED);test.active=false;remount();
        assert(status.job[0] && !status.resume_ready && test.injected);char old[KUI_DEST_JOB_CAP];snprintf(old,sizeof(old),"%s",status.job);
        uint8_t retained[32],again[32];transaction_hash(old,retained);test.fault="none";assert(f_mount(NULL,"0:",0)==FR_OK);test.active=true;
        assert(kui_repair_copy(folder,&plan,&ops,&options,&status)==KUI_REPAIR_COMPLETE);test.active=false;remount();
        assert(strcmp(old,status.job) && status.resume_ready && !status.resumed);transaction_hash(old,again);assert(!memcmp(retained,again,32));check_completed_copy(&status);return;
    }
    if(strstr(name,"active-torn")) {
        options.in_place=true;test.fault="inplace-stop";assert(f_mount(NULL,"0:",0)==FR_OK);test.active=true;
        assert(kui_repair_copy(folder,&plan,&ops,&options,&status)==KUI_REPAIR_STOPPED);test.active=false;remount();
        char job[KUI_DEST_JOB_CAP],path[KUI_DEST_PATH_CAP];snprintf(job,sizeof(job),"%s",status.job);snprintf(path,sizeof(path),"0:%s/resume.bin",job);
        write_file(path,"KUIRPH2",8);uint8_t retained[32],again[32];transaction_hash(job,retained);snapshot(before,bsize);unsigned writes=test.writes;
        test.fault="none";assert(f_mount(NULL,"0:",0)==FR_OK);test.active=true;assert(kui_repair_copy(folder,&plan,&ops,&options,&status)==KUI_REPAIR_FAILED);test.active=false;remount();
        assert(test.writes==writes && !status.complete);transaction_hash(job,again);assert(!memcmp(retained,again,32));snapshot(after,asize);
        assert(!memcmp(before,after,sizeof(before)) && !memcmp(bsize,asize,sizeof(bsize)));return;
    }
    if(strstr(name,"torn")) {
        bool related=strstr(name,"related-torn") && !strstr(name,"unrelated-torn");transaction_torn_job(related);
        if(strstr(name,"import"))for(unsigned i=0;i<4;i+=i?1:2) {char path[KUI_DEST_PATH_CAP];source_path(path,files[i]);assert(f_unlink(path)==FR_OK);}
        if(strstr(name,"fresh"))options.new_copy=true;
        uint8_t retained[32],again[32];transaction_hash("/KUI/repair/job-9999",retained);
        assert(f_mount(NULL,"0:",0)==FR_OK);test.active=true;enum kui_repair_result result=kui_repair_copy(folder,&plan,&ops,&options,&status);test.active=false;remount();
        bool refused=related && !options.new_copy;
        assert(result==(refused?KUI_REPAIR_FAILED:KUI_REPAIR_COMPLETE));
        if(!refused) {assert(strcmp(status.job,"/KUI/repair/job-9999"));check_completed_copy(&status);}
        transaction_hash("/KUI/repair/job-9999",again);assert(!memcmp(retained,again,32));return;
    }
    if(strstr(name,"new-copy-inplace")) {
        options.new_copy=true;options.in_place=true;unsigned writes=test.writes;
        assert(f_mount(NULL,"0:",0)==FR_OK);test.active=true;assert(kui_repair_copy(folder,&plan,&ops,&options,&status)==KUI_REPAIR_FAILED);test.active=false;remount();
        assert(!status.job[0] && test.writes==writes);return;
    }
    if(strstr(name,"source-change")) {
        test.fault="resume-stop";assert(f_mount(NULL,"0:",0)==FR_OK);test.active=true;
        assert(kui_repair_copy(folder,&plan,&ops,&options,&status)==KUI_REPAIR_STOPPED);test.active=false;remount();
        assert(status.resume_ready);char old[KUI_DEST_JOB_CAP];snprintf(old,sizeof(old),"%s",status.job);uint8_t retained[32],again[32];transaction_hash(old,retained);
        mutate("track03.bin",17u*KUI_RAW_BYTES+37u,1);snapshot(before,bsize);test.fault="none";unsigned writes=test.writes;
        assert(f_mount(NULL,"0:",0)==FR_OK);test.active=true;assert(kui_repair_copy(folder,&plan,&ops,&options,&status)==KUI_REPAIR_FAILED);test.active=false;remount();
        assert(test.writes==writes && !status.complete);transaction_hash(old,again);assert(!memcmp(retained,again,32));
        options.new_copy=true;assert(f_mount(NULL,"0:",0)==FR_OK);test.active=true;
        assert(kui_repair_copy(folder,&plan,&ops,&options,&status)==KUI_REPAIR_COMPLETE);test.active=false;remount();
        assert(strcmp(old,status.job) && !status.resumed);check_completed_copy(&status);transaction_hash(old,again);assert(!memcmp(retained,again,32));
        char fresh[KUI_DEST_JOB_CAP];snprintf(fresh,sizeof(fresh),"%s",status.job);options.new_copy=false;
        assert(f_mount(NULL,"0:",0)==FR_OK);test.active=true;assert(kui_repair_copy(folder,&plan,&ops,&options,&status)==KUI_REPAIR_COMPLETE);test.active=false;remount();
        assert(!strcmp(fresh,status.job) && status.resumed);snapshot(after,asize);assert(!memcmp(before,after,sizeof(before)) && !memcmp(bsize,asize,sizeof(bsize)));return;
    }
    assert(strstr(name,"catalogue"));test.fault="none";
    FILINFO info;FRESULT r=f_stat("0:/KUI/redump.db",&info);if(r==FR_OK)assert(f_unlink("0:/KUI/redump.db")==FR_OK);else assert(r==FR_NO_FILE);
    assert(f_mount(NULL,"0:",0)==FR_OK);test.active=true;assert(kui_repair_copy(folder,&plan,&ops,&options,&status)==KUI_REPAIR_COMPLETE);test.active=false;remount();
    assert(status.catalogue.result==KUI_KNOWN_NO_DATABASE);char job[KUI_DEST_JOB_CAP],path[KUI_DEST_PATH_CAP];snprintf(job,sizeof(job),"%s",status.job);
    uint8_t copy[32768];snprintf(path,sizeof(path),"0:%s/copy.txt",job);size_t size=load(path,copy,sizeof(copy)-1u);copy[size]=0;
    if(strstr(name,"v2")) {
        uint8_t legacy[32768];char *tracks=strstr((char *)copy,"TRACK ");assert(tracks);
        char reference[256];
        if(strstr(name,"plain"))snprintf(reference,sizeof(reference),"catalogue verification not performed.\n");
        else snprintf(reference,sizeof(reference),"independent catalogue result=%s.\n",kui_known_text(KUI_KNOWN_NO_DATABASE));
        int head=snprintf((char *)legacy,sizeof(legacy),"K-UI repair copy v2\nSOURCE %s\nCompleted readback of every saved copied byte. Original files unchanged.\nImported audio verified=source hashes available; %s",folder,reference);
        assert(head>0);size_t tail=size-(size_t)(tracks-(char *)copy);memcpy(legacy+head,tracks,tail);size=(size_t)head+tail;write_file(path,legacy,(UINT)size);memcpy(copy,legacy,size);
    }
    if(strstr(name,"corrupt")) {char *at=strstr((char *)copy,"CRC32=");assert(at);at[6]=at[6]=='0'?'1':'0';write_file(path,copy,(UINT)size);}
    uint32_t receipt=kui_crc32(0,copy,size);char first[KUI_DEST_PATH_CAP];snprintf(first,sizeof(first),"0:%s/catalogue-0001.txt",job);size_t first_size=load(first,copy,sizeof(copy));uint32_t first_crc=kui_crc32(0,copy,first_size);
    transaction_database();test.fault=strstr(name,"report-fail")?"transaction-catalogue-report-fail":"none";
    assert(f_mount(NULL,"0:",0)==FR_OK);test.active=true;enum kui_repair_result result=kui_repair_copy(folder,&plan,&ops,&options,&status);test.active=false;remount();
    assert(result==((strstr(name,"corrupt") || strstr(name,"report-fail"))?KUI_REPAIR_FAILED:KUI_REPAIR_COMPLETE));
    assert(!strcmp(job,status.job) && status.catalogue.result==KUI_KNOWN_FULL_MATCH);
    size_t current=load(path,copy,sizeof(copy));assert(current==size && kui_crc32(0,copy,current)==receipt);
    current=load(first,copy,sizeof(copy));assert(current==first_size && kui_crc32(0,copy,current)==first_crc);
    if(strstr(name,"report-fail")) {assert(test.injected);test.fault="none";assert(f_mount(NULL,"0:",0)==FR_OK);test.active=true;assert(kui_repair_copy(folder,&plan,&ops,&options,&status)==KUI_REPAIR_COMPLETE);test.active=false;remount();}
    snprintf(path,sizeof(path),"0:%s/catalogue-%04u.txt",job,strstr(name,"report-fail")?3u:2u);
    if(!strstr(name,"corrupt")) {size_t n=load(path,copy,sizeof(copy)-1u);copy[n]=0;assert(strstr((char *)copy,kui_known_text(KUI_KNOWN_FULL_MATCH)));check_completed_copy(&status);}
    snapshot(after,asize);assert(!memcmp(before,after,sizeof(before)) && !memcmp(bsize,asize,sizeof(bsize)));
}
