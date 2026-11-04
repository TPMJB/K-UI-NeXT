#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the actual GUI runtime guard and exclusive restart worker helper."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/dreamcast/main.c").read_text()


def function(name):
    at = SOURCE.rfind("static ", 0, SOURCE.index(name + "("))
    brace = SOURCE.index("{", at)
    depth, end = 1, brace + 1
    while depth:
        depth += (SOURCE[end] == "{") - (SOURCE[end] == "}")
        end += 1
    return SOURCE[at:end]


# Delivery stays on main, after the worker has parked; no BIOS/game image alias.
delivery = SOURCE[SOURCE.index("if(restart_kui) {"):SOURCE.index("if(launch_probe) {")]
assert delivery.index("update_runtime_handoff_refusal") < delivery.index("kui_cancelled()") < delivery.index("arch_exec(")
assert delivery.index("kui_runtime_free") < delivery.index("kui_update_boot_reload_abort") < delivery.index("update_restart_ready=false")
assert "kui_gd_play_boot" not in delivery and "probe_image" not in delivery
assert "worker_active_action==121u" in function("screenshot_request")
assert "if(action==121)update_restart_operation();" in SOURCE
assert '"Restart K-UI stopped before starting."' in SOURCE

program = r'''
#define KUI_BUILD_ID "012345abcdef"
#include "kui/update_boot.h"
#include "kui/storage_policy.h"
#include "kui/shell.h"
#include "kui/music.h"
#include "kui/menu_sound.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int lock,disc_idle;
static bool locked,audio_on,sound_on,connected,mounted,cancelled,update_restart_ready;
static unsigned scenario,shutdowns,inits,menu_inits,published,reloads,aborts,frees,failures,waits;
static unsigned music_cache_attempted;
static char payload;
static struct kui_runtime_image update_restart_image;
static struct {bool music_enabled,menu_sounds;unsigned music_volume;} system_current;
static void mutex_lock(int *p) {assert(p==&lock && !locked);locked=true;}
static void mutex_unlock(int *p) {assert(p==&lock && locked);locked=false;}
static void kui_log(const char *format,...) {(void)format;}
void kui_menu_sound_shutdown(void) {assert(locked && sound_on);sound_on=false;}
void kui_music_shutdown(void) {assert(!locked && audio_on && !sound_on);audio_on=false;++shutdowns;}
static void publish_music(void) {assert(!locked);++published;}
static void kui_disc_idle_cancel(int *p) {assert(p==&disc_idle && !audio_on);}
static void kui_sd_set_params(unsigned transport,bool checked) {assert(transport==KUI_STORAGE_AUTO && checked && !audio_on);}
static bool kui_sd_connect(void) {assert(!audio_on && !sound_on && !connected);connected=scenario!=1u;return connected;}
bool kui_mount(FATFS *fs,kui_log_fn log) {(void)fs;assert(connected && log==kui_log);mounted=scenario!=2u;return mounted;}
unsigned kui_storage_selected(void) {return KUI_STORAGE_SCI;}
enum kui_update_boot_result kui_update_boot_reload(const char *compiled,unsigned transport,
    struct kui_runtime_image *out,struct kui_update_boot_view *view,kui_log_fn log,kui_cancel_fn cancel) {
    assert(!strcmp(compiled,KUI_BUILD_ID) && transport==KUI_STORAGE_SCI && mounted && connected && !audio_on && !sound_on);
    assert(out==&update_restart_image && log==kui_log && cancel);++reloads;
    if(scenario==3u) {strcpy(view->notice,"Selected GUI refused");return KUI_UPDATE_BOOT_ERROR;}
    out->data=&payload;out->info.payload_bytes=4096u;
    if(scenario==5u)cancelled=true;
    return KUI_UPDATE_BOOT_CHAIN;
}
FRESULT f_mount(FATFS *fs,const TCHAR *path,BYTE immediate) {
    assert(!fs && !strcmp(path,"0:") && !immediate && mounted && !audio_on);mounted=false;
    return scenario==4u?FR_DISK_ERR:FR_OK;
}
static void kui_sd_disconnect(void) {connected=false;mounted=false;}
static bool kui_cancelled(void) {return cancelled;}
void kui_runtime_free(struct kui_runtime_image *image) {assert(image==&update_restart_image);*image=(struct kui_runtime_image){0};++frees;}
void kui_update_boot_reload_abort(void) {assert(!connected && !mounted);++aborts;}
static void update_restart_failure(bool stopped,const char *message) {
    assert(!connected && !mounted && message[0] && stopped==cancelled);++failures;
}
static void thd_sleep(unsigned milliseconds) {
    assert(milliseconds==16u && !connected && !mounted && !audio_on && !sound_on && update_restart_ready && update_restart_image.data);
    ++waits;
    /* Simulate main refusing a fully staged image: release it and restore
     * its old pin BEFORE the worker is allowed to rebuild audio services. */
    kui_runtime_free(&update_restart_image);kui_update_boot_reload_abort();update_restart_ready=false;
}
void kui_music_init(kui_log_fn log) {assert(log==kui_log && !audio_on && !connected && !mounted && !update_restart_ready);audio_on=true;++inits;}
void kui_music_set_config(bool enabled,unsigned volume) {assert(audio_on && enabled==system_current.music_enabled && volume==system_current.music_volume);}
bool kui_menu_sound_init(void) {assert(locked && audio_on && !sound_on);sound_on=true;++menu_inits;return true;}
void kui_menu_sound_config(bool enabled,unsigned volume) {assert(locked && sound_on && enabled==system_current.menu_sounds && volume==system_current.music_volume);}
''' + function("update_runtime_handoff_refusal") + "\n" + function("needs_cd_handoff") + "\n" + function("worker_action") + "\n" + function("update_restart_operation") + r'''
int main(void) {
    assert(worker_action(KUI_SHELL_RESTART_KUI)==121u && worker_action(KUI_SHELL_RESTART)==44u);
    assert(needs_cd_handoff(121u) && !needs_cd_handoff(44u));
    assert(!update_runtime_handoff_refusal(0x8c800000u,KUI_RUNTIME_MAX_BYTES,0x8cc10000u));
    assert(update_runtime_handoff_refusal(0x8c00fffcu,4096u,0x8cff0000u));
    assert(update_runtime_handoff_refusal(0x8d000000u,4096u,0x8cff0000u));
    assert(update_runtime_handoff_refusal(0x8c800002u,4096u,0x8cff0000u));
    assert(update_runtime_handoff_refusal(0x8c800000u,0u,0x8cff0000u));
    assert(update_runtime_handoff_refusal(0x8c800000u,4097u,0x8cff0000u));
    assert(update_runtime_handoff_refusal(0x8c800000u,KUI_RUNTIME_MAX_BYTES+4u,0x8cff0000u));
    assert(update_runtime_handoff_refusal(0x8cffff00u,4096u,0x8cff0000u));
    assert(update_runtime_handoff_refusal(0x8c800000u,KUI_RUNTIME_MAX_BYTES,0x8cc0fffcu));
    assert(update_runtime_handoff_refusal(0x8c800000u,4096u,0x8d000000u));
    for(scenario=0;scenario<6u;scenario++) {
        locked=connected=mounted=cancelled=update_restart_ready=false;audio_on=sound_on=true;
        shutdowns=inits=menu_inits=published=reloads=aborts=frees=failures=waits=0u;music_cache_attempted=255u;
        system_current.music_enabled=scenario%2u;system_current.menu_sounds=scenario%3u;system_current.music_volume=40u+scenario;
        update_restart_operation();
        assert(!connected && !mounted && !locked && !update_restart_ready && !update_restart_image.data);
        assert(audio_on && sound_on && shutdowns==1u && inits==1u && menu_inits==1u && published==2u && !music_cache_attempted);
        assert(reloads==(scenario==1u || scenario==2u?0u:1u));
        assert(aborts==1u && frees==1u && waits==(scenario==0u?1u:0u) && failures==(scenario==0u?0u:1u));
    }
    puts("PASS direct K-UI restart GUI: separate BIOS action, runtime bounds, quiesced worker delivery, mount/read/release/Stop aborts, restored audio services");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="kui-update-restart-") as temporary:
    source, binary = Path(temporary) / "restart.c", Path(temporary) / "restart"
    source.write_text(program)
    subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-O1", "-g",
        "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer", "-fno-pie", "-no-pie", "-Iinclude", "-I.deps/fatfs/source",
        "-Ifirmware/kui-wifi/components/kwlink/include", str(source), "-o", str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], check=True, env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1"))
