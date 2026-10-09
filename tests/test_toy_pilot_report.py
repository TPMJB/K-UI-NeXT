#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the production terminal report against its public telemetry ABI.

The reset and display callbacks destroy all live worker telemetry. The test
therefore verifies that the original prefix, diagnostic extensions and shared
transport page are retained before those callbacks, in the advertised order
and workspace. The shared case must fence through the GD RESET bridge.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/loader/toy_pilot_resident.c").read_text()
start = source.index("void kui_retail_menu_return(")
brace = source.index("{", start)
depth, end = 1, brace + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
production = source[start:end]

harness = r'''
#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "kui/toy_pilot.h"
#include "kui/gd_service.h"

_Static_assert(KUI_TOY_PILOT_API==8u,"Eight-block report version");
_Static_assert(sizeof(struct kui_toy_pilot_snapshot)==448u,"Retained report wire layout");
_Static_assert(KUI_TOY_PILOT_BLOCKS==8u && KUI_TOY_PILOT_BLOCK_FRAMES==4096u,
    "Block report geometry");

static struct { uint32_t before[4]; uint8_t block[512]; uint32_t after[4]; } image;
#if KUI_TOY_PILOT_SHARED_SCI
static struct {
    struct kui_toy_pilot_snapshot prefix;
    uint32_t transport[16];
} live_image;
#define live live_image.prefix
#define LIVE_BYTES sizeof(live_image)
#define LIVE_ADDRESS (&live_image)
_Static_assert(sizeof(live_image)==512u,"Shared cached snapshot and transport page");
#else
static struct kui_toy_pilot_snapshot live;
#define LIVE_BYTES sizeof(live)
#define LIVE_ADDRESS (&live)
#endif
static struct { uint32_t calls,requests,rejected,last_error; } diag_seed;
static struct { struct { uint32_t calls,requests,rejected,last_error; } diag; } service;
#if KUI_TOY_PILOT_GD_FIXED_STEP
static struct { uint32_t steps,sectors,ticks,max_ticks,max_sectors,invalid; } toy_gd_timing;
#endif
static unsigned display,captures,rows,seen_page,expected_pages;
static unsigned resets,snapshots;
static uint32_t words[8][16];
static bool missing;
static jmp_buf done;
static const struct kui_toy_pilot_snapshot *toy_snapshot(void) {
    assert(!resets);++snapshots;return missing?NULL:&live;
}
int toy_command(uint32_t command,uint32_t a,uint32_t b,uint32_t c) {
    assert(command==KUI_TOY_PILOT_RESET && !a && !b && !c);
#if KUI_TOY_PILOT_SHARED_SCI
    assert(!"Shared terminal reset bypassed the GD transport fence");return 0;
#else
    ++resets;memset(LIVE_ADDRESS,0xcc,LIVE_BYTES);return 1;
#endif
}
int32_t kui_retail_resident_dispatch(uint32_t r4,uint32_t r5,uint32_t r6,uint32_t function) {
    assert(KUI_TOY_PILOT_SHARED_SCI && !r4 && !r5 && !r6 && function==KUI_GD_RESET);
    assert(snapshots==1u && !resets && !captures && !rows);
    ++resets;memset(LIVE_ADDRESS,0xcc,LIVE_BYTES);return 0;
}
static void retail_display_restore(void *state) {
    assert(resets==1u && state==&display);memset(LIVE_ADDRESS,0xdd,LIVE_BYTES);
}
static void retail_display_values(const char *label,const uint32_t *input,unsigned count) {
    assert(resets==1u);
    if(!strcmp(label,"PILOT PAGE")) {
        assert(count==1u && *input==captures && !rows);
        seen_page=*input;assert(seen_page<expected_pages);
    } else {
        assert(!strcmp(label,"WORDS") && count==4u && rows<4u);
        memcpy(words[seen_page]+rows*4u,input,16u);++rows;
    }
}
static void retail_display_pause(unsigned frames) {
    assert(frames==900u && rows==4u);rows=0u;
    if(++captures==expected_pages) longjmp(done,1);
}
/* PRODUCTION_REPORT */
static void check(unsigned unavailable) {
    uint32_t seed[LIVE_BYTES/4u];
    for(unsigned i=0;i<LIVE_BYTES/4u;i++) seed[i]=0x5a000000u+i;
    seed[1]=KUI_TOY_PILOT_API;seed[2]=sizeof(live);
    seed[100]=7u;seed[101]=4096u; /* Highest block ID and completed block fill. */
    memcpy(LIVE_ADDRESS,seed,LIVE_BYTES);missing=unavailable==1u;
    if(unavailable==2u) --live.version;
    if(unavailable==3u) live.bytes-=64u;
    if(unavailable==4u) live.bytes+=64u;
    memset(&image,0xa5,sizeof(image));memset(words,0,sizeof(words));
    captures=rows=resets=snapshots=0u;
    diag_seed.calls=81u;diag_seed.requests=82u;diag_seed.rejected=83u;diag_seed.last_error=84u;
    memcpy(&service.diag,&diag_seed,sizeof(diag_seed));
#if KUI_TOY_PILOT_GD_FIXED_STEP
    const uint32_t timing[]={71u,72u,73u,74u,75u,76u};
    memcpy(&toy_gd_timing,timing,sizeof(timing));expected_pages=8u;
#else
    expected_pages=7u;
#endif
    if(!setjmp(done)) kui_retail_menu_return(0u,0u,0u);
    assert(resets==1u && snapshots==1u && captures==expected_pages);
    for(unsigned i=0;i<80u;i++) assert(words[i/16u][i%16u]==(unavailable?0u:seed[i]));
    unsigned extension=expected_pages-2u;
    for(unsigned i=0;i<32u;i++)
        assert(words[extension+i/16u][i%16u]==(unavailable?0u:seed[80u+i]));
#if KUI_TOY_PILOT_SHARED_SCI
    for(unsigned i=0;i<16u;i++) assert(words[5][i]==(unavailable?0u:seed[112u+i]));
#elif KUI_TOY_PILOT_GD_FIXED_STEP
    assert(words[5][0]==0x47444d31u && words[5][1]==KUI_TOY_PILOT_GD_FIXED_STEP);
    for(unsigned i=0;i<6u;i++) assert(words[5][i+2u]==timing[i]);
    for(unsigned i=0;i<4u;i++) assert(words[5][i+8u]==81u+i);
    for(unsigned i=12u;i<16u;i++) assert(!words[5][i]);
#endif
    for(unsigned i=expected_pages*64u;i<sizeof(image.block);i++) assert(image.block[i]==0xa5u);
    for(unsigned i=0;i<4u;i++)
        assert(image.before[i]==0xa5a5a5a5u && image.after[i]==0xa5a5a5a5u);
}
int main(void) {
    for(unsigned unavailable=0;unavailable<5u;unavailable++) check(unavailable);
    printf("Toy report%u shared%u: v%u block geometry, retained prefix/two-extension order, transport/GD insertion, exact snapshot admission, pre-reset retention, terminal fence and workspace guards checked\n",
        KUI_TOY_PILOT_GD_FIXED_STEP,KUI_TOY_PILOT_SHARED_SCI,KUI_TOY_PILOT_API);
    return 0;
}
'''.replace("/* PRODUCTION_REPORT */", production)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
parser.add_argument("--no-sanitizers", action="store_true")
args = parser.parse_args()
flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
         "-fno-pie", "-no-pie"]
if not args.no_sanitizers:
    flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
with tempfile.TemporaryDirectory(prefix="kui-toy-report-") as temporary:
    work = Path(temporary)
    test = work / "report.c"
    test.write_text(harness)
    for chunk, shared in ((0, 0), (2, 0), (3, 0), (3, 1)):
        binary = work / (f"report-{chunk}-shared-{shared}")
        subprocess.run(shlex.split(args.cc) + flags + ["-DKUI_TOY_PILOT_GD_FIXED_STEP=" + str(chunk),
            "-DKUI_TOY_PILOT_SHARED_SCI=" + str(shared),
            "-I" + str(ROOT / "include"), str(test), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"), check=True)
