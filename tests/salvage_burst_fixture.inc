/* SPDX-License-Identifier: GPL-3.0-only */
/* Large inherited false-positive queues exercise the production repair path.
 * Optical reads, persisted bytes and target order are checked independently. */
#define BURST_SECTORS 512u
static struct {
    bool holes[3][1024u],stop_ready,stop_collection,mutated,committed[1024];
    unsigned phase,targets,batches,singles,events,first_reverse_fad,first_reverse_count;
    unsigned target_track[1024],target_sector[1024],order[2048];
    uint8_t *first_batch;
    uint8_t alternate[KUI_RAW_BYTES];
} burst;
static bool burst_case(void) {return !strncmp(test.test,"burst-",6);}
static void burst_plan(void) {
    plan.tracks[2].end=plan.tracks[2].toc_end=45150+BURST_SECTORS;
    plan.bytes=(44u+BURST_SECTORS)*KUI_RAW_BYTES;
    if(legacy_index_case()) {
        plan.tracks[2].end=plan.tracks[2].toc_end=45150+1024u;
        plan.bytes=(44u+1024u)*KUI_RAW_BYTES;
        for(unsigned n=0;n<1024u;++n)burst.holes[2][n]=true;
    } else if(!strcmp(test.test,"burst-sd-window")) {
        plan.tracks[2].end=plan.tracks[2].toc_end=45150+BURST_SECTORS-1;
        plan.bytes-=KUI_RAW_BYTES;
        for(unsigned n=3;n<BURST_SECTORS-1;++n)burst.holes[2][n]=true;
    } else if(!strcmp(test.test,"burst-audio")) {
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
static struct {
    bool active,journal_seen,targets_seen,seed_candidate,pending_applied,injected,stage_mutated,clipped_tail,track_io;
    unsigned cancel_mode,reads,track_reads,largest_read;
    uint64_t journal_done,target_done,media_calls,media_blocks;
    uint8_t *stage;
    unsigned stage_bytes;
} saved_check;
static bool saved_check_case(void) {return !strncmp(test.test,"burst-sd-",9);}
static bool saved_check_targets(void) {
    return saved_check.active && active_status && active_status->sd_phase==KUI_SALVAGE_SD_TARGETS;
}
static bool saved_check_media_read(size_t count) {
    if(!saved_check_targets())return false;
    ++saved_check.media_calls;saved_check.media_blocks+=count;
    if(!saved_check.injected && !strcmp(test.test,"burst-sd-media-error") && active_status->sd_done>=64) {
        saved_check.injected=true;return true;
    }
    return false;
}
static bool saved_check_track_file(FIL *file,UINT bytes) {
    return saved_check_targets() && bytes>=KUI_RAW_BYTES &&
        f_size(file)==(uint64_t)(plan.tracks[2].end-plan.tracks[2].start)*KUI_RAW_BYTES;
}
static bool saved_check_read_error(FIL *file,void *data,UINT bytes,UINT *got,FRESULT *result) {
    saved_check.track_io=saved_check_track_file(file,bytes);
    if(!saved_check.track_io)return false;
    ++saved_check.track_reads;if(bytes>saved_check.largest_read)saved_check.largest_read=bytes;
    if(bytes>KUI_RAW_BYTES) {
        assert(!((uintptr_t)data%32u) && !(f_tell(file)%512u));
        assert(bytes<=75776u);
        assert(!(bytes%512u) || f_tell(file)+bytes==f_size(file));
    }
    if(!saved_check.injected && !strcmp(test.test,"burst-sd-read-error") && active_status->sd_done>=32) {
        saved_check.injected=true;*got=0;*result=FR_DISK_ERR;return true;
    }
    return false;
}
static void saved_check_read_result(FIL *file,void *data,UINT bytes,UINT *got,FRESULT result) {
    saved_check.track_io=false;
    if(!saved_check_track_file(file,bytes) || result!=FR_OK || *got!=bytes)return;
    if(bytes>KUI_RAW_BYTES) {
        if(bytes%512u) {assert(f_tell(file)==f_size(file));saved_check.clipped_tail=true;}
        if(!saved_check.stage) {
            unsigned first_sector=!strcmp(test.test,"burst-sd-window")?3u:0u;
            unsigned head=(first_sector*KUI_RAW_BYTES)%512u;
            saved_check.stage=(uint8_t *)data+head;saved_check.stage_bytes=bytes-head;
            if(!strcmp(test.test,"burst-sd-window"))assert(head==400u && (uintptr_t)saved_check.stage%32u==16u);
        }
    }
    if(!saved_check.injected && !strcmp(test.test,"burst-sd-short-read") && active_status->sd_done>=32) {
        saved_check.injected=true;--*got;
    }
    if(saved_check.cancel_mode==4 && !active_status->sd_done)test.cancelled=true;
}
static void saved_check_progress(const struct kui_salvage_status *status) {
    if(!saved_check.active || status->sd_phase==KUI_SALVAGE_SD_NONE)return;
    assert(!status->pass && !status->collection_active && status->sd_done<=status->sd_total);
    if(!saved_check.journal_seen) {saved_check.journal_seen=true;saved_check.reads=test.reads;}
    assert(test.reads==saved_check.reads); /* SD checks cannot masquerade as new optical repairs. */
    if(status->sd_phase==KUI_SALVAGE_SD_JOURNAL) {
        assert(status->sd_total && status->sd_done>=saved_check.journal_done);
        saved_check.journal_done=status->sd_done;
        if(saved_check.cancel_mode==1 && status->sd_done>=10 && status->sd_done<status->sd_total)test.cancelled=true;
    } else {
        assert(status->sd_phase==KUI_SALVAGE_SD_TARGETS && status->sd_total==burst.targets);
        assert(status->targets==burst.targets);
        if(!strcmp(test.test,"burst-sd-pending")) {
            assert(status->recovered==5 || status->recovered==6);
            if(status->recovered==6)saved_check.pending_applied=true;
        } else assert(status->recovered==5);
        assert(status->remaining==burst.targets-status->recovered);
        assert(!status->read_count && status->read_state==KUI_SALVAGE_READ_NONE);
        if(!saved_check.targets_seen) {assert(!status->sd_done);saved_check.targets_seen=true;}
        assert(status->sd_done>=saved_check.target_done && status->sd_done<=saved_check.target_done+1);
        if(status->track)assert(status->track==3 && status->fad>=45150 && status->fad<plan.tracks[2].end);
        saved_check.target_done=status->sd_done;
        if(!strcmp(test.test,"burst-sd-staged-mutation") && !saved_check.stage_mutated && status->sd_done==1 && saved_check.stage) {
            assert(saved_check.stage_bytes>=32u*KUI_RAW_BYTES);
            saved_check.stage[31u*KUI_RAW_BYTES+17]^=1;saved_check.stage_mutated=true;
        }
        if((saved_check.cancel_mode==2 && !status->sd_done) || (saved_check.cancel_mode==3 && status->sd_done==111) ||
           (saved_check.cancel_mode==5 && status->sd_done==32))test.cancelled=true;
    }
}
static void burst_progress(const struct kui_salvage_status *status) {
    legacy_index_progress(status);
    saved_check_progress(status);
    if(burst.stop_ready && status->first_pass_complete && !status->pass) {test.cancelled=true;test.did_cancel=true;}
    if(burst.stop_collection && status->collection_active && status->done>=76u*KUI_RAW_BYTES) {
        test.cancelled=true;test.did_cancel=true;
    }
    if(burst.phase==1 && !strcmp(test.test,"burst-stop-before-read") && status->pass &&
       status->read_state==KUI_SALVAGE_READ_READING && status->read_count>1) {
        test.cancelled=true;test.did_cancel=true;
    }
}
static void burst_event(unsigned type,const uint8_t *event) {
    if(burst.phase!=1)return;
    if(type==7 && saved_check.seed_candidate) {assert(get32(event+20)==5);test.cancelled=true;}
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
    /* A resumed ready queue must never submit the multi-sector command that
     * failed to release firmware ownership in the hardware report. */
    if(!strncmp(test.test,"burst-policy-ready-",19) && count>1) {
        *result=KUI_READ_FATAL;return true;
    }
    if(!strcmp(test.test,"burst-policy-ready-hard")) {
        memset(data,0xcc,(size_t)count*KUI_RAW_BYTES);*result=KUI_READ_RETRY;return true;
    }
    if(!strcmp(test.test,"burst-policy-ready-fatal") && at==5) {
        *result=KUI_READ_FATAL;return true;
    }
    /* One yielding batch separates the first miss from the next two misses.
     * Successful singleton fallbacks must not hide zero-yield batch history. */
    if(!strcmp(test.test,"burst-policy-yield-reset") && count>1 && (at==0 || at==32 || at==48)) {
        memset(data,0xcc,(size_t)count*KUI_RAW_BYTES);*result=KUI_READ_RETRY;return true;
    }
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
/* Deliberately corrupt persisted evidence between invocations, preserving a
 * complete copy so rejection can be followed by an ordinary valid Resume. */
static void saved_check_edit(const char *job,const char *name,uint64_t offset,uint8_t *bytes,unsigned size,bool writing) {
    remount();char path[512];FIL file;UINT count;
    snprintf(path,sizeof(path),"0:%s/%s",job,name);
    assert(f_open(&file,path,FA_READ|(writing?FA_WRITE:0))==FR_OK && f_lseek(&file,offset)==FR_OK && f_tell(&file)==offset);
    if(writing)assert(f_write(&file,bytes,size,&count)==FR_OK && count==size && f_sync(&file)==FR_OK);
    else assert(f_read(&file,bytes,size,&count)==FR_OK && count==size);
    assert(f_close(&file)==FR_OK && f_mount(NULL,"0:",0)==FR_OK);
}
static void run_saved_check_fixture(const char *job,struct kui_capture_ops *ops,struct kui_salvage_options *options) {
    struct kui_salvage_status status;
    test.stop_after=5;
    assert(run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status)==KUI_SALVAGE_STOPPED && status.recovered==5);
    assert(burst_inspect(job,false)==5);
    test.stop_after=0;test.cancelled=false;
    if(!strcmp(test.test,"burst-sd-pending")) {
        saved_check.seed_candidate=true;
        assert(run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status)==KUI_SALVAGE_STOPPED && status.recovered==5);
        saved_check.seed_candidate=false;test.cancelled=false;assert(burst_inspect(job,false)==5);
    }
    bool changed_placeholder=!strcmp(test.test,"burst-sd-placeholder");
    bool changed_candidate=!strcmp(test.test,"burst-sd-candidate");
    uint8_t saved[5120],damaged[5120];
    if(changed_placeholder) {
        saved_check_edit(job,"track03.bin",511u*KUI_RAW_BYTES+17,saved,1,false);
        assert(!saved[0]);damaged[0]=0x7a;
        saved_check_edit(job,"track03.bin",511u*KUI_RAW_BYTES+17,damaged,1,true);
    }
    if(changed_candidate) {
        saved_check_edit(job,"patches.bin",4u*5120u,saved,sizeof(saved),false);
        memcpy(damaged,saved,sizeof(damaged));damaged[64+17]^=1;
        put32(damaged+24,kui_crc32(0,damaged+64,KUI_RAW_BYTES));
        put32(damaged+sizeof(damaged)-4,kui_crc32(0,damaged,sizeof(damaged)-4));
        saved_check_edit(job,"patches.bin",4u*5120u,damaged,sizeof(damaged),true);
    }
    saved_check.active=true;
    saved_check.cancel_mode=!strcmp(test.test,"burst-sd-journal-stop")?1u:
        !strcmp(test.test,"burst-sd-check-start-stop")?2u:!strcmp(test.test,"burst-sd-check-mid-stop")?3u:
        !strcmp(test.test,"burst-sd-after-read-stop")?4u:!strcmp(test.test,"burst-sd-boundary-stop")?5u:0u;
    bool failed=changed_placeholder || changed_candidate || !strcmp(test.test,"burst-sd-staged-mutation") ||
        !strcmp(test.test,"burst-sd-short-read") || !strcmp(test.test,"burst-sd-read-error") || !strcmp(test.test,"burst-sd-media-error");
    unsigned batches=burst.batches,singles=burst.singles;
    enum kui_salvage_result result=run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status);
    assert(saved_check.journal_seen && saved_check.journal_done);
    if(saved_check.cancel_mode || failed) {
        assert(result==(failed?KUI_SALVAGE_FAILED:KUI_SALVAGE_STOPPED) && !status.complete);
        assert(saved_check.cancel_mode==1?status.recovered<=5:status.recovered==5);
        if(changed_placeholder)saved_check_edit(job,"track03.bin",511u*KUI_RAW_BYTES+17,saved,1,true);
        if(changed_candidate)saved_check_edit(job,"patches.bin",4u*5120u,saved,sizeof(saved),true);
        assert(burst.batches==batches && burst.singles==singles && burst_inspect(job,false)==5);
        if(saved_check.cancel_mode==1)assert(!saved_check.targets_seen && status.sd_phase==KUI_SALVAGE_SD_JOURNAL);
        else {
            assert(saved_check.targets_seen && status.sd_phase==KUI_SALVAGE_SD_TARGETS);
            if(saved_check.cancel_mode==2 || saved_check.cancel_mode==4)assert(!saved_check.target_done);
            if(saved_check.cancel_mode==3)assert(saved_check.target_done==111);
            if(saved_check.cancel_mode==5)assert(saved_check.target_done==32 && saved_check.track_reads==1);
        }
        if(changed_placeholder)assert(saved_check.target_done==511);
        if(changed_candidate)assert(saved_check.target_done==4);
        if(!strcmp(test.test,"burst-sd-staged-mutation"))assert(saved_check.stage_mutated && saved_check.target_done==31);
        if(!strcmp(test.test,"burst-sd-short-read") || !strcmp(test.test,"burst-sd-read-error"))assert(saved_check.injected && saved_check.target_done==32);
        if(!strcmp(test.test,"burst-sd-media-error"))assert(saved_check.injected && saved_check.target_done==64);
        saved_check.active=false;test.cancelled=false;
        result=run_salvage(&plan,ops,options,KUI_SALVAGE_RESUME,&status);
    } else {
        assert(saved_check.targets_seen && saved_check.target_done==burst.targets);
        assert(saved_check.track_reads==(burst.targets+31u)/32u+!strcmp(test.test,"burst-sd-pending"));
        assert(saved_check.largest_read==(!strcmp(test.test,"burst-sd-window")?75776u:32u*KUI_RAW_BYTES));
        if(!strcmp(test.test,"burst-sd-window"))assert(saved_check.clipped_tail);
        if(!strcmp(test.test,"burst-sd-pending"))assert(saved_check.pending_applied);
        printf("PASS saved check batching: %u targets, %u grouped track reads, %llu SD requests / %llu blocks\n",
            burst.targets,(burst.targets+31u)/32u,(unsigned long long)saved_check.media_calls,(unsigned long long)saved_check.media_blocks);
    }
    assert(result==KUI_SALVAGE_RESOLVED && status.complete && status.recovered==burst.targets && status.sd_phase==KUI_SALVAGE_SD_NONE);
    test.capturing=false;compare_complete(job);
}
static void run_burst_fixture(char **argv,struct kui_capture_ops *ops,struct kui_salvage_options *options) {
    if(!strcmp(test.test,"burst-staged-mutation")) {
        char path[1024];snprintf(path,sizeof(path),"%s/alternate-staged.raw",argv[2]);FILE *f=fopen(path,"rb");
        assert(f && fread(burst.alternate,1,KUI_RAW_BYTES,f)==KUI_RAW_BYTES && !fclose(f));
    }
    test.healthy=true;ops->now_ms=NULL;
    bool ready_policy=!strncmp(test.test,"burst-policy-ready-",19);
    bool incomplete_resume=!strcmp(test.test,"burst-policy-incomplete-resume");
    bool seed_ready=saved_check_case() || legacy_index_case() || ready_policy;
    struct kui_salvage_status status;char job[KUI_DEST_JOB_CAP];
    enum kui_salvage_action action=KUI_SALVAGE_NEW;
    if(seed_ready || incomplete_resume) {
        burst.stop_ready=seed_ready;burst.stop_collection=incomplete_resume;
        assert(run_salvage(&plan,ops,options,KUI_SALVAGE_NEW,&status)==KUI_SALVAGE_STOPPED);
        assert(!status.recovered && status.targets==(incomplete_resume?32u:burst.targets));
        assert(status.first_pass_complete==seed_ready);
        strcpy(job,status.job);options->job=job;
        if(seed_ready)assert(!burst_inspect(job,false));
        burst.stop_ready=burst.stop_collection=false;test.cancelled=false;
        action=!strcmp(test.test,"burst-policy-ready-recover")?KUI_SALVAGE_RECOVER:KUI_SALVAGE_RESUME;
    }
    burst.phase=1;test.repairing=true;
    if(saved_check_case()) {run_saved_check_fixture(job,ops,options);return;}
    if(legacy_index_case()) {run_legacy_index_fixture(job,ops,options);return;}
    if(!strcmp(test.test,"burst-reverse") || !strcmp(test.test,"burst-interior"))options->passes=5;
    if(!strcmp(test.test,"burst-held-recovery"))ops->recover=recover;
    if(!strcmp(test.test,"burst-stop-mid") || !strcmp(test.test,"burst-policy-ready-stop"))test.stop_after=5;
    if(!strcmp(test.test,"burst-torn-repaired"))test.fault="torn-repaired";
    /* Fresh/incomplete-collection cases retain batch validator and mutation
     * coverage; ready Resume/Recover cases exercise the singleton policy. */
    enum kui_salvage_result result=run_salvage(&plan,ops,options,action,&status);
    if(action==KUI_SALVAGE_NEW) {strcpy(job,status.job);options->job=job;}
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
    } else if(!strcmp(test.test,"burst-policy-yield-reset")) {
        assert(result==KUI_SALVAGE_RESOLVED && burst.batches==4 && burst.singles==496);
        assert(status.repair_batch_accepted==16 && status.repair_single_accepted==496);
    } else if(!strcmp(test.test,"burst-policy-incomplete-resume")) {
        assert(result==KUI_SALVAGE_RESOLVED && burst.batches==32 && !burst.singles);
        assert(status.collected_this_run==(BURST_SECTORS-32u)*KUI_RAW_BYTES);
    } else if(!strcmp(test.test,"burst-policy-ready-resume") || !strcmp(test.test,"burst-policy-ready-recover")) {
        assert(result==KUI_SALVAGE_RESOLVED && !burst.batches && burst.singles==BURST_SECTORS);
        assert(!status.repair_batches && status.repair_singles==BURST_SECTORS && !status.repair_batch_accepted);
    } else if(!strcmp(test.test,"burst-reverse")) {
        assert(result==KUI_SALVAGE_RESOLVED && status.pass==2 && burst.batches==4 && burst.singles==960 && burst.events==512);
        assert(!burst.first_reverse_fad && !burst.first_reverse_count);
        for(unsigned i=0;i<512;++i)assert(burst.order[i]==(i<32?i:543-i));
    } else if(!strcmp(test.test,"burst-interior")) {
        assert(result==KUI_SALVAGE_RESOLVED && status.pass==2 && burst.batches==65 && burst.singles==480 && burst.events==512);
        assert(burst.first_reverse_fad==45150+504 && burst.first_reverse_count==8);
        for(unsigned i=0;i<32;++i)assert(burst.order[i]==7u+16u*i);
        unsigned at=32;for(unsigned n=512;n--;)if(n%16u!=7u)assert(burst.order[at++]==n);
        assert(at==512);
    } else {
        bool unresolved=!strcmp(test.test,"burst-all-bad") || !strcmp(test.test,"burst-held-recovery") || !strcmp(test.test,"burst-policy-ready-hard");
        bool failed=!strcmp(test.test,"burst-fatal") || !strcmp(test.test,"burst-torn-repaired") || !strcmp(test.test,"burst-staged-mutation") || !strcmp(test.test,"burst-policy-ready-fatal");
        assert(result==(unresolved?KUI_SALVAGE_UNRESOLVED:failed?KUI_SALVAGE_FAILED:KUI_SALVAGE_STOPPED) && !status.complete);
        if(!strcmp(test.test,"burst-all-bad"))assert(!status.recovered && status.remaining==512 && burst.batches==2 && burst.singles==512);
        if(!strcmp(test.test,"burst-policy-ready-hard"))assert(!status.recovered && status.remaining==512 && !burst.batches && burst.singles==512);
        if(!strcmp(test.test,"burst-policy-ready-stop"))assert(status.recovered==5 && !burst.batches && burst.singles==5);
        if(!strcmp(test.test,"burst-policy-ready-fatal"))assert(status.recovered==5 && !burst.batches && burst.singles==6);
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
        assert(!burst.batches && !status.repair_batches); /* Resume does not restart burst probing. */
    }
    assert(result==KUI_SALVAGE_RESOLVED && status.complete && status.targets==burst.targets && status.recovered==burst.targets && !status.remaining);
    test.capturing=false;compare_complete(job);
}
