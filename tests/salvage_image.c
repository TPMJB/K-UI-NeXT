/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/salvage.h"
#include "kui/media.h"
#include "kui/recovery_guard.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static struct kui_capture_plan plan={.count=3,.bytes=84u*KUI_RAW_BYTES,.tracks={
    {1,4,0,150,190,340},{2,0,0,340,344,344},{3,4,1,45150,45190,45190}}};
static uint8_t *original[3];
static struct {
    FILE *image;uint64_t blocks,now;const char *test,*fault;
    bool capturing,injected,repairing,healthy,cancelled,automatic,did_cancel;unsigned audio_calls,reads,writes,anchor_failures,recoveries;
    bool identity_fault,identifying;unsigned discovery_reads,anchor_fault_reads;
    unsigned last_event,pass,first_attempts[2],last_attempts,stop_after,subdivided_good,single_healthy;bool patch_started;uint32_t repair_order[256];unsigned repair_count;
    unsigned leaf_attempt_mask[2],anchor_attempt_mask,audio_confirm_mask;
    bool saw_split,saw_deferred,saw_retained_problem,saw_audio_deferred,saw_restored_read,awaiting_restored_read;
    bool collection_seen,collection_frozen,saw_collection_stall,saw_resumed_collection;
    uint64_t collection_base,collection_bytes,collection_ms;
    unsigned rate_runs,rate_reads;
    bool fatal_resume_injected,resuming_after_fatal;
    unsigned fatal_read_at,resumed_collection_reads;
    uint32_t collection_fad[512];unsigned collection_count[512],collection_calls,collection_recoveries;
    unsigned collection_anchor_reads;
    bool defer_seen,defer_stop_before,defer_stop_after,defer_block_repairs,defer_block_all_repairs;
    unsigned defer_anchor_result,defer_bad_collection_reads;
    uint32_t defer_start,defer_count;uint64_t defer_initial_done;
    struct kui_salvage_status published,interrupted,last_callback;
} test;
static struct kui_salvage_status *active_status;
static bool legacy_index_case(void);
static void legacy_index_progress(const struct kui_salvage_status *status);
static void legacy_index_media_read(size_t count);
static FRESULT legacy_index_stat(const TCHAR *path,FILINFO *info);
static FRESULT legacy_index_opendir(DIR *dir,const TCHAR *path);
static FRESULT legacy_index_readdir(DIR *dir,FILINFO *info);
static FRESULT legacy_index_closedir(DIR *dir);
static bool legacy_index_sync_error(void);
static bool legacy_index_write_error(const void *data,UINT bytes);
static void run_legacy_index_fixture(const char *job,struct kui_capture_ops *ops,struct kui_salvage_options *options);

