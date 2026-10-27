/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_patch.h"
static uint32_t physical(uint32_t a) {return a&0x1fffffffu;}
static bool ram(uint32_t a,uint32_t n) {
    uint32_t p=physical(a),area=a&0xe0000000u;
    return (area==0u || area==0x80000000u || area==0xa0000000u) && n &&
        p>=0x0c010000u && p<0x0d000000u && n<=0x0d000000u-p;
}
bool kui_vmu_native_patch_encode(uint32_t address,uint32_t target,uint16_t out[6]) {
    uint32_t area=target&UINT32_C(0xe0000000);
    if(!out || (address&1u) || (target&1u) || !ram(address,12u) ||
       (area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000)) ||
       physical(target)<UINT32_C(0x0c000000) || physical(target)>UINT32_C(0x0cfffffe)) return false;
    uint16_t patch[6]={UINT16_C(0xd001),UINT16_C(0x402b),9u,9u,9u,9u};
    unsigned literal=(address&2u)?3u:4u;
    patch[literal]=(uint16_t)target;patch[literal+1u]=(uint16_t)(target>>16);
    for(unsigned i=0;i<6u;i++) out[i]=patch[i];
    return true;
}
bool kui_vmu_native_patch_no_interiors(const void *image,size_t bytes,uint32_t load,
    const struct kui_vmu_native_patch_window *windows,size_t count) {
    if(!image || bytes>0xc00000u || bytes<2u ||
       bytes>UINTPTR_MAX-(uintptr_t)image || !ram(load,(uint32_t)bytes) ||
       !windows || !count || count>8u || (load&1u)) return false;
    uint32_t begins[8],ends[8],first=UINT32_MAX,last=0u;
    for(size_t i=0;i<count;i++) {
        uint32_t begin=windows[i].begin,span=windows[i].bytes;
        if(!ram(begin,span) || ((begin|span)&1u) || span<2u ||
           physical(begin)<physical(load) || physical(begin)-physical(load)>bytes ||
           span>bytes-(physical(begin)-physical(load))) return false;
        begins[i]=physical(begin);ends[i]=begins[i]+span;
        for(size_t j=0;j<i;j++) if(begins[i]<ends[j] && begins[j]<ends[i]) return false;
        if(begins[i]<first) first=begins[i];
        if(ends[i]>last) last=ends[i];
    }
    const uint8_t *data=image;
    for(uint32_t at=0;at+2u<=bytes;at+=2u) {
        uint32_t op=(uint32_t)data[at]|(uint32_t)data[at+1u]<<8;
        int32_t displacement=0;bool branch=false;
        if((op&0xf000u)==0xa000u || (op&0xf000u)==0xb000u) {
            displacement=(int32_t)(op&0xfffu);if(displacement&0x800) displacement-=0x1000;
            branch=true;
        } else if((op&0xff00u)==0x8900u || (op&0xff00u)==0x8b00u ||
                  (op&0xff00u)==0x8d00u || (op&0xff00u)==0x8f00u) {
            displacement=(int32_t)(op&0xffu);if(displacement&0x80) displacement-=0x100;
            branch=true;
        }
        /* The admitted windows are usually clustered in a small SDK region.
         * Most executable/data halfwords cannot reach it. Do not reread and
         * normalize every window for each unrelated branch or RAM literal. */
        uint32_t target;
        if(branch) {
            target=physical(load+at+4u+(uint32_t)(displacement*2));
            if(target>first && target<last)
                for(size_t i=0;i<count;i++) if(target>begins[i] && target<ends[i]) return false;
        }
        if(!((load+at)&3u) && at+4u<=bytes) {
            uint32_t pointer=op|(uint32_t)data[at+2u]<<16|(uint32_t)data[at+3u]<<24;
            target=physical(pointer);
            if(target>first && target<last && ram(pointer,1u))
                for(size_t i=0;i<count;i++) if(target>begins[i] && target<ends[i]) return false;
        }
    }
    return true;
}
bool kui_vmu_native_patch_no_interior(const void *image,size_t bytes,uint32_t load,
    uint32_t begin,uint32_t span) {
    const struct kui_vmu_native_patch_window window={begin,span};
    return kui_vmu_native_patch_no_interiors(image,bytes,load,&window,1u);
}
