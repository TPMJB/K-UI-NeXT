#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual GD-Play report gate and report writer with a storage double.

The storage double checks ownership and cancellation; it does not claim a real
SD filesystem write. Production report sync/close remains in kui_report_save.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/dreamcast/main.c').read_text()


def function(name):
    at = source.rfind('static ', 0, source.index(name + '('))
    brace = source.index('{', at)
    depth, end = 1, brace + 1
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[at:end]


# Gate placement: app calls return before saving. The launch-success branch
# publishes main's handoff and waits before its only report call; therefore
# successful handoff cannot acquire SD storage for a report.
check_start = source.index('if(action==103) {')
check_end = source.index('if(action==56 || action==57 || action==58 || action==104 || action==121 || action==122)', check_start)
check = source[check_start:check_end]
assert check.index('kui_disc_launcher_check(') < check.index('disc_launch_progress(&result);')
assert check.index('disc_launch_progress(&result);') < check.index('disc_launch_failure_report(')
launch_start = check_end
launch_end = source.index('if(action==25 || action==44)', launch_start)
launch = source[launch_start:launch_end]
assert launch.count('disc_launch_failure_report(') == 2
assert launch.index('if(prepared) {') < launch.index('probe_launch_ready=true;')
assert launch.index('probe_launch_ready=true;') < launch.index('if(!waiting) break;')
assert launch.index('if(!waiting) break;') < launch.index('disc_launch_failure_report("auto GD Play handoff failure"')
assert launch.index('disc_result=probe_status;') < launch.index('disc_launch_failure_report("auto GD Play handoff failure"')
assert 'if(action==104) disc_launch_failure_report("auto GD Play boot failure",&disc_result);' in launch
assert launch.index('kui_disc_launcher_prepare_vmu(') < launch.index('disc_launch_failure_report("auto GD Play boot failure"')
main_handoff = source[source.index('if(launch_probe) {'):]
assert main_handoff.index('kui_runtime_free(&probe_image);') < main_handoff.index('probe_launch_ready=false;')
assert 'disc_launch_failure_report(' not in main_handoff

