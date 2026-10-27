/* SPDX-License-Identifier: GPL-3.0-only */
/* Run the actual C admission with distinguishable P1/P2 canary backing. */
#define KUI_VMU_NATIVE_PASSIVE_HOST_TEST 1
#define KUI_VMU_NATIVE_READONLY_PROBE 1
#define KUI_VMU_NATIVE_PERSISTENT_PROBE 1
#include "../src/loader/vmu_native_passive.c"
#include <assert.h>
#include <stdio.h>

static uint32_t test_sp,test_sr,expected_guard_base;
static unsigned guard_reads,corrupt_guard;
uint32_t kui_vmu_native_passive_test_sr(void) {return test_sr;}
uint32_t kui_vmu_native_passive_test_sp(void) {return test_sp;}
uint32_t kui_vmu_native_passive_test_guard(uint32_t address,unsigned i) {
    /* A wrong alias cannot accidentally see the same simulated canary. */
    assert(i<16u && address==expected_guard_base+4u*i);
    ++guard_reads;return i+1u==corrupt_guard?0u:GUARD;
}
static void accepts(uint32_t sp) {
    test_sp=sp;guard_reads=0u;
    assert(stack_valid());assert(guard_reads==16u);
}
static void rejects(uint32_t sp) {
    test_sp=sp;guard_reads=0u;
    assert(!stack_valid());assert(!guard_reads);
}
int main(void) {
    const uint32_t p2_low=SELF_ADDRESS+WORKER_SPAN-0x1000u;
    const uint32_t p1_low=p2_low-0x20000000u;
    test_sr=0xf0u;state.armed=0u;expected_guard_base=p2_low;
    /* Installer contract is exact P2 even in a cached-runtime build. */
    assert(STACK_LOW==p2_low && STACK_HIGH==p2_low+0x1000u);
    accepts(p2_low+256u);accepts(p2_low+0xffcu);
    rejects(p2_low+252u);rejects(p2_low+0x1000u);
    rejects(p1_low+0x800u);rejects((p2_low&0x1fffffffu)+0x800u);
    state.armed=1u;
#if KUI_VMU_NATIVE_CACHED_CODE
    expected_guard_base=p1_low;
    rejects(p2_low+0x800u);
#else
    expected_guard_base=p2_low;
    rejects(p1_low+0x800u);
#endif
    const uint32_t low=expected_guard_base;
    accepts(low+256u);accepts(low+0xffcu);
    rejects(low+252u);rejects(low+0x1000u);
    rejects((p2_low&0x1fffffffu)+0x800u);
    /* All inherited non-15 masks refuse before following canary pointers. */
    for(unsigned mask=0;mask<15u;mask++) {
        test_sr=mask<<4;rejects(low+0x800u);
    }
    test_sr=0xf0u;test_sp=low+0x800u;
    for(unsigned i=1u;i<=16u;i++) {
        corrupt_guard=i;guard_reads=0u;
        assert(!stack_valid());assert(guard_reads==i);
    }
    corrupt_guard=0u;accepts(low+0x800u);
    printf("Native private stack: cached=%u exact cold/runtime aliases, bounds, IRQs and16 canaries passed\n",
        (unsigned)KUI_VMU_NATIVE_CACHED_CODE);
    return 0;
}
