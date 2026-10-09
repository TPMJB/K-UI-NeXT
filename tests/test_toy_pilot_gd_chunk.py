#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare the production GD step policies using real GD and audio priority.

The physical-block clock is independently scheduled. Results describe this
fixture's latency/throughput tradeoff, not console FMV quality or IRQ timing.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/loader/retail_resident.c").read_text()
start = source.rindex("static int32_t step(")
brace = source.index("{", start)
depth, end = 1, brace + 1
while depth:
    depth += (source[end] == "{") - (source[end] == "}")
    end += 1
production_step = source[start:end]
# The production TCNT0 loads use a hosted volatile down-counter. All policy,
# GD dispatch and timing-accounting statements remain the extracted code.
assert production_step.count("(uintptr_t)0xffd8000cu") == 2
production_step = production_step.replace("(uintptr_t)0xffd8000cu", "(uintptr_t)&counter")

harness = r'''
#define main ring_fixture_unused_main
#include "RING_FIXTURE"
#undef main
#include "kui/retail_gd.h"
#include "kui/retail_pace.h"

#define GUEST UINT32_C(0x8c100000)
#define PARAM (GUEST+0x100u)
#define STATUS (GUEST+0x200u)
#define OUTPUT (GUEST+0x1000u)
#define SECTORS 129u
static uint8_t ram[512u*1024u];
static struct kui_retail_gd service;
static struct kui_retail_pace pace;
#if KUI_TOY_PILOT_GD_FIXED_STEP
static struct { uint32_t steps,sectors,ticks,max_ticks,max_sectors,invalid; } toy_gd_timing;
static int toy_gd_clock_valid(void) { return 1; }
#else
static struct { uint32_t paced,spun; } pacing;
#endif
static uint64_t timeline;
static volatile uint32_t counter;
static uint32_t io_calls,max_sectors,max_masked,masked_start,io_faults;
static bool inside_entry;
static void tick(uint32_t duration) {
    timeline+=duration;advance(duration);
    counter=~now;
}
static void video_sample(void) {
    uint32_t line=(uint32_t)(((uint64_t)timeline*60u*262u/781250u)%262u);
    kui_retail_pace_sample(&pace,line,260u,261u<<16,0x200000u);
}
static void report_fault(const char *label,uint32_t function,uint32_t param) {
    (void)label;(void)function;(void)param;++io_faults;
}
static uint8_t *guest_map(void *context,uint32_t address,uint32_t bytes,int access) {
    (void)context;(void)access;
    if(address<GUEST || address-GUEST>sizeof(ram) || bytes>sizeof(ram)-(address-GUEST)) return NULL;
    return ram+address-GUEST;
}
static void put(uint32_t address,uint32_t value) { memcpy(guest_map(NULL,address,4u,1),&value,4u); }
static uint32_t get(uint32_t address) { uint32_t value;memcpy(&value,guest_map(NULL,address,4u,0),4u);return value; }
static int check_data(void *context,uint32_t lba,uint32_t count,uint32_t bytes) {
    (void)context;return bytes==2048u && lba>=45000u && lba<=45129u && count<=45129u-lba?0:-1;
}
static int read_data(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *out) {
    (void)context;assert(inside_entry && (sr&0xf0u)==0xf0u);
    assert(!check_data(NULL,lba,count,bytes));++io_calls;
    if(count>max_sectors) max_sectors=count;
    /* Cooked2048-byte logical sectors occupy four512-byte blocks. Uniform
     *100-tick block costs are fixed across policies, including the tail. */
    for(uint32_t sector=0;sector<count;sector++) {
        for(unsigned block=0;block<4u;block++) { video_sample();tick(100u); }
        for(uint32_t byte=0;byte<bytes;byte++)
            ((uint8_t *)out)[sector*bytes+byte]=(uint8_t)((lba+sector)*13u+byte*3u);
    }
    return 0;
}
/* PRODUCTION_STEP */
static int32_t call(uint32_t function,uint32_t a,uint32_t b) {
    return kui_retail_gd_dispatch(&service,a,b,0u,function);
}
int main(void) {
    prepare(200u);ready();check_pcm=false;timeline=now;tick(0u);
    static const union kui_retail_slot track[]={
        {.track={.start_lba=45000u,.end_lba=45129u,.control=4u|KUI_RETAIL_TRACK_COOKED}}
    };
    const struct kui_gd_ops ops={NULL,guest_map,check_data,read_data};
    assert(!kui_retail_gd_init(&service,track,1u,&ops,GUEST,GUEST+sizeof(ram)));
    memset(ram,0xa5,sizeof(ram));
    put(PARAM,45150u);put(PARAM+4u,SECTORS);put(PARAM+8u,OUTPUT);put(PARAM+12u,0u);
    int32_t token=call(KUI_GD_REQUEST,KUI_GD_PIOREAD,PARAM);assert(token>0);
    uint32_t request_at=now,music_reads=reads,music_copies=copies;
    /* A stable framebuffer address supplies the admitted still-screen state;
     * no assumption about the displayed pixels is made here. */
    video_sample();pace.still=12u;pace.per=128u;
    uint32_t calls=0u,done=0u;
    while(service.pending) {
        if(calls && now-masked_start<13021u)
            tick(13021u-(now-masked_start)); /* One title EXEC per60Hz frame. */
        data_command=service.command;fill_step(); /* Actual authored data-priority refusal. */
        assert(reads==music_reads && copies==music_copies);
        video_sample();masked_start=now;uint32_t inherited=sr;sr|=0xf0u;inside_entry=true;
        assert(step(0u,0u)==0);inside_entry=false;sr=inherited;assert(sr==0x40000001u);
        uint32_t duration=now-masked_start;if(duration>max_masked) max_masked=duration;
        ++calls;assert(!io_faults && service.completed_bytes>done);
        done=service.completed_bytes;assert(done<=SECTORS*2048u && service.token==(uint32_t)token);
        if(service.pending) {
            assert(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_PROCESSING);
            assert(get(STATUS+8u)==done && get(STATUS+12u)==4u);
        }
    }
    uint32_t command_ticks=now-request_at;
    /* Even a terminal read retains data ownership until the native CHECK. */
    assert(service.command==KUI_GD_PIOREAD && call(KUI_GD_REQUEST,KUI_GD_NOP,0u)==0);
    data_command=service.command;fill_step();assert(reads==music_reads && copies==music_copies);
    assert(call(KUI_GD_CHECK,(uint32_t)token+1u,STATUS)==KUI_GD_FAILED);
    assert(get(STATUS)==5u && service.command==KUI_GD_PIOREAD);
    assert(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
    assert(get(STATUS+8u)==SECTORS*2048u && !service.command);
    assert(ram[OUTPUT-GUEST+SECTORS*2048u]==0xa5u);
    for(uint32_t sector=0;sector<SECTORS;sector++) for(uint32_t byte=0;byte<2048u;byte++)
        assert(ram[OUTPUT-GUEST+sector*2048u+byte]==(uint8_t)((45000u+sector)*13u+byte*3u));
    data_command=0u;visit(0u);assert(!reports && !owner.stats.fault);
#if KUI_TOY_PILOT_GD_FIXED_STEP
    uint32_t expected=(SECTORS+KUI_TOY_PILOT_GD_FIXED_STEP-1u)/KUI_TOY_PILOT_GD_FIXED_STEP;
    assert(calls==expected && io_calls==expected && max_sectors==KUI_TOY_PILOT_GD_FIXED_STEP);
    assert(max_masked==KUI_TOY_PILOT_GD_FIXED_STEP*400u && stop_queued);
    if(KUI_TOY_PILOT_GD_FIXED_STEP==2u) assert(command_ticks>781250u);
    else assert(command_ticks<781250u && command_ticks>290249u);
    /* This fixture deliberately provides no full worker observation while
     * the command owns data. Shorter command duration cannot authorize an
     * unobserved hardware revolution; the existing recovery must remain. */
    assert(toy_gd_timing.steps==expected && toy_gd_timing.sectors==129u &&
           toy_gd_timing.max_sectors==KUI_TOY_PILOT_GD_FIXED_STEP);
    assert(toy_gd_timing.ticks==129u*400u &&
           toy_gd_timing.max_ticks==KUI_TOY_PILOT_GD_FIXED_STEP*400u && !toy_gd_timing.invalid);
    recover_and_restart(1u); /* Real STOP ACK, full ownership wait and re-prime. */
#else
    assert(calls==17u && io_calls==17u && max_sectors==8u && max_masked==3200u);
    assert(command_ticks<290249u && !stop_queued); /* Original half remains a valid timeline. */
#endif
    printf("Toy GD chunk%u: exact129-sector payload/progress/handle; maxmask=%u modeledticks, command=%u modeledticks, audio-priority/refill recovery checked\n",
        KUI_TOY_PILOT_GD_FIXED_STEP,max_masked,command_ticks);
    return 0;
}
'''.replace("RING_FIXTURE", str(ROOT / "tests/test_toy_pilot_ring_worker.c")).replace(
    "/* PRODUCTION_STEP */", production_step)

