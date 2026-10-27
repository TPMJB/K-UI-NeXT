/* SPDX-License-Identifier: GPL-3.0-only */
/* The general split controller with prefetch restored: every proved split
 * completion dispatches exactly one genuine bound auxiliary TICK after its
 * publication; refused completions and full-path fallbacks dispatch none. */
#define _GNU_SOURCE 1
#define KUI_NATIVE_READAHEAD 1
#define KUI_NATIVE_IRQ_REARM 1
#define KUI_NATIVE_COOP_PUMP 1
#define KUI_NATIVE_REQUEST_TIMING 1
#define KUI_NATIVE_READER_DIAGNOSTIC 1
#define KUI_VMU_NATIVE_BACKGROUND 1
#define KUI_RETAIL_ASYNC_TEST 1
#define KUI_SPLIT_CONTROLLER_TEST_MAIN split_controller_main
#include "test_vmu_native_split_controller.c"
#include <sys/mman.h>

static kui_retail_async_region_type observed_region;
static unsigned tick_visits;
/* As in the reader-only test, a separately placed host text section is the
 * exact owned entry address the production getter validates before calling. */
static uint32_t __attribute__((section(".isolation_tick"),noinline))
tick_visit(uint32_t event,uint32_t arg,kui_retail_async_region_type *region,uint32_t reserved) {
    CHECK(event==KUI_NATIVE_RA_TICK && !arg && !reserved && region==&observed_region);
    CHECK(state.armed && !state.pending && state.submitted==state.completed);
    CHECK(controller_pair.pending==NATIVE_CONTROLLER_IDLE && live_plan.ready);
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
}
/* The production getter validates against the armed lease, which each case
 * arms itself; it is checked once the case has run, while still armed. */
static void tick_case(void (*run)(void),unsigned expected) {
    reader_bind();
    const unsigned before=tick_visits;
    run();
    CHECK(tick_visits==before+expected);
    CHECK(reader_readahead_exports(&observed_region)==(const void *)(uintptr_t)
        ((UINT32_C(0x8c700000)+KUI_NATIVE_RA_AUX_OFFSET)|UINT32_C(0x20000000)));
}
static void prefetch_suite(void) {
    tick_case(split_success,8u);              /* One TICK per proved split completion. */
    tick_case(split_geometry_refusals,0u);
    tick_case(split_deferred_refusals,0u);    /* Refused completions publish nothing. */
    tick_case(split_full_fallback,0u);
    tick_case(split_warm_tag_refusals,2u);    /* Only each first, proved completion. */
}
int main(void) {
    map_exact(UINT32_C(0x8c720000),4096u);
    map_exact(UINT32_C(0xac721000),4096u);
    prefetch_suite();
    split_other_identity=true;prefetch_suite();split_other_identity=false;
    for(unsigned mode=KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE;
        mode<=KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE;mode++) {
        split_prepare();input.diagnostics=mode;
        CHECK(kui_vmu_native_passive_arm(&input,NULL)==-2 && !state.armed);
    }
    CHECK(tick_visits==20u);
    printf("Split controller prefetch host checks: %u; %u TICK visits, none after a refusal\n",
        checks,tick_visits);
    return 0;
}
