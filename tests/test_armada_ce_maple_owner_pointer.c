/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_maple_owner_pointer.h"
#include <assert.h>
#include <stdio.h>

static unsigned cases;
static void refused(uint32_t raw,uint32_t slot,uint32_t bytes) {
    uint32_t out=UINT32_MAX;
    assert(!kui_armada_ce_maple_owner_pointer(raw,slot,bytes,&out) && !out);
    ++cases;
}
static void accepted(uint32_t raw,uint32_t slot,uint32_t bytes,uint32_t expected) {
    uint32_t out=UINT32_MAX;
    assert(kui_armada_ce_maple_owner_pointer(raw,slot,bytes,&out) && out==expected);
    ++cases;
}
int main(void) {
    /* Photographed raw owner root, with the requested complete extension
     * metadata span. Canonical DLL-root storage is a separate address. */
    accepted(0x00030a60u,0x08000000u,0x198u,0x08030a60u);
    accepted(0x08030a60u,0x08000000u,0x198u,0x08030a60u);
    refused(0x03dd72b8u,0x08000000u,0x198u);
    for(unsigned i=1u;i<=32u;i++) {
        uint32_t slot=i*0x02000000u;
        accepted(0x00030a60u,slot,0x198u,slot|0x00030a60u);
        accepted(slot|0x00030a60u,slot,0x198u,slot|0x00030a60u);
        accepted(0x00010004u,slot,4u,slot|0x00010004u);
        accepted(0x01fffffcu,slot,4u,slot|0x01fffffcu);
        refused(0x01fffffcu,slot,8u);
        refused(slot|0x00010000u,slot,4u);
        uint32_t other=i==32u?0x02000000u:slot+0x02000000u;
        refused(other|0x00030a60u,slot,0x198u);
    }
    static const uint32_t bad_slot[]={0u,1u,0x01000000u,0x03000000u,
        0x40000004u,0x42000000u,0x80000000u,0xffffffffu};
    for(unsigned i=0;i<sizeof(bad_slot)/sizeof(*bad_slot);i++)
        refused(0x00030a60u,bad_slot[i],0x198u);
    static const uint32_t bad_raw[]={0u,4u,0xfffcu,0x10000u,0x10001u,0x30a61u,
        0x08000000u,0x0800fffcu,0x08010000u,0x80030a60u,0x8c030a60u,
        0xac030a60u,0xcc030a60u,0xa05f6c88u,0xfffffffcu};
    for(unsigned i=0;i<sizeof(bad_raw)/sizeof(*bad_raw);i++)
        refused(bad_raw[i],0x08000000u,0x198u);
    static const uint32_t bad_bytes[]={0u,1u,2u,3u,5u,0x02000000u,0xfffffffcu,0xffffffffu};
    for(unsigned i=0;i<sizeof(bad_bytes)/sizeof(*bad_bytes);i++)
        refused(0x00030a60u,0x08000000u,bad_bytes[i]);
    accepted(0x00010004u,0x08000000u,0x01fefffcu,0x08010004u);
    refused(0x00010004u,0x08000000u,0x01ff0000u);
    accepted(0x40010004u,0x40000000u,0x01fefffcu,0x40010004u);
    refused(0x40010004u,0x40000000u,0x01ff0000u);
    assert(!kui_armada_ce_maple_owner_pointer(0x00030a60u,0x08000000u,0x198u,NULL));
    ++cases;
    printf("Armada CE Maple owner pointer: %u cases passed\n",cases);
    return 0;
}
