/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_MAPLE_RESERVATION_H
#define KUI_ARMADA_CE_MAPLE_RESERVATION_H
#include "kui/armada_ce_excerpt_pool.h"
#include "kui/armada_ce_vmu_layout.h"
#if KUI_ARMADA_CE_MAPLE_A2 || KUI_ARMADA_CE_MAPLE_A2_STAGE
#define KUI_MAPLE_RESERVE_BEGIN KUI_ARMADA_CE_VMU_RESERVE_BEGIN
#elif KUI_ARMADA_CE_MAPLE_FENCE || KUI_ARMADA_CE_MAPLE_FENCE_STAGE
#define KUI_MAPLE_RESERVE_BEGIN UINT32_C(0x8cfcd000)
#elif KUI_ARMADA_CE_MAPLE_TRAFFIC || KUI_ARMADA_CE_MAPLE_TRAFFIC_STAGE
#define KUI_MAPLE_RESERVE_BEGIN UINT32_C(0x8cfce000)
#else
#define KUI_MAPLE_RESERVE_BEGIN UINT32_C(0x8cfcf000)
#endif
#define KUI_MAPLE_RESERVE_END UINT32_C(0x8cfd0000)
#define KUI_MAPLE_RESERVE_BYTES (KUI_MAPLE_RESERVE_END-KUI_MAPLE_RESERVE_BEGIN)
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
#define KUI_MAPLE_RESERVE_CODE_MAX (KUI_ARMADA_CE_VMU_HANDOFF_P2-KUI_ARMADA_CE_VMU_HOOK_P2)
#else
#define KUI_MAPLE_RESERVE_CODE_MAX (UINT32_C(0x8cfcfe00)-KUI_MAPLE_RESERVE_BEGIN)
#endif
#define KUI_MAPLE_TEXT_RECORD UINT32_C(0x8c099bd4)
#define KUI_MAPLE_TEXT_RECORD_BYTES 24u
#define KUI_MAPLE_TEXT_RECORD_CRC UINT32_C(0x94183a25)
#define KUI_MAPLE_ORIGINAL_ENTRY UINT32_C(0x8c0b3b0c)
#define KUI_MAPLE_ORIGINAL_ENTRY_BYTES 32u
#define KUI_MAPLE_ORIGINAL_ENTRY_CRC UINT32_C(0x7bd7ff55)
#define KUI_MAPLE_BRIDGE_DEST UINT32_C(0x8c0b47a4)
#define KUI_MAPLE_BRIDGE_BYTES 92u
#define KUI_MAPLE_ENTRY_BRANCH UINT32_C(0x0009a64a)
#define KUI_MAPLE_PATCHED_ENTRY_CRC UINT32_C(0x576e75e7)
#define KUI_MAPLE_WORKER_PATCH_ADDRESS UINT32_C(0x8c0b3db0)
#define KUI_MAPLE_WORKER_ORIGINAL_CRC UINT32_C(0xd48fad4a)
#define KUI_MAPLE_WORKER_PATCH_WORD UINT32_C(0xb4f70009)
#define KUI_MAPLE_WORKER_PATCHED_CRC UINT32_C(0x004742ae)
#if KUI_ARMADA_CE_MAPLE_FENCE_STAGE
#define KUI_MAPLE_INSTALL_ADDRESS KUI_MAPLE_WORKER_PATCH_ADDRESS
#define KUI_MAPLE_INSTALL_WORD KUI_MAPLE_WORKER_PATCH_WORD
#else
#define KUI_MAPLE_INSTALL_ADDRESS KUI_MAPLE_ORIGINAL_ENTRY
#define KUI_MAPLE_INSTALL_WORD KUI_MAPLE_ENTRY_BRANCH
#endif
#define KUI_MAPLE_TEXT_ORIGINAL_BYTES UINT32_C(0x57a2)
#define KUI_MAPLE_TEXT_RESERVED_BYTES UINT32_C(0x5800)
/* A pre-NK plan for this exact admitted source only. RAMend excludes the page
 * from CE's allocator, while OEM driver globals remain at fd0000..1000000.
 * The relay widens an existing text section within its stored, zero-filled
 * last page. It writes the 4-byte entry branch only after all other checks.
 * No live CE code/cache patching or driver-object allocation occurs here. */
struct kui_maple_reservation_inputs {
    const uint8_t *rom_header,*text_record,*entry,*padding;
    size_t header_bytes,record_bytes,entry_bytes,padding_bytes;
    uint32_t stage_end,bss_end;
    bool source_admitted,gd_alias_guard;
};
struct kui_maple_reservation_plan {
    uint32_t ram_end_word,ram_end,vsize_word,vsize,bridge,entry,entry_word;
};
static inline bool kui_maple_reservation_destination_safe(uint32_t address,uint32_t bytes) {
    uint32_t area=address&0xe0000000u,physical=address&0x1fffffffu;
    if((area && area!=0x80000000u && area!=0xa0000000u) || !bytes ||
       physical<0x0c000000u || physical>=0x0d000000u || bytes>0x0d000000u-physical) return false;
    return physical>=(KUI_MAPLE_RESERVE_END&0x1fffffffu) ||
        physical+bytes<=(KUI_MAPLE_RESERVE_BEGIN&0x1fffffffu);
}
static inline bool kui_maple_reservation_prepare(const struct kui_maple_reservation_inputs *in,
    struct kui_maple_reservation_plan *out) {
    if(!in || !out || !in->source_admitted || !in->gd_alias_guard ||
       !in->rom_header || !in->text_record || !in->entry || !in->padding ||
       in->header_bytes!=84u || in->record_bytes!=24u || in->entry_bytes!=32u ||
       in->padding_bytes!=KUI_MAPLE_BRIDGE_BYTES ||
       in->stage_end<0x8ce10000u || in->bss_end<in->stage_end ||
       in->bss_end>=KUI_MAPLE_RESERVE_BEGIN ||
       !kui_armada_ce_excerpt_pool_header_valid(in->rom_header,84u) ||
       kui_armada_ce_excerpt_crc32(in->text_record,24u)!=KUI_MAPLE_TEXT_RECORD_CRC ||
       kui_armada_ce_excerpt_crc32(in->entry,32u)!=KUI_MAPLE_ORIGINAL_ENTRY_CRC) return false;
    for(unsigned i=0;i<KUI_MAPLE_BRIDGE_BYTES;i++) if(in->padding[i]) return false;
    const struct kui_maple_reservation_plan plan={KUI_ARMADA_CE_EXCERPT_RAM_END_WORD,
        KUI_MAPLE_RESERVE_BEGIN,KUI_MAPLE_TEXT_RECORD,KUI_MAPLE_TEXT_RESERVED_BYTES,
        KUI_MAPLE_BRIDGE_DEST,KUI_MAPLE_INSTALL_ADDRESS,KUI_MAPLE_INSTALL_WORD};
    *out=plan;return true;
}
#endif
