#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute main.c's real companion dispatch/snapshot/export boundary on host."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'src/dreamcast/main.c').read_text()


def function(name):
    at=source.rfind('static ',0,source.index(name+'('))
    brace=source.index('{',at)
    depth=1
    end=brace+1
    while depth:
        if source[end]=='{':depth+=1
        elif source[end]=='}':depth-=1
        end+=1
    return source[at:end]


prefix=r'''
#include "kui/shell.h"
#include "kui/storage.h"
#include "kui/version.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define KUI_ROLE "SD runtime"
#define KUI_BUILD_ID "dispatch-test"
#define LOG_LINES 1500
#define LINE_BYTES 77
static char report[LOG_LINES*LINE_BYTES+256],lines[LOG_LINES][LINE_BYTES];
static unsigned line_count;
static bool log_truncated;
static struct kui_browser_options browser_options_pending;
static struct kui_browser_status browser_snapshot;
static unsigned browser_generation;
static struct kui_problem_report_options problem_report_options_pending;
static struct kui_problem_report_view problem_report_snapshot;
static unsigned problem_report_generation;
static size_t problem_report_log_bytes_pending;
static int lock;
static unsigned held,opened,disconnected,mounts,releases,exports;
static bool connected,mount_ok=true,connect_ok=true,release_ok=true,cancelled;
static char frozen[sizeof(report)];
static size_t frozen_bytes;
static void mutex_lock(int *p){assert(p==&lock && !held);held=1;}
static void mutex_unlock(int *p){assert(p==&lock && held);held=0;}
static void kui_sd_set_params(unsigned storage,bool crc){assert(storage==KUI_STORAGE_AUTO && crc);}
static bool kui_sd_connect(void){assert(!connected);if(!connect_ok)return false;connected=true;++opened;return true;}
static void kui_sd_disconnect(void){assert(connected);connected=false;++disconnected;}
static void kui_log(const char *format,...){(void)format;strcpy(lines[0],"later live log text");}
static bool kui_cancelled(void){return cancelled;}
bool kui_mount(FATFS *fs,kui_log_fn log){assert(connected && fs && log);++mounts;return mount_ok;}
FRESULT f_mount(FATFS *fs,const TCHAR *path,BYTE immediately){
    assert(!fs && !strcmp(path,"0:") && !immediately && connected);++releases;
    return release_ok?FR_OK:FR_DISK_ERR;
}
bool kui_problem_report_export_mounted(const struct kui_problem_report_options *options,
    const char *version,const char *build,const void *data,size_t bytes,
    struct kui_problem_report_view *view,kui_log_fn log,kui_cancel_fn cancel){
    assert(connected && options->include_latest_screenshot && !strcmp(version,KUI_VERSION));
    assert(!strcmp(build,KUI_BUILD_ID) && bytes==frozen_bytes && !memcmp(data,frozen,bytes));
    assert(bytes<=KUI_PROBLEM_REPORT_LOG_MAX && log && cancel);++exports;
    memset(view,0,sizeof(*view));view->status.complete=true;
    if(cancel()){view->status.stopped=true;return false;}
    view->status.passed=true;view->status.line_count=1;
    strcpy(view->path,KUI_PROBLEM_REPORT_ROOT "/REPORT-000001");
    strcpy(view->status.lines[0],view->path);
    log("export continued after freezing");return true;
}
'''

suffix=r'''
int main(void){
    assert(worker_action(KUI_SHELL_BROWSER_START)==110u);
    assert(worker_action(KUI_SHELL_PROBLEM_REPORT)==116u);
    assert(worker_action(KUI_SHELL_VMU_HISTORY_RUN)==111u);
    assert(worker_action(KUI_SHELL_GAMES_EDIT_LOAD)==112u);
    assert(worker_action(KUI_SHELL_GAMES_EDIT_SAVE)==113u);
    assert(worker_action(KUI_SHELL_GAMES_JUMP)==114u);
    assert(worker_action(KUI_SHELL_GAMES_IDENTITY_VERIFY)==115u);
    assert(worker_action(KUI_SHELL_GAMES_FAVORITE)==109u);
    assert(worker_action(KUI_SHELL_FTP_START)==64u);
    assert(worker_action(KUI_SHELL_NONE)==0u);
    line_count=LOG_LINES;log_truncated=true;
    for(unsigned i=0;i<LOG_LINES;i++){memset(lines[i],'a'+i%26,LINE_BYTES-1);lines[i][LINE_BYTES-1]=0;}
    mutex_lock(&lock);problem_report_freeze_locked();mutex_unlock(&lock);
    frozen_bytes=problem_report_log_bytes_pending;memcpy(frozen,report,frozen_bytes);
    assert(frozen_bytes>115000u && frozen_bytes<=KUI_PROBLEM_REPORT_LOG_MAX);
    assert(strstr(report,"Log truncated: YES") && line_count==LOG_LINES);
    strcpy(lines[0],"live log changed before worker");
    assert(!memcmp(report,frozen,frozen_bytes));
    problem_report_options_pending.include_latest_screenshot=true;
    problem_report_operation();
    assert(exports==1 && opened==1 && disconnected==1 && mounts==1 && releases==1 && !connected && !held);
    assert(problem_report_snapshot.status.passed && problem_report_snapshot.path[0] && problem_report_generation==1);
    release_ok=false;problem_report_operation();
    assert(exports==2 && !connected && !problem_report_snapshot.status.passed && !problem_report_snapshot.path[0]);
    assert(problem_report_snapshot.status.errors==1 && !problem_report_snapshot.status.line_count);
    release_ok=true;mount_ok=false;problem_report_operation();
    assert(exports==2 && releases==3 && disconnected==3 && !problem_report_snapshot.path[0]);
    mount_ok=true;connect_ok=false;problem_report_operation();
    assert(exports==2 && releases==3 && disconnected==3 && problem_report_snapshot.status.errors==1);
    connect_ok=true;cancelled=true;problem_report_operation();
    assert(exports==3 && !connected && problem_report_snapshot.status.stopped && !problem_report_snapshot.path[0]);
    browser_options_pending.port=8080;
    browser_snapshot.state=KUI_BROWSER_READY;strcpy(browser_snapshot.url,"http://stale/");
    mutex_lock(&lock);companion_stopped_locked(110u);companion_stopped_locked(116u);mutex_unlock(&lock);
    assert(browser_snapshot.state==KUI_BROWSER_STOPPED && !browser_snapshot.url[0] && browser_generation==1);
    assert(problem_report_snapshot.status.complete && problem_report_snapshot.status.stopped && !problem_report_snapshot.path[0]);
    puts("PASS main companion dispatch: stable job IDs, frozen bounded log, exclusive mount/release, cancel/failure publication");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='kui-companion-dispatch-') as tmp:
    c=Path(tmp)/'dispatch.c';binary=Path(tmp)/'dispatch'
    c.write_text(prefix+'\n\n'.join(function(n) for n in
        ('problem_report_freeze_locked','companion_stopped_locked','problem_report_operation','worker_action'))+suffix)
    command=['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer','-Iinclude','-I.deps/fatfs/source',
        '-Ifirmware/kui-wifi/components/kwlink/include',str(c),'-o',str(binary)]
    subprocess.run(command,cwd=ROOT,check=True)
    subprocess.run([str(binary)],cwd=ROOT,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),check=True)
