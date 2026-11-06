#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the actual startup prefix/artwork and reject the old blank preflight."""
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src/dreamcast/main.c").read_text()
main = SOURCE[SOURCE.index("int main(void) {"):]
prefix = main[main.index("{") + 1:main.index("    update_boot_start();")]
# Exact main prefix from 7c9c039, before this repair. Keep the old execution
# order as the negative control instead of merely checking for a new call.
original = (ROOT / "tests/fixtures/startup-first-frame-original.inc").read_text()
assert hashlib.sha256(original.encode()).hexdigest() == "bbf5c30e35a2e5da3f89460195b6cb46a49a3c3eabf3292e47b616c78192de34"

assert main.index("vid_set_mode") < main.index("kui_splash_draw(vram_s)") < main.index("kui_log(")
assert main.index("kui_splash_draw(vram_s)") < main.index("kui_clock_start") < main.index("update_boot_start();")
# Paint only RAM artwork early; software selection and existing app ownership
# stay before shell/audio startup, optical STOP, and the I/O worker.
assert main.index("update_boot_start();") < main.index("kui_music_init")
assert main.index("update_boot_start();") < main.index("kui_shell_init")
assert main.index("kui_shell_init") < main.index("draw_shell();")
assert main.index("draw_shell();") < main.index("kui_disc_stop();") < main.index("thd_create_ex")

before = r'''
#include "kui/splash.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "splash_pixels.inc"
#define KUI_SD_RUNTIME 1
#define KUI_UPDATE_ENGINE_CURRENT 3
#define KUI_RELEASE_SHORT "test"
#define KUI_ROLE "runtime"
#define KUI_BUILD_ID "012345abcdef"
#define DM_640x480 1
#define DM_MULTIBUFFER 2
#define PM_RGB565 16
#define CT_VGA 1
#define VID_PAL 4
#define VID_INTERLACE 8
static uint8_t kui_update_bank_capability[16] = {[8] = 1};
static uint8_t kui_update_engine_capability[16] = {[8] = KUI_UPDATE_ENGINE_CURRENT};
static struct video_mode {unsigned width,height,cable_type,flags;} video = {640,480,CT_VGA,0};
static const struct video_mode *vid_mode = &video;
static void *ui_thread;
static uint16_t guarded_frame[640u*480u+2u];
static uint16_t *vram_s = guarded_frame+1;
static unsigned phase,modes,frames,preflights,logs;
static void require_first_frame(void) {
    if(phase!=3) {
        fputs("startup setup reached before first artwork frame\n",stderr);exit(23);
    }
}
static void kui_storage_boot_begin(void) {assert(phase==0);}
static void *thd_get_current(void) {assert(phase==0);return (void *)(uintptr_t)1;}
static void vid_set_mode(int mode,int format) {
    assert(phase==0 && ui_thread && mode==(DM_640x480|DM_MULTIBUFFER) && format==PM_RGB565);
    /* Both cold entry and a bank chain reset the prior framebuffer. */
    memset(vram_s,0,640u*480u*sizeof(*vram_s));++modes;phase=1;
}
void vid_waitvbl(void) {
    assert(phase==1);
    assert(!memcmp(vram_s,kui_splash_pixels,sizeof(kui_splash_pixels)));
    assert(guarded_frame[0]==0x5aa5 && guarded_frame[640u*480u+1u]==0xa55a);
    phase=2;
}
void vid_flip(int target) {assert(phase==2 && target==-1);++frames;phase=3;}
static void kui_log(const char *format,...) {require_first_frame();assert(format);++logs;}
static void kui_clock_start(void (*log)(const char *,...)) {require_first_frame();assert(log==kui_log);}
static void kui_update_boot_require_engine(unsigned engine) {require_first_frame();assert(engine==KUI_UPDATE_ENGINE_CURRENT);}
static void update_boot_start(void) {require_first_frame();assert(frames==modes);++preflights;}
static int startup_until_selection(void) {
'''
after = r'''
    update_boot_start();
#endif
    return 0;
}
int main(void) {
    guarded_frame[0]=0x5aa5;guarded_frame[640u*480u+1u]=0xa55a;
    /* Repeat the real prefix to cover CD handoff and a chained GUI's fresh
     * video mode. Neither entry may rely on the old framebuffer surviving. */
    for(unsigned entry=0;entry<2u;entry++) {
        phase=0;assert(startup_until_selection()==0 && phase==3);
        assert(modes==entry+1u && frames==entry+1u && preflights==entry+1u);
    }
    assert(logs);
    phase=0;kui_update_engine_capability[8]=2;
    assert(startup_until_selection()==1 && !phase && modes==2 && preflights==2);
    puts("PASS startup first frame: real main prefix and original artwork, cold/chained mode reset, bounded complete frame before setup/preflight, unchanged service ownership");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="kui-startup-frame-") as temporary:
    path = Path(temporary)
    # Regenerate the unchanged pinned artwork outside the repository, then
    # compile the real production copier with its ordinary relative include.
    subprocess.run(["python3", str(ROOT / "tools/build_splash.py"), "--directory", str(path / "build")], check=True)
    production = path / "src/apps/splash.c"
    production.parent.mkdir(parents=True)
    production.write_bytes((ROOT / "src/apps/splash.c").read_bytes())
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
    for label, body in (("current", prefix), ("original", original)):
        source, binary = path / (label + ".c"), path / label
        source.write_text(before + body + after)
        subprocess.run([*shlex.split(os.environ.get("HOST_CC", "cc")), "-std=c11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer", "-fno-pie", "-no-pie", "-I", str(ROOT / "include"),
            "-I", str(path / "build"), str(source), str(production), "-o", str(binary)], check=True)
        result = subprocess.run([str(binary)], env=env, capture_output=True, text=True)
        if label == "current":
            assert result.returncode == 0, result.stdout + result.stderr
            print(result.stdout, end="")
        else:
            assert result.returncode == 23 and "startup setup reached before first artwork frame" in result.stderr, result.stdout + result.stderr
            print("PASS original-source control: exact 7c9c039 startup prefix reaches setup with a blank frame and is rejected")
