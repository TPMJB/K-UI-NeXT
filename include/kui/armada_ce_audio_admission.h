/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_AUDIO_ADMISSION_H
#define KUI_ARMADA_CE_AUDIO_ADMISSION_H
#include "kui/retail_image.h"
#include <string.h>
/* Admission binds the selected exact US kernel/IP and raw track layout.
 * It does not embed or patch proprietary sound code. */
#if defined(KUI_WINCE_CDDA_WORMS) && KUI_WINCE_CDDA_WORMS == 1
#define KUI_ARMADA_CE_KERNEL_BYTES UINT32_C(1110016)
#elif defined(KUI_WINCE_CDDA_BAM4) && KUI_WINCE_CDDA_BAM4 == 1
#define KUI_ARMADA_CE_KERNEL_BYTES UINT32_C(1830912)
#else
#define KUI_ARMADA_CE_KERNEL_BYTES UINT32_C(1253376)
#endif
static inline bool kui_armada_ce_audio_admit(const struct kui_retail_manifest *m,
        uint32_t ip_crc,uint32_t prefix_crc,uint32_t body_crc,const uint8_t sha[32]) {
#if defined(KUI_WINCE_CDDA_WORMS) && KUI_WINCE_CDDA_WORMS == 1
    static const uint8_t kernel_sha[32]={
        0x39,0xb8,0x0e,0x30,0x59,0x47,0x05,0x65,0x51,0x31,0x8b,0xb6,0x55,0x29,0xcc,0xcc,
        0x88,0x60,0x21,0xbe,0x60,0x40,0x6c,0x06,0xf3,0x11,0x0f,0x7f,0xe0,0x3b,0xf5,0x71};
    static const uint32_t starts[8]={0u,450u,45000u,91528u,117095u,232929u,329558u,379575u};
    if(!m || !sha || m->track_count!=8u || m->session_lba!=45000u ||
       m->boot_lba!=548458u || m->boot_bytes!=KUI_ARMADA_CE_KERNEL_BYTES ||
       m->storage_transport!=KUI_STORAGE_SCI || m->reader!=KUI_RETAIL_READER_ASYNC ||
       (m->flags & ~KUI_RETAIL_IMAGE_BOOT_CRC) ||
       memcmp(m->product,"T40601N",8u) || memcmp(m->bootfile,"0WINCEOS.BIN",13u) ||
       ip_crc!=UINT32_C(0x86bbb334) || prefix_crc!=UINT32_C(0xeffd7677) ||
       body_crc!=UINT32_C(0xcd686951) || memcmp(sha,kernel_sha,32u)) return false;
    for(unsigned i=0;i<8u;i++)
        if(m->slots[i].track.start_lba!=starts[i] ||
           kui_retail_track_control(&m->slots[i].track)!=(i==0u || i==2u || i==7u?4u:0u) ||
           kui_retail_track_sector_bytes(&m->slots[i].track)!=KUI_GAME_RAW_BYTES ||
           kui_retail_track_file_offset(&m->slots[i].track)) return false;
#elif defined(KUI_WINCE_CDDA_BAM4) && KUI_WINCE_CDDA_BAM4 == 1
    static const uint8_t kernel_sha[32]={
        0x2a,0x8d,0x24,0x73,0x8b,0xb5,0xba,0x06,0x87,0xfa,0x56,0xad,0x33,0xe8,0x14,0xfe,
        0x79,0x81,0x58,0xf9,0xcb,0x8c,0xac,0xfd,0x4a,0x8a,0x20,0xde,0xa6,0xbb,0xb3,0x5d};
    static const uint32_t starts[26]={
        0u,450u,45000u,370673u,371347u,372014u,372915u,373626u,379011u,
        384738u,390481u,395473u,398926u,404448u,425246u,445520u,466032u,
        474231u,485598u,486386u,487098u,487822u,498356u,508297u,527383u,548106u};
    if(!m || !sha || m->track_count!=26u || m->session_lba!=45000u ||
       m->boot_lba!=548106u || m->boot_bytes!=KUI_ARMADA_CE_KERNEL_BYTES ||
       m->storage_transport!=KUI_STORAGE_SCI || m->reader!=KUI_RETAIL_READER_ASYNC ||
       (m->flags & ~KUI_RETAIL_IMAGE_BOOT_CRC) ||
       memcmp(m->product,"T-8117N",8u) || memcmp(m->bootfile,"0WINCEOS.BIN",13u) ||
       ip_crc!=UINT32_C(0x8fb049c5) || prefix_crc!=UINT32_C(0x70513a3c) ||
       body_crc!=UINT32_C(0xf6902800) || memcmp(sha,kernel_sha,32u)) return false;
    for(unsigned i=0;i<26u;i++)
        if(m->slots[i].track.start_lba!=starts[i] ||
           kui_retail_track_control(&m->slots[i].track)!=(i==0u || i==2u || i==25u?4u:0u) ||
           kui_retail_track_sector_bytes(&m->slots[i].track)!=KUI_GAME_RAW_BYTES ||
           kui_retail_track_file_offset(&m->slots[i].track)) return false;
#else
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
#endif
    return true;
}
#endif
