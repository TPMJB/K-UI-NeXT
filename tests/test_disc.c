/* SPDX-License-Identifier: GPL-3.0-only */
/* Run the actual Dreamcast optical adapter against deterministic firmware and
 * scheduler doubles. These tests exercise transfer acceptance, read policy,
 * bounded service/yield behavior, cancellation, deadlines and poisoned state. */
#include "platform.h"
#include <dc/syscalls.h>
#include <arch/cache.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static struct {
    uint64_t us,last_handoff,max_service_us,command_started,abort_started_at;
    uint64_t complete_after_us,abort_after_us;
    unsigned sends,reads,polls,total_polls,sleeps,passes,aborts,modes,bus_inits;
    unsigned target_polls,poll_us,guard;
    unsigned failure_prefix;
    int reported_delta,command;
    int failure_sense,failure_detail;
    size_t failure_size;
    bool stop,abort_started,submit_busy,submit_always_busy,underfill,fail;
    bool timeout,cancel,abort_fail,mode_fail,changed,cancel_on_pass,cancel_on_sleep;
    bool submit_fail,terminal_not_found,abort_medium,streaming_failure;
    cd_read_params_t params;
    cd_cmd_toc_params_t toc_params;
    cd_check_drive_status_t drive;
    int drive_result;
    unsigned drive_queries,toc_reads[2],toc_order[2],toc_count;
    bool cd_toc,toc_bad,toc_guard,recovery_during_command;
    struct kui_drive_mode hardware_mode,set_mode;
    struct kui_drive_mode *mode_destination;
    unsigned mode_queries,mode_sets,mode_guard,mode_short,mode_fail_command;
    unsigned mode_ignore_sets,mode_timeout_command,mode_fail_on_set;
    unsigned mode_format_change;
    bool mode_init_reset,controls_during_command;
    char log[32768];size_t used;
} fake;
char test_cache_log[64];unsigned test_cache_n;uintptr_t test_cache_start[16];size_t test_cache_bytes[16];
uint64_t timer_ms_gettime64(void) {return fake.us/1000;}
uint64_t timer_us_gettime64(void) {return fake.us;}
void thd_sleep(unsigned ms) {
    assert(ms==1);fake.us+=8000;++fake.sleeps;
    if(fake.cancel_on_sleep) fake.stop=true;
}
void thd_pass(void) {
    uint64_t service=fake.us-fake.last_handoff;
    if(service>fake.max_service_us) fake.max_service_us=service;
    fake.us+=8000;fake.last_handoff=fake.us;++fake.passes;
    if(fake.cancel_on_pass) fake.stop=true;
}
bool kui_cancelled(void) {return fake.stop;}
void kui_drive_init_bus(void) {++fake.bus_inits;}
void kui_log(const char *format,...) {
    va_list args;va_start(args,format);
    int n=vsnprintf(fake.log+fake.used,sizeof(fake.log)-fake.used,format,args);va_end(args);
    assert(n>=0 && (size_t)n+1<sizeof(fake.log)-fake.used);
    fake.used+=(size_t)n;fake.log[fake.used++]='\n';fake.log[fake.used]=0;
}
int syscall_gdrom_sector_mode(cd_sec_mode_params_t *p) {
    assert(p->size==2352 && p->track_type==0);fake.us+=7;++fake.modes;
    if(fake.mode_format_change==1)fake.hardware_mode.speed=(fake.hardware_mode.speed+1u)%8u;
    if(fake.mode_format_change==2)fake.hardware_mode.standby^=1u;
    if(fake.mode_format_change==3)fake.hardware_mode.read_flags^=8u;
    if(fake.mode_format_change==4)fake.hardware_mode.read_retry^=1u;
    return fake.mode_fail?-1:0;
}
int syscall_gdrom_check_drive(cd_check_drive_status_t *out) {
    assert(out);++fake.drive_queries;
    /* A failed syscall may leave its zero-filled destination untouched. A
     * positive failure must not turn that into apparent CDDA admission. */
    if(!fake.drive_result)*out=fake.drive;
    return fake.drive_result;
}
int syscall_gdrom_send_command(cd_cmd_code_t code,void *params) {
    assert(code==CD_CMD_PIOREAD || code==CD_CMD_DMAREAD || code==CD_CMD_INIT ||
           code==CD_CMD_STOP || code==CD_CMD_GETTOC2 || code==CD_CMD_REQ_MODE || code==CD_CMD_SET_MODE);
    fake.command=code;fake.us+=13;++fake.sends;
    if(fake.submit_fail)return -1;
    if(fake.recovery_during_command) {
        const struct kui_capture_plan expected={.count=1};unsigned sends=fake.sends;
        assert(!kui_disc_recover(NULL,&expected) && fake.sends==sends);
        fake.recovery_during_command=false;
    }
    if(fake.controls_during_command) {
        const struct kui_drive_options options={.speed=1};unsigned sends=fake.sends;
        assert(!kui_disc_controls_begin(&options) && fake.sends==sends);
        fake.controls_during_command=false;
    }
    if(fake.submit_busy || fake.submit_always_busy) {fake.submit_busy=false;return 0;}
    fake.polls=0;fake.abort_started=false;fake.command_started=fake.us;
    if(code==CD_CMD_PIOREAD || code==CD_CMD_DMAREAD) {fake.params=*(cd_read_params_t *)params;++fake.reads;}
    if(code==CD_CMD_REQ_MODE) {
        /* The first parameter is a pointer, not the mode tuple itself. */
        assert(params);fake.mode_destination=*(struct kui_drive_mode **)params;
        assert(fake.mode_destination && ((uintptr_t)fake.mode_destination&3u)==0);++fake.mode_queries;
    }
    if(code==CD_CMD_SET_MODE) {
        assert(params);fake.set_mode=*(struct kui_drive_mode *)params;++fake.mode_sets;
        assert(fake.set_mode.speed<=7 && fake.set_mode.standby<=65535 &&
               fake.set_mode.read_flags<=255 && fake.set_mode.read_retry<=255);
    }
    if(code==CD_CMD_GETTOC2) {
        assert(params);fake.toc_params=*(cd_cmd_toc_params_t *)params;
        assert(fake.toc_params.toc && fake.toc_params.area>=0 && fake.toc_params.area<2);
        assert(fake.toc_count<2u);fake.toc_order[fake.toc_count++]=(unsigned)fake.toc_params.area;
        ++fake.toc_reads[fake.toc_params.area];
    }
    if(code==CD_CMD_DMAREAD) test_cache_note('D',0,0);   /* the transfer starts here */
    return 1;
}
void syscall_gdrom_exec_server(void) {fake.us+=3;}
static void damage_guard(void) {
    uint8_t *data=fake.params.buffer;
    size_t bytes=(size_t)fake.params.num_sec*KUI_RAW_BYTES;
    if(fake.guard==1) data[bytes]=0; /* tail or trailing guard */
    if(fake.guard==2) data[-1]=0;
}
static void return_toc(void) {
    cd_toc_t *toc=fake.toc_params.toc;memset(toc,0xff,sizeof(*toc));
    if(fake.cd_toc) {
        assert(fake.toc_params.area==0);
        unsigned control=fake.drive.disc_type==CD_CDDA?0u:4u;
        toc->first=toc->last=0x01010000u;
        toc->entry[0]=(control<<28u)|0x01000000u|225u;
        toc->leadout_sector=0x01000000u|865u;
    } else if(!fake.toc_params.area) {
        toc->first=0x41010000u;toc->last=0x01020000u;
        toc->entry[0]=0x41000000u|150u;toc->entry[1]=0x01000000u|650u;
        toc->leadout_sector=0x01000000u|950u;
    } else {
        toc->first=toc->last=0x41030000u;
        toc->entry[2]=0x41000000u|45150u;toc->leadout_sector=0x41000000u|51813u;
    }
    if(fake.toc_bad)toc->entry[(toc->first>>16u&255u)-1u]=UINT32_MAX;
    if(fake.toc_guard)((uint8_t *)toc)[sizeof(*toc)]=0;
}
int syscall_gdrom_check_command(int handle,cd_cmd_chk_status_t *detail) {
    assert(handle==1);fake.us+=fake.poll_us;++fake.polls;++fake.total_polls;
    if(fake.abort_started) {
        if(fake.abort_medium) {detail->err1=3;detail->size=fake.failure_size;return -1;}
        return fake.abort_fail || fake.us-fake.abort_started_at<fake.abort_after_us?1:0;
    }
    if(fake.us-fake.command_started<fake.complete_after_us)return 1;
    if(fake.command==CD_CMD_REQ_MODE || fake.command==CD_CMD_SET_MODE) {
        if(fake.mode_timeout_command==(unsigned)fake.command)return 1;
        if(fake.mode_fail_command==(unsigned)fake.command ||
           (fake.command==CD_CMD_SET_MODE && fake.mode_fail_on_set==fake.mode_sets)) {
            /* SET can change the hardware and still report failure. */
            if(fake.command==CD_CMD_SET_MODE)fake.hardware_mode=fake.set_mode;
            detail->err1=5;return -1;
        }
    }
    if(fake.streaming_failure)return 3;
    if(fake.fail || fake.changed || fake.terminal_not_found) {
        if(fake.command==CD_CMD_PIOREAD || fake.command==CD_CMD_DMAREAD) {
            size_t bytes=(size_t)fake.params.num_sec*KUI_RAW_BYTES;
            assert(fake.failure_prefix<=bytes);
            kui_pattern(fake.params.buffer,(uint64_t)fake.params.start_sec*KUI_RAW_BYTES,fake.failure_prefix);
            damage_guard();
        }
        detail->err1=fake.changed?6:fake.failure_sense?fake.failure_sense:3;
        detail->err2=fake.failure_detail;detail->size=fake.failure_size;
        return fake.terminal_not_found?0:-1;
    }
    if(fake.cancel) fake.stop=true;
    if(fake.timeout || fake.cancel) return 1;
    unsigned needed=fake.target_polls?fake.target_polls:((fake.reads&1)?2u:4u);
    if(fake.polls<needed) return 1;
    if(fake.command==CD_CMD_STOP || fake.command==CD_CMD_INIT) {
        if(fake.command==CD_CMD_INIT && fake.mode_init_reset)
            fake.hardware_mode=(struct kui_drive_mode){0,45000,0x19,8};
        detail->size=0;return 2;
    }
    if(fake.command==CD_CMD_REQ_MODE) {
        *fake.mode_destination=fake.hardware_mode;
        if(fake.mode_guard==1)((uint8_t *)fake.mode_destination)[sizeof(*fake.mode_destination)]=0;
        if(fake.mode_guard==2)((uint8_t *)fake.mode_destination)[-1]=0;
        detail->size=fake.mode_short==(unsigned)fake.command?9u:10u;return 2;
    }
    if(fake.command==CD_CMD_SET_MODE) {
        if(!fake.mode_ignore_sets || fake.mode_sets>fake.mode_ignore_sets)fake.hardware_mode=fake.set_mode;
        detail->size=fake.mode_short==(unsigned)fake.command?9u:10u;return 2;
    }
    if(fake.command==CD_CMD_GETTOC2) {return_toc();detail->size=sizeof(cd_toc_t);return 2;}
    assert(fake.command==CD_CMD_PIOREAD || fake.command==CD_CMD_DMAREAD);
    if(fake.command==CD_CMD_DMAREAD) test_cache_note('C',0,0);   /* the transfer is done */
    size_t bytes=(size_t)fake.params.num_sec*KUI_RAW_BYTES;
    uint8_t *data=fake.params.buffer;
    kui_pattern(data,(uint64_t)fake.params.start_sec*KUI_RAW_BYTES,bytes-(fake.underfill?1:0));
    damage_guard();detail->size=(uint32_t)((int)bytes+fake.reported_delta);return 2;
}
void syscall_gdrom_abort_command(int handle) {
    assert(handle==1);fake.us+=11;++fake.aborts;fake.abort_started=true;fake.abort_started_at=fake.us;
}
static void reset(void) {
    memset(&fake,0,sizeof(fake));fake.poll_us=23;kui_disc_timing_reset();
    fake.drive=(cd_check_drive_status_t){CD_STATUS_PAUSED,CD_GDROM};
    fake.hardware_mode=(struct kui_drive_mode){0,45000,0x19,8};
}
static void expect(const char *line) {
    if(!strstr(fake.log,line)) {fprintf(stderr,"Missing %s\n%s",line,fake.log);assert(false);}
}
static void unchanged(const uint8_t *out,size_t size) {
    for(size_t i=0;i<size;i++) assert(out[i]==0xd3);
}
static void capture_phase(void) {kui_disc_timing_phase(NULL,true);}
static int run_salvage_partial(const char *mode) {
    uint8_t out[KUI_CAPTURE_CHUNK*KUI_RAW_BYTES],expected[sizeof(out)];
    memset(out,0xd3,sizeof(out));reset();capture_phase();
    fake.fail=true;fake.failure_prefix=5*KUI_RAW_BYTES+10;
    fake.failure_size=5*KUI_RAW_BYTES+10;fake.failure_detail=2;
    bool accepted=false;
    if(!strcmp(mode,"salvage-partial-medium"))accepted=true;
    else if(!strcmp(mode,"salvage-partial-size-untrusted")) {fake.failure_size=SIZE_MAX;accepted=true;}
    else if(!strcmp(mode,"salvage-partial-empty")) {fake.failure_prefix=0;fake.failure_size=10;accepted=true;}
    else if(!strcmp(mode,"salvage-partial-normal-raw")) {
        assert(kui_disc_read_raw(NULL,173622,32,out)==KUI_READ_RETRY);
        unchanged(out,sizeof(out));expect("RAW retry FAD=173622");
        printf("PASS salvage partial policy: %s\n",mode);return 0;
    } else if(!strcmp(mode,"salvage-partial-other-sense"))fake.failure_sense=5;
    else if(!strcmp(mode,"salvage-partial-not-found"))fake.terminal_not_found=true;
    else if(!strcmp(mode,"salvage-partial-submit-fail"))fake.submit_fail=true;
    else if(!strcmp(mode,"salvage-partial-timeout")) {
        fake.fail=false;fake.timeout=fake.abort_medium=true;
    } else if(!strcmp(mode,"salvage-partial-cancel")) {
        fake.fail=false;fake.timeout=fake.abort_medium=fake.cancel_on_pass=true;
    } else if(!strcmp(mode,"salvage-partial-unknown-abort")) {
        fake.fail=false;fake.streaming_failure=fake.abort_medium=true;
    } else if(!strcmp(mode,"salvage-partial-guard"))fake.guard=1;
    else {assert(!strcmp(mode,"salvage-partial-media"));fake.changed=true;}
    enum kui_read_result result=kui_disc_read_salvage(NULL,173622,32,out);
    if(accepted) {
        assert(result==KUI_READ_PARTIAL && fake.reads==1 && !fake.aborts);
        memset(expected,0xa5,sizeof(expected));
        kui_pattern(expected,(uint64_t)173622*KUI_RAW_BYTES,fake.failure_prefix);
        assert(!memcmp(out,expected,sizeof(out)));
        expect("RAW partial FAD=173622 sectors=32");expect("poll=-1");
        kui_disc_timing_report();expect("opt requests partial=1; tentative bytes need sector checks");
        /* The next ordinary read is independent, with no error-state latch. */
        fake.fail=false;assert(kui_disc_read_salvage(NULL,173654,32,out)==KUI_READ_OK);
        kui_pattern(expected,(uint64_t)173654*KUI_RAW_BYTES,sizeof(expected));assert(!memcmp(out,expected,sizeof(out)));
    } else {
        bool fatal=fake.guard || fake.changed || fake.cancel_on_pass;
        assert(result==(fatal?KUI_READ_FATAL:KUI_READ_RETRY));unchanged(out,sizeof(out));
        if(fake.guard)expect("CAPTURE GUARD CORRUPTION: RESET REQUIRED");
        if(fake.timeout && !fake.cancel_on_pass)expect("CMD 16 TIMEOUT");
        if(fake.cancel_on_pass)expect("CMD 16 CANCELLED");
        if(fake.submit_fail)expect("poll=-2");
        if(fake.terminal_not_found)expect("poll=0");
        if(fake.streaming_failure)assert(fake.aborts==1);
    }
    printf("PASS salvage partial policy: %s\n",mode);return 0;
}
static int run_salvage_policy(const char *mode) {
    if(!strncmp(mode,"salvage-partial-",16))return run_salvage_partial(mode);
    uint8_t out[KUI_CAPTURE_CHUNK*KUI_RAW_BYTES],expected[sizeof(out)];
    memset(out,0xd3,sizeof(out));reset();capture_phase();
    if(!strcmp(mode,"salvage-slow")) {
        fake.complete_after_us=6000000;
        assert(kui_disc_read_salvage(NULL,45150,2,out)==KUI_READ_OK);
        assert(fake.us>=6000000 && fake.us<6020000 && fake.reads==1 && !fake.aborts);
        kui_pattern(expected,(uint64_t)45150*KUI_RAW_BYTES,2*KUI_RAW_BYTES);
        assert(!memcmp(out,expected,2*KUI_RAW_BYTES));
        /* The same delayed command still reaches the normal five-second bound. */
        memset(out,0xd3,sizeof(out));reset();capture_phase();fake.complete_after_us=6000000;
        assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_RETRY);
        assert(fake.us>=5000000 && fake.us<5020000 && fake.aborts==1);unchanged(out,sizeof(out));
        expect("read_ms=5000 abort_ms=1000 poll=0");
    } else if(!strcmp(mode,"salvage-medium-delay")) {
        fake.complete_after_us=12000000;fake.fail=true;
        assert(kui_disc_read_salvage(NULL,173622,32,out)==KUI_READ_PARTIAL);
        assert(fake.us>=12000000 && fake.us<12020000 && !fake.aborts);
        for(unsigned i=0;i<sizeof(out);++i)assert(out[i]==0xa5);
        expect("RAW partial FAD=173622 sectors=32 elapsed_ms=120");
        expect("read_ms=30000 abort_ms=5000 poll=-1");expect("sense=3/0");
        fake.complete_after_us=0;fake.fail=false;
        assert(kui_disc_read_salvage(NULL,173622,1,out)==KUI_READ_OK);
    } else if(!strcmp(mode,"salvage-drain")) {
        fake.timeout=true;fake.abort_after_us=2000000;
        assert(kui_disc_read_salvage(NULL,173622,32,out)==KUI_READ_RETRY);
        assert(fake.us>=32000000 && fake.us<32030000 && fake.aborts==1);unchanged(out,sizeof(out));
        expect("read_ms=30000 abort_ms=5000 poll=0");expect("CMD 16 TIMEOUT");
        fake.timeout=false;fake.abort_after_us=0;
        assert(kui_disc_read_salvage(NULL,173622,1,out)==KUI_READ_OK);
    } else if(!strcmp(mode,"salvage-cancel")) {
        fake.timeout=fake.cancel_on_pass=true;fake.abort_after_us=2000000;
        assert(kui_disc_read_salvage(NULL,173622,32,out)==KUI_READ_FATAL);
        assert(fake.stop && fake.us>=2000000 && fake.us<2040000 && fake.aborts==1);unchanged(out,sizeof(out));
        expect("CMD 16 CANCELLED");
        fake.stop=fake.timeout=fake.cancel_on_pass=false;fake.abort_after_us=0;
        assert(kui_disc_read_salvage(NULL,173622,1,out)==KUI_READ_OK);
    } else if(!strcmp(mode,"salvage-mode-fail")) {
        fake.mode_fail=true;
        assert(kui_disc_read_salvage(NULL,173622,32,out)==KUI_READ_FATAL);
        assert(!fake.reads && !fake.aborts);unchanged(out,sizeof(out));
        expect("read_ms=30000 abort_ms=5000; no read command issued");
    } else {
        bool cancelling=!strcmp(mode,"salvage-cancel-abort-fail");
        if(!strcmp(mode,"salvage-abort-fail") || cancelling) {
            fake.timeout=fake.abort_fail=true;fake.cancel_on_pass=cancelling;
            assert(kui_disc_read_salvage(NULL,173622,32,out)==KUI_READ_FATAL);
            if(cancelling)assert(fake.us>=5000000 && fake.us<5040000);
            else assert(fake.us>=35000000 && fake.us<35030000);
            assert(fake.aborts==1);expect("CMD 16 ABORT FAILED: RESET REQUIRED");
            expect("read_ms=30000 abort_ms=5000 poll=1");
            fake.stop=fake.timeout=fake.abort_fail=fake.cancel_on_pass=false;
        } else if(!strcmp(mode,"salvage-guard")) {
            fake.guard=1;
            assert(kui_disc_read_salvage(NULL,173622,32,out)==KUI_READ_FATAL);
            assert(fake.reads==1 && !fake.aborts);expect("CAPTURE GUARD CORRUPTION: RESET REQUIRED");
            fake.guard=0;
        } else {
            assert(!strcmp(mode,"salvage-media-change"));fake.changed=true;
            assert(kui_disc_read_salvage(NULL,173622,32,out)==KUI_READ_FATAL);
            assert(!fake.aborts);fake.changed=false;
        }
        unchanged(out,sizeof(out));unsigned sends=fake.sends,modes=fake.modes;
        assert(kui_disc_read_salvage(NULL,173622,1,out)==KUI_READ_FATAL);
        assert(fake.sends==sends && fake.modes==modes);unchanged(out,sizeof(out));
        const struct kui_capture_plan plan={.count=1};struct kui_toc tocs[2];
        assert(!kui_disc_recover(NULL,&plan) && fake.sends==sends);
        /* A true failed abort/guard cannot be cleared through Prepare either. */
        if(strcmp(mode,"salvage-media-change"))assert(!kui_disc_prepare(tocs) && fake.sends==sends);
    }
    printf("PASS salvage optical policy: %s\n",mode);return 0;
}
static void salvage_policy_subprocesses(void) {
    const char *cases[]={"salvage-slow","salvage-medium-delay","salvage-drain","salvage-cancel",
        "salvage-mode-fail","salvage-abort-fail","salvage-cancel-abort-fail","salvage-guard","salvage-media-change",
        "salvage-partial-medium","salvage-partial-size-untrusted","salvage-partial-empty","salvage-partial-normal-raw",
        "salvage-partial-other-sense","salvage-partial-not-found","salvage-partial-submit-fail","salvage-partial-timeout",
        "salvage-partial-cancel","salvage-partial-unknown-abort","salvage-partial-guard","salvage-partial-media"};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i) {
        pid_t child=fork();assert(child>=0);
        if(!child) {execl("./build/test-disc","./build/test-disc",cases[i],(char *)NULL);_exit(127);}
        int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    }
}
static void check_refused(uint8_t *out) {
    unsigned sends=fake.sends;kui_disc_timing_reset();fake.stop=false;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL && fake.sends==sends);
    const struct kui_capture_plan expected={.count=1};
    assert(!kui_disc_recover(NULL,&expected) && fake.sends==sends);
}
/* The bench's GD-ROM DMA probe against the fake firmware. Each failure mode that turns DMA off
 * runs as its own process (like abort-fail above): the state it leaves is meant to persist. */
