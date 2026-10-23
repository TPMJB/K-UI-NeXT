/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_mode.h"

static uint32_t little32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
enum kui_vmu_native_mode kui_vmu_native_mode_decode(const void *wire,size_t bytes) {
    const uint8_t *p=wire;
    static const uint8_t magic[8]={'K','U','I','N','V','M','0','1'};
    if(!p || bytes!=KUI_VMU_NATIVE_MODE_BYTES) return KUI_VMU_NATIVE_MODE_INVALID;
    for(unsigned i=0;i<sizeof(magic);i++) if(p[i]!=magic[i]) return KUI_VMU_NATIVE_MODE_INVALID;
    if(little32(p+8u)!=1u || little32(p+12u)!=KUI_VMU_NATIVE_MODE_BYTES ||
       little32(p+16u)>KUI_VMU_NATIVE_MODE_RAW_CHAIN) return KUI_VMU_NATIVE_MODE_INVALID;
    for(unsigned i=20u;i<KUI_VMU_NATIVE_MODE_BYTES;i++)
        if(p[i]) return KUI_VMU_NATIVE_MODE_INVALID;
    return (enum kui_vmu_native_mode)little32(p+16u);
}
