/* SPDX-License-Identifier: GPL-3.0-only */
/* Scalar native control lifecycle, authored from the admitted interface's
 * record fields and numerical protocol values. It executes the real GD
 * adapter/core; no game instructions or sound-driver implementation. */
#define KUI_TOY_PILOT_PAUSE_TEST 1
#include "../src/loader/toy_pilot_pause.c"
#include "kui/toy_pilot_gd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define CONTEXT UINT32_C(0x8c100000)
#define PARAM (CONTEXT+0x3000u)
#define OUTPUT (CONTEXT+0x4000u)
#define GAME_SP (CONTEXT+0x6000u)
#define CHECK_OUTPUT (GAME_SP-92u)
#define CONTROL (CONTEXT+WORK_BEGIN)

volatile uint32_t kui_toy_pilot_pause_entries,kui_toy_pilot_pause_retries;
volatile uint32_t kui_toy_pilot_pause_pumps,kui_toy_pilot_pause_retired;
volatile uint32_t kui_toy_pilot_pause_detail,kui_toy_pilot_pause_max_attempts;
static uint32_t memory[0x8000u/4u],sr,timer,pump_calls,worker_calls;
static uint32_t native_checks,native_submissions,native_refusals;
static struct kui_retail_gd gd;
static struct kui_toy_pilot_snapshot snapshot;

extern int32_t kui_toy_pilot_gd_dispatch(struct kui_retail_gd *,uint32_t,
    uint32_t,uint32_t,uintptr_t);
static uint32_t *at(uint32_t address) {
    assert(!(address&3u) && address>=CONTEXT && address<=CONTEXT+sizeof(memory)-4u);
    return &memory[(address-CONTEXT)/4u];
}
static uint16_t halfword(uint32_t address) {
    assert(!(address&1u));return (uint16_t)(*at(address&~3u)>>((address&2u)*8u));
}
static void put_half(uint32_t address,uint16_t value) {
    assert(!(address&1u));uint32_t *p=at(address&~3u),shift=(address&2u)*8u;
    *p=(*p&~(UINT32_C(0xffff)<<shift))|(uint32_t)value<<shift;
}
static uint8_t *map(void *context,uint32_t address,uint32_t bytes,int writing) {
    (void)context;(void)writing;
    if(address<CONTEXT || address-CONTEXT>sizeof(memory) ||
       bytes>sizeof(memory)-(address-CONTEXT)) return NULL;
    return (uint8_t *)memory+address-CONTEXT;
}
static int extent(void *context,uint32_t lba,uint32_t count,uint32_t bytes) {
    (void)context;(void)lba;(void)count;(void)bytes;return 0;
}
static int read_image(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *out) {
    (void)context;(void)lba;(void)count;(void)bytes;(void)out;
    assert(!"Native scalar lifecycle must not execute an image read");return -1;
}
static int32_t base(uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7) {
    return kui_retail_gd_dispatch(&gd,r4,r5,r6,r7);
}
static int32_t dispatch(uint32_t r4,uint32_t r5,uint32_t function) {
    return kui_toy_pilot_gd_dispatch(&gd,r4,r5,function,(uintptr_t)base);
}
uint32_t kui_toy_pilot_request(uint32_t command,uint32_t p0,uint32_t p1,uint32_t p2) {
    snapshot.command=command;snapshot.parameters[0]=p0;
    snapshot.parameters[1]=p1;snapshot.parameters[2]=p2;
    return ++snapshot.generation;
}
const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void) { return &snapshot; }
uint32_t kui_toy_pilot_pause_test_sr(void) { return sr; }
void kui_toy_pilot_pause_test_set_sr(uint32_t value) { sr=value; }
uint32_t kui_toy_pilot_pause_test_game_sp(void) { return GAME_SP; }
void kui_toy_pilot_worker_step(void) { assert(sr==0x40000021u);++worker_calls; }
void kui_toy_pilot_worker_pause_fault(uint32_t detail) {
    (void)detail;assert(!"Native lifecycle fixture must remain live");
}
uint32_t kui_toy_pilot_pause_test_read(uint32_t address,unsigned bytes) {
    assert(bytes==4u);
    switch(address) {
    case NATIVE_CONTEXT:return CONTEXT;
    case NATIVE_WRAPPERS:return NATIVE_WRAPPER_TABLE;
    case NATIVE_WRAPPER_TABLE+100u:return 0x8c0b23a8u;
    case NATIVE_GD_TABLE+16u:return 0x8c0bd366u;
    case NATIVE_GD_TABLE+36u:return 0x8c0bd494u;
    case NATIVE_GD_TABLE+48u:return 0x8c0bd4e6u;
    case NATIVE_GD_TABLE+64u:return 0x8c0bd552u;
    case NATIVE_GD_TABLE+72u:return 0x8c0bd566u;
    case TIMER_TCNT:return ~(timer++);
    default:return *at(address);
    }
}

