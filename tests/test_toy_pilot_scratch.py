#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise production native scratch scoping with the real Toy GD/core.

The actual resident map and dispatch functions are extracted without replacing
their guards. Native instruction execution is not simulated: scalar record
retirement uses the observed numerical CHECK contract, and caller PR/SP model
the native veneer interrupted on the private worker stack.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


resident = (ROOT / "src/loader/toy_pilot_resident.c").read_text()
mapping = function(resident, "static uint8_t *toy_map(")
dispatch = function(resident, "int32_t kui_retail_resident_dispatch(")
stage = (ROOT / "src/loader/toy_pilot_stage.c").read_text()
stage_entry = function(stage, "static int toy_entry(")
stage_exports = function(stage, "static void toy_exports_check(")

harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include "kui/toy_pilot.h"
#include "kui/toy_pilot_boot.h"
#include "kui/toy_pilot_gd.h"
#include "kui/toy_pilot_scratch.h"

#define RAM UINT32_C(0x8c100000)
#define PARAM (RAM+0x100u)
#define OUTPUT (RAM+0x200u)
#define STACK_TOP UINT32_C(0x8cfd65c0)
#define SCRATCH UINT32_C(0x8cfd63b8)
#define STACK_BOTTOM (STACK_TOP-KUI_TOY_PILOT_STACK_BYTES)
#define LEASE_BEGIN UINT32_C(0x8cfcffe0)
#define LEASE_END UINT32_C(0x8cfe0000)

static uint8_t ram[4096], lease[LEASE_END-LEASE_BEGIN];
static struct kui_retail_gd service;
static struct kui_toy_pilot_snapshot snapshot;
static uint32_t toy_scratch, native_owner, native_token, native_busy;
static uint32_t native_status, native_error, pumps, checks, refusals;
static uint32_t simulated_sr;
static uint8_t toy_worker_blob[512];
static jmp_buf stage_escape;
static uint32_t stage_error;
volatile struct kui_toy_pilot_boot_control kui_toy_pilot_boot_control;
static volatile uint32_t kui_retail_native_caller[2],kui_retail_hook_source;

extern int32_t kui_toy_pilot_gd_dispatch(struct kui_retail_gd *,uint32_t,
    uint32_t,uint32_t,uintptr_t);
static uint8_t *cpu_memory(uint32_t address,uint32_t bytes) {
    if(address>=RAM && address-RAM<=sizeof(ram) && bytes<=sizeof(ram)-(address-RAM))
        return ram+address-RAM;
    if(address>=LEASE_BEGIN && address-LEASE_BEGIN<=sizeof(lease) &&
       bytes<=sizeof(lease)-(address-LEASE_BEGIN)) return lease+address-LEASE_BEGIN;
    return NULL;
}
static void put(uint32_t address,uint32_t value) {
    uint8_t *p=cpu_memory(address,4u);assert(p);memcpy(p,&value,4u);
}
static uint32_t get(uint32_t address) {
    uint32_t value;uint8_t *p=cpu_memory(address,4u);assert(p);memcpy(&value,p,4u);return value;
}
/* The ordinary guest mapper is a hosted RAM fixture. Production toy_map
 * retains ownership protection and the actual scoped scratch predicate. */
static uint8_t *map_guest(void *context,uint32_t address,uint32_t bytes,int writing) {
    (void)context;(void)writing;return cpu_memory(address,bytes);
}
static int extent(void *context,uint32_t lba,uint32_t count,uint32_t bytes) {
    (void)context;(void)lba;(void)count;(void)bytes;return 0;
}
static int image_read(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *out) {
    (void)context;(void)lba;(void)count;(void)bytes;(void)out;
    assert(!"Scalar scratch regression must not read game data");return -1;
}
static int32_t kui_toy_pilot_base_dispatch(uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7) {
    return kui_retail_gd_dispatch(&service,r4,r5,r6,r7);
}
/* PRODUCTION_MAP */
/* PRODUCTION_DISPATCH */

