/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_ce_dma_events.h"

static void native_ce_dma_stop(uint32_t function) {
    kui_native_ocram_trace.entry[14]=kui_native_ocram_trace.entry[13]+0x600u;
    kui_native_ocram_trace.entry[15]=KUI_OC_MAP_CE_EVENT_BINDING;
    report_fault("CE EVENT BINDING REFUSED",function,0u);
}
static void native_ce_dma_prepare(uint32_t command) {
    /* A native executable must never adopt a coincidentally shaped block.
     * Require the live MMU context and a recognized IRQ entry at its VBR. */
    if(!service.cpu_mmu || !kui_native_ce_dma_bind(
            kui_native_ocram_trace.entry[13],kui_native_boot_state.report.ce_kernel))
        native_ce_dma_stop(command);
}
static void native_ce_dma_events(uint32_t function) {
    uint32_t events=kui_retail_gd_native_dma_events(&service);
    if(((events&1u) && !kui_native_ce_dma_raise(kui_native_boot_state.report.ce_kernel,21u)) ||
       ((events&2u) && !kui_native_ce_dma_raise(kui_native_boot_state.report.ce_kernel,20u)))
        native_ce_dma_stop(function);
#if KUI_NATIVE_GD_PIO_STREAM
    /* The original PIO pump advances its scatter entry after FN12 returns.
     * Calling here on FN12 would recurse before that bookkeeping finishes.
     * The next EXEC may call it after the native gateway restores the
     * caller's stack, SR, preserved registers and GBR and releases the lock. */
    if(function==KUI_GD_EXEC && service.callback_due && service.pio_callback) {
        kui_native_pio_deferred[1]=service.pio_argument;
        kui_native_pio_deferred[0]=service.pio_callback;
        service.callback_due=0u;
    }
#endif
}
