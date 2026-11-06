#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Regress the photographed VC2 virtual-VRAM payload-copy path.

The policy harness compiles the actual production function with an authored
mapper and instrumented sinks.  A second harness executes the real halfword
copy on host RAM with odd boundaries and guard bytes.  Neither is a model of
the SH bus, CE page faults, VRAM, or DMA; those need console verification.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = "src/loader/retail_async.c"
BASELINE = "46577020a054ece45d9ab5feacc87986fd06a50e"


def actual_function(source):
    start = source.index("static bool native_payload_write(")
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


POLICY_PREFIX = r'''
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define KUI_VC2_TEXTURE_PROBE 0
#define KUI_NATIVE_GD_MMU_BUFFERS 1
#define KUI_RETAIL_MAP_DMA_WRITE 4
struct kui_retail_gd {
    struct {
        uint8_t *(*map)(void *,uint32_t,uint32_t,int);
        void *context;
    } ops;
    uint32_t cpu_mmu;
};
static struct { struct { struct kui_retail_gd service; } shared; } R;
static unsigned checks, cases, map_calls, byte_calls, half_calls;
static uint32_t wanted_address,wanted_count;
static int wanted_mode,reject_map;
static const char *label;
static uint8_t input[520],output[520];
#define CHECK(value) do { ++checks; if(!(value)) { \
    fprintf(stderr,"%s line %u: %s\n",label,__LINE__,#value);exit(1); \
} } while(0)
static uint8_t *map_payload(void *context,uint32_t address,uint32_t count,int mode) {
    ++map_calls;
    CHECK(context==(void *)&R);
    CHECK(address==wanted_address && count==wanted_count && mode==wanted_mode);
    return reject_map?NULL:output+7u;
}
static void *byte_copy(void *out,const void *in,size_t count) {
    ++byte_calls;CHECK(out==output+7u && in==input+3u && count==wanted_count);
    return memcpy(out,in,count);
}
static void native_vram_write(uint8_t *out,const uint8_t *in,uint32_t count) {
    ++half_calls;CHECK(out==output+7u && in==input+3u && count==wanted_count);
    memcpy(out,in,count);
}
#define memcpy byte_copy
'''

POLICY_SUFFIX = r'''
#undef memcpy
static void one(const char *name,uint32_t mmu,uint32_t address,uint32_t bytes,
                int mode,unsigned half,int refused) {
    label=name;++cases;
    memset(output,0x5a,sizeof(output));
    map_calls=byte_calls=half_calls=0u;
    wanted_address=address;wanted_count=bytes;wanted_mode=mode;reject_map=refused;
    R.shared.service.cpu_mmu=mmu;
    CHECK(native_payload_write(address,input+3u,bytes)==!refused);
    CHECK(map_calls==1u);
    if(refused)CHECK(half_calls==0u && byte_calls==0u);
    else {
        CHECK(half_calls==half && byte_calls==1u-half);
        CHECK(!memcmp(output+7u,input+3u,bytes));
    }
    for(unsigned i=0u;i<sizeof(output);i++)
        if(refused || i<7u || i>=7u+bytes)CHECK(output[i]==0x5au);
}
int main(void) {
    R.shared.service.ops.map=map_payload;R.shared.service.ops.context=&R;
    for(unsigned i=0u;i<sizeof(input);i++)input[i]=(uint8_t)(i*47u+13u);
    /* Hardware page1/2: VA4243E600, length1F0, physical0433E600.
     * Do not translate this VA into a DMA address: mapper must retain mode1.
     * Put this first so the old byte-copy implementation fails this case.
     */
    one("captured-virtual-vram",1u,0x4243e600u,0x1f0u,1,1u,0);
    one("virtual-vram-odd",1u,0x4243e601u,0x1f7u,1,1u,0);
    one("virtual-ram-u0",1u,0x01cf1978u,77u,1,1u,0);
    one("virtual-process-slot",1u,0x0c800001u,1u,1,1u,0);
    one("virtual-u0-end",1u,0x7ffffffeu,2u,1,1u,0);
    one("virtual-p3-start",1u,0xc0000000u,32u,1,1u,0);
    one("virtual-p3-odd",1u,0xdfffefefu,17u,1,1u,0);
    one("virtual-refused",1u,0x4243e600u,496u,1,1u,1);
    one("virtual-refused-p3",1u,0xc0010000u,15u,1,1u,1);
    one("direct-vram64-at1",1u,0xa433e600u,504u,4,1u,0);
    one("direct-vram32-at1",1u,0xa533e601u,17u,4,1u,0);
    one("direct-vram64-at0",0u,0xa433e600u,504u,4,1u,0);
    one("direct-vram32-at0",0u,0xa533e601u,17u,4,1u,0);
    one("direct-vram-refused",1u,0xa433e600u,504u,4,1u,1);
    one("p1-ram-at1",1u,0x8c800000u,504u,1,0u,0);
    one("p2-ram-at1",1u,0xac800001u,17u,1,0u,0);
    one("p1-ram-at0",0u,0x8c800000u,504u,1,0u,0);
    one("p2-ram-at0",0u,0xac800001u,17u,1,0u,0);
    one("mmu-off-u0-number",0u,0x4243e600u,496u,1,0u,0);
    one("mmu-off-p3-number",0u,0xc0010000u,33u,1,0u,0);
    /* Non-payload CPU regions are the mapper's rejection, not VRAM aliases. */
    one("p4-refused",1u,0xe0000000u,16u,1,0u,1);
    printf("{\"checks\":%u,\"cases\":%u}\n",checks,cases);
    return 0;
}
'''

