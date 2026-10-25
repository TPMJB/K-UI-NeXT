/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual persistent worker, protocol, overlay, allocator and boundary checker
 * execute over a contiguous host RAM/MMIO bus. No target C path is mocked. */
#define KUI_VMU_NATIVE_PASSIVE_HOST_TEST 1
#define KUI_VMU_NATIVE_READONLY_PROBE 1
#define KUI_VMU_NATIVE_PERSISTENT_PROBE 1
#include "../src/loader/vmu_native_passive.c"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include "kui/vmu_image.h"
#include "kui/hash.h"
#include "kui/retail_image.h"
#define card model_card
#include "vmu_sd_model.h"
#undef card
static uint8_t memory[0x1000000u];
static uint32_t mmio[4];
static unsigned used,bus_reads,fail_read,checks;
static bool refuse_rx_map;
static unsigned measured_submit_reads,measured_complete_reads;
static struct kui_vmu_native_profile_sample test_clock;
static uint32_t test_ccr;
static unsigned clock_reads;
static unsigned corrupt_before_loan,corrupt_after_loan;
static unsigned corrupt_on_restore;
static uint32_t live_sr=0xf0u,live_sp=SELF_ADDRESS+WORKER_SPAN-0x100u,bad_guard;
static jmp_buf terminal;static unsigned terminal_phase,terminal_error;
uint32_t kui_vmu_native_passive_resume,kui_vmu_native_passive_consumer;
uint32_t kui_vmu_native_readonly_release_resume;
const struct kui_vmu_native_passive_exports kui_vmu_native_worker_exports={
    .magic=KUI_VMU_NATIVE_PASSIVE_MAGIC,.version=1u,.bytes=sizeof(struct kui_vmu_native_passive_exports),
    .submit_hook=SELF_ADDRESS+0x100u,.complete_hook=SELF_ADDRESS+0x200u,.release_hook=SELF_ADDRESS+0x300u,
    .terminal=SELF_ADDRESS+0x400u,.sd_transaction=SELF_ADDRESS+0x500u};
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
    /* The first ownership read after a request has staged an SD operation
     * models a changed guest witness immediately before external acquisition. */
    if(corrupt_before_loan && durable_card.inflight && !durable_card.invoked) {
        unsigned which=corrupt_before_loan;corrupt_before_loan=0u;
        uint32_t target=which==1u?config.lease.header+4u:
            which==2u?config.layout.input_array:which==3u?state.source+12u:config.driver.submit;
        put(target,4u,0u);
    }
    ++bus_reads;if(fail_read && bus_reads==fail_read) return false;
    if(ram(a,width,width)) {*v=0u;memcpy(v,memory+physical(a)-0x0c000000u,width);return true;}
    int i=mmio_index(a);if(i<0 || width!=4u) return false;*v=mmio[i];return true;
}
uint8_t *kui_vmu_native_passive_test_rx(uint32_t address,unsigned bytes) {
    if(!ram(address,bytes,32u) || refuse_rx_map) return NULL;
    return memory+physical(address)-0x0c000000u;
}
uint32_t kui_vmu_native_passive_test_sr(void) {return live_sr;}
uint32_t kui_vmu_native_passive_test_sp(void) {return live_sp;}
uint32_t kui_vmu_native_passive_test_guard(uint32_t address,unsigned i) {
    uint32_t expected=STACK_LOW;
#if KUI_VMU_NATIVE_CACHED_CODE
    if(state.armed) expected-=UINT32_C(0x20000000);
#endif
    assert(address==expected+i*4u);
    return i+1u==bad_guard?0u:GUARD;
}
void kui_vmu_native_persistent_test_clock(struct kui_vmu_native_profile_sample *sample,uint32_t *ccr) {
    ++clock_reads;*sample=test_clock;*ccr=test_ccr;test_clock.counter-=10u;
}
void kui_vmu_native_passive_test_terminal(uint32_t phase,uint32_t error) {terminal_phase=phase;terminal_error=error;longjmp(terminal,1);}
static struct kui_vmu_native_passive_config input;
static struct kui_vmu_native_passive_frame frame;
static void runtime_phase(unsigned phase) {
    put(0x0c3010b4u,4u,phase);put(0x0c3010b8u,4u,phase);put(0x0c300fc0u,4u,phase?0u:1u);
    frame.sr=phase?0xf0u:0x60u;frame.pr=0x0c012226u;
}
static void fixture(void) {
    memset(&durable_card,0,sizeof(durable_card));memset(&overlay,0,sizeof(overlay));
    memset(&state,0,sizeof(state));memset(&config,0,sizeof(config));memset(captured,0,sizeof(captured));
    memset(receivers,0,sizeof(receivers));memset(&input,0,sizeof(input));memset(&frame,0,sizeof(frame));
    memset(memory,0,sizeof(memory));memset(mmio,0,sizeof(mmio));
    used=bus_reads=fail_read=0u;refuse_rx_map=false;live_sr=0xf0u;live_sp=SELF_ADDRESS+WORKER_SPAN-0x100u;bad_guard=0u;terminal_phase=terminal_error=0u;
    test_clock=(struct kui_vmu_native_profile_sample){0xe0au,UINT32_MAX,2u,1u,UINT32_MAX};
    test_ccr=0x105u;clock_reads=0u;
    corrupt_on_restore=0u;
    input.version=1u;input.bytes=sizeof(input);input.tx_base=0xac400000u;
    input.driver=(struct kui_vmu_native_discovery){.version=1u,.bytes=sizeof(input.driver),.family=1u,
        .image_bytes=0x200000u,.load_address=0x8c010000u,.requirements=7u,
        .submit=0x8c012000u,.resume=0x8c01200cu,.manager_cell=0x0c3010fcu,.manager_initial=0x0c300000u,
        .lock_cell=0x0c011000u,.completion_slot=0x0c012330u,
        .completion_call=0x8c012222u,.consumer=0x0c012116u};
    kui_vmu_native_passive_resume=input.driver.resume;kui_vmu_native_passive_consumer=input.driver.consumer;
    struct kui_vmu_native_heap *h=&input.lease.owner;
    *h=(struct kui_vmu_native_heap){.version=1u,.bytes=sizeof(*h),.family=1u,.layout=1u,
        .image_bytes=0x200000u,.load=0x8c010000u,.initializer=0x8c013400u,
        .initializer_slot=0x0c0133f0u,.initializer_target=0x8c013400u,
        .allocate=0x8c0134e2u,.release=0x8c0135b0u,.dispatch_cell=0x0c5ffffcu,.dispatch_table=0x0c011100u,
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
        .lease_address=0x8c700000u,.lease_bytes=0x30000u,.lease_header_bytes=32u,.required_lease_bytes=0x20000u,
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
    input.sd_lease=0x8c004100u;
    input.low_heap_hook=0x8c004200u;input.low_terminal=0x8c004300u;
    input.exits=(struct kui_vmu_native_exit){.version=KUI_VMU_NATIVE_EXIT_VERSION,
        .bytes=sizeof(input.exits),.family=KUI_VMU_NATIVE_EXIT_FAMILY,
        .load=input.driver.load_address,.image_bytes=input.driver.image_bytes,
        .count=KUI_VMU_NATIVE_EXIT_ENTRIES,.entries={0x8c013000u,0x8c013100u,0x8c013200u}};
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
        unsigned words=command==1u || command==2u || command==3u?0u:command==12u?34u:2u;
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
    put(rx_at(6),4u,0x1c604005u);put(rx_at(6)+4u,4u,0x01000000u);
    put(0xa05f6c18u,4u,0u);
}
static void arm(void) {
    CHECK(kui_vmu_native_passive_arm(&input,NULL)==0);
#if KUI_VMU_NATIVE_CACHED_CODE
    /* The runtime gateway selects P1 only after cold P2 arm returns. */
    live_sp-=UINT32_C(0x20000000);
#endif
}
static void submit(void) {
    unsigned before=bus_reads;
    if(setjmp(terminal)==0) {kui_vmu_native_passive_submit(&frame);measured_submit_reads=bus_reads-before;
        CHECK(state.pending && state.submitted==state.completed+1u);return;}
    fprintf(stderr,"Unexpected persistent submission refusal %u: %08x/%08x/%08x\n",terminal_error,state.overlay_result,state.fault_at,state.runtime_result);
    CHECK(false);
}
static void complete_return(void) {
    unsigned before=bus_reads;
    if(setjmp(terminal)==0) {kui_vmu_native_passive_complete(&frame);measured_complete_reads=bus_reads-before;
        CHECK(!state.pending && state.submitted==state.completed);return;}
    fprintf(stderr,"Unexpected persistent completion refusal %u: %08x/%08x/%08x\n",terminal_error,state.overlay_result,state.fault_at,state.runtime_result);
    CHECK(false);
}
static void reject_submit(void) {
    if(setjmp(terminal)==0) {kui_vmu_native_passive_submit(&frame);CHECK(false);}
    CHECK(terminal_error && !state.pending && !state.submitted && !state.proof);
}
static void reject_complete(void) {
    if(setjmp(terminal)==0) {kui_vmu_native_passive_complete(&frame);CHECK(false);}
    CHECK(terminal_error && state.pending && !state.proof);
}
/* Only the scoped SCI ownership boundary is substituted. The callback uses
 * the production durable store, CRC, command and PIO transfer code. */
static unsigned lease_calls,block_callbacks;
static bool release_saw_unpublished;
static bool block_transfer(void *context,const uint8_t *tx,uint8_t *rx,size_t count,
    bool slow,uint16_t *crc_out) {
    CHECK(context==&sd && !tx && rx && count==512u);++block_callbacks;
    for(size_t i=0;i<count;i++) rx[i]=transfer(context,0xffu,slow);
    if(crc_out) *crc_out=crc16(rx,count);
    return true;
}
void kui_vmu_native_passive_test_write(uint32_t address,unsigned width,uint32_t value) {
    CHECK(address==0xa05f6c04u && width==4u);
    if(corrupt_on_restore && state.pending && value==state.source) {
        unsigned which=corrupt_on_restore;corrupt_on_restore=0u;
        /* The detailed DMA/chain check has already succeeded. Model a
         * changed witness in the exact first-completion window from the
         * Evolution console refusal, before SD or virtual RX publication. */
        if(which==1u) return; /* MDSTAR restoration was not observed. */
        if(which==2u) frame.pr^=2u;
        else if(which==3u) frame.sr=0u;
        else if(which==4u) live_sp=STACK_HIGH;
        else if(which==5u) bad_guard=1u;
        else if(which==6u) put(config.driver.manager_cell,4u,config.driver.manager_initial+4u);
        else if(which==7u) put(config.driver.lock_cell,4u,0xac011008u);
        else if(which==8u) put(0xac011004u,1u,0u);
        else if(which==9u) put(config.driver.manager_initial+0xfc0u,4u,1u);
        else if(which==10u) kui_vmu_native_passive_consumer+=2u;
        else if(which==11u) state.pending=0u;
        else if(which==12u) ++state.submitted;
        else CHECK(false);
    }
    put(address,width,value);
}
uint32_t kui_vmu_native_persistent_test_chain(const void *pointer,unsigned bytes) {
    CHECK(pointer==issued_chain && bytes==sizeof(issued_chain));return physical(SELF_ADDRESS)+0x18000u;
}
int kui_vmu_native_persistent_test_lease(kui_vmu_sd_transaction callback,void *context) {
    CHECK(callback && context==&durable_card && state.pending);++lease_calls;
    if(sd.deny_enter || sd.deny_acquire) return KUI_VMU_SD_LEASE_BUSY;
    CHECK(enter(&sd) && acquire(&sd)==KUI_LOADER_SD_OK);
    uint32_t crc=durable_card.handoff.block_crc[durable_card.block];
    uint32_t commits=durable_card.commits,bound=durable_card.bound;
    unsigned committed=durable_card.protocol.committed_valid;
    uint8_t before[1024];memcpy(before,rx_bytes(2),sizeof(before));
    struct kui_loader_sd loan=model_card;
    int result=callback(context,&loan);
    CHECK(durable_card.handoff.block_crc[durable_card.block]==crc && durable_card.commits==commits);
    CHECK(durable_card.bound==bound && !durable_card.store.io.context);
    CHECK(durable_card.protocol.committed_valid==committed);
    CHECK(!memcmp(before,rx_bytes(2),sizeof(before)));
    release_saw_unpublished=true;release(&sd);leave(&sd);
    if(result || !loan.ready || !sd.health) {model_card.ready=false;return KUI_VMU_SD_LEASE_FAULT;}
    if(corrupt_after_loan) {
        unsigned which=corrupt_after_loan;corrupt_after_loan=0u;
        uint32_t target=which==1u?config.lease.header+4u:
            which==2u?config.layout.input_array:which==3u?state.source+12u:config.driver.submit;
        put(target,4u,0u);
    }
    return KUI_VMU_SD_LEASE_OK;
}
static void prepare_handoff(void) {
    struct kui_vmu_durable_handoff *h=&input.durable;
    memset(h,0,sizeof(*h));h->card_sectors=1024u;h->high_capacity=1u;
    memcpy(h->cid,sd.cid,16u);memcpy(h->image_id,store_config.image_id,16u);
    h->base_count=h->journal_count=2u;
    for(unsigned i=0;i<2u;i++) {
        h->base[i]=(struct kui_vmu_durable_extent){(uint32_t)image[i].first_lba,image[i].sectors};
        h->journal[i]=(struct kui_vmu_durable_extent){(uint32_t)journal[i].first_lba,journal[i].sectors};
    }
    struct kui_sha256 hash;kui_sha256_init(&hash);
    for(unsigned block=0;block<256u;block++) {
        h->block_crc[block]=kui_retail_crc32(0,sd.media[image_lba(block)],512u);
        kui_sha256_update(&hash,sd.media[image_lba(block)],512u);
    }
    kui_sha256_digest(&hash,h->base_sha256);CHECK(!kui_vmu_durable_handoff_seal(h));
}
static void install_hooks(void) {
    const uint32_t entries[]={input.driver.submit,input.lease.owner.release,
        input.lease.owner.initializer,input.exits.entries[0],input.exits.entries[1],input.exits.entries[2]};
    for(unsigned i=0;i<sizeof(entries)/sizeof(entries[0]);i++) {
        if(i>=3u+input.exits.count) break;
        uint32_t target=i==0u?kui_vmu_native_worker_exports.submit_hook:
            i==1u?kui_vmu_native_worker_exports.release_hook:input.low_terminal;
        uint16_t patch[6];CHECK(kui_vmu_native_patch_encode(entries[i],target,patch));
        for(unsigned byte=0;byte<12u;byte++)
            put(entries[i]+byte,1u,(patch[byte/2u]>>(8u*(byte&1u)))&255u);
    }
    put(input.lease.owner.initializer_slot,4u,input.low_heap_hook);
    put(input.driver.completion_slot,4u,kui_vmu_native_worker_exports.complete_hook);
    CHECK(durable_card.ready && !durable_card.bound && !durable_card.store.io.context);
}
static void cold_worker(void) {
    fixture();prepare_handoff();model_card.bus.transfer_block=block_transfer;
    lease_calls=block_callbacks=0u;corrupt_before_loan=corrupt_after_loan=0u;
    release_saw_unpublished=false;arm();install_hooks();
}
static void setup(void) {model_reset();cold_worker();}
static void idle(void) {
    CHECK(!sd.locked && !sd.owned && !sd.selected && !durable_card.inflight && !durable_card.handling);
    CHECK(!durable_card.store.io.context && sd.acquires==sd.releases && sd.enters==sd.leaves);
    CHECK(model_card.bus.transfer_block==block_transfer);
}
static void inspect_physical_chain(bool original_fast) {
    const uint8_t *hardware=issued_chain;
    size_t capacity=sizeof(issued_chain);
    uint32_t target=config.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_RAW_CHAIN?
        state.source:physical(SELF_ADDRESS)+0x18000u;
    CHECK((controller_pair.pending!=0u)==original_fast);
    if(original_fast) {
        CHECK((config.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_NORMAL ||
               config.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE) && !overlay.pending);
        CHECK(state.bytes==28u && state.descriptors==2u && !state.nops);
        CHECK(controller_pair.words==7u && controller_pair.count==2u && !controller_pair.nops);
        hardware=memory+state.source-0x0c000000u;capacity=state.bytes;target=state.source;
        /* This fixture's newly eligible batch is exactly A GETCOND plus
         * B DEVINFO, including the original RX destinations and LAST bit. */
        const uint32_t expected[7]={1u,rx_at(0),0x01002009u,0x01000000u,
            0x80010000u,rx_at(6),0x00406001u};
        CHECK(!memcmp(hardware,expected,sizeof(expected)));
    }
    CHECK(value_at(0xa05f6c04u)==target);
    unsigned at=0u,count=0u;
    for(;;) {
        CHECK(at+12u<=capacity);uint32_t descriptor[3];
        memcpy(descriptor,hardware+at,sizeof(descriptor));
        unsigned destination=(descriptor[2]>>8)&255u,cmd=descriptor[2]&255u;
        CHECK(destination!=2u && cmd!=12u && cmd!=13u);
        CHECK(destination==32u || destination==96u);
        at+=12u+4u*(descriptor[0]&255u);++count;
        if(descriptor[0]&0x80000000u) break;
    }
    CHECK(count==2u && at<=capacity);
    if(original_fast) CHECK(at==capacity);
}
static void begin_batch(unsigned cmd,unsigned block,unsigned phase,const uint8_t *payload) {
    stage_batch(cmd,block);
    uint32_t source=physical(input.tx_base)+packet_bank*0x3000u;
    if(cmd!=1u && cmd!=2u && cmd!=3u && cmd) put(source+32u,4u,(block<<24)|(phase<<8));
    if(cmd==12u) {
        CHECK(payload);memcpy(memory+source+36u-0x0c000000u,payload,128u);
    }
    uint8_t before[0x3000];memcpy(before,memory+source-0x0c000000u,sizeof(before));
    /* Eligibility follows this fixture's packet shape and prior full-path
     * admission, independently of whichever lane production then chooses. */
    bool original_fast=!cmd && live_plan.ready && overlay.admitted &&
        (config.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_NORMAL ||
         config.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE);
    submit();inspect_physical_chain(original_fast);
    CHECK(!memcmp(before,memory+source-0x0c000000u,sizeof(before)));
    /* Model the real MDST=1 instruction replayed by the assembly gateway. */
    put(0xa05f6c18u,4u,1u);
}
static void finish_batch(void) {
    uint8_t parent_reply[1024],unrelated_reply[1024];
    physical_complete();memcpy(parent_reply,rx_bytes(0),sizeof(parent_reply));
    memcpy(unrelated_reply,rx_bytes(6),sizeof(unrelated_reply));
    complete_return();idle();parent_reply[2]|=2u;
    CHECK(!memcmp(parent_reply,rx_bytes(0),sizeof(parent_reply)));
    CHECK(!memcmp(unrelated_reply,rx_bytes(6),sizeof(unrelated_reply)));
    CHECK(value_at(0xa05f6c04u)==physical(input.tx_base)+packet_bank*0x3000u);
}
static void transfer_batch(unsigned cmd,unsigned block,unsigned phase,const uint8_t *payload) {
    begin_batch(cmd,block,phase,payload);finish_batch();
}
static void stage_block(unsigned block,const uint8_t data[512]) {
    unsigned bytes=sd.bytes,calls=lease_calls;
    for(unsigned phase=0;phase<4u;phase++) {
        transfer_batch(12u,block,phase,data+phase*128u);
        CHECK(value_at(rx_at(2))==0x00020007u);
    }
    CHECK(sd.bytes==bytes && lease_calls==calls && durable_card.write_phases==4u);
}
static void read_compare(unsigned block,const uint8_t data[512]);
static void protect_media(void);
static void retry_batch(unsigned command,unsigned block,unsigned phase,const uint8_t *payload) {
    uint8_t parent[1024],unrelated[1024],target[1024];
    begin_batch(command,block,phase,payload);physical_complete();
    /* Retain the physical A1 attachment while advertising virtual A2. */
    rx_bytes(0)[2]|=1u;
    memcpy(parent,rx_bytes(0),sizeof(parent));memcpy(unrelated,rx_bytes(6),sizeof(unrelated));
    memcpy(target,rx_bytes(2),sizeof(target));
    complete_return();idle();parent[2]|=2u;
    CHECK(!memcmp(parent,rx_bytes(0),sizeof(parent)));
    CHECK(!memcmp(unrelated,rx_bytes(6),sizeof(unrelated)));
    CHECK(value_at(rx_at(2))==UINT32_C(0x000200fc));
    CHECK(!memcmp(target+4u,rx_bytes(2)+4u,sizeof(target)-4u));
    CHECK(!state.pending && !state.error && state.completed==state.submitted &&
        overlay.admitted && !overlay.pending && !overlay.stats.flags && !durable_card.faulted);
}
static void storage_contention_retries(void) {
    uint8_t old[512],next[512],different[128];
    memset(next,0x73,sizeof(next));memset(different,0x92,sizeof(different));
    for(unsigned deny=0;deny<2u;deny++) {
        setup();memcpy(old,sd.media[image_lba(42u)],sizeof(old));
        for(unsigned attempt=0;attempt<3u;attempt++) {
            sd.deny_enter=deny==0u;sd.deny_acquire=deny==1u;
            retry_batch(11u,42u,0u,NULL);
            CHECK(lease_calls==attempt+1u && !block_callbacks && !sd.bytes && !sd.writes);
            CHECK(durable_card.sd_busy==attempt+1u && !durable_card.reads &&
                !durable_card.commits && !durable_card.bound && !durable_card.protocol.read_valid);
        }
        sd.deny_enter=sd.deny_acquire=false;
        read_compare(42u,old);idle();CHECK(durable_card.reads==1u && durable_card.bound);

        setup();stage_block(43u,next);
        sd.deny_enter=deny==0u;sd.deny_acquire=deny==1u;
        retry_batch(13u,43u,4u,NULL);
        CHECK(lease_calls==1u && !block_callbacks && !sd.bytes && !sd.writes && !durable_card.commits);
        CHECK(durable_card.protocol.active && durable_card.protocol.phases==15u &&
            durable_card.protocol.staged_block==43u && durable_card.protocol.commit_attempted &&
            !durable_card.protocol.committed_valid && !memcmp(durable_card.protocol.staged,next,512u));
        /* A different write cannot replace the immutable pending commit. Its
         * valid AGAIN reply requires no additional storage acquisition. */
        retry_batch(12u,44u,0u,different);
        CHECK(lease_calls==1u && !sd.bytes && !sd.writes && !durable_card.commits);
        CHECK(durable_card.protocol.active && durable_card.protocol.phases==15u &&
            durable_card.protocol.staged_block==43u && !memcmp(durable_card.protocol.staged,next,512u));
        sd.deny_enter=sd.deny_acquire=false;
        transfer_batch(13u,43u,4u,NULL);idle();
        CHECK(value_at(rx_at(2))==UINT32_C(0x00020007) && durable_card.commits==1u &&
            durable_card.protocol.committed_valid && !durable_card.protocol.active);
        CHECK(!memcmp(sd.media[image_lba(43u)],next,512u));
        unsigned bytes=sd.bytes,calls=lease_calls;
        transfer_batch(13u,43u,4u,NULL);
        CHECK(value_at(rx_at(2))==UINT32_C(0x00020007) && durable_card.commits==1u &&
            sd.bytes==bytes && lease_calls==calls);
        read_compare(43u,next);protect_media();
    }
}
static void read_compare(unsigned block,const uint8_t data[512]) {
    transfer_batch(11u,block,0u,NULL);
    if(value_at(rx_at(2))!=0x82020008u || memcmp(rx_bytes(2)+12u,data,512u))
        fprintf(stderr,"Read block%u response%08x actual%02x expected%02x reads%u commits%u\n",
            block,value_at(rx_at(2)),rx_bytes(2)[12],data[0],durable_card.reads,durable_card.commits);
    CHECK(value_at(rx_at(2))==0x82020008u && !memcmp(rx_bytes(2)+12u,data,512u));
}
static void protect_media(void) {
    for(unsigned lba=0;lba<1024u;lba++) if(!allowed(lba))
        for(unsigned i=0;i<512u;i++) CHECK(sd.media[lba][i]==0x59u);
}
static void recover_checkpoint(void) {
    model_reopen();struct kui_vmu_backing backing={0};struct kui_vmu_store store={0};
    CHECK(!kui_vmu_backing_init(&backing,&model_bus,&backing_config));
    struct kui_vmu_store_io io=kui_vmu_backing_io(&backing);
    CHECK(!kui_vmu_store_open(&store,&io,&store_config));
    CHECK(!kui_vmu_store_checkpoint(&store));
    CHECK(!memcmp(sd.media[950u],(uint8_t[512]){0},512u));
    CHECK(!sd.owned && !sd.locked && !sd.selected);
    cold_worker();
}
static unsigned full_work(void) {
    return measured_work.heap+measured_work.layout+measured_work.runtime+
        measured_work.chain+measured_work.hooks;
}
static unsigned measure_batch(unsigned cmd,unsigned phase,const uint8_t *payload) {
    memset(&measured_work,0,sizeof(measured_work));
    transfer_batch(cmd,cmd==10u?0u:42u,phase,payload);
    return measured_submit_reads+measured_complete_reads;
}
static void bounded_controller_work(void) {
    setup();unsigned first=measure_batch(0u,0u,NULL);
    CHECK(first<=400u && full_work()<=10u && !measured_work.storage && !lease_calls);
    unsigned controller=measure_batch(0u,0u,NULL);
    CHECK(!clock_reads); /* Ordinary mode never samples timer/cache MMIO. */
    CHECK(controller<=350u && full_work()<=8u && !measured_work.storage && !lease_calls);
    unsigned info=measure_batch(1u,0u,NULL);
    CHECK(info<=360u && full_work()<=8u && !measured_work.storage);
    unsigned read=measure_batch(11u,0u,NULL);
    CHECK(read<=800u && full_work()<=20u && measured_work.storage==2u && lease_calls==1u);
    /* A valid fragmented free-list used to multiply every redundant proof.
     * All ranges remain below the still-owned worker chunk. */
    for(unsigned i=0;i<64u;i++) {
        uint32_t node=input.lease.begin+i*64u;
        put(node,4u,i==63u?input.lease.owner.sentinel:node+64u);put(node+4u,4u,1u);
    }
    unsigned fragmented=measure_batch(0u,0u,NULL);
    CHECK(fragmented==controller && full_work()<=8u && !measured_work.storage);
    printf("Native worker guest reads/pair: first=%u controller=%u INFO=%u READ=%u fragmented64=%u\n",
        first,controller,info,read,fragmented);
}
static void polling_ownership_scope(void) {
    /* Polling retains the continuously intercepted allocator's established
     * lease; it does not re-audit arbitrary free-list corruption. Such a
     * corrupt list must still fail before any storage loan or virtual reply. */
    for(volatile unsigned release=0u;release<2u;release++) {
        setup();transfer_batch(0u,0u,0u,NULL);
        put(input.lease.begin,4u,input.lease.begin); /* Invalid circular list. */
        CHECK(poll_lease_valid() && !lease_valid());
        transfer_batch(0u,0u,0u,NULL);CHECK(!sd.bytes && !lease_calls);
        uint8_t before[1024];memcpy(before,rx_bytes(2),sizeof(before));
        if(release) {
            frame.r4=0u;
            if(setjmp(terminal)==0) {kui_vmu_native_readonly_release_check(&frame);CHECK(false);}
            CHECK(terminal_error==0x30u);
        } else {
            begin_batch(11u,42u,0u,NULL);physical_complete();reject_complete();
            CHECK(durable_card.faulted && !durable_card.commits);
        }
        CHECK(!sd.bytes && !lease_calls && !sd.writes);
        CHECK(!memcmp(before,rx_bytes(2),sizeof(before)));
    }
}
static void halfword_exit_hooks(void) {
    for(unsigned family=KUI_VMU_NATIVE_EXIT_PVR_CLEAR;family<=KUI_VMU_NATIVE_EXIT_PVR_COMPACT;family++) {
        model_reset();fixture();prepare_handoff();model_card.bus.transfer_block=block_transfer;
        lease_calls=block_callbacks=0u;corrupt_before_loan=corrupt_after_loan=0u;
        input.exits.family=family;input.exits.entries[1]+=2u;arm();install_hooks();
        transfer_batch(1u,0u,0u,NULL);read_compare(255u,sd.media[image_lba(255u)]);
        CHECK(installed_hooks_valid());
        /* The unaligned literal and trailing NOP are both witnessed before
         * any later DMA. A changed halfword cannot masquerade as a patch. */
        for(unsigned byte=0u;byte<12u;byte+=2u) {
            uint32_t address=input.exits.entries[1]+byte;
            uint8_t old=memory[physical(address)-0x0c000000u];put(address,1u,old^1u);
            CHECK(!installed_hooks_valid());put(address,1u,old);
        }
        CHECK(installed_hooks_valid());idle();
    }
    model_reset();fixture();prepare_handoff();model_card.bus.transfer_block=block_transfer;
    lease_calls=block_callbacks=0u;corrupt_before_loan=corrupt_after_loan=0u;
    input.exits.family=KUI_VMU_NATIVE_EXIT_PAIR_ONLY;input.exits.count=2u;
    input.exits.entries[2]=input.exits.exec_thunk=0u;arm();install_hooks();
    transfer_batch(1u,0u,0u,NULL);CHECK(installed_hooks_valid() && !sd.bytes);
    for(unsigned invalid=0;invalid<3u;invalid++) {
        fixture();prepare_handoff();input.exits.family=KUI_VMU_NATIVE_EXIT_PAIR_ONLY;input.exits.count=2u;
        input.exits.entries[2]=input.exits.exec_thunk=0u;
        if(invalid==0u) input.exits.count=3u;
        if(invalid==1u) input.exits.entries[2]=0x8c013200u;
        if(invalid==2u) input.exits.exec_thunk=0x8c013200u;
        CHECK(kui_vmu_native_passive_arm(&input,NULL)<0 && !state.armed);
    }
}
/* Frozen pre-preparation witness scope. This independently rebuilds each
 * veneer and compares its original 16/32-bit reads, so the prepared masks
 * cannot accidentally omit a byte or acquire either adjacent halfword. */
static bool legacy_patch_matches(uint32_t address,uint32_t target) {
    uint16_t patch[6];
    if(!ram(address,12u,2u) || !kui_vmu_native_patch_encode(address,target,patch)) return false;
    unsigned width=(address&3u)?2u:4u;
    for(unsigned i=0;i<12u/width;i++) {
        uint32_t value,expected=patch[i*(width/2u)];
        if(width==4u) expected|=(uint32_t)patch[i*2u+1u]<<16;
        if(!read_value(NULL,(physical(address)+i*width)|UINT32_C(0xa0000000),width,&value) ||
           value!=expected) return false;
    }
    return true;
}
static bool legacy_installed_hooks_valid(void) {
    uint32_t value;
    if((config.driver.submit|config.lease.owner.release|config.lease.owner.initializer)&3u ||
       !legacy_patch_matches(config.driver.submit,kui_vmu_native_worker_exports.submit_hook) ||
       !legacy_patch_matches(config.lease.owner.release,kui_vmu_native_worker_exports.release_hook) ||
       !legacy_patch_matches(config.lease.owner.initializer,config.low_terminal) ||
       !read_value(NULL,physical(config.lease.owner.initializer_slot)|UINT32_C(0xa0000000),4u,&value) ||
       value!=config.low_heap_hook ||
       !read_value(NULL,physical(config.driver.completion_slot)|UINT32_C(0xa0000000),4u,&value) ||
       value!=kui_vmu_native_worker_exports.complete_hook) return false;
    for(unsigned i=0;i<config.exits.count;i++)
        if(!legacy_patch_matches(config.exits.entries[i],config.low_terminal)) return false;
    return true;
}
static void prepared_hook_witnesses(void) {
    unsigned mutations=0u,neighbors=0u,reader_faults=0u;
    for(unsigned family=KUI_VMU_NATIVE_EXIT_FAMILY;family<=KUI_VMU_NATIVE_EXIT_PAIR_ONLY;family++)
        for(unsigned half=0u;half<2u;half++) {
            model_reset();fixture();prepare_handoff();model_card.bus.transfer_block=block_transfer;
            lease_calls=block_callbacks=0u;corrupt_before_loan=corrupt_after_loan=0u;
            input.exits.family=family;
            if(family==KUI_VMU_NATIVE_EXIT_PAIR_ONLY) {
                input.exits.count=2u;input.exits.entries[2]=input.exits.exec_thunk=0u;
            }
            for(unsigned i=0;i<input.exits.count;i++) input.exits.entries[i]+=half*2u;
            arm();install_hooks();
            CHECK(installed_hooks_valid() && legacy_installed_hooks_valid());
            CHECK(hook_witness_count==11u+input.exits.count*(half?4u:3u));
            for(unsigned i=0;i<hook_witness_count;i++) {
                CHECK(ram(hook_witness[i].address,4u,4u));
                CHECK((hook_witness[i].address&UINT32_C(0xe0000000))==UINT32_C(0xa0000000));
                CHECK(hook_witness[i].mask && !(hook_witness[i].value&~hook_witness[i].mask));
            }
            const uint32_t entries[]={input.driver.submit,input.lease.owner.release,
                input.lease.owner.initializer,input.exits.entries[0],input.exits.entries[1],input.exits.entries[2]};
            /* Every bit of every instruction, literal and trailing NOP is
             * mandatory, including both bytes of all halfword-aligned exits. */
            for(unsigned i=0;i<3u+input.exits.count;i++) for(unsigned byte=0;byte<12u;byte++)
                for(unsigned bit=0;bit<8u;bit++) {
                    uint32_t address=entries[i]+byte;
                    uint8_t old=memory[physical(address)-UINT32_C(0x0c000000)];
                    put(address,1u,old^(1u<<bit));
                    CHECK(!installed_hooks_valid() && !legacy_installed_hooks_valid());
                    put(address,1u,old);
                    CHECK(installed_hooks_valid() && legacy_installed_hooks_valid());++mutations;
                }
            const uint32_t slots[]={input.lease.owner.initializer_slot,input.driver.completion_slot};
            for(unsigned i=0;i<2u;i++) for(unsigned byte=0;byte<4u;byte++)
                for(unsigned bit=0;bit<8u;bit++) {
                    uint32_t address=slots[i]+byte;
                    uint8_t old=memory[physical(address)-UINT32_C(0x0c000000)];
                    put(address,1u,old^(1u<<bit));
                    CHECK(!installed_hooks_valid() && !legacy_installed_hooks_valid());
                    put(address,1u,old);
                    CHECK(installed_hooks_valid() && legacy_installed_hooks_valid());++mutations;
                }
            /* A 12-byte halfword-aligned window spans four aligned loads.
             * The two neighboring halfwords in those loads remain outside
             * the original witnessed bytes and must not trigger refusal. */
            for(unsigned i=3u;i<3u+input.exits.count;i++) {
                const uint32_t adjacent[]={entries[i]-2u,entries[i]-1u,entries[i]+12u,entries[i]+13u};
                for(unsigned byte=0;byte<4u;byte++) for(unsigned bit=0;bit<8u;bit++) {
                    uint32_t address=adjacent[byte];
                    uint8_t old=memory[physical(address)-UINT32_C(0x0c000000)];
                    put(address,1u,old^(1u<<bit));
                    CHECK(installed_hooks_valid() && legacy_installed_hooks_valid());
                    put(address,1u,old);++neighbors;
                }
            }
            bus_reads=0u;CHECK(installed_hooks_valid());
            unsigned prepared_reads=bus_reads;CHECK(prepared_reads==hook_witness_count);
            for(unsigned fault=1u;fault<=prepared_reads;fault++) {
                bus_reads=0u;fail_read=fault;CHECK(!installed_hooks_valid());
                CHECK(bus_reads==fault);fail_read=0u;++reader_faults;
            }
            bus_reads=0u;CHECK(legacy_installed_hooks_valid());
            unsigned legacy_reads=bus_reads;
            CHECK(legacy_reads==11u+input.exits.count*(half?6u:3u));
            CHECK(prepared_reads<=legacy_reads);
            CHECK(installed_hooks_valid() && legacy_installed_hooks_valid());
            uint32_t ready=hook_witness_ready;
            for(unsigned bit=0u;bit<32u;bit++) {
                hook_witness_ready=ready^(UINT32_C(1)<<bit);bus_reads=0u;
                CHECK(!installed_hooks_valid() && !bus_reads);
            }
            hook_witness_ready=ready;
            unsigned count=hook_witness_count;hook_witness_count=0u;bus_reads=0u;
            CHECK(!installed_hooks_valid() && !bus_reads);
            hook_witness_count=HOOK_WITNESSES+1u;
            CHECK(!installed_hooks_valid() && !bus_reads);
            hook_witness_count=count;CHECK(installed_hooks_valid());idle();
        }
    printf("Prepared hook witnesses: %u byte/bit mutations, %u ignored neighbor bits, %u read-fault positions\n",
        mutations,neighbors,reader_faults);
}
static void external_boundary_mutations(void) {
    uint8_t next[512],before[1024];memset(next,0x73,sizeof(next));
    for(unsigned write=0;write<2u;write++) for(unsigned after=0;after<2u;after++)
        for(unsigned which=1u;which<=4u;which++) {
            setup();if(write) stage_block(42u,next);
            begin_batch(write?13u:11u,42u,write?4u:0u,NULL);physical_complete();
            memcpy(before,rx_bytes(2),sizeof(before));
            if(after) corrupt_after_loan=which;else corrupt_before_loan=which;
            reject_complete();
            CHECK(!memcmp(before,rx_bytes(2),sizeof(before)) && !durable_card.commits);
            if(after) CHECK(lease_calls==1u && sd.bytes && durable_card.faulted);
            else CHECK(!lease_calls && !sd.bytes && !sd.writes && durable_card.faulted);
        }
}
static void diagnostic_setup(void) {
    model_reset();fixture();prepare_handoff();model_card.bus.transfer_block=block_transfer;
    lease_calls=block_callbacks=0u;corrupt_before_loan=corrupt_after_loan=0u;
    input.diagnostics=KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE;arm();install_hooks();
}
static void diagnostic_pair(bool gesture) {
    begin_batch(0u,0u,0u,NULL);physical_complete();
    put(rx_at(0)+8u,4u,gesture?UINT32_C(0xfffffffd):UINT32_MAX);
    complete_return();idle();
}
static void gameplay_diagnostics(void) {
    diagnostic_setup();
    transfer_batch(11u,42u,0u,NULL);CHECK(diagnostic.timing.metric[KUI_VMU_NATIVE_PROFILE_STORAGE].calls==1u);
    for(unsigned i=0;i<29u;i++) diagnostic_pair(true);
    CHECK(!diagnostic.active && diagnostic.gesture_count==29u);
    diagnostic_pair(false);CHECK(!diagnostic.gesture_count && !diagnostic.active);
    for(unsigned i=0;i<30u;i++) diagnostic_pair(true);
    CHECK(diagnostic.active && !diagnostic.clock_started);
    for(unsigned i=0;i<KUI_VMU_NATIVE_PROFILE_KINDS;i++) CHECK(!diagnostic.timing.metric[i].calls);
    CHECK(!diagnostic.fast_submit && !diagnostic.fast_complete &&
          !diagnostic.full_submit && !diagnostic.full_complete);
    diagnostic_pair(false);
    CHECK(diagnostic.clock_started && diagnostic.clock_samples==4u && !diagnostic.clock_invalid);
    CHECK(diagnostic.ccr_first==0x105u && !diagnostic.ice_off);
    CHECK(diagnostic.timing.metric[KUI_VMU_NATIVE_PROFILE_SUBMIT].calls==1u);
    CHECK(diagnostic.timing.metric[KUI_VMU_NATIVE_PROFILE_COMPLETE].calls==1u);
    CHECK(diagnostic.fast_submit==1u && diagnostic.fast_complete==1u &&
          !diagnostic.full_submit && !diagnostic.full_complete);
    frame.r4=0u;kui_vmu_native_readonly_release_check(&frame);
    CHECK(diagnostic.timing.metric[KUI_VMU_NATIVE_PROFILE_FREE].valid==1u);
    transfer_batch(11u,43u,0u,NULL);
    CHECK(diagnostic.fast_submit==1u && diagnostic.fast_complete==1u &&
          diagnostic.full_submit==1u && diagnostic.full_complete==1u);
    CHECK(diagnostic.timing.metric[KUI_VMU_NATIVE_PROFILE_STORAGE].valid==1u);
    CHECK(diagnostic.timing.metric[KUI_VMU_NATIVE_PROFILE_COMPLETE].ticks_total>=
          diagnostic.timing.metric[KUI_VMU_NATIVE_PROFILE_STORAGE].ticks_total);
    test_clock.counter-=20u*KUI_VMU_NATIVE_PROFILE_HZ;
    begin_batch(0u,0u,0u,NULL);physical_complete();
    if(setjmp(terminal)==0) {kui_vmu_native_passive_complete(&frame);CHECK(false);}
    CHECK(terminal_phase==17u && !terminal_error && !state.pending);
    CHECK(state.submitted==state.completed && !durable_card.inflight && !durable_card.handling);
    idle();
    /* An incompatible owner timer never supplies guessed timing. Capture
     * still terminates after the bounded fallback number of complete pairs. */
    diagnostic_setup();test_clock.frqcr=0u;test_ccr=0u;
    for(unsigned i=0;i<30u;i++) diagnostic_pair(true);
    for(unsigned i=0;i<1199u;i++) diagnostic_pair(false);
    CHECK(diagnostic.active && !diagnostic.clock_started && diagnostic.clock_invalid);
    CHECK(diagnostic.ice_off && diagnostic.timing.metric[KUI_VMU_NATIVE_PROFILE_COMPLETE].invalid==1199u);
    begin_batch(0u,0u,0u,NULL);physical_complete();
    if(setjmp(terminal)==0) {kui_vmu_native_passive_complete(&frame);CHECK(false);}
    CHECK(terminal_phase==17u && !terminal_error && !state.pending);
    CHECK(!sd.bytes && !lease_calls && !durable_card.commits);idle();
    for(unsigned mode=KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE+1u;
        mode<=KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE+2u;mode++) {
        model_reset();fixture();prepare_handoff();input.diagnostics=mode;
        CHECK(kui_vmu_native_passive_arm(&input,NULL)<0 && !state.armed);
    }
}
static void hooks_only_setup(void) {
    model_reset();fixture();prepare_handoff();model_card.bus.transfer_block=block_transfer;
    lease_calls=block_callbacks=0u;corrupt_before_loan=corrupt_after_loan=0u;
    input.diagnostics=KUI_VMU_NATIVE_DIAGNOSTICS_HOOKS_ONLY;arm();install_hooks();
}
static void hooks_only_has_no_virtual_card_or_io(void) {
    hooks_only_setup();
    const unsigned commands[]={0u,1u,2u,3u,10u,11u,12u,13u};
    uint8_t payload[128];memset(payload,0x62,sizeof(payload));
    for(unsigned repeat=0;repeat<2u;repeat++) for(unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);i++) {
        unsigned cmd=commands[i];
        begin_batch(cmd,cmd==10u?0u:42u,cmd==13u?4u:0u,cmd==12u?payload:NULL);
        physical_complete();
        uint8_t parent[1024],target[1024],other[1024];
        memcpy(parent,rx_bytes(0),sizeof(parent));memcpy(target,rx_bytes(2),sizeof(target));
        memcpy(other,rx_bytes(6),sizeof(other));
        struct kui_vmu_durable saved_card=durable_card;
        complete_return();idle();
        CHECK(!memcmp(parent,rx_bytes(0),sizeof(parent)) && !(rx_bytes(0)[2]&2u));
        CHECK(!memcmp(other,rx_bytes(6),sizeof(other)));
        if(cmd) {
            CHECK(value_at(rx_at(2))==UINT32_MAX);
            CHECK(!memcmp(target+4u,rx_bytes(2)+4u,sizeof(target)-4u));
        } else CHECK(!memcmp(target,rx_bytes(2),sizeof(target)));
        CHECK(!memcmp(&saved_card,&durable_card,sizeof(durable_card)));
        CHECK(!overlay.stats.backend_calls && !overlay.stats.advertisements && !overlay.stats.replies);
        CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes && !durable_card.commits);
        CHECK(!clock_reads && !diagnostic.clock_samples && !diagnostic.active && !diagnostic.gesture_count);
        CHECK(value_at(0xa05f6c04u)==state.source);
    }
    CHECK(state.submitted==16u && state.completed==16u && overlay.stats.completed==16u);
    frame.r4=0u;kui_vmu_native_readonly_release_check(&frame);
    CHECK(!clock_reads && !diagnostic.timing.metric[KUI_VMU_NATIVE_PROFILE_FREE].calls);

    /* All ordinary physical/context/ownership guards remain mandatory before
     * any absent publication. Both failure cases preserve stale A2 headers. */
    for(unsigned failure=0;failure<3u;failure++) {
        hooks_only_setup();begin_batch(11u,42u,0u,NULL);physical_complete();
        if(!failure) rx_bytes(0)[2]|=2u;
        else if(failure==1u) put(input.driver.completion_slot,4u,kui_vmu_native_worker_exports.complete_hook+2u);
        else put(0x0c3010b4u,4u,packet_bank);
        uint8_t before[3][1024];
        memcpy(before[0],rx_bytes(0),1024u);memcpy(before[1],rx_bytes(2),1024u);
        memcpy(before[2],rx_bytes(6),1024u);reject_complete();
        CHECK(!memcmp(before[0],rx_bytes(0),1024u) && !memcmp(before[1],rx_bytes(2),1024u));
        CHECK(!memcmp(before[2],rx_bytes(6),1024u));
        CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes && !overlay.stats.backend_calls);
        CHECK(!clock_reads && !durable_card.commits);
    }
}
static void raw_chain_preserves_original_dma(void) {
    model_reset();fixture();prepare_handoff();model_card.bus.transfer_block=block_transfer;
    lease_calls=block_callbacks=0u;corrupt_before_loan=corrupt_after_loan=0u;
    input.diagnostics=KUI_VMU_NATIVE_DIAGNOSTICS_RAW_CHAIN;arm();install_hooks();
    for(unsigned i=0;i<32u;i++) {
        stage_batch(i&1u?1u:0u,0u);
        uint32_t source=value_at(0xa05f6c04u);
        uint8_t original[0x3000];memcpy(original,memory+source-0x0c000000u,sizeof(original));
        submit();CHECK(value_at(0xa05f6c04u)==source);
        CHECK(!memcmp(original,memory+source-0x0c000000u,sizeof(original)));
        put(0xa05f6c18u,4u,1u);physical_complete();complete_return();idle();
        CHECK(value_at(0xa05f6c04u)==source);
        CHECK(!sd.bytes && !sd.writes && !lease_calls && !overlay.stats.backend_calls);
        CHECK(!overlay.stats.advertisements && !overlay.stats.replies);
    }
    CHECK(persistent_lease(kui_vmu_native_persistent_sd_transaction,&durable_card)==KUI_VMU_SD_LEASE_FAULT);
    CHECK(!lease_calls);
}
static void reject_context_complete(void) {
    if(setjmp(terminal)==0) {kui_vmu_native_passive_complete(&frame);CHECK(false);}
    CHECK(terminal_error==0x43u && !state.proof);
}
static void completion_context_failures(void) {
    static const unsigned code[]={0x20u,12u,8u,3u,4u,0x30u,0x31u,0x32u,0x40u,14u,0x51u,0x52u};
    for(unsigned which=1u;which<=sizeof(code)/sizeof(code[0]);which++) {
        uint8_t before[3][1024];
        setup();begin_batch(1u,0u,0u,NULL);physical_complete();
        memcpy(before[0],rx_bytes(0),1024u);memcpy(before[1],rx_bytes(2),1024u);
        memcpy(before[2],rx_bytes(6),1024u);
        uint32_t entry_pr=frame.pr,entry_sr=frame.sr;corrupt_on_restore=which;
        reject_context_complete();
        CHECK(!corrupt_on_restore && terminal_error==0x43u && !state.proof);
        CHECK(state.runtime_result==(0x20000u|code[which-1u]));
        CHECK(state.frame_pr==entry_pr && state.frame_sr==entry_sr);
        CHECK(!lease_calls && !sd.bytes && !sd.writes && !durable_card.commits);
        CHECK(!memcmp(before[0],rx_bytes(0),1024u) && !memcmp(before[1],rx_bytes(2),1024u));
        CHECK(!memcmp(before[2],rx_bytes(6),1024u));
        if(which==1u) CHECK(state.fault_at==0xa05f6c04u &&
            state.fault_value==issued_plan.output_physical && state.fault_expect==state.source);
        if(which==2u) CHECK(completion_frame.pr==(entry_pr^2u) &&
            state.fault_value==completion_frame.pr && state.fault_expect==config.driver.completion_call+4u);
    }
}
int main(void) {
    uint8_t next[512],old[512],parent[1024],target[1024],other[1024];
    memset(next,0x73,sizeof(next));memset(old,0x59,sizeof(old));
    storage_contention_retries();
    setup();transfer_batch(1u,0u,0u,NULL);transfer_batch(10u,0u,0u,NULL);
    CHECK(overlay.stats.devinfo==1u && overlay.stats.media==1u && !lease_calls);
    stage_block(42u,next);transfer_batch(13u,42u,4u,NULL);
    CHECK(value_at(rx_at(2))==0x00020007u && durable_card.commits==1u && release_saw_unpublished);
    CHECK(!block_callbacks && !memcmp(sd.media[image_lba(42u)],next,512u));
    read_compare(42u,next);read_compare(43u,old);
    CHECK(block_callbacks==2u && durable_card.reads==2u && durable_card.commits==1u);
    protect_media();recover_checkpoint();read_compare(42u,next);protect_media();

    /* A2 payloads and full SDK state remain immutable through completion.
     * None of these corruptions may invoke the SD gateway or publish RX. */
    for(unsigned corruption=0;corruption<20u;corruption++) {
        setup();stage_block(42u,next);begin_batch(13u,42u,4u,NULL);physical_complete();
        if(corruption==0u) put(state.source+12u,4u,0x02000000u);
        else if(corruption==1u) put(input.layout.input_array,4u,0xac510000u);
        else if(corruption==2u) put(input.lease.header+4u,4u,input.lease.units+1u);
        else if(corruption==3u) kui_vmu_native_passive_consumer+=2u;
        else if(corruption==4u) issued_chain[8]^=1u;
        else if(corruption<17u) {
            const uint32_t entries[]={input.driver.submit,input.lease.owner.release,
                input.lease.owner.initializer,input.exits.entries[0],input.exits.entries[1],input.exits.entries[2]};
            unsigned which=(corruption-5u)/2u,offset=((corruption-5u)&1u)?8u:0u;
            put(entries[which]+offset,4u,0u);
        } else if(corruption==17u) put(input.lease.owner.initializer_slot,4u,input.low_heap_hook+2u);
        else if(corruption==18u) put(input.driver.completion_slot,4u,kui_vmu_native_worker_exports.complete_hook+2u);
        else put(rx_at(6),4u,0xa5a5a5a5u);
        memcpy(parent,rx_bytes(0),sizeof(parent));memcpy(target,rx_bytes(2),sizeof(target));
        memcpy(other,rx_bytes(6),sizeof(other));reject_complete();
        CHECK(!lease_calls && !sd.bytes && !sd.writes && !durable_card.commits);
        CHECK(!memcmp(parent,rx_bytes(0),sizeof(parent)) && !memcmp(target,rx_bytes(2),sizeof(target)));
        CHECK(!memcmp(other,rx_bytes(6),sizeof(other)));
    }
    /* A physical A2 attachment blocks the backend even though virtual A2
     * requests are absent from the entire physical schedule. */
    setup();begin_batch(1u,0u,0u,NULL);physical_complete();rx_bytes(0)[2]|=2u;
    memcpy(parent,rx_bytes(0),sizeof(parent));memcpy(target,rx_bytes(2),sizeof(target));reject_complete();
    CHECK(!lease_calls && !sd.bytes && !memcmp(parent,rx_bytes(0),sizeof(parent)));
    CHECK(!memcmp(target,rx_bytes(2),sizeof(target)));
    const unsigned unsupported[]={4u,9u};
    for(unsigned i=0;i<sizeof(unsupported)/sizeof(unsupported[0]);i++) {
        setup();stage_batch(unsupported[i],0u);reject_submit();CHECK(!sd.bytes && !lease_calls);
    }
    setup();begin_batch(12u,42u,0u,next);physical_complete();
    put(state.source+40u,4u,0x01020304u);memcpy(target,rx_bytes(2),sizeof(target));reject_complete();
    CHECK(!durable_card.write_phases && !lease_calls && !sd.bytes && !memcmp(target,rx_bytes(2),sizeof(target)));
    /* A torn base write is never acknowledged. Fresh controlled management
     * recovery replays its committed journal before resealing the next boot. */
    setup();stage_block(42u,next);sd.fail_write=4u;begin_batch(13u,42u,4u,NULL);physical_complete();
    memcpy(target,rx_bytes(2),sizeof(target));reject_complete();
    CHECK(durable_card.faulted && !durable_card.commits && !memcmp(target,rx_bytes(2),sizeof(target)));
    recover_checkpoint();read_compare(42u,next);protect_media();
    /* Successful wire programming still cannot publish before restoration. */
    setup();stage_block(42u,next);sd.fail_release=true;begin_batch(13u,42u,4u,NULL);physical_complete();
    memcpy(target,rx_bytes(2),sizeof(target));reject_complete();
    CHECK(!durable_card.commits && durable_card.faulted && !memcmp(target,rx_bytes(2),sizeof(target)));
    CHECK(!memcmp(sd.media[image_lba(42u)],next,512u));
    bounded_controller_work();external_boundary_mutations();halfword_exit_hooks();prepared_hook_witnesses();
    polling_ownership_scope();gameplay_diagnostics();hooks_only_has_no_virtual_card_or_io();raw_chain_preserves_original_dma();completion_context_failures();
    printf("Native persistent bridge: %u checks passed; filtered hardware chain, PIO save, reboot recovery and no false ACK\n",checks);
    return 0;
}
