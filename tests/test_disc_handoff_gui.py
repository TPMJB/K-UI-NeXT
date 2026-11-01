#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual main-thread staging/refusal and cache-release helpers."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/dreamcast/main.c").read_text()


def function(name):
    at = source.rfind("static ", 0, source.index(name + "("))
    brace = source.index("{", at)
    depth, end = 1, brace + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[at:end]


# The accepted job publishes its origin before the worker can fail. Logging
# and idle work never clear the retained result; only a new preparation does.
dispatch = source.index("if(action && !busy) {")
publish = source.index("pending = action; busy = true;", dispatch)
assert "probe_launch_disc=action==104;probe_launch_failed=false;" in source[dispatch:publish]
assert "probe_launch_disc=false;probe_launch_failed=false;" in source[dispatch:publish]
assert "if(action!=104) (void)games_selection_store(true);\n                else disc_launch_release_menu_memory();" in source
assert "shell.page=probe_handoff_failure_page(launch_disc);" in source

program = r'''
#include "kui/shell.h"
#include "kui/runtime.h"
#include "kui/disc_launch.h"
#include "kui/image_loader_layout.h"
#include "kui/music.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
static bool packet_valid;
static unsigned packet_calls;
static char payload;
bool kui_disc_launch_validate(const void *p,size_t bytes,struct kui_disc_launch_plan *out) {
    assert(p==&payload && bytes);++packet_calls;(void)out;return packet_valid;
}
static unsigned music_cache_attempted,music_request_generation;
static int music_requested,lock;
static struct kui_music_status original_music;
static unsigned cleared,published,logs;
static bool locked;
void kui_music_status_copy(struct kui_music_status *out) {*out=original_music;}
void kui_music_clear_cache(void) {assert(!locked);++cleared;}
static void publish_music(void) {assert(cleared==1u && !locked);++published;}
static void mutex_lock(int *p) {assert(p==&lock && !locked);locked=true;}
static void mutex_unlock(int *p) {assert(p==&lock && locked);locked=false;}
static void kui_memory_log(const char *reason) {
    assert(strstr(reason,"physical disc preload"));++logs;
    assert(cleared==(logs==2u?1u:0u));
}
''' + function("probe_handoff_refusal") + "\n" + function("disc_handoff_refusal_status") + "\n" + function("probe_handoff_failure_page") + "\n" + function("disc_launch_release_menu_memory") + r'''
static void refused(uintptr_t source,size_t length,uintptr_t stack,const char *why) {
    packet_calls=0u;
    const char *s=probe_handoff_refusal(source,length,stack,true,&payload);
    assert(s && strstr(s,why) && !packet_calls);
}
int main(void) {
    packet_valid=true;
    refused(0u,4096u,0x8cff0000u,"Source buffer");
    refused(0x8c00fffcu,4096u,0x8cff0000u,"Source buffer");
    refused(0x8d000000u,4096u,0x8cff0000u,"Source buffer");
    refused(0x8c200002u,4096u,0x8cff0000u,"Source buffer");
    refused(0x8c200000u,0u,0x8cff0000u,"Payload length");
    refused(0x8c200000u,4097u,0x8cff0000u,"Payload length");
    refused(0x8c200000u,0xe00004u,0x8cff0000u,"beyond main RAM");
    refused(0x8c200000u,4096u,0x8c200000u,"Main stack");
    refused(0x8c800000u,0x400000u,0x8cc0fffcu,"main-stack margin");
    packet_calls=0;
    assert(!probe_handoff_refusal(0x8c800000u,0x400000u,0x8cc10000u,true,&payload));
    assert(packet_calls==1u);
    assert(strstr(probe_handoff_refusal(0x8c800000u,0x400000u,0x8cc10000u,false,&payload),"Image-loader"));
    assert(packet_calls==1u);
    size_t image_max=KUI_IMAGE_RESIDENT_BLOB_OFFSET+KUI_IMAGE_RESIDENT_MAX_BYTES;
    assert(!probe_handoff_refusal(0x8c200000u,image_max,0x8cff0000u,false,&payload));
    assert(strstr(probe_handoff_refusal(0x8c200000u,image_max+4u,0x8cff0000u,false,&payload),"Image-loader"));
    packet_valid=false;
    assert(strstr(probe_handoff_refusal(0x8c200000u,4096u,0x8cff0000u,true,&payload),"packet"));
    struct kui_app_status s=disc_handoff_refusal_status("Source buffer overlaps the reserved main-stack margin",false,
        0x8cb00000u,4259840u,0x8cf00000u);
    assert(s.complete && !s.passed && !s.stopped && s.errors==1u && s.line_count==3u);
    assert(strstr(s.message,"main-stack margin") && strstr(s.lines[0],"4259840") && strstr(s.lines[1],"8CB00000") && strstr(s.lines[2],"8CF00000"));
    s=disc_handoff_refusal_status(NULL,true,0x8cb00000u,4259840u,0x8cf00000u);
    assert(s.complete && !s.passed && s.stopped && !s.errors && strstr(s.message,"stopped"));
    assert(probe_handoff_failure_page(true)==KUI_SHELL_GD_PLAY);
    assert(probe_handoff_failure_page(false)==KUI_SHELL_DIAGNOSTICS);
    for(unsigned mode=0;mode<4u;mode++) {
        original_music=(struct kui_music_status){.enabled=mode!=1u,.loaded=mode!=2u,
            .current_index=mode==3u?KUI_MUSIC_TRACKS:2u};
        music_cache_attempted=255u;music_request_generation=12u;music_requested=8;
        cleared=published=logs=0u;locked=false;
        disc_launch_release_menu_memory();
        assert(cleared==1u && published==1u && logs==2u && !locked);
        assert(!music_cache_attempted && music_request_generation==13u);
        assert(music_requested==(mode?-1:2));
    }
    puts("PASS direct-disc GUI handoff: actual staging bounds, validation ordering, retained failure/Stop routes, explicit memory diagnostics, drained menu cache and idle music recovery");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="kui-disc-handoff-") as work:
    path = Path(work) / "handoff.c"
    binary = Path(work) / "handoff"
    path.write_text(program)
    subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-O1", "-g",
                    "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer", "-fno-pie", "-no-pie", "-Iinclude",
                    "-I.deps/fatfs/source", "-Ifirmware/kui-wifi/components/kwlink/include",
                    str(path), "-o", str(binary)], cwd=ROOT, check=True)
    subprocess.run([str(binary)], check=True, env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