/* Admission rejects only a missing record or the currently owned record.
 * Allocation flag76 remains set after retirement and is not a PAUSE veto. */
static bool native_admit(uint32_t work) {
    if(!work || *at(*at(work)+28u)==work) return false;
    *at(work+4u)=*at(work+8u)=*at(work+12u)=*at(work+16u)=0u;
    *at(work+20u)=*at(work+24u)=*at(work+28u)=*at(work+32u)=0u;
    *at(work+60u)=*at(work+68u)=0u;
    put_half(work+72u,0u);put_half(work+74u,0u);put_half(work+76u,1u);
    put_half(work+80u,0u);put_half(work+82u,0u);return true;
}
static int32_t native_pause_attempt(void) {
    uint32_t work=*at(CONTEXT+20u);
    if(!native_admit(work)) return -13;
    int32_t token=dispatch(KUI_RETAIL_GD_PAUSE,0u,KUI_GD_REQUEST);
    if(!token) { ++native_refusals;return -13; }
    assert(token>0);++native_submissions;
    *at(work+60u)=(uint32_t)token;put_half(work+80u,4u);
    bool status_followup=halfword(CONTEXT+58u)!=0u && *at(CONTEXT+44u)>=0x10001u;
    put_half(work+72u,status_followup?8u:6u);
    if(status_followup) *at(work+4u)=OUTPUT+8u;
    *at(CONTEXT+28u)=work;
    assert(dispatch(0,0,KUI_GD_EXEC)==0);return 0;
}
static int32_t native_check(uint32_t token,uint32_t values[3]) {
    static const int32_t errors[17]={
        -27,-28,-29,-30,-31,-32,-33,-34,-26,-26,-26,-35,-26,-26,-26,-26,-36
    };
    int32_t result=dispatch(token,CHECK_OUTPUT,KUI_GD_CHECK);++native_checks;
    switch(result) {
    case KUI_GD_NOT_FOUND:return 0;
    case KUI_GD_PROCESSING:values[0]=*at(CHECK_OUTPUT+8u);values[2]=*at(CHECK_OUTPUT+12u);return 4;
    case KUI_GD_COMPLETED:values[0]=*at(CHECK_OUTPUT+8u);return 1;
    case 3:return 65;
    case 4:return 67;
    case KUI_GD_FAILED:
        values[0]=(uint32_t)(*at(CHECK_OUTPUT)<17u?errors[*at(CHECK_OUTPUT)]:-26);
        values[1]=*at(CHECK_OUTPUT+4u);return 5;
    default:return result;
    }
}
void kui_toy_pilot_pause_test_pump(uint32_t context,uint32_t borrowed_sp) {
    assert(context==CONTEXT && borrowed_sp==GAME_SP && (sr&0xf0u)==0xf0u);++pump_calls;
    if(*at(context+32u)) return;
    assert(dispatch(0,0,KUI_GD_EXEC)==0);
    uint32_t work=*at(context+28u);
    if(!work) return; /* EXEC does not acknowledge an ownerless GD handle. */
    *at(context+32u)=1u;
    uint32_t values[3]={0u,0u,0u};int32_t status=native_check(*at(work+60u),values);
    if(status==0 || status==1) {
        uint32_t subtype=halfword(work+72u);
        *at(context+28u)=*at(work+60u)=0u;
        put_half(work+72u,0u);put_half(work+80u,(uint16_t)status);
        if(status==1 && subtype==8u) {
            /* Native control completion chains REQ_STAT36 into the same
             * record. Completion of the first handle is not record idle. */
            *at(PARAM)=OUTPUT;*at(PARAM+4u)=OUTPUT+4u;
            *at(PARAM+8u)=*at(work+4u);*at(PARAM+12u)=OUTPUT+12u;
            int32_t token=dispatch(KUI_RETAIL_GD_REQ_STAT,PARAM,KUI_GD_REQUEST);
            assert(token>0);*at(work+60u)=(uint32_t)token;
            put_half(work+80u,4u);put_half(work+72u,6u);*at(context+28u)=work;
        }
    } else if(status==5 || status==64) {
        *at(context+28u)=*at(work+60u)=0u;
        put_half(work+80u,(uint16_t)status);put_half(work+82u,(uint16_t)values[0]);
        *at(work+68u)=values[1];
    }
    *at(context+32u)=0u;
}
static void pump(void) {
    uint32_t entry=sr;sr|=0xf0u;
    kui_toy_pilot_pause_test_pump(CONTEXT,GAME_SP);sr=entry;
}
static void reset(void) {
    static const union kui_retail_slot tracks[]={
        {.track={.start_lba=0,.end_lba=8,.control=4}},
        {.track={.start_lba=16,.end_lba=24}},
        {.track={.start_lba=45000,.end_lba=60000,.control=4}}
    };
    const struct kui_gd_ops ops={NULL,map,extent,read_image};
    memset(memory,0,sizeof(memory));
    assert(!kui_retail_gd_init(&gd,tracks,3u,&ops,CONTEXT,CONTEXT+sizeof(memory)));
    snapshot=(struct kui_toy_pilot_snapshot){.generation=1u,.applied_generation=1u,
        .state=KUI_TOY_PILOT_PLAYING,.driver_verified=1u,.sdk_init_result=1u,
        .track=2u,.position_fad=166u,.end_fad=174u};
    sr=0x40000021u;timer=pump_calls=worker_calls=native_checks=native_submissions=native_refusals=0u;
    kui_toy_pilot_pause_entries=kui_toy_pilot_pause_retries=kui_toy_pilot_pause_pumps=0u;
    kui_toy_pilot_pause_retired=kui_toy_pilot_pause_detail=kui_toy_pilot_pause_max_attempts=0u;
    attempts=first_tick=last_tick=retry_context=0u;
    *at(CONTEXT)=WORK_COUNT;*at(CONTEXT+12u)=NATIVE_GD_TABLE;
    *at(CONTEXT+20u)=CONTROL;*at(CONTEXT+24u)=CONTROL+WORK_BYTES;
    *at(CONTEXT+36u)=0x8c04d542u;*at(CONTROL)=CONTEXT;
}
static void native_play(void) {
    assert(native_admit(CONTROL));
    *at(PARAM)=*at(PARAM+4u)=2u;*at(PARAM+8u)=0u;
    int32_t token=dispatch(KUI_RETAIL_GD_PLAY,PARAM,KUI_GD_REQUEST);assert(token>0);
    *at(CONTROL+60u)=(uint32_t)token;put_half(CONTROL+72u,6u);
    put_half(CONTROL+80u,4u);*at(CONTEXT+28u)=CONTROL;
    assert(dispatch(0,0,KUI_GD_EXEC)==0);
}
static void owner_rejects_then_retry_admits(void) {
    reset();native_play();uint32_t play_token=*at(CONTROL+60u);
    assert(native_pause_attempt()==-13 && *at(CONTROL+60u)==play_token);
    assert(!native_refusals && snapshot.command==KUI_RETAIL_GD_PLAY);
    kui_toy_pilot_pause_after(-13);
    assert(!*at(CONTEXT+28u) && !gd.command && kui_toy_pilot_pause_retired==1u);
    assert(halfword(CONTROL+76u)==1u && native_pause_attempt()==0);
    assert(snapshot.command==KUI_RETAIL_GD_PAUSE && native_submissions==1u);
    pump();assert(!*at(CONTEXT+28u) && !gd.command && native_checks==2u);
    assert(!kui_toy_pilot_pause_detail && sr==0x40000021u);
}
static void subtype8_keeps_chained_status_owner(void) {
    reset();*at(CONTEXT+44u)=0x10001u;put_half(CONTEXT+58u,1u);
    assert(native_pause_attempt()==0 && halfword(CONTROL+72u)==8u);
    uint32_t pause_token=*at(CONTROL+60u);pump();
    assert(native_checks==1u && gd.pending && gd.command==KUI_RETAIL_GD_REQ_STAT);
    assert(*at(CONTEXT+28u)==CONTROL && *at(CONTROL+60u)!=pause_token);
    assert(halfword(CONTROL+72u)==6u && halfword(CONTROL+80u)==4u);
    assert(native_pause_attempt()==-13 && !native_refusals);
    pump();assert(native_checks==2u && !gd.command && !*at(CONTEXT+28u));
    assert(*at(OUTPUT)==1u && *at(OUTPUT+4u)==2u);
    assert((*at(OUTPUT+8u)&0x00ffffffu)==166u && *at(OUTPUT+12u)==1u);
    assert(halfword(CONTROL+72u)==0u && halfword(CONTROL+76u)==1u);
}
static void ownerless_terminal_handle_blocks_pause(void) {
    reset();native_play();kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_retired==1u && !gd.command && !*at(CONTEXT+28u));
    *at(PARAM)=1u;*at(PARAM+4u)=14u;*at(PARAM+8u)=OUTPUT;
    int32_t terminal=dispatch(KUI_RETAIL_GD_GETSCD,PARAM,KUI_GD_REQUEST);assert(terminal>0);
    assert(dispatch(0,0,KUI_GD_EXEC)==0 && !gd.pending && gd.command==KUI_RETAIL_GD_GETSCD);
    uint32_t checks_before=native_checks;
    for(unsigned i=0;i<8u;i++) {
        assert(native_pause_attempt()==-13 && !*at(CONTEXT+28u));
        kui_toy_pilot_pause_after(-13);
        assert(gd.command==KUI_RETAIL_GD_GETSCD && !gd.pending);
        assert(!*at(CONTROL+60u) && halfword(CONTROL+72u)==0u);
    }
    assert(native_refusals==8u && native_checks==checks_before && pump_calls==9u);
    assert(kui_toy_pilot_pause_retired==1u && snapshot.command==KUI_RETAIL_GD_PLAY);
    /* The precise owner acknowledges its own result. Automatic pumps must
     * not guess that an unrelated caller has abandoned a terminal handle. */
    assert(dispatch((uint32_t)terminal,CHECK_OUTPUT,KUI_GD_CHECK)==KUI_GD_COMPLETED);
    assert(!gd.command && native_pause_attempt()==0);pump();
    assert(!gd.command && !*at(CONTEXT+28u) && !kui_toy_pilot_pause_detail);
}
static void failed_native_check_does_not_retire_other_handle(void) {
    reset();native_play();uint32_t actual_token=*at(CONTROL+60u);
    *at(CONTROL+60u)=actual_token+1u;
    kui_toy_pilot_pause_after(-13);
    assert(!*at(CONTEXT+28u) && !*at(CONTROL+60u));
    assert(halfword(CONTROL+80u)==5u && (int16_t)halfword(CONTROL+82u)==-32);
    assert(gd.token==actual_token && gd.command==KUI_RETAIL_GD_PLAY && !gd.pending);
    assert(kui_toy_pilot_pause_retired==1u);
    uint32_t checks_before=native_checks;
    for(unsigned i=0;i<8u;i++) {
        assert(native_pause_attempt()==-13);kui_toy_pilot_pause_after(-13);
        assert(!*at(CONTEXT+28u) && gd.command==KUI_RETAIL_GD_PLAY);
    }
    assert(native_refusals==8u && native_checks==checks_before && pump_calls==9u);
    assert(kui_toy_pilot_pause_retired==1u && snapshot.command==KUI_RETAIL_GD_PLAY);
    assert(!kui_toy_pilot_pause_detail && sr==0x40000021u);
}
int main(void) {
    owner_rejects_then_retry_admits();subtype8_keeps_chained_status_owner();
    ownerless_terminal_handle_blocks_pause();failed_native_check_does_not_retire_other_handle();
    puts("Toy native lifecycle: real GD admission, owner equality, subtype8 status chain, ownerless terminal refusal and exact SR pass");
    return 0;
}
