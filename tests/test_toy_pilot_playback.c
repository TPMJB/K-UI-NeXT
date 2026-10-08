/* SPDX-License-Identifier: GPL-3.0-only */
/* Host simulation of the actual adapter, not a second playback algorithm.
 * Build with -fno-pie -no-pie: the console config stores callback addresses in
 * uint32_t. ARM command effects here follow the admitted finite port protocol.
 */
#define KUI_TOY_PILOT_WORKER_TEST 1
#include "../src/loader/toy_pilot_worker.c"
#include <assert.h>
#include <stdio.h>

uint8_t __toy_pilot_stack_bottom[64],__toy_pilot_stack_top[64],__toy_pilot_worker_end[32];
static uint8_t sound_memory[SOUND_END-SOUND_BASE];
static uint32_t sr,entry_sr,now,producer,bus_calls,packets,copies,raw_calls,reports;
static uint32_t voice_started,voice_frames;
static bool voice_running,hold_voice,hold_queue;
static enum kui_toy_pilot_bus_result read_result;

static uint32_t sound_get(uint32_t address) {
    uint32_t value;assert(address>=SOUND_BASE && address<=SOUND_END-4u);
    memcpy(&value,sound_memory+address-SOUND_BASE,4u);return value;
}
static void sound_set(uint32_t address,uint32_t value) {
    assert(address>=SOUND_BASE && address<=SOUND_END-4u);
    memcpy(sound_memory+address-SOUND_BASE,&value,4u);
}
static void voice_observe(void) {
    if(!voice_running) return;
    uint32_t elapsed=now-voice_started;
    uint32_t frames=(uint32_t)((uint64_t)elapsed*44100u/781250u);
    if(frames>=voice_frames && !hold_voice) {
        frames=voice_frames-1u;voice_running=false;
        sound_set(SOUND_BASE+0x14a4u,0u);
    } else if(frames>=voice_frames) frames=voice_frames-1u;
    sound_set(SOUND_BASE+0x15e8u,frames);sound_set(SOUND_BASE+0x15ecu,frames);
}
uint32_t kui_toy_pilot_worker_test_sr(void) { return sr; }
void kui_toy_pilot_worker_test_set_sr(uint32_t value) { sr=value; }
void kui_toy_pilot_worker_test_terminal(void) { assert(sr==entry_sr);++reports; }
uint32_t kui_toy_pilot_worker_test_read(uint32_t address,unsigned width) {
    (void)width;
    switch(address) {
    case 0x8c0a7318u:case 0x8c0a8940u:case 0x8c0af74cu:return 1u;
    case 0x8c112b08u:return QUEUE_BASE;
    case 0x8c112b0cu:return producer;
    case TIMER_TSTR:return 1u;
    case TIMER_TCOR:return UINT32_MAX;
    case TIMER_TCNT:return ~(now++);
    case TIMER_TCR:return 2u;
    case CLOCK_FRQCR:return 0xe0au;
    default:return 0u;
    }
}
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_read(uint32_t address,uint32_t *value) {
    assert((sr&0xf0u)==0xf0u);++bus_calls;
    if(read_result!=KUI_TOY_PILOT_BUS_OK) return read_result;
    voice_observe();
    if(address>=QUEUE_BASE && address<QUEUE_BASE+512u)
        *value=hold_queue?0xff9cu:0u;
    else *value=sound_get(address);
    return KUI_TOY_PILOT_BUS_OK;
}
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_publish(const uint32_t packet[4],uint32_t *slot) {
    assert((sr&0xf0u)==0xf0u);++packets;
    *slot=QUEUE_BASE+producer;producer=(producer+16u)&511u;
    uint32_t command=packet[0]&0xffffu,port=(packet[0]>>16)&255u;
    if(command==0xff9du) {
        assert(packet[1]==PORT_MASK);voice_running=false;sound_set(SOUND_BASE+0x14a4u,0u);
    } else if(command==0xff91u) {
        assert(port==62u || port==63u);
        uint32_t t=SOUND_BASE+0x2ca8u+(port-62u)*0x48u;
        for(unsigned i=0;i<0x48u;i+=4u) sound_set(t+i,0u);
    } else if(command==0xff90u) {
        assert(port==62u || port==63u);assert(packet[2] && !packet[3]);
        uint32_t t=SOUND_BASE+0x2ca8u+(port-62u)*0x48u;
        sound_set(t,(packet[1]>>16)&0x7fu);sound_set(t+4u,packet[1]&0xffffu);
        sound_set(t+8u,0u);sound_set(t+12u,packet[2]/2u-1u);sound_set(t+0x18u,0u);
    } else if(command==0xff9cu) {
        assert(packet[1]==PORT_MASK);
        voice_frames=sound_get(SOUND_BASE+0x2ca8u+12u)+1u;
        assert(voice_frames==sound_get(SOUND_BASE+0x2cf0u+12u)+1u);
        voice_started=now;voice_running=true;sound_set(SOUND_BASE+0x14a4u,0xffff0000u);
        sound_set(SOUND_BASE+0x15e8u,0u);sound_set(SOUND_BASE+0x15ecu,0u);
    } else assert(command==0xff96u || command==0xff97u);
    return KUI_TOY_PILOT_BUS_OK;
}
enum kui_toy_pilot_bus_result kui_toy_pilot_lease_allocate(uint32_t bytes,uint32_t alignment,uint32_t *address) {
    assert((sr&0xf0u)==0xf0u);assert(bytes==KUI_TOY_PILOT_SOUND_BYTES && alignment==32u);
    *address=SOUND_END-KUI_TOY_PILOT_SOUND_BYTES;return KUI_TOY_PILOT_BUS_OK;
}
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_copy(uint32_t address,const void *source,uint32_t bytes) {
    assert((sr&0xf0u)==0xf0u);assert(bytes && bytes<=256u && !(bytes&3u));
    assert(address>=owner.stats.sound_address && address<=SOUND_END-bytes);
    uint32_t bank=(address-owner.stats.sound_address)/KUI_TOY_PILOT_BANK_BYTES;
    assert(bank<2u && bank!=owner.model.active_bank && bank!=owner.model.pending_bank);
    memcpy(sound_memory+address-SOUND_BASE,source,bytes);++copies;return KUI_TOY_PILOT_BUS_OK;
}
static int read_raw(uint32_t lba,uint32_t sectors,void *destination) {
    assert((sr&0xf0u)==0xf0u);assert(sectors && sectors<=2u);assert(!((uintptr_t)destination&31u));
    assert(lba>=50u && lba+sectors<=150u);++raw_calls;
    uint8_t *p=destination;
    for(uint32_t i=0;i<sectors*588u;i++) {
        uint16_t frame=(uint16_t)((lba-50u)*588u+i);
        p[i*4u]=(uint8_t)frame;p[i*4u+1u]=(uint8_t)(frame>>8);
        p[i*4u+2u]=(uint8_t)~frame;p[i*4u+3u]=(uint8_t)(~frame>>8);
    }
    return 0;
}
static void prepare(void) {
    kui_toy_pilot_worker_test_prepare(KUI_TOY_PILOT_STOPPED,CMD_PLAY,true);
    owner.mailbox.end_fad=300u;owner.config.read_raw=(uint32_t)(uintptr_t)read_raw;
    owner.config.resident_active=0x8c004010u;owner.config.data_pending=0x8c004014u;
    memset(sound_memory,0,sizeof(sound_memory));
    sound_set(SOUND_BASE+0x1464u,0x800000u);sound_set(SOUND_BASE+0xe0u,0x1468u);
    sound_set(SOUND_BASE+0xe8u,0x14f0u);sound_set(SOUND_BASE+0xecu,0x30040u);
    sr=entry_sr=0x40000001u;now=producer=bus_calls=packets=copies=raw_calls=reports=0u;
    voice_started=voice_frames=0u;voice_running=hold_voice=hold_queue=false;
    read_result=KUI_TOY_PILOT_BUS_OK;
}
static void step(uint32_t elapsed) {
    now+=elapsed;entry_sr=sr;kui_toy_pilot_worker_step();assert(sr==entry_sr);
}
static void reach_start(void) {
    for(unsigned i=0;i<1000u && owner.stats.state!=KUI_TOY_PILOT_START_WAIT;i++) {
        step(13021u);assert(!owner.stats.fault && !reports);
    }
    assert(owner.stats.state==KUI_TOY_PILOT_START_WAIT && owner.model.pending_bank!=NONE);
}
static void reach_eof(void) {
    for(unsigned i=0;i<1000u && owner.stats.state!=KUI_TOY_PILOT_EOF;i++) {
        step(13021u);assert(!owner.stats.fault && !reports);
    }
    assert(owner.stats.state==KUI_TOY_PILOT_EOF);
    assert(owner.stats.filled_frames==58800u && owner.stats.retired_frames==58800u);
    assert(owner.stats.bank_starts==4u && owner.stats.bank_ends==4u && owner.stats.finite_ends==4u);
    assert(owner.model.active_bank==NONE && owner.model.pending_bank==NONE);
    assert(owner.model.banks[0].state==KUI_TOY_PILOT_BANK_EMPTY && owner.model.banks[1].state==KUI_TOY_PILOT_BANK_EMPTY);
    assert(!owner.stats.active_bank_writes && owner.stats.applied_generation==2u && copies && raw_calls);
}
static void fault_is_terminal(uint32_t reason) {
    assert(owner.stats.fault==reason && reports==1u && owner.disabled);
    uint32_t previous=bus_calls,previous_packets=packets;step(13021u);
    assert(bus_calls==previous && packets==previous_packets && reports==1u);
}
int main(void) {
    assert((uintptr_t)read_raw<=UINT32_MAX);
    prepare();reach_eof();assert(owner.stats.started_observed==4u);
    assert(owner.stats.handoff_gaps==3u);

    /* No callback observed the active interval. Queue consumption is sampled
     * after it ended, then a whole additional conservative interval elapses. */
    prepare();reach_start();step(finite_ticks(voice_frames)+1u);
    assert(!owner.play_seen && owner.play_ack_seen && !voice_running);
    assert(owner.stats.applied_generation!=2u && !owner.stats.started_observed);
    step(finite_ticks(voice_frames)+1u);
    assert(!owner.stats.fault && owner.stats.finite_ends==1u);
    assert(owner.stats.applied_generation==2u && !owner.stats.started_observed);
    reach_eof();assert(owner.stats.started_observed==3u);

    /* Inactive flags and a consumed START do not replace elapsed finite time. */
    prepare();reach_start();voice_running=false;sound_set(SOUND_BASE+0x14a4u,0u);step(1u);
    assert(owner.play_ack_seen && !owner.play_seen && !owner.stats.finite_ends);
    step(finite_ticks(voice_frames)/2u);
    assert(!owner.stats.fault && !owner.stats.finite_ends && owner.model.pending_bank!=NONE);
    step(finite_ticks(voice_frames)/2u+2u);
    assert(!owner.stats.fault && owner.stats.finite_ends==1u && !owner.stats.started_observed);

    /* Finite timing alone does not authorize a write/reuse while flags remain
     * active. The existing extra one-second phase limit is retained. */
    prepare();reach_start();hold_voice=true;step(1u);assert(owner.play_seen);
    step(finite_ticks(voice_frames)+1u);assert(!owner.stats.finite_ends && !owner.stats.fault);
    step(APPLY_LIMIT_TICKS+1u);fault_is_terminal(KUI_TOY_PILOT_FAULT_PHASE);
    assert(!owner.stats.finite_ends);

    prepare();reach_start();hold_queue=true;step(APPLY_LIMIT_TICKS+1u);
    fault_is_terminal(KUI_TOY_PILOT_FAULT_QUEUE);assert(!owner.stats.finite_ends);

    prepare();reach_start();step(finite_ticks(voice_frames)+1u);
    sound_set(SOUND_BASE+0x2ca8u+12u,1u);step(finite_ticks(voice_frames)+1u);
    fault_is_terminal(KUI_TOY_PILOT_FAULT_PORT);assert(!owner.stats.finite_ends);

    prepare();reach_start();step(CLOCK_GAP_LIMIT+1u);
    fault_is_terminal(KUI_TOY_PILOT_FAULT_CLOCK);assert(!owner.stats.finite_ends);

    prepare();reach_start();read_result=KUI_TOY_PILOT_BUS_BUSY;step(1u);step(APPLY_LIMIT_TICKS+1u);
    fault_is_terminal(KUI_TOY_PILOT_FAULT_BUS);assert(!owner.stats.finite_ends);

    /* A pending newer request cannot acquire an older finite-end handshake. */
    prepare();reach_start();step(finite_ticks(voice_frames)+1u);
    uint32_t prior_applied=owner.stats.applied_generation;
    ++owner.mailbox.generation;owner.stats.generation=owner.mailbox.generation;
    now+=finite_ticks(voice_frames)+1u;playback_step(now);
    assert(!owner.stats.finite_ends && owner.stats.applied_generation==prior_applied && sr==entry_sr);

    prepare();kui_toy_pilot_worker_allstop(UINT32_MAX,UINT32_MAX);
    assert(owner.sdk_ready && !owner.disabled); /* preallocation initialization */
    reach_start();kui_toy_pilot_worker_allstop(0x40000000u,0u);
    assert(owner.sdk_ready && !owner.disabled);
    kui_toy_pilot_worker_allstop(UINT32_MAX,UINT32_MAX);
    assert(!owner.sdk_ready && owner.disabled && owner.stats.state==KUI_TOY_PILOT_OFF);
    step(1u);assert(!reports); /* normal teardown is not a worker fault */

    prepare();owner.disabled=1;owner.stats.fault=KUI_TOY_PILOT_FAULT_DRIVER;
    step(1u);fault_is_terminal(KUI_TOY_PILOT_FAULT_DRIVER);
    assert(!bus_calls && !packets); /* a fault latched before service is reported */

    puts("Toy actual playback: complete bank/EOF cycle, missed-active retirement, generation handshake, finite safety proofs, terminal SR, and lifecycle boundaries");
    return 0;
}
