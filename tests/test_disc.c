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
    uint64_t us,last_handoff,max_service_us;
    unsigned sends,reads,polls,total_polls,sleeps,passes,aborts,modes,bus_inits;
    unsigned target_polls,poll_us,guard;
    int reported_delta,command;
    bool stop,abort_started,submit_busy,submit_always_busy,underfill,fail;
    bool timeout,cancel,abort_fail,mode_fail,changed,cancel_on_pass,cancel_on_sleep;
    cd_read_params_t params;
    cd_cmd_toc_params_t toc_params;
    cd_check_drive_status_t drive;
    int drive_result;
    unsigned drive_queries,toc_reads[2],toc_order[2],toc_count;
    bool cd_toc,toc_bad,toc_guard;
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
    assert(p->size==2352 && p->track_type==0);fake.us+=7;++fake.modes;return fake.mode_fail?-1:0;
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
           code==CD_CMD_STOP || code==CD_CMD_GETTOC2);
    fake.command=code;fake.us+=13;++fake.sends;
    if(fake.submit_busy || fake.submit_always_busy) {fake.submit_busy=false;return 0;}
    fake.polls=0;fake.abort_started=false;
    if(code==CD_CMD_PIOREAD || code==CD_CMD_DMAREAD) {fake.params=*(cd_read_params_t *)params;++fake.reads;}
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
    if(fake.abort_started) return fake.abort_fail?1:0;
    if(fake.fail || fake.changed) {
        if(fake.command==CD_CMD_PIOREAD || fake.command==CD_CMD_DMAREAD) damage_guard();
        detail->err1=fake.changed?6:3;return -1;
    }
    if(fake.cancel) fake.stop=true;
    if(fake.timeout || fake.cancel) return 1;
    unsigned needed=fake.target_polls?fake.target_polls:((fake.reads&1)?2u:4u);
    if(fake.polls<needed) return 1;
    if(fake.command==CD_CMD_STOP || fake.command==CD_CMD_INIT) {detail->size=0;return 2;}
    if(fake.command==CD_CMD_GETTOC2) {return_toc();detail->size=sizeof(cd_toc_t);return 2;}
    assert(fake.command==CD_CMD_PIOREAD || fake.command==CD_CMD_DMAREAD);
    if(fake.command==CD_CMD_DMAREAD) test_cache_note('C',0,0);   /* the transfer is done */
    size_t bytes=(size_t)fake.params.num_sec*KUI_RAW_BYTES;
    uint8_t *data=fake.params.buffer;
    kui_pattern(data,(uint64_t)fake.params.start_sec*KUI_RAW_BYTES,bytes-(fake.underfill?1:0));
    damage_guard();detail->size=(uint32_t)((int)bytes+fake.reported_delta);return 2;
}
void syscall_gdrom_abort_command(int handle) {assert(handle==1);fake.us+=11;++fake.aborts;fake.abort_started=true;}
static void reset(void) {
    memset(&fake,0,sizeof(fake));fake.poll_us=23;kui_disc_timing_reset();
    fake.drive=(cd_check_drive_status_t){CD_STATUS_PAUSED,CD_GDROM};
}
static void expect(const char *line) {
    if(!strstr(fake.log,line)) {fprintf(stderr,"Missing %s\n%s",line,fake.log);assert(false);}
}
static void unchanged(const uint8_t *out,size_t size) {
    for(size_t i=0;i<size;i++) assert(out[i]==0xd3);
}
static void capture_phase(void) {kui_disc_timing_phase(NULL,true);}
static void check_refused(uint8_t *out) {
    unsigned sends=fake.sends;kui_disc_timing_reset();fake.stop=false;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL && fake.sends==sends);
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
int main(int argc,char **argv) {
    uint8_t out[KUI_CAPTURE_CHUNK*KUI_RAW_BYTES],expected[sizeof(out)];
    if(argc==2 && !strcmp(argv[1],"prepare"))return run_prepare();
    if(argc==2 && !strncmp(argv[1],"dma",3)) return run_dma(argv[1]);
    if(argc==1)prepare_subprocess();
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
