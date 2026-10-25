/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_exit.h"
#include "kui/vmu_native_patch.h"
#include "kui/vmu_native_terminal_dispatch.h"
#include <string.h>
struct witness {uint16_t at,word;};
#if KUI_NATIVE_STAGE_FAST_SCAN
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
static bool short_at(const uint8_t *p,size_t bytes,uint32_t load,size_t at,unsigned reg,
    size_t slot,uint16_t expected) {
    if(at>bytes || bytes-at<2u || slot>bytes || bytes-slot<2u) return false;
    uint16_t op=u16(p+at);
    return (op&UINT16_C(0xff00))==(UINT16_C(0x9000)|(reg<<8)) &&
        load+(uint32_t)at+4u+2u*(op&255u)==load+(uint32_t)slot && u16(p+slot)==expected;
}
struct exit_shape {uint32_t second,vector,boot,vector_loads[3];};
struct exec_shape {uint32_t family,thunk,boot;};
/* The alternate table is useful only after its common fatal entry has itself
 * become a terminal hook. Prove all four pure argument wrappers, the complete
 * fatal call path to an already admitted SDK exit, and the actual allocator's
 * alternate/default selector wrappers. No arbitrary dispatch pointer is made
 * acceptable by the presence of a generic indirect call. */
static int terminal_dispatch(const uint8_t *p,size_t bytes,uint32_t load,
    uint32_t first_exit,uint32_t second_exit,uint32_t *common_out,uint32_t *table_out) {
    static const struct witness common_words[]={
        {0,0x4f22},{4,0x3f0c},{8,0x2f56},{10,0x2f46},{14,0x2f36},
        {16,0x64f3},{18,0x420b},{20,0x740c},{24,0x430b},{26,0x0009},
        {30,0x3f1c},{32,0x4f26},{34,0x000b},{36,0x0009}
    };
    static const struct witness exit_words[]={{0,0x4f22},{4,0x430b},{6,0x0009},
        {10,0x422b},{12,0x4f26}};
    unsigned found=0u;uint32_t selected_common=0u,selected_table=0u;
    struct kui_vmu_native_heap heap={0};bool heap_ready=false;
    for(size_t at=2u;at+0xf2u<=bytes;at+=4u) {
        if(u16(p+at)!=0x4f22u || !WORDS(p,at,common_words)) continue;
        uint32_t slot,value,log,fatal,cleanup,target;
        if(!literal(p,bytes,load,at+6u,3u,&slot,&value) || !in_image(value,load,bytes,1u) ||
           !literal(p,bytes,load,at+12u,2u,&slot,&log) || (log&1u) || !in_image(log,load,bytes,2u) ||
           !literal(p,bytes,load,at+22u,3u,&slot,&fatal) || (fatal&1u) || !in_image(fatal,load,bytes,14u)) continue;
        uint16_t op=u16(p+at+2u),op2=u16(p+at+28u);
        if((op&0xff00u)!=0x9000u || (op2&0xff00u)!=0x9100u) continue;
        size_t small=at+6u+2u*(op&255u),large=at+32u+2u*(op2&255u);
        if(small>bytes-2u || large>bytes-2u || u16(p+small)!=0xff00u || u16(p+large)!=0x010cu) continue;
        size_t fat=(size_t)(physical(fatal)-physical(load));
        if(!WORDS(p,fat,exit_words) ||
           !literal(p,bytes,load,fat+2u,3u,&slot,&cleanup) || (cleanup&1u) || !in_image(cleanup,load,bytes,2u) ||
           !literal(p,bytes,load,fat+8u,2u,&slot,&target) ||
           (target!=first_exit && target!=second_exit)) continue;
        uint32_t common=load+(uint32_t)at;bool wrappers=true;
        for(unsigned i=0;i<8u;i++) {
            const struct kui_vmu_native_terminal_dispatch_word *w=&kui_vmu_native_terminal_dispatch_words[i];
            if((u32(p+at+w->offset)&w->mask)!=w->value) {wrappers=false;break;}
        }
        if(!wrappers) continue;
        if(!heap_ready) {
            if(kui_vmu_native_heap_discover(p,bytes,load,&heap)!=KUI_VMU_NATIVE_HEAP_FOUND) return -1;
            heap_ready=true;
        }
        /* Both complete tail wrappers share the immutable selector pointer.
         * Their R4 arguments are exactly alternate and original tables. */
        if(u16(p+at+0x94u)!=0x432bu || u16(p+at+0x96u)!=0x0009u ||
           u16(p+at+0x9cu)!=0x432bu || u16(p+at+0x9eu)!=0x0009u ||
           !literal(p,bytes,load,at+0x90u,3u,&slot,&value) || value!=heap.initializer+0x46u ||
           !literal(p,bytes,load,at+0x98u,3u,&slot,&value) || value!=heap.initializer+0x46u ||
           !literal(p,bytes,load,at+0x92u,4u,&slot,&value) || (value&3u) || !in_image(value,load,bytes,16u)) continue;
        uint32_t table=value;
        if(table==heap.dispatch_table ||
           !literal(p,bytes,load,at+0x9au,4u,&slot,&value) || value!=heap.dispatch_table) continue;
        size_t tab=(size_t)(physical(table)-physical(load));bool entries=true;
        for(unsigned i=0;i<4u;i++)
            if(u32(p+tab+4u*i)!=common+kui_vmu_native_terminal_dispatch_entries[i]) {entries=false;break;}
        if(!entries) continue;
        /* A duplicate table cannot silently select a different capability. */
        unsigned copies=0u;
        for(size_t pos=0u;pos+16u<=bytes;pos+=4u) {
            bool same=true;
            for(unsigned i=0;i<4u;i++) if(u32(p+pos+4u*i)!=common+kui_vmu_native_terminal_dispatch_entries[i]) {same=false;break;}
            if(same) ++copies;
        }
        if(copies!=1u || ++found>1u) return -1;
        selected_common=common;selected_table=table;
    }
    if(!found) return 0;
    *common_out=selected_common;*table_out=selected_table;return 1;
}
static bool exec_prologue(const uint8_t *p,size_t bytes,size_t at) {
    if(bytes-at<24u) return false;
    /* A body whose thunk was removed/corrupted must not silently become a
     * pair-only image. These conservative exclusions do not admit a loader;
     * a complete exact loader is still required for the three-entry families. */
    if(u16(p+at)==0x2fe6u && u16(p+at+2u)==0x2fd6u && u16(p+at+4u)==0x4f22u &&
       u16(p+at+6u)==0x7ff4u && u16(p+at+8u)==0x1f42u &&
       (u16(p+at+10u)&0xff00u)==0xd400u && (u16(p+at+12u)&0xff00u)==0xd300u &&
       u16(p+at+14u)==0x1f51u && u16(p+at+16u)==0x430bu && u16(p+at+18u)==0xed00u)
        return true;
    return u16(p+at)==0x2fe6u && u16(p+at+2u)==0x4f22u && u16(p+at+4u)==0x7ff4u &&
        u16(p+at+6u)==0x1f42u && (u16(p+at+8u)&0xff00u)==0xd300u &&
        (u16(p+at+10u)&0xff00u)==0xd400u && u16(p+at+12u)==0x430bu &&
        u16(p+at+14u)==0x1f51u && (u16(p+at+16u)&0xff00u)==0xd300u &&
        u16(p+at+18u)==0xe500u && u16(p+at+20u)==0x430bu;
}
static bool exit_pair(const uint8_t *p,size_t bytes,uint32_t load,size_t at,uint32_t *cleanup) {
    /* Both complete SDK exits, before any cleanup or branch to boot RAM.
     * PC-relative loads are decoded and bound to their shared literal pool. */
    static const struct witness w[]={
        {2,0x6030},{4,0x600c},{6,0x8801},{8,0x8b0e},{12,0x6020},{14,0x600c},
        {16,0x8801},{18,0x8b02},{22,0x412b},{24,0x0009},{30,0x2012},
        {34,0x6312},{36,0x432b},{38,0xe403},{42,0x6212},{44,0x422b},{46,0xe401},
        {48,0x4f22},{52,0x430b},{54,0x0009},{58,0xe402},{60,0x6032},
        {62,0x402b},{64,0x4f26}
    };
    /* The halfword after the unconditional JMP and its delay slot is
     * linker fill. Admit only the two canonical fill values; no executable
     * instruction or literal witness is optional. */
    return bytes-at>=0x60u && (u16(p+at+66u)==0u || u16(p+at+66u)==UINT16_C(0xffff)) && WORDS(p,at,w) &&
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
static bool exit_pvr_pair(const uint8_t *p,size_t bytes,uint32_t load,size_t at,uint32_t *cleanup) {
    /* This SDK first writes its PVR register, then performs the same
     * three platform-vector exits. The cleanup entry is at +0x36, before
     * its first call. Every branch, delay slot and pointer transport below
     * is independently constrained; this is not an optional-instruction mask. */
    static const struct witness w[]={
        {6,0x2232},{8,0x6010},{10,0x600c},{12,0x8801},{14,0x8b0e},
        {18,0x6030},{20,0x600c},{22,0x8801},{24,0x8b02},{28,0x422b},{30,0x0009},
        {36,0x2022},{40,0x6122},{42,0x412b},{44,0xe403},{48,0x6032},
        {50,0x402b},{52,0xe401},{54,0x4f22},{58,0x430b},{60,0x0009},
        {64,0xe402},{66,0x6032},{68,0x402b},{70,0x4f26},{74,0x0000}
    };
    return bytes-at>=0x6cu && WORDS(p,at,w) &&
        value_at(p,bytes,load,at,1u,at+0x50u,UINT32_C(0x8c0080fc)) &&
        short_at(p,bytes,load,at+2u,3u,at+0x48u,UINT16_C(0x00ff)) &&
        value_at(p,bytes,load,at+4u,2u,at+0x4cu,UINT32_C(0xa05f811c)) &&
        value_at(p,bytes,load,at+16u,3u,at+0x54u,UINT32_C(0x8c0080fe)) &&
        value_at(p,bytes,load,at+26u,2u,at+0x58u,UINT32_C(0x8c008300)) &&
        value_at(p,bytes,load,at+32u,0u,at+0x60u,UINT32_C(0x8c000064)) &&
        value_at(p,bytes,load,at+34u,2u,at+0x5cu,UINT32_C(0x8c008000)) &&
        value_at(p,bytes,load,at+38u,2u,at+0x64u,UINT32_C(0x8c0000e0)) &&
        value_at(p,bytes,load,at+46u,3u,at+0x64u,UINT32_C(0x8c0000e0)) &&
        value_at(p,bytes,load,at+62u,3u,at+0x64u,UINT32_C(0x8c0000e0)) &&
        code_at(p,bytes,load,at+56u,3u,at+0x68u,cleanup);
}
static bool exec_loader(const uint8_t *p,size_t bytes,uint32_t load,size_t at,uint32_t exit,bool pvr,
    struct exec_shape *shape) {
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
        {78,0x2dd8},{80,0x8902},{84,0x430b},{86,0x0009}
    };
    static const struct witness tail[]={
        {88,0x52f1},
        {90,0x420b},{92,0x0009},{94,0xe001},{96,0x7f0c},{98,0x4f26},
        {100,0x6df6},{102,0x000b},{104,0x6ef6},{110,0xafc7},{112,0xe601},{114,0x0000}
    };
    static const struct witness pvr_tail[]={
        {92,0x2322},{94,0x51f1},{96,0x410b},{98,0x0009},{100,0xe001},
        {102,0x7f0c},{104,0x4f26},{106,0x6df6},{108,0x000b},{110,0x6ef6},
        {116,0xafc4},{118,0xe601},{122,0x0000}
    };
    size_t thunk=pvr?112u:106u,context_slot=pvr?0x7cu:0x74u;
    size_t thunk_context_slot=pvr?0xa4u:0x98u,boot_slot=pvr?0xa0u:0x94u;
    if(bytes-at<(pvr?0xa8u:0x9cu) || !WORDS(p,at,w) ||
       (pvr?!WORDS(p,at,pvr_tail):!WORDS(p,at,tail))) return false;
    uint32_t address,context,thunk_context,target;
    if(!literal(p,bytes,load,at+10u,4u,&address,&context) || address!=load+at+context_slot ||
       (context&3u) || !in_image(context,load,bytes,8u) ||
       !literal(p,bytes,load,at+thunk,4u,&address,&thunk_context) || address!=load+at+thunk_context_slot ||
       thunk_context!=context+4u ||
       !value_at(p,bytes,load,at+thunk+2u,5u,at+boot_slot,UINT32_C(0x8c008300))) return false;
    if(pvr && (!value_at(p,bytes,load,at+88u,3u,at+0x9cu,UINT32_C(0xa05f811c)) ||
       !short_at(p,bytes,load,at+90u,2u,at+0x78u,UINT16_C(0x00ff)))) return false;
    static const uint16_t calls[]={12u,20u,34u,42u,60u,72u,82u};
    for(unsigned i=0;i<sizeof(calls)/sizeof(calls[0]);i++) {
        if(!code_at(p,bytes,load,at+calls[i],3u,at+context_slot+4u+4u*i,&target)) return false;
        if(i==6u && physical(target)!=physical(exit)) return false;
    }
    *shape=(struct exec_shape){pvr?KUI_VMU_NATIVE_EXIT_PVR_CLEAR:KUI_VMU_NATIVE_EXIT_FAMILY,
        (uint32_t)thunk,(uint32_t)thunk+2u};
    return true;
}
static bool exec_compact(const uint8_t *p,size_t bytes,uint32_t load,size_t at,
    struct exec_shape *shape) {
    /* The compact SDK still loads the file, calls its cleanup routine and
     * invokes the caller's saved R5; it omits the older error/exit path.
     * Intercept its entry, before any of those calls, and its BIOS thunk's
     * sole encoded bootstrap consumer remains bound by consumers(). */
    static const struct witness w[]={
        {0,0x2fe6},{2,0x4f22},{4,0x7ff4},{6,0x1f42},{12,0x430b},{14,0x1f51},
        {18,0xe500},{20,0x430b},{22,0x54f2},{24,0x6e03},{26,0x2ee8},{28,0x890b},
        {32,0x65f3},{34,0x430b},{36,0x64e3},{40,0x65f2},{42,0x56f1},{44,0x430b},{46,0x64e3},
        {50,0x420b},{52,0x64e3},{56,0x430b},{58,0x0009},{60,0x52f1},{62,0x420b},{64,0x0009},
        {66,0xe001},{68,0x7f0c},{70,0x4f26},{72,0x000b},{74,0x6ef6},{80,0xafd6},{82,0xe601}
    };
    if(bytes-at<0x78u || !WORDS(p,at,w)) return false;
    uint32_t address,context,thunk_context,target;
    if(!literal(p,bytes,load,at+10u,4u,&address,&context) || address!=load+at+0x54u ||
       (context&3u) || !in_image(context,load,bytes,8u) ||
       !literal(p,bytes,load,at+76u,4u,&address,&thunk_context) || address!=load+at+0x74u ||
       thunk_context!=context+4u ||
       !value_at(p,bytes,load,at+78u,5u,at+0x70u,UINT32_C(0x8c008300))) return false;
    static const uint16_t calls[]={8u,16u,30u,38u,48u,54u};
    for(unsigned i=0;i<sizeof(calls)/sizeof(calls[0]);i++)
        if(!code_at(p,bytes,load,at+calls[i],i==4u?2u:3u,at+0x58u+4u*i,&target)) return false;
    *shape=(struct exec_shape){KUI_VMU_NATIVE_EXIT_PVR_COMPACT,76u,78u};return true;
}
static bool consumers(const uint8_t *p,size_t bytes,uint32_t load,size_t pair,size_t exec,
    const struct exit_shape *pair_shape,const struct exec_shape *exec_shape) {
    bool pair_only=exec_shape->family==KUI_VMU_NATIVE_EXIT_PAIR_ONLY;
    unsigned vectors=0u,boots=0u;
    for(size_t at=0;at+2u<=bytes;at+=2u) {
        uint16_t op=u16(p+at);uint32_t address,value;
        if((op&UINT16_C(0xf000))!=UINT16_C(0xd000) ||
           !literal(p,bytes,load,at,(op>>8)&15u,&address,&value) || !direct(value)) continue;
        if(physical(value)==UINT32_C(0x0c0000e0)) {
            if(at!=pair+pair_shape->vector_loads[0] && at!=pair+pair_shape->vector_loads[1] &&
               at!=pair+pair_shape->vector_loads[2]) return false;
            ++vectors;
        } else if(physical(value)==UINT32_C(0x0c008300)) {
            if(at!=pair+pair_shape->boot && (pair_only || at!=exec+exec_shape->boot)) return false;
            ++boots;
        }
    }
    return vectors==3u && boots==(pair_only?1u:2u);
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
bool kui_vmu_native_exit_discover_guarded(const void *image,size_t bytes,uint32_t load,
    const struct kui_vmu_native_patch_window *additional,size_t extra_count,
    struct kui_vmu_native_exit *out) {
    if(!out) return false;
    uintptr_t begin=(uintptr_t)image,dest=(uintptr_t)out;
    if(bytes>UINTPTR_MAX-begin || sizeof(*out)>UINTPTR_MAX-dest ||
       (dest<begin+bytes && begin<dest+sizeof(*out)) ||
       aliases_overlap(begin,bytes,dest,sizeof(*out))) return false;
    uintptr_t extra=(uintptr_t)additional;
    if(additional && extra_count) {
        if(extra_count>(UINTPTR_MAX-extra)/sizeof(*additional)) return false;
        size_t extra_bytes=extra_count*sizeof(*additional);
        if((dest<extra+extra_bytes && extra<dest+sizeof(*out)) ||
           aliases_overlap(extra,extra_bytes,dest,sizeof(*out))) return false;
    }
    memset(out,0,sizeof(*out));
    if(!image || bytes<0x9cu || bytes>UINT32_C(0xc00000) || (load&3u) ||
       physical(load)<UINT32_C(0x0c010000) || !direct(load) ||
       bytes>UINT32_C(0x0d000000)-physical(load) || extra_count>5u ||
       (extra_count && !additional)) return false;
    const uint8_t *p=image;unsigned count=0u;size_t pair=0u,exec=0u;uint32_t cleanup=0u;
    bool pvr=false;struct exit_shape pair_shape={0};struct exec_shape exec_shape={0};
    for(size_t at=0;at+0x60u<=bytes;at+=4u) {
        uint16_t op=u16(p+at),next=u16(p+at+2u);uint32_t target;
        if((op&UINT16_C(0xff00))==UINT16_C(0xd300) && next==UINT16_C(0x6030) &&
           exit_pair(p,bytes,load,at,&target)) {
            pair=at;cleanup=target;pvr=false;
            pair_shape=(struct exit_shape){48u,0x58u,20u,{32u,40u,56u}};
            if(++count>1u) return false;
        } else if((op&UINT16_C(0xff00))==UINT16_C(0xd100) &&
           (next&UINT16_C(0xff00))==UINT16_C(0x9300) && exit_pvr_pair(p,bytes,load,at,&target)) {
            pair=at;cleanup=target;pvr=true;
            pair_shape=(struct exit_shape){54u,0x64u,26u,{38u,46u,62u}};
            if(++count>1u) return false;
        }
    }
    if(count!=1u) return false;
    count=0u;bool loader_prologue=false;
    for(size_t at=0;at+24u<=bytes;at+=4u) {
        if(u16(p+at)!=UINT16_C(0x2fe6)) continue;
        if(exec_prologue(p,bytes,at)) loader_prologue=true;
        if(bytes-at<0x78u) continue;
        struct exec_shape shape={0};bool matched=false;
        if(u16(p+at+2u)==UINT16_C(0x2fd6) && u16(p+at+4u)==UINT16_C(0x4f22) &&
           u16(p+at+6u)==UINT16_C(0x7ff4))
            matched=exec_loader(p,bytes,load,at,load+(uint32_t)pair,pvr,&shape);
        else if(pvr && u16(p+at+2u)==UINT16_C(0x4f22) && u16(p+at+4u)==UINT16_C(0x7ff4))
            matched=exec_compact(p,bytes,load,at,&shape);
        if(matched) {exec=at;exec_shape=shape;if(++count>1u) return false;}
    }
    uint32_t entries=3u;
    if(!count && !pvr && !loader_prologue) {
        exec_shape=(struct exec_shape){KUI_VMU_NATIVE_EXIT_PAIR_ONLY,0u,0u};entries=2u;
    } else if(count!=1u) return false;
    if(!consumers(p,bytes,load,pair,exec,&pair_shape,&exec_shape)) return false;
    uint32_t terminal_table=0u;
    if(exec_shape.family==KUI_VMU_NATIVE_EXIT_PAIR_ONLY) {
        uint32_t common;
        int capability=terminal_dispatch(p,bytes,load,load+(uint32_t)pair,
            load+(uint32_t)pair+pair_shape.second,&common,&terminal_table);
        if(capability<0) return false;
        if(capability) {
            exec_shape.family=KUI_VMU_NATIVE_EXIT_TERMINAL_DISPATCH;
            exec=(size_t)(physical(common)-physical(load));entries=3u;
        }
    }
    struct kui_vmu_native_patch_window windows[8]={
        {load+(uint32_t)pair,12u},{load+(uint32_t)pair+pair_shape.second,12u},{load+(uint32_t)exec,12u}};
    for(size_t i=0;i<extra_count;i++) windows[entries+i]=additional[i];
    if(!kui_vmu_native_patch_no_interiors(image,bytes,load,windows,entries+extra_count)) return false;
    *out=(struct kui_vmu_native_exit){.version=KUI_VMU_NATIVE_EXIT_VERSION,.bytes=sizeof(*out),
        .family=exec_shape.family,.load=load,.image_bytes=(uint32_t)bytes,.count=entries,
        .entries={windows[0].begin,windows[1].begin,entries==3u?windows[2].begin:0u},
        .bios_vector_literal=load+(uint32_t)pair+pair_shape.vector,
        .exec_thunk=terminal_table?0u:
            entries==3u?load+(uint32_t)exec+exec_shape.thunk:0u,.cleanup=cleanup};
    if(terminal_table) out->terminal_dispatch.table=terminal_table;
    return true;
}
bool kui_vmu_native_exit_discover(const void *image,size_t bytes,uint32_t load,
    struct kui_vmu_native_exit *out) {
    return kui_vmu_native_exit_discover_guarded(image,bytes,load,NULL,0u,out);
}
