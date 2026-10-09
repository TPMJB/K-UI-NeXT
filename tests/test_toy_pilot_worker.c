/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_pilot.h"
#include "kui/toy_pilot_bus.h"
#include "kui/toy_pilot_lease.h"
#include "kui/retail_image.h"
#include <assert.h>
#include <stdio.h>

extern void kui_toy_pilot_worker_test_prepare(uint32_t,uint32_t,bool);
extern void kui_toy_pilot_worker_test_manifest(const struct kui_retail_manifest *);
extern void kui_toy_pilot_worker_step(void);
extern uint32_t kui_toy_pilot_request(uint32_t,uint32_t,uint32_t,uint32_t);
extern const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void);
/* Uncalled initialization/link boundaries; no actual mapped console RAM. */
uint8_t __toy_pilot_stack_bottom[64],__toy_pilot_stack_top[64],__toy_pilot_worker_end[32];
volatile uint32_t kui_toy_pilot_native_diagnostics[8];
static uint32_t sr=0x40000001u,now,bus_calls,leases,packets,reports,entry_sr;
static enum kui_toy_pilot_bus_result read_result,lease_result,packet_result;
static const struct kui_retail_manifest admitted={.track_count=2u,
    .slots={[1]={.track={.start_lba=50u,.end_lba=60u,.extent_count=1u}}}};
