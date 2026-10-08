/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_pilot_gd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define BEGIN UINT32_C(0x8c010000)
#define PARAM (BEGIN+32u)
#define STATUS (BEGIN+64u)
static uint8_t ram[128];
static uint8_t *map(void *context,uint32_t address,uint32_t bytes,int writing) {
    (void)context;(void)writing;
    if(address<BEGIN || address-BEGIN>sizeof(ram) || bytes>sizeof(ram)-(address-BEGIN))
        return NULL;
    return ram+address-BEGIN;
}
static int check(void *context,uint32_t lba,uint32_t count,uint32_t bytes) {
    (void)context;(void)lba;(void)count;(void)bytes;
    assert(!"audio command must not validate a data read");return -1;
}
static int read_data(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *out) {
    (void)context;(void)lba;(void)count;(void)bytes;(void)out;
    assert(!"GD audio acknowledgement must not perform sound or card work");return -1;
}
static uint32_t get(uint32_t address) {
    const uint8_t *p=ram+address-BEGIN;
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void execute(struct kui_retail_gd *s,const struct kui_toy_pilot_snapshot *p) {
    if(kui_toy_pilot_gd_audio_pending(s)) kui_toy_pilot_gd_acknowledge(s,p);
    else assert(kui_retail_gd_dispatch(s,0,0,0,KUI_GD_EXEC)==0);
}
static uint32_t request(struct kui_retail_gd *s,uint32_t command,uint32_t generation) {
    int32_t token=kui_retail_gd_dispatch(s,command,PARAM,0,KUI_GD_REQUEST);
    assert(token>0 && s->pending && s->status==KUI_GD_PROCESSING);
    s->count=generation; /* Positive generation returned by the pure mailbox. */
    return (uint32_t)token;
}
static void terminal(struct kui_retail_gd *s,uint32_t token,int32_t status,uint32_t error) {
    /* A terminal result still owns its handle until the normal CHECK. */
    assert(kui_retail_gd_dispatch(s,KUI_GD_NOP,0,0,KUI_GD_REQUEST)==0);
    assert(kui_retail_gd_dispatch(s,token,STATUS,0,KUI_GD_CHECK)==status);
    assert(get(STATUS)==(error?1u:0u) && get(STATUS+4u)==error);
    assert(!s->pending && !s->command);
}
int main(void) {
    const union kui_retail_slot tracks[]={
        {.track={.start_lba=0,.end_lba=8,.control=4}},
        {.track={.start_lba=16,.end_lba=24,.control=0}},
        {.track={.start_lba=45000,.end_lba=60000,.control=4}}
    };
    const struct kui_gd_ops ops={NULL,map,check,read_data};
    struct kui_retail_gd s;
    assert(kui_retail_gd_init(&s,tracks,3,&ops,BEGIN,BEGIN+sizeof(ram))==0);
    struct kui_toy_pilot_snapshot p={.generation=7,.applied_generation=6,.position_fad=170};
    const uint32_t commands[]={KUI_RETAIL_GD_PLAY,KUI_RETAIL_GD_PLAY2,
        KUI_RETAIL_GD_PAUSE,KUI_RETAIL_GD_RELEASE,KUI_GD_STOP,KUI_GD_COMMAND_INIT};
    for(unsigned i=0;i<sizeof(commands)/sizeof(*commands);i++) {
        uint32_t token=request(&s,commands[i],p.generation);
        assert(kui_retail_gd_dispatch(&s,token,STATUS,0,KUI_GD_CHECK)==KUI_GD_PROCESSING);
        /* Simulate GD polling while the game cannot reach its sound hook:
         * no worker step occurs and applied_generation remains behind. */
        struct kui_toy_pilot_snapshot before=p;
        execute(&s,&p);
        assert(s.status==KUI_GD_COMPLETED && s.position_lba==20);
        assert(!memcmp(&before,&p,sizeof(p)));
        terminal(&s,token,KUI_GD_COMPLETED,KUI_GD_ERROR_NONE);
    }
    /* Old requests, known faults and absent worker state must still fail,
     * and normal CHECK must release their handle for the next request. */
    uint32_t token=request(&s,KUI_RETAIL_GD_PLAY,p.generation-1u);
    execute(&s,&p);terminal(&s,token,KUI_GD_FAILED,KUI_GD_ERROR_CANCELLED);
    p.fault=KUI_TOY_PILOT_FAULT_QUEUE;
    token=request(&s,KUI_RETAIL_GD_PAUSE,p.generation);
    execute(&s,&p);terminal(&s,token,KUI_GD_FAILED,KUI_GD_ERROR_UNAVAILABLE);
    token=request(&s,KUI_GD_STOP,p.generation);
    execute(&s,NULL);terminal(&s,token,KUI_GD_FAILED,KUI_GD_ERROR_UNAVAILABLE);
    p.fault=0;

    /* An aborted request keeps its CANCELLED result; EXEC must not turn it
     * into success. Non-audio commands retain the ordinary GD execution. */
    token=request(&s,KUI_RETAIL_GD_PLAY,p.generation);
    assert(kui_retail_gd_dispatch(&s,token,0,0,KUI_GD_ABORT)==0);
    execute(&s,&p);terminal(&s,token,KUI_GD_FAILED,KUI_GD_ERROR_CANCELLED);
    token=request(&s,KUI_GD_NOP,0);
    assert(!kui_toy_pilot_gd_audio_pending(&s));
    execute(&s,&p);terminal(&s,token,KUI_GD_COMPLETED,KUI_GD_ERROR_NONE);
    puts("Toy GD progress: six audio commands retire without sound service; failures, cancellation and handle ownership preserved");
    return 0;
}
