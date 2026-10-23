/* SPDX-License-Identifier: GPL-3.0-only */
/* Replay the first four-port DEVINFO transaction with an external executable's
 * complete, freshly reverified discovery record. No retail bytes are embedded.
 * The shared fixture supplies only the host RAM/MMIO/SD bus and real worker;
 * every game address, layout, allocator and hook target comes from discovery.
 */
#include <stdlib.h>
#ifndef KUI_VMU_NATIVE_TEST_SELF_ADDRESS
#define KUI_VMU_NATIVE_TEST_SELF_ADDRESS UINT32_C(0xacfd0000)
#endif
#define main native_persistent_reference_main
#include "test_vmu_native_persistent.c"
#undef main
#include "kui/vmu_native_resolution_cache.h"

static const uint32_t aliases[3]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
static uint8_t *startup;
static size_t startup_bytes;
static struct kui_vmu_native_resolution resolution;
static uint32_t manager,source;
static unsigned current_bank,guard_refusals;

static uint8_t *load_file(const char *path,size_t maximum,size_t *bytes) {
    FILE *file=fopen(path,"rb");CHECK(file);
    CHECK(!fseek(file,0,SEEK_END));long length=ftell(file);
    CHECK(length>0 && (unsigned long)length<=maximum);rewind(file);
    uint8_t *data=malloc((size_t)length);CHECK(data);
    CHECK(fread(data,1,(size_t)length,file)==(size_t)length);
    CHECK(fgetc(file)==EOF && !fclose(file));*bytes=(size_t)length;return data;
}

static void load_resolution(const char *image,const char *record) {
    size_t record_bytes;uint8_t digest[32],fresh[1024];
    startup=load_file(image,0xc00000u,&startup_bytes);
    uint8_t *wire=load_file(record,sizeof(fresh),&record_bytes);
    CHECK(record_bytes==sizeof(fresh));
    /* A digest from an untrusted input is not a replay authority. First run
     * complete discovery over the original executable and compare its wire. */
    CHECK(kui_vmu_native_resolution_cache_build(startup,startup_bytes,0x8c010000u,
        fresh,sizeof(fresh),NULL));
    CHECK(!memcmp(fresh,wire,sizeof(fresh)));
    CHECK(kui_vmu_native_resolution_cache_record_digest(fresh,sizeof(fresh),digest));
    CHECK(kui_vmu_native_resolution_cache_replay(startup,startup_bytes,0x8c010000u,
        wire,record_bytes,digest,&resolution));
    CHECK(resolution.layout.rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48);
    CHECK(physical(resolution.call.entry_begin)+resolution.call.entry_bytes-0x30000u==
        physical(SELF_ADDRESS));
    free(wire);
}

static void reserve_actual_heap(unsigned owner_alias) {
    const struct kui_vmu_native_heap *h=&resolution.heap;
    uint32_t begin=physical(resolution.call.entry_begin)|aliases[owner_alias];
    uint32_t bytes=resolution.call.entry_bytes;
    put(h->begin_cell,4u,begin);put(h->begin_copy_cell,4u,begin);put(h->bytes_cell,4u,bytes);
    put(h->dispatch_cell,4u,h->dispatch_table);
    put(h->dispatch_table,4u,h->allocate);put(h->dispatch_table+4u,4u,h->release);
    put(h->sentinel,4u,begin);put(h->sentinel+4u,4u,0u);put(h->cursor_cell,4u,h->sentinel);
    put(begin,4u,h->sentinel);put(begin+4u,4u,bytes/32u);
    struct kui_vmu_native_heap_lease lease;
    CHECK(kui_vmu_native_heap_fresh(h,read_heap,NULL,begin,bytes,1u,0x30000u,&lease));
    CHECK(kui_vmu_native_heap_preallocate(&lease,read_heap,NULL,1u));
    put(begin+4u,4u,lease.remaining);put(lease.header+4u,4u,lease.units);
    CHECK(kui_vmu_native_heap_postallocate(&lease,read_heap,NULL,1u,lease.payload));
    CHECK(kui_vmu_native_heap_lease_valid(&lease,read_heap,NULL,1u));
    input.lease=lease;input.runtime.lease_address=physical(lease.payload)|UINT32_C(0x80000000);
    kui_vmu_native_readonly_release_resume=h->release+20u;
}

