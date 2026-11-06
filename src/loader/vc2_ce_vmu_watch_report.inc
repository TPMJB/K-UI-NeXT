/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_ce_vmu_watch.h"
#include "kui/vc2_ce_vmu_handoff.h"
#include "vc2_ce_vmu_symbols.h"
__asm__(".global __kui_vc2_vmu_watch_resident\n.set __kui_vc2_vmu_watch_resident,1\n");

void kui_vc2_ce_vmu_watch_stop(void) {
    const struct kui_vc2_vmu_watch_report *r=(const void *)
        ((uintptr_t)RETAIL_MANIFEST|UINT32_C(0x20000000));
    unsigned page=0u;
    for(;;) {
        uint32_t row[5];
        retail_display_restore(&display);
        retail_display_line(page?"VC2 WRITE WATCH 2 OF 2 CONTEXT":
                                 "VC2 WRITE WATCH 1 OF 2 REGS");
        unsigned end=page?36u:16u;
        for(unsigned at=page?16u:0u;at<end;at+=4u) {
            row[0]=at;
            for(unsigned j=0u;j<4u;j++)row[j+1u]=r->words[at+j];
            retail_display_values("INDEX AND FOUR WORDS",row,5u);
        }
        retail_display_line("PHOTO BOTH PAGES - POWER OFF");
        retail_display_pause(480u);
        page^=1u;
    }
}
static void watch_no_trap(const char *label) {
    uint32_t row[4];
    retail_display_restore(&display);
    retail_display_line("VC2 WATCH NO TRAP - ONE PAGE");
    retail_display_line(label);
    retail_display_values("FN PR SP SR SRC",(const uint32_t *)(const void *)
        &kui_native_ocram_trace.entry[4],5u);
    row[0]=*(volatile const uint16_t *)UINT32_C(0xff200020);
    row[1]=*(volatile const uint16_t *)UINT32_C(0xff200008);
    row[2]=*(volatile const uint16_t *)UINT32_C(0xff200014);
    __asm__ volatile("stc dbr,%0":"=r"(row[3]));
    retail_display_values("CONTROL A B DBR",row,4u);
    retail_display_line("PHOTO THEN POWER OFF");
    for(;;)__asm__ volatile("nop");
}
