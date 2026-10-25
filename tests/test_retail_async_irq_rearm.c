/* SPDX-License-Identifier: GPL-3.0-only */
/* Reproduce an SDK changing IPRB without replacing our still-live VBR.
 * Execute the actual reader and byte-correct card/DMA model. */
#include <assert.h>
#include <stdint.h>
static uint32_t clock_counter=5u;
uint32_t kui_retail_async_test_read32(uint32_t address) {
    assert(address==UINT32_C(0xffd8000c));
    uint32_t result=clock_counter;clock_counter-=19u;return result;
}
#define KUI_NATIVE_HYBRID_MAIN hybrid_suite_main
#include "test_retail_async_native_hybrid.c"

static void level_changed_without_vbr(unsigned field) {
    cooked_fixture=true;setup(GAME_VBR,X,48,false);mode(2048);
    int32_t token=request(45001u,64u,OUTPUT);
    CHECK(R.engine.active && hooked() && (hw.iprb&0xf0u)==0x10u);
    unsigned before=R.engine.stats.execs;
    uint16_t other=(uint16_t)(hw.iprb&~0xf0u);
    hw.iprb=(uint16_t)(other|field);
    /* A status query supplies no waiting read allowance. Its usual hook
     * admission must repair our SCI field while preserving every other one. */
    (void)gd(KUI_GD_DRIVE,STATUS,0u);
    CHECK(hooked() && hw.iprb==(uint16_t)(other|0x10u));
    CHECK(R.engine.release.sci==field && R.engine.stats.execs==before+1u);
    (void)gd(KUI_GD_DRIVE,STATUS,0u);
    CHECK(R.engine.stats.execs==before+1u); /* no needless repair/write */
    m.delay=0u;elapse();before=R.engine.stats.irq_blocks;
    unsigned entries=R.engine.stats.exec_int;
    CHECK(!kui_retail_async_irq());
    CHECK(R.engine.stats.irq_blocks-before==KUI_RETAIL_NATIVE_IRQ_BATCH_LIMIT);
    CHECK(R.engine.stats.exec_int==entries+1u);
    CHECK(finish(token,0,1)==KUI_GD_COMPLETED);compare(45001u,64u,OUTPUT);
    CHECK(!hooked() && hw.iprb==(uint16_t)(other|field));
}
int main(void) {
    level_changed_without_vbr(0u);
    level_changed_without_vbr(0x90u);
    hybrid_suite_main();
    printf("native IRQ rearm: %u checks; same-VBR cleared/changed SCI field, exact owner restoration and full CHECK/EXEC supply passed\n",checks);
    return 0;
}
