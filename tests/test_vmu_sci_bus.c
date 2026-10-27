/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the established SCI MMIO model and PIO fixtures. Only this TU sees
 * the full runtime test declarations; sci_sd_bus.c is compiled separately
 * with KUI_RETAIL_TRANSPORT=1 and KUI_VMU_EXPERIMENTAL=1. Section GC removes
 * the uncalled runtime profiling/DMA fixtures (and their excluded APIs).
 * Build with -ffunction-sections -fdata-sections -Wl,--gc-sections. */
#if !KUI_VMU_EXPERIMENTAL
#error "Explicit experimental write transport required"
#endif
#undef KUI_RETAIL_TRANSPORT
#define main kui_vmu_sci_unused_runtime_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "test_sci_sd_bus.c"
#pragma GCC diagnostic pop
#undef main

int main(void) {
    const struct kui_loader_sd_bus *bus=kui_sci_sd_bus();
    test_blocks(bus);
    test_block_failures(bus);
    puts("PASS VMU experimental resident SCI PIO TX/RX, CRC, bounds, faults, ownership restoration");
    return 0;
}
