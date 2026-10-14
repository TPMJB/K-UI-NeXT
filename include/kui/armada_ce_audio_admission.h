/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_AUDIO_ADMISSION_H
#define KUI_ARMADA_CE_AUDIO_ADMISSION_H
#include "kui/retail_image.h"
#include <string.h>
/* This first diagnostic binds only the owner's exact US kernel/IP and raw
 * five-track layout. It does not embed or patch proprietary sound code. */
#define KUI_ARMADA_CE_KERNEL_BYTES UINT32_C(1253376)
static inline bool kui_armada_ce_audio_admit(const struct kui_retail_manifest *m,
        uint32_t ip_crc,uint32_t prefix_crc,uint32_t body_crc,const uint8_t sha[32]) {
    static const uint8_t kernel_sha[32]={
        0xa7,0x62,0x8c,0x78,0x09,0x0f,0xce,0x2c,0x93,0x01,0x2d,0x5e,0x4d,0xb6,0x14,0x20,
        0x96,0xe1,0x38,0xdc,0xeb,0x7e,0xbb,0xf5,0x55,0x14,0x2e,0x90,0x2c,0xa0,0x3c,0x1f};
    static const uint32_t starts[5]={0u,756u,45000u,149842u,434062u};
    if(!m || !sha || m->track_count!=5u || m->session_lba!=45000u ||
       m->boot_lba!=548388u || m->boot_bytes!=KUI_ARMADA_CE_KERNEL_BYTES ||
       m->storage_transport!=KUI_STORAGE_SCI || m->reader!=KUI_RETAIL_READER_ASYNC ||
       (m->flags & ~KUI_RETAIL_IMAGE_BOOT_CRC) ||
       memcmp(m->product,"T40301N 00",11u) || memcmp(m->bootfile,"0WINCEOS.BIN",13u) ||
       ip_crc!=UINT32_C(0x684f8040) || prefix_crc!=UINT32_C(0x6200729c) ||
       body_crc!=UINT32_C(0xb9f755bc) || memcmp(sha,kernel_sha,32u)) return false;
    for(unsigned i=0;i<5;i++)
        if(m->slots[i].track.start_lba!=starts[i] ||
           kui_retail_track_control(&m->slots[i].track)!=(i==1u || i==3u?0u:4u) ||
           kui_retail_track_sector_bytes(&m->slots[i].track)!=KUI_GAME_RAW_BYTES ||
           kui_retail_track_file_offset(&m->slots[i].track)) return false;
    return true;
}
#endif
