/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_TOY_OBSERVE_ADMISSION_H
#define KUI_VMU_TOY_OBSERVE_ADMISSION_H
#include "kui/retail_image.h"
#include <stdbool.h>
#include <stdint.h>

#define KUI_VMU_TOY_EXEC_BYTES 748444u
#define KUI_VMU_TOY_EXEC_CRC UINT32_C(0xcdc493b3)
#define KUI_VMU_TOY_HEAP_SLOT UINT32_C(0x8c04e9b4)
#define KUI_VMU_TOY_HEAP_INIT UINT32_C(0x8c0b3e10)
#define KUI_VMU_TOY_HEAP_BEGIN UINT32_C(0x8c124340)
#define KUI_VMU_TOY_HEAP_BYTES UINT32_C(0x00edbcc0)
#define KUI_VMU_TOY_HEAP_ALLOCATE UINT32_C(0x8c0b3ede)
#define KUI_VMU_TOY_LEASE_BEGIN UINT32_C(0x8cfd0000)
#define KUI_VMU_TOY_LEASE_BYTES UINT32_C(0x30000)
#define KUI_VMU_TOY_WORKER_END UINT32_C(0x8cfe0000)
#define KUI_VMU_TOY_HOOK_BEGIN UINT32_C(0x8c0bf264)
#define KUI_VMU_TOY_HOOK_END UINT32_C(0x8c0bf270)
#define KUI_VMU_TOY_HOOK_BYTES 12u

/* Exact original raw GDI profile. The frontend's map validation is still
 * mandatory; this check adds title, reader and extent-budget restrictions
 * before the isolated stage performs storage I/O. */
static inline bool kui_vmu_toy_observe_manifest(const struct kui_retail_manifest *m) {
    static const uint32_t starts[15]={0,7257,45000,201900,219712,234091,255599,
        272017,282799,303092,323352,341314,356690,374201,377422};
    static const uint32_t ends[15]={7107,7753,201750,219712,234091,255599,
        272017,282799,303092,323352,341314,356690,374201,377272,549150};
    if(!m || m->gdi_crc32!=UINT32_C(0xe25531d1) || m->track_count!=15u ||
       m->storage_transport!=KUI_STORAGE_SCI || m->reader!=KUI_RETAIL_READER_STANDARD ||
       m->flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED) ||
       m->session_lba!=45000u || m->boot_lba!=548634u ||
       m->boot_bytes!=KUI_VMU_TOY_EXEC_BYTES || !m->extent_count ||
       m->extent_count>49u ||
       ((m->flags&KUI_RETAIL_IMAGE_BOOT_CRC) && m->boot_crc32!=KUI_VMU_TOY_EXEC_CRC))
        return false;
    for(unsigned i=0;i<15u;i++) {
        const struct kui_retail_track *t=&m->slots[i].track;
        if(t->start_lba!=starts[i] || t->end_lba!=ends[i] ||
           t->control!=((i==0u || i==2u || i==14u)?4u:0u) ||
           kui_retail_track_sector_bytes(t)!=2352u ||
           kui_retail_track_file_offset(t) || !t->extent_count)
            return false;
    }
    return true;
}

/* Only the already leased code/data/stack and its allocator header are
 * denied GD destinations. The existing title profile permits direct SDK
 * scratch in FE0000..RAM-end; VMU code must not treat that as free space. */
static inline bool kui_vmu_toy_observe_destination(uint32_t address,uint32_t bytes) {
    uint32_t area=address&UINT32_C(0xff000000);
    if(area!=UINT32_C(0x0c000000) && area!=UINT32_C(0x8c000000) &&
       area!=UINT32_C(0xac000000)) return false;
    uint32_t canonical=(address&UINT32_C(0x1fffffff))|UINT32_C(0x80000000);
    if(!bytes || canonical<UINT32_C(0x8c008000) ||
       canonical>=UINT32_C(0x8d000000) || bytes>UINT32_C(0x8d000000)-canonical)
        return false;
    if(canonical>=KUI_VMU_TOY_WORKER_END) return true;
    if(canonical>=UINT32_C(0x8cfcffe0)) return false;
    return bytes<=UINT32_C(0x8cfcffe0)-canonical;
}

#endif
