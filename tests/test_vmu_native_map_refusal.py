#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the production native map/guard wiring with a terminal host trap."""
from pathlib import Path
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/loader/vmu_maple_observe_resident.c").read_text()
guard = re.search(r"/\* NATIVE_RESIDENT_GUARDS_BEGIN.*?\*/\n(.*?)/\* NATIVE_RESIDENT_GUARDS_END \*/", source, re.S)
mapping = re.search(r"(static uint8_t \*observe_map\(.*?\n\})\n\nint kui_retail_resident_init", source, re.S)
assert guard and mapping
# Host addresses cannot carry the SH P2 alias. Change only this conversion;
# all production predicates, conditional routes and forwarding code remain.
mapping = mapping.group(1).replace(
    "((uintptr_t)&kui_vmu_observe_boot_control+UINT32_C(0x20000000))",
    "((uintptr_t)&kui_vmu_observe_boot_control)",
)
assert "+UINT32_C(0x20000000)" not in mapping

fixture = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "kui/vmu_maple_observe_boot.h"
struct kui_vmu_observe_boot_control kui_vmu_observe_boot_control;
static jmp_buf terminal;
static unsigned checks,mapped,stopped;
static uintptr_t got_context;
static uint32_t got_address,got_bytes;
static int got_writing;
static uint8_t owner[64];
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"line%u: %s\n",__LINE__,#x);exit(1);}} while(0)
static uint8_t *map_guest(void *context,uint32_t address,uint32_t bytes,int writing) {
    ++mapped;got_context=(uintptr_t)context;got_address=address;got_bytes=bytes;got_writing=writing;
    return owner;
}
void kui_vmu_observe_low_failure(void) {++stopped;longjmp(terminal,1);}
'''
cases = r'''
static void exercise(uint32_t address,uint32_t bytes,int writing) {
    const struct kui_vmu_observe_boot_control before=kui_vmu_observe_boot_control;
    uint8_t saved[sizeof(owner)];memcpy(saved,owner,sizeof(owner));
    bool accepted=native_resident_destination(&kui_vmu_observe_boot_control,address,bytes);
    mapped=stopped=0;
    if(!setjmp(terminal)) {
        uint8_t *result=observe_map((void *)(uintptr_t)0x1234u,address,bytes,writing);
        if(accepted) {
            CHECK(result==owner && mapped==1 && !stopped);
            CHECK(got_context==0x1234u && got_address==address && got_bytes==bytes && got_writing==writing);
        } else {
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
            CHECK(false);
#else
            CHECK(!result && !mapped && !stopped);
#endif
        }
    } else {
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
        CHECK(!accepted && stopped==1 && !mapped);
#else
        CHECK(false);
#endif
    }
    CHECK(!memcmp(&before,&kui_vmu_observe_boot_control,sizeof(before)));
    CHECK(!memcmp(saved,owner,sizeof(owner)));
}
int main(void) {
    const uint32_t payloads[]={0x0c010020u,0x0c38a880u,0x0cfd0000u};
    const uint32_t aliases[]={0,0x80000000u,0xa0000000u};
    for(unsigned p=0;p<3;p++) {
        struct kui_vmu_observe_boot_control *c=&kui_vmu_observe_boot_control;
        *c=(struct kui_vmu_observe_boot_control){.status=KUI_VMU_OBS_BOOT_INSTALLED,.lease=payloads[p],
            .worker_end=(payloads[p]|0xa0000000u)+NATIVE_RESIDENT_WORKER_BYTES,
            .terminal=(payloads[p]|
#if KUI_VMU_NATIVE_CACHED_CODE
            0x80000000u
#else
            0xa0000000u
#endif
            )+256u};
        CHECK(native_resident_terminal(c));
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
        c->sd_transaction=c->terminal+2;CHECK(native_resident_transaction(c,c->sd_transaction));
#endif
        for(unsigned a=0;a<3;a++) for(int writing=-1;writing<=1;writing++) {
            exercise((payloads[p]-36u)|aliases[a],4u,writing);
            exercise((payloads[p]-36u)|aliases[a],5u,writing);
            exercise(payloads[p]|aliases[a],16u,writing);
            exercise((payloads[p]+0x30000u-1u)|aliases[a],1u,writing);
            exercise(0x0c100000u|aliases[a],0u,writing);
        }
        const struct kui_vmu_observe_boot_control good=*c;
        for(unsigned status=0;status<16;status++){*c=good;c->status=status;exercise(0x8c100000u,16u,1);}
        *c=good;c->installer=4;exercise(0x8c100000u,16u,1);
        *c=good;c->stage_stack=4;exercise(0x8c100000u,16u,1);
        *c=good;c->worker_end+=4;exercise(0x8c100000u,16u,1);
        *c=good;c->lease|=1;exercise(0x8c100000u,16u,1);
        *c=good;
        uint32_t random=0x23456781u;
        for(unsigned i=0;i<20000;i++) {
            random=random*1664525u+1013904223u;uint32_t address=0x0c000000u+(random&0xffffffu);
            random=random*1664525u+1013904223u;
            exercise(address|aliases[i%3],(random&0xfffffu)+1u,(int)(i%3)-1);
        }
    }
    printf("Native map refusal wiring: %u checks, persistent=%u cached=%u, accepted forwards/failure-before-write passed\n",
        checks,KUI_VMU_NATIVE_PERSISTENT_PROBE,KUI_VMU_NATIVE_CACHED_CODE);
}
'''
with tempfile.TemporaryDirectory(prefix="kui-native-map-refusal-") as temporary:
    directory = Path(temporary)
    path = directory / "test.c"
    path.write_text(fixture + guard.group(1) + mapping + cases)
    for persistent, cached in ((0, 0), (1, 0), (1, 1)):
        binary = directory / f"test-{persistent}-{cached}"
        subprocess.run([
            os.environ.get("HOST_CC", "cc"), "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-Wpedantic", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie",
            "-I", str(ROOT / "include"), "-DKUI_VMU_NATIVE_PASSIVE_PROBE=1",
            f"-DKUI_VMU_NATIVE_PERSISTENT_PROBE={persistent}", f"-DKUI_VMU_NATIVE_CACHED_CODE={cached}",
            str(path), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True, env={**os.environ,"ASAN_OPTIONS":"detect_leaks=0"})
