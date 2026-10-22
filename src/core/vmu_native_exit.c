/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_exit.h"
#include "kui/vmu_native_patch.h"
#include <string.h>
struct witness {uint16_t at,word;};
static uint16_t u16(const uint8_t *p) {return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p) {return (uint32_t)u16(p)|(uint32_t)u16(p+2u)<<16;}
static uint32_t physical(uint32_t a) {return a&UINT32_C(0x1fffffff);}
static bool direct(uint32_t a) {
    uint32_t area=a&UINT32_C(0xe0000000),p=physical(a);
    return (area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000)) &&
        p>=UINT32_C(0x0c000000) && p<UINT32_C(0x0d000000);
}
static bool in_image(uint32_t a,uint32_t load,size_t bytes,uint32_t span) {
    uint32_t at=physical(a),base=physical(load);
    return direct(a) && at>=base && at-base<=bytes && span<=bytes-(at-base);
}
static bool words(const uint8_t *p,size_t at,const struct witness *w,size_t count) {
    for(size_t i=0;i<count;i++) if(u16(p+at+w[i].at)!=w[i].word) return false;
    return true;
}
#define WORDS(p,at,w) words((p),(at),(w),sizeof(w)/sizeof((w)[0]))
static bool literal(const uint8_t *p,size_t bytes,uint32_t load,size_t at,unsigned reg,
    uint32_t *address,uint32_t *value) {
    if(at>bytes || bytes-at<2u) return false;
    uint16_t op=u16(p+at);
    if((op&UINT16_C(0xff00))!=(UINT16_C(0xd000)|(reg<<8))) return false;
    *address=((load+(uint32_t)at+4u)&~3u)+4u*(op&255u);
    if(!in_image(*address,load,bytes,4u)) return false;
    *value=u32(p+physical(*address)-physical(load));return true;
}
static bool value_at(const uint8_t *p,size_t bytes,uint32_t load,size_t at,unsigned reg,
    size_t slot,uint32_t expected) {
    uint32_t address,value;
    return literal(p,bytes,load,at,reg,&address,&value) &&
        address==load+(uint32_t)slot && value==expected;
}
static bool code_at(const uint8_t *p,size_t bytes,uint32_t load,size_t at,unsigned reg,
    size_t slot,uint32_t *value) {
    uint32_t address;
    return literal(p,bytes,load,at,reg,&address,value) && address==load+(uint32_t)slot &&
        !(*value&1u) && in_image(*value,load,bytes,2u);
}
static bool exit_pair(const uint8_t *p,size_t bytes,uint32_t load,size_t at,uint32_t *cleanup) {
    /* Both complete SDK exits, before any cleanup or branch to boot RAM.
     * PC-relative loads are decoded and bound to their shared literal pool. */
    static const struct witness w[]={
        {2,0x6030},{4,0x600c},{6,0x8801},{8,0x8b0e},{12,0x6020},{14,0x600c},
        {16,0x8801},{18,0x8b02},{22,0x412b},{24,0x0009},{30,0x2012},
        {34,0x6312},{36,0x432b},{38,0xe403},{42,0x6212},{44,0x422b},{46,0xe401},
        {48,0x4f22},{52,0x430b},{54,0x0009},{58,0xe402},{60,0x6032},
        {62,0x402b},{64,0x4f26},{66,0x0000}
    };
    return bytes-at>=0x60u && WORDS(p,at,w) &&
        value_at(p,bytes,load,at,3u,at+0x44u,UINT32_C(0x8c0080fc)) &&
        value_at(p,bytes,load,at+10u,2u,at+0x48u,UINT32_C(0x8c0080fe)) &&
        value_at(p,bytes,load,at+20u,1u,at+0x4cu,UINT32_C(0x8c008300)) &&
        value_at(p,bytes,load,at+26u,0u,at+0x54u,UINT32_C(0x8c000064)) &&
        value_at(p,bytes,load,at+28u,1u,at+0x50u,UINT32_C(0x8c008000)) &&
        value_at(p,bytes,load,at+32u,1u,at+0x58u,UINT32_C(0x8c0000e0)) &&
        value_at(p,bytes,load,at+40u,1u,at+0x58u,UINT32_C(0x8c0000e0)) &&
        value_at(p,bytes,load,at+56u,3u,at+0x58u,UINT32_C(0x8c0000e0)) &&
        code_at(p,bytes,load,at+50u,3u,at+0x5cu,cleanup);
}
static bool exec_loader(const uint8_t *p,size_t bytes,uint32_t load,size_t at,uint32_t exit) {
    /* Generic executable loader transports its saved R5 as an indirect call
     * target after loading a file and invoking cleanup. Its reviewed thunk
     * supplies 8c008300, so interception must precede the loader's entry. */
    static const struct witness w[]={
        {0,0x2fe6},{2,0x2fd6},{4,0x4f22},{6,0x7ff4},{8,0x1f42},{14,0x1f51},
        {16,0x430b},{18,0xed00},{22,0x65d3},{24,0x430b},{26,0x54f2},
        {28,0x6e03},{30,0x2ee8},{32,0x8911},{36,0x65f3},{38,0x430b},{40,0x64e3},
        {44,0x65f2},{46,0x56f1},{48,0x430b},{50,0x64e3},{52,0x2008},
        {54,0x8d01},{56,0x64e3},{58,0xedff},{62,0x430b},{64,0x0009},
        {66,0xa001},{68,0x0009},{70,0xedff},{74,0x430b},{76,0x0009},
        {78,0x2dd8},{80,0x8902},{84,0x430b},{86,0x0009},{88,0x52f1},
        {90,0x420b},{92,0x0009},{94,0xe001},{96,0x7f0c},{98,0x4f26},
        {100,0x6df6},{102,0x000b},{104,0x6ef6},{110,0xafc7},{112,0xe601},{114,0x0000}
    };
    if(bytes-at<0x9cu || !WORDS(p,at,w)) return false;
    uint32_t address,context,thunk_context,target;
    if(!literal(p,bytes,load,at+10u,4u,&address,&context) || address!=load+at+0x74u ||
       (context&3u) || !in_image(context,load,bytes,8u) ||
       !literal(p,bytes,load,at+106u,4u,&address,&thunk_context) || address!=load+at+0x98u ||
       thunk_context!=context+4u ||
       !value_at(p,bytes,load,at+108u,5u,at+0x94u,UINT32_C(0x8c008300))) return false;
    static const uint16_t calls[]={12u,20u,34u,42u,60u,72u,82u};
    for(unsigned i=0;i<sizeof(calls)/sizeof(calls[0]);i++) {
        if(!code_at(p,bytes,load,at+calls[i],3u,at+0x78u+4u*i,&target)) return false;
        if(i==6u && physical(target)!=physical(exit)) return false;
    }
    return true;
}
static bool consumers(const uint8_t *p,size_t bytes,uint32_t load,size_t pair,size_t exec) {
    unsigned vectors=0u,boots=0u;
    for(size_t at=0;at+2u<=bytes;at+=2u) {
        uint16_t op=u16(p+at);uint32_t address,value;
        if((op&UINT16_C(0xf000))!=UINT16_C(0xd000) ||
           !literal(p,bytes,load,at,(op>>8)&15u,&address,&value) || !direct(value)) continue;
        if(physical(value)==UINT32_C(0x0c0000e0)) {
            if(at!=pair+32u && at!=pair+40u && at!=pair+56u) return false;
            ++vectors;
        } else if(physical(value)==UINT32_C(0x0c008300)) {
            if(at!=pair+20u && at!=exec+108u) return false;
            ++boots;
        }
    }
    return vectors==3u && boots==2u;
}
static bool aliases_overlap(uintptr_t a,size_t an,uintptr_t b,size_t bn) {
#if UINTPTR_MAX>UINT32_MAX
    if(a>UINT32_MAX || b>UINT32_MAX) return false;
#endif
    return direct((uint32_t)a) && direct((uint32_t)b) &&
        an<=UINT32_C(0x0d000000)-physical((uint32_t)a) &&
        bn<=UINT32_C(0x0d000000)-physical((uint32_t)b) &&
        physical((uint32_t)a)<physical((uint32_t)b)+bn &&
        physical((uint32_t)b)<physical((uint32_t)a)+an;
}
bool kui_vmu_native_exit_discover(const void *image,size_t bytes,uint32_t load,
    struct kui_vmu_native_exit *out) {
    if(!out) return false;
    uintptr_t begin=(uintptr_t)image,dest=(uintptr_t)out;
    if(bytes>UINTPTR_MAX-begin || sizeof(*out)>UINTPTR_MAX-dest ||
       (dest<begin+bytes && begin<dest+sizeof(*out)) ||
       aliases_overlap(begin,bytes,dest,sizeof(*out))) return false;
    memset(out,0,sizeof(*out));
    if(!image || bytes<0x9cu || bytes>UINT32_C(0xc00000) || (load&3u) ||
       physical(load)<UINT32_C(0x0c010000) || !direct(load) ||
       bytes>UINT32_C(0x0d000000)-physical(load)) return false;
    const uint8_t *p=image;unsigned count=0u;size_t pair=0u,exec=0u;uint32_t cleanup=0u;
    for(size_t at=0;at+0x60u<=bytes;at+=4u) {
        if((u16(p+at)&UINT16_C(0xff00))!=UINT16_C(0xd300) || u16(p+at+2u)!=UINT16_C(0x6030)) continue;
        uint32_t target;
        if(exit_pair(p,bytes,load,at,&target)) {pair=at;cleanup=target;if(++count>1u) return false;}
    }
    if(count!=1u) return false;
    count=0u;
    for(size_t at=0;at+0x9cu<=bytes;at+=4u) {
        if(u16(p+at)!=UINT16_C(0x2fe6) || u16(p+at+2u)!=UINT16_C(0x2fd6) ||
           u16(p+at+4u)!=UINT16_C(0x4f22) || u16(p+at+6u)!=UINT16_C(0x7ff4)) continue;
        if(exec_loader(p,bytes,load,at,load+(uint32_t)pair)) {exec=at;if(++count>1u) return false;}
    }
    if(count!=1u || !consumers(p,bytes,load,pair,exec)) return false;
    const struct kui_vmu_native_patch_window windows[]={
        {load+(uint32_t)pair,12u},{load+(uint32_t)pair+48u,12u},{load+(uint32_t)exec,12u}};
    if(!kui_vmu_native_patch_no_interiors(image,bytes,load,windows,3u)) return false;
    *out=(struct kui_vmu_native_exit){.version=KUI_VMU_NATIVE_EXIT_VERSION,.bytes=sizeof(*out),
        .family=KUI_VMU_NATIVE_EXIT_FAMILY,.load=load,.image_bytes=(uint32_t)bytes,.count=3u,
        .entries={windows[0].begin,windows[1].begin,windows[2].begin},
        .bios_vector_literal=load+(uint32_t)pair+0x58u,.exec_thunk=load+(uint32_t)exec+0x6au,.cleanup=cleanup};
    return true;
}
