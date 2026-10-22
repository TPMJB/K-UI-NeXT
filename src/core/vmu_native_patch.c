/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_patch.h"
static uint32_t physical(uint32_t a) {return a&0x1fffffffu;}
static bool ram(uint32_t a,uint32_t n) {
    uint32_t p=physical(a),area=a&0xe0000000u;
    return (area==0u || area==0x80000000u || area==0xa0000000u) && n &&
        p>=0x0c010000u && p<0x0d000000u && n<=0x0d000000u-p;
}
bool kui_vmu_native_patch_no_interiors(const void *image,size_t bytes,uint32_t load,
    const struct kui_vmu_native_patch_window *windows,size_t count) {
    if(!image || bytes>0xc00000u || bytes<2u ||
       bytes>UINTPTR_MAX-(uintptr_t)image || !ram(load,(uint32_t)bytes) ||
       !windows || !count || count>8u || (load&1u)) return false;
    for(size_t i=0;i<count;i++) {
        uint32_t begin=windows[i].begin,span=windows[i].bytes;
        if(!ram(begin,span) || ((begin|span)&1u) || span<2u ||
           physical(begin)<physical(load) || physical(begin)-physical(load)>bytes ||
           span>bytes-(physical(begin)-physical(load))) return false;
        for(size_t j=0;j<i;j++) if(physical(begin)<physical(windows[j].begin)+windows[j].bytes &&
           physical(windows[j].begin)<physical(begin)+span) return false;
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
        uint32_t target=physical(load+at+4u+(uint32_t)(displacement*2));
        for(size_t i=0;branch && i<count;i++) if(target>physical(windows[i].begin) &&
           target<physical(windows[i].begin)+windows[i].bytes) return false;
        if(!((load+at)&3u) && at+4u<=bytes) {
            uint32_t pointer=op|(uint32_t)data[at+2u]<<16|(uint32_t)data[at+3u]<<24;
            target=physical(pointer);
            for(size_t i=0;ram(pointer,1u) && i<count;i++) if(target>physical(windows[i].begin) &&
               target<physical(windows[i].begin)+windows[i].bytes) return false;
        }
    }
    return true;
}
bool kui_vmu_native_patch_no_interior(const void *image,size_t bytes,uint32_t load,
    uint32_t begin,uint32_t span) {
    const struct kui_vmu_native_patch_window window={begin,span};
    return kui_vmu_native_patch_no_interiors(image,bytes,load,&window,1u);
}
