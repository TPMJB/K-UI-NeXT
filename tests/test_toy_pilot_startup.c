/* SPDX-License-Identifier: GPL-3.0-only */
/* Startup integration: real GD acceptance/CHECK feeds the observed native
 * scalar protocol, with the game's normal registered error callback present.
 * No game instructions, sound driver, or duplicated audio algorithm. */
#define KUI_TOY_PILOT_PAUSE_TEST 1
#include "../src/loader/toy_pilot_pause.c"
#include "kui/toy_pilot_gd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define CONTEXT UINT32_C(0x8c100000)
#define PARAM (CONTEXT+0x40u)
#define OUTPUT (CONTEXT+0x4000u)
#define GAME_SP (CONTEXT+0x3000u)
#define CHECK_OUTPUT (GAME_SP-92u)
#define REGISTERED_ERROR UINT32_C(0x8c04d542)
#define REGISTERED_DELEGATE UINT32_C(0x8c04d53e)
#define ERROR_DELEGATE_ADDRESS UINT32_C(0x8c0a7590)
#define ERROR_ACTIVE_ADDRESS UINT32_C(0x8c0a759c)

volatile uint32_t kui_toy_pilot_pause_entries, kui_toy_pilot_pause_retries;
volatile uint32_t kui_toy_pilot_pause_pumps, kui_toy_pilot_pause_retired;
volatile uint32_t kui_toy_pilot_pause_detail, kui_toy_pilot_pause_max_attempts;
static uint32_t memory[0x8000u/4u], sr, timer, delegate, delegate_active;
static unsigned worker_calls, pump_calls, faults, delegate_calls, image_reads;
static bool refuse_audio;
static struct kui_retail_gd service;
static struct kui_toy_pilot_snapshot snapshot;
static int32_t last_native_status, last_native_error;

extern int32_t kui_toy_pilot_gd_dispatch(struct kui_retail_gd *,uint32_t,
    uint32_t,uint32_t,uintptr_t);

static uint32_t *at(uint32_t address) {
    assert(address>=CONTEXT && address<=CONTEXT+sizeof(memory)-4u);
    assert(!(address&3u));return &memory[(address-CONTEXT)/4u];
}
static void put_half(uint32_t address,uint16_t value) {
    assert(!(address&1u));
    uint32_t *p=at(address&~3u),shift=(address&2u)*8u;
    *p=(*p&~(UINT32_C(0xffff)<<shift))|(uint32_t)value<<shift;
}
static uint8_t *map(void *context,uint32_t address,uint32_t bytes,int writing) {
    (void)context;(void)writing;
    if(address<CONTEXT || address-CONTEXT>sizeof(memory) ||
       bytes>sizeof(memory)-(address-CONTEXT)) return NULL;
    return (uint8_t *)memory+address-CONTEXT;
}
static int check_extent(void *context,uint32_t lba,uint32_t count,uint32_t bytes) {
    (void)context;(void)lba;(void)count;(void)bytes;return 0;
}
static int read_image(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *out) {
    (void)context;(void)lba;++image_reads;memset(out,0,count*bytes);return 0;
}
static int32_t base(uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7) {
    return kui_retail_gd_dispatch(&service,r4,r5,r6,r7);
}
static int32_t dispatch(uint32_t r4,uint32_t r5,uint32_t function) {
    return kui_toy_pilot_gd_dispatch(&service,r4,r5,function,(uintptr_t)base);
}
uint32_t kui_toy_pilot_request(uint32_t command,uint32_t p0,uint32_t p1,uint32_t p2) {
    if(refuse_audio && (command==KUI_RETAIL_GD_PAUSE || command==KUI_RETAIL_GD_RELEASE))
        return 0u;
    snapshot.command=command;snapshot.parameters[0]=p0;
    snapshot.parameters[1]=p1;snapshot.parameters[2]=p2;
    return ++snapshot.generation;
}
const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void) { return &snapshot; }

/* Observed CHECK contract at native endpoint 0x8c0bd366. These numerical
 * status/error values are independent of the native instruction sequence. */
