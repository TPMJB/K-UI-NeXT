/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual saved-job trial sequencing, publication and restart on FAT images. */
static struct {
    unsigned phase,profile,switches,qualification_reads,reads,progress_done;
    unsigned seen[16][KUI_SALVAGE_TRIAL_PROFILES],order[64],fads[64];
    bool armed,seeded,fatal,unqualified;
    uint8_t alternate_anchor[KUI_RAW_BYTES],alternate_target[KUI_RAW_BYTES];
} trial;
static bool trial_case(void) {return !strncmp(test.test,"trial-",6);}
static bool trial_is(const char *name) {return !strcmp(test.test,name);}
static void trial_progress(const struct kui_salvage_status *status) {
    if(!trial.phase) {
        if(trial_is("trial-unfinished") && status->collection_active && status->done>=32u*KUI_RAW_BYTES)
            test.cancelled=true;
        else if(!trial_is("trial-unfinished") && status->first_pass_complete && !status->pass)
            test.cancelled=true;
        return;
    }
    if(status->trial_active) {
        assert(status->trial_done>=trial.progress_done && status->trial_done<=status->trial_total);
        trial.progress_done=status->trial_done;
        if(trial_is("trial-stop-qualification") && !status->trial_qualification_done && trial.qualification_reads)
            test.cancelled=true;
        if(trial_is("trial-stop-before-read") && status->trial_qualification_done &&
           status->read_state==KUI_SALVAGE_READ_READING && !trial.reads)test.cancelled=true;
    }
}
static int trial_select_fixture(void *ctx,unsigned profile) {
    (void)ctx;assert(profile<KUI_SALVAGE_TRIAL_PROFILES && trial.phase && !trial.fatal);
    ++trial.switches;
    if(trial_is("trial-invalid-selection"))return 2;
    if(trial_is("trial-select-fatal") && profile==1) {trial.fatal=true;return -1;}
    if(((trial_is("trial-unsupported") || trial_is("trial-select-stop") || trial_is("trial-two-profiles")) && profile==2) ||
       (trial_is("trial-two-profiles") && profile==3)) {
        if(trial_is("trial-select-stop"))test.cancelled=true;
        return 0;
    }
    if(trial_is("trial-profile-lost") && trial.reads && profile==2)return 0;
    trial.profile=profile;test.now+=7;return 1;
}
static bool trial_optical(unsigned ti,uint32_t fad,unsigned count,uint8_t *data,enum kui_read_result *result) {
    assert(!trial.fatal);unsigned at=fad-plan.tracks[ti].start;
    memcpy(data,original[ti]+(size_t)at*KUI_RAW_BYTES,(size_t)count*KUI_RAW_BYTES);*result=KUI_READ_OK;
    if(!trial.phase) {
        if(active_status->collection_active) {
            if(trial_is("trial-audio-only")) {
                if(ti==1)*result=KUI_READ_RETRY;
            } else if(ti==2)
                for(unsigned n=0;n<count;++n)if(at+n>=8 && at+n<(trial_is("trial-single")?9u:24u))data[n*KUI_RAW_BYTES+2200]^=1;
        }
        return true;
    }
    if(!active_status->trial_active)return true; /* Admission identity only. */
    assert(count==1);
    if(!active_status->trial_qualification_done) {
        ++trial.qualification_reads;
        if((trial_is("trial-qualify-fail") && !trial.profile) ||
           (trial_is("trial-qualify-skip") && trial.profile==2)) {*result=KUI_READ_RETRY;trial.unqualified=true;}
        if(trial_is("trial-identity-mismatch") && trial.profile==1) {
            /* Another valid FAD-matching codeword, not merely parity damage. */
            if(fad==45150)memcpy(data,trial.alternate_anchor,KUI_RAW_BYTES);
        }
        return true;
    }
    assert(ti==2 && at>=8 && at<24 && trial.reads<64);
    trial.order[trial.reads]=trial.profile;trial.fads[trial.reads]=fad;++trial.reads;
    assert(!trial.seen[at-8][trial.profile]);++trial.seen[at-8][trial.profile];
    test.now+=100u*(trial.profile+1);
    if(trial_is("trial-budget") && trial.reads==1)test.now+=300001u;
    if(trial_is("trial-fatal") && trial.reads==2) {trial.fatal=true;*result=KUI_READ_FATAL;return true;}
    if(trial_is("trial-stop-read") && trial.reads==2) {test.cancelled=true;*result=KUI_READ_RETRY;return true;}
    if(trial_is("trial-all-bad")) {*result=KUI_READ_RETRY;return true;}
    if(trial_is("trial-invalid-partial")) {data[2200]^=1;*result=KUI_READ_PARTIAL;return true;}
    if(trial_is("trial-partial"))*result=KUI_READ_PARTIAL;
    if(trial_is("trial-conflict") && trial.reads==2)memcpy(data,trial.alternate_target,KUI_RAW_BYTES);
    return true;
}
static unsigned trial_inspect(const char *job) {
    remount();char path[512];uint8_t event[512];UINT got;FIL journal;bool repaired[16]={false};unsigned targets=0,repairs=0;
    snprintf(path,sizeof(path),"0:%s/journal.bin",job);assert(f_open(&journal,path,FA_READ)==FR_OK);
    unsigned expected_targets=trial_is("trial-single")?1u:16u;
    while(f_tell(&journal)<f_size(&journal)) {
        assert(f_read(&journal,event,sizeof(event),&got)==FR_OK && got==sizeof(event));
        assert(get32(event+508)==kui_crc32(0,event,508));
        unsigned type=get32(event+16);
        if(type==1) {assert(targets<expected_targets && get32(event+20)==2 && get32(event+24)==8+targets);++targets;}
        if(type==5) {unsigned index=get32(event+20);assert(index<expected_targets && !repaired[index]);repaired[index]=true;++repairs;}
    }
    assert(targets==expected_targets && f_close(&journal)==FR_OK);
    uint8_t data[40*KUI_RAW_BYTES],expected[sizeof(data)];
    for(unsigned ti=0;ti<3;++ti) {
        unsigned size=(plan.tracks[ti].end-plan.tracks[ti].start)*KUI_RAW_BYTES;
        memcpy(expected,original[ti],size);
        if(ti==2)for(unsigned n=0;n<expected_targets;++n)if(!repaired[n])memset(expected+(8+n)*KUI_RAW_BYTES,0,KUI_RAW_BYTES);
        snprintf(path,sizeof(path),"0:%s/track%02u.%s",job,ti+1,ti==1?"raw":"bin");
        assert(load(path,data,sizeof(data))==size && !memcmp(data,expected,size));
    }
    assert(f_mount(NULL,"0:",0)==FR_OK);return repairs;
}
static void run_trial_fixture(char **argv,struct kui_capture_ops *ops,struct kui_salvage_options *options) {
    struct kui_salvage_status status;enum kui_salvage_result result;
    const char *names[]={"alternate-anchor.raw","alternate-target.raw"};
    uint8_t *buffers[]={trial.alternate_anchor,trial.alternate_target};
    for(unsigned i=0;i<2;++i) {
        char path[1024];snprintf(path,sizeof(path),"%s/%s",argv[2],names[i]);FILE *file=fopen(path,"rb");
        assert(file && fread(buffers[i],1,KUI_RAW_BYTES,file)==KUI_RAW_BYTES && !fclose(file));
    }
    if(trial_is("trial-invalid-action")) {
        options->control_trial=true;options->trial_select=trial_select_fixture;
        assert(run_salvage(&plan,ops,options,KUI_SALVAGE_NEW,&status)==KUI_SALVAGE_FAILED && !test.reads && !test.writes);return;
    }
    result=run_salvage(&plan,ops,options,KUI_SALVAGE_NEW,&status);
    assert(result==KUI_SALVAGE_STOPPED && status.job[0]);
    char job[KUI_DEST_JOB_CAP];strcpy(job,status.job);options->job=job;
    if(!trial_is("trial-unfinished"))assert(status.first_pass_complete && status.done==plan.bytes);
    trial.phase=1;test.cancelled=false;test.repairing=true;test.healthy=true;
    options->control_trial=true;options->trial_select=trial_select_fixture;ops->recover=recover;
    if(trial_is("trial-missing-callback"))options->trial_select=NULL;
    if(trial_is("trial-missing-clock"))ops->now_ms=NULL;
    unsigned reads=test.reads,writes=test.writes;
    result=run_salvage(&plan,ops,options,KUI_SALVAGE_RECOVER,&status);
    assert(!status.trial_active && !test.recoveries);
    if(trial_is("trial-missing-callback") || trial_is("trial-missing-clock")) {
        assert(result==KUI_SALVAGE_FAILED && test.reads==reads && test.writes==writes);return;
    }
    if(trial_is("trial-unfinished") || trial_is("trial-audio-only")) {
        assert(result==KUI_SALVAGE_FAILED && !trial.switches && !trial.reads);return;
    }
    unsigned expected_targets=trial_is("trial-single")?1u:16u,samples=trial_is("trial-single")?1u:8u;
    assert(status.targets==expected_targets && status.trial_sample_count==samples);
    if(trial_is("trial-qualify-fail") || trial_is("trial-select-fatal") || trial_is("trial-identity-mismatch") || trial_is("trial-invalid-selection")) {
        assert(result==KUI_SALVAGE_FAILED && !trial.reads && !status.trial_complete && !status.recovered);
        assert(trial_inspect(job)==0);return;
    }
    if(trial_is("trial-stop-qualification") || trial_is("trial-select-stop") || trial_is("trial-stop-before-read")) {
        assert(result==KUI_SALVAGE_STOPPED && !trial.reads && !status.recovered && !status.trial_complete);
        assert(trial_inspect(job)==0);return;
    }
    if(trial_is("trial-budget") || trial_is("trial-fatal") || trial_is("trial-stop-read") || trial_is("trial-conflict")) {
        assert(result==(trial_is("trial-budget")?KUI_SALVAGE_UNRESOLVED:
            (trial_is("trial-fatal") || trial_is("trial-conflict"))?KUI_SALVAGE_FAILED:KUI_SALVAGE_STOPPED));
        assert(status.recovered==1 && trial_inspect(job)==1);
        assert(status.trial_budget_expired==trial_is("trial-budget"));
        assert(!status.trial_complete);
        assert(trial.reads==(trial_is("trial-budget")?1u:2u));
    } else {
        assert(result==(trial_is("trial-single")?KUI_SALVAGE_RESOLVED:KUI_SALVAGE_UNRESOLVED) && status.trial_complete && !status.trial_budget_expired);
        unsigned profiles=trial_is("trial-two-profiles")?2u:
            (trial_is("trial-unsupported") || trial_is("trial-qualify-skip"))?3u:4u;
        assert(status.trial_done==samples*profiles && status.trial_total==samples*profiles);
        assert(trial.reads==(trial_is("trial-profile-lost")?24u:samples*profiles));
        assert(status.recovered==((trial_is("trial-all-bad") || trial_is("trial-invalid-partial"))?0u:samples));
        assert(trial_inspect(job)==status.recovered);
        const unsigned ranks[]={0,2,4,6,8,10,12,15};
        unsigned seen=0,admitted[4],admitted_count=0,first[4]={0};
        for(unsigned p=0;p<4;++p)if(status.trial_qualified[p])admitted[admitted_count++]=p;
        assert(admitted_count==profiles);
        for(unsigned sample=0;sample<samples;++sample)for(unsigned turn=0;turn<profiles;++turn) {
            unsigned p=admitted[(sample+turn)%profiles];
            if(!turn)++first[p];
            if(trial_is("trial-profile-lost") && p==2)continue;
            assert(trial.fads[seen]==45158+ranks[sample] && trial.order[seen]==p);++seen;
        }
        assert(seen==trial.reads);
        unsigned minimum=8,maximum=0;
        for(unsigned i=0;i<admitted_count;++i) {
            unsigned n=first[admitted[i]];if(n<minimum)minimum=n;if(n>maximum)maximum=n;
        }
        assert(maximum-minimum<=1); /* Rotates among admitted profiles only. */
        for(unsigned p=0;p<4;++p) {
            if(!status.trial_qualified[p] || (trial_is("trial-profile-lost") && p==2))assert(!status.trial_profile_reads[p]);
            else assert(status.trial_profile_reads[p]==samples);
        }
    }
    /* Ordinary retry resumes the same authoritative job without trial state. */
    options->control_trial=false;options->trial_select=NULL;test.cancelled=false;trial.fatal=false;trial.phase=2;
    ops->recover=NULL;test.stop_after=0;
    result=run_salvage(&plan,ops,options,KUI_SALVAGE_RECOVER,&status);
    assert(result==KUI_SALVAGE_RESOLVED && status.complete && status.recovered==expected_targets && !status.trial_active && !status.trial_complete);
    test.capturing=false;compare_complete(job);
    puts("PASS trial fixed sample, balanced profiles, guarded publication and ordinary resume");
}
