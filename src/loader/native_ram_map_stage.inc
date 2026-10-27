/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_ram_map.h"
#if !KUI_NATIVE_STANDALONE_READER
#error "The RAM-map test belongs to the standalone reader"
#endif
__asm__(".global __kui_native_ram_map_stage\n"
        ".set __kui_native_ram_map_stage,1\n");
extern const uint8_t __retail_stage_bss_end[] __asm__("__retail_stage_bss_end");
_Static_assert(!(KUI_RETAIL_STAGE_ADDRESS&(KUI_NATIVE_RAM_MAP_PAGE_BYTES-1u)),
               "the stage starts on a map page");
_Static_assert(((KUI_RETAIL_STAGE_STACK+KUI_NATIVE_RAM_MAP_PAGE_BYTES-1u)&
                ~(KUI_NATIVE_RAM_MAP_PAGE_BYTES-1u))==KUI_RETAIL_RAM_END,
               "the stage stack's page is the last map page");
/* The relay calls this last, with both caches off, so every store reaches
 * RAM. Every whole page from the executable's end gets the pattern except
 * the pages of this stage's image and BSS, and the last page, which holds
 * its stack: those are still in use until the game's entry. */
static void __attribute__((noinline)) ram_map_fill(void) {
    const uint32_t mask=KUI_NATIVE_RAM_MAP_PAGE_BYTES-1u;
    const uint32_t first=(KUI_RETAIL_EXEC_ADDRESS+exec_bytes+mask)&~mask;
    const uint32_t stage=(((uint32_t)(uintptr_t)__retail_stage_bss_end+mask)&~mask)-
        KUI_RETAIL_STAGE_ADDRESS;
    for(uint32_t a=first;a<(KUI_RETAIL_STAGE_MEMORY_END&~mask);a+=4u) {
        if(a-KUI_RETAIL_STAGE_ADDRESS<stage) continue;
        *(volatile uint32_t *)(uintptr_t)(a|0x20000000u)=kui_native_ram_map_word(a);
    }
    retail_display_line("RAM MAP TEST - FREE PAGES MARKED");
}
