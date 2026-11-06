/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_source_owner.h"
#include "kui/core.h"
#include <stdbool.h>
#include <string.h>
static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
int kui_native_source_owner_find(const void *image,size_t bytes,struct kui_native_source_owner *out) {
    static const uint8_t magic[8]={'K','U','I','N','O','W','N','1'};
    if(!image || bytes>UINTPTR_MAX-(uintptr_t)image ||
       (out && ((sizeof(*out)>UINTPTR_MAX-(uintptr_t)out) ||
        ((uintptr_t)out<(uintptr_t)image+bytes && (uintptr_t)image<(uintptr_t)out+sizeof(*out)))))return -1;
    const uint8_t *p=image;bool found=false;struct kui_native_source_owner result;
    for(size_t at=0u;at+KUI_NATIVE_SOURCE_OWNER_BYTES<=bytes;at++) {
        const uint8_t *r=p+at;
        if(memcmp(r,magic,sizeof(magic)) || word(r+8u)!=1u || word(r+12u)!=KUI_NATIVE_SOURCE_OWNER_BYTES)continue;
        if(found || (at&3u) || word(r+16u)<768u || word(r+16u)>UINT32_C(0xc00000) ||
           word(r+20u)!=UINT32_C(0x8c010000) || word(r+56u)!=1u ||
           kui_crc32(0u,r,60u)!=word(r+60u))return -1;
        result.image_bytes=word(r+16u);result.load=word(r+20u);memcpy(result.sha256,r+24u,32u);found=true;
    }
    if(found && out)*out=result;
    return found?1:0;
}