uint32_t kui_toy_pilot_request(uint32_t command,uint32_t p0,uint32_t p1,uint32_t p2) {
    snapshot.command=command;snapshot.parameters[0]=p0;
    snapshot.parameters[1]=p1;snapshot.parameters[2]=p2;return ++snapshot.generation;
}
const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void) { return &snapshot; }
static int32_t call(uint32_t function,uint32_t r4,uint32_t r5,uint32_t pr,uint32_t sp) {
    kui_retail_native_caller[0]=pr;kui_retail_native_caller[1]=sp;
    uint32_t inherited=simulated_sr;
    int32_t result=kui_retail_resident_dispatch(r4,r5,0u,function);
    assert(!toy_scratch && simulated_sr==inherited);return result;
}
static void reset(void) {
    static const union kui_retail_slot tracks[]={
        {.track={.start_lba=0,.end_lba=8,.control=4}},
        {.track={.start_lba=16,.end_lba=24}},
        {.track={.start_lba=45000,.end_lba=60000,.control=4}}
    };
    const struct kui_gd_ops ops={NULL,toy_map,extent,image_read};
    memset(ram,0,sizeof(ram));memset(lease,0xa5,sizeof(lease));
    memset(cpu_memory(SCRATCH,16u),0,16u);
    assert(!kui_retail_gd_init(&service,tracks,3u,&ops,0x8c008000u,0x8d000000u));
    service.token=128u;
    snapshot=(struct kui_toy_pilot_snapshot){.generation=1u,.applied_generation=1u,
        .state=KUI_TOY_PILOT_PLAYING,.track=2u,.position_fad=166u,.end_fad=174u};
    assert((uintptr_t)kui_toy_pilot_gd_dispatch<=UINT32_MAX);
    kui_toy_pilot_boot_control=(struct kui_toy_pilot_boot_control){
        .status=KUI_TOY_BOOT_INSTALLED,.worker_end=STACK_TOP,
        .gd_dispatch=(uint32_t)(uintptr_t)kui_toy_pilot_gd_dispatch};
    kui_retail_hook_source=1u;toy_scratch=native_owner=native_token=native_busy=0u;
    native_status=native_error=pumps=checks=refusals=0u;simulated_sr=0x500000f1u;
}
static void play(void) {
    put(PARAM,2u);put(PARAM+4u,2u);put(PARAM+8u,0u);
    int32_t token=call(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,PARAM,0u,0u);
    assert(token==129);native_token=(uint32_t)token;native_owner=1u;
    assert(call(KUI_GD_EXEC,0,0,0,0)==0);
    assert(service.command==KUI_RETAIL_GD_PLAY && !service.pending &&
           service.status==KUI_GD_COMPLETED);
}
/* Native IRQ callback inherits the private worker stack. Its CHECK veneer
 * passes its owned16-byte SP scratch, unlike the authored manual pump that
 * deliberately borrows the suspended game stack. */
static void vblank(void) {
    ++pumps;assert(!native_busy);assert(call(KUI_GD_EXEC,0,0,0,0)==0);
    if(!native_owner) return;
    native_busy=1u;
    int32_t bios=call(KUI_GD_CHECK,native_token,SCRATCH,0x8c0bd374u,SCRATCH);++checks;
    if(bios==KUI_GD_COMPLETED || bios==KUI_GD_NOT_FOUND) {
        native_status=bios==KUI_GD_COMPLETED?1u:0u;native_error=0u;
        native_owner=native_token=0u;
    } else if(bios==KUI_GD_FAILED) {
        /* Errorclass0=>-27; illegal-request class5=>-32. A denied map does
         * not write scratch, so initialized class0 remains visible to SDK. */
        native_status=5u;native_error=(uint32_t)(get(SCRATCH)==5u?-32:-27);
        native_owner=native_token=0u;
    } else assert(bios==KUI_GD_PROCESSING);
    native_busy=0u;
}
static int32_t pause_attempt(void) {
    if(native_owner) return -13;
    native_token=0u;
    int32_t token=call(KUI_GD_REQUEST,KUI_RETAIL_GD_PAUSE,0u,0x8c0bd4f0u,PARAM);
    if(!token) { ++refusals;return -13; }
    assert(token>0);native_owner=1u;native_token=(uint32_t)token;
    assert(call(KUI_GD_EXEC,0,0,0,0)==0);return 0;
}
static void assert_only_scratch_changed(const uint8_t before[sizeof(lease)]) {
    for(uint32_t i=0;i<sizeof(lease);i++)
        if(i<SCRATCH-LEASE_BEGIN || i>=SCRATCH-LEASE_BEGIN+16u) assert(lease[i]==before[i]);
}
/* Baseline comparison implements the former blanket lease denial only.
 * The positive regression below always uses extracted production toy_map. */
