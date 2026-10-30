#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay the actual main.c card-hint scheduler under foreground/UI races."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/dreamcast/main.c").read_text()


def function(name):
    at = source.index("static ", source.index(name + "(") - 32)
    # Find the complete definition, ignoring braces in comments/strings. The
    # selected scheduler functions have no braces inside strings/comments.
    brace = source.index("{", at)
    depth = 1
    end = brace + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[at:end]


names = ("games_page", "games_idle_cancelled", "vmu_game_info_request_start",
         "vmu_game_info_current", "vmu_game_info_cancelled", "vmu_game_info_idle_work",
         "worker_action")
functions = "\n\n".join(function(name) for name in names)
prefix = r'''
#include "kui/shell.h"
#include "kui/storage.h"
#include "kui/vmu_game_info.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static struct kui_shell shell;
struct vmu_game_info_request {
    char product[12]; unsigned generation; bool native_route;
};
static struct vmu_game_info_request vmu_game_info_requested,vmu_game_info_running;
static struct kui_vmu_game_info vmu_game_info_snapshot;
static unsigned vmu_game_info_request_result,vmu_game_info_generation;
static bool vmu_game_info_request_valid;
static bool busy,video_preview,cancel_requested,games_cache_clear_pending;
static bool startup_finished,splash_active,boot_ready,probe_launch_ready,cd_owned;
static struct {bool requested;} sci_video_quiet;
static unsigned pending;
static int lock,lock_depth;
static unsigned mode,runs,live_storage,finished_locks;
static bool inject_publish,run_finished;
static bool observed_native;
static char observed_product[12];
static void mutex_lock(int *value) {
    (void)value;assert(!lock_depth);++lock_depth;
    if(inject_publish && run_finished && ++finished_locks==3u) {
        /* Foreground launch arrives after the final cancellation sample but
         * before publication acquires the lock. It still wins. */
        pending=58;busy=true;
    }
}
static void mutex_unlock(int *value) {(void)value;assert(lock_depth==1);--lock_depth;}
bool kui_cd_audio_owns_drive(void) {return cd_owned;}
static void kui_sd_set_params(unsigned transport,bool crc) {
    assert(!lock_depth && transport==KUI_STORAGE_AUTO && crc);
}
static void kui_log(const char *format,...) {(void)format;}
static void request_ui(const char *product,unsigned generation,bool native);
void kui_vmu_game_info_run(const char *product,bool native,
    struct kui_vmu_game_info *out,kui_log_fn log,kui_cancel_fn cancel) {
    (void)log;assert(!lock_depth && !busy && !pending && !live_storage);
    ++runs;++live_storage;observed_native=native;
    memcpy(observed_product,product,sizeof(observed_product));
    memset(out,0,sizeof(*out));memcpy(out->product,product,sizeof(out->product));
    out->known=true;strcpy(out->label,"Selected card");
    if(mode==1u) { /* UI opens another game while the old lookup reads. */
        request_ui("B-200",2u,false);
        assert(!strcmp(product,"A-100") && native);
        assert(cancel());
    } else if(mode==2u) { /* Launch queues on this same worker. */
        assert(!busy);pending=58;busy=true;assert(cancel());
    } else if(mode==3u) { /* Back changes the page without waiting for I/O. */
        shell.page=KUI_SHELL_HOME;assert(!busy && cancel());
    } else if(mode==4u) { /* A failed read is reported once, not retried/poll. */
        out->known=false;strcpy(out->label,"Unavailable");
    } else if(mode==5u) { /* Native-route data is part of the request token. */
        request_ui("A-100",2u,false);assert(cancel());
    } else if(mode==6u) {
        inject_publish=true;finished_locks=0;
    }
    --live_storage;run_finished=true;
}
'''
suffix = r'''
static void request_ui(const char *product,unsigned generation,bool native) {
    mutex_lock(&lock);
    shell.page=KUI_SHELL_GAMES_LAUNCH_OPTIONS;
    memset(shell.games_vmu_product,0,sizeof(shell.games_vmu_product));
    strcpy(shell.games_vmu_product,product);
    shell.games_vmu_generation=generation;shell.games_vmu_loading=true;
    shell.games_detail.native_gd=native;shell.games_detail.native_cd=false;
    shell.games_detail.windows_ce=!native;
    vmu_game_info_request_start();mutex_unlock(&lock);
}
static void reset(void) {
    memset(&shell,0,sizeof(shell));memset(&vmu_game_info_snapshot,0,sizeof(vmu_game_info_snapshot));
    memset(&vmu_game_info_requested,0,sizeof(vmu_game_info_requested));
    memset(&vmu_game_info_running,0,sizeof(vmu_game_info_running));
    vmu_game_info_request_result=vmu_game_info_generation=0;vmu_game_info_request_valid=false;
    busy=video_preview=cancel_requested=games_cache_clear_pending=false;
    splash_active=boot_ready=probe_launch_ready=cd_owned=false;startup_finished=true;
    sci_video_quiet.requested=false;pending=0;mode=runs=live_storage=finished_locks=0;
    inject_publish=run_finished=false;lock_depth=0;
}
int main(void) {
    reset();
    assert(worker_action(KUI_SHELL_VMU_GAME_INFO)==0u);
    assert(worker_action(KUI_SHELL_GAMES_RETAIL)==58u);
    request_ui("A-100",1u,true);
    assert(!busy && !pending && vmu_game_info_request_valid);
    assert(vmu_game_info_idle_work() && runs==1u && !live_storage);
    assert(vmu_game_info_generation==1u && vmu_game_info_request_result==1u);
    assert(!strcmp(vmu_game_info_snapshot.product,"A-100") && observed_native);
    assert(!vmu_game_info_request_valid && !vmu_game_info_idle_work());

    reset();request_ui("A-100",1u,true);mode=1;
    assert(vmu_game_info_idle_work() && !vmu_game_info_generation);
    assert(vmu_game_info_request_valid && !strcmp(observed_product,"A-100"));
    mode=0;run_finished=false;
    assert(vmu_game_info_idle_work() && runs==2u && vmu_game_info_request_result==2u);
    assert(!strcmp(vmu_game_info_snapshot.product,"B-200") && !observed_native);

    reset();request_ui("A-100",1u,true);mode=2;
    assert(vmu_game_info_idle_work() && !vmu_game_info_generation);
    assert(pending==58u && busy && !live_storage);
    assert(!vmu_game_info_idle_work() && runs==1u && pending==58u);
    /* A queued foreground action is consumed only after the lookup returned
     * and released storage; this idle helper never clears the busy flag. */
    unsigned action=pending;pending=0;assert(action==58u && !live_storage);
    assert(!vmu_game_info_idle_work() && busy);
    busy=false;shell.page=KUI_SHELL_HOME;assert(!vmu_game_info_idle_work());

    reset();request_ui("A-100",1u,true);mode=3;
    assert(vmu_game_info_idle_work() && !vmu_game_info_generation && !busy);
    assert(!vmu_game_info_idle_work() && runs==1u);
    request_ui("A-100",2u,true);mode=0;
    assert(vmu_game_info_idle_work() && vmu_game_info_request_result==2u);

    reset();request_ui("A-100",1u,true);mode=4;
    assert(vmu_game_info_idle_work() && !vmu_game_info_snapshot.known);
    assert(vmu_game_info_generation==1u && !vmu_game_info_idle_work() && runs==1u);

    reset();request_ui("A-100",1u,true);mode=5;
    assert(vmu_game_info_idle_work() && !vmu_game_info_generation);
    mode=0;assert(vmu_game_info_idle_work() && !observed_native);

    reset();request_ui("A-100",1u,true);mode=6;
    assert(vmu_game_info_idle_work() && !vmu_game_info_generation);
    assert(pending==58u && busy && !live_storage);

    for(unsigned guard=0;guard<10u;guard++) {
        reset();request_ui("A-100",1u,true);
        if(guard==0)busy=true;
        else if(guard==1)pending=58u;
        else if(guard==2)video_preview=true;
        else if(guard==3)cancel_requested=true;
        else if(guard==4)games_cache_clear_pending=true;
        else if(guard==5)sci_video_quiet.requested=true;
        else if(guard==6)startup_finished=false;
        else if(guard==7)splash_active=true;
        else if(guard==8)boot_ready=true;
        else probe_launch_ready=true;
        assert(!vmu_game_info_idle_work() && !runs && !live_storage);
    }
    reset();request_ui("A-100",1u,true);cd_owned=true;
    assert(!vmu_game_info_idle_work() && !runs);
    reset();shell.games_vmu_loading=false;
    vmu_game_info_request_start();assert(!vmu_game_info_request_valid && !vmu_game_info_idle_work());
    puts("PASS menu lookup: actual dispatcher and background C, first/latest request, native token, Back, foreground launch priority, publication race, no busy/no concurrent storage, stop/video/boot guards");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="kui-menu-lookup-dispatch-") as work:
    test = Path(work) / "dispatch.c"
    test.write_text(prefix + functions + suffix)
    output = Path(work) / "dispatch"
    subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-O1", "-g",
                    "-Wall", "-Wextra", "-Werror", "-Wpedantic",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
                    "-Iinclude", "-I.deps/fatfs/source", "-Ifirmware/kui-wifi/components/kwlink/include",
                    str(test), "-o", str(output)],
                   cwd=ROOT, check=True)
    subprocess.run([str(output)], check=True, env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
print(json.dumps({"actual_main_functions": list(names),
                  "extracted_source_sha256": hashlib.sha256(functions.encode()).hexdigest(),
                  "hardware_latency_measured": False}, indent=2))
