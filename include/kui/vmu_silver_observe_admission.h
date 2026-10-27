/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_SILVER_OBSERVE_ADMISSION_H
#define KUI_VMU_SILVER_OBSERVE_ADMISSION_H
#include "kui/retail_image.h"
#include <stdbool.h>
#include <stdint.h>

/* Scalar contracts for the owner's native T15108N V1.001 image only. */
#define KUI_VMU_SILVER_EXEC_BYTES 2956128u
#define KUI_VMU_SILVER_EXEC_CRC UINT32_C(0x7d83cf24)
#define KUI_VMU_SILVER_HEAP_SLOT UINT32_C(0x8c0ddf20)
#define KUI_VMU_SILVER_HEAP_BEGIN UINT32_C(0x8c2d9b80)
#define KUI_VMU_SILVER_HEAP_BYTES UINT32_C(0x00d26480)
#define KUI_VMU_SILVER_HEAP_ALLOCATE UINT32_C(0x8c11e296)

static inline bool kui_vmu_silver_observe_manifest(const struct kui_retail_manifest *m) {
    static const uint32_t starts[3]={0,660,45000};
    static const uint32_t ends[3]={510,6610,549150};
    if(!m || m->gdi_crc32!=UINT32_C(0x5e1b611a) || m->track_count!=3u ||
       m->storage_transport!=KUI_STORAGE_SCI || m->reader!=KUI_RETAIL_READER_STANDARD ||
       m->flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED) ||
       m->session_lba!=45000u || m->boot_lba!=547556u ||
       m->boot_bytes!=KUI_VMU_SILVER_EXEC_BYTES || !m->extent_count || m->extent_count>61u ||
       ((m->flags&KUI_RETAIL_IMAGE_BOOT_CRC) && m->boot_crc32!=KUI_VMU_SILVER_EXEC_CRC))
        return false;
    for(unsigned i=0;i<3u;i++) {
        const struct kui_retail_track *t=&m->slots[i].track;
        if(t->start_lba!=starts[i] || t->end_lba!=ends[i] || t->control!=(i==1u?0u:4u) ||
           kui_retail_track_sector_bytes(t)!=2352u || kui_retail_track_file_offset(t) ||
           !t->extent_count) return false;
    }
    return true;
}

/* The exact native root allocator owns the complete 0x30000 lease. Worker
 * code/state/stack occupy its first 64KiB; the existing SDK high scratch
 * above FE0000 remains an admitted GD destination. Protect its header too. */
static inline bool kui_vmu_silver_observe_destination(uint32_t address,uint32_t bytes) {
    uint32_t area=address&UINT32_C(0xff000000);
    if(area!=UINT32_C(0x0c000000) && area!=UINT32_C(0x8c000000) &&
       area!=UINT32_C(0xac000000)) return false;
    uint32_t canonical=(address&UINT32_C(0x1fffffff))|UINT32_C(0x80000000);
    if(!bytes || canonical<UINT32_C(0x8c008000) || canonical>=UINT32_C(0x8d000000) ||
       bytes>UINT32_C(0x8d000000)-canonical) return false;
    if(canonical>=UINT32_C(0x8cfe0000)) return true;
    if(canonical>=UINT32_C(0x8cfcffe0)) return false;
    return bytes<=UINT32_C(0x8cfcffe0)-canonical;
}
#endif
