/* SPDX-License-Identifier: GPL-3.0-only */
/* Run the actual Dreamcast optical adapter with a deterministic firmware and
 * scheduler. Check preserved paired-read/guard/deadline behavior and measured
 * first/second command costs, including busy submit, cancellation and faults. */
#include "platform.h"
#include <dc/syscalls.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static struct {
    uint64_t us;
    unsigned sends,reads,polls,sleeps,aborts,modes;
    bool stop,abort_started,submit_busy,underfill,guard,fail,timeout,cancel,abort_fail,mode_fail,changed;
    cd_read_params_t params;
    char log[32768];size_t used;
} fake;
uint64_t timer_ms_gettime64(void) {return fake.us/1000;}
uint64_t timer_us_gettime64(void) {return fake.us;}
void thd_sleep(unsigned ms) {assert(ms==1);fake.us+=(uint64_t)ms*1000;++fake.sleeps;}
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
    assert(code==CD_CMD_PIOREAD);fake.us+=13;++fake.sends;
    if(fake.submit_busy) {fake.submit_busy=false;return 0;}
    fake.params=*(cd_read_params_t *)params;fake.polls=0;fake.abort_started=false;++fake.reads;
    return 1;
}
void syscall_gdrom_exec_server(void) {fake.us+=3;}
int syscall_gdrom_check_command(int handle,cd_cmd_chk_status_t *detail) {
    assert(handle==1);fake.us+=23;++fake.polls;
    if(fake.abort_started) return fake.abort_fail?1:0;
    if(fake.fail || fake.changed) {detail->err1=fake.changed?6:3;return -1;}
    if(fake.cancel) fake.stop=true;
    if(fake.timeout || fake.cancel) return 1;
    if(fake.polls<((fake.reads&1)?2u:4u)) return 1;
    size_t bytes=(size_t)fake.params.num_sec*KUI_RAW_BYTES;
    uint8_t *data=fake.params.buffer;
    kui_pattern(data,(uint64_t)fake.params.start_sec*KUI_RAW_BYTES,bytes-(fake.underfill?1:0));
    if(fake.guard) data[bytes]=0; /* untouched tail must retain its initial fill */
    detail->size=(uint32_t)bytes;return 2;
}
void syscall_gdrom_abort_command(int handle) {assert(handle==1);fake.us+=11;++fake.aborts;fake.abort_started=true;}
static void reset(void) {memset(&fake,0,sizeof(fake));kui_disc_timing_reset();}
static void expect(const char *line) {
    if(!strstr(fake.log,line)) {fprintf(stderr,"Missing %s\n%s",line,fake.log);assert(false);}
}
int main(int argc,char **argv) {
    uint8_t out[2*KUI_RAW_BYTES],expected[sizeof(out)];
    reset();
    if(argc==2) {
        if(!strcmp(argv[1],"abort-fail")) {
            fake.timeout=fake.abort_fail=true;
            assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL);
            assert(fake.aborts==1 && fake.us>=6000000 && fake.us<6010000);
            kui_disc_timing_report();expect("abort_failed=1");
        } else {
            assert(!strcmp(argv[1],"media-change"));fake.changed=true;
            assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL);
        }
        unsigned sends=fake.sends;kui_disc_timing_reset();fake.stop=false;
        assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL && fake.sends==sends);
        puts("PASS optical: failed abort/media change stays refused after profile reset");return 0;
    }
    kui_pattern(expected,(uint64_t)45150*KUI_RAW_BYTES,sizeof(expected));
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_OK && !memcmp(out,expected,sizeof(out)));
    assert(fake.reads==2 && fake.modes==1 && fake.us==4195);
    kui_disc_timing_report();
    expect("OPTICAL setup:");expect("opt wall_us=4195 requests=1 bytes=4704");
    expect("opt mode_us=7 calls=1 failed=0");
    expect("opt read1 us=1068 max_us=1068");expect("opt read2 us=3120 max_us=3120");
    expect("opt read1 submit_us=16 submits=1");expect("opt read1 poll_us=52 polls=2");
    expect("opt read1 wait_us=1000 waits=1");expect("opt read2 wait_us=3000 waits=3");
    expect("opt read2 max_fad=45150 max_sectors=2");
    kui_disc_timing_phase(NULL,true);fake.used=0;fake.log[0]=0;
    assert(kui_disc_read_raw(NULL,45152,1,out)==KUI_READ_OK);
    kui_disc_timing_report();expect("OPTICAL capture:");expect("requests=1 bytes=2352");
    expect("requests=1 bytes=4704"); /* setup retained independently */

    reset();fake.submit_busy=true;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_OK);
    kui_disc_timing_report();expect("opt read1 submit_us=32 submits=2");expect("opt read1 wait_us=2000 waits=2");
    reset();fake.underfill=true;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_RETRY && fake.reads==2);
    kui_disc_timing_report();expect("opt mismatch=1 guards=0 refused=0");expect("requests=1 bytes=0");
    reset();fake.fail=true;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_RETRY && fake.reads==1);
    kui_disc_timing_report();expect("opt read1 ok=0 failed=1 timeout=0");expect("opt read2 calls=0 bytes=0");
    reset();fake.timeout=true;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_RETRY);
    assert(fake.aborts==1 && fake.us>=5000000 && fake.us<5010000);
    kui_disc_timing_report();expect("opt read1 ok=0 failed=0 timeout=1");expect("abort_us=11 aborts=1");
    reset();fake.cancel=true;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL && fake.aborts==1);
    kui_disc_timing_report();expect("opt read1 cancelled=1 abort_failed=0 invalid=0");
    reset();fake.mode_fail=true;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL && fake.reads==0);
    kui_disc_timing_report();expect("opt mode_us=7 calls=1 failed=1");
    reset();fake.stop=true;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL && fake.sends==0);
    kui_disc_timing_report();expect("opt mismatch=0 guards=0 refused=1");
    reset();fake.guard=true;
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL);
    assert(kui_disc_read_raw(NULL,45150,2,out)==KUI_READ_FATAL && fake.reads==1);
    kui_disc_timing_report();expect("opt mismatch=0 guards=1 refused=1");
    puts("PASS optical: exclusive subtimers, paired bytes, phase/reset, busy submit, underfill, failures, deadlines, cancel and guards");
    return 0;
}
