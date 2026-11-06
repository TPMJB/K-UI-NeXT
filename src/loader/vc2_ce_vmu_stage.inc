/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_ce_vmu_handoff.h"
#include "vc2_ce_vmu_symbols.h"

extern const uint8_t __vc2_vmu_blob_start[] __asm__("__vc2_vmu_blob_start");
extern const uint8_t __vc2_vmu_blob_end[] __asm__("__vc2_vmu_blob_end");
extern const uint8_t __retail_stage_binary_end[] __asm__("__retail_stage_binary_end");
extern const uint8_t __retail_stage_bss_end[] __asm__("__retail_stage_bss_end");

/* Call after validating/loading the ordinary native owner and before the
 * final relay. This only stages untrusted bytes. It neither lowers RAMend
 * nor patches any native/CE instruction, and native Smash Pack may overwrite
 * it. The later trusted low gate rechecks every immutable byte before the
 * admitted P2 entry initializes its entire working tail. */
static void vc2_ce_vmu_stage_prepare(void) {
    size_t bytes=(size_t)(__vc2_vmu_blob_end-__vc2_vmu_blob_start);
    if(bytes!=KUI_VC2_VMU_BLOB_BYTES || !bytes ||
       bytes>KUI_VC2_VMU_RESERVE_BYTES ||
       (uint32_t)(uintptr_t)__retail_stage_binary_end>=KUI_VC2_VMU_RESERVE_P1 ||
       (uint32_t)(uintptr_t)__retail_stage_bss_end>=KUI_VC2_VMU_RESERVE_P1 ||
       KUI_RETAIL_EXEC_ADDRESS+exec_bytes>KUI_VC2_VMU_RESERVE_P1)
        stopped("VC2 VMU STAGING BOUNDS REFUSED",(uint32_t)bytes);
    memset((void *)(uintptr_t)KUI_VC2_VMU_RESERVE_P1,0,KUI_VC2_VMU_RESERVE_BYTES);
    memcpy((void *)(uintptr_t)KUI_VC2_VMU_RESERVE_P1,__vc2_vmu_blob_start,bytes);
    if(kui_vc2_ce_vmu_crc32((const volatile void *)(uintptr_t)
        KUI_VC2_VMU_RESERVE_P1,KUI_VC2_VMU_RESERVE_BYTES)!=KUI_VC2_VMU_PREPARED_CRC32)
        stopped("VC2 VMU STAGING COPY CHANGED",(uint32_t)bytes);
}
