/* SPDX-License-Identifier: GPL-3.0-only */
/* The real isolated worker keeps NORMAL's fast lane and dispatches a genuine
 * bound auxiliary TICK, while every virtual-card command remains absent. */
#define _GNU_SOURCE 1
#define KUI_NATIVE_READER_ONLY_ISOLATION 1
#define KUI_NATIVE_DUAL_READERS 1
#define KUI_NATIVE_READAHEAD 1
#define KUI_NATIVE_IRQ_REARM 1
#define KUI_NATIVE_COOP_PUMP 1
#define KUI_NATIVE_REQUEST_TIMING 1
#define KUI_NATIVE_READER_DIAGNOSTIC 1
#define KUI_VMU_NATIVE_BACKGROUND 1
#define KUI_VMU_NATIVE_CACHED_CODE 1
#define KUI_RETAIL_ASYNC_TEST 1
#define main persistent_regression_main
#include "test_vmu_native_persistent.c"
#undef main
#include <sys/mman.h>

static kui_retail_async_region_type observed_region;
static unsigned tick_visits;
/* The console ABI validates the entry's exact owned address before calling.
 * A separately placed host text section exercises that actual dispatch; it
 * avoids replacing the production getter or reader service with a mock. */
static uint32_t __attribute__((section(".isolation_tick"),noinline))
tick_visit(uint32_t event,uint32_t arg,kui_retail_async_region_type *region,uint32_t reserved) {
    CHECK(event==KUI_NATIVE_RA_TICK && !arg && !reserved && region==&observed_region);
    CHECK(state.armed && !state.pending && state.submitted==state.completed);
    CHECK(!durable_card.handling && !durable_card.inflight);
    ++tick_visits;return 1u;
}
void *kui_vmu_native_reader_test_region(uint32_t address) {
    CHECK(kui_vmu_native_background_region_address(config.lease.payload,address));
    return &observed_region;
}
static void map_exact(uintptr_t address,size_t bytes) {
    void *p=mmap((void *)address,bytes,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    CHECK(p==(void *)address);
}
static void reader_bind(void) {
    const uint32_t lease=UINT32_C(0x8c700000);
    struct kui_retail_manifest *owned=(void *)(uintptr_t)
        (lease+KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES);
    struct kui_native_ra_exports *a=(void *)(uintptr_t)
        ((lease+KUI_NATIVE_RA_AUX_OFFSET)|UINT32_C(0x20000000));
    memset(&observed_region,0,sizeof(observed_region));
    memset(owned,0,sizeof(*owned));memset(a,0,sizeof(*a));
    owned->reader=KUI_RETAIL_READER_ASYNC;owned->storage_transport=KUI_STORAGE_SCI;
    observed_region.engine.manifest=owned;
    observed_region.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]=KUI_NATIVE_SDK_SCI_COOKIE;
    a->magic=KUI_NATIVE_RA_AUX_MAGIC;a->version=1u;a->bytes=sizeof(*a);
    a->visit=tick_visit;a->context=(void *)((uintptr_t)a+256u);
    a->image_end=lease+KUI_NATIVE_RA_AUX_OFFSET+0x2000u;
    CHECK((uintptr_t)tick_visit==lease+KUI_NATIVE_RA_AUX_OFFSET+0x1000u);
    observed_region.vector600[KUI_NATIVE_RA_ENTRY_INDEX]=(uint32_t)(uintptr_t)a->visit;
    observed_region.vector600[KUI_NATIVE_RA_CONTEXT_INDEX]=(uint32_t)(uintptr_t)a->context;
    CHECK(reader_readahead_exports(&observed_region)==a);
}
static void reader_only_setup(unsigned mode) {
    model_reset();fixture();
    /* Preserve the fixed worker payload, extending this fixture's exact heap
     * to own the production340KiB lease and its192KiB cache arena. */
    struct kui_vmu_native_heap_lease *l=&input.lease;
    l->begin=0x0c680000u;l->heap_bytes=0x80000u+KUI_NATIVE_RA_LEASE_BYTES;l->lease_bytes=KUI_NATIVE_RA_LEASE_BYTES;
    l->units=KUI_NATIVE_RA_LEASE_BYTES/32u+1u;l->remaining=0x3fffu;
    put(l->owner.begin_cell,4u,l->begin);put(l->owner.begin_copy_cell,4u,l->begin);
    put(l->owner.bytes_cell,4u,l->heap_bytes);put(l->owner.sentinel,4u,l->begin);
    put(l->begin,4u,l->owner.sentinel);put(l->begin+4u,4u,l->remaining);
    put(l->header+4u,4u,l->units);
    input.runtime.lease_bytes=input.runtime.required_lease_bytes=l->lease_bytes;
#if KUI_NATIVE_E1_WAIT_CAPTURE
    static const uint8_t e1_sha[32]={
        0xff,0xb5,0xa8,0xe5,0xff,0x55,0x47,0x02,0x6d,0x93,0x64,0x9e,0x1f,0x90,0x58,0xb7,
        0xee,0x5e,0x29,0xd8,0xd0,0xff,0x8a,0x89,0x33,0x57,0x01,0xf7,0xe9,0x64,0x7f,0x35};
    input.driver.image_bytes=input.exits.image_bytes=l->owner.image_bytes=2143652u;
    memcpy(input.driver.image_sha256,e1_sha,sizeof(e1_sha));
#endif
    input.diagnostics=mode;prepare_handoff();model_card.bus.transfer_block=block_transfer;
    lease_calls=block_callbacks=0u;corrupt_before_loan=corrupt_after_loan=0u;
    release_saw_unpublished=false;arm();install_hooks();reader_bind();
    CHECK(config.lease.lease_bytes==KUI_NATIVE_RA_LEASE_BYTES &&
        KUI_NATIVE_RA_DATA_BYTES==0x30000u);
}
static void no_virtual_or_storage(void) {
    CHECK(!overlay.stats.advertisements && !overlay.stats.backend_calls && !overlay.stats.replies);
    CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes);
    CHECK(!durable_card.reads && !durable_card.commits && !durable_card.commit_attempts);
    idle();
}
static void absent_batch(unsigned command,unsigned phase) {
    uint8_t payload[128];memset(payload,0x62,sizeof(payload));
    unsigned before_ticks=tick_visits;
    bool fast=!command && live_plan.ready;
    begin_batch(command,command==10u?0u:42u,phase,command==12u?payload:NULL);
    CHECK((controller_pair.pending!=0u)==fast);
    physical_complete();
    uint8_t parent[1024],target[1024],other[1024];
    memcpy(parent,rx_bytes(0u),sizeof(parent));memcpy(target,rx_bytes(2u),sizeof(target));
    memcpy(other,rx_bytes(6u),sizeof(other));
    struct kui_vmu_durable before_card=durable_card;
    complete_return();
    CHECK(!memcmp(parent,rx_bytes(0u),sizeof(parent)) && !(rx_bytes(0u)[2]&2u));
    CHECK(!memcmp(other,rx_bytes(6u),sizeof(other)));
    if(command) {
        CHECK(value_at(rx_at(2u))==UINT32_MAX);
        CHECK(!memcmp(target+4u,rx_bytes(2u)+4u,sizeof(target)-4u));
    } else CHECK(!memcmp(target,rx_bytes(2u),sizeof(target)));
    CHECK(!memcmp(&before_card,&durable_card,sizeof(before_card)));
    CHECK(tick_visits==before_ticks+((fast && !KUI_NATIVE_RA_TICK_OFF_ISOLATION)?1u:0u));
    CHECK(value_at(0xa05f6c04u)==state.source);no_virtual_or_storage();
}
#ifndef KUI_READER_ONLY_TEST_MAIN
#define KUI_READER_ONLY_TEST_MAIN main
#endif
int KUI_READER_ONLY_TEST_MAIN(void) {
    map_exact(UINT32_C(0x8c720000),4096u);
    map_exact(UINT32_C(0xac721000),4096u);
    for(unsigned mode=KUI_VMU_NATIVE_DIAGNOSTICS_NORMAL;
        mode<=KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE;mode++) {
        reader_only_setup(mode);
        absent_batch(0u,0u); /* First complete proof still uses the full path. */
        for(unsigned repeat=0u;repeat<4u;repeat++) {
            absent_batch(0u,0u);absent_batch(1u,0u);absent_batch(2u,0u);
            absent_batch(3u,0u);absent_batch(10u,0u);absent_batch(11u,0u);
            for(unsigned phase=0u;phase<4u;phase++) absent_batch(12u,phase);
            absent_batch(13u,1u);absent_batch(13u,4u);
        }
    }
    /* Existing conflict/context checks still reject before absent publication. */
    for(unsigned fault=0u;fault<2u;fault++) {
        reader_only_setup(KUI_VMU_NATIVE_DIAGNOSTICS_NORMAL);
        begin_batch(11u,42u,0u,NULL);physical_complete();
        if(!fault) rx_bytes(0u)[2]|=2u;
        else put(input.driver.completion_slot,4u,kui_vmu_native_worker_exports.complete_hook+2u);
        uint8_t before[3][1024];
        memcpy(before[0],rx_bytes(0u),1024u);memcpy(before[1],rx_bytes(2u),1024u);
        memcpy(before[2],rx_bytes(6u),1024u);reject_complete();
        CHECK(!memcmp(before[0],rx_bytes(0u),1024u) && !memcmp(before[1],rx_bytes(2u),1024u));
        CHECK(!memcmp(before[2],rx_bytes(6u),1024u));no_virtual_or_storage();
    }
    CHECK(tick_visits==(KUI_NATIVE_RA_TICK_OFF_ISOLATION?0u:8u));
    printf("Reader-only isolation%s: %u checks; no A2 advertisement, protocol backend or SD IO; %u NORMAL/PROFILE TICK visits; physical/conflict guards preserved\n",
        KUI_NATIVE_RA_TICK_OFF_ISOLATION?" / prefetch off":"",checks,tick_visits);
    return 0;
}
