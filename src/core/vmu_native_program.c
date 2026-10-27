/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_program.h"
#include "kui/vmu_native_heap.h"
#include "kui/vmu_native_exit.h"
#include <string.h>

enum { IP_BYTES=0x8000, IP_SEARCH_BYTES=0x10000 };
static const uint8_t signature[]="SEGA SEGAKATANA SEGA ENTERPRISES";
#if KUI_NATIVE_STAGE_FAST_SCAN
__asm__(".global __kui_native_stage_fast_scan\n.set __kui_native_stage_fast_scan,1\n");
#include "kui/native_scan_bytes.h"
#define u16 kui_native_scan_le16
#define u32 kui_native_scan_le32
#else
static uint16_t u16(const uint8_t *p) {return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p) {return (uint32_t)u16(p)|(uint32_t)u16(p+2u)<<16;}
#endif
static uint32_t physical(uint32_t a) {return a&UINT32_C(0x1fffffff);}
static bool direct(uint32_t a) {
    uint32_t area=a&UINT32_C(0xe0000000),p=physical(a);
    return (area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000)) &&
        p>=UINT32_C(0x0c000000) && p<UINT32_C(0x0d000000);
}
static bool overlap(uintptr_t a,size_t an,uintptr_t b,size_t bn) {
    if(an>UINTPTR_MAX-a || bn>UINTPTR_MAX-b) return true;
    if(a<b+bn && b<a+an) return true;
#if UINTPTR_MAX>UINT32_MAX
    if(a>UINT32_MAX || b>UINT32_MAX) return false;
#endif
    return direct((uint32_t)a) && direct((uint32_t)b) &&
        an<=UINT32_C(0x0d000000)-physical((uint32_t)a) &&
        bn<=UINT32_C(0x0d000000)-physical((uint32_t)b) &&
        physical((uint32_t)a)<physical((uint32_t)b)+bn && physical((uint32_t)b)<physical((uint32_t)a)+an;
}
static bool bootstrap_hint(const uint8_t *p,size_t n) {
    /* A truncated/moved header must not silently turn into arbitrary data.
     * No hint authorizes normalization: the complete independent checks below
     * remain mandatory. Limit the search to a possible trailing bootstrap. */
    size_t first=n>IP_SEARCH_BYTES?n-IP_SEARCH_BYTES:0u;
    for(size_t at=first;at+32u<=n;at++)
        if((p[at]==signature[0] && !memcmp(p+at,signature,16u)) ||
           (p[at+16u]==signature[16] && !memcmp(p+at+16u,signature+16u,16u))) return true;
    return false;
}
static bool bootstrap(const uint8_t *p) {
    if(memcmp(p,signature,32u) || memcmp(p+37u,"GD-ROM",6u) ||
       memcmp(p+0x60u,"1ST_READ.BIN    ",16u)) return false;
    for(unsigned i=0x20u;i<0x100u;i++) if(p[i]<32u || p[i]>126u) return false;
    /* Reviewed IP entry establishes its own canonical boot link domain. The
     * setup, delay slot, PC-relative bindings and fixed platform literals are
     * all exact; product/version/title fields are deliberately not selectors. */
    static const uint16_t entry[]={0xd007u,0xd108u,0x1017u,0xdf05u,0xd007u,
        0xd108u,0x412au,0x402bu,0x0009u};
    for(unsigned i=0;i<sizeof(entry)/sizeof(entry[0]);i++) if(u16(p+0x300u+2u*i)!=entry[i]) return false;
    if(u32(p+0x31cu)!=UINT32_C(0x7e001000) || u32(p+0x320u)!=UINT32_C(0xff000000) ||
       u32(p+0x324u)!=UINT32_C(0x0000092b) || u32(p+0x328u)!=UINT32_C(0x8c0083a8) ||
       u32(p+0x32cu)!=UINT32_C(0xac00b700)) return false;
    /* This admitted bootstrap format reserves its final four KiB as zero
     * fill. Unknown appended payloads cannot be hidden in that reservation. */
    for(unsigned i=0x7000u;i<IP_BYTES;i++) if(p[i]) return false;
    return true;
}
static bool prefix_references(const uint8_t *p,size_t prefix,size_t total,uint32_t load) {
    uint32_t first=physical(load)+(uint32_t)prefix,last=physical(load)+(uint32_t)total;
    unsigned boundaries=0u;
    for(size_t at=0;at+2u<=prefix;at+=2u) {
        if(!(at&3u) && prefix-at>=4u) {
            uint32_t value=u32(p+at);
            if(direct(value) && physical(value)>=first && physical(value)<last) {
                /* The one linker end word may name the reclaimed boundary;
                 * no interior address, second boundary word or alias escapes. */
                if(physical(value)!=first || ++boundaries>1u) return false;
            }
        }
        uint16_t op=u16(p+at);uint32_t target=0u;bool branch=false;
        if((op&UINT16_C(0xe000))==UINT16_C(0xa000)) {
            int32_t displacement=op&0xfffu;if(displacement&0x800) displacement-=0x1000;
            target=physical(load)+(uint32_t)at+4u+(uint32_t)(displacement*2);branch=true;
        } else if((op&0xff00u)==0x8900u || (op&0xff00u)==0x8b00u ||
                  (op&0xff00u)==0x8d00u || (op&0xff00u)==0x8f00u) {
            int32_t displacement=op&255u;if(displacement&128) displacement-=256;
            target=physical(load)+(uint32_t)at+4u+(uint32_t)(displacement*2);branch=true;
        }
        if(branch && target>=first && target<last) return false;
        /* Literal/MOVA transport can reach the suffix without an absolute
         * address word. Reject the target slot itself as well as its value. */
        if((op&0xf000u)==0xd000u || (op&0xff00u)==0xc700u) {
            target=((physical(load)+(uint32_t)at+4u)&~3u)+4u*(op&255u);
            if(target>=first && target<last) return false;
        } else if((op&0xf000u)==0x9000u) {
            target=physical(load)+(uint32_t)at+4u+2u*(op&255u);
            if(target>=first && target<last) return false;
        }
    }
    return boundaries==1u;
}
bool kui_vmu_native_program_extent(const void *image,size_t n,uint32_t load,size_t *out) {
    if(!image || !out || n<0x300u || n>UINT32_C(0xc00000) || (load&3u) ||
       !direct(load) || physical(load)<UINT32_C(0x0c010000) ||
       n>UINT32_C(0x0d000000)-physical(load) ||
       overlap((uintptr_t)image,n,(uintptr_t)out,sizeof(*out))) return false;
    const uint8_t *p=image;
    if(!bootstrap_hint(p,n)) {*out=n;return true;}
    if(n<IP_BYTES+0x300u) return false;
    size_t prefix=n-IP_BYTES;
    if((prefix&31u) || !bootstrap(p+prefix) || !prefix_references(p,prefix,n,load)) return false;
    struct kui_vmu_native_heap raw,program;struct kui_vmu_native_heap_call before,after;
    if(kui_vmu_native_heap_discover(image,n,load,&raw)!=KUI_VMU_NATIVE_HEAP_FOUND ||
       !kui_vmu_native_heap_call_discover(image,n,&raw,&before) ||
       kui_vmu_native_heap_discover(image,prefix,load,&program)!=KUI_VMU_NATIVE_HEAP_FOUND ||
       !kui_vmu_native_heap_call_discover(image,prefix,&program,&after)) return false;
    raw.image_bytes=(uint32_t)prefix;
    if(memcmp(&raw,&program,sizeof(raw)) || memcmp(&before,&after,sizeof(before)) ||
       physical(before.entry_begin)!=physical(load)+(uint32_t)prefix+32u ||
       before.entry_bytes!=UINT32_C(0x0d000000)-physical(before.entry_begin)) return false;
    struct kui_vmu_native_exit exits;
    if(!kui_vmu_native_exit_discover(image,prefix,load,&exits)) return false;
    *out=prefix;return true;
}