# A second fixture supplies full worker visits while the real GD core owns
# the data handle. It verifies physical PCM and every GD output byte. Callback
# costs are explicit assumptions anchored to the completed build7b55156 run:
# GD two-sector mean8.250204ms/max10.44736ms; raw mean4.442ms/max9.8752ms.
# Scaling GD cost linearly with count and a tail every32callbacks are modeling
# choices, not observed three-sector latency or a reconstruction of its IRQs.
mixed_harness = r'''
#include "ASYNC_FIXTURE"
#include "kui/retail_gd.h"
#include "kui/retail_pace.h"
#define GUEST UINT32_C(0x8c100000)
#define PARAM (GUEST+0x100u)
#define STATUS (GUEST+0x200u)
#define OUTPUT (GUEST+0x1000u)
#define GD_MEAN_TICKS 6445u
#define GD_TAIL_TICKS 8162u
static uint8_t ram[512u*1024u];
static struct kui_retail_gd service;
static struct kui_retail_pace pace;
static struct { uint32_t steps,sectors,ticks,max_ticks,max_sectors,invalid; } toy_gd_timing;
static volatile uint32_t counter;
static uint32_t io_calls,io_sequence,io_faults,max_masked,requested_sectors;
static bool inside_entry;
static int toy_gd_clock_valid(void) { return 1; }
static void video_sample(void) {
    uint32_t line=(uint32_t)(((uint64_t)now*60u*262u/781250u)%262u);
    kui_retail_pace_sample(&pace,line,260u,261u<<16,0x200000u);
}
static void report_fault(const char *label,uint32_t function,uint32_t param) {
    (void)label;(void)function;(void)param;++io_faults;
}
static uint8_t *guest_map(void *context,uint32_t address,uint32_t bytes,int access) {
    (void)context;(void)access;
    if(address<GUEST || address-GUEST>sizeof(ram) || bytes>sizeof(ram)-(address-GUEST)) return NULL;
    return ram+address-GUEST;
}
static void put(uint32_t address,uint32_t value) { memcpy(guest_map(NULL,address,4u,1),&value,4u); }
static uint32_t get(uint32_t address) { uint32_t value;memcpy(&value,guest_map(NULL,address,4u,0),4u);return value; }
static int check_data(void *context,uint32_t lba,uint32_t count,uint32_t bytes) {
    (void)context;return bytes==2048u && lba>=45000u && lba<=45129u && count<=45129u-lba?0:-1;
}
static int read_data(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *out) {
    (void)context;assert(inside_entry && (sr&0xf0u)==0xf0u);
    assert(!check_data(NULL,lba,count,bytes));++io_calls;
    uint32_t cost=(++io_sequence%32u?GD_MEAN_TICKS:GD_TAIL_TICKS)*count/2u;
    /* Independently advance ARM publications and physical PCM throughout
     * each modeled cooked-data callback, while the SH entry is masked. */
    for(uint32_t block=0u;block<count*4u;block++) {
        video_sample();advance(cost/(count*4u)+(block<cost%(count*4u)));
    }
    counter=~now;
    for(uint32_t sector=0u;sector<count;sector++) for(uint32_t byte=0u;byte<bytes;byte++)
        ((uint8_t *)out)[sector*bytes+byte]=(uint8_t)((lba+sector)*13u+byte*3u);
    return 0;
}
/* PRODUCTION_STEP */
static int32_t call(uint32_t function,uint32_t a,uint32_t b) {
    return kui_retail_gd_dispatch(&service,a,b,0u,function);
}
static void worker_visit(void) {
    data_command=service.command;visit(0u);counter=~now;
    assert(!owner.stats.fault && !reports && !owner.stats.raw_errors &&
           !owner.stats.queue_errors && !owner.stats.stale_actions && !owner.stats.active_bank_writes);
}
static void run(uint32_t count,uint32_t cadence,bool expect_recovery,bool first_tail) {
    prepare(200u);repeat_pcm=true;strict_source_reads=true;
    raw_ticks=3470u;raw_tail_period=32u;raw_tail_ticks=7715u;
    serial_cursor=true;serial_period=7813u;serial_gap=8u;serial_right_delay=1000u;
    assert(kui_toy_pilot_request(CMD_PLAY,2u,2u,15u)==3u);ready();
    memset(&service,0,sizeof(service));memset(&pace,0,sizeof(pace));
    memset(&toy_gd_timing,0,sizeof(toy_gd_timing));memset(ram,0xa5,sizeof(ram));
    io_calls=io_faults=max_masked=0u;inside_entry=false;requested_sectors=count;
    io_sequence=first_tail?31u:0u;
    static const union kui_retail_slot track[]={
        {.track={.start_lba=45000u,.end_lba=45129u,.control=4u|KUI_RETAIL_TRACK_COOKED}}
    };
    const struct kui_gd_ops ops={NULL,guest_map,check_data,read_data};
    assert(!kui_retail_gd_init(&service,track,1u,&ops,GUEST,GUEST+sizeof(ram)));
    put(PARAM,45150u);put(PARAM+4u,count);put(PARAM+8u,OUTPUT);put(PARAM+12u,0u);
    int32_t token=call(KUI_GD_REQUEST,KUI_GD_PIOREAD,PARAM);assert(token>0);
    uint32_t request_at=now,next=now,completed=0u,calls=0u;
    while(service.pending) {
        if(now<next) advance(next-now);
        uint32_t entered=now,before_reads=reads,before_copies=copies;
        worker_visit();assert(reads==before_reads && copies==before_copies);
        uint32_t started=now,inherited=sr;counter=~now;sr|=0xf0u;inside_entry=true;
        assert(!step(0u,0u));inside_entry=false;sr=inherited;assert(sr==0x40000001u);
        uint32_t elapsed=now-started;if(elapsed>max_masked) max_masked=elapsed;
        ++calls;assert(!io_faults && service.completed_bytes>completed && service.token==(uint32_t)token);
        completed=service.completed_bytes;
        if(service.pending) {
            assert(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_PROCESSING);
            assert(get(STATUS+8u)==completed && get(STATUS+12u)==4u);
        }
        worker_visit();assert(reads==before_reads && copies==before_copies);
        next=entered+cadence; /* Never subtract past time from unsigned ticks. */
    }
    uint32_t command_ticks=now-request_at;
    assert(service.command==KUI_GD_PIOREAD && call(KUI_GD_REQUEST,KUI_GD_NOP,0u)==0);
    uint32_t old_reads=reads,old_copies=copies;
    worker_visit();assert(reads==old_reads && copies==old_copies);
    assert(call(KUI_GD_CHECK,(uint32_t)token+1u,STATUS)==KUI_GD_FAILED && get(STATUS)==5u);
    assert(service.command==KUI_GD_PIOREAD && call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
    assert(get(STATUS+8u)==count*2048u && !service.command && ram[OUTPUT-GUEST+count*2048u]==0xa5u);
    for(uint32_t sector=0u;sector<count;sector++) for(uint32_t byte=0u;byte<2048u;byte++)
        assert(ram[OUTPUT-GUEST+sector*2048u+byte]==(uint8_t)((45000u+sector)*13u+byte*3u));
    assert(calls==(count+KUI_TOY_PILOT_GD_FIXED_STEP-1u)/KUI_TOY_PILOT_GD_FIXED_STEP && io_calls==calls);
    assert(toy_gd_timing.steps==calls && toy_gd_timing.sectors==count &&
           toy_gd_timing.max_sectors==(count<KUI_TOY_PILOT_GD_FIXED_STEP?count:KUI_TOY_PILOT_GD_FIXED_STEP) &&
           toy_gd_timing.max_ticks==max_masked && !toy_gd_timing.invalid);
    worker_visit();
    if(expect_recovery) {
        assert(owner.stats.recovery_last_reason==KUI_TOY_PILOT_RECOVERY_COPY_RESERVE ||
               owner.stats.recovery_last_reason==KUI_TOY_PILOT_RECOVERY_MISSING_HALF);
        assert(owner.stop_wait && !running && checked_frames && !silent_frames);
        /* The data token has retired, but STOP still owns the sound horizon. */
        old_reads=reads;old_copies=copies;
        for(unsigned i=0u;i<8u;i++) { advance(13021u);worker_visit();assert(reads==old_reads && copies==old_copies); }
    } else {
        assert(!owner.stats.recovery_last_reason && starts==1u && running && checked_frames && !silent_frames);
        for(unsigned i=0u;i<90u;i++) {
            uint32_t entered=now;worker_visit();advance(7813u);worker_visit();
            if(now-entered<26042u) advance(26042u-(now-entered));
            assert(!owner.stats.recovery_last_reason && starts==1u && running && !silent_frames);
        }
        assert(reads>old_reads && owner.stats.bank_ends>8u && checked_frames>32768u);
    }
    printf("Toy GD mixed chunk%u: sectors=%u cadence=%u first_tail=%u command=%u maxmask=%u physical_PCM=%u recovery=%u; calibrated assumptions, not console prediction\n",
           KUI_TOY_PILOT_GD_FIXED_STEP,requested_sectors,cadence,first_tail,command_ticks,max_masked,checked_frames,owner.stats.recovery_last_reason);
}
int main(void) {
    for(unsigned i=0u;i<2u;i++) {
        uint32_t cadence=i?26042u:13021u;
        run(1u,cadence,false,false);run(5u,cadence,false,false);
        run(17u,cadence,false,false);run(17u,cadence,false,true);
        if(!i) run(129u,cadence,true,false);
    }
    return 0;
}
'''.replace("ASYNC_FIXTURE", str(ROOT / "tests/toy_pilot_async_fixture.h")).replace(
    "/* PRODUCTION_STEP */", production_step)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
