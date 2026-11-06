/* SPDX-License-Identifier: GPL-3.0-only */
#if KUI_VC2_VMU_WATCH
#include "kui/vc2_ce_vmu_handoff.h"
#include "vc2_watch_symbols.h"

/* SH7750 hardware manual, UBC register table/section20: BAR is32 bits,
 * BASR/BAMR8 bits, BBR/BRCR16 bits. UBDE selects DBR rather than VBR; it
 * does NOT override SR.BL masking. This diagnostic leaves SR and VBR alone.
 * A masked or unwatched-alias writer must remain an explicit no-hit case. */
static void vc2_watch_stage_wait(void) {
    __asm__ volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop":::"memory");
}

/* Invoke only after high-helper staging/copy verification has completed.
 * The generated entry and saved-DBR word both belong to the already checked
 * low resident. No old KOS exception handler or retail VBR is retained. */
static void vc2_ce_vmu_watch_stage_arm(void) {
    volatile uint32_t *const bara=(void *)(uintptr_t)UINT32_C(0xff200000);
    volatile uint8_t *const bamra=(void *)(uintptr_t)UINT32_C(0xff200004);
    volatile uint16_t *const bbra=(void *)(uintptr_t)UINT32_C(0xff200008);
    volatile uint32_t *const barb=(void *)(uintptr_t)UINT32_C(0xff20000c);
    volatile uint8_t *const bamrb=(void *)(uintptr_t)UINT32_C(0xff200010);
    volatile uint16_t *const bbrb=(void *)(uintptr_t)UINT32_C(0xff200014);
    volatile uint16_t *const brcr=(void *)(uintptr_t)UINT32_C(0xff200020);
    volatile uint8_t *const basra=(void *)(uintptr_t)UINT32_C(0xff000014);
    volatile uint8_t *const basrb=(void *)(uintptr_t)UINT32_C(0xff000018);
    const uint32_t entry=KUI_VC2_WATCH_ENTRY,save=KUI_VC2_WATCH_DBR_SAVE;
    const uint32_t begin=KUI_RETAIL_RESIDENT_ADDRESS|UINT32_C(0x20000000);
    const uint32_t limit=resident_limit|UINT32_C(0x20000000);
    uint32_t sr,old_dbr;
    __asm__ volatile("stc sr,%0":"=r"(sr)::"memory");
    if((sr&UINT32_C(0x400000f0))!=UINT32_C(0x400000f0))
        stopped("VC2 WATCH STAGE CONTEXT REFUSED",sr);
    __asm__ volatile("stc dbr,%0":"=r"(old_dbr)::"memory");
    if(resident_bytes<4u || (entry&3u) || entry<begin ||
       entry>begin+resident_bytes-4u || (save&3u) ||
       save<begin+resident_bytes || save>limit-4u)
        stopped("VC2 WATCH LOW SYMBOLS REFUSED",entry);
    /* Stale match flags alone do not own a debug channel. Any configured
     * BRCR control bit, enabled bus condition or DBR debug route does. */
    uint32_t occupied=(uint32_t)(*brcr&UINT16_C(0x3fff))|*bbra|*bbrb;
    if(occupied)stopped("VC2 WATCH DEBUG CHANNEL OCCUPIED",occupied);
    __asm__ volatile("ocbp @%0"::"r"(save^UINT32_C(0x20000000)):"memory");
    *(volatile uint32_t *)(uintptr_t)save=old_dbr;
    if(*(volatile const uint32_t *)(uintptr_t)save!=old_dbr)
        stopped("VC2 WATCH DBR SNAPSHOT CHANGED",old_dbr);

    *brcr=0u;*bbra=0u;*bbrb=0u;
    *bara=KUI_VC2_VMU_RESERVE_P1;*barb=KUI_VC2_VMU_RESERVE_P2;
    *basra=0u;*basrb=0u;
    *bamra=4u;*bamrb=4u; /* Ignore ASID; compare every address bit. */
    __asm__ volatile("ldc %0,dbr"::"r"(entry):"memory");
    *bbra=UINT16_C(0x28);*bbrb=UINT16_C(0x28); /* Operand/write, any size. */
    *brcr=1u; /* Independent address conditions, no data comparison, UBDE. */
    vc2_watch_stage_wait();

    uint32_t now_dbr;
    __asm__ volatile("stc dbr,%0":"=r"(now_dbr)::"memory");
    if(now_dbr!=entry || *bara!=KUI_VC2_VMU_RESERVE_P1 ||
       *barb!=KUI_VC2_VMU_RESERVE_P2 || *basra || *basrb ||
       *bamra!=4u || *bamrb!=4u || *bbra!=UINT16_C(0x28) ||
       *bbrb!=UINT16_C(0x28) || *brcr!=1u) {
        *bbra=0u;*bbrb=0u;*brcr=0u;
        __asm__ volatile("ldc %0,dbr"::"r"(old_dbr):"memory");
        vc2_watch_stage_wait();
        stopped("VC2 WATCH CONFIGURATION READBACK FAILED",now_dbr);
    }
}
#endif
