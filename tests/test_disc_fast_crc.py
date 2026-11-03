#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prove the optional physical-stage CRC keeps packet/hash admission unchanged."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FLAGS = ("-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
         "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
         "-fno-pie", "-no-pie", "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
         "-Iinclude", "-I.deps/fatfs/source", "-Isrc/loader")
CORE = ("src/core/disc_launch.c", "src/core/hash.c", "src/core/data.c")
CANONICAL = r'''
#include "kui/disc_launch.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void put(uint8_t *p,uint32_t v) {
    for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(v>>(8u*i));
}
static void digest(const void *p,size_t bytes,uint8_t out[32]) {
    struct kui_sha256 sha;kui_sha256_init(&sha);kui_sha256_update(&sha,p,bytes);kui_sha256_digest(&sha,out);
}
int main(void) {
    const uint32_t sizes[]={128u,129u,0x400001u,5460080u,KUI_DISC_LAUNCH_EXEC_MAX_BYTES};
    for(unsigned flags=1u;flags<=3u;flags+=2u)for(unsigned n=0;n<sizeof(sizes)/sizeof(sizes[0]);n++) {
        const uint32_t module=0x2104u;
        uint8_t *p=calloc(1u,(size_t)module+32768u+sizes[n]+8192u);assert(p);
        memcpy(p+0x100u,"KUIDISC1",8u);
        const uint32_t h[]={1u,64u,0x1000u,512u,0x8ce00000u,module-0x2000u,
            0x8ce00000u,0x8c010000u,KUI_DISC_LAUNCH_EXEC_MAX_BYTES,0x8cff0000u,0x2000u,flags,0u,0u};
        for(unsigned i=0;i<14u;i++)put(p+0x108u+i*4u,h[i]);
        struct kui_disc_launch_plan plan,out;assert(kui_disc_launch_layout(p,module,sizes[n],&plan));
        uint8_t *ip=p+plan.ip_offset,*exec=p+plan.exec_offset;
        memset(ip,' ',32768u);memcpy(ip,"SEGA SEGAKATANA ",16u);
        memcpy(ip+37u,"GD-ROM",6u);memcpy(ip+56u,"0000000 ",8u);
        for(uint32_t i=0;i<sizes[n];i++)exec[i]=(uint8_t)(i*37u+(i>>11u)+29u);
        plan.ip_crc32=kui_crc32(0u,ip,32768u);plan.exec_crc32=kui_crc32(0u,exec,sizes[n]);
        digest(ip,32768u,plan.ip_sha256);digest(exec,sizes[n],plan.exec_sha256);
        if(plan.vmu_profile_bytes) {
            uint8_t *profile=p+plan.vmu_profile_offset;
            for(uint32_t i=0;i<plan.vmu_profile_bytes;i++)profile[i]=(uint8_t)(i*19u+7u);
            digest(profile,plan.vmu_profile_bytes,plan.vmu_profile_sha256);
        }
        assert(kui_disc_launch_encode(p+0x1000u,&plan));
        assert(kui_disc_launch_validate(p,plan.payload_bytes,&out));
        assert(!memcmp(&plan,&out,sizeof(plan)));
        uint8_t sha[32];char hex[65];digest(p,plan.payload_bytes,sha);kui_hex(sha,sizeof(sha),hex);
        printf("%u %u %08x %s\n",flags,sizes[n],kui_crc32(0u,p,plan.payload_bytes),hex);free(p);
    }
    return 0;
}
'''


def run(*args):
    result = subprocess.run(args, cwd=ROOT, text=True, capture_output=True,
                            env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0",
                                 "UBSAN_OPTIONS": "halt_on_error=1"})
    if result.returncode:
        print(result.stdout, end="")
        print(result.stderr, end="")
        raise SystemExit(f"Disc stage CRC check failed ({result.returncode}): {args}")
    return result.stdout


def build(path, sources, fast=False):
    run(*shlex.split(os.environ.get("CC", "cc")), *FLAGS,
        *(("-DKUI_DISC_STAGE_FAST_CRC32=1",) if fast else ()), *map(str, sources), "-o", str(path))


with tempfile.TemporaryDirectory(prefix="kui-disc-fast-crc-") as folder:
    base = Path(folder)
    primitive = base / "primitive"
    build(primitive, ("tests/test_stage_fast_crc.c", "src/core/retail_image.c"))
    print(run(str(primitive)), end="", flush=True)
    for name in ("test_disc_launch", "test_disc_launch_adversarial", "test_disc_launch_vmu"):
        outputs = []
        for fast in (False, True):
            binary = base / (name + ("-fast" if fast else "-compact"))
            build(binary, ("tests/" + name + ".c", *CORE), fast)
            outputs.append(run(str(binary)))
        assert outputs[0] == outputs[1], f"Admission results differ for {name}"
        print(f"PASS compact/fast physical-stage CRC admission: {name}", flush=True)
    source = base / "canonical.c"
    source.write_text(CANONICAL)
    outputs = []
    for fast in (False, True):
        binary = base / ("canonical-fast" if fast else "canonical-compact")
        build(binary, (source, *CORE), fast)
        outputs.append(run(str(binary)))
    assert outputs[0] == outputs[1] and len(outputs[0].splitlines()) == 10
    print("PASS identical canonical packet/payload CRC32 and SHA-256: both modes, exact Skies length and 12 MiB")
print("PASS physical-stage fast CRC: full checks retained; generic CRC API unchanged")