uint32_t kui_toy_pilot_worker_test_sr(void) { return sr; }
void kui_toy_pilot_worker_test_set_sr(uint32_t value) { sr=value; }
void kui_toy_pilot_worker_test_terminal(void) {
    assert(sr==entry_sr);++reports;
}
uint32_t kui_toy_pilot_worker_test_read(uint32_t address,unsigned width) {
    (void)width;
    switch(address) {
    case 0x8c0a7318u:case 0x8c0a8940u:case 0x8c0af74cu:return 1;
    case 0x8c112b08u:return 0xa080b200u;
    case 0xffd80004u:return 1;
    case 0xffd80008u:return UINT32_MAX;
    case 0xffd8000cu:return ~(now++);
    case 0xffd80010u:return 2;
    case 0xffc00000u:return 0xe0au;
    case 0xff00001cu:return 0x101u;
    default:return 0;
    }
}
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_read(uint32_t address,uint32_t *value) {
    assert((sr&0xf0u)==0xf0u);++bus_calls;
    if(read_result!=KUI_TOY_PILOT_BUS_OK) return read_result;
    switch(address) {
    case 0xa0801464u:*value=0x800000u;break;
    case 0xa08000e0u:*value=0x1468u;break;
    case 0xa08000e8u:*value=0x14f0u;break;
    case 0xa08000ecu:*value=0x30040u;break;
    case 0xa080b200u:*value=0xff9du;break; /* submitted stop not consumed yet */
    case 0xa080b204u:*value=0xc0000000u;break;
    default:*value=0;break;
    }
    return KUI_TOY_PILOT_BUS_OK;
}
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_publish(const uint32_t packet[4],uint32_t *slot) {
    assert((sr&0xf0u)==0xf0u);++packets;
    assert(packet[0]==0xff9du && packet[1]==0xc0000000u && !packet[2] && !packet[3]);
    if(packet_result==KUI_TOY_PILOT_BUS_OK || packet_result==KUI_TOY_PILOT_BUS_PUBLISHED_STALLED)
        *slot=0xa080b200u;
    return packet_result;
}
enum kui_toy_pilot_bus_result kui_toy_pilot_lease_allocate(uint32_t bytes,uint32_t alignment,uint32_t *address) {
    assert((sr&0xf0u)==0xf0u);++leases;
    assert(bytes==131072u && alignment==32u);
    if(lease_result==KUI_TOY_PILOT_BUS_OK) *address=0xa09d3fe0u;
    return lease_result;
}
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_copy(uint32_t a,const void *p,uint32_t b) {
    (void)a;(void)p;(void)b;assert(!"No raw fill while stop is pending");return KUI_TOY_PILOT_BUS_STATE;
}
static void prepare(uint32_t state,uint32_t command,bool pending) {
    kui_toy_pilot_worker_test_prepare(state,0,false);
    kui_toy_pilot_worker_test_manifest(&admitted);
    now=bus_calls=leases=packets=reports=0;sr=0x40000001u;
    read_result=lease_result=packet_result=KUI_TOY_PILOT_BUS_OK;
    if(pending) assert(kui_toy_pilot_request(command,2u,2u,0u)==2u);
}
static void step(void) {
    uint32_t before=entry_sr=sr;kui_toy_pilot_worker_step();assert(sr==before);
}
int main(void) {
    const uint32_t idle[]={KUI_TOY_PILOT_STOPPED,KUI_TOY_PILOT_PAUSED,KUI_TOY_PILOT_EOF};
    for(unsigned i=0;i<3;i++) {
        prepare(idle[i],0,false);
        for(unsigned n=0;n<10000u;n++) step();
        assert(!bus_calls && !leases && !packets && !now);
        assert(!kui_toy_pilot_snapshot()->fault);
    }
    const uint32_t quiet_commands[]={33u,KUI_TOY_PILOT_RESET,22u};
    for(unsigned i=0;i<3;i++) {
        prepare(KUI_TOY_PILOT_STOPPED,quiet_commands[i],true);step();
        assert(!bus_calls && !leases && !packets);
        assert(kui_toy_pilot_snapshot()->applied_generation==2u);
        assert(kui_toy_pilot_snapshot()->state==(quiet_commands[i]==22u?KUI_TOY_PILOT_PAUSED:KUI_TOY_PILOT_STOPPED));
    }
    prepare(KUI_TOY_PILOT_STOPPED,20u,true);read_result=KUI_TOY_PILOT_BUS_BUSY;step();
    assert(bus_calls==1 && !leases && !packets && !kui_toy_pilot_snapshot()->fault);
    now+=781251u;step();assert(kui_toy_pilot_snapshot()->fault==KUI_TOY_PILOT_FAULT_BUS);
    assert(reports==1u);
    uint32_t count=bus_calls;step();assert(bus_calls==count && reports==1u); /* fault cannot retry/report twice */
    prepare(KUI_TOY_PILOT_STOPPED,20u,true);read_result=KUI_TOY_PILOT_BUS_TIMEOUT;step();
    assert(bus_calls==1 && !leases && !packets && kui_toy_pilot_snapshot()->fault==KUI_TOY_PILOT_FAULT_BUS);
    assert(reports==1u);
    prepare(KUI_TOY_PILOT_STOPPED,20u,true);lease_result=KUI_TOY_PILOT_BUS_TIMEOUT;step();
    assert(bus_calls==4 && leases==1 && !packets && !kui_toy_pilot_snapshot()->sound_address);
    assert(kui_toy_pilot_snapshot()->fault==KUI_TOY_PILOT_FAULT_BUS);
    prepare(KUI_TOY_PILOT_STOPPED,20u,true);packet_result=KUI_TOY_PILOT_BUS_BUSY;step();
    assert(leases==1 && packets==1 && !kui_toy_pilot_snapshot()->fault);
    packet_result=KUI_TOY_PILOT_BUS_OK;step();
    assert(leases==1 && packets==2 && !kui_toy_pilot_snapshot()->fault);
    assert(kui_toy_pilot_snapshot()->state==KUI_TOY_PILOT_PREFILL);
    step();assert(packets==2); /* accepted stop is waited on, never republished */
    prepare(KUI_TOY_PILOT_STOPPED,20u,true);packet_result=KUI_TOY_PILOT_BUS_PUBLISHED_STALLED;step();
    assert(packets==1 && kui_toy_pilot_snapshot()->fault==KUI_TOY_PILOT_FAULT_BUS);
    step();assert(packets==1);
    prepare(KUI_TOY_PILOT_STOPPED,20u,true);sr|=0x10000000u;step();
    assert(!bus_calls && !leases && !packets);
    puts("Toy actual worker: idle has zero bus/heap activity; busy retries, bounded failure and committed-packet fault preserve progress");
    return 0;
}
