/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_MAP_DIAGNOSTIC_H
#define KUI_RETAIL_MAP_DIAGNOSTIC_H
#include "kui/retail_image.h"

/* Failure-only observations of the existing KUIRTI03 wire form. These bits
 * explain its framing and initial policy; they never admit a map, repair
 * bytes, replace the canonical decoder or grant storage/native ownership. */
enum kui_retail_map_diagnostic_reason {
    KUI_RETAIL_MAP_DIAG_MAGIC=1u<<0, KUI_RETAIL_MAP_DIAG_VERSION=1u<<1,
    KUI_RETAIL_MAP_DIAG_SIZE=1u<<2, KUI_RETAIL_MAP_DIAG_CRC=1u<<3,
    KUI_RETAIL_MAP_DIAG_RESERVED=1u<<4, KUI_RETAIL_MAP_DIAG_COUNT=1u<<5,
    KUI_RETAIL_MAP_DIAG_UNUSED=1u<<6, KUI_RETAIL_MAP_DIAG_TRACK=1u<<7,
    KUI_RETAIL_MAP_DIAG_TEXT=1u<<8, KUI_RETAIL_MAP_DIAG_FLAGS=1u<<9,
    KUI_RETAIL_MAP_DIAG_BOUNDS=1u<<10, KUI_RETAIL_MAP_DIAG_READER=1u<<11,
    KUI_RETAIL_MAP_DIAG_SESSION=1u<<12, KUI_RETAIL_MAP_DIAG_BOOT=1u<<13,
    KUI_RETAIL_MAP_DIAG_NULL=1u<<14, KUI_RETAIL_MAP_DIAG_SOURCE=1u<<15
};
struct kui_retail_map_diagnostic {
    uint32_t reasons,stored_crc,actual_crc,flags,reader,track_count,extent_count;
};
static inline uint32_t kui_retail_map_diagnostic_u32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static inline uint64_t kui_retail_map_diagnostic_u64(const uint8_t *p) {
    return (uint64_t)kui_retail_map_diagnostic_u32(p)|
        ((uint64_t)kui_retail_map_diagnostic_u32(p+4u)<<32);
}
static inline uint32_t kui_retail_map_diagnostic_crc(const uint8_t *wire) {
    static const uint8_t zero[4]={0};
    if(!wire) return 0u;
    uint32_t crc=kui_retail_crc32(0u,wire,16u);
    crc=kui_retail_crc32(crc,zero,sizeof(zero));
    return kui_retail_crc32(crc,wire+20u,KUI_RETAIL_IMAGE_WIRE_BYTES-20u);
}
static inline bool kui_retail_map_diagnostic_zero(const uint8_t *p,size_t bytes) {
    for(size_t i=0;i<bytes;i++) if(p[i]) return false;
    return true;
}
static inline bool kui_retail_map_diagnostic_text(const uint8_t *p,size_t bytes,bool required) {
    if(required && !p[0]) return false;
    for(size_t i=0;i<bytes;i++) {
        if(!p[i]) return kui_retail_map_diagnostic_zero(p+i,bytes-i);
        if(p[i]<32u || p[i]>126u) return false;
    }
    return false;
}
/* Input is one accessible, immutable 4096-byte wire image. Every index into
 * its records is bounded before use, including corrupt count arithmetic.
 * No large decoded object, heap allocation, hardware access or input write. */