static void first_transfer(unsigned owner_alias,unsigned manager_alias,unsigned pr_alias,
    unsigned bank,unsigned imask) {
    model_reset();fixture();
    CHECK(physical(resolution.driver.load_address)-UINT32_C(0x0c000000)+startup_bytes<=sizeof(memory));
    memcpy(memory+physical(resolution.driver.load_address)-UINT32_C(0x0c000000),
        startup,startup_bytes);
    input.driver=resolution.driver;input.layout=resolution.layout;input.exits=resolution.exits;
    input.tx_base=input.layout.tx_base;input.runtime.rx_layout=input.layout.rx_layout;
    input.runtime.rx_base=input.layout.rx_base;
    memcpy(input.runtime.rx_slots,input.layout.rx_slots,sizeof(input.runtime.rx_slots));
    reserve_actual_heap(owner_alias);
    manager=physical(input.driver.manager_initial)|aliases[manager_alias];
    memset(memory+physical(manager)-UINT32_C(0x0c000000),0,0x1100u);
    put(input.driver.manager_cell,4u,manager);
    uint32_t lock=physical(input.driver.lock_cell+4u)|UINT32_C(0xa0000000);
    put(input.driver.lock_cell,4u,lock);put(lock,1u,0x80u);
    put(manager+0xfc4u,4u,input.tx_base);put(manager+0xfc8u,4u,input.layout.rx_base);
    put(manager+0x10a8u,4u,input.tx_base);put(manager+0x10acu,4u,input.tx_base+0x3000u);
    current_bank=bank;source=physical(input.tx_base)+bank*0x3000u;
    put(manager+0x10b0u,4u,source|UINT32_C(0xa0000000));
    put(manager+0x10b4u,4u,bank);put(manager+0x10b8u,4u,bank);put(manager+0xfc0u,4u,1u);
    put(manager+0x10f4u,4u,UINT32_C(0xa05f6c00));
    for(unsigned i=0;i<48u;i++) put(manager+0xfe8u+i*4u,4u,input.layout.rx_slots[i]);
    put(input.layout.gun_flag,4u,0u);put(input.layout.timer1_callback,4u,0u);
    put(input.layout.timer2_callback,4u,0u);
    kui_vmu_native_passive_resume=input.driver.resume;
    kui_vmu_native_passive_consumer=input.driver.consumer;
    frame.r14=input.driver.manager_cell;frame.r12=1u;frame.sr=UINT32_C(0x60000101)|(imask<<4);
    frame.pr=physical(input.driver.completion_call+4u)|aliases[pr_alias];
    prepare_handoff();model_card.bus.transfer_block=block_transfer;
    lease_calls=block_callbacks=0u;corrupt_before_loan=corrupt_after_loan=0u;
    release_saw_unpublished=false;arm();install_hooks();
    for(unsigned port=0;port<4u;port++) {
        uint32_t rx=physical(input.layout.rx_slots[bank*24u+port*6u]);
        memset(memory+rx-UINT32_C(0x0c000000),0xa5,1024u);
        put(source+port*12u,4u,(port<<16)|(port==3u?UINT32_C(0x80000000):0u));
        put(source+port*12u+4u,4u,rx);
        put(source+port*12u+8u,4u,((port*64u)<<16)|((port*64u+32u)<<8)|1u);
    }
    put(UINT32_C(0xa05f6c04),4u,source);submit();
    CHECK(state.fault_at==source+0x24u && state.fault_value==UINT32_C(0x80030000));
    put(manager+0x10b4u,4u,bank^1u);put(manager+0x10b8u,4u,bank^1u);
    put(manager+0xfc0u,4u,0u);
    for(unsigned port=0;port<4u;port++)
        put(input.layout.rx_slots[bank*24u+port*6u],4u,port?UINT32_MAX:UINT32_C(0x1c200005));
    put(input.layout.rx_slots[bank*24u]+4u,4u,UINT32_C(0x01000000));
}

static void no_storage(void) {
    CHECK(!durable_card.reads && !durable_card.write_phases && !durable_card.commits);
    CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes);
}

static void attempt_complete(void) {
    if(setjmp(terminal)==0) kui_vmu_native_passive_complete(&frame);
}
static void guard_read_refusals(unsigned completion_reads) {
    for(unsigned at=1u;at<=completion_reads;at++) {
        first_transfer(1u,1u,1u,0u,15u);
        uint8_t before[4][1024];
        for(unsigned port=0;port<4u;port++)
            memcpy(before[port],memory+physical(input.layout.rx_slots[port*6u])-UINT32_C(0x0c000000),1024u);
        fail_read=bus_reads+at;
        attempt_complete();
        if(terminal_error!=0x43u) continue;
        ++guard_refusals;CHECK(state.pending && state.submitted==1u && !state.completed);
        CHECK((state.runtime_result&UINT32_C(0xffff0000))==UINT32_C(0x20000));
        CHECK(state.fault_expect!=UINT32_MAX);no_storage();
        for(unsigned port=0;port<4u;port++)
            CHECK(!memcmp(before[port],memory+physical(input.layout.rx_slots[port*6u])-UINT32_C(0x0c000000),1024u));
    }
    /* The two guards before publication each read the complete witness. */
    CHECK(guard_refusals==26u);
}

int main(int argc,char **argv) {
    CHECK(argc==3);load_resolution(argv[1],argv[2]);
    for(unsigned owner=0;owner<3u;owner++) for(unsigned m=0;m<3u;m++)
        for(unsigned pr=0;pr<3u;pr++) for(unsigned bank=0;bank<2u;bank++)
            for(unsigned mask=0;mask<2u;mask++) {
                first_transfer(owner,m,pr,bank,mask?15u:6u);complete_return();
                CHECK(state.completed==1u && !state.pending);no_storage();
                uint32_t rx=input.layout.rx_slots[current_bank*24u];
                CHECK(value_at(rx)==UINT32_C(0x1c210005));
            }
    unsigned complete_reads=measured_complete_reads;
    guard_read_refusals(complete_reads);free(startup);
    printf("Actual uniform native first transfer: %u checks,108 owner/manager/PR alias-bank-SR cases,"
        "%u context read refusals without RX/SD publication\n",checks,guard_refusals);
    return 0;
}