parser.add_argument("--no-sanitizers", action="store_true")
args = parser.parse_args()
flags = ["-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wpedantic",
         "-fno-pie", "-no-pie", "-ffunction-sections", "-fdata-sections"]
if not args.no_sanitizers:
    flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
with tempfile.TemporaryDirectory(prefix="kui-toy-gd-chunk-") as temporary:
    work = Path(temporary)
    test = work / "gd-chunk.c"
    test.write_text(harness)
    for chunk in (0, 2, 3):
        binary = work / ("gd-chunk-" + str(chunk))
        subprocess.run(shlex.split(args.cc) + flags + ["-DKUI_TOY_PILOT_GD_FIXED_STEP=" + str(chunk),
            "-I" + str(ROOT / "include"), str(test), str(ROOT / "src/core/retail_gd.c"),
            str(ROOT / "src/core/retail_pace.c"), str(ROOT / "src/core/toy_pilot.c"),
            str(ROOT / "src/core/hash.c"), "-Wl,--gc-sections", "-o", str(binary)], check=True)
        subprocess.run([str(binary)], env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"), check=True)
    mixed_test = work / "gd-mixed.c"
    mixed_test.write_text(mixed_harness)
    for chunk in (2, 3):
        binary = work / ("gd-mixed-" + str(chunk))
        subprocess.run(shlex.split(args.cc) + flags + ["-Wno-unused-function",
            "-DKUI_TOY_PILOT_GD_FIXED_STEP=" + str(chunk), "-I" + str(ROOT / "include"),
            str(mixed_test), str(ROOT / "src/core/retail_gd.c"),
            str(ROOT / "src/core/retail_pace.c"), str(ROOT / "src/core/toy_pilot.c"),
            str(ROOT / "src/core/hash.c"), "-Wl,--gc-sections", "-o", str(binary)], check=True)
        subprocess.run([str(binary)], env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"), check=True)