COPY_HARNESS = r'''
#include "retail_vram_copy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static _Alignas(32) uint8_t output[1088],expected[1088],input[1056];
static unsigned checks,cases;
#define CHECK(value) do { ++checks; if(!(value)) { \
    fprintf(stderr,"real halfword copy line %u: %s\n",__LINE__,#value);exit(1); \
} } while(0)
static void compare(void) {CHECK(!memcmp(output,expected,sizeof(output)));}
int main(void) {
    for(unsigned i=0u;i<sizeof(input);i++)input[i]=(uint8_t)(i*109u+7u);
    for(unsigned offset=32u;offset<66u;offset++)
        for(unsigned count=0u;count<=513u;count++) {
            ++cases;memset(output,0x5a,sizeof(output));
            memcpy(expected,output,sizeof(output));
            memcpy(expected+offset,input+3u,count);
            native_vram_write(output+offset,input+3u,count);compare();
        }
    /* Repeated arbitrary cursor splits must compose into one exact payload.
     * Check the whole guard area after every callback, including the unowned
     * half of a read-modify-write boundary and a source/destination overlap
     * in cache/page index only (source bytes themselves remain separate).
     */
    const unsigned split[]={1u,17u,2u,255u,3u,511u,7u};
    for(unsigned offset=32u;offset<66u;offset++) {
        unsigned done=0u;++cases;memset(output,0xa6,sizeof(output));
        memcpy(expected,output,sizeof(output));
        for(unsigned i=0u;i<sizeof(split)/sizeof(split[0]);i++) {
            memcpy(expected+offset+done,input+1u+done,split[i]);
            native_vram_write(output+offset+done,input+1u+done,split[i]);
            done+=split[i];compare();
        }
    }
    printf("{\"checks\":%u,\"cases\":%u}\n",checks,cases);
    return 0;
}
'''


def sha(data):
    return hashlib.sha256(data).hexdigest()


def run_harness(folder, name, source, sanitizer):
    path = folder / (name + ".c")
    binary = folder / (name + "-" + sanitizer.replace(",", "-"))
    path.write_text(source)
    command = [os.environ.get("HOST_CC", "cc"), "-std=c11", "-O1", "-g",
               "-Wall", "-Wextra", "-Werror", "-Wpedantic", "-fno-pie",
               "-no-pie", "-fno-sanitize-recover=all", "-fsanitize=" + sanitizer,
               "-I" + str(ROOT / "src/loader"), str(path), "-o", str(binary)]
    subprocess.run(command, check=True, capture_output=True, text=True)
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1")
    return subprocess.run([str(binary)], env=env, capture_output=True, text=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--baseline-ref", default=BASELINE)
    args = parser.parse_args()
    candidate_text = (ROOT / SOURCE).read_text()
    candidate = actual_function(candidate_text)
    baseline_text = subprocess.run(["git", "show", args.baseline_ref + ":" + SOURCE],
                                   cwd=ROOT, capture_output=True, text=True, check=True).stdout
    baseline = actual_function(baseline_text)
    report = dict(schema="kui.native.virtual-vram-copy.v1", status="FAIL",
                  hardware_executed=False, source_sha256=sha(candidate_text.encode()),
                  function_sha256=sha(candidate.encode()), baseline_ref=args.baseline_ref,
                  copy_header_sha256=sha((ROOT / "src/loader/retail_vram_copy.h").read_bytes()),
                  runs={}, limits=["Mapper and sink instrumentation are authored host behavior.",
                      "Real halfword routine runs on host RAM; this does not emulate VRAM or SH bus widths.",
                      "No CE translation, DMA, target gateway, or physical console executes."])
    with tempfile.TemporaryDirectory(prefix="kui-virtual-vram-") as temp:
        folder = Path(temp)
        for sanitizer in ("undefined", "address,undefined"):
            policy = run_harness(folder, "policy", POLICY_PREFIX + candidate + POLICY_SUFFIX, sanitizer)
            if policy.returncode:
                raise AssertionError(policy.stdout + policy.stderr)
            copy = run_harness(folder, "halfword-copy", COPY_HARNESS, sanitizer)
            if copy.returncode:
                raise AssertionError(copy.stdout + copy.stderr)
            mutation = run_harness(folder, "baseline", POLICY_PREFIX + baseline + POLICY_SUFFIX, sanitizer)
            if mutation.returncode != 1 or "captured-virtual-vram" not in mutation.stderr:
                raise AssertionError("Baseline did not fail captured VRAM policy: " + mutation.stdout + mutation.stderr)
            report["runs"][sanitizer] = dict(policy=json.loads(policy.stdout),
                real_copy=json.loads(copy.stdout), baseline_rejected=True,
                baseline_failure=mutation.stderr.strip())
    report["status"] = "PASS"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
