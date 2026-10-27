/* SPDX-License-Identifier: GPL-3.0-only */
/* Replay the first four-port DEVINFO transaction with an external executable's
 * complete, freshly reverified discovery record. No retail bytes are embedded.
 * The shared fixture supplies only the host RAM/MMIO/SD bus and real worker;
 * every game address, layout, allocator and hook target comes from discovery.
 */
#include <stdlib.h>
#ifndef KUI_VMU_NATIVE_TEST_SELF_ADDRESS
#if KUI_NATIVE_READAHEAD
#define KUI_VMU_NATIVE_TEST_SELF_ADDRESS UINT32_C(0xacfa0000)
#else
#define KUI_VMU_NATIVE_TEST_SELF_ADDRESS UINT32_C(0xacfd0000)
#endif
#endif
#define main native_persistent_reference_main
#include "test_vmu_native_persistent.c"
#undef main
#include "kui/vmu_native_resolution_cache.h"
#if KUI_VMU_NATIVE_ENLARGED_LEASE_TEST
#include "kui/vmu_native_stage_placement.h"
#include "kui/vmu_maple_observe_boot.h"
#include "vmu_native_background_guards.h"
#endif

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
    CHECK(physical(resolution.call.entry_begin)+resolution.call.entry_bytes-
        KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES==
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
    CHECK(kui_vmu_native_heap_fresh(h,read_heap,NULL,begin,bytes,1u,
        KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES,&lease));
    CHECK(kui_vmu_native_heap_preallocate(&lease,read_heap,NULL,1u));
    put(begin+4u,4u,lease.remaining);put(lease.header+4u,4u,lease.units);
    CHECK(kui_vmu_native_heap_postallocate(&lease,read_heap,NULL,1u,lease.payload));
    CHECK(kui_vmu_native_heap_lease_valid(&lease,read_heap,NULL,1u));
    input.lease=lease;input.runtime.lease_address=physical(lease.payload)|UINT32_C(0x80000000);
    input.runtime.lease_bytes=lease.lease_bytes;
#if KUI_NATIVE_READAHEAD
    input.runtime.required_lease_bytes=KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES;
#endif
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

#if KUI_VMU_NATIVE_ENLARGED_LEASE_TEST
static unsigned destination_refusals,lifetime_refusals,release_hook_refusals,placement_cases;
static void release_hook_refuse(uint32_t payload) {
    frame.r4=payload;terminal_error=0u;
    if(setjmp(terminal)==0) {kui_vmu_native_readonly_release_check(&frame);CHECK(false);}
    CHECK(terminal_error==0x30u);++release_hook_refusals;no_storage();
}
static void selected_lease_contracts(uint32_t stage_begin,uint32_t stage_end) {
    first_transfer(1u,1u,1u,0u,15u);complete_return();
    const struct kui_vmu_native_heap_lease *lease=&input.lease;
    uint32_t payload=physical(lease->payload),header=physical(lease->header);
    uint32_t end=payload+lease->lease_bytes;
    CHECK(lease->lease_bytes==KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES);
    CHECK(payload==physical(SELF_ADDRESS) && end==UINT32_C(0x0d000000));
    CHECK(lease->units==lease->lease_bytes/32u+1u);
    CHECK(kui_vmu_native_heap_lease_valid(lease,read_heap,NULL,1u));
    CHECK(kui_vmu_native_heap_lease_identity_valid(lease,read_heap,NULL,1u));
    CHECK(!kui_vmu_native_heap_lease_valid(lease,read_heap,NULL,2u));
    CHECK(!kui_vmu_native_lifetime_release_prepared(&lifetime_plan,read_heap,NULL,2u,0u));
    frame.r4=0u;kui_vmu_native_readonly_release_check(&frame);
    /* The actual linked installer bounds, not an invented short stage, must
     * exclude the whole selected allocation/header for every owner alias. */
    for(unsigned a=0;a<3u;a++) {
        uint32_t begin=physical(resolution.call.entry_begin)|aliases[a];
        CHECK(kui_vmu_native_stage_allocation_safe(&resolution.heap,(uint32_t)startup_bytes,
            stage_begin,stage_end,begin,resolution.call.entry_bytes));++placement_cases;
        const uint32_t at[]={header,payload,payload+UINT32_C(0x20000),end-1u};
        for(unsigned i=0;i<sizeof(at)/sizeof(at[0]);i++) {
            CHECK(!kui_vmu_native_stage_allocation_safe(&resolution.heap,(uint32_t)startup_bytes,
                at[i]|aliases[a],(at[i]+1u)|aliases[a],begin,resolution.call.entry_bytes));
            ++placement_cases;
        }
        /* Extending a stage by one byte into the allocation must refuse. */
        CHECK(kui_vmu_native_stage_allocation_safe(&resolution.heap,(uint32_t)startup_bytes,
            (header-32u)|aliases[a],header|aliases[a],begin,resolution.call.entry_bytes));++placement_cases;
        CHECK(!kui_vmu_native_stage_allocation_safe(&resolution.heap,(uint32_t)startup_bytes,
            (header-32u)|aliases[a],(header+1u)|aliases[a],begin,resolution.call.entry_bytes));++placement_cases;
    }
    struct kui_vmu_observe_boot_control c={.status=KUI_VMU_OBS_BOOT_INSTALLED,
        .lease=payload,.worker_end=SELF_ADDRESS+WORKER_SPAN,
        .terminal=(physical(SELF_ADDRESS)|
#if KUI_VMU_NATIVE_CACHED_CODE
            UINT32_C(0x80000000)
#else
            UINT32_C(0xa0000000)
#endif
            )+0x400u};
    c.sd_transaction=c.terminal+0x100u;
    uint32_t guarded_begin,guarded_end;
    CHECK(native_resident_lease(&c,&guarded_begin,&guarded_end));
    CHECK(guarded_begin==header && guarded_end==end);
    CHECK(native_resident_terminal(&c));CHECK(native_resident_transaction(&c,c.sd_transaction));
    c.terminal^=UINT32_C(0x20000000);
    CHECK(!native_resident_terminal(&c));c.terminal^=UINT32_C(0x20000000);
    CHECK(!native_resident_transaction(&c,c.sd_transaction^UINT32_C(0x20000000)));
    for(unsigned a=0;a<3u;a++) {
        CHECK(native_resident_destination(&c,(header-1u)|aliases[a],1u));
        CHECK(!native_resident_destination(&c,(header-1u)|aliases[a],2u));++destination_refusals;
        for(uint32_t at=header;at<end;at+=32u) {
            CHECK(!native_resident_destination(&c,at|aliases[a],1u));++destination_refusals;
            CHECK(!kui_vmu_native_lifetime_release_safe(lease,read_heap,NULL,1u,at|aliases[a]));
            CHECK(!kui_vmu_native_lifetime_release_prepared(&lifetime_plan,read_heap,NULL,1u,at|aliases[a]));
            ++lifetime_refusals;
        }
        CHECK(!native_resident_destination(&c,(end-1u)|aliases[a],1u));++destination_refusals;
        CHECK(!native_resident_destination(&c,end|aliases[a],1u));++destination_refusals;
        const uint32_t releases[]={payload,payload+0x20000u,payload+lease->lease_bytes-32u};
        for(unsigned i=0;i<sizeof(releases)/sizeof(releases[0]);i++)release_hook_refuse(releases[i]|aliases[a]);
    }
    /* Identity/header, free-list and installed reinitializer interception
     * remain mandatory before original release code can run. */
    uint32_t old=value_at(lease->header+4u);put(lease->header+4u,4u,old+1u);
    release_hook_refuse(0u);put(lease->header+4u,4u,old);
    old=value_at(lease->begin+4u);put(lease->begin+4u,4u,old+1u);
    release_hook_refuse(0u);put(lease->begin+4u,4u,old);
    old=value_at(lease->owner.initializer_slot);put(lease->owner.initializer_slot,4u,old+2u);
    release_hook_refuse(0u);put(lease->owner.initializer_slot,4u,old);
    frame.r4=0u;kui_vmu_native_readonly_release_check(&frame);no_storage();
    printf("Selected Evolution lease %u KiB: destination=%u lifetime=%u release-hook=%u placement=%u\n",
        lease->lease_bytes/1024u,destination_refusals,lifetime_refusals,release_hook_refusals,placement_cases);
}
#endif

int main(int argc,char **argv) {
#if KUI_VMU_NATIVE_ENLARGED_LEASE_TEST
    CHECK(argc==5);load_resolution(argv[1],argv[2]);
    char *end1,*end2;
    unsigned long stage_begin=strtoul(argv[3],&end1,0),stage_end=strtoul(argv[4],&end2,0);
    CHECK(!*end1 && !*end2 && stage_begin<=UINT32_MAX && stage_end<=UINT32_MAX);
#else
    CHECK(argc==3);load_resolution(argv[1],argv[2]);
#endif
    for(unsigned owner=0;owner<3u;owner++) for(unsigned m=0;m<3u;m++)
        for(unsigned pr=0;pr<3u;pr++) for(unsigned bank=0;bank<2u;bank++)
            for(unsigned mask=0;mask<2u;mask++) {
                first_transfer(owner,m,pr,bank,mask?15u:6u);complete_return();
                CHECK(state.completed==1u && !state.pending);no_storage();
                uint32_t rx=input.layout.rx_slots[current_bank*24u];
                /* Current generic adapter advertises A2 while preserving
                 * the physical A1 presence bit. The older fixture expected
                 * the former virtual-A1 source byte0x21. */
                CHECK(value_at(rx)==UINT32_C(0x1c220005));
            }
    unsigned complete_reads=measured_complete_reads;
    guard_read_refusals(complete_reads);
#if KUI_VMU_NATIVE_ENLARGED_LEASE_TEST
    selected_lease_contracts((uint32_t)stage_begin,(uint32_t)stage_end);
#endif
    free(startup);
    printf("Actual uniform native first transfer: %u checks,108 owner/manager/PR alias-bank-SR cases,"
        "%u context read refusals without RX/SD publication\n",checks,guard_refusals);
    return 0;
}
