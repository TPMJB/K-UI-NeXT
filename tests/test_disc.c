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

static struct {
    uint64_t us,last_handoff,max_service_us;
    unsigned sends,reads,polls,total_polls,sleeps,passes,aborts,modes;
    unsigned target_polls,poll_us,guard;
    int reported_delta,command;
    bool stop,abort_started,submit_busy,submit_always_busy,underfill,fail;
    bool timeout,cancel,abort_fail,mode_fail,changed,cancel_on_pass,cancel_on_sleep;
    cd_read_params_t params;
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
void kui_drive_init_bus(void) {}
void kui_log(const char *format,...) {
    va_list args;va_start(args,format);
    int n=vsnprintf(fake.log+fake.used,sizeof(fake.log)-fake.used,format,args);va_end(args);
    assert(n>=0 && (size_t)n+1<sizeof(fake.log)-fake.used);
    fake.used+=(size_t)n;fake.log[fake.used++]='\n';fake.log[fake.used]=0;
}
int syscall_gdrom_sector_mode(cd_sec_mode_params_t *p) {
    assert(p->size==2352 && p->track_type==0);fake.us+=7;++fake.modes;return fake.mode_fail?-1:0;
}
int syscall_gdrom_send_command(cd_cmd_code_t code,void *params) {
    assert(code==CD_CMD_PIOREAD || code==CD_CMD_DMAREAD || code==CD_CMD_INIT);
    fake.command=code;fake.us+=13;++fake.sends;
    if(fake.submit_busy || fake.submit_always_busy) {fake.submit_busy=false;return 0;}
    fake.polls=0;fake.abort_started=false;
    if(code==CD_CMD_PIOREAD || code==CD_CMD_DMAREAD) {fake.params=*(cd_read_params_t *)params;++fake.reads;}
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
int main(int argc,char **argv) {
    uint8_t out[KUI_CAPTURE_CHUNK*KUI_RAW_BYTES],expected[sizeof(out)];
    if(argc==2 && !strncmp(argv[1],"dma",3)) return run_dma(argv[1]);
    memset(out,0xd3,sizeof(out));reset();
    if(argc==2) {
        capture_phase();
        if(!strcmp(argv[1],"abort-fail")) {
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
    puts("PASS optical: paired identity, single capture, transfer size, bounded fast service, periodic scheduling, busy submit, deadlines, cancel and guards");
    return 0;
}
