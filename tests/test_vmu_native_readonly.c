/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual read-only worker, protocol, overlay, allocator and boundary checker
 * execute over a contiguous host RAM/MMIO bus. No target C path is mocked. */
#define KUI_VMU_NATIVE_PASSIVE_HOST_TEST 1
#define KUI_VMU_NATIVE_READONLY_PROBE 1
#include "../src/loader/vmu_native_passive.c"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "kui/vmu_image.h"
static uint8_t memory[0x1000000u];
static uint32_t mmio[4];
static unsigned used,reads,fail_read,checks;
static uint32_t live_sr=0xf0u,live_sp=0xac70ff00u,bad_guard;
static jmp_buf terminal;static unsigned terminal_phase,terminal_error;
uint32_t kui_vmu_native_passive_resume,kui_vmu_native_passive_consumer;
uint32_t kui_vmu_native_readonly_release_resume;
#define CHECK(x) do {++checks;assert(x);} while(0)
static int mmio_index(uint32_t a) {
    switch(a) {case 0xa05f6c04u:return 0;case 0xa05f6c10u:return 1;
        case 0xa05f6c14u:return 2;case 0xa05f6c18u:return 3;default:return -1;}
}
static void put(uint32_t a,unsigned width,uint32_t v) {
    CHECK(width==1u || width==4u);
    if(ram(a,width,width)) {memcpy(memory+physical(a)-0x0c000000u,&v,width);return;}
    int i=mmio_index(a);CHECK(i>=0 && width==4u);mmio[i]=v;
}
bool kui_vmu_native_passive_test_read(uint32_t a,unsigned width,uint32_t *v) {
    ++reads;if(fail_read && reads==fail_read) return false;
    if(ram(a,width,width)) {*v=0u;memcpy(v,memory+physical(a)-0x0c000000u,width);return true;}
    int i=mmio_index(a);if(i<0 || width!=4u) return false;*v=mmio[i];return true;
}
uint8_t *kui_vmu_native_passive_test_rx(uint32_t address,unsigned bytes) {
    if(!ram(address,bytes,32u)) return NULL;
    return memory+physical(address)-0x0c000000u;
}
uint32_t kui_vmu_native_passive_test_sr(void) {return live_sr;}
uint32_t kui_vmu_native_passive_test_sp(void) {return live_sp;}
uint32_t kui_vmu_native_passive_test_guard(uint32_t address,unsigned i) {
    (void)address;return i+1u==bad_guard?0u:GUARD;
}
void kui_vmu_native_passive_test_terminal(uint32_t phase,uint32_t error) {terminal_phase=phase;terminal_error=error;longjmp(terminal,1);}
static struct kui_vmu_native_passive_config input;
static struct kui_vmu_native_passive_frame frame;
static void runtime_phase(unsigned phase) {
    put(0x0c3010b4u,4u,phase);put(0x0c3010b8u,4u,phase);put(0x0c300fc0u,4u,phase?0u:1u);
    frame.sr=phase?0xf0u:0x60u;frame.pr=0x0c012226u;
}
static void fixture(void) {
    memset(&state,0,sizeof(state));memset(&config,0,sizeof(config));memset(captured,0,sizeof(captured));
    memset(receivers,0,sizeof(receivers));memset(&input,0,sizeof(input));memset(&frame,0,sizeof(frame));
    memset(memory,0,sizeof(memory));memset(mmio,0,sizeof(mmio));
    used=reads=fail_read=0u;live_sr=0xf0u;live_sp=0xac70ff00u;bad_guard=0u;terminal_phase=terminal_error=0u;
    input.version=1u;input.bytes=sizeof(input);input.tx_base=0xac400000u;
    input.driver=(struct kui_vmu_native_discovery){.version=1u,.bytes=sizeof(input.driver),.family=1u,
        .image_bytes=0x200000u,.load_address=0x8c010000u,.requirements=7u,
        .submit=0x8c012000u,.resume=0x8c01200cu,.manager_cell=0x0c3010fcu,.manager_initial=0x0c300000u,
        .lock_cell=0x0c011000u,.completion_call=0x8c012222u,.consumer=0x0c012116u};
    kui_vmu_native_passive_resume=input.driver.resume;kui_vmu_native_passive_consumer=input.driver.consumer;
    struct kui_vmu_native_heap *h=&input.lease.owner;
    *h=(struct kui_vmu_native_heap){.version=1u,.bytes=sizeof(*h),.family=1u,.layout=1u,
        .image_bytes=0x200000u,.load=0x8c010000u,.initializer=0x8c012000u,
        .allocate=0x8c0120e2u,.release=0x8c0121b0u,.dispatch_cell=0x0c5ffffcu,.dispatch_table=0x0c011100u,
        .sentinel=0x0c600000u,.cursor_cell=0x0c600020u,.begin_cell=0x0c600024u,
        .bytes_cell=0x0c600028u,.begin_copy_cell=0x0c60002cu};
    kui_vmu_native_readonly_release_resume=h->release+20u;
    input.lease.version=1u;input.lease.bytes=sizeof(input.lease);input.lease.phase=2u;input.lease.initializations=1u;
    input.lease.begin=0x0c6b0000u;input.lease.heap_bytes=0x80000u;input.lease.lease_bytes=0x30000u;
    input.lease.header=0x0c6fffe0u;input.lease.payload=0x0c700000u;input.lease.units=0x1801u;input.lease.remaining=0x27ffu;
    put(h->begin_cell,4u,input.lease.begin);put(h->begin_copy_cell,4u,input.lease.begin);put(h->bytes_cell,4u,0x80000u);
    put(h->dispatch_cell,4u,h->dispatch_table);put(h->dispatch_table,4u,h->allocate);put(h->dispatch_table+4u,4u,h->release);
    put(h->sentinel+4u,4u,0u);put(h->sentinel,4u,input.lease.begin);put(h->cursor_cell,4u,h->sentinel);
    put(input.lease.begin,4u,h->sentinel);put(input.lease.begin+4u,4u,input.lease.remaining);
    put(input.lease.header+4u,4u,input.lease.units);
    input.runtime=(struct kui_vmu_native_runtime_config){.version=1u,.bytes=sizeof(input.runtime),
        .lease_address=0x8c700000u,.lease_bytes=0x30000u,.lease_header_bytes=32u,.required_lease_bytes=0x10000u,
        .rx_layout=KUI_VMU_NATIVE_RX_EXPLICIT48};
    static const unsigned ids[12]={0,1,2,6,7,8,12,13,14,18,19,20};
    input.layout=(struct kui_vmu_native_layout){.version=1u,.bytes=sizeof(input.layout),
        .initializer=0x8c011ac4u,.initializer_wrapper=0x8c020000u,.initializer_call=0x8c020020u,
        .tx_base=0xac400000u,.rx_layout=KUI_VMU_NATIVE_RX_EXPLICIT48,.rx_base=0xac020040u,
        .gun_flag=0x0c012300u,.timer1_callback=0x8c012304u,.timer2_callback=0xac012308u,
        .inputs=12u,.input_array=0x0c02002cu,.index_array=0x0c020060u};
    for(unsigned i=0;i<12u;i++) {
        input.layout.indices[i]=ids[i];input.layout.input_values[i]=0xac500000u+i*0x820u;
        input.layout.rx_slots[ids[i]]=0xac500000u+i*0x820u;
        input.layout.rx_slots[ids[i]+24u]=0xac500400u+i*0x820u;
        put(input.layout.input_array+i*4u,4u,input.layout.input_values[i]);
        put(0x0c300fe8u+ids[i]*4u,4u,0xac500000u+i*0x820u);
        put(0x0c300fe8u+(ids[i]+24u)*4u,4u,0xac500400u+i*0x820u);
    }
    for(unsigned i=0;i<48u;i++) {
        bool found=false;for(unsigned j=0;j<12u;j++) if(i==ids[j] || i==ids[j]+24u) found=true;
        if(!found) put(0x0c300fe8u+i*4u,4u,0u);
    }
    put(0xa05f6c18u,4u,0u);put(0xa05f6c10u,4u,0u);put(0xa05f6c14u,4u,1u);put(0xa05f6c04u,4u,0x0c400000u);
    put(input.driver.manager_cell,4u,0x0c300000u);put(input.driver.lock_cell,4u,0xac011004u);
    put(0x0c300fc4u,4u,0xac400000u);put(0x0c300fc8u,4u,input.layout.rx_base);
    put(0x0c3010a8u,4u,0xac400000u);put(0x0c3010acu,4u,0xac403000u);put(0x0c3010b0u,4u,0xac400000u);
    put(0x0c3010c4u,4u,0u);put(0x0c3010f4u,4u,0xa05f6c00u);put(0xac011004u,1u,0x80u);
    put(input.layout.gun_flag,4u,0u);put(input.layout.timer1_callback,4u,0u);put(input.layout.timer2_callback,4u,0u);
    put(0x0c400000u,4u,0x80000001u);put(0x0c400004u,4u,0x0c500000u);
    put(0x0c400008u,4u,0x01002009u);put(0x0c40000cu,4u,0x01000000u);put(0x0c500000u,4u,0x03002008u);
    frame.r14=input.driver.manager_cell;frame.r12=1u;runtime_phase(0u);
}
static uint32_t packet_bank;
static uint32_t rx_at(unsigned unit) {
    return physical(input.layout.rx_slots[packet_bank*24u+unit]);
}
static uint8_t *rx_bytes(unsigned unit) {
    return kui_vmu_native_passive_test_rx(rx_at(unit),1024u);
}
static uint32_t value_at(uint32_t address) {
    uint32_t result;CHECK(kui_vmu_native_passive_test_read(address,4u,&result));return result;
}
static void stage_batch(unsigned command,unsigned block) {
    packet_bank=state.completed&1u;
    uint32_t source=physical(input.tx_base)+packet_bank*0x3000u;
    put(0xa05f6c04u,4u,source);put(0x0c3010b0u,4u,source|0xa0000000u);
    put(0x0c3010b4u,4u,packet_bank);put(0x0c3010b8u,4u,packet_bank);
    put(0x0c300fc0u,4u,1u);frame.sr=0x60000161u;frame.pr=0x0c012226u;
    memset(memory+source-0x0c000000u,0,0x3000u);
    memset(rx_bytes(0),0xa5,1024u);memset(rx_bytes(2),0xa5,1024u);memset(rx_bytes(6),0xa5,1024u);
    /* Main A controller polling is kept physical. */
    put(source,4u,1u);put(source+4u,4u,rx_at(0));put(source+8u,4u,0x01002009u);put(source+12u,4u,0x01000000u);
    source+=16u;
    if(command) {
        unsigned words=command==1u || command==2u?0u:command==12u?34u:2u;
        put(source,4u,words);put(source+4u,4u,rx_at(2));
        put(source+8u,4u,(words<<24)|0x00000200u|command);
        if(words) put(source+12u,4u,0x02000000u);
        if(words>1u) put(source+16u,4u,block<<24);
        source+=12u+4u*words;
    }
    /* Unrelated main B response must remain byte-identical. */
    put(source,4u,0x80010000u);put(source+4u,4u,rx_at(6));put(source+8u,4u,0x00406001u);
}
static void physical_complete(void) {
    put(0x0c3010b4u,4u,packet_bank^1u);put(0x0c3010b8u,4u,packet_bank^1u);put(0x0c300fc0u,4u,0u);
    frame.sr=0x600001f1u;
    put(rx_at(0),4u,0x03200008u);put(rx_at(0)+4u,4u,0x01000000u);
    put(rx_at(2),4u,UINT32_MAX);put(rx_at(6),4u,0x1c604005u);
}
static void arm(void) {CHECK(kui_vmu_native_passive_arm(&input,NULL)==0);}
static void submit(void) {
    if(setjmp(terminal)==0) {kui_vmu_native_passive_submit(&frame);CHECK(state.pending && state.submitted==state.completed+1u);return;}
    fprintf(stderr,"Unexpected readonly submission refusal %u: %08x/%08x/%08x\n",terminal_error,state.overlay_result,state.fault_at,state.runtime_result);
    CHECK(false);
}
static void complete_return(void) {
    if(setjmp(terminal)==0) {kui_vmu_native_passive_complete(&frame);CHECK(!state.pending && state.submitted==state.completed);return;}
    fprintf(stderr,"Unexpected readonly completion refusal %u: %08x/%08x/%08x\n",terminal_error,state.overlay_result,state.fault_at,state.runtime_result);
    CHECK(false);
}
static void reject_submit(void) {
    if(setjmp(terminal)==0) {kui_vmu_native_passive_submit(&frame);CHECK(false);}
    CHECK(terminal_phase==14u && terminal_error && !state.pending && !state.submitted && !state.proof);
}
static void reject_complete(void) {
    if(setjmp(terminal)==0) {kui_vmu_native_passive_complete(&frame);CHECK(false);}
    CHECK(terminal_phase==14u && terminal_error && state.pending && !state.completed && !state.proof);
}
int main(void) {
    uint8_t saved_parent[1024],saved_other[1024],saved_target[1024],expected_block[512];
    fixture();arm();stage_batch(0u,0u);submit();physical_complete();
    memcpy(saved_parent,rx_bytes(0),sizeof(saved_parent));memcpy(saved_other,rx_bytes(6),sizeof(saved_other));
    complete_return();saved_parent[2]|=2u;
    CHECK(!memcmp(saved_parent,rx_bytes(0),sizeof(saved_parent)));
    CHECK(!memcmp(saved_other,rx_bytes(6),sizeof(saved_other)));
    CHECK(overlay.stats.advertisements==1u && !overlay.stats.replies && !card.requests && !card.commit_attempts);
    /* Alternate real SDK banks, advertise A2, then return DEVINFO and media. */
    stage_batch(1u,0u);CHECK(packet_bank==1u);submit();physical_complete();complete_return();
    CHECK(value_at(rx_at(2))==0x1c020005u && overlay.stats.devinfo==1u && card.requests==1u);
    stage_batch(10u,0u);CHECK(packet_bank==0u);submit();physical_complete();complete_return();
    CHECK(value_at(rx_at(2))==0x07020008u && overlay.stats.media==1u && card.requests==2u);
    stage_batch(11u,255u);submit();physical_complete();
    memcpy(saved_other,rx_bytes(6),sizeof(saved_other));
    if(setjmp(terminal)==0) {kui_vmu_native_passive_complete(&frame);CHECK(false);}
    CHECK(terminal_phase==13u && !terminal_error && state.proof && state.completed==4u && !state.pending);
    CHECK(value_at(rx_at(2))==0x82020008u && overlay.stats.reads==1u && card.reads==1u && card.requests==3u);
    CHECK(!kui_vmu_image_blank_block(255u,expected_block));
    CHECK(!memcmp(rx_bytes(2)+12u,expected_block,sizeof(expected_block)));
    CHECK(!memcmp(saved_other,rx_bytes(6),sizeof(saved_other)) && !card.commit_attempts && !card.faulted);

    /* A physical A2 attachment or reply is refused before any guest mutation. */
    for(unsigned conflict=0;conflict<2u;conflict++) {
        fixture();arm();stage_batch(1u,0u);submit();physical_complete();
        if(conflict) put(rx_at(2),4u,0x1c020005u);else rx_bytes(0)[2]|=2u;
        memcpy(saved_parent,rx_bytes(0),sizeof(saved_parent));memcpy(saved_other,rx_bytes(6),sizeof(saved_other));
        memcpy(saved_target,rx_bytes(2),sizeof(saved_target));reject_complete();
        CHECK(state.error==23u && !card.requests && !overlay.stats.replies && !overlay.stats.advertisements);
        CHECK(!memcmp(saved_parent,rx_bytes(0),sizeof(saved_parent)) && !memcmp(saved_target,rx_bytes(2),sizeof(saved_target)));
        CHECK(!memcmp(saved_other,rx_bytes(6),sizeof(saved_other)));
    }
    const unsigned unsupported[]={3u,4u,9u,12u,13u};
    for(unsigned i=0;i<sizeof(unsupported)/sizeof(unsupported[0]);i++) {
        fixture();arm();stage_batch(unsupported[i],0u);memcpy(saved_target,rx_bytes(2),sizeof(saved_target));reject_submit();
        CHECK(state.error==21u && !card.requests && !card.commit_attempts && !overlay.stats.backend_calls);
        CHECK(!memcmp(saved_target,rx_bytes(2),sizeof(saved_target)));
    }
    /* A TX, layout, lease or consumer-cell change after the physical transfer
     * cannot publish a virtual response or return to an unproved consumer. */
    for(unsigned corruption=0;corruption<4u;corruption++) {
        fixture();arm();stage_batch(1u,0u);submit();physical_complete();
        if(corruption==0u) put(physical(input.tx_base)+12u,4u,0x02000000u);
        else if(corruption==1u) put(input.layout.input_array,4u,0xac510000u);
        else if(corruption==2u) put(input.lease.header+4u,4u,input.lease.units+1u);
        else kui_vmu_native_passive_consumer+=2u;
        memcpy(saved_parent,rx_bytes(0),sizeof(saved_parent));memcpy(saved_target,rx_bytes(2),sizeof(saved_target));reject_complete();
        CHECK(!memcmp(saved_parent,rx_bytes(0),sizeof(saved_parent)) && !memcmp(saved_target,rx_bytes(2),sizeof(saved_target)) && !card.requests);
    }
    fixture();arm();stage_batch(0u,0u);submit();
    if(setjmp(terminal)==0) {kui_vmu_native_passive_submit(&frame);CHECK(false);}
    CHECK(terminal_phase==14u && state.error==1u && !state.proof);
    fixture();arm();stage_batch(0u,0u);state.submitted=state.completed=READONLY_PAIR_LIMIT;
    if(setjmp(terminal)==0) {kui_vmu_native_passive_submit(&frame);CHECK(false);}
    CHECK(terminal_phase==14u && state.error==20u && state.submitted==READONLY_PAIR_LIMIT && !state.pending && !state.proof);
    /* Free-NULL and a distinct allocated chunk remain callable, even with
     * an in-flight DMA. The worker's own lease never reaches native free. */
    fixture();arm();frame.r4=0u;kui_vmu_native_readonly_release_check(&frame);
    put(input.lease.begin+4u,4u,input.lease.remaining-2u);
    uint32_t other_header=input.lease.header-64u;
    put(other_header+4u,4u,2u);frame.r4=other_header+32u;
    kui_vmu_native_readonly_release_check(&frame);
    stage_batch(1u,0u);submit();frame.r4=other_header+32u;
    kui_vmu_native_readonly_release_check(&frame);CHECK(state.pending && state.submitted==1u && !state.completed);
    frame.r4=input.lease.payload;
    if(setjmp(terminal)==0) {kui_vmu_native_readonly_release_check(&frame);CHECK(false);}
    CHECK(terminal_phase==14u && state.error==0x30u && !state.proof);
    CHECK(value_at(input.lease.header+4u)==input.lease.units && !card.commit_attempts);
    printf("Native read-only bridge: %u checks passed; alternating banks, physical conflicts, blank-card reads, no writes\n",checks);
    return 0;
}