/* The split DMA read's failure policy. Each mode is its own process, so disc.c's latches start
 * clean. The bug these pin down was seen on hardware: a lid-open left DMA off for the rest of
 * the boot, and a same-boot resume ran at PIO speed. */
static int run_dma_async(const char *mode) {
    static _Alignas(32) uint8_t buf[32*KUI_RAW_BYTES];
    reset();
    if(!strcmp(mode,"dma-async-stop")) {
        assert(kui_disc_read_begin(NULL,45150,32,buf));
        unsigned sends=fake.sends;
        assert(!kui_disc_stop() && fake.sends==sends && !fake.bus_inits);
        const struct kui_capture_plan expected={.count=1};struct kui_toc tocs[2];
        assert(!kui_disc_recover(NULL,&expected) && fake.sends==sends);
        assert(!kui_disc_prepare(tocs) && fake.sends==sends);
        assert(kui_disc_read_raw(NULL,45150,1,buf)==KUI_READ_FATAL && fake.sends==sends);
        fake.stop=true;                                   /* B pressed with a read in flight */
        assert(kui_disc_read_end(NULL)==KUI_READ_FATAL);
        fake.stop=false;
        assert(!strstr(fake.log,"DMA stays off"));
        assert(kui_disc_read_begin(NULL,45150,32,buf));   /* DMA is still there for the resume */
        assert(kui_disc_read_end(NULL)==KUI_READ_OK);
    } else if(!strcmp(mode,"dma-async-change")) {
        assert(kui_disc_read_begin(NULL,45150,32,buf));
        fake.changed=true;                                /* the lid opened */
        assert(kui_disc_read_end(NULL)==KUI_READ_FATAL);
        assert(!strstr(fake.log,"DMA stays off"));
    } else if(!strcmp(mode,"dma-async-fail")) {
        /* One damaged sector: the chunk falls back to PIO and DMA stays on. */
        assert(kui_disc_read_begin(NULL,45150,32,buf));
        fake.fail=true;
        assert(kui_disc_read_end(NULL)==KUI_READ_RETRY);
        assert(strstr(fake.log,"re-reading this chunk with PIO") && !strstr(fake.log,"DMA stays off"));
        fake.fail=false;
        assert(kui_disc_read_begin(NULL,45150,32,buf));
        assert(kui_disc_read_end(NULL)==KUI_READ_OK);      /* a success clears the count */
        /* Three failures in a row on an unchanged disc is DMA misbehaving. */
        for(int k=0;k<3;k++) {
            fake.fail=false;
            assert(kui_disc_read_begin(NULL,45150,32,buf));
            fake.fail=true;
            assert(kui_disc_read_end(NULL)==KUI_READ_RETRY);
        }
        assert(strstr(fake.log,"DMA stays off until reboot"));
        fake.fail=false;
        assert(!kui_disc_read_begin(NULL,45150,32,buf));
    } else {
        assert(!strcmp(mode,"dma-async-timeout"));        /* the drive never finishes: DMA's fault */
        assert(kui_disc_read_begin(NULL,45150,32,buf));
        fake.timeout=true;
        assert(kui_disc_read_end(NULL)!=KUI_READ_OK);
        assert(strstr(fake.log,"DMA stays off until reboot"));
        fake.timeout=false;
        assert(!kui_disc_read_begin(NULL,45150,32,buf));
    }
    if(!strcmp(mode,"dma-async-fail") || !strcmp(mode,"dma-async-timeout")) {
        /* A same-boot Resume resets profiling, then can read successfully by
         * PIO. Neither event certifies the failed DMA path or rearms it. This
         * is distinct from B/door interruptions, which keep DMA available. */
        kui_disc_timing_reset();capture_phase();
        unsigned reads=fake.reads;
        assert(kui_disc_read_raw(NULL,45150,32,buf)==KUI_READ_OK);
        assert(fake.reads==reads+1 && fake.command==CD_CMD_PIOREAD);
        reads=fake.reads;
        assert(!kui_disc_read_begin(NULL,45182,32,buf));
        assert(fake.reads==reads);
    }
    printf("PASS dma async: %s\n",mode);
    return 0;
}
static int run_dma(const char *mode) {
    if(!strncmp(mode,"dma-async",9)) return run_dma_async(mode);
    static uint8_t expected[KUI_OPT_SWEEP_CHUNK_MAX*KUI_RAW_BYTES];
    struct kui_probe_stats st;const uint8_t *data=NULL;
    reset();memset(&st,0,sizeof(st));test_cache_n=0;test_cache_log[0]=0;
    if(!strcmp(mode,"dma")) {
        fake.target_polls=6;
        assert(kui_disc_read_probe_dma(NULL,45150,32,&data,&st)==KUI_READ_OK && data);
        kui_pattern(expected,(uint64_t)45150*KUI_RAW_BYTES,32*KUI_RAW_BYTES);
        assert(!memcmp(data,expected,32*KUI_RAW_BYTES));
        assert(fake.command==CD_CMD_DMAREAD && fake.reads==1 && fake.modes==1);
        assert(fake.params.start_sec==45150 && fake.params.num_sec==32);
        assert(((uintptr_t)fake.params.buffer&31u)==0);         /* the DMA engine needs 32-byte alignment */
        /* The worker sleeps between polls; it never yields in a loop, so the CPU is really free. */
        assert(fake.sleeps>0 && fake.passes==0);
        assert(st.cmd_us>0 && st.last_us==st.cmd_us && st.reported_bytes==32*KUI_RAW_BYTES && st.polls>0);
        /* Cache: the guard pattern purged (3 ranges) and the data range invalidated BEFORE the transfer
         * starts, and the data range invalidated again after it: the whole 2352 bytes of every sector,
         * not the 2048 KOS's own helper would cover. */
        assert(!strcmp(test_cache_log,"PPPIDCI"));
        assert(test_cache_start[3]==(uintptr_t)data && test_cache_bytes[3]==32*KUI_RAW_BYTES);
        assert(test_cache_start[6]==(uintptr_t)data && test_cache_bytes[6]==32*KUI_RAW_BYTES);
        assert(test_cache_bytes[1]==KUI_OPT_SWEEP_CHUNK_MAX*KUI_RAW_BYTES-32*KUI_RAW_BYTES ||
               test_cache_bytes[1]==4096);                      /* the tail guard, bounded */
        /* The largest read, in the same run. */
        test_cache_n=0;test_cache_log[0]=0;
        assert(kui_disc_read_probe_dma(NULL,45150,KUI_OPT_SWEEP_CHUNK_MAX,&data,&st)==KUI_READ_OK);
        kui_pattern(expected,(uint64_t)45150*KUI_RAW_BYTES,KUI_OPT_SWEEP_CHUNK_MAX*KUI_RAW_BYTES);
        assert(!memcmp(data,expected,KUI_OPT_SWEEP_CHUNK_MAX*KUI_RAW_BYTES));
        assert(test_cache_bytes[3]==KUI_OPT_SWEEP_CHUNK_MAX*KUI_RAW_BYTES);
        /* Statistics accumulate across reads; the caller owns them. */
        assert(st.cmd_us>=st.last_us && st.polls>6);
        /* Refusals send nothing: odd counts (a 2352-byte sector is a multiple of 32 only in pairs),
         * zero, too many, bad addresses, missing pointers, and a stop request. */
        unsigned sends=fake.sends;
        assert(kui_disc_read_probe_dma(NULL,45150,31,&data,&st)==KUI_READ_FATAL);
        assert(kui_disc_read_probe_dma(NULL,45150,1,&data,&st)==KUI_READ_FATAL);
        assert(kui_disc_read_probe_dma(NULL,45150,0,&data,&st)==KUI_READ_FATAL);
        assert(kui_disc_read_probe_dma(NULL,45150,KUI_OPT_SWEEP_CHUNK_MAX+2,&data,&st)==KUI_READ_FATAL);
        assert(kui_disc_read_probe_dma(NULL,149,32,&data,&st)==KUI_READ_FATAL);
        assert(kui_disc_read_probe_dma(NULL,0xffffff,32,&data,&st)==KUI_READ_FATAL);
        assert(kui_disc_read_probe_dma(NULL,45150,32,NULL,&st)==KUI_READ_FATAL);
        assert(kui_disc_read_probe_dma(NULL,45150,32,&data,NULL)==KUI_READ_FATAL);
        fake.stop=true;
        assert(kui_disc_read_probe_dma(NULL,45150,32,&data,&st)==KUI_READ_FATAL);
        assert(fake.sends==sends);
        puts("PASS dma probe: data, alignment, sleeping poll, cache order and 2352-byte ranges, refusals");
        return 0;
    }
    /* Everything below leaves DMA off, and a second attempt must not even reach the drive. */
    unsigned sends;
    if(!strcmp(mode,"dma-fail")) {
        fake.fail=true;
        assert(kui_disc_read_probe_dma(NULL,45150,32,&data,&st)==KUI_READ_RETRY);
        assert(strstr(fake.log,"DMA stays off until reboot"));
    } else if(!strcmp(mode,"dma-timeout")) {
        fake.timeout=true;
        assert(kui_disc_read_probe_dma(NULL,45150,32,&data,&st)==KUI_READ_RETRY);
        assert(fake.aborts==1 && fake.us>=5000000 && fake.us<5012000);   /* bounded: never a hang */
        assert(strstr(fake.log,"DMA stays off until reboot"));
    } else {
        assert(!strcmp(mode,"dma-guard"));
        fake.guard=1;   /* the fake drive writes one byte past the request */
        assert(kui_disc_read_probe_dma(NULL,45150,32,&data,&st)==KUI_READ_FATAL);
        assert(strstr(fake.log,"DMA PROBE GUARD CORRUPTION"));
    }
    sends=fake.sends;fake.fail=fake.timeout=false;fake.guard=0;fake.stop=false;
    assert(kui_disc_read_probe_dma(NULL,45150,32,&data,&st)==KUI_READ_FATAL && fake.sends==sends);
    printf("PASS dma probe: %s leaves DMA off and refuses further attempts without touching the drive\n",mode);
    return 0;
}
static void preparation_counts(unsigned queries,unsigned low,unsigned high) {
    assert(fake.drive_queries==queries && fake.toc_reads[0]==low && fake.toc_reads[1]==high);
    assert(fake.sends==1u+low+high && fake.reads==0u && fake.modes==0u && fake.aborts==0u);
    if(low)assert(fake.toc_order[0]==0u);
    if(high)assert(fake.toc_order[1]==1u);
}
static void gd_geometry(const struct kui_toc sessions[2]) {
    assert(sessions[0].count==2u && sessions[1].count==1u);
    assert(sessions[0].tracks[0].number==1u && sessions[0].tracks[0].start==150u &&
           sessions[0].tracks[0].end==650u && sessions[0].tracks[0].control==4u);
    assert(sessions[0].tracks[1].number==2u && sessions[0].tracks[1].start==650u &&
           sessions[0].tracks[1].end==950u && sessions[0].tracks[1].control==0u);
    assert(sessions[1].tracks[0].number==3u && sessions[1].tracks[0].start==45150u &&
           sessions[1].tracks[0].end==51813u && sessions[1].tracks[0].control==4u);
}
static int run_prepare(void) {
    struct kui_toc sessions[2];bool is_cd=true;
    /* Default retail preparation never acquires the opt-in status/query path:
     * even a failing or apparently absent check-drive double is irrelevant. */
    reset();fake.drive_result=1;fake.drive=(cd_check_drive_status_t){CD_STATUS_NO_DISC,CD_CDDA};
    memset(sessions,0xc7,sizeof(sessions));
    assert(kui_disc_prepare(sessions));preparation_counts(0u,1u,1u);gd_geometry(sessions);

    const cd_disc_types_t cds[]={CD_CDDA,CD_CDROM,CD_CDROM_XA};
    const cd_stat_t ready[]={CD_STATUS_PAUSED,CD_STATUS_STANDBY};
    for(unsigned i=0;i<sizeof(cds)/sizeof(cds[0]);i++) for(unsigned j=0;j<sizeof(ready)/sizeof(ready[0]);j++) {
        reset();fake.cd_toc=true;fake.drive=(cd_check_drive_status_t){ready[j],cds[i]};
        memset(sessions,0xc7,sizeof(sessions));is_cd=false;
        assert(kui_disc_prepare_capture(sessions,&is_cd) && is_cd);
        preparation_counts(1u,1u,0u);
        assert(sessions[0].count==1u && sessions[0].tracks[0].number==1u &&
               sessions[0].tracks[0].start==225u && sessions[0].tracks[0].end==865u &&
               sessions[0].tracks[0].control==(cds[i]==CD_CDDA?0u:4u));
        const struct kui_toc blank={0};assert(!memcmp(&sessions[1],&blank,sizeof(blank)));
    }
    /* The optional result pointer does not change the media or TOC contract. */
    reset();fake.cd_toc=true;fake.drive.disc_type=CD_CDROM;
    assert(kui_disc_prepare_capture(sessions,NULL));preparation_counts(1u,1u,0u);

    const cd_stat_t unavailable[]={CD_STATUS_READ_FAIL,CD_STATUS_BUSY,CD_STATUS_PLAYING,
        CD_STATUS_SEEKING,CD_STATUS_SCANNING,CD_STATUS_OPEN,CD_STATUS_NO_DISC,
        CD_STATUS_RETRY,CD_STATUS_ERROR,CD_STATUS_FATAL,(cd_stat_t)42};
    for(unsigned i=0;i<sizeof(cds)/sizeof(cds[0]);i++) for(unsigned j=0;j<sizeof(unavailable)/sizeof(unavailable[0]);j++) {
        reset();fake.drive=(cd_check_drive_status_t){unavailable[j],cds[i]};is_cd=true;
        assert(!kui_disc_prepare_capture(sessions,&is_cd) && !is_cd);
        preparation_counts(1u,0u,0u);expect("CD is not ready");
    }
    const cd_disc_types_t unsupported[]={CD_CDI,CD_FAIL,(cd_disc_types_t)0x40};
    for(unsigned i=0;i<sizeof(unsupported)/sizeof(unsupported[0]);i++) {
        reset();fake.drive.disc_type=unsupported[i];is_cd=true;
        assert(!kui_disc_prepare_capture(sessions,&is_cd) && !is_cd);
        preparation_counts(1u,0u,0u);expect("Unsupported optical media type");
    }
    const int errors[]={-1,1};
    for(unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);i++) {
        reset();fake.drive_result=errors[i];is_cd=true;
        assert(!kui_disc_prepare_capture(sessions,&is_cd) && !is_cd);
        preparation_counts(1u,0u,0u);
    }
    reset();fake.drive.disc_type=CD_GDROM;is_cd=true;
    assert(kui_disc_prepare_capture(sessions,&is_cd) && !is_cd);
    preparation_counts(1u,1u,1u);gd_geometry(sessions);

    reset();fake.fail=true;is_cd=true;
    assert(!kui_disc_prepare_capture(sessions,&is_cd) && !is_cd);
    preparation_counts(0u,0u,0u);expect("Disc initialization did not complete");
    reset();fake.cd_toc=true;fake.drive.disc_type=CD_CDROM;fake.toc_bad=true;
    assert(!kui_disc_prepare_capture(sessions,&is_cd));preparation_counts(1u,1u,0u);
    expect("TOC invalid or unsupported");

    /* Guard failure poisons preparation permanently, so this final case stays
     * in its own process and cannot alter the existing transfer-policy tests. */
    reset();fake.cd_toc=true;fake.drive.disc_type=CD_CDROM;fake.toc_guard=true;
    assert(!kui_disc_prepare_capture(sessions,&is_cd));preparation_counts(1u,1u,0u);
    expect("TOC GUARD CORRUPTION: RESET REQUIRED");
    unsigned sends=fake.sends,queries=fake.drive_queries;
    fake.toc_guard=false;is_cd=true;
    assert(!kui_disc_prepare_capture(sessions,&is_cd) && !is_cd);
    assert(!kui_disc_prepare(sessions) && fake.sends==sends && fake.drive_queries==queries);
    puts("PASS optical preparation: unchanged default GD path, explicit ready CD TOC, status/errors/types, zero second area, GD opt-in and guarded refusal");
    return 0;
}
static void prepare_subprocess(void) {
    /* Run by the ordinary no-argument test gate, while preserving disc.c's
     * static INIT/poison latches for all pre-existing transfer assertions. */
    pid_t child=fork();assert(child>=0);
    if(!child) {int result=run_prepare();assert(fflush(stdout)==0);_exit(result);}
    int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
}
static struct kui_capture_plan recovery_plan(bool cd,bool audio) {
    struct kui_capture_plan p={0};
    if(cd) {
        p.profile=KUI_CAPTURE_PROFILE_CD_TOC;p.count=1;
        p.tracks[0]=(struct kui_capture_track){1,audio?0u:4u,0,225,865,865};
    } else {
        p.count=3;p.tracks[0]=(struct kui_capture_track){1,4,0,150,500,650};
        p.tracks[1]=(struct kui_capture_track){2,0,0,650,950,950};
        p.tracks[2]=(struct kui_capture_track){3,4,1,45150,51813,51813};
    }
    for(unsigned i=0;i<p.count;++i)p.bytes+=(uint64_t)(p.tracks[i].end-p.tracks[i].start)*KUI_RAW_BYTES;
    return p;
}
static int run_recovery(const char *mode) {
    static _Alignas(32) uint8_t out[32*KUI_RAW_BYTES];
    bool cd=!strcmp(mode,"recover-cd") || !strcmp(mode,"recover-audio");
    bool audio=!strcmp(mode,"recover-audio");
    struct kui_capture_plan p=recovery_plan(cd,audio);
    reset();capture_phase();
    if(cd) {fake.cd_toc=true;fake.drive.disc_type=audio?CD_CDDA:CD_CDROM;}
    if(!strcmp(mode,"recover-abort-fail")) {
        fake.timeout=fake.abort_fail=true;
        assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL);
        unsigned sends=fake.sends;fake.timeout=fake.abort_fail=false;
        assert(!kui_disc_recover(NULL,&p) && fake.sends==sends && !fake.bus_inits);
    } else if(!strcmp(mode,"recover-timeout")) {
        fake.timeout=true;
        assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_RETRY && fake.aborts==1);
        unsigned reads=fake.reads;fake.timeout=false;
        assert(kui_disc_recover(NULL,&p));
        assert(fake.reads==reads && fake.toc_reads[0]==1 && fake.toc_reads[1]==1);
        assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_OK);
    } else if(!strcmp(mode,"recover-init-timeout")) {
        fake.timeout=true;uint64_t start=fake.us;
        assert(!kui_disc_recover(NULL,&p));
        assert(fake.us-start>=10000000 && fake.us-start<10012000 && fake.aborts==1);
        assert(fake.toc_count==0 && fake.reads==0);
        fake.timeout=false;
        assert(kui_disc_recover(NULL,&p)); /* aborted INIT released ownership */
    } else if(!strcmp(mode,"recover-layout")) {
        ++p.tracks[2].end;p.bytes+=KUI_RAW_BYTES;
        assert(!kui_disc_recover(NULL,&p));unsigned sends=fake.sends;
        assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL && fake.sends==sends);
        assert(!kui_disc_recover(NULL,&p) && fake.sends==sends);
    } else if(!strcmp(mode,"recover-profile")) {
        fake.cd_toc=true;fake.drive.disc_type=CD_CDROM;
        assert(!kui_disc_recover(NULL,&p));unsigned sends=fake.sends;
        assert(!kui_disc_recover(NULL,&p) && fake.sends==sends);
    } else if(!strcmp(mode,"recover-cancel")) {
        fake.stop=true;assert(!kui_disc_recover(NULL,&p) && fake.sends==0 && !fake.bus_inits);
    } else if(!strcmp(mode,"recover-command-live")) {
        fake.recovery_during_command=true;
        assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_OK);
        assert(fake.sends==1 && !fake.bus_inits && !fake.recovery_during_command);
    } else if(!strcmp(mode,"recover-init-abort-fail")) {
        fake.timeout=fake.abort_fail=true;assert(!kui_disc_recover(NULL,&p));
        unsigned sends=fake.sends;fake.timeout=fake.abort_fail=false;
        assert(!kui_disc_recover(NULL,&p) && fake.sends==sends);
    } else if(!strcmp(mode,"recover-dma-disabled")) {
        assert(kui_disc_read_begin(NULL,45150,32,out));fake.timeout=true;
        assert(kui_disc_read_end(NULL)==KUI_READ_RETRY);fake.timeout=false;
        assert(kui_disc_recover(NULL,&p));
        assert(!kui_disc_read_begin(NULL,45150,32,out)); /* no silent DMA rearm */
        assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_OK);
    } else {
        assert(!strcmp(mode,"recover-gd") || cd);
        assert(kui_disc_recover(NULL,&p));
        preparation_counts(1,1,cd?0u:1u);
        assert(fake.bus_inits==1 && !fake.modes && !fake.reads);
    }
    printf("PASS drive recovery: %s\n",mode);return 0;
}
static void recovery_subprocesses(void) {
    const char *cases[]={"recover-gd","recover-cd","recover-audio","recover-timeout",
        "recover-init-timeout","recover-layout","recover-profile","recover-cancel","recover-command-live",
        "recover-abort-fail","recover-init-abort-fail","recover-dma-disabled"};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        pid_t child=fork();assert(child>=0);
        if(!child) {int result=run_recovery(cases[i]);assert(fflush(stdout)==0);_exit(result);}
        int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    }
}
static bool same_mode(const struct kui_drive_mode *a,const struct kui_drive_mode *b) {
    return a->speed==b->speed && a->standby==b->standby &&
        a->read_flags==b->read_flags && a->read_retry==b->read_retry;
}
static int run_controls(const char *mode) {
    static _Alignas(32) uint8_t out[32*KUI_RAW_BYTES];
    struct kui_toc sessions[2];struct kui_drive_options options={.speed=4,.retries=3};
    const struct kui_drive_mode original={6,12345,0xd9,4};
    reset();assert(kui_disc_prepare(sessions));fake.toc_count=0;
    fake.hardware_mode=original;unsigned sends=fake.sends;
    if(!strcmp(mode,"controls-default")) {
        options=(struct kui_drive_options){0};
        assert(kui_disc_controls_begin(&options) && fake.sends==sends);
        assert(kui_disc_prepare(sessions));
        assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_OK);
        assert(kui_disc_controls_end() && !fake.mode_queries && !fake.mode_sets);
        assert(same_mode(&fake.hardware_mode,&original));
    } else if(!strncmp(mode,"controls-format-",16)) {
        assert(kui_disc_controls_begin(&options) && fake.mode_queries==2 && fake.mode_sets==1);
        capture_phase();memset(out,0xd3,sizeof(out));
        bool success=!strcmp(mode,"controls-format-retained");
        bool fatal=!strcmp(mode,"controls-format-guard") || !strcmp(mode,"controls-format-abort-fail");
        if(!strcmp(mode,"controls-format-speed"))fake.mode_format_change=1;
        else if(!strcmp(mode,"controls-format-standby"))fake.mode_format_change=2;
        else if(!strcmp(mode,"controls-format-flags"))fake.mode_format_change=3;
        else if(!strcmp(mode,"controls-format-retries"))fake.mode_format_change=4;
        else if(!strcmp(mode,"controls-format-query-fail"))fake.mode_fail_command=CD_CMD_REQ_MODE;
        else if(!strcmp(mode,"controls-format-guard"))fake.mode_guard=1;
        else if(!strcmp(mode,"controls-format-abort-fail")) {fake.mode_timeout_command=CD_CMD_REQ_MODE;fake.abort_fail=true;}
        else assert(success);
        assert(kui_disc_read_raw(NULL,45150,2,out)==(success?KUI_READ_OK:KUI_READ_FATAL));
        assert(fake.mode_queries==3 && fake.reads==(success?1u:0u));
        sends=fake.sends;unsigned queries=fake.mode_queries,modes=fake.modes;
        fake.mode_format_change=fake.mode_fail_command=fake.mode_guard=fake.mode_timeout_command=0;fake.abort_fail=false;
        if(success) {
            expect("after sector format: firmware tuple retained");
            assert(kui_disc_read_raw(NULL,45152,2,out)==KUI_READ_OK && fake.mode_queries==queries);
            struct kui_capture_plan plan=recovery_plan(false,false);fake.mode_init_reset=true;
            assert(kui_disc_recover(NULL,&plan));assert(fake.mode_queries==queries+1);
            queries=fake.mode_queries;
            assert(kui_disc_read_raw(NULL,45154,2,out)==KUI_READ_OK && fake.mode_queries==queries+1);
            assert(kui_disc_read_raw(NULL,45156,2,out)==KUI_READ_OK && fake.mode_queries==queries+1);
        } else {
            unchanged(out,sizeof(out));
            assert(kui_disc_read_raw(NULL,45152,2,out)==KUI_READ_FATAL);
            assert(fake.sends==sends && fake.mode_queries==queries && fake.modes==modes);
        }
        assert(kui_disc_controls_end()==!fatal);
        if(fatal) {
            assert(fake.sends==sends && !kui_disc_prepare(sessions));
            assert(kui_disc_read_raw(NULL,45152,2,out)==KUI_READ_FATAL && fake.sends==sends);
        } else {
            assert(same_mode(&fake.hardware_mode,&original));
            queries=fake.mode_queries;
            assert(kui_disc_read_raw(NULL,45158,2,out)==KUI_READ_OK && fake.mode_queries==queries);
        }
    } else if(!strcmp(mode,"controls-cancel")) {
        fake.stop=true;assert(!kui_disc_controls_begin(&options));
        assert(kui_disc_controls_end() && fake.sends==sends);
    } else if(!strcmp(mode,"controls-live")) {
        fake.controls_during_command=true;
        assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_OK);
        assert(!fake.controls_during_command && !fake.mode_queries && !fake.mode_sets);
        assert(kui_disc_read_begin(NULL,45150,32,out));sends=fake.sends;
        assert(!kui_disc_controls_begin(&options) && fake.sends==sends);
        assert(kui_disc_read_end(NULL)==KUI_READ_OK && kui_disc_controls_end());
    } else if(!strcmp(mode,"controls-query-fail") || !strcmp(mode,"controls-query-short") ||
              !strncmp(mode,"controls-width-",15)) {
        if(!strcmp(mode,"controls-query-fail"))fake.mode_fail_command=CD_CMD_REQ_MODE;
        else if(!strcmp(mode,"controls-query-short"))fake.mode_short=CD_CMD_REQ_MODE;
        else if(!strcmp(mode,"controls-width-speed"))fake.hardware_mode.speed=8;
        else if(!strcmp(mode,"controls-width-standby"))fake.hardware_mode.standby=65536;
        else if(!strcmp(mode,"controls-width-flags"))fake.hardware_mode.read_flags=256;
        else {assert(!strcmp(mode,"controls-width-retries"));fake.hardware_mode.read_retry=256;}
        assert(!kui_disc_controls_begin(&options) && fake.mode_queries==1 && !fake.mode_sets);
        fake.mode_fail_command=fake.mode_short=0;fake.hardware_mode=original;
        assert(kui_disc_controls_end());
        assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_OK);
    } else if(!strcmp(mode,"controls-query-timeout") || !strcmp(mode,"controls-set-timeout")) {
        fake.mode_timeout_command=!strcmp(mode,"controls-query-timeout")?CD_CMD_REQ_MODE:CD_CMD_SET_MODE;
        uint64_t start=fake.us;
        assert(!kui_disc_controls_begin(&options) && fake.aborts==1);
        assert(fake.us-start>=5000000 && fake.us-start<5100000);
        fake.mode_timeout_command=0;assert(kui_disc_controls_end());
        assert(same_mode(&fake.hardware_mode,&original));
        assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_OK);
    } else if(!strcmp(mode,"controls-query-abort-fail") || !strcmp(mode,"controls-set-abort-fail") ||
              !strcmp(mode,"controls-guard-before") || !strcmp(mode,"controls-guard-after")) {
        bool guard=!strncmp(mode,"controls-guard-",15);
        if(guard)fake.mode_guard=!strcmp(mode,"controls-guard-before")?2:1;
        else {fake.mode_timeout_command=!strcmp(mode,"controls-query-abort-fail")?CD_CMD_REQ_MODE:CD_CMD_SET_MODE;fake.abort_fail=true;}
        uint64_t start=fake.us;assert(!kui_disc_controls_begin(&options));
        if(!guard)assert(fake.us-start>=10000000 && fake.us-start<10100000 && fake.aborts==1);
        sends=fake.sends;fake.mode_timeout_command=fake.mode_guard=0;fake.abort_fail=false;
        assert(!kui_disc_controls_end() && !kui_disc_controls_begin(&options));
        assert(!kui_disc_controls_end() && !kui_disc_prepare(sessions) && !kui_disc_stop());
        assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL && fake.sends==sends);
        if(guard)expect("DRIVE MODE GUARD CORRUPTION: RESET REQUIRED");
    } else if(!strcmp(mode,"controls-rejected") || !strcmp(mode,"controls-ignored") ||
              !strcmp(mode,"controls-set-short")) {
        if(!strcmp(mode,"controls-rejected"))fake.mode_fail_on_set=1;
        else if(!strcmp(mode,"controls-ignored"))fake.mode_ignore_sets=1;
        else fake.mode_short=CD_CMD_SET_MODE;
        assert(!kui_disc_controls_begin(&options) && fake.mode_sets==1);
        fake.mode_short=0;assert(kui_disc_controls_end());
        assert(fake.mode_sets==2 && same_mode(&fake.hardware_mode,&original));
        assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_OK);
    } else {
        if(!strcmp(mode,"controls-speed-only"))options.retries=0;
        else if(!strcmp(mode,"controls-retry-only"))options.speed=0;
        assert(kui_disc_controls_begin(&options));
        struct kui_drive_mode requested=original;
        if(options.speed)requested.speed=3;
        if(options.retries)requested.read_retry=2;
        assert(fake.mode_queries==2 && fake.mode_sets==1 && same_mode(&fake.hardware_mode,&requested));
        expect("physical speed not measured");expect("flags=d9");
        sends=fake.sends;assert(!kui_disc_controls_begin(&options) && fake.sends==sends);
        if(!strcmp(mode,"controls-reapply") || !strcmp(mode,"controls-reapply-ignored")) {
            fake.mode_init_reset=true;
            if(!strcmp(mode,"controls-reapply-ignored"))fake.mode_ignore_sets=2;
            struct kui_capture_plan plan=recovery_plan(false,false);
            bool ok=kui_disc_recover(NULL,&plan);
            assert(ok==!strcmp(mode,"controls-reapply"));
            assert(fake.mode_sets==2 && fake.mode_queries==3);
            if(ok) {
                assert(same_mode(&fake.hardware_mode,&requested));
                assert(fake.toc_reads[0]==2 && fake.toc_reads[1]==2);
            } else assert(fake.toc_reads[0]==1 && fake.toc_reads[1]==1);
        }
        if(!strcmp(mode,"controls-restore-fail")) {
            fake.mode_fail_on_set=fake.mode_sets+1;
            assert(!kui_disc_controls_end());sends=fake.sends;fake.mode_fail_on_set=0;
            assert(!kui_disc_prepare(sessions) && !kui_disc_stop());
            assert(!kui_disc_controls_begin(&options) && !kui_disc_controls_end() && fake.sends==sends);
        } else if(!strcmp(mode,"controls-media-change")) {
            fake.changed=true;assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL);
            sends=fake.sends;fake.changed=false;
            assert(!kui_disc_controls_end() && fake.sends==sends);
            assert(!kui_disc_prepare(sessions) && !kui_disc_stop() && fake.sends==sends);
        } else {
            fake.stop=true;assert(kui_disc_controls_end()); /* latched B cannot skip restore */
            assert(same_mode(&fake.hardware_mode,&original));
            sends=fake.sends;assert(kui_disc_controls_end() && fake.sends==sends);
        }
    }
    printf("PASS native drive controls: %s\n",mode);return 0;
}
static void controls_subprocesses(void) {
    const char *cases[]={"controls-default","controls-apply","controls-speed-only","controls-retry-only",
        "controls-cancel","controls-live","controls-query-fail","controls-query-short","controls-width-speed",
        "controls-width-standby","controls-width-flags","controls-width-retries","controls-query-timeout",
        "controls-set-timeout","controls-query-abort-fail","controls-set-abort-fail","controls-guard-before",
        "controls-guard-after","controls-rejected","controls-ignored","controls-set-short","controls-reapply",
        "controls-reapply-ignored","controls-restore-fail","controls-media-change",
        "controls-format-retained","controls-format-speed","controls-format-standby","controls-format-flags",
        "controls-format-retries","controls-format-query-fail","controls-format-guard","controls-format-abort-fail"};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
        pid_t child=fork();assert(child>=0);
        if(!child) {int result=run_controls(cases[i]);assert(fflush(stdout)==0);_exit(result);}
        int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    }
}
int main(int argc,char **argv) {
    uint8_t out[KUI_CAPTURE_CHUNK*KUI_RAW_BYTES],expected[sizeof(out)];
    if(argc==2 && !strcmp(argv[1],"prepare"))return run_prepare();
    if(argc==2 && !strncmp(argv[1],"recover-",8))return run_recovery(argv[1]);
    if(argc==2 && !strncmp(argv[1],"salvage-",8))return run_salvage_policy(argv[1]);
    if(argc==2 && !strncmp(argv[1],"dma",3)) return run_dma(argv[1]);
    if(argc==2 && !strncmp(argv[1],"controls-",9))return run_controls(argv[1]);
    if(argc==1) {prepare_subprocess();recovery_subprocesses();salvage_policy_subprocesses();controls_subprocesses();}
    memset(out,0xd3,sizeof(out));reset();
    if(argc==2) {
        capture_phase();
        if(!strcmp(argv[1],"stop-abort-fail")) {
            fake.timeout=fake.abort_fail=fake.stop=true;
            assert(!kui_disc_stop());
            assert(fake.command==CD_CMD_STOP && fake.reads==0 && fake.modes==0 && fake.bus_inits==1);
            assert(fake.aborts==1 && fake.us>=4000000 && fake.us<4024000);
            unsigned sends=fake.sends,bus=fake.bus_inits;
            fake.stop=fake.timeout=fake.abort_fail=false;
            assert(!kui_disc_stop() && fake.sends==sends && fake.bus_inits==bus);
            struct kui_toc sessions[2];
            assert(!kui_disc_prepare(sessions) && fake.sends==sends && fake.bus_inits==bus);
            puts("PASS STOP: bounded failed abort remains poisoned; no reset, INIT, read or retry");return 0;
        } else if(!strcmp(argv[1],"abort-fail")) {
            fake.timeout=fake.abort_fail=true;
            assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL);
            assert(fake.aborts==1 && fake.us>=6000000 && fake.us<6024000);
            assert(fake.passes>0 && fake.sleeps==0);
            kui_disc_timing_report();expect("abort_failed=1");
        } else if(!strcmp(argv[1],"media-change")) {
            fake.changed=true;
            assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL);
        } else {
            assert(!strcmp(argv[1],"guard-failed") || !strcmp(argv[1],"guard-before"));
            fake.guard=!strcmp(argv[1],"guard-before")?2:1;
            fake.fail=fake.guard==1;
            assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL);
            kui_disc_timing_report();expect("opt mismatch=0 guards=1 refused=0");
            if(fake.fail) expect("opt read1 ok=0 failed=1 timeout=0");
        }
        unchanged(out,sizeof(out));check_refused(out);
        puts("PASS optical: failed abort/media change/guard stays refused after profile reset");return 0;
    }

    /* Startup STOP does not discover/read the boot disc. Holding B cannot
     * cancel required cleanup; both command and abort have finite deadlines. */
    fake.stop=true;
    assert(kui_disc_stop());
    assert(fake.sends==1 && fake.command==CD_CMD_STOP && fake.bus_inits==1);
    assert(fake.reads==0 && fake.modes==0 && fake.aborts==0 && fake.sleeps>0);
    fake.timeout=true;fake.cancel_on_sleep=true;
    uint64_t stop_start=fake.us;
    assert(!kui_disc_stop());
    assert(fake.sends==2 && fake.bus_inits==1 && fake.reads==0 && fake.modes==0);
    assert(fake.aborts==1 && fake.us-stop_start>=3000000 && fake.us-stop_start<3012000);
    reset();

    /* Identification stays paired and byte-identical; short service commands
     * finish without a scheduler tick. Counters remain exclusive and exact. */
    kui_pattern(expected,(uint64_t)45150*KUI_RAW_BYTES,sizeof(expected));
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_OK && !memcmp(out,expected,2*KUI_RAW_BYTES));
    assert(fake.reads==2 && fake.modes==1 && fake.us==195);
    assert(fake.sleeps==0 && fake.passes==0);
    kui_disc_timing_report();
    expect("OPTICAL setup:");expect("opt policy=paired PIO yield_us=2000");
    expect("opt wall_us=195 requests=1 bytes=4704");expect("opt mode_us=7 calls=1 failed=0");
    expect("opt read1 us=68 max_us=68");expect("opt read2 us=120 max_us=120");
    expect("opt read1 submit_us=16 submits=1");expect("opt read1 poll_us=52 polls=2");
    expect("opt read1 wait_us=0 waits=0");expect("opt read2 wait_us=0 waits=0");
    expect("opt read2 max_fad=45150 max_sectors=2");
    capture_phase();fake.used=0;fake.log[0]=0;
    assert(kui_disc_read_raw(NULL,45152,1,out)==KUI_READ_OK && fake.reads==3);
    kui_pattern(expected,(uint64_t)45152*KUI_RAW_BYTES,KUI_RAW_BYTES);
    assert(!memcmp(out,expected,KUI_RAW_BYTES));
    kui_disc_timing_report();expect("OPTICAL capture:");expect("opt policy=single PIO yield_us=2000");
    expect("requests=1 bytes=2352");expect("requests=1 bytes=4704");
    expect("opt read2 calls=0 bytes=0"); /* capture has no routine duplicate */

    /* Emulate the observed ~67 service calls per 32-sector transfer. Even if
     * each scheduled pass costs 8 ms, yields must depend on elapsed service
     * time rather than one tick per busy firmware response. This is not a
     * prediction of physical-drive throughput. */
    reset();capture_phase();fake.target_polls=67;fake.poll_us=350;
    assert(kui_disc_read_raw(NULL,45150,KUI_CAPTURE_CHUNK,out)==KUI_READ_OK);
    kui_pattern(expected,(uint64_t)45150*KUI_RAW_BYTES,sizeof(expected));
    assert(!memcmp(out,expected,sizeof(out)) && fake.reads==1 && fake.total_polls==67);
    assert(fake.sleeps==0 && fake.passes>=8 && fake.passes<=12);
    assert(fake.max_service_us>=2000 && fake.max_service_us<2500);
    uint64_t old_sleep_cost=66u*8000u+67u*353u+16u+7u;
    assert(fake.us<old_sleep_cost/2);
    kui_disc_timing_report();expect("opt read1 calls=1 bytes=75264");expect("opt read2 calls=0 bytes=0");

    reset();capture_phase();fake.submit_busy=true;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_OK && fake.reads==1);
    kui_disc_timing_report();expect("opt read1 submit_us=32 submits=2");expect("opt read1 wait_us=0 waits=0");

    /* A claimed-full but untouched byte remains detectable during paired
     * identification. During single capture, transfer status/length and the
     * data-track EDC check in the capture core provide the applicable checks. */
    memset(out,0xd3,sizeof(out));reset();fake.underfill=true;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_RETRY && fake.reads==2);
    unchanged(out,sizeof(out));kui_disc_timing_report();
    expect("opt mismatch=1 guards=0 refused=0");expect("requests=1 bytes=0");
    const int lengths[]={-1,1,-2*KUI_RAW_BYTES};
    for(unsigned i=0;i<sizeof(lengths)/sizeof(lengths[0]);i++) {
        reset();capture_phase();fake.reported_delta=lengths[i];fake.underfill=lengths[i]<0;
        assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_RETRY && fake.reads==1);
        unchanged(out,sizeof(out));kui_disc_timing_report();
        expect("Transfer size mismatch");expect("opt short=1");expect("opt read1 calls=1 bytes=0");
    }
    reset();capture_phase();fake.fail=true;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_RETRY && fake.reads==1);
    unchanged(out,sizeof(out));kui_disc_timing_report();
    expect("opt read1 ok=0 failed=1 timeout=0");expect("opt read2 calls=0 bytes=0");

    reset();capture_phase();fake.timeout=true;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_RETRY);
    assert(fake.aborts==1 && fake.us>=5000000 && fake.us<5012000);
    assert(fake.passes>0 && fake.sleeps==0);unchanged(out,sizeof(out));
    kui_disc_timing_report();expect("opt read1 ok=0 failed=0 timeout=1");expect("abort_us=11 aborts=1");
    reset();capture_phase();fake.submit_always_busy=true;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_RETRY);
    assert(fake.reads==0 && fake.aborts==0 && fake.us>=5000000 && fake.us<5012000);
    assert(fake.passes>0 && fake.sleeps==0);unchanged(out,sizeof(out));

    reset();capture_phase();fake.cancel=true;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL && fake.aborts==1);
    assert(fake.passes==0);unchanged(out,sizeof(out));
    kui_disc_timing_report();expect("opt read1 cancelled=1 abort_failed=0 invalid=0");
    reset();capture_phase();fake.timeout=fake.cancel_on_pass=true;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL && fake.aborts==1);
    assert(fake.passes==1 && fake.sleeps==0 && fake.us<11000);unchanged(out,sizeof(out));
    kui_disc_timing_report();expect("opt read1 cancelled=1 abort_failed=0 invalid=0");

    /* Fast PIO servicing must not silently change the scheduler behavior of
     * drive initialization and other non-PIO commands. */
    reset();fake.timeout=fake.cancel_on_sleep=true;struct kui_toc sessions[2];
    assert(!kui_disc_prepare(sessions));
    assert(fake.sleeps==1 && fake.passes==0 && fake.aborts==1);

    reset();fake.mode_fail=true;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL && fake.reads==0);
    unchanged(out,sizeof(out));kui_disc_timing_report();expect("opt mode_us=7 calls=1 failed=1");
    reset();fake.stop=true;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL && fake.sends==0);
    unchanged(out,sizeof(out));kui_disc_timing_report();expect("opt mismatch=0 guards=0 refused=1");
    reset();
    assert(kui_disc_read_raw(NULL,149,1,out)==KUI_READ_FATAL);
    assert(kui_disc_read_raw(NULL,0xffffff,2,out)==KUI_READ_FATAL);
    assert(kui_disc_read_raw(NULL,45150,KUI_CAPTURE_CHUNK+1,out)==KUI_READ_FATAL);
    assert(kui_disc_read_raw(NULL,45150,0,out)==KUI_READ_FATAL);
    assert(kui_disc_read_raw(NULL,45150,1,NULL)==KUI_READ_FATAL);
    assert(fake.sends==0);unchanged(out,sizeof(out));

    reset();capture_phase();fake.guard=1;
    assert(kui_disc_read_raw(NULL,45150,KUI_CAPTURE_CHUNK,out)==KUI_READ_FATAL);
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL && fake.reads==1);
    unchanged(out,sizeof(out));kui_disc_timing_report();expect("opt mismatch=0 guards=1 refused=1");
    unsigned sends=fake.sends;
    assert(!kui_disc_stop() && fake.sends==sends && !fake.bus_inits);
    puts("PASS optical: paired identity, single capture, transfer size, bounded fast service, periodic scheduling, busy submit, deadlines, cancel and guards");
    return 0;
}
