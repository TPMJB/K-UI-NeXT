/* SPDX-License-Identifier: GPL-3.0-only */
/* Model released medium-error buffers independently of transfer byte counts.
 * Expected holes are explicit, including valid data returned after a gap. */
#define HEALTH_SECTORS (80u*32u)
static struct {
    bool missing[3][HEALTH_SECTORS],healthy,stop_ready,stop_boundary,health_announced;
    unsigned health_reads,health_checks,batches,partial_reads,phase;
    uint8_t alternate[KUI_RAW_BYTES];
} partial;
static bool partial_case(void) {return !strncmp(test.test,"partial-",8) || !strncmp(test.test,"health-",7);}
static bool health_case(void) {return !strncmp(test.test,"health-",7);}
static void partial_plan(void) {
    if(health_case()) {plan.tracks[2].end=plan.tracks[2].toc_end=45150+HEALTH_SECTORS;plan.bytes=(44u+HEALTH_SECTORS)*KUI_RAW_BYTES;}
    if(!strncmp(test.test,"partial-cd-",11)) {
        plan.profile=KUI_CAPTURE_PROFILE_CD_TOC;plan.tracks[0].toc_end=190;
        plan.tracks[1].start=190;plan.tracks[1].end=plan.tracks[1].toc_end=194;
        plan.tracks[2].start=194;plan.tracks[2].end=plan.tracks[2].toc_end=234;plan.tracks[2].session=0;
    }
}
static void partial_progress(const struct kui_salvage_status *status) {
    bool health=!strcmp(status->message,"Collection health check; verifying saved disc anchors");
    if(health && !partial.health_announced)++partial.health_checks;
    partial.health_announced=health;
    if(health && !strcmp(test.test,"health-stop-before-probe")) {test.cancelled=true;test.did_cancel=true;}
    if(partial.stop_ready && status->first_pass_complete && !status->pass) {test.cancelled=true;test.did_cancel=true;}
    if(partial.stop_boundary && status->collection_active && status->done>=(44u+288u)*KUI_RAW_BYTES) {test.cancelled=true;test.did_cancel=true;}
}
static void partial_mark(unsigned ti,uint32_t fad,unsigned count,bool missing) {
    unsigned at=fad-plan.tracks[ti].start;assert(at+count<=HEALTH_SECTORS);
    for(unsigned n=0;n<count;++n)partial.missing[ti][at+n]=missing;
}
static bool partial_optical(unsigned ti,uint32_t fad,unsigned count,uint8_t *data,enum kui_read_result *result) {
    const struct kui_salvage_status *s=active_status;assert(s);
    memcpy(data,original[ti]+(fad-plan.tracks[ti].start)*KUI_RAW_BYTES,(size_t)count*KUI_RAW_BYTES);
    *result=KUI_READ_OK;
    if(partial.healthy)return true;
    if(!s->collection_active) {
        if(s->pass && !partial.healthy && !strcmp(test.test,"partial-audio") && ti==1) {*result=KUI_READ_PARTIAL;++partial.partial_reads;}
        if(s->pass && !partial.healthy && !strncmp(test.test,"partial-repair",14) && ti==0) {
            *result=KUI_READ_PARTIAL;++partial.partial_reads;
            if(!strcmp(test.test,"partial-repair-invalid") && fad==152)data[2200]^=1;
            if(!strcmp(test.test,"partial-repair-streak") && fad<158)data[2200]^=1;
        }
        return true;
    }
    if(count==1) {
        ++partial.health_reads;
        if(!strcmp(test.test,"health-fatal")) {*result=KUI_READ_FATAL;return true;}
        if(!strcmp(test.test,"health-cancel")) {test.cancelled=true;*result=KUI_READ_RETRY;return true;}
        if(!strcmp(test.test,"health-different") && fad==45150) {memcpy(data,partial.alternate,KUI_RAW_BYTES);return true;}
        if(!strcmp(test.test,"health-second-different") && fad==plan.tracks[2].end-1) {memcpy(data,partial.alternate,KUI_RAW_BYTES);return true;}
        if(!strcmp(test.test,"health-one-anchor") && fad==45150) {*result=KUI_READ_RETRY;return true;}
        if(!strcmp(test.test,"health-partial-anchors")) {*result=KUI_READ_PARTIAL;++partial.partial_reads;return true;}
        bool unreadable=!strcmp(test.test,"health-recover") || !strcmp(test.test,"health-recover-fail") ||
            !strcmp(test.test,"health-verify-fail") || !strcmp(test.test,"health-no-recover");
        if(unreadable && (!test.recoveries || !strcmp(test.test,"health-verify-fail"))) *result=KUI_READ_RETRY;
        return true;
    }
    if(health_case() && ti==2) {
        unsigned n=partial.batches++;
        if(!strcmp(test.test,"health-partial-reset")) {
            if(n<7 || (n>7 && n<15)) {*result=KUI_READ_RETRY;partial_mark(ti,fad,count,true);}
            else if(n==7) {
                memset(data+KUI_RAW_BYTES,0xa5,(size_t)(count-1)*KUI_RAW_BYTES);*result=KUI_READ_PARTIAL;
                partial_mark(ti,fad+1,count-1,true);++partial.partial_reads;
            }
            return true;
        }
        if(!strcmp(test.test,"health-recover") && test.recoveries)return true;
        *result=!strcmp(test.test,"health-invalid")?KUI_READ_OK:KUI_READ_RETRY;
        memset(data,0xa5,(size_t)count*KUI_RAW_BYTES);partial_mark(ti,fad,count,true);return true;
    }
    if(!partial.healthy && !strcmp(test.test,"partial-audio") && ti==1) {
        partial_mark(ti,fad,count,true);*result=KUI_READ_PARTIAL;++partial.partial_reads;return true;
    }
    if(health_case() || !strcmp(test.test,"partial-audio"))return true;
    if(partial.healthy || ti!=0 || fad!=150)return true;
    ++partial.partial_reads;*result=KUI_READ_PARTIAL;
    if(!strncmp(test.test,"partial-repair",14)) {*result=KUI_READ_RETRY;partial_mark(ti,fad,count,true);return true;}
    if(!strcmp(test.test,"partial-fill")) {memset(data,0xa5,(size_t)count*KUI_RAW_BYTES);partial_mark(ti,fad,count,true);return true;}
    if(!strcmp(test.test,"partial-mixed")) {
        memcpy(data+2*KUI_RAW_BYTES,data,KUI_RAW_BYTES);
        data[3*KUI_RAW_BYTES+20]^=1;data[4*KUI_RAW_BYTES+2200]^=1;
        memset(data+31*KUI_RAW_BYTES,0xa5,KUI_RAW_BYTES);
        const unsigned bad[]={2,3,4,31};for(unsigned n=0;n<4;++n)partial.missing[0][bad[n]]=true;
        return true;
    }
    assert(!strcmp(test.test,"partial-prefix") || !strncmp(test.test,"partial-cd-",11));
    memset(data+11*KUI_RAW_BYTES,0xa5,(size_t)(count-11)*KUI_RAW_BYTES);partial_mark(ti,fad+11,count-11,true);return true;
}
static bool partial_recover(bool *result) {
    if(!strcmp(test.test,"partial-repair-streak")) {*result=true;return true;}
    assert(health_case());
    if(!strcmp(test.test,"health-recover-fail")) {*result=false;return true;}
    assert(!strcmp(test.test,"health-recover") || !strcmp(test.test,"health-verify-fail"));
    *result=true;return true;
}
static unsigned partial_saved(const char *job,const uint32_t sectors[3]) {
    remount();unsigned holes=0;char path[512];uint8_t data[32*KUI_RAW_BYTES],expected[sizeof(data)];
    for(unsigned ti=0;ti<3;++ti) {
        snprintf(path,sizeof(path),"0:%s/track%02u.%s",job,ti+1,plan.tracks[ti].control==4?"bin":"raw");FIL file;
        assert(f_open(&file,path,FA_READ)==FR_OK && f_size(&file)==(uint64_t)sectors[ti]*KUI_RAW_BYTES);
        for(unsigned at=0;at<sectors[ti];) {
            unsigned count=sectors[ti]-at;if(count>32)count=32;UINT got=0;
            assert(f_read(&file,data,count*KUI_RAW_BYTES,&got)==FR_OK && got==count*KUI_RAW_BYTES);
            memcpy(expected,original[ti]+at*KUI_RAW_BYTES,count*KUI_RAW_BYTES);
            for(unsigned n=0;n<count;++n)if(partial.missing[ti][at+n]) {memset(expected+n*KUI_RAW_BYTES,0,KUI_RAW_BYTES);++holes;}
            assert(!memcmp(data,expected,count*KUI_RAW_BYTES));at+=count;
        }
        assert(f_close(&file)==FR_OK);
    }
    snprintf(path,sizeof(path),"0:%s/journal.bin",job);FIL file;uint8_t event[512];UINT got;unsigned targets=0;
    assert(f_open(&file,path,FA_READ)==FR_OK);
    while(f_tell(&file)<f_size(&file)) {
        assert(f_read(&file,event,sizeof(event),&got)==FR_OK && got==sizeof(event));
        if(get32(event+16)==1) {unsigned ti=get32(event+20),at=get32(event+24);assert(ti<3 && at<sectors[ti] && partial.missing[ti][at]);++targets;}
    }
    assert(targets==holes && f_close(&file)==FR_OK && f_mount(NULL,"0:",0)==FR_OK);return holes;
}
static void run_partial_fixture(char **argv,struct kui_capture_ops *ops,struct kui_salvage_options *options) {
    test.healthy=true;ops->now_ms=NULL;
    if(health_case() && strcmp(test.test,"health-no-recover"))ops->recover=recover;
    if(!strcmp(test.test,"partial-repair") || !strcmp(test.test,"partial-repair-streak"))ops->recover=recover;
    partial.stop_ready=strncmp(test.test,"partial-repair",14)!=0;
    if(!strcmp(test.test,"health-different") || !strcmp(test.test,"health-second-different")) {
        char path[1024];snprintf(path,sizeof(path),"%s/%s",argv[2],!strcmp(test.test,"health-different")?"alternate.raw":"alternate-end.raw");FILE *in=fopen(path,"rb");
        assert(in && fread(partial.alternate,1,KUI_RAW_BYTES,in)==KUI_RAW_BYTES && !fclose(in));
    }
    enum kui_salvage_action action=KUI_SALVAGE_NEW;char job[KUI_DEST_JOB_CAP]={0};
    if(!strcmp(test.test,"health-manual-skip")) {
        seed_legacy_narrow(2,job);options->job=job;options->defer_next_range=true;action=KUI_SALVAGE_RESUME;
        partial.missing[0][2]=partial.missing[1][2]=true;partial_mark(2,45182,32,true);partial.stop_boundary=true;
    }
    struct kui_salvage_status status;enum kui_salvage_result result=run_salvage(&plan,ops,options,action,&status);
    if(!job[0])strcpy(job,status.job);
    assert(job[0]);
    uint32_t sectors[3]={40,4,plan.tracks[2].end-plan.tracks[2].start};
    bool failed_health=health_case() && strcmp(test.test,"health-region") && strcmp(test.test,"health-invalid") &&
        strcmp(test.test,"health-recover") && strcmp(test.test,"health-partial-reset") && strcmp(test.test,"health-manual-skip") &&
        strcmp(test.test,"health-one-anchor") && strcmp(test.test,"health-partial-anchors");
    if(failed_health) {
        sectors[2]=256;
        bool stopped=!strcmp(test.test,"health-cancel") || !strcmp(test.test,"health-stop-before-probe");
        assert(result==(stopped?KUI_SALVAGE_STOPPED:KUI_SALVAGE_FAILED));
        assert(!status.first_pass_complete && status.done==(44u+256u)*KUI_RAW_BYTES && status.targets==256 && !status.recovered);
        assert(partial.batches==8 && partial.health_checks==1);
        if(!strcmp(test.test,"health-stop-before-probe"))assert(!partial.health_reads);
        else assert(partial.health_reads);
        unsigned expected_recovery=!strcmp(test.test,"health-recover-fail") || !strcmp(test.test,"health-verify-fail");
        assert(test.recoveries==expected_recovery && test.collection_recoveries==expected_recovery);
        assert(partial_saved(job,sectors)==256);
        partial.healthy=true;partial.stop_ready=false;test.cancelled=false;options->job=job;
        result=run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status);
        assert(result==KUI_SALVAGE_RESOLVED && status.complete && status.targets==256 && status.recovered==256);
        assert(test.recoveries==expected_recovery);test.capturing=false;compare_complete(job);return;
    }
    if(!strcmp(test.test,"health-manual-skip")) {
        sectors[2]=288;assert(result==KUI_SALVAGE_STOPPED && !status.first_pass_complete && status.targets==258);
        assert(partial.batches==7 && !partial.health_checks && !partial.health_reads && !test.recoveries);
        assert(partial_saved(job,sectors)==258);return;
    }
    if(!strcmp(test.test,"partial-repair")) {
        assert(result==KUI_SALVAGE_RESOLVED && status.targets==32 && status.recovered==32 && status.complete && partial.partial_reads==33 && !test.recoveries);
        test.capturing=false;compare_complete(job);return;
    }
    if(!strcmp(test.test,"partial-repair-invalid") || !strcmp(test.test,"partial-repair-streak")) {
        unsigned missing=!strcmp(test.test,"partial-repair-streak")?8u:1u;
        assert(result==KUI_SALVAGE_UNRESOLVED && status.targets==32 && status.recovered==32-missing && status.remaining==missing);
        assert(test.recoveries==(!strcmp(test.test,"partial-repair-streak")?1u:0u) && !test.collection_recoveries);
    } else {
        assert(result==KUI_SALVAGE_STOPPED && status.first_pass_complete && !status.recovered && status.done==plan.bytes);
        unsigned expected=!strcmp(test.test,"partial-mixed")?4:!strcmp(test.test,"partial-fill")?32:!strcmp(test.test,"partial-audio")?4:
            !strcmp(test.test,"health-recover")?256:!strcmp(test.test,"health-partial-reset")?479:health_case()?HEALTH_SECTORS:21;
        assert(status.targets==expected && partial_saved(job,sectors)==expected);
        if(!strcmp(test.test,"health-recover"))assert(partial.health_checks==1 && test.recoveries==1 && test.collection_recoveries==1);
        else if(!strcmp(test.test,"health-partial-reset"))assert(!partial.health_checks && !partial.health_reads && !test.recoveries);
        else if(health_case())assert(partial.health_checks==2 && partial.health_reads>=2 && !test.recoveries);
    }
    partial.stop_ready=false;test.cancelled=false;options->job=job;
    if(!strcmp(test.test,"partial-audio")) {
        result=run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status);
        assert(result==KUI_SALVAGE_UNRESOLVED && !status.recovered && status.remaining==4);
        assert(partial.partial_reads>=5);
    }
    partial.healthy=true;test.cancelled=false;
    /* The largest all-hole region needs >256 individual reads; the older
     * repair_order array is deliberately unused by these independent cases. */
    result=run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status);
    assert(result==KUI_SALVAGE_RESOLVED && status.complete && status.recovered==status.targets && !status.remaining);
    test.capturing=false;compare_complete(job);
}
