/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_pilot_lease.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define MANAGER UINT32_C(0x8c10e55c)
#define LOWER UINT32_C(0xa0830040)
#define UPPER UINT32_C(0xa09f4000)
#define HEAD 24u
#define TAIL UINT32_C(0x7e8)
#define WORDS (UINT32_C(0xfb8)/4u)
#define SR_BL UINT32_C(0x10000000)

static uint32_t manager[WORDS],before[WORDS];
static uint32_t sr,reads,writes,sr_writes,bus_calls,canary_address,canary_value;
static enum kui_toy_pilot_bus_result bus_result;

uint32_t kui_toy_pilot_lease_test_read(uint32_t address) {
    assert(address>=MANAGER && address<MANAGER+sizeof(manager) && !(address&3u));
    assert((sr&0xf0u)==0xf0u);
    ++reads;
    return manager[(address-MANAGER)/4u];
}
void kui_toy_pilot_lease_test_write(uint32_t address,uint32_t value) {
    assert(address>=MANAGER && address<MANAGER+sizeof(manager) && !(address&3u));
    assert((sr&0xf0u)==0xf0u && bus_calls==1u && bus_result==KUI_TOY_PILOT_BUS_OK);
    ++writes;
    manager[(address-MANAGER)/4u]=value;
}
uint32_t kui_toy_pilot_lease_test_sr_read(void) { return sr; }
void kui_toy_pilot_lease_test_sr_write(uint32_t value) { ++sr_writes;sr=value; }
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_write(uint32_t address,uint32_t value) {
    assert((sr&0xf0u)==0xf0u && !(sr&SR_BL));
    assert(!writes && !memcmp(before,manager,sizeof(manager)));
    ++bus_calls;
    canary_address=address;canary_value=value;
    return bus_result;
}

static void reset(void) {
    memset(manager,0,sizeof(manager));
    manager[0]=1u;manager[1]=manager[2]=UPPER-LOWER;
    manager[3]=manager[4]=LOWER;manager[5]=UPPER;
    sr=UINT32_C(0x40008003);
    bus_result=KUI_TOY_PILOT_BUS_OK;
}
static enum kui_toy_pilot_bus_result allocate(uint32_t bytes,uint32_t align,uint32_t *address) {
    uint32_t saved=sr,output=address?*address:0u;
    memcpy(before,manager,sizeof(manager));
    reads=writes=sr_writes=bus_calls=canary_address=canary_value=0;
    enum kui_toy_pilot_bus_result result=kui_toy_pilot_lease_allocate(bytes,align,address);
    assert(sr==saved && reads<=806u);
    if(result!=KUI_TOY_PILOT_BUS_OK) {
        assert(!writes && !memcmp(before,manager,sizeof(manager)));
        if(address) assert(*address==output);
    } else {
        assert(writes==6u && bus_calls==1u && sr_writes==2u);
    }
    return result;
}
static void record(uint32_t offset,uint32_t index,uint32_t start,uint32_t reserved) {
    uint32_t *r=&manager[(offset+index*20u)/4u];
    r[0]=start;r[1]=start+reserved-4u;r[2]=reserved;r[3]=1u;
    manager[1]-=reserved;
}

