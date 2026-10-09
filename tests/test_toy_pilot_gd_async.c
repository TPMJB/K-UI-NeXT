/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the split adapter with both real GD cores: the separately named
 * low synchronous core validates submissions, the linked async core answers
 * data EXEC/CHECK. The fake physical engine controls completion timing only. */
#include "kui/toy_pilot_gd.h"
#include "toy_pilot_sci.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifndef KUI_RETAIL_GD_ASYNC
#error This fixture requires the separately linked high asynchronous GD core
#endif
#undef KUI_RETAIL_GD_ASYNC
#define kui_retail_gd_dispatch fixture_low_dispatch
#define kui_retail_gd_init fixture_low_init
#define kui_retail_gd_init_validated fixture_low_init_validated
#define kui_retail_gd_init_prepared fixture_low_init_prepared
#define kui_retail_gd_set_disc_type fixture_low_set_disc_type
void fixture_low_init_validated(struct kui_retail_gd *,const union kui_retail_slot *,
    uint32_t,const struct kui_gd_ops *,uint32_t,uint32_t);
#include "../src/core/retail_gd.c"
#undef kui_retail_gd_dispatch
#undef kui_retail_gd_init
#undef kui_retail_gd_init_validated
#undef kui_retail_gd_init_prepared
#undef kui_retail_gd_set_disc_type
#define KUI_RETAIL_GD_ASYNC 1

#define BEGIN UINT32_C(0x8c010000)
#define PARAM (BEGIN+64u)
#define STATUS (BEGIN+128u)
#define DATA (BEGIN+512u)
static uint8_t fixture_ram[8192];
static struct kui_retail_gd fixture_service;
static struct kui_toy_pilot_snapshot fixture_snapshot;
static unsigned low_calls, low_execs, hidden_steps, physical_reads;
static unsigned pumps, services, starts, cancels, audio_requests;
static uint32_t live_token, cancelled_token;
static bool engine_live, produce, reject_source;
#if KUI_TOY_PILOT_ASYNC_CDDA
static bool raw_pending;
static unsigned raw_progress;
#endif

extern int32_t kui_toy_pilot_gd_dispatch(struct kui_retail_gd *,uint32_t,
    uint32_t,uint32_t,uintptr_t);

