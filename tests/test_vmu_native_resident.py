#!/usr/bin/env python3
"""Compile the production native resident guards on the host; no SH/MMIO calls."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/loader/vmu_maple_observe_resident.c").read_text()
guard = re.search(
    r"/\* NATIVE_RESIDENT_GUARDS_BEGIN.*?\*/\n(.*?)/\* NATIVE_RESIDENT_GUARDS_END \*/",
    source, re.S,
)
assert guard, "Production native resident guard section is missing"
assert re.search(r"#if KUI_VMU_NATIVE_PASSIVE_PROBE\n.*?native_resident_destination\(c,address,bytes\)", source, re.S)
assert re.search(r"#if KUI_VMU_NATIVE_PASSIVE_PROBE\n    if\(native_resident_terminal\(c\)\)", source)

checks = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(expr) do {++checks;if(!(expr)) { \
    fprintf(stderr,"line %u: %s\n",(unsigned)__LINE__,#expr);exit(1);}} while(0)
static struct kui_vmu_observe_boot_control installed(uint32_t payload) {
    return (struct kui_vmu_observe_boot_control){
        .status=KUI_VMU_OBS_BOOT_INSTALLED,.lease=payload,
        .worker_end=(payload|UINT32_C(0xa0000000))+UINT32_C(0x10000),
        .terminal=(payload|UINT32_C(0xa0000000))+UINT32_C(0x100)};
}
int main(void) {
    struct kui_vmu_observe_boot_control c={0},before;
    const uint32_t aliases[]={0,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    for(unsigned status=0;status<=KUI_VMU_OBS_BOOT_INITIALIZING;status++) {
        c.status=status;c.installer=UINT32_C(0x8ce00100);c.stage_stack=UINT32_C(0x8cfe0000);
        CHECK(native_resident_destination(&c,UINT32_C(0x8c100000),2048));
        CHECK(!native_resident_terminal(&c));
        c.lease=UINT32_C(0x0c38a880);
        CHECK(!native_resident_destination(&c,UINT32_C(0x8c100000),2048));c.lease=0;
    }
    const uint32_t payloads[]={UINT32_C(0x0c010020),UINT32_C(0x0c38a880),UINT32_C(0x0cfd0000)};
    for(unsigned p=0;p<sizeof(payloads)/sizeof(payloads[0]);p++) {
        c=installed(payloads[p]);before=c;
        uint32_t first=c.lease-32u,last=c.lease+UINT32_C(0x30000);
        for(unsigned a=0;a<3;a++) {
            CHECK(native_resident_destination(&c,(first-4u)|aliases[a],4));
            CHECK(!native_resident_destination(&c,(first-4u)|aliases[a],5));
            CHECK(!native_resident_destination(&c,first|aliases[a],1));
            CHECK(!native_resident_destination(&c,c.lease|aliases[a],2048));
            CHECK(!native_resident_destination(&c,(last-1u)|aliases[a],1));
            if(last<UINT32_C(0x0d000000)) CHECK(native_resident_destination(&c,last|aliases[a],1));
        }
        CHECK(native_resident_terminal(&c));CHECK(!memcmp(&before,&c,sizeof(c)));
        c.terminal=(c.lease|UINT32_C(0xa0000000))+64u;CHECK(native_resident_terminal(&c));
        c.terminal=c.worker_end-2u;CHECK(native_resident_terminal(&c));
        const uint32_t terminals[]={0,UINT32_C(0x8ce00100),UINT32_C(0xacff0100),
            (c.lease|UINT32_C(0x80000000))+256u,c.lease+256u,
            (c.lease|UINT32_C(0xa0000000)),(c.lease|UINT32_C(0xa0000000))+62u,
            (c.lease|UINT32_C(0xa0000000))+65u,c.worker_end,c.worker_end+2u};
        for(unsigned t=0;t<sizeof(terminals)/sizeof(terminals[0]);t++) {
            c.terminal=terminals[t];CHECK(!native_resident_terminal(&c));
        }
        c=installed(payloads[p]);c.installer=UINT32_C(0x8ce00100);
        CHECK(!native_resident_terminal(&c));
        CHECK(!native_resident_destination(&c,UINT32_C(0x8c100000),2048));
        c=installed(payloads[p]);c.stage_stack=UINT32_C(0x8cfe0000);
        CHECK(!native_resident_terminal(&c));
        CHECK(!native_resident_destination(&c,UINT32_C(0x8c100000),2048));
        c=installed(payloads[p]);c.worker_end^=32u;
        CHECK(!native_resident_terminal(&c));
        CHECK(!native_resident_destination(&c,UINT32_C(0x8c100000),2048));
    }
    c=installed(UINT32_C(0x0c38a880));
    for(unsigned status=0;status<16u;status++) {
        c.status=status;
        CHECK(native_resident_terminal(&c)==(status==KUI_VMU_OBS_BOOT_INSTALLED));
        CHECK(native_resident_destination(&c,UINT32_C(0x8c100000),2048)==
            (status==KUI_VMU_OBS_BOOT_INSTALLED));
    }
    const uint32_t invalid_lease[]={0,UINT32_C(0x0c010000),UINT32_C(0x0c38a881),
        UINT32_C(0x0cfd0020),UINT32_C(0x8c38a880),UINT32_C(0xac38a880),UINT32_MAX};
    for(unsigned i=0;i<sizeof(invalid_lease)/sizeof(invalid_lease[0]);i++) {
        c=installed(invalid_lease[i]);
        CHECK(!native_resident_terminal(&c));
        CHECK(!native_resident_destination(&c,UINT32_C(0x8c100000),2048));
    }
    c=installed(UINT32_C(0x0c38a880));
    const uint32_t invalid_destination[]={0,UINT32_C(0x0bffffff),UINT32_C(0x0d000000),
        UINT32_C(0x4c100000),UINT32_C(0xcc100000),UINT32_C(0xff000000)};
    for(unsigned i=0;i<sizeof(invalid_destination)/sizeof(invalid_destination[0]);i++)
        CHECK(!native_resident_destination(&c,invalid_destination[i],32));
    CHECK(!native_resident_destination(&c,UINT32_C(0x8c100000),0));
    CHECK(!native_resident_destination(&c,UINT32_C(0x8c100000),UINT32_MAX));
    CHECK(!native_resident_destination(&c,UINT32_C(0x8cffffff),2));
    /* Independently compare interval intersections over all three aliases. */
    uint32_t random=UINT32_C(0x12e58f91);
    for(unsigned trial=0;trial<20000u;trial++) {
        random=random*UINT32_C(1664525)+UINT32_C(1013904223);
        uint32_t payload=UINT32_C(0x0c010020)+((random%UINT32_C(0x7d7ff))*32u);
        c=installed(payload);
        random=random*UINT32_C(1664525)+UINT32_C(1013904223);
        uint32_t physical=UINT32_C(0x0c000000)+(random&UINT32_C(0x00ffffff));
        random=random*UINT32_C(1664525)+UINT32_C(1013904223);
        uint32_t size=(random&UINT32_C(0x000fffff))+1u;
        int expected=(uint64_t)physical+size<=UINT32_C(0x0d000000) &&
            ((uint64_t)physical+size<=payload-32u || physical>=payload+UINT32_C(0x30000));
        for(unsigned a=0;a<3;a++) CHECK(native_resident_destination(&c,physical|aliases[a],size)==expected);
        CHECK(native_resident_terminal(&c));
    }
    printf("Native resident guard checks: %u passed\n",checks);
}
'''

with tempfile.TemporaryDirectory(prefix="kui-native-resident-") as tmp:
    path = Path(tmp)
    test = path / "test.c"
    test.write_text('#include <stdbool.h>\n#include "kui/vmu_maple_observe_boot.h"\n' + guard.group(1) + checks)
    binary = path / "test"
    subprocess.run([
        os.environ.get("HOST_CC", "cc"), "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
        "-Wpedantic", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
        "-I", str(ROOT / "include"), str(test), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
