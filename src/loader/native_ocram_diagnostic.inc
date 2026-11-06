/* SPDX-License-Identifier: GPL-3.0-only */
/* Terminal only. Freeze cached and uncached service views before the renderer
 * walks VRAM and changes cache occupancy. No state is inferred from RET. */
#ifndef KUI_OC_DIAG_P2
#define KUI_OC_DIAG_P2(pointer) \
    (*(volatile const uint32_t *)(uintptr_t)((uintptr_t)(pointer)|UINT32_C(0x20000000)))
#endif
/* The OCRAM display profile restores volatile qualification before reading. */
#define KUI_OC_DIAG_VALUES(legend,values,count) \
    retail_display_values(legend,(const uint32_t *)(const void *)(values),count)
/* Keep this helper separate: its final render call is a SH tail branch, so
 * the table decoder adds no stack to the renderer/font chain. */
static void __attribute__((noinline)) native_ocram_diagnostic_row(
    const unsigned char *field,const uint32_t *saved,unsigned count,const char *legend) {
    _Static_assert(offsetof(struct kui_native_ocram_diagnostic,copyback_ccr)==36u*4u,
        "diagnostic trace word map changed");
    volatile uint32_t *row=kui_native_ocram_trace.history[0];
    for(unsigned j=0;j<count;j++) {
        int index=((const signed char *)field)[j];
        row[j]=index>=0?*(volatile const uint32_t *)
            ((volatile const unsigned char *)&kui_native_ocram_trace+4u*index):saved[-1-index];
    }
    KUI_OC_DIAG_VALUES(legend,row,count);
}
static void native_ocram_diagnostic_report(const char *label,uint32_t caller,uint32_t sp) {
    (void)caller;(void)sp;
    uint32_t saved[16]={service.token,service.command,(uint32_t)service.status,
        service.pending,service.error,service.request_bytes,service.completed_bytes,
        kui_retail_hook_active,kui_retail_hook_fault,
        KUI_OC_DIAG_P2(&service.token),KUI_OC_DIAG_P2(&service.command),
        KUI_OC_DIAG_P2(&service.status),KUI_OC_DIAG_P2(&kui_retail_hook_active),
        (uint32_t)service.diag.last_result,service.outputs[0],service.sector_bytes};
    __asm__ volatile("" : : "m"(saved) : "memory");
    retail_display_restore(&display);
    retail_display_line(label);
    retail_display_line("HIST FN R4 R5 RET CCR");
    for(unsigned i=0;i<4u;i++)
        KUI_OC_DIAG_VALUES("",kui_native_ocram_trace.history[i],5);
    /* The history is now on screen. This terminal path never resumes a game
     * or exports the ring, so its first row becomes bounded display scratch. */
    /* Each record is count, source indices, and a zero-terminated legend.
     * Nonnegative indices are trace word offsets; -1-index selects saved words. */
    static const unsigned char rows[]=
        "\005\000\004\010\011\362" "SEQ FN SRC ASM CORE\0"
        "\005\001\002\003\016\017" "R4 R5 AUX VALUE WHY\0"
        "\005\005\006\007\015\013" "PR SP SR VBR MMU\0"
        "\005\014\044\012\370\367" "CCR CB LOCKW LOCKN FAULT\0"
        "\005\377\376\375\374\373" "TOKEN CMD STAT PEND ERR\0"
        "\004\372\371\361\360" "TOTAL DONE PIECE SECTOR\0"
        "\004\366\365\364\363" "P2 TOKEN CMD STAT LOCK\0";
    const unsigned char *spec=rows;
    while(*spec) {
        unsigned count=*spec++;
        const unsigned char *field=spec;
        spec+=count;
        const char *legend=(const char *)spec;
        while(*spec++){}
        native_ocram_diagnostic_row(field,saved,count,legend);
    }
}