int main(void) {
    uint32_t address=UINT32_C(0xabcdef00);
    reset();
    assert(allocate(128u*1024u,32u,&address)==KUI_TOY_PILOT_BUS_OK);
    assert(address==UINT32_C(0xa09d3fe0));
    assert(manager[TAIL/4u]==address && manager[TAIL/4u+1u]==UPPER-4u);
    assert(manager[TAIL/4u+2u]==UINT32_C(0x20020));
    assert(manager[TAIL/4u+3u]==1u && !manager[TAIL/4u+4u]);
    assert(manager[1]==UPPER-LOWER-UINT32_C(0x20020));
    assert(canary_address==UPPER-4u && canary_value==UINT32_C(0x54544352));
    assert(allocate(128u*1024u,32u,&address)==KUI_TOY_PILOT_BUS_OK);
    assert(address==UINT32_C(0xa09b3fc0) && canary_address==UINT32_C(0xa09d3fdc));

    reset();
    record(HEAD,0u,LOWER,UINT32_C(0x40008));
    record(TAIL,0u,UPPER-64u,64u);
    manager[TAIL/4u+4u]=UINT32_C(0x8c06d700); /* Existing callback is retained. */
    manager[TAIL/4u+5u+4u]=UINT32_C(0x8c06d700); /* Freed callback is allowed. */
    assert(allocate(1u,4u,&address)==KUI_TOY_PILOT_BUS_OK);
    assert(address==UPPER-72u && manager[TAIL/4u+7u]==8u);
    assert(manager[TAIL/4u+4u]==UINT32_C(0x8c06d700) && !manager[TAIL/4u+9u]);

    /* Conservative admission must not wrap or consume another allocation. */
    reset();record(HEAD,0u,LOWER,UPPER-LOWER-128u);
    assert(allocate(128u,32u,&address)==KUI_TOY_PILOT_BUS_BUSY);
    assert(!bus_calls);
    reset();manager[1]--;
    assert(allocate(128u,32u,&address)==KUI_TOY_PILOT_BUS_STATE);
    assert(!bus_calls);
    reset();manager[4]=UPPER;manager[2]=0u;
    assert(allocate(128u,32u,&address)==KUI_TOY_PILOT_BUS_STATE);
    reset();manager[5]=UINT32_C(0xfffffffc);
    assert(allocate(128u,32u,&address)==KUI_TOY_PILOT_BUS_STATE);
    reset();manager[0]=0u;
    assert(allocate(128u,32u,&address)==KUI_TOY_PILOT_BUS_STATE);
    reset();manager[3]=LOWER-4u;
    assert(allocate(128u,32u,&address)==KUI_TOY_PILOT_BUS_STATE);

    /* Full stacks and holes are finite failures, never speculative leases. */
    reset();
    for(uint32_t i=0;i<100u;i++) record(TAIL,i,UPPER-(i+1u)*8u,8u);
    assert(allocate(128u,32u,&address)==KUI_TOY_PILOT_BUS_BUSY);
    assert(!bus_calls);
    reset();record(TAIL,1u,UPPER-8u,8u);
    assert(allocate(128u,32u,&address)==KUI_TOY_PILOT_BUS_STATE);
    reset();record(TAIL,0u,UPPER-12u,8u);
    assert(allocate(128u,32u,&address)==KUI_TOY_PILOT_BUS_STATE);
    reset();record(HEAD,0u,LOWER+4u,8u);
    assert(allocate(128u,32u,&address)==KUI_TOY_PILOT_BUS_STATE);
    reset();record(TAIL,0u,UPPER-8u,8u);manager[TAIL/4u+3u]=2u;
    assert(allocate(128u,32u,&address)==KUI_TOY_PILOT_BUS_STATE);
    reset();record(TAIL,0u,UPPER-8u,8u);manager[TAIL/4u+1u]-=4u;
    assert(allocate(128u,32u,&address)==KUI_TOY_PILOT_BUS_STATE);
    reset();manager[TAIL/4u]=UPPER-8u;
    assert(allocate(128u,32u,&address)==KUI_TOY_PILOT_BUS_STATE);
    reset();record(HEAD,0u,LOWER,UPPER-LOWER-8u);
    record(TAIL,0u,UPPER-12u,12u); /* Overlap even if free arithmetic wraps. */
    assert(allocate(128u,32u,&address)==KUI_TOY_PILOT_BUS_STATE);

    /* Every canary failure, including a stalled final drain, leaves SHRAM
     * metadata unpublished so native heap ownership remains unchanged. */
    const enum kui_toy_pilot_bus_result failures[]={
        KUI_TOY_PILOT_BUS_BUSY,KUI_TOY_PILOT_BUS_TIMEOUT,
        KUI_TOY_PILOT_BUS_STATE,KUI_TOY_PILOT_BUS_PUBLISHED_STALLED
    };
    for(unsigned i=0;i<sizeof(failures)/sizeof(*failures);i++) {
        reset();bus_result=failures[i];
        assert(allocate(128u*1024u,32u,&address)==failures[i]);
        assert(bus_calls==1u && sr_writes==2u);
    }

    reset();sr|=SR_BL;
    assert(allocate(128u,32u,&address)==KUI_TOY_PILOT_BUS_BUSY);
    assert(!reads && !sr_writes && !bus_calls);
    reset();sr=UINT32_C(0x400083f1); /* A nested mask restores every SR bit. */
    assert(allocate(128u,32u,&address)==KUI_TOY_PILOT_BUS_OK);
    reset();
    assert(allocate(0u,32u,&address)==KUI_TOY_PILOT_BUS_ARGUMENT);
    assert(allocate(128u,16u,&address)==KUI_TOY_PILOT_BUS_ARGUMENT);
    assert(allocate(128u,32u,NULL)==KUI_TOY_PILOT_BUS_ARGUMENT);
    assert(allocate(UINT32_MAX,32u,&address)==KUI_TOY_PILOT_BUS_ARGUMENT);
    assert(allocate(UINT32_MAX-3u,32u,&address)==KUI_TOY_PILOT_BUS_ARGUMENT);
    _Alignas(4) uint8_t unaligned[8]={0};
    assert(kui_toy_pilot_lease_allocate(128u,32u,(uint32_t *)(void *)(unaligned+1u))==
           KUI_TOY_PILOT_BUS_ARGUMENT);
    puts("Toy tracked sound lease: bounds, native ownership, canary failure and exact SR checks passed");
    return 0;
}
