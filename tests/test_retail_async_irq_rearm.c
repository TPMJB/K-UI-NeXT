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
    CHECK(R.engine.active && hooked() && (hw.iprb&0xf0u)==KUI_RETAIL_SCI_PRIORITY_FIELD);
    unsigned before=R.engine.stats.execs;
    unsigned owner=field==KUI_RETAIL_SCI_PRIORITY_FIELD?R.engine.release.sci:field;
    uint16_t other=(uint16_t)(hw.iprb&~0xf0u);
    hw.iprb=(uint16_t)(other|field);
    /* A status query supplies no waiting read allowance. Its usual hook
     * admission must repair our SCI field while preserving every other one. */
    (void)gd(KUI_GD_DRIVE,STATUS,0u);
    CHECK(hooked() && hw.iprb==(uint16_t)(other|KUI_RETAIL_SCI_PRIORITY_FIELD));
    /* A field equal to the reader's has no observable owner change. Keep
     * the admitted prior owner, rather than pretending a repair happened. */
    CHECK(R.engine.release.sci==owner &&
        R.engine.stats.execs==before+(field!=KUI_RETAIL_SCI_PRIORITY_FIELD));
    (void)gd(KUI_GD_DRIVE,STATUS,0u);
    CHECK(R.engine.stats.execs==before+(field!=KUI_RETAIL_SCI_PRIORITY_FIELD));
    m.delay=0u;elapse();before=R.engine.stats.irq_blocks;
    unsigned entries=R.engine.stats.exec_int;
    CHECK(!kui_retail_async_irq());
    CHECK(R.engine.stats.irq_blocks-before==KUI_RETAIL_NATIVE_IRQ_BATCH_LIMIT);
    CHECK(R.engine.stats.exec_int==entries+1u);
    CHECK(finish(token,0,1)==KUI_GD_COMPLETED);compare(45001u,64u,OUTPUT);
    CHECK(!hooked() && hw.iprb==(uint16_t)(other|owner));
}
static void initial_owner_field(unsigned field) {
    cooked_fixture=true;setup(GAME_VBR,X,48,false);mode(2048);
    uint16_t original=(uint16_t)((hw.iprb&~0xf0u)|field);
    hw.iprb=original;
    int32_t token=request(45001u,64u,OUTPUT);
    CHECK(hooked() && R.engine.release.sci==field && hw.iprb==READER_IPRB);
    foreign_event_with(0u,NULL);
    CHECK(!hooked() && hw.iprb==original && hw.vbr==GAME_VBR);
    (void)gd(KUI_GD_DRIVE,STATUS,0u);
    CHECK(hooked() && hw.iprb==READER_IPRB && R.engine.release.sci==field);
    foreign_event_with(2u,NULL);
    CHECK(hooked() && hw.iprb==READER_IPRB && R.engine.release.sci==field &&
        R.engine.release.rehooks==1u);
    CHECK(finish(token,1u,1u)==KUI_GD_COMPLETED);compare(45001u,64u,OUTPUT);
    CHECK(!hooked() && hw.iprb==original && hw.vbr==GAME_VBR);
}
static void interrupt_admission(void) {
    for(unsigned blocked=0u;blocked<2u;++blocked)
        for(unsigned mask=0u;mask<16u;++mask) {
            cooked_fixture=true;setup(GAME_VBR,X,48,false);mode(2048);
            int32_t token=request(45001u,64u,OUTPUT);
            m.delay=0u;elapse();
            hw.sr=0x60000100u|(mask<<4)|(blocked<<28);
            unsigned before=R.engine.stats.exec_int,blocks=R.engine.stats.irq_blocks;
            unsigned admitted=!blocked && mask<(KUI_RETAIL_SCI_PRIORITY_FIELD>>4);
            CHECK(interrupts(1u)==admitted);
            CHECK(R.engine.stats.exec_int==before+admitted);
            CHECK(R.engine.stats.irq_blocks-blocks==
                (admitted?KUI_RETAIL_NATIVE_IRQ_BATCH_LIMIT:0u));
            hw.sr=0u;
            CHECK(finish(token,1u,1u)==KUI_GD_COMPLETED);compare(45001u,64u,OUTPUT);
        }
}
int main(void) {
    for(unsigned field=0u;field<0x100u;field+=0x10u) {
        initial_owner_field(field);
        level_changed_without_vbr(field);
    }
    interrupt_admission();
    hybrid_suite_main();
    printf("native IRQ level %u: %u checks; all16 owner fields, rearm/rehook/restoration, modelled BL/IMASK admission and full CHECK/EXEC supply passed\n",
        KUI_RETAIL_SCI_PRIORITY_FIELD>>4,checks);
    return 0;
}
