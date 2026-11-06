/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_STANDALONE_FOREGROUND_H
#define KUI_NATIVE_STANDALONE_FOREGROUND_H
#include "kui/retail_image.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef KUI_NATIVE_STANDALONE_FOREGROUND
#define KUI_NATIVE_STANDALONE_FOREGROUND 0
#endif
#if KUI_NATIVE_STANDALONE_FOREGROUND != 0 && KUI_NATIVE_STANDALONE_FOREGROUND != 1
#error "Foreground control selector must be zero or one"
#endif
#if KUI_NATIVE_STANDALONE_FOREGROUND && \
    (!KUI_NATIVE_STANDALONE_READER || !KUI_NATIVE_STANDALONE_STRUCTURAL)
#error "Foreground control requires its separate structural standalone profile"
#endif
/* Separate Standard-route control; this is not the New-reader capability. */
#define KUI_NATIVE_FOREGROUND_CAP_MAGIC0 UINT32_C(0x3146474b)
#define KUI_NATIVE_FOREGROUND_CAP_MAGIC1 UINT32_C(0x574f4c46)
#define KUI_NATIVE_FOREGROUND_CAP_BYTES 32u
#define KUI_NATIVE_FOREGROUND_CAP_VC2_READONLY 1u
#define KUI_NATIVE_FOREGROUND_CAP_VC2_LATE 2u
#define KUI_NATIVE_FOREGROUND_CAP_VC2_PERSIST 3u
#define KUI_NATIVE_FOREGROUND_CAP_SMASH_MENU 4u
#define KUI_NATIVE_FOREGROUND_VC2_SLOTS 48u
#define KUI_NATIVE_FOREGROUND_SMASH_SLOTS 48u
struct kui_native_foreground_capability {
    uint32_t magic0,magic1,version,bytes,reader,slots,lease_bytes,flags;
};
_Static_assert(sizeof(struct kui_native_foreground_capability)==KUI_NATIVE_FOREGROUND_CAP_BYTES,
    "Foreground control capability ABI");
static inline bool kui_native_foreground_manifest_valid(const struct kui_retail_manifest *m) {
    return m && m->storage_transport==KUI_STORAGE_SCI &&
        m->reader==KUI_RETAIL_READER_STANDARD &&
        m->track_count && m->track_count<=KUI_RETAIL_ASYNC_SLOTS &&
        m->extent_count && m->extent_count<=KUI_RETAIL_ASYNC_SLOTS-m->track_count &&
        !(m->flags&~KUI_RETAIL_IMAGE_BOOT_CRC);
}
static inline uint32_t kui_native_foreground_u32(const volatile uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static inline bool kui_native_foreground_descriptor_valid(const volatile void *data,size_t bytes) {
    const volatile uint8_t *p=data;
    if(!p || bytes<KUI_NATIVE_FOREGROUND_CAP_BYTES)return false;
    const uint32_t flags=kui_native_foreground_u32(p+28u);
    const uint32_t slots=(flags==KUI_NATIVE_FOREGROUND_CAP_VC2_READONLY ||
        flags==KUI_NATIVE_FOREGROUND_CAP_VC2_LATE ||
        flags==KUI_NATIVE_FOREGROUND_CAP_VC2_PERSIST || flags==KUI_NATIVE_FOREGROUND_CAP_SMASH_MENU)?
        KUI_NATIVE_FOREGROUND_VC2_SLOTS:KUI_RETAIL_ASYNC_SLOTS;
    return p && bytes>=KUI_NATIVE_FOREGROUND_CAP_BYTES &&
        kui_native_foreground_u32(p)==KUI_NATIVE_FOREGROUND_CAP_MAGIC0 &&
        kui_native_foreground_u32(p+4u)==KUI_NATIVE_FOREGROUND_CAP_MAGIC1 &&
        kui_native_foreground_u32(p+8u)==1u &&
        kui_native_foreground_u32(p+12u)==KUI_NATIVE_FOREGROUND_CAP_BYTES &&
        kui_native_foreground_u32(p+16u)==KUI_RETAIL_READER_STANDARD &&
        kui_native_foreground_u32(p+20u)==slots &&
        !kui_native_foreground_u32(p+24u) &&
        (flags==0u || flags==KUI_NATIVE_FOREGROUND_CAP_VC2_READONLY ||
         flags==KUI_NATIVE_FOREGROUND_CAP_VC2_LATE ||
         flags==KUI_NATIVE_FOREGROUND_CAP_VC2_PERSIST || flags==KUI_NATIVE_FOREGROUND_CAP_SMASH_MENU);
}
static inline int kui_native_foreground_descriptor_find(const void *data,size_t bytes,size_t *offset) {
    const volatile uint8_t *p=data;size_t found=0u;bool have=false;
    if(!p || bytes<8u)return 0;
    for(size_t i=0;i<=bytes-8u;i++) {
        if(kui_native_foreground_u32(p+i)!=KUI_NATIVE_FOREGROUND_CAP_MAGIC0 ||
           kui_native_foreground_u32(p+i+4u)!=KUI_NATIVE_FOREGROUND_CAP_MAGIC1 ||
           !kui_native_foreground_descriptor_valid(p+i,bytes-i))continue;
        if(have)return -1;
        have=true;found=i;
    }
    if(have && offset)*offset=found;
    return have?1:0;
}
#endif
