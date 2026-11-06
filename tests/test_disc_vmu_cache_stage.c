/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the production cache-admission and disc mapping control flow.
 * Core cache cryptographic/structural replay is tested independently; these
 * stubs isolate its installer boundary, especially partially copied misses. */
#include "kui/vmu_native_cache_seed.h"
#include "kui/vmu_native_resolution_cache.h"
#include "kui/retail_loader_layout.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#define KUI_NATIVE_PHYSICAL_DISC 1
#define KUI_NATIVE_DUAL_READERS 0
#define native_arena 0u
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static const uint8_t native_resolution_seed[KUI_VMU_NATIVE_CACHE_SEED_BYTES]={0};
static struct kui_vmu_native_resolution native_cached_resolution,record;
static struct kui_vmu_native_discovery native_driver;
static struct kui_vmu_native_layout passive_layout;
static struct kui_vmu_native_heap native_heap;
static struct kui_vmu_native_heap_call native_call;
static struct kui_vmu_native_exit native_exits;
static unsigned exec_bytes=0x1000u;
static struct {unsigned exec_bytes;} plan={0x1000u};
static unsigned display,native_probe;
static bool seed_ok,replay_ok,release_ok,allocation_ok,coherence_ok,driver_ok,heap_ok,call_ok;
static bool first_allocation_fails;
static unsigned releases,allocations,coherences,captures,drivers,heaps,calls,prepares,probes,reports;
static unsigned preclears;
static int preclear_result;
static uint32_t fresh_slot;
static uint32_t last_detail;
static jmp_buf refusal;
static void reset(void) {
    seed_ok=replay_ok=release_ok=allocation_ok=coherence_ok=driver_ok=heap_ok=call_ok=true;
    releases=allocations=coherences=captures=drivers=heaps=calls=prepares=probes=reports=0;
    preclears=0;preclear_result=0;first_allocation_fails=false;
    last_detail=0;
    memset(&record,0,sizeof(record));record.heap.family=KUI_VMU_NATIVE_HEAP_TAIL32;
    record.heap.layout=KUI_VMU_NATIVE_HEAP_DISPATCH2;
    record.heap.initializer_slot=0x8c123400u;record.driver.submit=0x8c123456u;
    record.layout.tx_base=0xac800000u;record.call.entry_begin=0x8c500000u;record.call.entry_bytes=0x10000u;
    record.exits.family=0xdeadbeefu;
    memset(&native_driver,0,sizeof(native_driver));memset(&passive_layout,0,sizeof(passive_layout));
    memset(&native_heap,0,sizeof(native_heap));memset(&native_call,0,sizeof(native_call));
    memset(&native_exits,0,sizeof(native_exits));fresh_slot=0x8c789000u;
}
bool kui_vmu_native_cache_seed_read(const void *seed,size_t bytes,const uint8_t **wire,const uint8_t **digest) {
    CHECK(seed==native_resolution_seed && bytes==sizeof(native_resolution_seed));
    if(!seed_ok)return false;
    *wire=native_resolution_seed;*digest=native_resolution_seed+64u;return true;
}
bool kui_vmu_native_resolution_cache_replay(const void *image,size_t bytes,uint32_t load,
    const void *wire,size_t wire_bytes,const uint8_t digest[32],struct kui_vmu_native_resolution *out) {
    CHECK((uintptr_t)image==KUI_RETAIL_EXEC_ADDRESS && bytes==exec_bytes && load==KUI_RETAIL_EXEC_ADDRESS);
    CHECK(wire==native_resolution_seed && wire_bytes==KUI_VMU_NATIVE_RESOLUTION_CACHE_BYTES && digest==native_resolution_seed+64u);
    if(!replay_ok)return false;
    *out=record;return true;
}
static bool readonly_release_replay_valid(void) {++releases;return release_ok;}
bool kui_vmu_native_heap_metadata_valid(const struct kui_vmu_native_heap *heap) {
    return (heap->family==KUI_VMU_NATIVE_HEAP_TAIL32 && heap->layout==KUI_VMU_NATIVE_HEAP_DISPATCH2) ||
        (heap->family==KUI_VMU_NATIVE_HEAP_DIRECT32 && heap->layout==KUI_VMU_NATIVE_HEAP_DIRECT &&
         !heap->dispatch_cell && !heap->dispatch_table);
}
static bool allocation_safe(uint32_t begin,uint32_t bytes) {
    ++allocations;CHECK(begin==0x8c500000u && bytes==0x10000u);return allocation_ok && !(first_allocation_fails && allocations==1u);
}
static bool native_sdk_coherence_cached_admit(void) {++coherences;return coherence_ok;}
static void native_passive_capture_windows(void) {++captures;}
static void retail_display_restore(const void *p) {CHECK(p==&display);}
static void retail_display_line(const char *s) {CHECK(s && *s);}
bool kui_vmu_native_discover_layout(const void *image,size_t bytes,uint32_t load,
    struct kui_vmu_native_discovery *driver,struct kui_vmu_native_layout *layout) {
    CHECK((uintptr_t)image==KUI_RETAIL_EXEC_ADDRESS && bytes==exec_bytes && load==KUI_RETAIL_EXEC_ADDRESS);
    ++drivers;memset(driver,0,sizeof(*driver));memset(layout,0,sizeof(*layout));
    driver->submit=0x8c333000u;layout->tx_base=0xac900000u;return driver_ok;
}
static int kui_disc_vmu_probe_run(const void *image,size_t bytes,uint32_t load,void *out) {
    (void)image;(void)bytes;(void)load;CHECK(out==&native_probe);++probes;return 0;
}
static enum kui_vmu_native_heap_result test_heap_discover(const void *image,size_t bytes,uint32_t load,
    struct kui_vmu_native_heap *heap) {
    (void)image;(void)bytes;(void)load;++heaps;memset(heap,0,sizeof(*heap));heap->initializer_slot=fresh_slot;
    heap->family=KUI_VMU_NATIVE_HEAP_TAIL32;heap->layout=KUI_VMU_NATIVE_HEAP_DISPATCH2;
    return heap_ok?KUI_VMU_NATIVE_HEAP_FOUND:KUI_VMU_NATIVE_HEAP_NONE;
}
static bool test_heap_call(const void *image,size_t bytes,const struct kui_vmu_native_heap *heap,
    struct kui_vmu_native_heap_call *call) {
    (void)image;(void)bytes;CHECK(heap==&native_heap);++calls;memset(call,0,sizeof(*call));
    call->entry_begin=0x8c500000u;call->entry_bytes=0x10000u;return call_ok;
}
#define kui_vmu_native_heap_discover test_heap_discover
#define kui_vmu_native_heap_call_discover test_heap_call
static void native_passive_prepare(void) {
    ++prepares;CHECK(native_driver.submit==0x8c333000u && passive_layout.tx_base==0xac900000u);
    CHECK(native_heap.initializer_slot==fresh_slot && native_call.entry_begin==0x8c500000u);
    /* The actual fresh prepare independently discovers exits and captures
     * their original windows. Poison from a late cache miss is discarded. */
    memset(&native_exits,0,sizeof(native_exits));native_exits.family=123u;
}
int kui_vmu_native_heap_preclear_plan(const void *image,size_t bytes,const uint8_t sha[32],
    const struct kui_vmu_native_heap *heap,const struct kui_vmu_native_heap_call *call,
    struct kui_vmu_native_heap_preclear *out) {
    CHECK((uintptr_t)image==KUI_RETAIL_EXEC_ADDRESS && bytes==exec_bytes && out);
    CHECK(sha==native_driver.image_sha256 && heap==&native_heap && call==&native_call);
    ++preclears;return preclear_result;
}
static void native_report(uint32_t phase,uint32_t detail) {
    CHECK(phase==2u);last_detail=detail;++reports;longjmp(refusal,1);
}
#include "disc-vmu-cache-stage-fixture.h"
static void mapping_refuses(void) {
    if(!setjmp(refusal)) {native_disc_mapping_prepare();CHECK(false);}
}
int main(void) {
    reset();CHECK(native_resolution_replay());CHECK(releases==1 && allocations==1 && coherences==1 && captures==1);
    reset();record.heap.family=KUI_VMU_NATIVE_HEAP_DIRECT32;record.heap.layout=KUI_VMU_NATIVE_HEAP_DIRECT;
    CHECK(native_resolution_replay());CHECK(releases==1 && allocations==1 && coherences==1 && captures==1);
    reset();record.heap.family=KUI_VMU_NATIVE_HEAP_DIRECT32;CHECK(!native_resolution_replay());CHECK(!releases && !captures);
    reset();record.heap.family=KUI_VMU_NATIVE_HEAP_DIRECT32;record.heap.layout=KUI_VMU_NATIVE_HEAP_DIRECT;
    record.heap.dispatch_cell=0x8c200000u;CHECK(!native_resolution_replay());CHECK(!releases && !captures);
    reset();record.heap.family=99u;CHECK(!native_resolution_replay());CHECK(!releases && !captures);
    reset();seed_ok=false;CHECK(!native_resolution_replay());CHECK(!releases && !captures);
    reset();replay_ok=false;CHECK(!native_resolution_replay());CHECK(!releases && !captures);
    reset();record.heap.family=KUI_VMU_NATIVE_HEAP_ARENA;CHECK(!native_resolution_replay());CHECK(!releases && !allocations && !captures);
    reset();record.heap.initializer_slot=0;CHECK(!native_resolution_replay());CHECK(!releases && !allocations && !captures);
    reset();record.heap.initializer=3u;CHECK(!native_resolution_replay());CHECK(!releases && !allocations && !captures);
    reset();record.heap.release=3u;CHECK(!native_resolution_replay());CHECK(!releases && !allocations && !captures);
    reset();native_disc_mapping_prepare();CHECK(!drivers && !heaps && !calls && !prepares && captures==1 && preclears==1);
    for(unsigned late=0;late<7;late++) {
        reset();
        if(late==0)seed_ok=false;else if(late==1)replay_ok=false;
        else if(late==2)release_ok=false;else if(late==3)coherence_ok=false;
        else if(late==4)record.heap.family=KUI_VMU_NATIVE_HEAP_ARENA;else if(late==5)record.heap.initializer_slot=0;else first_allocation_fails=true;
        native_disc_mapping_prepare();
        CHECK(drivers==1 && heaps==1 && calls==1 && prepares==1 && !reports && !captures);
        CHECK(native_exits.family==123u && native_driver.submit!=record.driver.submit && native_heap.initializer_slot!=record.heap.initializer_slot);
        CHECK(preclears==1u);if(late==6)CHECK(allocations==2u);
    }
    for(unsigned cached=0;cached<2u;cached++) for(int hazard=-1;hazard<=1;hazard+=2) {
        reset();seed_ok=cached!=0u;preclear_result=hazard;
        mapping_refuses();
        CHECK(reports==1 && preclears==1u);
        CHECK(last_detail==0x75u);
        CHECK(captures==(cached?1u:0u) && prepares==(cached?0u:1u));
        CHECK(drivers==(cached?0u:1u) && heaps==(cached?0u:1u));
    }
    for(unsigned failure=0;failure<5;failure++) {
        reset();seed_ok=false;
        if(failure==0)driver_ok=false;else if(failure==1)heap_ok=false;
        else if(failure==2)fresh_slot=0;else if(failure==3)call_ok=false;else allocation_ok=false;
        mapping_refuses();
        CHECK(reports==1 && !prepares && !captures && !preclears);
    }
    printf("Physical-disc mapping stage: %u checks; hit skips scanners, late misses rebuild metadata, wrong-kind cache refuses before entry reads\n",checks);
}
