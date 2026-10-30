/* SPDX-License-Identifier: GPL-3.0-only */
struct defer_snapshot { uint8_t hash[5][32],header[4096],journal[4096]; uint64_t size[5];unsigned journal_prefix; };
static void defer_snapshot(const char *job,struct defer_snapshot *saved) {
    static const char *names[]={"header.bin","journal.bin","track01.bin","track02.raw","track03.bin"};
    remount();char path[512];uint8_t bytes[4096];
    for(unsigned i=0;i<5;++i) {
        snprintf(path,sizeof(path),"0:%s/%s",job,names[i]);FIL file;FRESULT r=f_open(&file,path,FA_READ);
        if(r==FR_NO_FILE) {saved->size[i]=UINT64_MAX;memset(saved->hash[i],0,32);continue;}
        assert(r==FR_OK);saved->size[i]=f_size(&file);struct kui_sha256 hash;kui_sha256_init(&hash);uint64_t left=f_size(&file),offset=0;
        while(left) {
            UINT n=left>sizeof(bytes)?sizeof(bytes):(UINT)left,got=0;assert(f_read(&file,bytes,n,&got)==FR_OK && got==n);
            kui_sha256_update(&hash,bytes,n);
            if(!i) {assert(!offset && n==4096);memcpy(saved->header,bytes,4096);}
            if(i==1 && !offset) {memcpy(saved->journal,bytes,n);saved->journal_prefix=n;}
            offset+=n;left-=n;
        }
        kui_sha256_digest(&hash,saved->hash[i]);assert(f_close(&file)==FR_OK);
    }
    assert(f_mount(NULL,"0:",0)==FR_OK);
}
static void defer_unchanged(const char *job,const struct defer_snapshot *before) {
    struct defer_snapshot after={0};defer_snapshot(job,&after);
    assert(!memcmp(before->hash,after.hash,sizeof(after.hash)) && !memcmp(before->size,after.size,sizeof(after.size)));
}
static void defer_prefix(const char *job,const struct defer_snapshot *before) {
    remount();char path[512];uint8_t bytes[4096];
    snprintf(path,sizeof(path),"0:%s/header.bin",job);assert(load(path,bytes,sizeof(bytes))==4096 && !memcmp(bytes,before->header,4096));
    snprintf(path,sizeof(path),"0:%s/journal.bin",job);FIL file;UINT got=0;assert(f_open(&file,path,FA_READ)==FR_OK);
    assert(f_size(&file)>=before->journal_prefix && f_read(&file,bytes,before->journal_prefix,&got)==FR_OK && got==before->journal_prefix && !memcmp(bytes,before->journal,got));
    assert(f_close(&file)==FR_OK && f_mount(NULL,"0:",0)==FR_OK);
}
static void defer_output(const char *job,const struct defer_snapshot *before,unsigned old_targets) {
    defer_prefix(job,before);remount();char path[512];uint8_t bytes[32*KUI_RAW_BYTES],expected[32*KUI_RAW_BYTES];
    for(unsigned i=0;i<3;++i) {
        snprintf(path,sizeof(path),"0:%s/track%02u.%s",job,i+1,i==1?"raw":"bin");FIL file;assert(f_open(&file,path,FA_READ)==FR_OK);
        unsigned total=plan.tracks[i].end-plan.tracks[i].start;assert(f_size(&file)==(uint64_t)total*KUI_RAW_BYTES);
        for(unsigned sector=0;sector<total;) {
            unsigned count=total-sector;if(count>32)count=32;UINT got=0;assert(f_read(&file,bytes,count*KUI_RAW_BYTES,&got)==FR_OK && got==count*KUI_RAW_BYTES);
            memcpy(expected,original[i]+sector*KUI_RAW_BYTES,count*KUI_RAW_BYTES);
            for(unsigned n=0;n<count;++n)if(plan.tracks[i].start+sector+n>=test.defer_start && plan.tracks[i].start+sector+n<test.defer_start+test.defer_count)memset(expected+n*KUI_RAW_BYTES,0,KUI_RAW_BYTES);
            assert(!memcmp(bytes,expected,count*KUI_RAW_BYTES));sector+=count;
        }
        assert(f_close(&file)==FR_OK);
    }
    snprintf(path,sizeof(path),"0:%s/journal.bin",job);FIL journal;assert(f_open(&journal,path,FA_READ)==FR_OK);unsigned old=0,added=0;
    while(f_tell(&journal)<f_size(&journal)) {
        UINT got=0;assert(f_read(&journal,bytes,512,&got)==FR_OK && got==512);
        if(get32(bytes+16)!=1)continue;
        unsigned ti=get32(bytes+20),sector=get32(bytes+24);uint32_t fad=plan.tracks[ti].start+sector;
        if(fad>=test.defer_start && fad<test.defer_start+test.defer_count) {assert(fad==test.defer_start+added);++added;}
        else {assert((ti==0 || ti==1) && sector==2);++old;}
    }
    assert(old==old_targets && added==test.defer_count && f_close(&journal)==FR_OK && f_mount(NULL,"0:",0)==FR_OK);
}
static void run_defer_fixture(char **argv,struct kui_capture_ops *ops,struct kui_salvage_options *options) {
    char job[KUI_DEST_JOB_CAP];bool boundary=!strcmp(test.test,"defer-boundary");
    if(boundary)seed_legacy_track_boundary(job);else seed_legacy_narrow(2,job);
    test.defer_start=boundary?340u:45182u;test.defer_count=boundary?4u:!strcmp(test.test,"defer-tail")?8u:32u;
    test.defer_initial_done=(boundary?40u:76u)*KUI_RAW_BYTES;
    unsigned old_targets=boundary?1u:2u,added=test.defer_count;
    options->job=job;options->defer_next_range=true;ops->now_ms=NULL;test.healthy=true;test.capturing=true;test.defer_block_repairs=true;
    struct defer_snapshot before={0};defer_snapshot(job,&before);struct kui_salvage_status status;
    if(!strcmp(test.test,"defer-new") || !strcmp(test.test,"defer-recover")) {
        unsigned reads=test.reads,writes=test.writes;
        assert(run_salvage(&plan,ops,options,!strcmp(test.test,"defer-new")?KUI_SALVAGE_NEW:KUI_SALVAGE_RECOVER,&status)==KUI_SALVAGE_FAILED);
        assert(test.reads==reads && test.writes==writes);defer_unchanged(job,&before);return;
    }
    if(!strcmp(test.test,"defer-wrong-disc") || !strncmp(test.test,"defer-anchor-",13) || !strcmp(test.test,"defer-quarantine")) {
        uint8_t original_sector[KUI_RAW_BYTES];memcpy(original_sector,original[2],KUI_RAW_BYTES);
        if(!strcmp(test.test,"defer-wrong-disc")) {char path[1024];snprintf(path,sizeof(path),"%s/alternate.raw",argv[2]);FILE *f=fopen(path,"rb");assert(f && fread(original[2],1,KUI_RAW_BYTES,f)==KUI_RAW_BYTES && !fclose(f));}
        else if(!strcmp(test.test,"defer-quarantine")) {remount();assert(kui_recovery_guard_mark(job,log_line));assert(f_mount(NULL,"0:",0)==FR_OK);}
        else test.defer_anchor_result=!strcmp(test.test,"defer-anchor-retry")?1u:2u;
        unsigned reads=test.reads,writes=test.writes;assert(run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status)==KUI_SALVAGE_FAILED);
        assert(test.writes==writes && !test.defer_seen && !test.collection_calls);
        if(test.defer_anchor_result)assert(test.reads-reads==(test.defer_anchor_result==1?11u:1u));
        defer_unchanged(job,&before);memcpy(original[2],original_sector,KUI_RAW_BYTES);return;
    }
    if(!strcmp(test.test,"defer-ready") || !strcmp(test.test,"defer-complete") || !strcmp(test.test,"defer-no-ready")) {
        options->defer_next_range=false;test.defer_count=0;test.defer_block_repairs=false;test.defer_block_all_repairs=strcmp(test.test,"defer-complete")!=0;
        if(!strcmp(test.test,"defer-no-ready"))test.fault="ready-write";
        enum kui_salvage_result made=run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status);
        assert(status.done==plan.bytes && status.targets==2);
        if(!strcmp(test.test,"defer-complete"))assert(made==KUI_SALVAGE_RESOLVED && status.complete && status.first_pass_complete && status.recovered==2 && !status.remaining);
        else if(!strcmp(test.test,"defer-ready"))assert(made==KUI_SALVAGE_UNRESOLVED && !status.complete && status.first_pass_complete && !status.recovered && status.remaining==2);
        else assert(made==KUI_SALVAGE_FAILED && test.injected && !status.complete && !status.first_pass_complete && !status.recovered && status.remaining==2);
        test.fault=NULL;options->defer_next_range=true;test.collection_calls=0;unsigned reads=test.reads;
        defer_snapshot(job,&before);assert(run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status)==KUI_SALVAGE_FAILED && !status.complete);
        assert(!test.collection_calls && test.reads==reads+2 && !test.defer_seen);defer_unchanged(job,&before);return;
    }
    if(!strcmp(test.test,"defer-default")) {
        options->defer_next_range=false;
        assert(run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status)==KUI_SALVAGE_FAILED && !status.complete && status.done==test.defer_initial_done && status.targets==2);
        assert(test.defer_bad_collection_reads==1 && test.collection_calls==1 && test.collection_fad[0]==45182 && test.collection_count[0]==32 && !test.defer_seen);
        defer_unchanged(job,&before);return;
    }
    test.defer_stop_before=!strcmp(test.test,"defer-stop-before");test.defer_stop_after=!strcmp(test.test,"defer-stop-after");
    if(!strcmp(test.test,"defer-queue"))test.fault="queue-write";
    if(!strcmp(test.test,"defer-torn"))test.fault="torn-commit";
    if(!strcmp(test.test,"defer-index"))test.fault="defer-index";
    enum kui_salvage_result result=run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status);
    if(test.defer_stop_before) {
        assert(result==KUI_SALVAGE_STOPPED && status.done==test.defer_initial_done && status.targets==old_targets && !test.collection_calls && test.defer_seen);
        defer_unchanged(job,&before);test.cancelled=false;test.defer_stop_before=false;
    } else if(test.defer_stop_after) {
        assert(result==KUI_SALVAGE_STOPPED && status.done==test.defer_initial_done+(uint64_t)added*KUI_RAW_BYTES && status.targets==old_targets+added && !test.collection_calls);
        test.cancelled=false;test.defer_stop_after=false;options->defer_next_range=false;
    } else if(test.fault) {
        assert(result==KUI_SALVAGE_FAILED && test.injected && !test.collection_calls && !status.complete);defer_prefix(job,&before);
        if(!strcmp(test.fault,"defer-index"))options->defer_next_range=false;
        test.fault=NULL;test.injected=false;
    }
    if(result!=KUI_SALVAGE_UNRESOLVED)result=run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status);
    assert(result==KUI_SALVAGE_UNRESOLVED && status.first_pass_complete && !status.complete && status.done==plan.bytes &&
        status.targets==old_targets+added && status.recovered==old_targets && status.remaining==added && !test.defer_bad_collection_reads && test.defer_seen);
    unsigned calls=boundary?4u:!strcmp(test.test,"defer-tail")?0u:2u;assert(test.collection_calls==calls);
    for(unsigned i=0;i<calls;++i) {assert(test.collection_fad[i]==(boundary?45150u:45214u)+32u*i);assert(test.collection_count[i]==(i+1==calls?8u:32u));}
    defer_output(job,&before,old_targets);
    options->defer_next_range=false;test.defer_block_repairs=false;
    result=run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status);
    assert(result==KUI_SALVAGE_RESOLVED && status.complete && status.targets==old_targets+added && status.recovered==old_targets+added && !status.remaining);
    test.capturing=false;compare_complete(job);
}