static bool partial_case(void);
static bool partial_optical(unsigned ti,uint32_t fad,unsigned count,uint8_t *data,enum kui_read_result *result);
static bool partial_recover(bool *result);
static void partial_progress(const struct kui_salvage_status *status);
static bool burst_case(void);
static void burst_plan(void);
static bool burst_optical(unsigned ti,uint32_t fad,unsigned count,uint8_t *data,enum kui_read_result *result);
static void burst_progress(const struct kui_salvage_status *status);
static void burst_event(unsigned type,const uint8_t *event);
static bool saved_check_media_read(size_t count);
static bool trial_case(void);
static void trial_progress(const struct kui_salvage_status *status);
static bool trial_optical(unsigned ti,uint32_t fad,unsigned count,uint8_t *data,enum kui_read_result *result);
static bool saved_check_read_error(FIL *file,void *data,UINT bytes,UINT *got,FRESULT *result);
static void saved_check_read_result(FIL *file,void *data,UINT bytes,UINT *got,FRESULT result);
static enum kui_salvage_result run_salvage(const struct kui_capture_plan *p,const struct kui_capture_ops *ops,
    const struct kui_salvage_options *options,enum kui_salvage_action action,struct kui_salvage_status *status) {
    unsigned before=test.reads;
    test.collection_seen=false;test.collection_frozen=false;test.collection_bytes=test.collection_ms=0;
    if(!strcmp(test.test,"status-collection-rate"))++test.rate_runs;
    active_status=status;enum kui_salvage_result r=kui_salvage_run(p,ops,options,action,status);active_status=NULL;
    assert(status->attempts==test.reads-before && !status->collection_active && status->collected_this_run<=status->done);
    if(!partial_case())assert(!test.collection_recoveries);
    if(!test.collection_seen)assert(!status->collected_this_run && !status->collection_elapsed_ms);
    return r;
}
#define LARGE_TARGETS 8208u
static bool cd_case(void) {return !strncmp(test.test,"cd-",3);}
static bool large_case(void) {return !strcmp(test.test,"large-map");}
static bool fast_case(void) {return !strcmp(test.test,"fast-dense") || !strcmp(test.test,"fast-invalid");}
static bool defer_case(void) {return !strncmp(test.test,"defer-",6);}
static unsigned expected_targets(void) {
    if(!strcmp(test.test,"bad-parity"))return 5;
    if(!strcmp(test.test,"auto-legacy-resume"))return 4;
    return 36;
}
#ifdef KUI_TEST_ALLOCATIONS
static size_t largest_worker_allocation,live_worker_bytes,peak_worker_bytes;
static unsigned worker_allocations;
static struct {void *pointer;size_t bytes;} worker_owned[32];
void *__real_malloc(size_t);
void *__real_calloc(size_t,size_t);
void __real_free(void *);
static void worker_allocation(void *pointer,size_t bytes) {
    if(!test.capturing || !pointer)return;
    assert(bytes<=256u*1024u);++worker_allocations;
    if(bytes>largest_worker_allocation)largest_worker_allocation=bytes;
    unsigned i=0;while(i<32 && worker_owned[i].pointer)++i;assert(i<32);
    worker_owned[i].pointer=pointer;worker_owned[i].bytes=bytes;live_worker_bytes+=bytes;
    assert(live_worker_bytes<=256u*1024u);
    if(live_worker_bytes>peak_worker_bytes)peak_worker_bytes=live_worker_bytes;
}
void *__wrap_malloc(size_t bytes) {void *p=__real_malloc(bytes);worker_allocation(p,bytes);return p;}
void *__wrap_calloc(size_t count,size_t bytes) {assert(!bytes || count<=SIZE_MAX/bytes);void *p=__real_calloc(count,bytes);worker_allocation(p,count*bytes);return p;}
void __wrap_free(void *pointer) {
    for(unsigned i=0;i<32;++i)if(worker_owned[i].pointer==pointer && pointer) {assert(live_worker_bytes>=worker_owned[i].bytes);live_worker_bytes-=worker_owned[i].bytes;worker_owned[i].pointer=NULL;break;}
    __real_free(pointer);
}
#endif
static FATFS fs;
static const char *export_dir;
static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));}
static void log_line(const char *fmt,...) {va_list args;va_start(args,fmt);vprintf(fmt,args);va_end(args);puts("");}
static bool fault(const char *name) {return test.capturing && test.fault && !strcmp(test.fault,name) && !test.injected;}
static uint64_t blocks(void *ctx) {(void)ctx;return test.blocks;}
static int image_read(void *ctx,uint32_t block,size_t count,uint8_t *data) {
    (void)ctx;legacy_index_media_read(count);if(burst_case() && saved_check_media_read(count))return -1;
    return fseeko(test.image,(off_t)block*512,SEEK_SET) || fread(data,512,count,test.image)!=count?-1:0;
}
static int image_write(void *ctx,uint32_t block,size_t count,const uint8_t *data) {
    (void)ctx;++test.writes;return fseeko(test.image,(off_t)block*512,SEEK_SET) || fwrite(data,512,count,test.image)!=count?-1:0;
}
static int image_sync(void *ctx) {(void)ctx;return fflush(test.image) || fsync(fileno(test.image))?-1:0;}
static const struct kui_media_ops media={NULL,blocks,image_read,image_write,image_sync};
FRESULT __real_f_stat(const TCHAR *,FILINFO *);
FRESULT __wrap_f_stat(const TCHAR *path,FILINFO *info) {return legacy_index_stat(path,info);}
FRESULT __real_f_opendir(DIR *,const TCHAR *);
FRESULT __wrap_f_opendir(DIR *dir,const TCHAR *path) {return legacy_index_opendir(dir,path);}
FRESULT __real_f_readdir(DIR *,FILINFO *);
FRESULT __wrap_f_readdir(DIR *dir,FILINFO *info) {return legacy_index_readdir(dir,info);}
FRESULT __real_f_closedir(DIR *);
FRESULT __wrap_f_closedir(DIR *dir) {return legacy_index_closedir(dir);}
FRESULT __real_f_write(FIL *,const void *,UINT,UINT *);
FRESULT __wrap_f_write(FIL *file,const void *data,UINT bytes,UINT *written) {
    if(legacy_index_write_error(data,bytes)) {*written=0;return FR_DISK_ERR;}
    const uint8_t *p=data;
    if(fault("defer-index") && bytes==32 && test.last_event==2 && !memcmp(p,"KUITGT2",7)) {test.injected=true;*written=0;return FR_DISK_ERR;}
    if(test.capturing && bytes==512 && !memcmp(p,"KUISJNL1",8)) {
        unsigned type=get32(p+16);test.last_event=type;
        if(burst_case())burst_event(type,p);
        if(fault("event-buffer-mutation") && type==1) {test.injected=true;((uint8_t *)data)[100]^=1;}
        const char *kind=type==1?"queue-write":type==2?"commit-write":type==3?"baseline-write":type==4?"ready-write":type==5?"repaired-write":type==7?"backup-publish":"complete-write";
        if(fault(kind) || (fault("queue-error-cancel") && type==1)) {test.injected=true;if(!strcmp(test.fault,"queue-error-cancel"))test.cancelled=true;*written=0;return FR_DISK_ERR;}
        if(fault("torn-commit") && type==2) {test.injected=true;FRESULT r=__real_f_write(file,data,81,written);assert(r==FR_OK);return FR_DISK_ERR;}
        if(fault("torn-repaired") && type==5) {test.injected=true;FRESULT r=__real_f_write(file,data,81,written);assert(r==FR_OK);return FR_DISK_ERR;}
        if(fault("torn-candidate") && type==7) {test.injected=true;FRESULT r=__real_f_write(file,data,81,written);assert(r==FR_OK);return FR_DISK_ERR;}
    }
    if(test.capturing && (bytes==8192 || bytes==5120) && (!memcmp(p,"KUISPAT1",8) || !memcmp(p,"KUISPAT2",8))) {
        test.patch_started=true;
        if(fault("backup-write")) {test.injected=true;*written=0;return FR_DISK_ERR;}
    }
    if((fault("target-buffer-mutation") && test.repairing && bytes==KUI_RAW_BYTES && test.patch_started) ||
       (fault("source-buffer-mutation") && !test.repairing && bytes==32u*KUI_RAW_BYTES)) {test.injected=true;((uint8_t *)data)[17]^=1;}
    if(fault("patch-write") && test.repairing && bytes==KUI_RAW_BYTES && test.patch_started) {
        test.injected=true;FRESULT r=__real_f_write(file,data,317,written);assert(r==FR_OK);return FR_DISK_ERR;
    }
    return __real_f_write(file,data,bytes,written);
}
FRESULT __real_f_sync(FIL *);
FRESULT __wrap_f_sync(FIL *file) {
    if(legacy_index_sync_error())return FR_DISK_ERR;
    if((fault("queue-sync") && test.last_event==1) || (fault("baseline-sync") && test.last_event==3) ||
       (fault("backup-sync") && test.patch_started) || (fault("complete-sync") && test.last_event==6) || (fault("candidate-sync") && test.last_event==7)) {
        test.injected=true;return FR_DISK_ERR;
    }
    return __real_f_sync(file);
}
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *file,void *data,UINT bytes,UINT *got) {
    FRESULT injected;if(burst_case() && saved_check_read_error(file,data,bytes,got,&injected))return injected;
    if(fault("readback-fail") && test.repairing && test.patch_started && bytes==KUI_RAW_BYTES) {test.injected=true;*got=0;return FR_DISK_ERR;}
    FRESULT result=__real_f_read(file,data,bytes,got);
    if(burst_case())saved_check_read_result(file,data,bytes,got,result);
    if(fault("header-readback") && bytes==4096 && result==FR_OK) {test.injected=true;((uint8_t *)data)[42]^=1;}
    if(fault("journal-readback") && bytes==512 && result==FR_OK) {test.injected=true;((uint8_t *)data)[100]^=1;}
    if(fault("candidate-readback") && test.last_event==7 && bytes==512 && result==FR_OK) {test.injected=true;((uint8_t *)data)[100]^=1;}
    if(fault("backup-substitute") && (bytes==8192 || bytes==5120) && result==FR_OK && get32((uint8_t *)data+12)==1) {
        uint8_t *p=data;test.injected=true;p[64+KUI_RAW_BYTES+3]^=1;
        put32(p+28,kui_crc32(0,p+64+KUI_RAW_BYTES,KUI_RAW_BYTES));put32(p+bytes-4,kui_crc32(0,p,bytes-4));
    }
    if(fault("readback-mismatch") && test.repairing && test.patch_started && bytes==KUI_RAW_BYTES && result==FR_OK && *got==bytes) {test.injected=true;((uint8_t *)data)[3]^=1;}
    return result;
}
FRESULT __real_f_rename(const TCHAR *,const TCHAR *);
FRESULT __wrap_f_rename(const TCHAR *old,const TCHAR *next) {
    if((fault("backup-publish") && strstr(next,"patch-")) || (fault("final-publish") && strstr(next,"salvage.json"))) {test.injected=true;return FR_DISK_ERR;}
    return __real_f_rename(old,next);
}
static bool cancelled(void *ctx) {(void)ctx;return test.cancelled;}
static uint64_t now_ms(void *ctx) {(void)ctx;if(!strcmp(test.test,"status-collection-rate"))return test.now;test.now+=!strcmp(test.test,"auto-read-stop")?100:1;return test.now;}
static void progress(void *ctx,const struct kui_salvage_status *status) {
    (void)ctx;assert(status->done<=status->total && status->recovered+status->remaining==status->targets);
    if(status->collection_active) {
        assert(!test.collection_frozen && status->collected_this_run<=status->done);
        if(!test.collection_seen) {
            test.collection_base=status->done-status->collected_this_run;test.collection_seen=true;
            assert(!status->collected_this_run);
            if(!strcmp(test.test,"status-collection-rate")) {
                assert(!status->collection_elapsed_ms);
                if(test.rate_runs==2) {assert(test.collection_base==32u*KUI_RAW_BYTES);test.saw_resumed_collection=true;}
            }
        }
        assert(status->collected_this_run==status->done-test.collection_base && status->collected_this_run>=test.collection_bytes && status->collection_elapsed_ms>=test.collection_ms);
        test.collection_bytes=status->collected_this_run;test.collection_ms=status->collection_elapsed_ms;
        if(!status->collected_this_run && status->collection_elapsed_ms>=5000)test.saw_collection_stall=true;
    } else if(test.collection_seen) {
        if(test.collection_frozen)assert(status->collected_this_run==test.collection_bytes && status->collection_elapsed_ms==test.collection_ms);
        test.collection_bytes=status->collected_this_run;test.collection_ms=status->collection_elapsed_ms;test.collection_frozen=true;
    }
    assert(status->read_state<=KUI_SALVAGE_READ_FATAL && status->last_problem_state<=KUI_SALVAGE_READ_FATAL);
    if(status->read_count) {
        if(status->read_state==KUI_SALVAGE_READ_DEFERRED && !status->read_limit) {
            assert(!status->read_attempt && !status->read_confirm && !status->read_confirm_limit);
            if(status->collection_active && defer_case()) {
                assert(status->read_fad==test.defer_start && status->read_count==test.defer_count);test.defer_seen=true;
                if(test.defer_stop_before && !test.did_cancel) {test.cancelled=true;test.did_cancel=true;}
            }
        } else assert(status->read_attempt && status->read_attempt<=status->read_limit && status->read_confirm && status->read_confirm<=status->read_confirm_limit);
    }
    if(status->last_problem_state!=KUI_SALVAGE_READ_NONE) {
        assert(status->last_problem_count && status->last_problem_attempt<=status->last_problem_limit);
        if(!status->last_problem_limit)assert(status->last_problem_state==KUI_SALVAGE_READ_DEFERRED && !status->last_problem_attempt && !status->last_problem_confirm && !status->last_problem_confirm_limit);
        if(status->last_problem_state==KUI_SALVAGE_READ_SPLIT) {assert(status->last_problem_count>1 && status->last_problem_attempt==1 && status->last_problem_limit==1);test.saw_split=true;}
        if(status->last_problem_state==KUI_SALVAGE_READ_DEFERRED) {
            assert(status->last_problem_attempt==status->last_problem_limit);test.saw_deferred=true;
            if(status->last_problem_confirm==2 && status->last_problem_confirm_limit==2)test.saw_audio_deferred=true;
        }
        if(status->read_state==KUI_SALVAGE_READ_ACCEPTED && status->read_fad!=status->last_problem_fad)test.saw_retained_problem=true;
    }
    if(test.awaiting_restored_read && status->read_state!=KUI_SALVAGE_READ_READING && status->read_fad==test.interrupted.read_fad) {
        assert(status->track==test.interrupted.track && status->read_count==test.interrupted.read_count &&
            status->read_attempt==test.interrupted.read_attempt && status->read_limit==test.interrupted.read_limit &&
            status->read_confirm==test.interrupted.read_confirm && status->read_confirm_limit==test.interrupted.read_confirm_limit);
        test.saw_restored_read=true;test.awaiting_restored_read=false;
    }
    test.published=*status;
    if(partial_case())partial_progress(status);
    if(burst_case())burst_progress(status);
    if(trial_case())trial_progress(status);
    test.identifying=!status->job[0];
    test.pass=status->pass;if(test.automatic)test.repairing=status->pass!=0;
    bool first_stop=!strcmp(test.test,"cancel") || !strcmp(test.test,"auto-resume") || !strcmp(test.test,"auto-legacy-resume") || !strcmp(test.test,"status-collection-rate");
    if(!test.did_cancel && first_stop && !test.repairing && status->done>=32u*KUI_RAW_BYTES) {test.cancelled=true;test.did_cancel=true;}
    if(!test.did_cancel && !strcmp(test.test,"auto-until-stop") && status->pass==3) {test.cancelled=true;test.did_cancel=true;}
    if(!test.did_cancel && !strcmp(test.test,"auto-read-stop") && status->pass==1 && status->attempts>test.last_attempts) {test.cancelled=true;test.did_cancel=true;}
    if(!test.did_cancel && !strcmp(test.test,"identity-stop") && !strncmp(status->message,"Finding healthy",15)) {test.cancelled=true;test.did_cancel=true;}
    if(!test.did_cancel && !strcmp(test.test,"finish-stop") && !strncmp(status->message,"Finalizing",10)) {test.cancelled=true;test.did_cancel=true;}
    if(test.stop_after && status->recovered>=test.stop_after) {test.cancelled=true;test.did_cancel=true;}
    if(fast_case() && !test.did_cancel && status->first_pass_complete && !status->pass) {test.cancelled=true;test.did_cancel=true;}
    if(test.defer_stop_after && !test.did_cancel && status->collection_active && status->done>=test.defer_initial_done+(uint64_t)test.defer_count*KUI_RAW_BYTES) {test.cancelled=true;test.did_cancel=true;}
    test.last_attempts=status->attempts;
}
static enum kui_read_result optical(void *ctx,uint32_t fad,unsigned count,uint8_t *data) {
    (void)ctx;++test.reads;if(test.identifying)++test.discovery_reads;
    unsigned ti=3;for(unsigned i=0;i<3;++i)if(fad>=plan.tracks[i].start && fad<plan.tracks[i].end)ti=i;
    assert(ti<3 && count<=plan.tracks[ti].end-fad);
    if(active_status) {
        const struct kui_salvage_status *s=active_status;
        assert(s->read_state==KUI_SALVAGE_READ_READING && s->read_fad==fad && s->read_count==count && s->track==ti+1);
        /* Even an immediate/slow blocking call must have published its actual
         * range, logical attempt and audio confirmation before it starts. */
        assert(test.published.read_fad==fad && test.published.read_count==count &&
            test.published.read_attempt==s->read_attempt && test.published.read_limit==s->read_limit &&
            test.published.read_confirm==s->read_confirm && test.published.read_confirm_limit==s->read_confirm_limit);
        if(!s->pass && count==1 && s->read_limit==2 && (fad==152 || fad==342))test.leaf_attempt_mask[fad==342]|=1u<<(s->read_attempt-1);
        if(test.identity_fault && fad==45150 && s->read_limit==11)test.anchor_attempt_mask|=1u<<(s->read_attempt-1);
        if(s->read_confirm_limit==2) {
            assert(count==1);test.audio_confirm_mask|=1u<<(s->read_confirm-1);
            if(s->read_confirm==2)assert(test.last_callback.read_fad==fad && test.last_callback.read_attempt==s->read_attempt &&
                test.last_callback.read_limit==s->read_limit && test.last_callback.read_confirm==1 && test.last_callback.read_confirm_limit==2);
        }
        test.last_callback=*s;
        if(s->collection_active && count>1) {
            assert(s->read_attempt==1 && s->read_limit==1 && s->read_confirm==1 && s->read_confirm_limit==1);
            assert(test.collection_calls<512);test.collection_fad[test.collection_calls]=fad;
            test.collection_count[test.collection_calls++]=count;
        }
        if(s->collection_active && count==1) {
            assert(!strcmp(s->message,"Collection health check; verifying saved disc anchors") ||
                !strcmp(s->message,"Safely reinitializing drive; checking saved disc identity"));
            ++test.collection_anchor_reads;
        }
        if(test.resuming_after_fatal && s->collection_active) {
            assert(fad==45182 && count==8);++test.resumed_collection_reads;
        }
        if(defer_case()) {
            if(!s->collection_active && !s->pass && fad==45150 && count==1 && test.defer_anchor_result)return test.defer_anchor_result==1?KUI_READ_RETRY:KUI_READ_FATAL;
            bool overlap=fad<test.defer_start+test.defer_count && fad+count>test.defer_start;
            if(s->collection_active && overlap) {++test.defer_bad_collection_reads;return KUI_READ_FATAL;}
            if(s->pass && (test.defer_block_all_repairs || (test.defer_block_repairs && overlap)))return KUI_READ_RETRY;
        }
    }
    enum kui_read_result partial_result;
    if(trial_case() && trial_optical(ti,fad,count,data,&partial_result))return partial_result;
    if(burst_case() && burst_optical(ti,fad,count,data,&partial_result))return partial_result;
    if(partial_case() && partial_optical(ti,fad,count,data,&partial_result))return partial_result;
    if(!strcmp(test.test,"fatal-committed-resume") && !test.fatal_resume_injected && fad==45182) {
        /* A failed command may have modified its entire RAM destination. No
         * byte from this uncommitted region may reach the saved track. */
        memset(data,0xcc,(size_t)count*KUI_RAW_BYTES);test.fatal_resume_injected=true;
        test.fatal_read_at=test.reads;return KUI_READ_FATAL;
    }
    if(!strcmp(test.test,"status-collection-rate")) {
        if(active_status && active_status->collection_active) {
            test.now+=1000;
            if(!test.rate_reads++) {test.now+=4000;return KUI_READ_RETRY;}
        } else test.now+=60000; /* Identity/replay time must not dilute collection rate. */
    }
    if(!strcmp(test.test,"drive-buffer") && !test.recoveries && fad+count>182 && fad<190)return KUI_READ_RETRY;
    if(!strncmp(test.test,"anchor-discovery-",17)) {
        if(!strcmp(test.test,"anchor-discovery-fatal"))return KUI_READ_FATAL;
        if(!strcmp(test.test,"anchor-discovery-stop") && test.reads==3)test.cancelled=true;
        return KUI_READ_RETRY;
    }
    if(test.identity_fault && fad==45150 && count==1) {
        ++test.anchor_fault_reads;
        if(!strcmp(test.test,"anchor-resume-fatal"))return KUI_READ_FATAL;
        if(!strcmp(test.test,"anchor-resume-exhausted") || test.anchor_failures++<3)return KUI_READ_RETRY;
    }
    if(!strcmp(test.test,"drive-fatal") && test.pass==1 && fad==152)return KUI_READ_FATAL;
    if(!strcmp(test.test,"cd-audio-anchor-mismatch") && count==1) {memcpy(data,original[ti]+(fad-plan.tracks[ti].start)*KUI_RAW_BYTES,count*KUI_RAW_BYTES);data[17]^=(uint8_t)(test.reads&1);return KUI_READ_OK;}
    bool damaged=large_case()?ti==1:(fad<=152 && fad+count>152) || (fad<=342 && fad+count>342);
    if(!strcmp(test.test,"status-deferred-recovery"))damaged=active_status && active_status->collection_active && fad<=152 && fad+count>152;
    if(fast_case())damaged=false;
    if(!strcmp(test.test,"anchor-interior"))damaged=ti==2 && (fad<45166u || fad+count>45174u);
    if(!test.repairing && !damaged && count>1 && count<32)++test.subdivided_good;
    if(!test.repairing && !damaged && count==1 && fad<45150)++test.single_healthy;
    if(!large_case() && test.pass==0 && count==1 && (fad==152 || fad==342))++test.first_attempts[fad==342];
    if(test.automatic && test.repairing && damaged) {
        if(count==1) {assert(test.repair_count<256);test.repair_order[test.repair_count++]=fad;}
        unsigned resolves=!strcmp(test.test,"auto-late")?5:51;
        if(!test.healthy && ((!strcmp(test.test,"auto-late") || !strcmp(test.test,"auto-until-late"))?test.pass<resolves:
           (!strcmp(test.test,"auto-exhausted") || !strcmp(test.test,"auto-until-stop"))))return KUI_READ_RETRY;
    }
    if(!strcmp(test.test,"legacy-repair") && active_status && active_status->pass && !test.repairing)return KUI_READ_RETRY;
    if(!strcmp(test.test,"fast-dense") && active_status && active_status->collection_active && count>1 && ti==2 && fad<45150+9*32) {
        memset(data,0xcc,(size_t)count*KUI_RAW_BYTES);return KUI_READ_RETRY;
    }
    if(damaged && !test.healthy && !test.repairing) {
        if(!strcmp(test.test,"fatal"))return KUI_READ_FATAL;
        if(!strcmp(test.test,"bad-parity")) {
            memcpy(data,original[ti]+(fad-plan.tracks[ti].start)*KUI_RAW_BYTES,count*KUI_RAW_BYTES);
            if(ti==0)data[(152-fad)*KUI_RAW_BYTES+2200]^=1;else return KUI_READ_RETRY;
            return KUI_READ_OK;
        }
        return KUI_READ_RETRY;
    }
    memcpy(data,original[ti]+(fad-plan.tracks[ti].start)*KUI_RAW_BYTES,count*KUI_RAW_BYTES);
    if(!strcmp(test.test,"fast-invalid") && active_status && active_status->collection_active && ti==0)
        for(unsigned i=0;i<count;++i)if(fad+i==152 || fad+i==157)data[i*KUI_RAW_BYTES+2200]^=1;
    if(test.repairing && !strcmp(test.test,"five-passes") && (fad==152 || fad==342)) {assert(test.repair_count<256);test.repair_order[test.repair_count++]=fad;}
    if(test.repairing && fad==342 && (!strcmp(test.test,"audio-mismatch") || !strcmp(test.test,"five-passes"))) {data[3]^=(uint8_t)(++test.audio_calls&1);}
    if(test.repairing && fad==152 && !strcmp(test.test,"wrong-fad")) {memcpy(data,original[0],KUI_RAW_BYTES);}
    if(test.repairing && fad==152 && (!strcmp(test.test,"bad-ecc") || !strcmp(test.test,"five-passes")))data[2200]^=1;
    if(test.repairing && fad==152 && !strcmp(test.test,"repair-fatal"))return KUI_READ_FATAL;
    if(test.repairing && fad==152 && !strcmp(test.test,"repair-stop")) {test.cancelled=true;return KUI_READ_RETRY;}
    return KUI_READ_OK;
}
static bool recover(void *ctx,const struct kui_capture_plan *expected) {
    (void)ctx;assert(expected==&plan);++test.recoveries;
    if(active_status && active_status->collection_active)++test.collection_recoveries;
    if(active_status && !partial_case()) {test.interrupted=*active_status;test.awaiting_restored_read=true;}
    bool partial_result;
    if(partial_case() && partial_recover(&partial_result))return partial_result;
    if(!strcmp(test.test,"drive-stop")) {test.cancelled=true;return false;}
    if(!strcmp(test.test,"drive-fail"))return false;
    if(!strcmp(test.test,"drive-media"))original[2][42]^=1;
    if(!strcmp(test.test,"drive-buffer") && test.recoveries==1)assert(test.pass==1);
    test.repairing=true;return true;
}
static void remount(void) {assert(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(&media);assert(kui_mount(&fs,log_line));}
static void write_file(const char *path,const void *data,UINT bytes) {
    FIL file;UINT put;assert(f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    assert(f_write(&file,data,bytes,&put)==FR_OK && put==bytes && f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}
static size_t load(const char *path,void *data,size_t cap) {
    FIL file;UINT got;assert(f_open(&file,path,FA_READ)==FR_OK && f_size(&file)<=cap);
    size_t size=f_size(&file);assert(f_read(&file,data,(UINT)size,&got)==FR_OK && got==size && f_close(&file)==FR_OK);return size;
}
static void compare_unresolved(const char *job) {
    /* A bad early region must not discard the rest of its track or later
     * tracks. Only the durably recorded two missing sectors may be zero. */
    remount();uint8_t data[131072],expected[40*KUI_RAW_BYTES];char path[512];
    for(unsigned i=0;i<3;++i) {
        unsigned size=(plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES;
        memcpy(expected,original[i],size);
        if(i<2) {
            if(!strcmp(test.test,"auto-read-stop") || !strcmp(test.test,"legacy-repair"))memset(expected,0,(i?4u:32u)*KUI_RAW_BYTES);
            else memset(expected+2*KUI_RAW_BYTES,0,KUI_RAW_BYTES);
        }
        snprintf(path,sizeof(path),"0:%s/track%02u.%s",job,i+1,plan.tracks[i].control==4?"bin":"raw");
        assert(load(path,data,sizeof(data))==size && !memcmp(data,expected,size));
    }
    FILINFO info;snprintf(path,sizeof(path),"0:%s/salvage.json",job);assert(f_stat(path,&info)==FR_NO_FILE);
    snprintf(path,sizeof(path),"0:%s/salvage.gdi",job);assert(f_stat(path,&info)==FR_NO_FILE);
    assert(f_mount(NULL,"0:",0)==FR_OK);
}
#include "salvage_legacy_narrow.inc"
static void legacy_header(const char *job,bool holes) {
    /* Recreate an old OFF job stopped after healthy committed data.
     * Identity binds the exact immutable header into every journal record. */
    remount();uint8_t header[4096],digest[32];char path[512];
    snprintf(path,sizeof(path),"0:%s/header.bin",job);assert(load(path,header,sizeof(header))==sizeof(header));
    memcpy(header,"KUISALV1",8);put32(header+8,1);put32(header+20,holes?1:0);put32(header+4092,kui_crc32(0,header,4092));
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,header,sizeof(header));kui_sha256_digest(&hash,digest);
    write_file(path,header,sizeof(header));
    snprintf(path,sizeof(path),"0:%s/journal.bin",job);FIL journal;UINT got,put;uint8_t event[512];
    assert(f_open(&journal,path,FA_READ|FA_WRITE)==FR_OK && f_size(&journal) && !(f_size(&journal)%512));
    FSIZE_t size=f_size(&journal);
    for(FSIZE_t at=0;at<size;at+=512) {
        assert(f_lseek(&journal,at)==FR_OK && f_read(&journal,event,sizeof(event),&got)==FR_OK && got==sizeof(event));
        unsigned type=get32(event+16);assert(type==2 || type==3 || (holes && (type==1 || type==4)));
        memcpy(event+40,digest,32);put32(event+508,kui_crc32(0,event,508));
        assert(f_lseek(&journal,at)==FR_OK && f_write(&journal,event,sizeof(event),&put)==FR_OK && put==sizeof(event));
    }
    assert(f_sync(&journal)==FR_OK && f_close(&journal)==FR_OK && f_mount(NULL,"0:",0)==FR_OK);
}
static void check_legacy_header(const char *job) {
    remount();uint8_t header[4096];char path[512];snprintf(path,sizeof(path),"0:%s/header.bin",job);
    assert(load(path,header,sizeof(header))==sizeof(header) && !memcmp(header,"KUISALV1",8) && get32(header+8)==1 && get32(header+20)==0);
    assert(f_mount(NULL,"0:",0)==FR_OK);
}
static void compare_complete(const char *job) {
    remount();size_t cap=131072;for(unsigned i=0;i<3;++i) {size_t n=(size_t)(plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES;if(n>cap)cap=n;}uint8_t *data=malloc(cap);assert(data);char path[512];
    for(unsigned i=0;i<3;++i) {
        snprintf(path,sizeof(path),"0:%s/track%02u.%s",job,i+1,plan.tracks[i].control==4?"bin":"raw");
        unsigned size=(plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES;
        assert(load(path,data,cap)==size && !memcmp(data,original[i],size));
    }
    snprintf(path,sizeof(path),"0:%s/salvage.json",job);size_t n=load(path,data,cap-1);data[n]=0;
    for(unsigned i=0;i<3;++i) {char crc[16];snprintf(crc,sizeof(crc),"%08x",kui_crc32(0,original[i],(plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES));assert(strstr((char *)data,crc));}
    if(export_dir) {
        DIR dir;FILINFO info;snprintf(path,sizeof(path),"0:%s",job);assert(f_opendir(&dir,path)==FR_OK);
        for(;;) {
            assert(f_readdir(&dir,&info)==FR_OK);if(!info.fname[0])break;
            if(info.fattrib&AM_DIR)continue;
            assert(strlen(info.fname)<64);snprintf(path,sizeof(path),"0:%s/%.63s",job,info.fname);FIL input;assert(f_open(&input,path,FA_READ)==FR_OK);
            char host[1024];snprintf(host,sizeof(host),"%s/%s",export_dir,info.fname);FILE *out=fopen(host,"wb");assert(out);
            uint64_t left=f_size(&input);while(left) {UINT bytes=left>cap?(UINT)cap:(UINT)left,got=0;assert(f_read(&input,data,bytes,&got)==FR_OK && got==bytes);assert(fwrite(data,1,bytes,out)==bytes);left-=bytes;}
            assert(f_close(&input)==FR_OK && !fclose(out));
        }
        assert(f_closedir(&dir)==FR_OK);
    }
    free(data);assert(f_mount(NULL,"0:",0)==FR_OK);
}
#include "salvage_defer_fixture.inc"
#include "salvage_partial_fixture.inc"
#include "salvage_burst_fixture.inc"
#include "salvage_legacy_index_fixture.inc"
#include "salvage_trial_fixture.inc"
int main(int argc,char **argv) {
    if(argc!=4 && argc!=5)return 2;
    if(argc==5)export_dir=argv[4];
    test.test=argv[3];
    if(partial_case())partial_plan();
    if(burst_case())burst_plan();
    if(defer_case() && strcmp(test.test,"defer-tail")) {plan.tracks[2].end=plan.tracks[2].toc_end=45150+104;plan.bytes=(44u+104u)*KUI_RAW_BYTES;}
    if(!strcmp(test.test,"fast-dense")) {plan.tracks[2].end=plan.tracks[2].toc_end=45150+320;plan.bytes=(44u+320u)*KUI_RAW_BYTES;}
    if(cd_case()) {plan.profile=KUI_CAPTURE_PROFILE_CD_TOC;plan.tracks[0].toc_end=190;
        plan.tracks[1].start=190;plan.tracks[1].end=plan.tracks[1].toc_end=194;
        plan.tracks[2].start=194;plan.tracks[2].end=plan.tracks[2].toc_end=234;plan.tracks[2].session=0;
        if(!strncmp(test.test,"cd-audio",8))plan.tracks[0].control=plan.tracks[2].control=0;
        if(!strcmp(test.test,"cd-offset"))for(unsigned i=0;i<3;++i) {plan.tracks[i].start+=75;plan.tracks[i].end+=75;plan.tracks[i].toc_end+=75;}
    }
    if(large_case()) {plan.tracks[1].end=plan.tracks[1].toc_end=340+LARGE_TARGETS;plan.bytes=(80u+LARGE_TARGETS)*KUI_RAW_BYTES;}struct stat info;assert(!lstat(argv[1],&info) && S_ISREG(info.st_mode) && !(info.st_size%512));
    test.image=fopen(argv[1],"r+b");assert(test.image);test.blocks=(uint64_t)info.st_size/512;
    for(unsigned i=0;i<3;++i) {char path[1024];snprintf(path,sizeof(path),"%s/track%02u.%s",argv[2],i+1,plan.tracks[i].control==4?"bin":"raw");FILE *in=fopen(path,"rb");assert(in);unsigned size=(plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES;original[i]=malloc(size);assert(original[i]);assert(fread(original[i],1,size,in)==size && !fclose(in));}
    kui_media_set(&media);
    struct kui_capture_ops ops={.read=optical,.cancelled=cancelled,.now_ms=now_ms,.log=log_line};
    if(!strncmp(test.test,"drive-",6))ops.recover=recover;
    if(fast_case())ops.recover=recover;
    /* Publish every outcome in the restoration regression; ordinary cases
     * retain a clock advancing only 1ms to catch stale pre-read snapshots. */
    if(!strcmp(test.test,"drive-buffer"))ops.now_ms=NULL;
    if(!strcmp(test.test,"fatal-committed-resume")) {
        ops.recover=recover;
        remount();assert(f_mkdir("0:/KUI")==FR_OK && f_mkdir("0:/KUI/salvage")==FR_OK && f_mkdir("0:/KUI/salvage/job-0001")==FR_OK);
        assert(f_mount(NULL,"0:",0)==FR_OK);
    }
    const struct kui_capture_options discovery_retry_preference={.retry_limit=50};
    if(!strcmp(test.test,"anchor-discovery-budget"))ops.options=&discovery_retry_preference;
    struct kui_salvage_options options={.zero_fill=strcmp(test.test,"zero-off")!=0,.passes=1,.progress=progress};
    test.automatic=!strncmp(test.test,"auto-",5);
    if(!strncmp(test.test,"drive-",6) || !strcmp(test.test,"five-passes") || !strcmp(test.test,"auto-late") || !strcmp(test.test,"auto-exhausted"))options.passes=5;
    if(!strncmp(test.test,"auto-until-",11) || !strcmp(test.test,"auto-read-stop"))options.passes=0;
    if(!strcmp(test.test,"invalid-limit"))options.passes=51;
    struct kui_salvage_status status;
    bool first_fault=!strncmp(test.test,"queue-",6) || !strcmp(test.test,"commit-write") || !strncmp(test.test,"baseline-",9) || !strcmp(test.test,"ready-write") || !strcmp(test.test,"torn-commit") || !strcmp(test.test,"header-readback") || !strcmp(test.test,"journal-readback") || !strcmp(test.test,"source-buffer-mutation") || !strcmp(test.test,"event-buffer-mutation");
    bool repair_fault=!strncmp(test.test,"backup-",7) || !strncmp(test.test,"patch-",6) || !strncmp(test.test,"readback-",9) || !strcmp(test.test,"repaired-write") || !strcmp(test.test,"torn-repaired") || !strncmp(test.test,"complete-",9) || !strcmp(test.test,"final-publish") || !strcmp(test.test,"torn-candidate") || !strcmp(test.test,"candidate-sync") || !strcmp(test.test,"candidate-readback") || !strcmp(test.test,"target-buffer-mutation");
    test.fault=first_fault?test.test:NULL;test.healthy=!strcmp(test.test,"healthy") || !strcmp(test.test,"auto-legacy-resume") || !strcmp(test.test,"finish-stop") || !strcmp(test.test,"status-collection-rate");test.capturing=true;
    if(!strcmp(test.test,"cd-offset") || !strcmp(test.test,"cd-mode-switch") || !strcmp(test.test,"drive-buffer"))test.healthy=true;
    enum kui_salvage_result result;
    if(trial_case()) {run_trial_fixture(argv,&ops,&options);goto end;}
    if(burst_case()) {run_burst_fixture(argv,&ops,&options);goto end;}
    if(partial_case()) {run_partial_fixture(argv,&ops,&options);goto end;}
    if(defer_case()) {run_defer_fixture(argv,&ops,&options);goto end;}
    if(!strncmp(test.test,"legacy-narrow-v",15)) {
        char job[KUI_DEST_JOB_CAP];seed_legacy_narrow(test.test[15]=='1'?1u:2u,job);options.job=job;
        test.healthy=true;test.repairing=true;char latest[KUI_DEST_JOB_CAP];unsigned writes=test.writes;
        assert(kui_salvage_latest(&plan,&ops,latest) && !strcmp(latest,job) && test.writes==writes);
        result=run_salvage(&plan,&ops,&options,KUI_SALVAGE_RESUME,&status);
        assert(result==KUI_SALVAGE_RESOLVED && status.complete && status.targets==2 && status.recovered==2 &&
            status.collected_this_run==8u*KUI_RAW_BYTES && test.collection_calls==1 && test.collection_fad[0]==45182 && test.collection_count[0]==8);
        test.capturing=false;compare_complete(job);goto end;
    }
    result=run_salvage(&plan,&ops,&options,KUI_SALVAGE_NEW,&status);
    if(fast_case()) {
        unsigned missing=!strcmp(test.test,"fast-dense")?288u:2u;
        assert(result==KUI_SALVAGE_STOPPED && status.first_pass_complete && status.done==plan.bytes &&
            status.targets==missing && !status.recovered && !test.recoveries && !test.saw_split);
        unsigned expected_calls=!strcmp(test.test,"fast-dense")?13u:5u;assert(test.collection_calls==expected_calls);
        assert(test.collection_anchor_reads==(!strcmp(test.test,"fast-dense")?2u:0u));
        unsigned at=0;for(unsigned i=0;i<3;++i)for(uint32_t fad=plan.tracks[i].start;fad<plan.tracks[i].end;) {
            unsigned count=plan.tracks[i].end-fad;if(count>32)count=32;
            assert(test.collection_fad[at]==fad && test.collection_count[at]==count);++at;fad+=count;
        }
        remount();char path[512];uint8_t bytes[32*KUI_RAW_BYTES],expected[32*KUI_RAW_BYTES];
        for(unsigned i=0;i<3;++i) {
            FIL file;snprintf(path,sizeof(path),"0:%s/track%02u.%s",status.job,i+1,i==1?"raw":"bin");assert(f_open(&file,path,FA_READ)==FR_OK);
            for(uint32_t sector=0;sector<plan.tracks[i].end-plan.tracks[i].start;) {
                unsigned count=plan.tracks[i].end-plan.tracks[i].start-sector;if(count>32)count=32;UINT got=0;
                assert(f_read(&file,bytes,count*KUI_RAW_BYTES,&got)==FR_OK && got==count*KUI_RAW_BYTES);
                memcpy(expected,original[i]+sector*KUI_RAW_BYTES,count*KUI_RAW_BYTES);
                if(!strcmp(test.test,"fast-dense") && i==2 && sector<288)memset(expected,0,count*KUI_RAW_BYTES);
                if(!strcmp(test.test,"fast-invalid") && i==0 && !sector) {memset(expected+2*KUI_RAW_BYTES,0,KUI_RAW_BYTES);memset(expected+7*KUI_RAW_BYTES,0,KUI_RAW_BYTES);}
                assert(!memcmp(bytes,expected,count*KUI_RAW_BYTES));sector+=count;
            }
            assert(f_close(&file)==FR_OK);
        }
        assert(f_mount(NULL,"0:",0)==FR_OK);char job[KUI_DEST_JOB_CAP];strcpy(job,status.job);options.job=job;
        test.cancelled=false;test.healthy=true;test.repairing=true;
        struct kui_salvage_status resumed;result=run_salvage(&plan,&ops,&options,KUI_SALVAGE_RESUME,&resumed);
        assert(result==KUI_SALVAGE_RESOLVED && resumed.complete && resumed.targets==missing && resumed.recovered==missing &&
            !resumed.collected_this_run && test.collection_calls==expected_calls && !test.recoveries);
        test.capturing=false;compare_complete(job);goto end;
    }
    if(!strncmp(test.test,"anchor-discovery-",17)) {
        bool stopped=!strcmp(test.test,"anchor-discovery-stop");
        assert(result==(stopped?KUI_SALVAGE_STOPPED:KUI_SALVAGE_FAILED) && !status.job[0] && !status.complete && !test.writes);
        unsigned expected=!strcmp(test.test,"anchor-discovery-budget")?40u:stopped?3u:1u;
        assert(test.reads==expected && test.discovery_reads==expected && status.attempts==expected);
        assert(status.read_attempt==1 && status.read_limit==1);
        if(!strcmp(test.test,"anchor-discovery-budget"))assert(status.last_problem_state==KUI_SALVAGE_READ_DEFERRED && status.last_problem_attempt==1 && status.last_problem_limit==1);
        goto end;
    }
    if(cd_case()) {
        if(!strcmp(test.test,"cd-audio-anchor-mismatch") || !strcmp(test.test,"cd-audio-silence")) {
            assert(result==KUI_SALVAGE_FAILED && !status.job[0] && !test.writes && test.audio_confirm_mask==3);
            if(!strcmp(test.test,"cd-audio-anchor-mismatch"))assert(test.saw_audio_deferred && status.last_problem_confirm==2 && status.last_problem_confirm_limit==2);
            goto end;
        }
        if(!strcmp(test.test,"cd-mode-switch")) {assert(result==KUI_SALVAGE_FAILED && !status.complete && !status.targets);goto end;}
        if(!strcmp(test.test,"cd-offset")) {assert(result==KUI_SALVAGE_RESOLVED && status.complete && !status.targets);test.capturing=false;compare_complete(status.job);goto end;}
        assert(result==KUI_SALVAGE_UNRESOLVED && status.targets==32 && status.remaining==1 && status.first_pass_complete);
        char selected[KUI_DEST_JOB_CAP];strcpy(selected,status.job);options.job=selected;
        char latest[KUI_DEST_JOB_CAP];assert(kui_salvage_latest(&plan,&ops,latest) && !strcmp(selected,latest));
        test.repairing=true;result=run_salvage(&plan,&ops,&options,KUI_SALVAGE_RESUME,&status);
        assert(result==KUI_SALVAGE_RESOLVED && status.complete && status.targets==32 && status.recovered==32);
        test.capturing=false;compare_complete(selected);
        if(!strcmp(test.test,"cd-profile-mismatch")) {plan.profile=KUI_CAPTURE_PROFILE_GD;unsigned writes=test.writes;test.capturing=true;assert(run_salvage(&plan,&ops,&options,KUI_SALVAGE_RESUME,&status)==KUI_SALVAGE_FAILED && !status.complete && writes==test.writes);}
        goto end;
    }
    if(!strncmp(test.test,"drive-",6)) {
        if(!strcmp(test.test,"drive-buffer")) {assert(result==KUI_SALVAGE_RESOLVED && test.recoveries>=1 && test.saw_restored_read);test.capturing=false;compare_complete(status.job);}
        else if(!strcmp(test.test,"drive-pass")) {assert(result==KUI_SALVAGE_RESOLVED && test.recoveries==1 && status.recovered==expected_targets());test.capturing=false;compare_complete(status.job);}
        else {
            assert(result==(!strcmp(test.test,"drive-stop")?KUI_SALVAGE_STOPPED:KUI_SALVAGE_FAILED) && !status.complete && status.recovered==(!strcmp(test.test,"drive-fatal")?2u:34u));
            assert(test.recoveries==(!strcmp(test.test,"drive-fatal")?0u:1u));
            if(!strcmp(test.test,"drive-media"))assert(status.read_state==KUI_SALVAGE_READ_FATAL && status.read_fad==45150 && status.track==3 && status.read_attempt==11 && status.read_limit==11);
            if(!strcmp(test.test,"drive-fatal"))assert(status.read_state==KUI_SALVAGE_READ_FATAL && status.read_fad==152 && status.track==1);
        }
        goto end;
    }
    if(!strcmp(test.test,"invalid-limit")) {assert(result==KUI_SALVAGE_FAILED && !test.reads && !test.writes);goto end;}
    if(!strcmp(test.test,"identity-stop")) {assert(result==KUI_SALVAGE_STOPPED && !status.complete && !status.job[0] && !test.reads && !test.writes);goto end;}
    char job[KUI_DEST_JOB_CAP];strcpy(job,status.job);options.job=job;
    if(!strcmp(test.test,"fatal-committed-resume")) {
        assert(result==KUI_SALVAGE_FAILED && !strcmp(job,"/KUI/salvage/job-0002") && !status.complete && !status.first_pass_complete &&
            status.done==76u*KUI_RAW_BYTES && status.targets==36 && status.remaining==36 && !status.recovered &&
            test.fatal_resume_injected && test.reads==test.fatal_read_at && !test.recoveries);
        uint8_t header[4096],journal[131072],prefix[131072],expected[40*KUI_RAW_BYTES];char path[512];
        remount();snprintf(path,sizeof(path),"0:%s/header.bin",job);assert(load(path,header,sizeof(header))==sizeof(header));
        snprintf(path,sizeof(path),"0:%s/journal.bin",job);size_t journal_bytes=load(path,journal,sizeof(journal));
        for(unsigned i=0;i<3;++i) {
            unsigned bytes=(i==2?32u:plan.tracks[i].end-plan.tracks[i].start)*KUI_RAW_BYTES;
            snprintf(path,sizeof(path),"0:%s/track%02u.%s",job,i+1,i==1?"raw":"bin");
            assert(load(path,prefix,sizeof(prefix))==bytes);memcpy(expected,original[i],bytes);
            if(i<2)memset(expected,0,(i?4u:32u)*KUI_RAW_BYTES);
            assert(!memcmp(prefix,expected,bytes));
        }
        /* Both folder headers match the disc; latest must retain the greater
         * numbered interrupted job, not silently start the older empty job. */
        write_file("0:/KUI/salvage/job-0001/header.bin",header,sizeof(header));
        assert(f_mount(NULL,"0:",0)==FR_OK && !fclose(test.image));
        test.image=fopen(argv[1],"r+b");assert(test.image);kui_media_set(&media);
        test.healthy=true;test.repairing=true;test.resuming_after_fatal=true;
        unsigned writes=test.writes;char selected[KUI_DEST_JOB_CAP];
        assert(kui_salvage_latest(&plan,&ops,selected) && !strcmp(selected,job) && test.writes==writes);
        struct kui_salvage_status resumed;
        result=run_salvage(&plan,&ops,&options,KUI_SALVAGE_RESUME,&resumed);
        assert(result==KUI_SALVAGE_RESOLVED && resumed.complete && resumed.first_pass_complete && resumed.done==plan.bytes &&
            resumed.targets==36 && resumed.recovered==36 && !resumed.remaining && resumed.collected_this_run==8u*KUI_RAW_BYTES &&
            test.resumed_collection_reads==1 && !test.recoveries);
        remount();snprintf(path,sizeof(path),"0:%s/header.bin",job);assert(load(path,prefix,sizeof(prefix))==sizeof(header) && !memcmp(prefix,header,sizeof(header)));
        snprintf(path,sizeof(path),"0:%s/journal.bin",job);assert(load(path,prefix,sizeof(prefix))>journal_bytes && !memcmp(prefix,journal,journal_bytes));
        assert(f_mount(NULL,"0:",0)==FR_OK);test.capturing=false;compare_complete(job);goto end;
    }
    if(!strcmp(test.test,"status-collection-rate")) {
        assert(result==KUI_SALVAGE_STOPPED && status.done==32u*KUI_RAW_BYTES && status.collected_this_run==status.done &&
            status.collection_elapsed_ms>=5000 && status.collection_elapsed_ms<status.elapsed_ms && test.saw_collection_stall);
        test.cancelled=false;result=run_salvage(&plan,&ops,&options,KUI_SALVAGE_RESUME,&status);
        assert(result==KUI_SALVAGE_RESOLVED && status.complete && test.saw_resumed_collection &&
            status.collected_this_run==plan.bytes-32u*KUI_RAW_BYTES && status.collection_elapsed_ms>0 && status.collection_elapsed_ms<status.elapsed_ms);
        test.capturing=false;compare_complete(job);goto end;
    }
    if(!strcmp(test.test,"fatal")) {
        assert(result==KUI_SALVAGE_FAILED && !status.complete && !status.targets);goto end;
    }
    if(!strcmp(test.test,"healthy") || !strcmp(test.test,"status-deferred-recovery")) {
        assert(result==KUI_SALVAGE_RESOLVED && status.complete);
        if(!strcmp(test.test,"healthy"))assert(!status.targets && status.last_problem_state==KUI_SALVAGE_READ_NONE);
        else assert(status.targets==32 && status.recovered==32 && !test.first_attempts[0] && status.last_problem_state==KUI_SALVAGE_READ_DEFERRED && status.last_problem_fad==150 &&
            status.last_problem_count==32 && status.last_problem_attempt==1 && status.last_problem_limit==1);
        test.capturing=false;compare_complete(job);goto end;
    }
    if(!strcmp(test.test,"finish-stop")) {
        assert(result==KUI_SALVAGE_STOPPED && status.first_pass_complete && !status.complete && !status.targets);
        test.capturing=false;remount();char path[512];FILINFO info;snprintf(path,sizeof(path),"0:%s/salvage.gdi",job);assert(f_stat(path,&info)==FR_NO_FILE);snprintf(path,sizeof(path),"0:%s/salvage.json",job);assert(f_stat(path,&info)==FR_NO_FILE);assert(f_mount(NULL,"0:",0)==FR_OK);
        test.cancelled=false;test.capturing=true;result=run_salvage(&plan,&ops,&options,KUI_SALVAGE_RESUME,&status);
        assert(result==KUI_SALVAGE_RESOLVED && status.complete && !status.targets);test.capturing=false;compare_complete(job);goto end;
    }
    if(!strcmp(test.test,"anchor-interior")) {
        assert(test.discovery_reads<=80u); /* One probe per candidate, not eleven. */
        assert(result==KUI_SALVAGE_UNRESOLVED && status.first_pass_complete && status.done==plan.bytes && status.targets==40 && status.remaining==32);
        test.capturing=false;remount();uint8_t header[4096];char path[512];snprintf(path,sizeof(path),"0:%s/header.bin",job);assert(load(path,header,sizeof(header))==sizeof(header));
        uint32_t a=get32(header+24),b=get32(header+28);assert(a!=b && a>=45166 && a<45174 && b>=45166 && b<45174);assert(f_mount(NULL,"0:",0)==FR_OK);
        char latest[KUI_DEST_JOB_CAP];assert(kui_salvage_latest(&plan,&ops,latest) && !strcmp(latest,job));
        test.healthy=true;test.repairing=true;test.capturing=true;result=run_salvage(&plan,&ops,&options,KUI_SALVAGE_RECOVER,&status);
        assert(result==KUI_SALVAGE_RESOLVED && status.complete && status.recovered==40 && !status.remaining);test.capturing=false;compare_complete(job);goto end;
    }
    if(large_case()) {
        assert(result==KUI_SALVAGE_UNRESOLVED && status.first_pass_complete && !status.complete && status.targets==LARGE_TARGETS && status.remaining==LARGE_TARGETS);
        assert(status.done==plan.bytes && LARGE_TARGETS>8192u);
        test.repairing=true;test.healthy=true;test.stop_after=128;test.capturing=true;
        result=run_salvage(&plan,&ops,&options,KUI_SALVAGE_RECOVER,&status);
        assert(result==KUI_SALVAGE_STOPPED && !status.complete && status.targets==LARGE_TARGETS && status.recovered==128 && status.remaining==LARGE_TARGETS-128);
        test.cancelled=false;test.stop_after=0;
        result=run_salvage(&plan,&ops,&options,KUI_SALVAGE_RESUME,&status);
        assert(result==KUI_SALVAGE_RESOLVED && status.complete && status.targets==LARGE_TARGETS && status.recovered==LARGE_TARGETS && !status.remaining);
        test.capturing=false;compare_complete(job);
#ifdef KUI_TEST_ALLOCATIONS
        assert(worker_allocations>=3 && !live_worker_bytes && peak_worker_bytes<=256u*1024u);
        printf("PASS bounded worker allocations: %u calls, largest %zu bytes, peak %zu bytes for %u targets\n",worker_allocations,largest_worker_allocation,peak_worker_bytes,LARGE_TARGETS);
#endif
        remount();char path[512];snprintf(path,sizeof(path),"0:%s/patches.bin",job);FILINFO packed;assert(f_stat(path,&packed)==FR_OK && packed.fsize==(uint64_t)LARGE_TARGETS*5120u);
        snprintf(path,sizeof(path),"0:%s",job);DIR dir;FILINFO entry;assert(f_opendir(&dir,path)==FR_OK);unsigned count=0;
        for(;;) {assert(f_readdir(&dir,&entry)==FR_OK);if(!entry.fname[0])break;assert(strncmp(entry.fname,"patch-",6));++count;}assert(f_closedir(&dir)==FR_OK && count<20);
        assert(f_mount(NULL,"0:",0)==FR_OK);goto end;
    }
    if(first_fault || !strcmp(test.test,"cancel") || !strcmp(test.test,"auto-resume") || !strcmp(test.test,"auto-legacy-resume")) {
        assert(result==(first_fault?KUI_SALVAGE_FAILED:KUI_SALVAGE_STOPPED));
        assert(!status.complete);if(first_fault)assert(test.injected);
        test.fault=NULL;test.cancelled=false;test.capturing=false;
        if(!strcmp(test.test,"auto-legacy-resume")) {assert(!status.targets);legacy_header(job,false);test.healthy=false;}
        if(!test.automatic)test.test="resume-first";
        test.capturing=true;
        result=run_salvage(&plan,&ops,&options,KUI_SALVAGE_RESUME,&status);
    }
    if(test.automatic) {
        if(!strcmp(test.test,"auto-exhausted") || !strcmp(test.test,"auto-until-stop") || !strcmp(test.test,"auto-read-stop")) {
            bool stopped=strcmp(test.test,"auto-exhausted")!=0,read_stop=!strcmp(test.test,"auto-read-stop");
            assert(result==(stopped?KUI_SALVAGE_STOPPED:KUI_SALVAGE_UNRESOLVED) && status.first_pass_complete && !status.complete && status.targets==expected_targets() && status.remaining==(read_stop?36u:2u));
            assert(status.pass==(read_stop?1u:stopped?3u:5u));
            if(read_stop)assert(!test.repair_count && status.attempts==test.reads);
            test.capturing=false;compare_unresolved(job);
            test.cancelled=false;test.healthy=true;test.capturing=true;
            result=run_salvage(&plan,&ops,&options,KUI_SALVAGE_RESUME,&status);
        }
        unsigned targets=expected_targets();
        assert(result==KUI_SALVAGE_RESOLVED && status.complete && status.first_pass_complete && status.targets==targets && status.recovered==targets && !status.remaining);
        if(!strcmp(test.test,"auto-late") || !strcmp(test.test,"auto-until-late")) {
            unsigned final=!strcmp(test.test,"auto-late")?5:51;
            assert(status.pass==final && test.repair_count==2*final+1 && !test.first_attempts[0] && !test.first_attempts[1]);
            for(unsigned pass=1;pass<=final;++pass) {
                unsigned at=(pass-1)*2;assert(test.repair_order[at]==((pass&1)?152u:342u));
                if(pass==final) {assert(test.repair_order[at+1]==342 && test.repair_order[at+2]==342);}
                else assert(test.repair_order[at+1]==((pass&1)?342u:152u));
            }
        }
        test.capturing=false;compare_complete(job);
        if(!strcmp(test.test,"auto-legacy-resume"))check_legacy_header(job);
        goto end;
    }
    assert(result==KUI_SALVAGE_UNRESOLVED && status.first_pass_complete && !status.complete && status.targets==expected_targets() && status.remaining==(!strcmp(test.test,"legacy-repair")?36u:2u));
    if(!strcmp(test.test,"adaptive")) {
        const uint32_t fads[5]={150,182,340,45150,45182};const unsigned counts[5]={32,8,4,32,8};
        assert(test.collection_calls==5 && !memcmp(test.collection_fad,fads,sizeof(fads)) && !memcmp(test.collection_count,counts,sizeof(counts)) &&
            !test.first_attempts[0] && !test.first_attempts[1] && !test.saw_split && test.saw_deferred);
    }
    if(!strcmp(test.test,"bad-parity"))assert(!test.leaf_attempt_mask[0] && !test.first_attempts[0] && test.saw_deferred);
    test.capturing=false;compare_unresolved(job);
    if(!strncmp(test.test,"anchor-",7) && strcmp(test.test,"anchor-interior")) {
        test.identity_fault=true;test.repairing=true;test.capturing=true;
        unsigned writes=test.writes;
        if(!strcmp(test.test,"anchor-latest-retry")) {
            char selected[KUI_DEST_JOB_CAP];assert(kui_salvage_latest(&plan,&ops,selected) && !strcmp(selected,job) && test.anchor_failures==4);
            assert(test.writes==writes);test.identity_fault=false;
        } else {
            result=run_salvage(&plan,&ops,&options,KUI_SALVAGE_RESUME,&status);
            if(!strcmp(test.test,"anchor-resume-retry")) {assert(result==KUI_SALVAGE_RESOLVED && test.anchor_failures==4 && test.anchor_attempt_mask==15);test.capturing=false;compare_complete(job);}
            else {
                assert(result==KUI_SALVAGE_FAILED && !status.complete && test.writes==writes);
                assert(test.anchor_fault_reads==(!strcmp(test.test,"anchor-resume-fatal")?1u:11u));
                if(!strcmp(test.test,"anchor-resume-exhausted"))assert(test.anchor_attempt_mask==2047 && status.last_problem_state==KUI_SALVAGE_READ_DEFERRED && status.last_problem_attempt==11 && status.last_problem_limit==11);
            }
            goto end;
        }
    }
    if(!strcmp(test.test,"quarantine-resume")) {
        remount();assert(kui_recovery_guard_mark(job,log_line));assert(f_mount(NULL,"0:",0)==FR_OK);unsigned writes=test.writes;
        remount();
        char alias[KUI_DEST_JOB_CAP];snprintf(alias,sizeof(alias),"%s",job);
        for(unsigned i=0;alias[i];++i) {
            if(alias[i]>='a' && alias[i]<='z' && !(i&1u))alias[i]=(char)(alias[i]-'a'+'A');
            else if(alias[i]>='A' && alias[i]<='Z' && (i&1u))alias[i]=(char)(alias[i]-'A'+'a');
        }
        assert(!kui_recovery_guard_check(alias,log_line));assert(f_mount(NULL,"0:",0)==FR_OK);
        test.repairing=true;test.healthy=true;test.capturing=true;
        assert(run_salvage(&plan,&ops,&options,KUI_SALVAGE_RESUME,&status)==KUI_SALVAGE_FAILED && !status.complete && strstr(status.message,"unstable"));
        assert(test.writes==writes);test.capturing=false;compare_unresolved(job);goto end;
    }
    if(!strcmp(test.test,"legacy-repair")) {legacy_header(job,true);}
    if(!strcmp(test.test,"index-corrupt") || !strcmp(test.test,"index-delete")) {
        remount();char path[512];snprintf(path,sizeof(path),"0:%s/targets.cache",job);
        if(!strcmp(test.test,"index-delete"))assert(f_unlink(path)==FR_OK);
        else {uint8_t invalid[64];memset(invalid,0xa7,sizeof(invalid));write_file(path,invalid,sizeof(invalid));}
        assert(f_mount(NULL,"0:",0)==FR_OK);
    }
    if(!strcmp(test.test,"latest-wrong-disc")) {
        remount();uint8_t header[4096];char source[512];snprintf(source,sizeof(source),"0:%s/header.bin",job);
        assert(load(source,header,sizeof(header))==sizeof(header));header[32]^=1;put32(header+4092,kui_crc32(0,header,4092));
        assert(f_mkdir("0:/KUI/salvage/job-0002")==FR_OK);write_file("0:/KUI/salvage/job-0002/header.bin",header,sizeof(header));assert(f_mount(NULL,"0:",0)==FR_OK);
    }
    char latest[KUI_DEST_JOB_CAP];assert(kui_salvage_latest(&plan,&ops,latest) && !strcmp(latest,job));
    if(!strcmp(test.test,"corrupt-journal") || !strcmp(test.test,"corrupt-header") || !strcmp(test.test,"wrong-disc") || !strcmp(test.test,"placeholder-changed")) {
        remount();char path[512];uint8_t data[131072];
        if(!strcmp(test.test,"wrong-disc"))original[2][42]^=1;
        else {
            const char *name=!strcmp(test.test,"corrupt-journal")?"journal.bin":!strcmp(test.test,"corrupt-header")?"header.bin":"track01.bin";
            snprintf(path,sizeof(path),"0:%s/%s",job,name);size_t n=load(path,data,sizeof(data));
            size_t at=!strcmp(test.test,"placeholder-changed")?2*KUI_RAW_BYTES:0;data[at]^=1;write_file(path,data,(UINT)n);
        }
        assert(f_mount(NULL,"0:",0)==FR_OK);test.repairing=true;test.capturing=true;
        result=run_salvage(&plan,&ops,&options,KUI_SALVAGE_RECOVER,&status);assert(result==KUI_SALVAGE_FAILED && !status.complete);goto end;
    }
    test.repairing=true;test.capturing=true;test.fault=repair_fault?test.test:NULL;test.injected=false;test.last_event=0;
    result=run_salvage(&plan,&ops,&options,KUI_SALVAGE_RECOVER,&status);
    if(!strcmp(test.test,"five-passes")) {
        assert(result==KUI_SALVAGE_UNRESOLVED && status.remaining==2 && status.pass==5 && test.repair_count==15);
        for(unsigned i=0;i<5;++i) {const uint32_t forward[3]={152,342,342},reverse[3]={342,342,152};assert(!memcmp(test.repair_order+i*3,(i&1)?reverse:forward,sizeof(forward)));}goto end;
    }
    if(!strcmp(test.test,"audio-mismatch") || !strcmp(test.test,"wrong-fad") || !strcmp(test.test,"bad-ecc")) {
        assert(result==KUI_SALVAGE_UNRESOLVED && status.remaining==1 && status.recovered==expected_targets()-1 && !status.complete);
        if(!strcmp(test.test,"audio-mismatch"))assert(test.audio_confirm_mask==3 && status.last_problem_state==KUI_SALVAGE_READ_DEFERRED &&
            status.last_problem_fad==342 && status.last_problem_attempt==1 && status.last_problem_limit==1 &&
            status.last_problem_confirm==2 && status.last_problem_confirm_limit==2);
        goto end;
    }
    if(!strcmp(test.test,"repair-fatal") || !strcmp(test.test,"repair-stop")) {assert(result==(!strcmp(test.test,"repair-stop")?KUI_SALVAGE_STOPPED:KUI_SALVAGE_FAILED) && !status.complete && status.recovered==34);goto end;}
    if(repair_fault) {
        assert(test.injected && result==KUI_SALVAGE_FAILED && !status.complete);
        if(!strcmp(test.test,"backup-substitute")) {
            /* This changes a successful read of an already published audio
             * candidate, recomputing both CRCs. Its journal publication must
             * reject that substitute; the fresh valid reread proves saved
             * reads disagreed, so an immediate Resume must remain blocked. */
            assert(!status.pass && status.recovered==34 && status.remaining==2 &&
                strstr(status.message,"Saved candidate reads differ"));
            test.capturing=false;compare_unresolved(job);
            remount();assert(!kui_recovery_guard_check(job,NULL));
            assert(f_mount(NULL,"0:",0)==FR_OK);
            test.fault=NULL;test.injected=false;test.last_event=0;test.patch_started=false;
            unsigned writes=test.writes;test.capturing=true;
            result=run_salvage(&plan,&ops,&options,KUI_SALVAGE_RESUME,&status);
            assert(result==KUI_SALVAGE_FAILED && !status.complete && strstr(status.message,"unstable") && test.writes==writes);
            test.capturing=false;compare_unresolved(job);goto end;
        }
        test.fault=NULL;test.injected=false;test.last_event=0;test.patch_started=false;
        result=run_salvage(&plan,&ops,&options,KUI_SALVAGE_RECOVER,&status);
    }
    assert(result==KUI_SALVAGE_RESOLVED && status.complete && status.targets==expected_targets() && status.recovered==expected_targets() && !status.remaining);
    test.capturing=false;compare_complete(job);
    if(!strcmp(test.test,"corrupt-patch")) {
        remount();uint8_t patch[8192];char path[512];snprintf(path,sizeof(path),"0:%s/patches.bin",job);FILINFO info;bool packed=f_stat(path,&info)==FR_OK;
        if(!packed)snprintf(path,sizeof(path),"0:%s/patch-0000.bin",job);
        FIL file;UINT got=0,put=0;assert(f_open(&file,path,FA_READ|FA_WRITE)==FR_OK);unsigned n=packed?5120u:8192u;assert(f_read(&file,patch,n,&got)==FR_OK && got==n);patch[64+KUI_RAW_BYTES+17]^=1;
        assert(f_lseek(&file,0)==FR_OK && f_write(&file,patch,n,&put)==FR_OK && put==n && f_sync(&file)==FR_OK && f_close(&file)==FR_OK);assert(f_mount(NULL,"0:",0)==FR_OK);test.capturing=true;
        assert(run_salvage(&plan,&ops,&options,KUI_SALVAGE_RESUME,&status)==KUI_SALVAGE_FAILED && !status.complete);goto end;
    }
    if(!strcmp(test.test,"candidate-journal-forgery")) {
        remount();uint8_t events[131072];char path[512];snprintf(path,sizeof(path),"0:%s/journal.bin",job);size_t bytes=load(path,events,sizeof(events));bool found=false;
        for(size_t at=0;at<bytes;at+=512)if(get32(events+at+16)==7) {put32(events+at+32,get32(events+at+32)^1u);put32(events+at+508,kui_crc32(0,events+at,508));found=true;break;}
        assert(found);write_file(path,events,(UINT)bytes);assert(f_mount(NULL,"0:",0)==FR_OK);test.capturing=true;
        assert(run_salvage(&plan,&ops,&options,KUI_SALVAGE_RESUME,&status)==KUI_SALVAGE_FAILED && !status.complete);goto end;
    }
    unsigned reads=test.reads;test.capturing=true;
    assert(run_salvage(&plan,&ops,&options,KUI_SALVAGE_RESUME,&status)==KUI_SALVAGE_RESOLVED && status.recovered==expected_targets());
    assert(test.reads==reads+2 && status.last_problem_state==KUI_SALVAGE_READ_NONE);test.capturing=false;compare_complete(job);
end:
#ifdef KUI_TEST_ALLOCATIONS
    assert(!live_worker_bytes && peak_worker_bytes<=256u*1024u);
    if(!large_case())printf("PASS bounded worker allocations: %u calls, largest %zu bytes, peak %zu bytes\n",worker_allocations,largest_worker_allocation,peak_worker_bytes);
#endif
    test.capturing=false;assert(f_mount(NULL,"0:",0)==FR_OK && !fclose(test.image));
    for(unsigned i=0;i<3;++i)free(original[i]);
    printf("PASS salvage %s\n",argv[3]);return 0;
}