static uint8_t *blanket_map(void *context,uint32_t address,uint32_t bytes,int writing) {
    if(bytes && address<LEASE_END && address+bytes>LEASE_BEGIN) return NULL;
    return map_guest(context,address,bytes,writing);
}
static void reproduce_reported_start_failure(void) {
    reset();play();service.ops.map=blanket_map;vblank();
    assert(native_status==5u && (int32_t)native_error==-27 && !native_owner && !native_token);
    assert(service.token==129u && service.command==KUI_RETAIL_GD_PLAY && !service.pending);
    for(unsigned i=0;i<8u;i++) { assert(pause_attempt()==-13);vblank(); }
    assert(refusals==8u && pumps==9u && checks==1u && !native_busy);
    assert(snapshot.command==KUI_RETAIL_GD_PLAY && service.command==KUI_RETAIL_GD_PLAY);
}
static void scoped_irq_check_retires_and_next_pause_succeeds(void) {
    reset();play();uint8_t before[sizeof(lease)];memcpy(before,lease,sizeof(before));
    vblank();
    assert(native_status==1u && !native_owner && !native_token && !native_busy && !service.command);
    assert(get(SCRATCH)==0u && get(SCRATCH+4u)==0u && get(SCRATCH+8u)==0u && get(SCRATCH+12u)==0u);
    assert_only_scratch_changed(before);
    assert(!toy_map(NULL,SCRATCH,16u,1)); /* Dispatch capability has expired. */
    assert(pause_attempt()==0 && snapshot.command==KUI_RETAIL_GD_PAUSE);vblank();
    assert(native_status==1u && !service.command && !native_owner);
    assert_only_scratch_changed(before);
}
static void wrong_token_still_gets_legal_scratch_error(void) {
    reset();play();uint8_t before[sizeof(lease)];memcpy(before,lease,sizeof(before));
    assert(call(KUI_GD_CHECK,130u,SCRATCH,0x8c0bd374u,SCRATCH)==KUI_GD_FAILED);
    assert(get(SCRATCH)==5u && get(SCRATCH+4u)==0u && get(SCRATCH+8u)==0u && get(SCRATCH+12u)==0u);
    assert(service.command==KUI_RETAIL_GD_PLAY && service.token==129u && !service.pending);
    assert_only_scratch_changed(before);assert(!toy_map(NULL,SCRATCH,16u,1));
    vblank();assert(!service.command && !native_owner && native_status==1u);
}
static void status_parameters_read_from_native_irq_stack(void) {
    reset();play();vblank();assert(!native_owner && !service.command);
    for(unsigned i=0;i<4u;i++) put(SCRATCH+i*4u,OUTPUT+i*4u);
    uint8_t before[sizeof(lease)];memcpy(before,lease,sizeof(before));
    int32_t token=call(KUI_GD_REQUEST,KUI_RETAIL_GD_REQ_STAT,SCRATCH,0x8c0bd57eu,SCRATCH);
    assert(token>0 && service.pending && !memcmp(before,lease,sizeof(before)));
    assert(!toy_map(NULL,SCRATCH,16u,0));
    assert(call(KUI_GD_EXEC,0,0,0,0)==0);
    assert(call(KUI_GD_CHECK,(uint32_t)token,OUTPUT+32u,0x8c0bd374u,OUTPUT+32u)==KUI_GD_COMPLETED);
    assert(get(OUTPUT)==3u && get(OUTPUT+4u)==2u);
    assert((get(OUTPUT+8u)&0x00ffffffu)==166u && get(OUTPUT+12u)==1u);
    assert(!memcmp(before,lease,sizeof(before)) && !service.command);
}
static void malformed_callers_keep_lease_protected(void) {
    const uint32_t addresses[]={LEASE_BEGIN,KUI_TOY_PILOT_WORKER_BEGIN,
        STACK_BOTTOM,STACK_BOTTOM+32u,STACK_BOTTOM+64u,STACK_BOTTOM+96u,
        STACK_TOP-16u,STACK_TOP-12u,STACK_TOP,LEASE_END-4u};
    for(unsigned i=0;i<sizeof(addresses)/sizeof(*addresses);i++) {
        reset();play();uint8_t before[sizeof(lease)];memcpy(before,lease,sizeof(before));
        assert(call(KUI_GD_CHECK,129u,addresses[i],0x8c0bd374u,addresses[i])==KUI_GD_FAILED);
        assert(!memcmp(before,lease,sizeof(before)) && service.command==KUI_RETAIL_GD_PLAY);
    }
    const uint32_t wrong_pr[]={0u,0x8c0bd372u,0x8c0bd376u,0x8c0bd57eu};
    for(unsigned i=0;i<sizeof(wrong_pr)/sizeof(*wrong_pr);i++) {
        reset();play();uint8_t before[sizeof(lease)];memcpy(before,lease,sizeof(before));
        assert(call(KUI_GD_CHECK,129u,SCRATCH,wrong_pr[i],SCRATCH)==KUI_GD_FAILED);
        assert(!memcmp(before,lease,sizeof(before)) && service.command==KUI_RETAIL_GD_PLAY);
    }
    reset();play();assert(call(KUI_GD_CHECK,129u,SCRATCH,0x8c0bd374u,SCRATCH+4u)==KUI_GD_FAILED);
    assert(call(KUI_GD_DRIVE,SCRATCH,0u,0x8c0bd374u,SCRATCH)==-1);
    assert(service.command==KUI_RETAIL_GD_PLAY);
    kui_toy_pilot_boot_control.status=KUI_TOY_BOOT_ARMED;
    toy_scratch=SCRATCH|1u;
    assert(call(KUI_GD_CHECK,129u,SCRATCH,0x8c0bd374u,SCRATCH)==KUI_GD_FAILED);
    assert(service.command==KUI_RETAIL_GD_PLAY && !toy_scratch);
}
static void capability_boundaries_and_directions(void) {
    uint32_t cap=kui_toy_pilot_scratch_capability(KUI_GD_CHECK,129u,SCRATCH,
        0x8c0bd374u,SCRATCH,STACK_TOP);
    assert(cap==(SCRATCH|1u) && kui_toy_pilot_scratch_maps(cap,SCRATCH,16u,1));
    const uint32_t bytes[]={0u,1u,4u,8u,12u,15u,17u,20u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(bytes)/sizeof(*bytes);i++)
        assert(!kui_toy_pilot_scratch_maps(cap,SCRATCH,bytes[i],1));
    assert(!kui_toy_pilot_scratch_maps(cap,SCRATCH,16u,0));
    assert(!kui_toy_pilot_scratch_maps(cap,SCRATCH+4u,16u,1));
    assert(!kui_toy_pilot_scratch_maps(cap,SCRATCH,16u,KUI_RETAIL_MAP_VALIDATE));
    assert(!kui_toy_pilot_scratch_maps(0u,SCRATCH,16u,1));
    uint32_t readcap=kui_toy_pilot_scratch_capability(KUI_GD_REQUEST,KUI_RETAIL_GD_REQ_STAT,
        SCRATCH,0x8c0bd57eu,SCRATCH,STACK_TOP);
    assert(readcap==SCRATCH && kui_toy_pilot_scratch_maps(readcap,SCRATCH,16u,0));
    assert(!kui_toy_pilot_scratch_maps(readcap,SCRATCH,16u,1));
    for(uint32_t function=0;function<=KUI_GD_DATATYPE+1u;function++) {
        if(function!=KUI_GD_CHECK)
            assert(!kui_toy_pilot_scratch_capability(function,129u,SCRATCH,0x8c0bd374u,SCRATCH,STACK_TOP));
        if(function!=KUI_GD_REQUEST)
            assert(!kui_toy_pilot_scratch_capability(function,KUI_RETAIL_GD_REQ_STAT,SCRATCH,
                0x8c0bd57eu,SCRATCH,STACK_TOP));
    }
    const uint32_t wrong_commands[]={KUI_RETAIL_GD_PLAY,KUI_RETAIL_GD_PAUSE,
        KUI_RETAIL_GD_GETSCD,KUI_GD_COMMAND_INIT,0u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(wrong_commands)/sizeof(*wrong_commands);i++)
        assert(!kui_toy_pilot_scratch_capability(KUI_GD_REQUEST,wrong_commands[i],SCRATCH,
            0x8c0bd57eu,SCRATCH,STACK_TOP));
    /* top is protected INSTALLED control already validated by the stage;
     * callers do not supply it. Exercise bounds against that admitted top. */
    const uint32_t valid_edges[]={STACK_BOTTOM+64u+36u,STACK_TOP-32u};
    for(unsigned i=0;i<sizeof(valid_edges)/sizeof(*valid_edges);i++)
        assert(kui_toy_pilot_scratch_capability(KUI_GD_CHECK,129u,valid_edges[i],
            0x8c0bd374u,valid_edges[i],STACK_TOP)==(valid_edges[i]|1u));
    assert(!kui_toy_pilot_scratch_capability(KUI_GD_CHECK,129u,SCRATCH+1u,
        0x8c0bd374u,SCRATCH+1u,STACK_TOP));
}
static void toy_failure(uint32_t error,uint32_t detail) __attribute__((noreturn));
static void toy_failure(uint32_t error,uint32_t detail) {
    (void)detail;stage_error=error;longjmp(stage_escape,1);
}
/* PRODUCTION_STAGE_ENTRY */
/* PRODUCTION_STAGE_EXPORTS */
static void stage_admission(const struct kui_toy_pilot_exports *input,bool rejected) {
    memcpy(toy_worker_blob,input,sizeof(*input));stage_error=0u;
    if(!setjmp(stage_escape)) {
        struct kui_toy_pilot_exports result;
        toy_exports_check(&result,sizeof(toy_worker_blob));
        assert(!rejected && !memcmp(&result,input,sizeof(result)));
    } else assert(rejected && stage_error==3u);
}
static void actual_stage_validates_the_trusted_stack_top(void) {
    uint32_t entry=KUI_TOY_PILOT_WORKER_BEGIN+sizeof(struct kui_toy_pilot_exports);
    struct kui_toy_pilot_exports e={
        .magic=KUI_TOY_PILOT_MAGIC,.version=KUI_TOY_PILOT_API,.bytes=sizeof(e),
        .initialize=entry,.request=entry,.service_hook=entry,.am_init_hook=entry,
        .shutdown_hook=entry,.snapshot=entry,.allstop_hook=entry,.driver_load_hook=entry,
        .gd_dispatch=entry,.pause_hook=entry,.main_lease_bytes=KUI_TOY_PILOT_MAIN_LEASE_BYTES,
        .bss_begin=KUI_TOY_PILOT_WORKER_BEGIN+sizeof(toy_worker_blob),
        .bss_end=STACK_BOTTOM,.stack_bottom=STACK_BOTTOM,
        .stack_top=STACK_TOP,.worker_end=STACK_TOP};
    stage_admission(&e,false);
    e.worker_end=STACK_TOP+32u;stage_admission(&e,true);
    e.worker_end=STACK_TOP;e.stack_bottom=STACK_BOTTOM-32u;stage_admission(&e,true);
    e.stack_bottom=STACK_BOTTOM;e.stack_top=STACK_TOP+4u;e.worker_end=e.stack_top;
    stage_admission(&e,true);
}
int main(void) {
    reproduce_reported_start_failure();scoped_irq_check_retires_and_next_pause_succeeds();
    wrong_token_still_gets_legal_scratch_error();status_parameters_read_from_native_irq_stack();
    malformed_callers_keep_lease_protected();capability_boundaries_and_directions();
    actual_stage_validates_the_trusted_stack_top();
    puts("Toy IRQ scratch: production map/dispatch, token129 Start regression, native retirement, next PAUSE, REQ_STAT, canaries and expired/invalid capability pass");
    return 0;
}
'''.replace("/* PRODUCTION_MAP */", mapping).replace("/* PRODUCTION_DISPATCH */", dispatch).replace(
    "/* PRODUCTION_STAGE_ENTRY */", stage_entry).replace("/* PRODUCTION_STAGE_EXPORTS */", stage_exports)

with tempfile.TemporaryDirectory(prefix="kui-toy-scratch-") as temporary:
    work = Path(temporary)
    test, binary = work / "scratch.c", work / "scratch"
    test.write_text(harness)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--no-sanitizers", action="store_true")
    args = parser.parse_args()
    compiler = shlex.split(args.cc)
    flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
             "-Wpedantic", "-fno-pie", "-no-pie"]
    if not args.no_sanitizers:
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(compiler + flags + ["-I" + str(ROOT / "include"), str(test),
        str(ROOT / "src/loader/toy_pilot_gd.c"), str(ROOT / "src/core/retail_gd.c"),
        "-o", str(binary)], check=True)
    subprocess.run([str(binary)], env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"), check=True)
