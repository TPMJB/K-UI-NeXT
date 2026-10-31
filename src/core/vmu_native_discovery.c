/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_discovery.h"
#include "kui/hash.h"
#include <string.h>

struct witness {int16_t offset;uint16_t value;};
/* Authored sparse contracts shared by the reviewed uniform SDK routines.
 * A new compiler/SDK shape needs another family, never a fuzzy acceptance. */
static const struct witness words[]={
    {0,0x9028},{2,0x63e2},{4,0x023e},{6,0x12c6},{8,0x9029},{10,0x63e2},
    {84,0x10f4},{94,0x0fd8},{-192,0x401b},{-258,0x60e2},{-256,0x001e},
    {-254,0x8801},{-252,0x8d02},{-248,0xa0d3},{-246,0xe0fd},
    {266,0x0316},{272,0x2320},{22,0x22ca},{24,0x0326},{58,0x22ca},
    {60,0x0326},{170,0x03d6},{176,0x23d0},{198,0x024e},{200,0x2228},
    {214,0x5036},{216,0xc801},{230,0x401b},{232,0x8906},{248,0xe000},
    {328,0x420b},{366,0x430b}
};
static uint16_t u16(const uint8_t *p) {return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p) {return (uint32_t)u16(p)|(uint32_t)u16(p+2)<<16;}
static uint32_t physical(uint32_t a) {return a&0x1fffffffu;}
/* U0, P1 and P2 expose the same physical Dreamcast RAM. A disjoint virtual
 * destination may still overwrite the supposedly immutable executable. */
