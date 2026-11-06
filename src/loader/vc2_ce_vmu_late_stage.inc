/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_ce_vmu_late.h"
#include "kui/vc2_ce_vmu_late_seed.h"
#include "vc2_ce_vmu_symbols.h"
#include "vc2_late_symbols.h"
#if KUI_VC2_VMU_PERSIST
#include "vc2_ce_vmu_persist_stage.inc"
#define VC2_LATE_SEED_VERSION KUI_VC2_PERSIST_SEED_VERSION
#else
#define VC2_LATE_SEED_VERSION KUI_VC2_LATE_SEED_VERSION
#endif
extern const uint8_t __vc2_late_raw_start[] __asm__("__vc2_late_raw_start");
extern const uint8_t __vc2_late_raw_end[] __asm__("__vc2_late_raw_end");
/* The launcher fills only the three physical-run scalars and seed CRC.
 * Volatile keeps those launch-local bytes distinct from compile-time zeros. */
const volatile struct kui_vc2_vmu_late_seed kui_vc2_vmu_late_seed
    __attribute__((used,section(".rodata.vc2_late_seed")))={
        KUI_VC2_LATE_SEED_MAGIC0,KUI_VC2_LATE_SEED_MAGIC1,
        VC2_LATE_SEED_VERSION,KUI_VC2_LATE_SEED_BYTES,
        KUI_VC2_LATE_FILE_BYTES,KUI_VC2_VMU_PREPARED_CRC32,0u,0u,0u,0u,0u};

static void vc2_ce_vmu_late_publish(void) {
    struct kui_vc2_vmu_late_seed s=kui_vc2_vmu_late_seed;
    uint32_t expected=s.seed_crc32;s.seed_crc32=0u;
    uint32_t actual=kui_retail_crc32(0u,&s,sizeof(s));
    if(s.magic0!=KUI_VC2_LATE_SEED_MAGIC0 || s.magic1!=KUI_VC2_LATE_SEED_MAGIC1 ||
       s.version!=VC2_LATE_SEED_VERSION || s.bytes!=sizeof(s) ||
       s.helper_bytes!=KUI_VC2_LATE_FILE_BYTES ||
       s.helper_crc32!=KUI_VC2_VMU_PREPARED_CRC32 || s.reserved || actual!=expected)
        stopped("VC2 HELPER MAP SEED CHANGED",actual);
#if KUI_VC2_VMU_PERSIST
    /* The separately admitted capsule has99 sectors; the low helper read
     * still copies exactly96 immutable sectors. */
    const uint32_t config_crc=vc2_ce_vmu_persist_prepare(&s);
#else
    if(!kui_vc2_vmu_late_map_valid(s.first_lba,s.first_blocks,s.second_lba,
        manifest.partition_start,manifest.partition_end))
        stopped("VC2 HELPER CARD MAP REFUSED",s.first_lba);
#endif
    volatile struct kui_retail_manifest *owned=(void *)(uintptr_t)KUI_VC2_LATE_MAP_P2;
    if((size_t)(__vc2_late_raw_end-__vc2_late_raw_start)!=KUI_VC2_LATE_RAW_BYTES)
        stopped("VC2 HELPER READER SIZE CHANGED",0u);
    for(uintptr_t at=KUI_VC2_LATE_RAW_P1;
        at<KUI_VC2_LATE_RAW_P1+KUI_VC2_LATE_RAW_BYTES;at+=32u)
        __asm__ volatile("ocbp @%0"::"r"(at):"memory");
    memcpy((void *)(uintptr_t)KUI_VC2_LATE_RAW_P2,__vc2_late_raw_start,KUI_VC2_LATE_RAW_BYTES);
    if(kui_retail_crc32(0u,(const void *)(uintptr_t)KUI_VC2_LATE_RAW_P2,
        KUI_VC2_LATE_RAW_BYTES)!=KUI_VC2_LATE_RAW_CRC32)
        stopped("VC2 HELPER READER COPY CHANGED",0u);
    const uint32_t fields[]={36u,40u,56u,60u,64u
#if KUI_SMASH_CE_RETURN
        ,KUI_SMASH_CE_TRANSPORT_SEAL_OFFSET
#endif
    };
    for(unsigned i=0u;i<sizeof(fields)/sizeof(fields[0]);i++) {
        uintptr_t at=(KUI_VC2_LATE_MAP_P2^UINT32_C(0x20000000))+fields[i];
        __asm__ volatile("ocbp @%0"::"r"(at):"memory");
    }
    /* All original executable/map checks have finished. These retired
     * scalars are unused by live disc reads; retain the disc map verbatim. */
    owned->boot_lba=s.first_lba;
    owned->boot_bytes=s.second_lba;
    owned->gdi_crc32=s.first_blocks;
    owned->ip_crc32=KUI_VC2_LATE_RAW_CRC32;
#if KUI_SMASH_CE_RETURN
    _Static_assert(offsetof(struct kui_retail_manifest,slots)==252u,"CE low map overlay ABI");
    if(!smash_ce_return_transport_crc)stopped("SMASH CE RETURN LIVE SEAL MISSING",0u);
    volatile uint32_t *seal=(void *)((uintptr_t)owned+KUI_SMASH_CE_TRANSPORT_SEAL_OFFSET);
    *seal=smash_ce_return_transport_crc;
    if(*seal!=smash_ce_return_transport_crc)stopped("SMASH CE RETURN LIVE SEAL COPY CHANGED",0u);
#endif
#if KUI_VC2_VMU_PERSIST
    owned->boot_crc32=config_crc;
    if(owned->boot_crc32!=config_crc)stopped("VC2 CONFIG CRC PUBLICATION FAILED",config_crc);
#endif
    if(owned->boot_lba!=s.first_lba || owned->boot_bytes!=s.second_lba ||
       owned->gdi_crc32!=s.first_blocks || owned->ip_crc32!=KUI_VC2_LATE_RAW_CRC32)
        stopped("VC2 HELPER MAP PUBLICATION FAILED",s.first_lba);
}
