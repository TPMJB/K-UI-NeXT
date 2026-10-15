#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the production terminal snapshot against bounded synthetic P1 RAM.

The fixed console alias overlaps ASan shadow RAM, so this fixture uses UBSan.
It extracts the production capture helper instead of duplicating its guards.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/loader/retail_resident.c").read_text()
start = source.index("static void capture_native_fault(uint32_t function, uint32_t current_param) {")
brace = source.index("{", start)
depth = 1
end = brace + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
capture = source[start:end]

harness = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "kui/retail_gd.h"
#include "kui/retail_loader_layout.h"

static struct kui_retail_gd service;
static struct kui_retail_image *test_image;
#define image (*test_image)
static volatile uint32_t kui_retail_native_caller[2];
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { \
    fprintf(stderr, "caller trace assertion at line %u: %s\n", __LINE__, #x); \
    abort(); } } while(0)

/* PRODUCTION_CAPTURE */

static void reset(uint32_t sp, uint32_t pr, uint32_t reject) {
    memset(&service, 0, sizeof(service));
    memset(test_image, 0, sizeof(*test_image));
    memset(image.block, 0xa5, sizeof(image.block));
    service.diag.last_command = KUI_GD_DMAREAD;
    service.diag.reject_reason = reject;
    /* These previous request fields must not enter the current snapshot. */
    service.diag.last_lba = 0xdeadbeefu;
    service.diag.last_count = 0xcafef00du;
    service.diag.last_destination = 0xfeedbeefu;
    kui_retail_native_caller[0] = pr;
    kui_retail_native_caller[1] = sp;
}
static void inspect(uint32_t sp, uint32_t pr, uint32_t reject,
                    uint32_t function, uint32_t current_param, int traced) {
    const uint32_t saved[3] = {0x8c604e50u, 0x8c604c98u, 0x8c09b4deu};
    if(traced) {
        const unsigned offsets[3] = {20, 36, 60};
        for(unsigned i=0;i<3;i++)
            memcpy((void *)(uintptr_t)(sp+offsets[i]), saved+i, sizeof(saved[i]));
    }
    uint8_t before[sizeof(image.block)];
    memcpy(before, image.block, sizeof(before));
    capture_native_fault(function, current_param);
    uint32_t snapshot[9];
    memcpy(snapshot, image.block, sizeof(snapshot));
    CHECK(snapshot[0] == function);
    CHECK(snapshot[1] == KUI_GD_DMAREAD);
    CHECK(snapshot[2] == reject);
    CHECK(snapshot[3] == sp);
    CHECK(snapshot[4] == (function == KUI_GD_REQUEST &&
        reject == KUI_RETAIL_GD_REJECT_PARAMETERS ? current_param : 0));
    CHECK(snapshot[5] == pr);
    for(unsigned i=0;i<3;i++) CHECK(snapshot[6+i] == (traced ? saved[i] : 0));
    for(unsigned i=sizeof(snapshot);i<sizeof(image.block);i++)
        CHECK(image.block[i] == before[i]);
}
int main(void) {
    void *ram = mmap((void *)(uintptr_t)0x8c000000u, 0x1000000u,
        PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE, -1, 0);
    if(ram == MAP_FAILED) { perror("map synthetic caller P1 RAM"); return 1; }
    test_image = (struct kui_retail_image *)(uintptr_t)0x8c009000u;
    const uint32_t pr = 0x8c648d7au;
    const uint32_t valid[] = {0x8c00b9f0u, 0x8c00b338u, KUI_RETAIL_IP_ADDRESS,
        KUI_RETAIL_RAM_END-64u};
    for(unsigned i=0;i<sizeof(valid)/sizeof(valid[0]);i++) {
        reset(valid[i],pr,KUI_RETAIL_GD_REJECT_PARAMETERS);
        inspect(valid[i],pr,KUI_RETAIL_GD_REJECT_PARAMETERS,
            KUI_GD_REQUEST,valid[i]+4u,1);
    }
    /* Trace sources coincide with snapshot words 0, 4 and 10. Loading all
     * source words first is essential: the first snapshot store destroys S20. */
    uint32_t overlap = (uint32_t)(uintptr_t)image.block-20u;
    reset(overlap,pr,KUI_RETAIL_GD_REJECT_PARAMETERS);
    inspect(overlap,pr,KUI_RETAIL_GD_REJECT_PARAMETERS,
        KUI_GD_REQUEST,0x8c00b9f4u,1);
    const uint32_t invalid[] = {0x0c00b9f0u,0xac00b9f0u,0x8c007ffcu,
        0x8c00b9f1u,0x8c00b9f2u,0x8c00b9f3u,KUI_RETAIL_RAM_END-60u,
        KUI_RETAIL_RAM_END,0xffffffc0u,0xfffffffcu,0u};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        reset(invalid[i],pr,KUI_RETAIL_GD_REJECT_PARAMETERS);
        inspect(invalid[i],pr,KUI_RETAIL_GD_REJECT_PARAMETERS,
            KUI_GD_REQUEST,0x8c00b9f4u,0);
    }
    /* Grandia's PR and synthetic unrelated callers use the same safe raw-word
     * capture. This does not identify an ancestor or imply a wrapper ABI. */
    const uint32_t other_pr[] = {0x8c07cb22u,0,pr-2u,pr+2u,0xac648d7au};
    for(unsigned i=0;i<sizeof(other_pr)/sizeof(other_pr[0]);i++) {
        reset(valid[1],other_pr[i],KUI_RETAIL_GD_REJECT_PARAMETERS);
        inspect(valid[1],other_pr[i],KUI_RETAIL_GD_REJECT_PARAMETERS,
            KUI_GD_REQUEST,0x8c00b360u,1);
    }
    /* Record R5 verbatim without dereferencing it, including null, aliases,
     * unaligned and wrapped values. It must not be synthesized from SP. */
    const uint32_t params[] = {0,0x0c00b360u,0xac00b360u,0x8c00b360u,
        0x8c00b361u,0xffffffffu,1u};
    for(unsigned i=0;i<sizeof(params)/sizeof(params[0]);i++) {
        reset(valid[1],other_pr[0],KUI_RETAIL_GD_REJECT_PARAMETERS);
        inspect(valid[1],other_pr[0],KUI_RETAIL_GD_REJECT_PARAMETERS,
            KUI_GD_REQUEST,params[i],1);
    }
    for(uint32_t reject=0;reject<=KUI_RETAIL_GD_REJECT_FUNCTION_ARGUMENT;reject++) {
        if(reject == KUI_RETAIL_GD_REJECT_PARAMETERS) continue;
        reset(valid[0],pr,reject);
        inspect(valid[0],pr,reject,KUI_GD_REQUEST,0xffffffffu,0);
    }
    for(uint32_t function=1;function<=KUI_GD_DATATYPE+1u;function++) {
        reset(valid[1],other_pr[0],KUI_RETAIL_GD_REJECT_PARAMETERS);
        inspect(valid[1],other_pr[0],KUI_RETAIL_GD_REJECT_PARAMETERS,
            function,0xffffffffu,0);
    }
    CHECK(!munmap(ram,0x1000000u));
    printf("PASS native caller snapshot: %u checks, current R5, guards, bounds, aliases, overlap\n", checks);
    return 0;
}
'''.replace("/* PRODUCTION_CAPTURE */", capture)

with tempfile.TemporaryDirectory(prefix="kui-sonic-caller-") as temporary:
    work = Path(temporary)
    test = work / "capture.c"
    binary = work / "capture"
    test.write_text(harness)
    compiler = shlex.split(os.environ.get("CC", "cc"))
    subprocess.run(compiler + ["-std=c11", "-O2", "-flto", "-fstrict-aliasing",
        "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fsanitize=undefined",
        "-fno-omit-frame-pointer", "-I" + str(ROOT / "include"), str(test),
        "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