static int32_t native_check_status(int32_t bios,const uint32_t words[4],int32_t *error) {
    static const int32_t error_classes[17]={
        -27,-28,-29,-30,-31,-32,-33,-34,-26,-26,-26,-35,-26,-26,-26,-26,-36
    };
    *error=0;
    switch(bios) {
    case KUI_GD_NOT_FOUND:return 0;
    case KUI_GD_PROCESSING:return 4;
    case KUI_GD_COMPLETED:return 1;
    case 3:return 65;
    case 4:return 67;
    case KUI_GD_FAILED:
        *error=words[0]<17u?error_classes[words[0]]:-26;
        return 5;
    default:return bios;
    }
}
/* Native registered callback at 0x8c04d542 calls its dynamic delegate only
 * for these two error codes, with a present delegate and a clear busy flag. */
static void native_error_callback(int32_t error) {
    if((error!=-23 && error!=-33) || !delegate || delegate_active) return;
    delegate_active=1u;++delegate_calls;delegate_active=0u;
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
    case ERROR_DELEGATE_ADDRESS:return delegate;
    case ERROR_ACTIVE_ADDRESS:return delegate_active;
    case TIMER_TCNT:return ~(timer++);
    default:return *at(address);
    }
}
uint32_t kui_toy_pilot_pause_test_sr(void) { return sr; }
void kui_toy_pilot_pause_test_set_sr(uint32_t value) { sr=value; }
uint32_t kui_toy_pilot_pause_test_game_sp(void) { return GAME_SP; }
void kui_toy_pilot_worker_step(void) { assert(sr==0x40000021u);++worker_calls; }
void kui_toy_pilot_worker_pause_fault(uint32_t detail) {
    assert(detail==kui_toy_pilot_pause_detail);assert(sr==0x40000021u);++faults;
}
void kui_toy_pilot_pause_test_pump(uint32_t context,uint32_t borrowed_sp) {
    assert(context==CONTEXT && borrowed_sp==GAME_SP && (sr&0xf0u)==0xf0u);
    ++pump_calls;
    if(*at(context+32u)) return;
    assert(dispatch(0,0,KUI_GD_EXEC)==0);
    uint32_t work=*at(context+28u);
    if(!work) return;
    *at(context+32u)=1u;
    int32_t bios=dispatch(*at(work+60u),CHECK_OUTPUT,KUI_GD_CHECK);
    uint32_t output[4];memcpy(output,at(CHECK_OUTPUT),sizeof(output));
    last_native_status=native_check_status(bios,output,&last_native_error);
    if(last_native_status==0 || last_native_status==1) {
        *at(context+28u)=0u;*at(work+60u)=0u;put_half(work+72u,0u);
        put_half(work+80u,(uint16_t)last_native_status);
    } else if(last_native_status==5 || last_native_status==64) {
        *at(context+28u)=0u;*at(work+60u)=0u;
        put_half(work+80u,(uint16_t)last_native_status);
        put_half(work+82u,(uint16_t)last_native_error);
        /* Success and PROCESSING do not call the registered error handler. */
        if(*at(context+36u)) native_error_callback(last_native_error);
    }
    *at(context+32u)=0u;
}
static void reset(void) {
    static const union kui_retail_slot tracks[]={
        {.track={.start_lba=0,.end_lba=8,.control=4}},
        {.track={.start_lba=16,.end_lba=24,.control=0}},
        {.track={.start_lba=45000,.end_lba=60000,.control=4}}
    };
    const struct kui_gd_ops ops={NULL,map,check_extent,read_image};
    memset(memory,0,sizeof(memory));
    assert(!kui_retail_gd_init(&service,tracks,3u,&ops,CONTEXT,CONTEXT+sizeof(memory)));
    snapshot=(struct kui_toy_pilot_snapshot){.generation=1u,.applied_generation=1u,
        .state=KUI_TOY_PILOT_STOPPED,.driver_verified=1u,.sdk_init_result=1u};
    sr=0x40000021u;timer=0u;delegate=REGISTERED_DELEGATE;delegate_active=0u;
    worker_calls=pump_calls=faults=delegate_calls=image_reads=0u;
    refuse_audio=false;
    last_native_status=last_native_error=0;
    kui_toy_pilot_pause_entries=kui_toy_pilot_pause_retries=0u;
    kui_toy_pilot_pause_pumps=kui_toy_pilot_pause_retired=0u;
    kui_toy_pilot_pause_detail=kui_toy_pilot_pause_max_attempts=0u;
    attempts=first_tick=last_tick=retry_context=0u;
    *at(CONTEXT)=WORK_COUNT;*at(CONTEXT+12u)=NATIVE_GD_TABLE;
    *at(CONTEXT+20u)=CONTEXT+WORK_BEGIN;
    *at(CONTEXT+24u)=CONTEXT+WORK_BEGIN+WORK_BYTES;
    *at(CONTEXT+36u)=REGISTERED_ERROR;
}
static uint32_t request(uint32_t command) {
    int32_t token=dispatch(command,PARAM,KUI_GD_REQUEST);assert(token>0);
    uint32_t work=CONTEXT+WORK_BEGIN;
    *at(CONTEXT+28u)=work;*at(work)=CONTEXT;*at(work+60u)=(uint32_t)token;
    put_half(work+72u,6u);return (uint32_t)token;
}
static void pause_retry(void) {
    unsigned previous_worker_calls=worker_calls;
    kui_toy_pilot_pause_after(-13);
    assert(!faults && !kui_toy_pilot_pause_detail && sr==0x40000021u);
    assert(worker_calls==previous_worker_calls+1u);
    assert(pump_calls==kui_toy_pilot_pause_pumps && !delegate_calls);
}
static void audio_success_with_registered_delegate(void) {
    reset();*at(PARAM)=2u;*at(PARAM+4u)=2u;*at(PARAM+8u)=0u;
    uint32_t token=request(KUI_RETAIL_GD_PLAY);
    assert(snapshot.generation!=snapshot.applied_generation && service.pending);
    pause_retry();
    assert(last_native_status==1 && !last_native_error && !service.command && !service.pending);
    assert(!*at(CONTEXT+28u) && !*at(CONTEXT+WORK_BEGIN+60u));
    assert(kui_toy_pilot_pause_retired==1u && !image_reads);
    /* Acknowledgement must not pretend hardware audio has applied. */
    assert(snapshot.applied_generation==1u && snapshot.generation==2u);
    assert(dispatch(token,CHECK_OUTPUT,KUI_GD_CHECK)==KUI_GD_NOT_FOUND);
    kui_toy_pilot_pause_after(0);assert(!attempts && !retry_context);
}
static void startup_init_reset_and_idle_pause(void) {
    reset();
    assert(dispatch(0,0,KUI_GD_INIT)==0 && snapshot.command==KUI_TOY_PILOT_RESET);
    assert(!service.command && !service.pending && service.sector_bytes==2048u);
    uint32_t token=request(KUI_GD_COMMAND_INIT);
    assert(snapshot.command==KUI_TOY_PILOT_RESET && service.pending);
    /* Startup's allocated control record keeps the next SDK admission busy.
     * A retry services that real INIT handle with the normal callback set. */
    pause_retry();assert(last_native_status==1 && !service.command && !service.pending);
    assert(dispatch(token,CHECK_OUTPUT,KUI_GD_CHECK)==KUI_GD_NOT_FOUND);
    kui_toy_pilot_pause_after(0);assert(!attempts && !retry_context);
    /* Inject mailbox refusal to cover the unavailable GD error contract.
     * Real idle PAUSE acceptance is covered by the production-worker flow. */
    refuse_audio=true;token=request(KUI_RETAIL_GD_PAUSE);
    assert(!service.pending && service.status==KUI_GD_FAILED);
    pause_retry();assert(last_native_status==5 && last_native_error==-28);
    assert(!service.command && !*at(CONTEXT+28u) && !delegate_calls && !image_reads);
    kui_toy_pilot_pause_after(-28);assert(!attempts && !retry_context);
    assert(dispatch(token,CHECK_OUTPUT,KUI_GD_CHECK)==KUI_GD_NOT_FOUND);
    assert(dispatch(0,0,KUI_GD_RESET)==0 && snapshot.command==KUI_TOY_PILOT_RESET);
    assert(!service.command && !service.pending && !snapshot.fault && !faults);
}
static void pending_then_complete(void) {
    reset();*at(PARAM)=45150u;*at(PARAM+4u)=4u;*at(PARAM+8u)=OUTPUT;
    uint32_t token=request(KUI_GD_PIOREAD);
    pause_retry();
    assert(last_native_status==4 && !last_native_error && service.pending && service.command);
    assert(service.completed_bytes==4096u && *at(CONTEXT+28u)==CONTEXT+WORK_BEGIN);
    assert(*at(CONTEXT+WORK_BEGIN+60u)==token && !kui_toy_pilot_pause_retired);
    pause_retry();
    assert(last_native_status==1 && !service.pending && !service.command && image_reads==2u);
    assert(!*at(CONTEXT+28u) && kui_toy_pilot_pause_retired==1u);
}
static void illegal_handle_keeps_real_handle(void) {
    reset();*at(PARAM)=2u;*at(PARAM+4u)=2u;
    uint32_t token=request(KUI_RETAIL_GD_PLAY);
    *at(CONTEXT+WORK_BEGIN+60u)=token+1u;
    pause_retry();
    assert(last_native_status==5 && last_native_error==-32);
    assert(!*at(CONTEXT+28u) && service.command && !service.pending);
    assert(*at(CHECK_OUTPUT)==5u && dispatch(token,CHECK_OUTPUT,KUI_GD_CHECK)==KUI_GD_COMPLETED);
    assert(!service.command && kui_toy_pilot_pause_retired==1u);
}
static void unavailable_returns_without_delegate(void) {
    reset();*at(PARAM)=2u;*at(PARAM+4u)=2u;
    uint32_t token=request(KUI_RETAIL_GD_PLAY);
    snapshot.fault=KUI_TOY_PILOT_FAULT_QUEUE;
    pause_retry();
    assert(last_native_status==5 && last_native_error==-28);
    assert(*at(CHECK_OUTPUT)==1u && *at(CHECK_OUTPUT+4u)==KUI_GD_ERROR_UNAVAILABLE);
    assert(!service.command && !service.pending && !*at(CONTEXT+28u));
    assert(dispatch(token,CHECK_OUTPUT,KUI_GD_CHECK)==KUI_GD_NOT_FOUND);
}
static void callback_scalar_boundaries(void) {
    reset();const int32_t returning[]={0,-13,-19,-26,-27,-28,-29,-30,-31,-32,-34,-35,-36,-37};
    for(unsigned i=0;i<sizeof(returning)/sizeof(*returning);i++) native_error_callback(returning[i]);
    assert(!delegate_calls && !delegate_active);
    native_error_callback(-23);native_error_callback(-33);
    assert(delegate_calls==2u && !delegate_active);
    delegate_active=1u;native_error_callback(-23);native_error_callback(-33);
    assert(delegate_calls==2u && delegate_active==1u);
    delegate_active=0u;delegate=0u;native_error_callback(-23);native_error_callback(-33);
    assert(delegate_calls==2u && !delegate_active);
    uint32_t words[4]={6u,0u,0u,0u};int32_t error;
    assert(native_check_status(KUI_GD_FAILED,words,&error)==5 && error==-33);
    /* The real service above emits error class1/5, never class6. */
    words[0]=1u;assert(native_check_status(KUI_GD_FAILED,words,&error)==5 && error==-28);
    words[0]=5u;assert(native_check_status(KUI_GD_FAILED,words,&error)==5 && error==-32);
}
int main(void) {
    startup_init_reset_and_idle_pause();audio_success_with_registered_delegate();pending_then_complete();
    illegal_handle_keeps_real_handle();unavailable_returns_without_delegate();
    callback_scalar_boundaries();
    puts("Toy startup integration: registered callback, real GD acceptance/CHECK, native status translation, retained pending/invalid handle ownership and exact SR pass");
    return 0;
}
