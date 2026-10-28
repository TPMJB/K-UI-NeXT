/* SPDX-License-Identifier: GPL-3.0-only */
/* Large inherited false-positive queues exercise the production repair path.
 * Optical reads, persisted bytes and target order are checked independently. */
#define BURST_SECTORS 512u
static struct {
    bool holes[3][BURST_SECTORS],stop_ready,mutated,committed[1024];
    unsigned phase,targets,batches,singles,events,first_reverse_fad,first_reverse_count;
    unsigned target_track[1024],target_sector[1024],order[2048];
    uint8_t *first_batch;
    uint8_t alternate[KUI_RAW_BYTES];
} burst;
static bool burst_case(void) {return !strncmp(test.test,"burst-",6);}
static void burst_plan(void) {
    plan.tracks[2].end=plan.tracks[2].toc_end=45150+BURST_SECTORS;
    plan.bytes=(44u+BURST_SECTORS)*KUI_RAW_BYTES;
    if(!strcmp(test.test,"burst-audio")) {
        for(unsigned n=0;n<4;++n)burst.holes[1][n]=true;
    } else if(!strcmp(test.test,"burst-boundaries")) {
        for(unsigned n=32;n<40;++n)burst.holes[0][n]=true;
        for(unsigned n=0;n<4;++n)burst.holes[1][n]=true;
        for(unsigned n=0;n<8;++n)burst.holes[2][n]=true;
    } else if(!strcmp(test.test,"burst-gaps")) {
        for(unsigned n=0;n<16;++n)burst.holes[2][n]=burst.holes[2][n+20]=true;
        burst.holes[2][100]=burst.holes[2][110]=true;
    } else for(unsigned n=0;n<BURST_SECTORS;++n)burst.holes[2][n]=true;
    for(unsigned ti=0;ti<3;++ti)for(unsigned n=0;n<plan.tracks[ti].end-plan.tracks[ti].start;++n)
        if(burst.holes[ti][n]) {
            assert(burst.targets<1024);burst.target_track[burst.targets]=ti;
            burst.target_sector[burst.targets++]=n;
        }
}
static void burst_progress(const struct kui_salvage_status *status) {
    if(burst.stop_ready && status->first_pass_complete && !status->pass) {test.cancelled=true;test.did_cancel=true;}
    if(burst.phase==1 && !strcmp(test.test,"burst-stop-before-read") && status->pass &&
       status->read_state==KUI_SALVAGE_READ_READING && status->read_count>1) {
        test.cancelled=true;test.did_cancel=true;
    }
}
static void burst_event(unsigned type,const uint8_t *event) {
    if(burst.phase!=1)return;
    if(type==7 && !strcmp(test.test,"burst-stop-candidate"))test.cancelled=true;
    if(type!=5)return;
    assert(burst.events<2048);unsigned index=get32(event+20);assert(index<burst.targets);
    burst.order[burst.events++]=index;burst.committed[index]=true;
    if(!strcmp(test.test,"burst-staged-mutation") && !burst.mutated) {
        assert(burst.first_batch);memcpy(burst.first_batch+7*KUI_RAW_BYTES,burst.alternate,KUI_RAW_BYTES);burst.mutated=true;
    }
}
static bool burst_optical(unsigned ti,uint32_t fad,unsigned count,uint8_t *data,enum kui_read_result *result) {
    const struct kui_salvage_status *s=active_status;assert(s);
    unsigned at=fad-plan.tracks[ti].start;
    memcpy(data,original[ti]+at*KUI_RAW_BYTES,(size_t)count*KUI_RAW_BYTES);*result=KUI_READ_OK;
    if(!s->pass) {
        if(!s->collection_active || count==1)return true;
        unsigned missing=0;
        for(unsigned n=0;n<count;++n)if(burst.holes[ti][at+n]) {
            ++missing;if(plan.tracks[ti].control==4)data[n*KUI_RAW_BYTES+2200]^=1;
        }
        if(missing==count) {memset(data,0xcc,(size_t)count*KUI_RAW_BYTES);*result=KUI_READ_RETRY;}
        return true;
    }
    /* Recovery identity reads must remain independently possible even when a
     * saved anchor was part of the inherited target queue. */
    if(strstr(s->message,"checking saved disc identity"))return true;
    if(!strcmp(test.test,"burst-interior") && s->pass>1)
        for(unsigned n=0;n<count;++n)assert(!burst.committed[at+n]);
    if(count>1) {
        assert(plan.tracks[ti].control==4 && count<=16);
        for(unsigned n=0;n<count;++n)assert(burst.holes[ti][at+n]);
        ++burst.batches;
        if(!burst.first_batch)burst.first_batch=data;
        if(s->pass==2 && !burst.first_reverse_count) {burst.first_reverse_fad=fad;burst.first_reverse_count=count;}
    } else ++burst.singles;
    if(burst.phase==2)return true;
    if(!strcmp(test.test,"burst-interior") && s->pass==1) {
        for(unsigned n=0;n<count;++n)if((at+n)%16u!=7u)memset(data+n*KUI_RAW_BYTES,0xa5,KUI_RAW_BYTES);
        *result=count>1?KUI_READ_PARTIAL:KUI_READ_RETRY;return true;
    }
    if(!strcmp(test.test,"burst-fatal") && count>1) {memset(data,0xcc,(size_t)count*KUI_RAW_BYTES);*result=KUI_READ_FATAL;return true;}
    if(!strcmp(test.test,"burst-cancel-read") && count>1) {test.cancelled=true;*result=KUI_READ_PARTIAL;return true;}
    if(!strcmp(test.test,"burst-all-bad") || (!strcmp(test.test,"burst-reverse") && s->pass==1 && ti==2 && at>=32)) {
        memset(data,0xcc,(size_t)count*KUI_RAW_BYTES);*result=KUI_READ_RETRY;return true;
    }
    if(!strcmp(test.test,"burst-held-recovery") && ti==2 && at<8) {
        if(count==1) {*result=KUI_READ_RETRY;return true;}
        for(unsigned n=0;n<count && at+n<8;++n)memset(data+n*KUI_RAW_BYTES,0xa5,KUI_RAW_BYTES);
        *result=KUI_READ_PARTIAL;return true;
    }
    if(!strcmp(test.test,"burst-prefix") && count>1) {
        for(unsigned n=5;n<count;++n)memset(data+n*KUI_RAW_BYTES,0xa5,KUI_RAW_BYTES);
        *result=KUI_READ_PARTIAL;return true;
    }
    if(!strcmp(test.test,"burst-mixed") && ti==2 && !at && count>1) {
        memcpy(data,data+KUI_RAW_BYTES,KUI_RAW_BYTES); /* Valid sector, wrong FAD. */
        data[3*KUI_RAW_BYTES+20]^=1;data[6*KUI_RAW_BYTES+2200]^=1;
        memset(data+8*KUI_RAW_BYTES,0xa5,KUI_RAW_BYTES);*result=KUI_READ_PARTIAL;
    }
    return true;
}
static unsigned burst_inspect(const char *job,bool provisional) {
    remount();char path[512];FIL file;UINT got;uint8_t event[512];bool repaired[1024]={false};unsigned targets=0,done=0;
    snprintf(path,sizeof(path),"0:%s/journal.bin",job);assert(f_open(&file,path,FA_READ)==FR_OK);
    while(f_size(&file)-f_tell(&file)>=sizeof(event)) {
        assert(f_read(&file,event,sizeof(event),&got)==FR_OK && got==sizeof(event));
        assert(get32(event+508)==kui_crc32(0,event,508));unsigned type=get32(event+16);
        if(type==1) {
            assert(targets<burst.targets && get32(event+20)==burst.target_track[targets] &&
                get32(event+24)==burst.target_sector[targets]);++targets;
        }
        if(type==5) {unsigned index=get32(event+20);assert(index<burst.targets && !repaired[index]);repaired[index]=true;++done;}
    }
    assert(targets==burst.targets && f_close(&file)==FR_OK);
    uint8_t data[32*KUI_RAW_BYTES],expected[sizeof(data)];
    for(unsigned ti=0;ti<3;++ti) {
        snprintf(path,sizeof(path),"0:%s/track%02u.%s",job,ti+1,plan.tracks[ti].control==4?"bin":"raw");
        assert(f_open(&file,path,FA_READ)==FR_OK && f_size(&file)==(uint64_t)(plan.tracks[ti].end-plan.tracks[ti].start)*KUI_RAW_BYTES);
        for(unsigned at=0;at<plan.tracks[ti].end-plan.tracks[ti].start;) {
            unsigned count=plan.tracks[ti].end-plan.tracks[ti].start-at;if(count>32)count=32;
            assert(f_read(&file,data,count*KUI_RAW_BYTES,&got)==FR_OK && got==count*KUI_RAW_BYTES);
            memcpy(expected,original[ti]+at*KUI_RAW_BYTES,count*KUI_RAW_BYTES);
            for(unsigned n=0;n<count;++n)if(burst.holes[ti][at+n]) {
                unsigned index=0;while(index<burst.targets && (burst.target_track[index]!=ti || burst.target_sector[index]!=at+n))++index;
                assert(index<burst.targets);
                if(!repaired[index] && !(provisional && !index))memset(expected+n*KUI_RAW_BYTES,0,KUI_RAW_BYTES);
            }
            assert(!memcmp(data,expected,count*KUI_RAW_BYTES));at+=count;
        }
        assert(f_close(&file)==FR_OK);
    }
    snprintf(path,sizeof(path),"0:%s/salvage.json",job);FILINFO info;assert(f_stat(path,&info)==FR_NO_FILE);
    assert(f_mount(NULL,"0:",0)==FR_OK);return done;
}
static void run_burst_fixture(char **argv,struct kui_capture_ops *ops,struct kui_salvage_options *options) {
    if(!strcmp(test.test,"burst-staged-mutation")) {
        char path[1024];snprintf(path,sizeof(path),"%s/alternate-staged.raw",argv[2]);FILE *f=fopen(path,"rb");
        assert(f && fread(burst.alternate,1,KUI_RAW_BYTES,f)==KUI_RAW_BYTES && !fclose(f));
    }
    burst.stop_ready=true;test.healthy=true;ops->now_ms=NULL;
    struct kui_salvage_status status;assert(run_salvage(&plan,ops,options,KUI_SALVAGE_NEW,&status)==KUI_SALVAGE_STOPPED);
    assert(status.first_pass_complete && !status.recovered && status.targets==burst.targets);
    char job[KUI_DEST_JOB_CAP];strcpy(job,status.job);options->job=job;
    assert(!burst_inspect(job,false));
    burst.stop_ready=false;burst.phase=1;test.cancelled=false;test.repairing=true;
    if(!strcmp(test.test,"burst-reverse") || !strcmp(test.test,"burst-interior"))options->passes=5;
    if(!strcmp(test.test,"burst-held-recovery"))ops->recover=recover;
    if(!strcmp(test.test,"burst-stop-mid"))test.stop_after=5;
    if(!strcmp(test.test,"burst-torn-repaired"))test.fault="torn-repaired";
    enum kui_salvage_result result=run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status);
    if(!strcmp(test.test,"burst-good")) {
        assert(result==KUI_SALVAGE_RESOLVED && burst.batches==32 && !burst.singles);
        assert(status.repair_batches==32 && !status.repair_singles && status.repair_batch_accepted==512 && !status.repair_single_accepted);
        printf("PASS repair optical reduction: 512 queued sectors, 32 requests, 16x fewer than singleton reads\n");
    } else if(!strcmp(test.test,"burst-mixed")) {
        assert(result==KUI_SALVAGE_RESOLVED && burst.batches==32 && burst.singles==4);
        assert(status.repair_batch_accepted==508 && status.repair_single_accepted==4);
    } else if(!strcmp(test.test,"burst-prefix")) {
        assert(result==KUI_SALVAGE_RESOLVED && burst.batches==32 && burst.singles==352);
        assert(status.repair_batch_accepted==160 && status.repair_single_accepted==352);
    } else if(!strcmp(test.test,"burst-gaps")) {
        assert(result==KUI_SALVAGE_RESOLVED && burst.targets==34 && burst.batches==2 && burst.singles==2);
    } else if(!strcmp(test.test,"burst-audio")) {
        assert(result==KUI_SALVAGE_RESOLVED && burst.targets==4 && !burst.batches && burst.singles==8);
        assert(!status.repair_batches && status.repair_singles==8 && status.repair_single_accepted==4);
    } else if(!strcmp(test.test,"burst-boundaries")) {
        assert(result==KUI_SALVAGE_RESOLVED && burst.targets==20 && burst.batches==2 && burst.singles==8);
    } else if(!strcmp(test.test,"burst-reverse")) {
        assert(result==KUI_SALVAGE_RESOLVED && status.pass==2 && burst.batches==62 && burst.singles==480 && burst.events==512);
        assert(burst.first_reverse_fad==45150+496 && burst.first_reverse_count==16);
        for(unsigned i=0;i<512;++i)assert(burst.order[i]==(i<32?i:543-i));
    } else if(!strcmp(test.test,"burst-interior")) {
        assert(result==KUI_SALVAGE_RESOLVED && status.pass==2 && burst.batches==65 && burst.singles==480 && burst.events==512);
        assert(burst.first_reverse_fad==45150+504 && burst.first_reverse_count==8);
        for(unsigned i=0;i<32;++i)assert(burst.order[i]==7u+16u*i);
        unsigned at=32;for(unsigned n=512;n--;)if(n%16u!=7u)assert(burst.order[at++]==n);
        assert(at==512);
    } else {
        bool unresolved=!strcmp(test.test,"burst-all-bad") || !strcmp(test.test,"burst-held-recovery");
        bool failed=!strcmp(test.test,"burst-fatal") || !strcmp(test.test,"burst-torn-repaired") || !strcmp(test.test,"burst-staged-mutation");
        assert(result==(unresolved?KUI_SALVAGE_UNRESOLVED:failed?KUI_SALVAGE_FAILED:KUI_SALVAGE_STOPPED) && !status.complete);
        if(!strcmp(test.test,"burst-all-bad"))assert(!status.recovered && burst.batches==32 && burst.singles==512);
        if(!strcmp(test.test,"burst-held-recovery"))assert(status.recovered==504 && status.remaining==8 && burst.batches==32 && burst.singles==8 && test.recoveries==1);
        if(!strcmp(test.test,"burst-fatal") || !strcmp(test.test,"burst-cancel-read"))assert(!status.recovered && burst.batches==1 && !burst.singles);
        if(!strcmp(test.test,"burst-stop-before-read"))assert(!status.recovered && !burst.batches && !burst.singles);
        if(!strcmp(test.test,"burst-stop-mid"))assert(status.recovered==5 && burst.batches==1 && !burst.singles);
        if(!strcmp(test.test,"burst-stop-candidate"))assert(!status.recovered && burst.batches==1 && !burst.singles);
        if(!strcmp(test.test,"burst-staged-mutation"))assert(status.recovered==7 && burst.mutated && burst.batches==1 && !burst.singles);
        if(!strcmp(test.test,"burst-torn-repaired"))assert(!status.recovered && test.injected);
        assert(burst_inspect(job,!strcmp(test.test,"burst-torn-repaired"))==status.recovered);
        burst.phase=2;burst.batches=burst.singles=0;burst.first_batch=NULL;
        test.cancelled=false;test.stop_after=0;test.fault=NULL;options->passes=1;
        result=run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status);
    }
    assert(result==KUI_SALVAGE_RESOLVED && status.complete && status.targets==burst.targets && status.recovered==burst.targets && !status.remaining);
    test.capturing=false;compare_complete(job);
}
