/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_pilot_gd_status.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define BEGIN UINT32_C(0x8c010000)
#define PARAM (BEGIN+32u)
#define OUT (BEGIN+64u)
static uint8_t ram[512];
static unsigned maps,reads;
static uint8_t *map(void *context,uint32_t address,uint32_t bytes,int writing) {
    (void)context;(void)writing;++maps;
    if(address<BEGIN || address-BEGIN>sizeof(ram) || bytes>sizeof(ram)-(address-BEGIN)) return NULL;
    return ram+address-BEGIN;
}
static int check(void *context,uint32_t lba,uint32_t count,uint32_t bytes) {
    (void)context;(void)lba;(void)count;(void)bytes;
    assert(!"scalar audio projection must not validate storage");return -1;
}
static int read_data(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *out) {
    (void)context;(void)lba;(void)count;(void)bytes;(void)out;++reads;
    assert(!"scalar audio projection must not read storage");return -1;
}
static uint32_t get(uint32_t address) {
    const uint8_t *p=ram+address-BEGIN;
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void put(uint32_t address,uint32_t value) {
    for(unsigned i=0;i<4;i++) ram[address-BEGIN+i]=(uint8_t)(value>>(8u*i));
}
static uint32_t project(struct kui_retail_gd *s,const struct kui_toy_pilot_snapshot *p) {
    unsigned before=maps;
    uint32_t result=kui_toy_pilot_gd_project(s,p);
    assert(maps==before && !reads); /* The low path touches scalar RAM only. */
    return result;
}
static uint32_t drive(struct kui_retail_gd *s) {
    assert(kui_retail_gd_dispatch(s,OUT,0,0,KUI_GD_DRIVE)==0);
    return get(OUT);
}
static void query(struct kui_retail_gd *s,uint32_t command) {
    uint32_t token=(uint32_t)kui_retail_gd_dispatch(s,command,PARAM,0,KUI_GD_REQUEST);
    assert(token && s->pending);
    assert(kui_retail_gd_dispatch(s,0,0,0,KUI_GD_EXEC)==0);
    assert(kui_retail_gd_dispatch(s,token,BEGIN+256u,0,KUI_GD_CHECK)==KUI_GD_COMPLETED);
    assert(!s->command && !s->pending);
}
int main(void) {
    const union kui_retail_slot tracks[]={
        {.track={.start_lba=0,.end_lba=8,.control=4}},
        {.track={.start_lba=16,.end_lba=24,.control=0}},
        {.track={.start_lba=45000,.end_lba=60000,.control=4}}
    };
    const struct kui_gd_ops ops={NULL,map,check,read_data};
    struct kui_retail_gd s;
    assert(!kui_retail_gd_init(&s,tracks,3,&ops,BEGIN,BEGIN+sizeof(ram)));
    struct kui_toy_pilot_snapshot p={.state=KUI_TOY_PILOT_PLAYING,.command=20,
        .generation=7,.applied_generation=7,.track=2,.position_fad=170};
    const uint32_t active[]={KUI_TOY_PILOT_PREFILL,KUI_TOY_PILOT_START_WAIT,KUI_TOY_PILOT_PLAYING};
    for(unsigned i=0;i<sizeof(active)/sizeof(*active);i++) {
        p.state=active[i];assert(project(&s,&p)==0x11u);
        /* The title's verified predicate discards its desired/current music
         * IDs when DRIVE returns 1. Every finite handoff must avoid that. */
        uint32_t desired=2,current=2;
        if(drive(&s)==1u) desired=current=UINT32_MAX;
        assert(desired==2u && current==2u && drive(&s)==3u);
        assert(s.position_lba==20u);
    }
    /* A new accepted PLAY has not run the frame worker: it is already a
     * logical play, with its requested track's first FAD, not the old cursor. */
    p.state=KUI_TOY_PILOT_EOF;p.applied_generation=6;p.parameters[0]=2;
    assert(project(&s,&p)==0x11u && drive(&s)==3u && s.position_lba==16u);
    p.command=22;assert(project(&s,&p)==0x12u && drive(&s)==1u && s.position_lba==20u);
    p.command=23;assert(project(&s,&p)==0x11u && drive(&s)==3u);
    p.applied_generation=7;p.command=20;p.state=KUI_TOY_PILOT_PAUSED;
    assert(project(&s,&p)==0x12u && drive(&s)==1u);
    p.state=KUI_TOY_PILOT_EOF;p.position_fad=174;
    assert(project(&s,&p)==0x13u && drive(&s)==1u && s.position_lba==23u);
    p.command=33;p.state=KUI_TOY_PILOT_STOPPED;
    assert(project(&s,&p)==0x13u && drive(&s)==2u);
    p.command=KUI_TOY_PILOT_RESET;
    assert(project(&s,&p)==0x15u && drive(&s)==1u);
    p.command=KUI_RETAIL_GD_RELEASE;p.track=0;p.applied_generation=6;
    s.drive_status=3u;
    assert(project(&s,&p)==0x15u && drive(&s)==1u);
    p.applied_generation=7;
    assert(project(&s,&p)==0x15u && drive(&s)==1u);
    p.track=2;
    p.command=20;p.state=KUI_TOY_PILOT_OFF;
    assert(project(&s,&p)==0x13u && drive(&s)==2u);
    p.state=KUI_TOY_PILOT_FAULT;p.fault=KUI_TOY_PILOT_FAULT_QUEUE;
    assert(project(&s,&p)==0x14u && drive(&s)==9u);
    p.fault=0;p.state=KUI_TOY_PILOT_PLAYING;p.position_fad=170;
    assert(project(&s,&p)==0x11u && drive(&s)==3u);

    /* REQ_STAT and GETSCD derive track and absolute/relative positions from
     * the same refreshed scalar state. The adapter replaces only GETSCD's
     * status byte after the ordinary format encoder finishes. */
    for(unsigned i=0;i<4;i++) put(PARAM+4u*i,OUT+4u*i);
    query(&s,KUI_RETAIL_GD_REQ_STAT);
    assert(get(OUT)==3u && get(OUT+4u)==2u && get(OUT+8u)==0x100000aau && get(OUT+12u)==1u);
    put(PARAM,1);put(PARAM+4u,14);put(PARAM+8u,OUT);
    query(&s,KUI_RETAIL_GD_GETSCD);
    ram[OUT-BEGIN+1u]=(uint8_t)project(&s,&p);
    assert(ram[OUT-BEGIN+1u]==0x11u && ram[OUT-BEGIN+3u]==14u);
    assert(ram[OUT-BEGIN+5u]==2u && ram[OUT-BEGIN+6u]==1u);
    assert(ram[OUT-BEGIN+7u]==0u && ram[OUT-BEGIN+8u]==0u && ram[OUT-BEGIN+9u]==4u);
    assert(ram[OUT-BEGIN+11u]==0u && ram[OUT-BEGIN+12u]==0u && ram[OUT-BEGIN+13u]==170u);
    /* Refused/invalid snapshots cannot index beyond the immutable map or
     * publish a data-track position. Absence preserves ordinary drive state. */
    p.track=UINT32_MAX;s.position_lba=22;project(&s,&p);assert(s.position_lba==22u);
    p.track=3;project(&s,&p);assert(s.position_lba==22u);
    assert(project(&s,NULL)==0x15u && s.position_lba==22u && drive(&s)==3u);
    puts("Toy GD audio status: title predicate, logical handoff, pending commands, position and scalar responses pass");
    return 0;
}