static uint8_t *fixture_map(void *context,uint32_t address,uint32_t bytes,int writing) {
    (void)context;
    if(address<BEGIN || address-BEGIN>sizeof(fixture_ram) ||
       bytes>sizeof(fixture_ram)-(address-BEGIN)) return NULL;
    return writing==KUI_RETAIL_MAP_VALIDATE?fixture_ram+sizeof(fixture_ram):
        fixture_ram+address-BEGIN;
}
static int fixture_check(void *context,uint32_t lba,uint32_t count,uint32_t bytes) {
    (void)context;(void)lba;(void)count;(void)bytes;return reject_source?-1:0;
}
static int fixture_read(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *out) {
    (void)context;(void)lba;(void)count;(void)bytes;(void)out;++physical_reads;
    assert(!"async data calls must never invoke the low synchronous reader");return -1;
}
static int32_t fixture_base(uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7) {
    ++low_calls;
    /* Deliberately eager low CHECK pacing, so routing errors perform I/O. */
    if(r7==KUI_GD_CHECK && kui_toy_pilot_gd_data_pending(&fixture_service)) {
        ++hidden_steps;
        (void)fixture_low_dispatch(&fixture_service,0,0,0,KUI_GD_EXEC);
    }
    if(r7==KUI_GD_EXEC) ++low_execs;
    return fixture_low_dispatch(&fixture_service,r4,r5,r6,r7);
}
static int32_t fixture_dispatch(uint32_t r4,uint32_t r5,uint32_t function) {
    return kui_toy_pilot_gd_dispatch(&fixture_service,r4,r5,function,(uintptr_t)fixture_base);
}
uint32_t kui_toy_pilot_request(uint32_t command,uint32_t p0,uint32_t p1,uint32_t p2) {
    ++audio_requests;fixture_snapshot.command=command;
    fixture_snapshot.parameters[0]=p0;fixture_snapshot.parameters[1]=p1;
    fixture_snapshot.parameters[2]=p2;return ++fixture_snapshot.generation;
}
const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void) { return &fixture_snapshot; }
static void fixture_arrival(void) {
    struct kui_retail_gd *s=&fixture_service;
    if(!engine_live || !s->pending || s->token!=live_token) return;
    uint32_t done=s->completed_bytes/s->sector_bytes;
    uint8_t *out=fixture_map(NULL,(s->destination&0x00ffffffu)|0x8c000000u,
        s->request_bytes,1);
    assert(out && done<s->count);
    memset(out+done*s->sector_bytes,0x30+(int)done,s->sector_bytes);
    kui_retail_gd_progress(s,done+1u,0);
    if(!s->pending) engine_live=false;
}
static int fixture_engine_step(struct kui_retail_gd *s) {
    assert(s==&fixture_service);
    if(!kui_toy_pilot_gd_data_pending(s)) return 0;
    if(!engine_live) {
        assert(s->token!=cancelled_token);live_token=s->token;engine_live=true;++starts;
        return 0;
    }
    assert(s->token==live_token);
    if(produce) fixture_arrival();
    return 0;
}
int kui_toy_pilot_sci_pump(struct kui_retail_gd *s) {
    ++pumps; return fixture_engine_step(s);
}
int kui_toy_pilot_sci_service(struct kui_retail_gd *s) {
#if KUI_TOY_PILOT_ASYNC_CDDA
    if(raw_pending) ++raw_progress;
#endif
    ++services; return fixture_engine_step(s);
}
void kui_toy_pilot_sci_cancel(struct kui_retail_gd *s) {
    assert(s==&fixture_service);++cancels;
    if(engine_live) {
        /* Cancellation must fence while the old handle is still live. */
        assert(s->pending && s->token==live_token);
        cancelled_token=live_token;engine_live=false;
    }
}
static void fixture_put(uint32_t address,uint32_t value) {
    put32(fixture_ram+address-BEGIN,value);
}
static uint32_t fixture_get(uint32_t address) { return get32(fixture_ram+address-BEGIN); }
static void fixture_init(void) {
    static const union kui_retail_slot tracks[]={
        {.track={.start_lba=0,.end_lba=8,.control=4}},
        {.track={.start_lba=16,.end_lba=24,.control=0}},
        {.track={.start_lba=45000,.end_lba=60000,.control=4}}
    };
    const struct kui_gd_ops ops={NULL,fixture_map,fixture_check,fixture_read};
    assert(!fixture_low_init(&fixture_service,tracks,3,&ops,BEGIN,BEGIN+sizeof(fixture_ram)));
    fixture_snapshot=(struct kui_toy_pilot_snapshot){.command=20,.generation=7,
        .applied_generation=7,.state=KUI_TOY_PILOT_PLAYING,.track=2,.position_fad=170};
    memset(fixture_ram,0xcc,sizeof(fixture_ram));
    low_calls=low_execs=hidden_steps=physical_reads=pumps=services=starts=cancels=audio_requests=0;
    live_token=cancelled_token=0;engine_live=produce=reject_source=false;
#if KUI_TOY_PILOT_ASYNC_CDDA
    raw_pending=false;raw_progress=0u;
#endif
    fixture_put(PARAM,150);fixture_put(PARAM+4u,3);
    fixture_put(PARAM+8u,DATA|0x20000000u);fixture_put(PARAM+12u,0);
}
static uint32_t fixture_request(uint32_t command) {
    int32_t result=fixture_dispatch(command,PARAM,KUI_GD_REQUEST);
    assert(result>0 && fixture_service.pending && engine_live);
    return (uint32_t)result;
}
static void data_progress_and_handles(void) {
    const uint32_t commands[]={KUI_GD_PIOREAD,KUI_GD_DMAREAD};
    for(unsigned c=0;c<2;c++) {
        fixture_init();uint32_t token=fixture_request(commands[c]);
        assert(low_calls==1 && starts==1 && pumps==1 && !services && audio_requests==0);
        unsigned before=low_calls;
        for(unsigned i=0;i<4;i++) {
            assert(fixture_dispatch(0,0,KUI_GD_EXEC)==0);
            assert(fixture_dispatch(token,STATUS,KUI_GD_CHECK)==KUI_GD_PROCESSING);
            assert(fixture_get(STATUS+8u)==0 && fixture_service.pending);
        }
        assert(low_calls==before && !low_execs && !hidden_steps && !physical_reads);
        assert(pumps==1 && services==8u);
        /* Bad CHECK tokens may observe progress but cannot consume ownership. */
        assert(fixture_dispatch(token+1u,STATUS,KUI_GD_CHECK)==KUI_GD_FAILED);
        assert(fixture_service.command==commands[c] && fixture_service.pending);
        assert(fixture_get(STATUS)==5);
        produce=true;
        assert(fixture_dispatch(0,0,KUI_GD_EXEC)==0);
        assert(fixture_service.completed_bytes==2048);
        assert(fixture_dispatch(token,STATUS,KUI_GD_CHECK)==KUI_GD_PROCESSING);
        assert(fixture_get(STATUS+8u)==4096);
        assert(fixture_dispatch(0,0,KUI_GD_EXEC)==0);
        assert(!fixture_service.pending && fixture_service.command==commands[c]);
        assert(fixture_dispatch(KUI_GD_NOP,0,KUI_GD_REQUEST)==0);
        assert(fixture_service.command==commands[c]);
        assert(fixture_dispatch(token+1u,STATUS,KUI_GD_CHECK)==KUI_GD_FAILED);
        assert(fixture_service.command==commands[c]);
        assert(fixture_dispatch(token,STATUS,KUI_GD_CHECK)==KUI_GD_COMPLETED);
        assert(fixture_get(STATUS+8u)==6144 && !fixture_service.command);
        assert(fixture_dispatch(token,STATUS,KUI_GD_CHECK)==KUI_GD_NOT_FOUND);
        for(unsigned i=0;i<6144;i++) assert(fixture_ram[DATA-BEGIN+i]==0x30+i/2048);
        assert(!audio_requests && !hidden_steps && !physical_reads && starts==1);
    }
}
static void validation_and_cancellation(void) {
    fixture_init();fixture_put(PARAM+8u,BEGIN+sizeof(fixture_ram)-4u);
    assert(fixture_dispatch(KUI_GD_PIOREAD,PARAM,KUI_GD_REQUEST)==0);
    assert(!pumps && !services && !starts && !engine_live);
    fixture_put(PARAM+8u,DATA);reject_source=true;
    assert(fixture_dispatch(KUI_GD_PIOREAD,PARAM,KUI_GD_REQUEST)==0);
    assert(!pumps && !services && !starts);reject_source=false;
    uint32_t token=fixture_request(KUI_GD_PIOREAD);produce=true;
    assert(fixture_dispatch(0,0,KUI_GD_EXEC)==0);
    assert(fixture_service.completed_bytes==2048);
    produce=false;
    assert(fixture_dispatch(token+1u,0,KUI_GD_ABORT)==-1 && !cancels && engine_live);
    assert(fixture_dispatch(0,0,KUI_GD_ABORT)==-1 && !cancels && engine_live);
    assert(fixture_dispatch(token,0,KUI_GD_ABORT)==0 && cancels==1 && !engine_live);
    assert(!audio_requests && fixture_service.error==KUI_GD_ERROR_CANCELLED);
    fixture_arrival(); /* A fenced old completion cannot touch the tail. */
    for(unsigned i=2048;i<6144;i++) assert(fixture_ram[DATA-BEGIN+i]==0xcc);
    assert(fixture_dispatch(token,STATUS,KUI_GD_CHECK)==KUI_GD_FAILED);
    assert(fixture_get(STATUS+4u)==KUI_GD_ERROR_CANCELLED && fixture_get(STATUS+8u)==2048);
    token=fixture_request(KUI_GD_DMAREAD);assert(token!=cancelled_token);
    assert(fixture_dispatch(0,0,KUI_GD_RESET)==0 && cancels==2 && !engine_live);
    assert(!fixture_service.pending && !fixture_service.command && audio_requests==1);
    fixture_arrival();assert(fixture_service.completed_bytes==0);
}
static void audio_keeps_its_scalar_base(void) {
    fixture_init();fixture_put(PARAM,2);fixture_put(PARAM+4u,2);fixture_put(PARAM+8u,0);
    int32_t token=fixture_dispatch(KUI_RETAIL_GD_PLAY,PARAM,KUI_GD_REQUEST);
    assert(token>0 && audio_requests==1 && !starts && !pumps && !services);
    assert(fixture_dispatch(0,0,KUI_GD_EXEC)==0 && low_execs==1);
    assert(fixture_dispatch((uint32_t)token,STATUS,KUI_GD_CHECK)==KUI_GD_COMPLETED);
    assert(!cancels && !physical_reads && !fixture_service.command);
#if KUI_TOY_PILOT_ASYNC_CDDA
    assert(services==2u);
#else
    assert(!services);
#endif
}
#if KUI_TOY_PILOT_ASYNC_CDDA
static void raw_progress_without_data_handle(void) {
    fixture_init();raw_pending=true;
    assert(!fixture_service.pending && !fixture_service.command);
    assert(fixture_dispatch(0u,0u,KUI_GD_EXEC)==0);
    assert(services==1u && raw_progress==1u && !starts && !pumps);
    assert(fixture_dispatch(777u,STATUS,KUI_GD_CHECK)==KUI_GD_FAILED);
    assert(fixture_get(STATUS)==5u && fixture_get(STATUS+4u)==0u);
    assert(services==2u && raw_progress==2u && !starts && !physical_reads);
    /* Audio protocol still uses its scalar base, with the raw receiver
     * serviced before the existing mailbox generation projection. */
    fixture_put(PARAM,2u);fixture_put(PARAM+4u,2u);fixture_put(PARAM+8u,0u);
    int32_t token=fixture_dispatch(KUI_RETAIL_GD_PLAY,PARAM,KUI_GD_REQUEST);
    assert(token>0 && services==2u && audio_requests==1u);
    assert(fixture_dispatch(0u,0u,KUI_GD_EXEC)==0);
    assert(fixture_dispatch((uint32_t)token,STATUS,KUI_GD_CHECK)==KUI_GD_COMPLETED);
    assert(services==4u && raw_progress==4u && !starts && !physical_reads && !cancels);
    raw_pending=false;
    assert(fixture_dispatch(0u,0u,KUI_GD_EXEC)==0);
    assert(services==5u && raw_progress==4u);
}
#endif
int main(void) {
    data_progress_and_handles();validation_and_cancellation();audio_keeps_its_scalar_base();
#if KUI_TOY_PILOT_ASYNC_CDDA
    raw_progress_without_data_handle();
    puts("Toy async audio GD adapter: scalar/audio EXEC/CHECK services RAW without a data handle; protocol acknowledgement stays unchanged");
#endif
    puts("Toy async GD adapter: low validation, foreground high EXEC/CHECK, nonblocking REQUEST, partial abort fencing, stale tokens and audio mailbox independence pass");
    return 0;
}