static bool direct_ram_span(uintptr_t a,size_t n,uint32_t *p) {
#if UINTPTR_MAX > UINT32_MAX
    if(a>UINT32_MAX) return false;
#endif
    uint32_t address=(uint32_t)a,area=address&0xe0000000u;
    *p=physical(address);
    return (area==0u || area==0x80000000u || area==0xa0000000u) &&
        *p>=0x0c000000u && *p<0x0d000000u && n<=0x0d000000u-*p;
}
static bool aliased_overlap(uintptr_t a,size_t an,uintptr_t b,size_t bn) {
    uint32_t ap,bp;
    return direct_ram_span(a,an,&ap) && direct_ram_span(b,bn,&bp) &&
        ap<bp+bn && bp<ap+an;
}
static bool ram(uint32_t a) {
    uint32_t area=a&0xe0000000u,p=physical(a);
    return (area==0u || area==0x80000000u || area==0xa0000000u) &&
        p>=0x0c010000u && p<0x0d000000u;
}
static bool valid(const void *image,size_t n,uint32_t base) {
    return image && n>=2u && n<=0xc00000u && n<=UINTPTR_MAX-(uintptr_t)image &&
        ram(base) && !(base&3u) && n<=0x0d000000u-physical(base);
}
static bool index_of(uint32_t a,uint32_t base,size_t n,size_t bytes,size_t *at) {
    uint32_t p=physical(a),b=physical(base);
    if(!ram(a) || (a&3u) || p<b || p-b>n || bytes>n-(p-b)) return false;
    *at=p-b;return true;
}
static void hash(const void *p,size_t n,uint8_t out[32]) {
    struct kui_sha256 h;kui_sha256_init(&h);kui_sha256_update(&h,p,n);kui_sha256_digest(&h,out);
}
static bool sdk_at(const uint8_t *p,size_t n,uint32_t base,size_t site,
    struct kui_vmu_native_discovery *r) {
    if(site<0x53cu || site>n || n-site<0x1b8u || (site&1u)) return false;
    for(unsigned i=0;i<sizeof(words)/sizeof(words[0]);i++)
        if(u16(p+site+words[i].offset)!=words[i].value) return false;
    size_t init=site-0x53cu;
    static const uint16_t begin[5]={0x2fe6,0xde3e,0xd33e,0x9071,0x6233};
    for(unsigned i=0;i<5u;i++) if(u16(p+init+2u*i)!=begin[i]) return false;
    uint32_t manager_cell=u32(p+site+0x1a4u),lock_cell=u32(p+site+0x1a0u);
    /* Decode the initializer's two exact PC-relative loads, including its
     * halfword-aligned instruction position, rather than guessing RAM holes. */
    size_t cell=((base+(uint32_t)init+6u)&~3u)-base+4u*0x3eu;
    size_t object=((base+(uint32_t)init+8u)&~3u)-base+4u*0x3eu;
    if(cell>n || n-cell<4u || object>n || n-object<4u) return false;
    uint32_t manager=u32(p+object);
    if(!ram(manager_cell) || !ram(manager) || !ram(lock_cell) ||
       ((manager_cell|manager|lock_cell)&3u) ||
       u32(p+cell)!=manager_cell || physical(manager)>0x0d000000u-0x1100u ||
       physical(manager_cell)!=physical(manager)+0x10fcu) return false;
    size_t lock;
    if(!index_of(lock_cell,base,n,4u,&lock) || u32(p+lock)!=lock_cell+4u) return false;
    r->submit=base+(uint32_t)site;r->resume=r->submit+12u;
    r->initializer=base+(uint32_t)init;r->manager_cell=manager_cell;
    r->manager_initial=manager;r->lock_cell=lock_cell;return true;
}
static bool wrapper_at(const uint8_t *p,size_t n,uint32_t base,size_t site,size_t call,
    struct kui_vmu_native_discovery *r) {
    if(call<8u || call>n || n-call<6u || (call&1u)) return false;
    uint16_t load=u16(p+call),test=u16(p+call-8u);
    if((load&0xff00u)!=0xd300u || u16(p+call+2u)!=0x430bu ||
       u16(p+call+4u)!=0x0009u || (test!=0x4c11u && test!=0x4d11u) ||
       u16(p+call-6u)!=0x8901u) return false;
    uint32_t address=((base+(uint32_t)call+4u)&~3u)+4u*(load&255u);
    size_t slot;
    if(!index_of(address,base,n,20u,&slot) || slot<4u) return false;
    uint32_t consumer=u32(p+slot),gate=u32(p+slot-4u),ready=u32(p+slot+16u);
    if(!ram(consumer) || !ram(gate) || !ram(ready) || ((consumer|gate|ready)&1u) ||
       physical(consumer)!=physical(base)+(uint32_t)site+0x116u ||
       physical(gate)!=physical(base)+(uint32_t)site+0xbeu ||
       physical(ready)!=physical(base)+(uint32_t)site+0x100u) return false;
    r->completion_slot=address;r->completion_call=base+(uint32_t)call+2u;
    r->consumer=consumer;r->gate=gate;r->ready=ready;r->gate_register=(test>>8)&15u;
    return true;
}
/* A PC-relative MOV.L of register reg at offset at, decoded to its literal. */
static bool pc_literal(const uint8_t *p,size_t n,uint32_t base,size_t at,unsigned reg,uint32_t *value) {
    if(at>n || n-at<2u || (u16(p+at)&0xff00u)!=(0xd000u|reg<<8)) return false;
    size_t slot;
    if(!index_of(((base+(uint32_t)at+4u)&~3u)+4u*(u16(p+at)&255u),base,n,4u,&slot)) return false;
    *value=u32(p+slot);return true;
}
static bool pc_word(const uint8_t *p,size_t n,uint32_t base,size_t at,unsigned reg,uint16_t value) {
    if(at>n || n-at<2u || (u16(p+at)&0xff00u)!=(0x9000u|reg<<8)) return false;
    size_t slot=at+4u+2u*(u16(p+at)&255u);(void)base;
    return slot<=n && n-slot>=2u && u16(p+slot)==value;
}
/* The pd 1.46 server entry, completion_call-0x3a: it saves R8-R14 and PR,
 * calls the lock helper with the busy word set to one in the delay slot, and
 * records a -2 (lock held) result in manager+0x10e8 for the API wrappers'
 * retry. The helper returns -3 or -1 unless the driver is idle and Maple DMA
 * has stopped, then TAS.Bs the byte the lock cell points at: -2 if it was
 * already held, 0 once this server holds it. The other words are the
 * unchanged exit branches, identical in every reviewed image. */
