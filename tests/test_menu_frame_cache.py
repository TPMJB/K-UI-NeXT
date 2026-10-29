#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay the actual optional UI frame allocation and handoff release helpers."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/"src/dreamcast/main.c").read_text()


def function(name):
    at=source.rfind("static ",0,source.index(name+"("))
    brace=source.index("{",at);depth=1;end=brace+1
    while depth:
        if source[end]=="{":depth+=1
        elif source[end]=="}":depth-=1
        end+=1
    return source[at:end]


# Dispatch releases before making the job visible to the I/O worker.
dispatch=source.index("if(action && !busy) {")
release=source.index("shell_frame_release_for_action(action);",dispatch)
publish=source.index("pending = action; busy = true;",dispatch)
assert dispatch<release<publish
assert "static uint16_t *shell_frame;" in source

program=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
static _Alignas(32) uint8_t pool[640u*480u*2u];
static uint16_t *shell_frame;
static unsigned attempts,releases;
static bool live,fail_next;
static void *tracked_allocate(size_t alignment,size_t bytes) {
    ++attempts;assert(!live && alignment==32u && bytes==614400u);
    if(fail_next) {fail_next=false;return NULL;}
    live=true;return pool;
}
static void tracked_free(void *pointer) {
    if(!pointer)return;
    assert(live && pointer==pool);live=false;++releases;
}
#define aligned_alloc tracked_allocate
#define free tracked_free
'''+function("shell_frame_prepare")+"\n"+function("shell_frame_release_for_action")+r'''
#undef free
#undef aligned_alloc
int main(void) {
    assert(!shell_frame_prepare(true) && !attempts);
    uint16_t *frame=shell_frame_prepare(false);
    assert(frame && live && attempts==1u && !((uintptr_t)frame&31u));
    /* Ordinary frames and small storage operations reuse the same cache. */
    assert(shell_frame_prepare(false)==frame && attempts==1u);
    assert(shell_frame_prepare(true)==frame && attempts==1u);
    shell_frame_release_for_action(58u);
    assert(!shell_frame && !live && releases==1u);
    /* No cache allocation competes with game preloading after dispatch. */
    assert(!shell_frame_prepare(true) && attempts==1u);
    /* A failed launch returns to idle; the cache may be recreated then. */
    assert(shell_frame_prepare(false)==frame && attempts==2u);
    shell_frame_release_for_action(19u);
    fail_next=true;
    assert(!shell_frame_prepare(false) && !live);
    assert(!shell_frame_prepare(true) && !live);
    assert(shell_frame_prepare(false)==frame && live);
    shell_frame_release_for_action(58u);
    for(unsigned action=0;action<=102u;action++) {
        assert(shell_frame_prepare(false) && live);
        bool large=action==19u || (action>=56u && action<=58u) || action==62u || action==79u;
        unsigned before=releases;
        shell_frame_release_for_action(action);
        assert((shell_frame==NULL)==large && live==!large);
        assert(releases==before+(large?1u:0u));
        shell_frame_release_for_action(58u);
        assert(!shell_frame && !live);
    }
    puts("PASS menu frame cache: actual helper C, aligned600KiB optional cache, allocation failure fallback, no allocation during preload, released before dispatch, failed-launch reuse, all action boundaries");
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="kui-menu-frame-") as work:
    test=Path(work)/"frame.c";test.write_text(program)
    binary=Path(work)/"frame"
    subprocess.run([*shlex.split(os.environ.get("CC","cc")),"-std=c11","-O1","-g",
                    "-Wall","-Wextra","-Werror","-Wpedantic","-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer","-fno-pie","-no-pie",str(test),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True,env=dict(os.environ,ASAN_OPTIONS="detect_leaks=0"))
