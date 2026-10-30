#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise pacing and exact Standard dispatch/step bodies with the real GD core."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
resident = (ROOT / "src/loader/retail_resident.c").read_text()


def last_function(begin):
    start = resident.rindex(begin)
    return resident[start:resident.index("\n}\n", start) + 3]


adapter = last_function("static int32_t step(uint32_t r4, uint32_t r5)")
adapter += last_function("int32_t kui_retail_resident_dispatch(uint32_t r4, uint32_t r5,")
harness = r'''
#define main existing_gd_tests
#include "test_retail_gd.c"
#undef main
#include "kui/retail_pace.h"

static struct kui_retail_pace pace;
static struct { uint32_t paced, spun; } pacing;
static uint32_t kui_retail_hook_source, geometry, samples, faults;

static void video_sample(void) {
    uint32_t i=samples++, line=i&1u?500u:100u, period=525u, vbi=520u;
    switch(geometry) {
    case 0: period=1u;vbi=0u;break;
    case 1: vbi=period;break;
    case 2: line=period+(i&1u?100u:0u);break;
    case 3: period=i&1u?525u:625u;break;
    case 4: vbi=i&1u?520u:260u;break;
    default: line=40u;break;
    }
    kui_retail_pace_sample(&pace,line,vbi,(period-1u)<<16,1u);
}
static void report_fault(const char *message,uint32_t function,uint32_t param) {
    (void)message;(void)function;(void)param;++faults;
}
#include "standard_adapter_under_test.h"

static void pending_read_progress(void) {
    for(geometry=0;geometry<6u;++geometry) {
        reset();memset(&pace,0,sizeof(pace));memset(&pacing,0,sizeof(pacing));
        samples=faults=0;read_params(45000u,6u,OUTPUT);
        int32_t token=kui_retail_resident_dispatch(KUI_GD_DMAREAD,PARAM,0,KUI_GD_REQUEST);
        CHECK(token>0 && service.pending && !ctx.reads);
        for(unsigned polls=1;polls<=48u;++polls) {
            int32_t status=kui_retail_resident_dispatch((uint32_t)token,STATUS,0,KUI_GD_CHECK);
            CHECK(status==(polls==48u?KUI_GD_COMPLETED:KUI_GD_PROCESSING));
            CHECK(ctx.reads==polls/16u && ctx.sectors==2u*(polls/16u));
            CHECK(service.completed_bytes==4096u*(polls/16u));
            CHECK(!service.error && !faults);
            if(geometry<5u) CHECK(!pace.per && !pace.still);
        }
        CHECK(!service.pending && !service.command && pacing.spun==3u);
        CHECK(service.diag.exec_calls==3u); /* internal steps; no caller EXEC */
        CHECK(ctx.max_count==2u); /* invalid geometry cannot enlarge a batch */
        for(uint32_t n=0;n<6u;++n)
            for(uint32_t byte=0;byte<2048u;++byte)
                CHECK(ram[OUTPUT-BEGIN+n*2048u+byte]==pattern(45000u+n,byte));
        CHECK(kui_retail_resident_dispatch((uint32_t)token,STATUS,0,KUI_GD_CHECK)==KUI_GD_NOT_FOUND);
        CHECK(ctx.reads==3u && !faults);
    }
}

int main(void) {
    pending_read_progress();
    printf("Standard CHECK-only progress: %u checks; six accepted DMAREAD cases, "
           "exact dispatch/step, invalid/changing/valid SPG, bounded batches and complete output\n",assertions);
    return 0;
}
'''

cc = shlex.split(os.environ.get("HOST_CC", "cc"))
flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
         "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
         "-Iinclude", "-Itests"]
env = {**os.environ, "ASAN_OPTIONS": "detect_leaks=0"}
with tempfile.TemporaryDirectory(prefix="kui-retail-pace-") as tmp:
    out = Path(tmp)
    (out / "standard_adapter_under_test.h").write_text(adapter)
    (out / "adapter.c").write_text(harness)
    for name, sources in [
        ("pace", ["tests/test_retail_pace.c", "src/core/retail_pace.c"]),
        ("adapter", [str(out / "adapter.c"), "src/core/retail_gd.c", "src/core/retail_pace.c"]),
    ]:
        binary = out / name
        subprocess.run([*cc, *flags, "-I", str(out), *sources, "-o", str(binary)],
                       cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, env=env, check=True)