program = r'''
#define KUI_SD_RUNTIME 1
#define KUI_RELEASE_SHORT "K-UI test"
#define KUI_ROLE "SD runtime"
#define KUI_BUILD_ID "0123456789ab"
#define LOG_LINES 32
#define LINE_BYTES 77
#include "kui/apps.h"
#include "kui/report.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static int lock;
static char lines[LOG_LINES][LINE_BYTES],report[LOG_LINES*LINE_BYTES+256];
static unsigned line_count,connects,disconnects,saves,forgets,observations;
static bool log_truncated,cancel_requested,saving_report,dma_degraded,busy,locked;
static bool connected,connect_ok,request_new_stop,optical_owned,files_open;
static enum kui_report_result returned;
static char saved[sizeof(report)];
static void mutex_lock(int *p) {assert(p==&lock && !locked);locked=true;}
static void mutex_unlock(int *p) {assert(p==&lock && locked);locked=false;}
static void kui_log(const char *fmt,...) {
    assert(!locked && line_count<LOG_LINES);
    va_list a;va_start(a,fmt);vsnprintf(lines[line_count++],LINE_BYTES,fmt,a);va_end(a);
}
static bool kui_cancelled(void) {return cancel_requested;}
static void kui_memory_log(const char *reason) {assert(!strcmp(reason,"save log"));}
static void kui_music_log_stats(const char *reason) {assert(!strcmp(reason,"save log"));}
static bool kui_sd_connect(void) {
    assert(busy && saving_report && !locked && !connected && !optical_owned && !files_open);
    ++connects;connected=connect_ok;return connected;
}
static void kui_sd_disconnect(void) {assert(connected && !locked);++disconnects;connected=false;}
static void salvage_session_forget(void) {++forgets;}
static void salvage_session_observe_errors(void) {++observations;}
enum kui_report_result kui_report_save(const void *data,size_t bytes,char path[96],
    kui_log_fn log,kui_cancel_fn cancel) {
    assert(connected && busy && saving_report && !locked && !optical_owned && !files_open);
    assert(log==kui_log && cancel==kui_cancelled && bytes<sizeof(saved));
    assert(!cancel()); /* An older B press cannot suppress its failure report. */
    memcpy(saved,data,bytes);saved[bytes]=0;++saves;
    if(request_new_stop) {cancel_requested=true;assert(cancel());return KUI_REPORT_STOPPED;}
    strcpy(path,"0:/KUI/probes/p0001/diagnostics.txt");return returned;
}
''' + function('save_report') + '\n' + function('disc_launch_failure_report') + r'''
static void reset(void) {
    memset(lines,0,sizeof(lines));memset(report,0,sizeof(report));memset(saved,0,sizeof(saved));
    connects=disconnects=saves=forgets=observations=line_count=0;
    log_truncated=cancel_requested=saving_report=dma_degraded=locked=connected=false;
    request_new_stop=optical_owned=files_open=false;busy=connect_ok=true;returned=KUI_REPORT_SAVED;
}
static struct kui_app_status status(void) {
    struct kui_app_status s={.complete=true,.errors=1,.line_count=2};
    strcpy(s.message,"Physical disc boot data could not be read");
    strcpy(s.lines[0],"Stage: executable hashes");
    strcpy(s.lines[1],"Last read: FAD 549158, 1 sector(s)");return s;
}
int main(void) {
    for(unsigned mask=0;mask<16u;mask++) {
        reset();struct kui_app_status s=status();
        s.complete=!!(mask&1u);s.passed=!!(mask&2u);s.stopped=!!(mask&4u);s.errors=!!(mask&8u);
        struct kui_app_status before=s;
        disc_launch_failure_report("auto GD Play boot failure",&s);
        unsigned expected=s.complete && !s.passed && !s.stopped && s.errors;
        assert(saves==expected && connects==expected && disconnects==expected);
        assert(!memcmp(&s,&before,sizeof(s)) && !saving_report && !connected && !locked && busy);
        if(expected) {
            assert(strstr(saved,"Report trigger: auto GD Play boot failure"));
            assert(strstr(saved,"Operation result: Physical disc boot data could not be read"));
            assert(strstr(saved,"FAD 549158") && strstr(saved,"Stage: executable hashes"));
            assert(!forgets && observations==1u);
        }
    }
    reset();disc_launch_failure_report("unused",NULL);assert(!saves && !connects && !line_count);
    for(unsigned mode=0;mode<4u;mode++) {
        reset();struct kui_app_status s=status(),before=s;
        cancel_requested=true;request_new_stop=mode==0u;
        returned=mode==1u?KUI_REPORT_FAILED:KUI_REPORT_SAVED;connect_ok=mode!=2u;
        disc_launch_failure_report("auto GD Play check failure",&s);
        assert(!memcmp(&s,&before,sizeof(s)) && !saving_report && !connected && !locked && busy);
        assert(connects==1u && disconnects==(mode!=2u) && saves==(mode!=2u));
        assert(forgets==(mode!=3u) && observations==1u);
        assert(cancel_requested==(mode==0u)); /* Only a new report Stop persists. */
    }
    reset();struct kui_app_status s=status();s.line_count=KUI_APP_LINES+5u;
    for(unsigned i=0;i<KUI_APP_LINES;i++)strcpy(s.lines[i],"bounded detail");
    disc_launch_failure_report("auto GD Play handoff failure",&s);
    assert(saves==1u && line_count==KUI_APP_LINES+3u);
    puts("PASS GD Play failure reports: actual gate/writer, retained status and FAD, stopped/success suppression, exclusive SD ownership, prior/new Stop, failed saves and post-refusal dispatch ordering");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='kui-gd-play-report-') as folder:
    work=Path(folder);path=work/'failure-reports.c';binary=work/'failure-reports'
    path.write_text(program)
    subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-O1','-g',
        '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
        '-fno-omit-frame-pointer','-fno-pie','-no-pie','-Iinclude','-I.deps/fatfs/source',
        str(path),'-o',str(binary)],cwd=ROOT,check=True)
    subprocess.run([str(binary)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
