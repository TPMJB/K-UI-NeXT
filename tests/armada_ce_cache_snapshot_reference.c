/* SPDX-License-Identifier: GPL-3.0-only */
/* Host leaf boundary adapter: compile the real generic reference separately
 * so the full RX fixture can exercise production native C routes unchanged.
 * This is not a machine-leaf model; that leaf has separate instruction tests. */
#ifdef KUI_ARMADA_CE_CACHE_SNAPSHOT_NATIVE
#undef KUI_ARMADA_CE_CACHE_SNAPSHOT_NATIVE
#endif
#define KUI_ARMADA_CE_CACHE_SNAPSHOT_NATIVE 0
#include "kui/armada_ce_cache_line.h"
unsigned kui_test_armada_ce_cache_snapshot_reference(uint32_t va,uint32_t p1,
    uint32_t page,uint32_t mask,kui_armada_ce_ram_read32 read32,void *context,uint32_t out[8]) {
    return kui_armada_ce_cache_line(va,p1,page,0x105u,mask,read32,context,out);
}