static bool early_server(const uint8_t *p,size_t n,uint32_t base,const struct kui_vmu_native_discovery *r) {
    static const uint16_t entry[31]={
        0x2fe6,0x2fd6,0x2fc6,0x2fb6,0x2fa6,0x2f96,0x2f86,0x4f22,0x7ffc,0xd234,0xea01,0xd332,0xed00,
        0xd82f,0xd92f,0x420b,0x23a2,0x88fe,0x8f04,0x6c03,0x9053,0x6282,0xa0ab,0x02a6,0x4c11,0x8901,
        0xa09c,0x0009,0xd32b,0x430b,0x0009};
    static const uint16_t helper[33]={
        0xd339,0x906b,0x4f22,0x6432,0x024e,0x2228,0x8902,0x4f26,0x000b,0xe0fd,0x9063,0x034e,0x5036,
        0xc801,0x8902,0x4f26,0x000b,0xe0ff,0xd32f,0x6032,0x401b,0x8906,0xd12f,0xb0b6,0x2f16,0x7f04,
        0x4f26,0x000b,0xe0fe,0xe000,0x4f26,0x000b,0x0009};
    size_t s,h;uint32_t value,busy,lock,cell;
    if(r->completion_call<base+0x3au || !index_of(r->completion_call-0x3au,base,n,sizeof(entry),&s)) return false;
    for(unsigned i=0;i<31u;i++) if(u16(p+s+2u*i)!=entry[i]) return false;
    if(!pc_literal(p,n,base,s+18u,2u,&value) || (value&1u) ||
       !index_of(value&~3u,base,n,sizeof(helper)+2u,&h)) return false;
    h+=value&2u;
    for(unsigned i=0;i<33u;i++) if(u16(p+h+2u*i)!=helper[i]) return false;
    return pc_literal(p,n,base,s+22u,3u,&busy) && ram(busy) && !(busy&3u) &&
        pc_literal(p,n,base,s+26u,8u,&value) && value==r->manager_cell &&
        pc_literal(p,n,base,s+28u,9u,&cell) && ram(cell) && !(cell&3u) &&
        pc_word(p,n,base,s+40u,0u,0x10e8u) &&
        pc_literal(p,n,base,h,3u,&value) && value==r->manager_cell &&
        pc_word(p,n,base,h+2u,0u,0xfc0u) && pc_word(p,n,base,h+20u,0u,0x10f4u) &&
        pc_literal(p,n,base,h+36u,3u,&lock) && lock==r->lock_cell;
}
static void finish(struct kui_vmu_native_discovery *r,const void *p,size_t n,uint32_t base) {
    r->version=KUI_VMU_NATIVE_DISCOVERY_VERSION;r->bytes=sizeof(*r);
    r->family=early_server(p,n,base,r)?KUI_VMU_NATIVE_FAMILY_EARLY_V1:KUI_VMU_NATIVE_FAMILY_UNIFORM_V1;
    r->image_bytes=(uint32_t)n;
    r->load_address=base;r->requirements=KUI_VMU_NATIVE_REQUIREMENTS;hash(p,n,r->image_sha256);
}
enum kui_vmu_native_discovery_result kui_vmu_native_discover(
    const void *image,size_t n,uint32_t base,struct kui_vmu_native_discovery *out) {
    if(!out) return KUI_VMU_NATIVE_INVALID;
    uintptr_t start=(uintptr_t)image,dest=(uintptr_t)out;
    if(sizeof(*out)>UINTPTR_MAX-dest || n>UINTPTR_MAX-start ||
       (dest<start+n && start<dest+sizeof(*out)) ||
       aliased_overlap(start,n,dest,sizeof(*out))) return KUI_VMU_NATIVE_INVALID;
    memset(out,0,sizeof(*out));if(!valid(image,n,base)) return KUI_VMU_NATIVE_INVALID;
    const uint8_t *p=image;unsigned found=0,partial=0;
    struct kui_vmu_native_discovery chosen={0};
    for(size_t site=0x53cu;site<n && n-site>=0x1b8u;site+=2u) {
        if(u16(p+site)!=0x9028u || u16(p+site+2u)!=0x63e2u) continue;
        struct kui_vmu_native_discovery r={0};
        if(!sdk_at(p,n,base,site,&r)) continue;
        ++partial;
        for(size_t call=8u;call<n && n-call>=6u;call+=2u) {
            if(!wrapper_at(p,n,base,site,call,&r)) continue;
            if(++found>1u) return KUI_VMU_NATIVE_AMBIGUOUS;
            chosen=r;
        }
        /* More than one SDK body is ambiguous even if one wrapper is absent.
         * Never choose the first candidate from an image/overlay collection. */
        if(partial>1u) return KUI_VMU_NATIVE_AMBIGUOUS;
    }
    if(!found) return partial?KUI_VMU_NATIVE_INCOMPLETE:KUI_VMU_NATIVE_NOT_FOUND;
    finish(&chosen,image,n,base);*out=chosen;return KUI_VMU_NATIVE_RESOLVED;
}
bool kui_vmu_native_discovery_cache_valid(const struct kui_vmu_native_discovery *r,
    const void *image,size_t n,uint32_t base) {
    if(!r || !valid(image,n,base) || r->version!=KUI_VMU_NATIVE_DISCOVERY_VERSION ||
       r->bytes!=sizeof(*r) || !kui_vmu_native_family_valid(r->family) ||
       r->requirements!=KUI_VMU_NATIVE_REQUIREMENTS || r->image_bytes!=n ||
       r->load_address!=base || r->submit<base || r->completion_call<base+2u) return false;
    /* A cache is untrusted metadata. Recheck uniqueness as well as hashes:
     * a forged record must not select one of several plausible SDK copies.
     * This conservative reference implementation trades cache speed for
     * complete replay of discovery; an installer can never trust addresses
     * merely because a previous boot wrote a file containing them. */
    struct kui_vmu_native_discovery fresh;
    return kui_vmu_native_discover(image,n,base,&fresh)==KUI_VMU_NATIVE_RESOLVED &&
        !memcmp(r,&fresh,sizeof(fresh));
}
