/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the established synthetic MMU/cache/device fixtures, then change
 * the wire target and extension owner to Swirl's independently reviewed A1.
 * The original A2 tests remain unchanged and run separately. */
#define main a2_fixture_test_main
#include "test_armada_ce_maple_rx_slice.c"
#undef main

static void a1_prepare(bool generic) {
    prepare(generic);
    put(EXT+8u,(UNIT+0x20u)&0x1ffffffu);
    put(EXT+12u,0u);
    uint32_t header=((generic?2u:0u)<<24)|(1u<<8)|(generic?11u:1u);
    put((TX+24u)|0xa0000000u,header);
    CHECK(!kui_armada_ce_maple_traffic_visit(0x30a60u,TTB,PROC,0x105u,
        read32,read32,read32,&f,&capture,&traffic));
    f.calls=0;
    for(unsigned i=0;i<f.cells;i++)f.cell[i].visits=0;
}

int main(void) {
    _Static_assert(KUI_CE_VMU_ADDRESS==1u && KUI_CE_VMU_UNIT_INDEX==1u &&
        KUI_CE_VMU_ATTACHMENT_BIT==0x10000u,"Swirl's source requires A1");
    a1_prepare(false);
    CHECK(!run(1u,116u));
    CHECK(out.unit_index==1u && out.unit_va==UNIT+0x20u &&
        out.function_va==FN+0x100u && out.descriptor[3]==0x101u);
    CHECK(!run(0u,16u) && out.unit_index==0u);
    a1_prepare(true);
    CHECK(!run(1u,524u) && out.unit_index==1u && out.dispatch==2u &&
        out.pending_irp==0x40000u && out.capacity==524u);
    /* The adjacent physical A2 descriptor/owner is outside this profile's
     * admission, even when it would otherwise be a valid request. */
    prepare(false);
    CHECK(run(1u,116u)==KUI_ARMADA_CE_MAPLE_RX_SLICE_ARGUMENT && !f.calls);
    a1_prepare(false);
    put(EXT+8u,0u);put(EXT+12u,(UNIT+0x20u)&0x1ffffffu);
    CHECK(run(1u,116u)!=KUI_ARMADA_CE_MAPLE_RX_SLICE_OK);
    /* Preserve pending-IRP and repeated owner checks on the selected unit. */
    a1_prepare(true);put(FN+0x11cu,0u);
    CHECK(run(1u,524u)==KUI_ARMADA_CE_MAPLE_RX_SLICE_METADATA);
    a1_prepare(false);
    unsigned cell=put(EXT+8u,(UNIT+0x20u)&0x1ffffffu);
    f.mutate=cell+1u;f.mutate_visit=2u;f.replacement=0u;
    CHECK(run(1u,116u)==KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED);
    printf("Swirl A1 source owner slice checks: %u\n",checks);
    return 0;
}