static inline void kui_retail_map_diagnose(const uint8_t *wire,
        struct kui_retail_map_diagnostic *out) {
    if(!out) return;
    *out=(struct kui_retail_map_diagnostic){0};
    if(!wire) {out->reasons=KUI_RETAIL_MAP_DIAG_NULL;return;}
    static const uint8_t magic[8]={'K','U','I','R','T','I','0','3'};
    for(unsigned i=0;i<sizeof(magic);i++)
        if(wire[i]!=magic[i]) out->reasons|=KUI_RETAIL_MAP_DIAG_MAGIC;
    if(kui_retail_map_diagnostic_u32(wire+8u)!=KUI_RETAIL_IMAGE_VERSION)
        out->reasons|=KUI_RETAIL_MAP_DIAG_VERSION;
    if(kui_retail_map_diagnostic_u32(wire+12u)!=KUI_RETAIL_IMAGE_WIRE_BYTES)
        out->reasons|=KUI_RETAIL_MAP_DIAG_SIZE;
    out->stored_crc=kui_retail_map_diagnostic_u32(wire+16u);
    out->actual_crc=kui_retail_map_diagnostic_crc(wire);
    if(out->stored_crc!=out->actual_crc) out->reasons|=KUI_RETAIL_MAP_DIAG_CRC;
    if(!kui_retail_map_diagnostic_zero(wire+272u,48u))
        out->reasons|=KUI_RETAIL_MAP_DIAG_RESERVED;
    out->track_count=kui_retail_map_diagnostic_u32(wire+20u);
    out->extent_count=kui_retail_map_diagnostic_u32(wire+24u);
    out->reader=kui_retail_map_diagnostic_u32(wire+264u);
    out->flags=kui_retail_map_diagnostic_u32(wire+268u);
    bool counts=out->track_count && out->track_count<=KUI_RETAIL_IMAGE_TRACKS &&
        out->track_count<=KUI_RETAIL_MANIFEST_SLOTS && out->extent_count &&
        out->extent_count<=KUI_RETAIL_WIRE_SLOTS-out->track_count;
    if(!counts) out->reasons|=KUI_RETAIL_MAP_DIAG_COUNT;
    if(!kui_retail_map_diagnostic_text(wire+72u,128u,true) ||
       !kui_retail_map_diagnostic_text(wire+200u,16u,false) ||
       !kui_retail_map_diagnostic_text(wire+216u,24u,true) ||
       !kui_retail_map_diagnostic_text(wire+240u,16u,false))
        out->reasons|=KUI_RETAIL_MAP_DIAG_TEXT;
    if((out->flags&~(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED|KUI_RETAIL_IMAGE_BOOT_CRC)) ||
       ((out->flags&KUI_RETAIL_IMAGE_SCRAMBLED) && !(out->flags&KUI_RETAIL_IMAGE_CD)))
        out->reasons|=KUI_RETAIL_MAP_DIAG_FLAGS;
    uint32_t transport=kui_retail_map_diagnostic_u32(wire+28u);
    uint64_t card=kui_retail_map_diagnostic_u64(wire+32u),
        first=kui_retail_map_diagnostic_u64(wire+40u),
        last=kui_retail_map_diagnostic_u64(wire+48u);
    if(transport>KUI_STORAGE_IDE || !card || card>UINT64_C(0x100000000) ||
       first>=last || last>card) out->reasons|=KUI_RETAIL_MAP_DIAG_BOUNDS;
    if(out->reader>KUI_RETAIL_READER_ASYNC_EAGER ||
       (out->reader!=KUI_RETAIL_READER_STANDARD &&
        (transport!=KUI_STORAGE_SCI || out->track_count>KUI_RETAIL_ASYNC_SLOTS ||
         (out->track_count<=KUI_RETAIL_ASYNC_SLOTS &&
          out->extent_count>KUI_RETAIL_ASYNC_SLOTS-out->track_count))))
        out->reasons|=KUI_RETAIL_MAP_DIAG_READER;
    uint32_t session=kui_retail_map_diagnostic_u32(wire+56u),
        boot=kui_retail_map_diagnostic_u32(wire+60u),
        bytes=kui_retail_map_diagnostic_u32(wire+64u);
    uint64_t boot_end=(uint64_t)boot+((uint64_t)bytes+2047u)/2048u;
    if(!bytes || bytes>KUI_RETAIL_IMAGE_BOOT_MAX || boot<session || boot_end>KUI_GAME_LBA_LIMIT)
        out->reasons|=KUI_RETAIL_MAP_DIAG_BOOT;
    if(counts) {
        const uint32_t used=out->track_count+out->extent_count;
        size_t unused=320u+(size_t)used*12u;
        if(!kui_retail_map_diagnostic_zero(wire+unused,KUI_RETAIL_IMAGE_WIRE_BYTES-unused))
            out->reasons|=KUI_RETAIL_MAP_DIAG_UNUSED;
        bool session_found=false;uint32_t previous=0u,next=out->track_count,cursor=boot;
        for(unsigned i=0;i<out->track_count;i++) {
            const uint8_t *p=wire+320u+(size_t)i*12u;
            uint32_t start=kui_retail_map_diagnostic_u32(p),end=kui_retail_map_diagnostic_u32(p+4u);
            uint8_t control=p[8];uint32_t extents=p[9];
            if(start>=end || end>KUI_GAME_LBA_LIMIT || (i && start<previous) || p[11] ||
               (!(out->flags&KUI_RETAIL_IMAGE_CD) && start<45000u && end>45000u) ||
               (control&3u) ||
               (!(control&4u) && (control&(KUI_RETAIL_TRACK_COOKED|KUI_RETAIL_TRACK_MODE2|KUI_RETAIL_TRACK_2336))) ||
               ((control&KUI_RETAIL_TRACK_COOKED) && (control&(KUI_RETAIL_TRACK_2336|KUI_RETAIL_TRACK_2448))) ||
               ((control&KUI_RETAIL_TRACK_2336) && (!(control&KUI_RETAIL_TRACK_MODE2) || (control&KUI_RETAIL_TRACK_2448))) ||
               (!extents && (control&4u)) || next>used || extents>used-next)
                out->reasons|=KUI_RETAIL_MAP_DIAG_TRACK;
            next+=extents;previous=end;
            if((control&4u) && start==session && ((out->flags&KUI_RETAIL_IMAGE_CD) || session>=45000u))
                session_found=true;
            if(cursor<boot_end && start<=cursor && end>cursor && (control&4u) && extents)
                cursor=(uint64_t)end<boot_end?end:(uint32_t)boot_end;
        }
        if(next!=used) out->reasons|=KUI_RETAIL_MAP_DIAG_TRACK;
        if(!session_found) out->reasons|=KUI_RETAIL_MAP_DIAG_SESSION;
        if(cursor!=boot_end) out->reasons|=KUI_RETAIL_MAP_DIAG_BOOT;
    }
}
#endif
