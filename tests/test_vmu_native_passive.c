/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthetic RAM/MMIO bus runs the actual passive worker, lease validator,
 * initializer binder and runtime boundary checker. No target code is mocked. */
#define KUI_VMU_NATIVE_PASSIVE_HOST_TEST 1
#include "../src/loader/vmu_native_passive.c"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
struct word {uint32_t address,value;unsigned width;};
static struct word memory[4096];static unsigned used,reads,fail_read,checks;
static uint32_t live_sr=0xf0u,live_sp=0xac70ff00u,bad_guard;
static jmp_buf terminal;static unsigned terminal_phase,terminal_error;
uint32_t kui_vmu_native_passive_resume,kui_vmu_native_passive_consumer;
#define CHECK(x) do {++checks;assert(x);} while(0)
static uint32_t canonical(uint32_t a) {return ram(a,1u,1u)?physical(a):a;}
static void put(uint32_t a,unsigned width,uint32_t v) {
    a=canonical(a);
    for(unsigned i=0;i<used;i++) if(memory[i].address==a) {assert(memory[i].width==width);memory[i].value=v;return;}
    assert(used<4096u);memory[used++]=(struct word){a,v,width};
}
bool kui_vmu_native_passive_test_read(uint32_t a,unsigned width,uint32_t *v) {
    ++reads;if(fail_read && reads==fail_read) return false;a=canonical(a);
    for(unsigned i=0;i<used;i++) if(memory[i].address==a && memory[i].width==width) {*v=memory[i].value;return true;}
    return false;
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
static void arm(void) {CHECK(kui_vmu_native_passive_arm(&input,NULL)==0);}
static void submit(void) {
    if(setjmp(terminal)==0) {kui_vmu_native_passive_submit(&frame);CHECK(state.pending && state.submitted==1u);return;}
    fprintf(stderr,"Unexpected submission refusal %u at %08x value %08x runtime %d\n",terminal_error,state.fault_at,state.fault_value,(int)state.runtime_result);
    CHECK(false);
}
static void complete(void) {
    runtime_phase(1u);
    if(setjmp(terminal)==0) {kui_vmu_native_passive_complete(&frame);CHECK(false);}
    CHECK(terminal_phase==11u && !terminal_error && state.completed==1u && state.pending==0u);
}
static void rejected_submit(void) {
    if(setjmp(terminal)==0) {kui_vmu_native_passive_submit(&frame);CHECK(false);}
    CHECK(terminal_phase==12u && terminal_error && !state.submitted);
}
static void rejected_complete(void) {
    if(setjmp(terminal)==0) {kui_vmu_native_passive_complete(&frame);CHECK(false);}
    CHECK(terminal_phase==12u && terminal_error && !state.completed);
}
int main(void) {
    fixture();arm();submit();CHECK(state.bytes==16u && state.descriptors==1u && state.rx_slots==24u);complete();
    fixture();arm();memset(&input,0xa5,sizeof(input));submit();complete();
    const uint32_t aliases[3]={0u,0x80000000u,0xa0000000u};
    for(unsigned i=0;i<3u;i++) {fixture();arm();frame.r14=physical(frame.r14)|aliases[i];submit();complete();}
    fixture();arm();submit();
    if(setjmp(terminal)==0) {kui_vmu_native_passive_submit(&frame);CHECK(false);}
    CHECK(terminal_phase==12u && terminal_error && state.submitted==1u && !state.completed);
    /* A valid padding word followed by a last descriptor is admitted. */
    fixture();arm();put(0x0c400000u,4u,0x00000700u);put(0x0c400004u,4u,0x80000001u);
    put(0x0c400008u,4u,0x0c500000u);put(0x0c40000cu,4u,0x01002009u);put(0x0c400010u,4u,0x01000000u);
    submit();CHECK(state.nops==1u && state.bytes==20u);complete();
    /* Each original TX word is compared, including request payload. */
    for(unsigned word=0;word<4u;word++) {
        fixture();arm();submit();runtime_phase(1u);
        uint32_t changed;CHECK(kui_vmu_native_passive_test_read(0x0c400000u+4u*word,4u,&changed));
        put(0x0c400000u+4u*word,4u,changed^1u);rejected_complete();
    }
    fixture();CHECK(kui_vmu_native_passive_arm(NULL,NULL)<0);
    fixture();input.version++;CHECK(kui_vmu_native_passive_arm(&input,NULL)<0);
    fixture();input.lease.payload+=32u;CHECK(kui_vmu_native_passive_arm(&input,NULL)<0);
    fixture();input.tx_base=0x8c400000u;CHECK(kui_vmu_native_passive_arm(&input,NULL)<0);
    fixture();kui_vmu_native_passive_resume+=2u;CHECK(kui_vmu_native_passive_arm(&input,NULL)<0);
    fixture();bad_guard=1u;CHECK(kui_vmu_native_passive_arm(&input,NULL)<0);
    fixture();arm();CHECK(kui_vmu_native_passive_arm(&input,NULL)<0);
    fixture();arm();frame.r14+=4u;rejected_submit();
    fixture();arm();frame.r12=0u;rejected_submit();
    fixture();arm();kui_vmu_native_passive_resume+=2u;rejected_submit();
    fixture();arm();live_sr=0x60u;rejected_submit();
    fixture();arm();bad_guard=16u;rejected_submit();
    fixture();arm();put(0xa05f6c18u,4u,1u);rejected_submit();
    fixture();arm();put(0x0c400004u,4u,0x0c6fffe0u);rejected_submit();
    fixture();arm();put(0x0c400004u,4u,0x0c500020u);rejected_submit();
    fixture();arm();put(0x0c400008u,4u,0x01000109u);rejected_submit();
    fixture();arm();put(0x0c400000u,4u,0x80000101u);rejected_submit();
    fixture();arm();put(0x0c400000u,4u,0x00000001u);rejected_submit();
    fixture();arm();put(input.layout.input_array,4u,0xac510000u);rejected_submit();
    CHECK(state.fault_at==input.layout.input_array && state.fault_value==0xac510000u &&
        state.fault_expect==input.layout.input_values[0] &&
        state.runtime_result==(0x10000u|KUI_VMU_LAYOUT_BIND_INPUT_VALUE));
    fixture();arm();put(0x0c6fffe4u,4u,0x1802u);rejected_submit();
    fixture();arm();runtime_phase(1u);rejected_complete();
    fixture();arm();submit();runtime_phase(1u);frame.pr+=2u;rejected_complete();
    fixture();arm();submit();runtime_phase(1u);put(0x0c40000cu,4u,0x02000000u);rejected_complete();
    fixture();arm();submit();runtime_phase(1u);put(0xa05f6c04u,4u,0x0c403000u);rejected_complete();
    fixture();arm();submit();runtime_phase(1u);put(0x0c3010b4u,4u,0u);rejected_complete();
    fixture();arm();submit();runtime_phase(1u);put(0x0c6fffe4u,4u,0x1802u);rejected_complete();
    fixture();arm();submit();runtime_phase(1u);put(input.layout.input_array,4u,0xac510000u);rejected_complete();
    fixture();arm();submit();runtime_phase(1u);put(0x0c300fe8u,4u,0xac510000u);rejected_complete();
    fixture();arm();reads=0u;submit();unsigned successful_reads=reads;
    for(volatile unsigned i=1;i<=successful_reads;i++) {
        fixture();arm();reads=0u;fail_read=i;
        if(setjmp(terminal)==0) {kui_vmu_native_passive_submit(&frame);continue;}
        CHECK(terminal_error && !state.submitted);
    }
    printf("Native passive worker: %u checks passed; exact TX snapshot, physical pair, no writes\n",checks);
    return 0;
}
