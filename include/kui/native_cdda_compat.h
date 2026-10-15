/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_CDDA_COMPAT_H
#define KUI_NATIVE_CDDA_COMPAT_H
#include "kui/retail_image.h"
#include "kui/mdk2_cdda_layout.h"
#include <stdbool.h>
#ifndef KUI_NATIVE_CDDA_COMPAT
#define KUI_NATIVE_CDDA_COMPAT 0
#endif
#if KUI_NATIVE_CDDA_COMPAT != 0 && KUI_NATIVE_CDDA_COMPAT != 1
#error Native CDDA compatibility must be zero or one
#endif

/* Compatibility is a property of the unmodified executable and its retained
 * sound/heap adapter, not a title string or a disc descriptor checksum. These
 * are only cheap routing fingerprints: the stage independently verifies the
 * complete loaded executable SHA and every hook before changing game RAM.
 * The installed ARM driver and all allocation/lifecycle guards stay exact. */
enum kui_native_cdda_binding {
    KUI_NATIVE_CDDA_NONE, KUI_NATIVE_CDDA_TOY, KUI_NATIVE_CDDA_MDK2
};
/* Authored adapter identity embedded in the experimental stage and checked
 * by its launcher. The runtime envelope verifies all payload bytes; this
 * descriptor binds the allocation/adapter selected by that launcher. It is
 * independent of the build stamp, avoiding source-commit/self-hash cycles. */
struct kui_native_cdda_descriptor {
    uint32_t magic0,magic1,version,bytes,binding,cooked,slots;
    uint32_t resident_begin,resident_limit,stack_bottom,stack_top;
    uint32_t worker_begin,worker_limit,lease_end,boot_bytes,boot_crc32;
    uint8_t boot_sha256[32],driver_sha256[32];
};
_Static_assert(sizeof(struct kui_native_cdda_descriptor)==128u,"Native CDDA descriptor ABI");
#define KUI_NATIVE_CDDA_DESCRIPTOR_HEAD(binding,cooked,slots,bytes,crc) \
    UINT32_C(0x4e49554b),UINT32_C(0x31304443),1u,128u,binding,cooked,slots, \
    UINT32_C(0x8c004000),UINT32_C(0x8c007800),UINT32_C(0xac007800),UINT32_C(0xac007d00), \
    UINT32_C(0x8cfd0000),UINT32_C(0x8cfe0000),UINT32_C(0x8d000000),bytes,crc
#define KUI_NATIVE_CDDA_TOY_BOOT_SHA \
    {0xee,0x68,0xa3,0x26,0xda,0x96,0x4f,0xec,0x58,0x92,0xb7,0x22,0x63,0x11,0xf2,0x29, \
     0x3f,0xd3,0xa0,0xbb,0xf5,0xba,0xc4,0x96,0xec,0x82,0x99,0x19,0x64,0xd4,0x9b,0xbd}
#define KUI_NATIVE_CDDA_TOY_DRIVER_SHA \
    {0x47,0x7e,0xde,0x37,0x66,0xc2,0x7f,0xa5,0x8e,0x4c,0x14,0xd5,0x21,0x8c,0x58,0x3b, \
     0x78,0x06,0xa2,0x9c,0x32,0x8a,0x6e,0x0d,0x49,0x65,0x29,0x33,0x75,0xb7,0x04,0xe5}
#define KUI_NATIVE_CDDA_TOY_DESCRIPTOR(cooked) \
    {KUI_NATIVE_CDDA_DESCRIPTOR_HEAD(KUI_NATIVE_CDDA_TOY,cooked,64u,748444u,UINT32_C(0xcdc493b3)), \
     KUI_NATIVE_CDDA_TOY_BOOT_SHA,KUI_NATIVE_CDDA_TOY_DRIVER_SHA}
#define KUI_NATIVE_CDDA_MDK2_DESCRIPTOR \
    {KUI_NATIVE_CDDA_DESCRIPTOR_HEAD(KUI_NATIVE_CDDA_MDK2,0u,KUI_MDK2_CDDA_MANIFEST_SLOTS,750804u,UINT32_C(0x682d90e3)), \
     {0xac,0x58,0x26,0xc9,0xa3,0x71,0xde,0x04,0x20,0x77,0x48,0xe6,0x0e,0x49,0xe0,0xa3, \
      0xc1,0x3a,0x39,0x46,0x89,0x02,0xf8,0x9d,0x8c,0x1c,0x82,0xe1,0xf5,0x44,0xbb,0x48}, \
     {0xaf,0x8f,0x90,0x44,0x0e,0xdc,0xea,0xcf,0x33,0xa9,0x0d,0x09,0x4c,0x9b,0x57,0x0d, \
      0x8d,0x40,0xc2,0x77,0x4a,0x5f,0x60,0x67,0x95,0x32,0x77,0x85,0x9a,0x6b,0xa4,0x13}}
static inline enum kui_native_cdda_binding kui_native_cdda_boot_binding(
        uint32_t bytes,uint32_t crc) {
    if(bytes==748444u && crc==UINT32_C(0xcdc493b3)) return KUI_NATIVE_CDDA_TOY;
    if(bytes==750804u && crc==UINT32_C(0x682d90e3)) return KUI_NATIVE_CDDA_MDK2;
    return KUI_NATIVE_CDDA_NONE;
}
static inline unsigned kui_native_cdda_capacity(enum kui_native_cdda_binding binding) {
    return binding==KUI_NATIVE_CDDA_MDK2?KUI_MDK2_CDDA_MANIFEST_SLOTS:
        binding==KUI_NATIVE_CDDA_TOY?64u:0u;
}
/* This accepts a bounded complete inventory rather than a single GD layout.
 * Audio always remains raw 2352; cooked is only supported by the Toy DATA
 * adapter. Changed audio/FAD arrangements are experimental: the executable
 * may itself assume its original track numbers or absolute play positions. */
static inline bool kui_native_cdda_manifest_compatible(
        const struct kui_retail_manifest *m,enum kui_native_cdda_binding binding,bool cooked) {
    const unsigned capacity=kui_native_cdda_capacity(binding);
    if(!m || !capacity || (cooked && binding!=KUI_NATIVE_CDDA_TOY) ||
       m->storage_transport!=KUI_STORAGE_SCI || m->reader!=KUI_RETAIL_READER_STANDARD ||
       m->flags!=KUI_RETAIL_IMAGE_BOOT_CRC || m->session_lba<45000u ||
       kui_native_cdda_boot_binding(m->boot_bytes,m->boot_crc32)!=binding ||
       !m->track_count || m->track_count>KUI_RETAIL_IMAGE_TRACKS ||
       m->track_count>=capacity || !m->extent_count ||
       m->extent_count>capacity-m->track_count ||
       m->track_count+m->extent_count>KUI_RETAIL_MANIFEST_SLOTS ||
       kui_retail_manifest_validate(m)!=KUI_GAME_OK) return false;
    unsigned audio=0u;
    for(unsigned i=0;i<m->track_count;i++) {
        const struct kui_retail_track *t=&m->slots[i].track;
        const bool data=kui_retail_track_control(t)!=0u;
        if(!t->extent_count || kui_retail_track_file_offset(t) ||
           t->control!=(data?(4u|(cooked?KUI_RETAIL_TRACK_COOKED:0u)):0u) ||
           kui_retail_track_sector_bytes(t)!=(data && cooked?2048u:2352u)) return false;
        if(!data && t->start_lba>=m->session_lba) ++audio;
    }
    return audio!=0u;
}
#endif
