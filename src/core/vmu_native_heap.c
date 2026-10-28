/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_heap.h"
#include <string.h>

struct heap_word {uint16_t offset,value;};
/* Sparse authored contracts for the reviewed dispatch-table tail32 variant.
 * The initializer's PC-relative pointer loads are decoded independently. */
static const struct heap_word heap_words[]={
    {0,0x2fe6},{2,0x6e53},{4,0x741f},{0xa,0xe5e0},{0xc,0x2459},
    {0x12,0x2e59},{0x16,0x2342},{0x18,0x2242},{0x1e,0x21e2},
    {0x24,0x2242},{0x26,0x2432},{0x30,0x4e09},{0x32,0x4e09},
    {0x36,0x4e01},{0x3a,0x1de1},{0x3e,0x7420},
    {0x48,0x2448},{0x4a,0x8f03},{0x4c,0x6652},{0x50,0xa001},
    {0x52,0x2522},{0x54,0x2542},{0x56,0x000b},
    {0xe2,0x2fe6},{0xe4,0x7ffc},{0xe6,0x0002},
    {0xfe,0x751f},{0x104,0x4509},{0x106,0x4509},{0x10c,0x4501},
    {0x110,0x7501},{0x114,0x3252},
    {0x144,0x3158},{0x148,0x4308},{0x14a,0x4308},{0x14c,0x1411},
    {0x14e,0x4300},{0x150,0x343c},{0x152,0x1451},{0x16a,0x7020},
    {0x1b0,0x0002},{0x1b4,0x4009},{0x1b6,0x4009},{0x1b8,0xc90f},
    {0x1c0,0xcbf0},{0x1c2,0x400e},{0x1c4,0x2448},{0x1cc,0x75e0}
};
#if KUI_NATIVE_STAGE_FAST_SCAN
#include "kui/native_scan_bytes.h"
#define h16 kui_native_scan_le16
#define h32 kui_native_scan_le32
#else
static uint16_t h16(const uint8_t *p) {return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t h32(const uint8_t *p) {return (uint32_t)h16(p)|(uint32_t)h16(p+2)<<16;}
#endif
static uint32_t phys(uint32_t a) {return a&0x1fffffffu;}
static bool span(uint32_t a,uint32_t n) {
    uint32_t area=a&0xe0000000u,p=phys(a);
    return (area==0u || area==0x80000000u || area==0xa0000000u) &&
        p>=0x0c000000u && p<0x0d000000u && n<=0x0d000000u-p;
}
static bool overlaps(uintptr_t a,size_t an,uintptr_t b,size_t bn) {
    if(an>UINTPTR_MAX-a || bn>UINTPTR_MAX-b) return true;
    if(a<b+bn && b<a+an) return true;
#if UINTPTR_MAX > UINT32_MAX
    if(a>UINT32_MAX || b>UINT32_MAX) return false;
#endif
    return an<=UINT32_MAX && bn<=UINT32_MAX && span((uint32_t)a,(uint32_t)an) &&
        span((uint32_t)b,(uint32_t)bn) && phys((uint32_t)a)<phys((uint32_t)b)+bn &&
        phys((uint32_t)b)<phys((uint32_t)a)+an;
}
static bool index(uint32_t a,uint32_t base,size_t n,size_t need,size_t *at) {
    uint32_t p=phys(a),b=phys(base);
    if((a&3u) || !span(a,4u) || p<b || p-b>n || need>n-(p-b)) return false;
    *at=p-b;return true;
}
static bool literal(const uint8_t *p,size_t n,uint32_t base,size_t at,
    unsigned reg,uint32_t *value) {
    if(at>n || n-at<2u) return false;
    uint16_t op=h16(p+at);
    if((op&0xff00u)!=(0xd000u|(reg<<8))) return false;
    uint32_t address=((base+(uint32_t)at+4u)&~3u)+4u*(op&255u);
    size_t slot;if(!index(address,base,n,4u,&slot)) return false;
    *value=h32(p+slot);return true;
}
static bool metadata(const struct kui_vmu_native_heap *h) {
    if(!h || h->version!=KUI_VMU_NATIVE_HEAP_VERSION || h->bytes!=sizeof(*h) ||
       h->family!=KUI_VMU_NATIVE_HEAP_TAIL32 || h->layout!=KUI_VMU_NATIVE_HEAP_DISPATCH2 ||
       h->image_bytes<2u || h->image_bytes>0xc00000u || (h->load&3u) ||
       !span(h->load,h->image_bytes) || !span(h->sentinel,0x30u) ||
       !span(h->dispatch_table,8u) || ((h->sentinel|h->dispatch_table)&3u) ||
       (h->initializer&1u) || h->initializer<h->load ||
       h->initializer-h->load>h->image_bytes ||
       h->image_bytes-(h->initializer-h->load)<0x1d0u) return false;
    size_t at;
    if(!index(h->dispatch_table,h->load,h->image_bytes,8u,&at) ||
       (h->initializer_slot &&
        (!index(h->initializer_slot,h->load,h->image_bytes,4u,&at) ||
         !span(h->initializer_target,2u) || phys(h->initializer_target)!=phys(h->initializer))) ||
       (!h->initializer_slot && h->initializer_target)) return false;
    return h->cursor_cell==h->sentinel+0x20u && h->begin_cell==h->sentinel+0x24u &&
        h->bytes_cell==h->sentinel+0x28u && h->begin_copy_cell==h->sentinel+0x2cu &&
        h->dispatch_cell==h->sentinel-4u &&
        span(h->dispatch_cell,4u) && !(h->allocate&1u) && !(h->release&1u) &&
        phys(h->allocate)==phys(h->initializer)+0xe2u &&
        phys(h->release)==phys(h->initializer)+0x1b0u &&
        span(h->initializer,0x1d0u) && span(h->allocate,2u) && span(h->release,2u);
}
static bool candidate(const uint8_t *p,size_t n,uint32_t base,size_t at,
    struct kui_vmu_native_heap *h) {
    if(n-at<0x1d0u) return false;
    for(unsigned i=0;i<sizeof(heap_words)/sizeof(heap_words[0]);i++)
        if(h16(p+at+heap_words[i].offset)!=heap_words[i].value) return false;
    uint32_t cursor2,begin2;
    if(!literal(p,n,base,at+6u,2u,&h->begin_copy_cell) ||
       !literal(p,n,base,at+8u,3u,&h->begin_cell) ||
       !literal(p,n,base,at+0xeu,1u,&h->bytes_cell) ||
       !literal(p,n,base,at+0x1au,4u,&h->sentinel) ||
       !literal(p,n,base,at+0x1cu,2u,&h->cursor_cell) ||
       !literal(p,n,base,at+0x2eu,2u,&begin2) || begin2!=h->begin_cell ||
       !literal(p,n,base,at+0x46u,5u,&h->dispatch_cell) ||
       !literal(p,n,base,at+0x4eu,2u,&h->dispatch_table) ||
       !literal(p,n,base,at+0xfau,14u,&cursor2) || cursor2!=h->cursor_cell) return false;
    size_t table;if(!index(h->dispatch_table,base,n,8u,&table)) return false;
    h->allocate=h32(p+table);h->release=h32(p+table+4u);
    h->version=KUI_VMU_NATIVE_HEAP_VERSION;h->bytes=sizeof(*h);
    h->family=KUI_VMU_NATIVE_HEAP_TAIL32;h->layout=KUI_VMU_NATIVE_HEAP_DISPATCH2;
    h->image_bytes=(uint32_t)n;h->load=base;h->initializer=base+(uint32_t)at;
    return metadata(h);
}
static int call_discover(const void *,size_t,const struct kui_vmu_native_heap *,
    struct kui_vmu_native_heap_call *);
enum kui_vmu_native_heap_result kui_vmu_native_heap_discover(
    const void *image,size_t n,uint32_t base,struct kui_vmu_native_heap *out) {
    if(!out || overlaps((uintptr_t)image,n,(uintptr_t)out,sizeof(*out)))
        return KUI_VMU_NATIVE_HEAP_INVALID;
    memset(out,0,sizeof(*out));
    if(!image || n<2u || n>0xc00000u || n>UINTPTR_MAX-(uintptr_t)image ||
       (base&3u) || !span(base,(uint32_t)n)) return KUI_VMU_NATIVE_HEAP_INVALID;
    const uint8_t *p=image;struct kui_vmu_native_heap found={0};unsigned count=0;
    for(size_t at=0;at<n && n-at>=0x1d0u;at+=2u) {
        if(h16(p+at)!=0x2fe6u || h16(p+at+2u)!=0x6e53u) continue;
        struct kui_vmu_native_heap h={0};
        if(!candidate(p,n,base,at,&h)) continue;
        if(++count>1u) return KUI_VMU_NATIVE_HEAP_AMBIGUOUS;
        found=h;
    }
    if(!count) return KUI_VMU_NATIVE_HEAP_NONE;
    /* An SDK export table can retain the initializer address in addition to
     * its actual startup literal. Raw pointer multiplicity alone must not
     * choose a slot: require one and only one complete immutable argument /
     * call proof among the bounded candidates. Reinitialization by any entry
     * remains intercepted independently once the worker owns its lease. */
    enum {REFERENCE_LIMIT=8};
    uint32_t slots[REFERENCE_LIMIT],targets[REFERENCE_LIMIT];unsigned references=0;
    for(size_t at=0;at<n && n-at>=4u;at+=4u) {
        uint32_t value=h32(p+at);
        if(span(value,2u) && phys(value)==phys(found.initializer)) {
            if(references==REFERENCE_LIMIT) return KUI_VMU_NATIVE_HEAP_AMBIGUOUS;
            slots[references]=base+(uint32_t)at;targets[references++]=value;
        }
    }
    if(references==1u) {
        found.initializer_slot=slots[0];found.initializer_target=targets[0];
    } else if(references>1u) {
        unsigned calls=0u;struct kui_vmu_native_heap chosen={0};
        for(unsigned i=0;i<references;i++) {
            struct kui_vmu_native_heap trial=found;struct kui_vmu_native_heap_call call;
            trial.initializer_slot=slots[i];trial.initializer_target=targets[i];
            int result=call_discover(image,n,&trial,&call);
            if(result<0) return KUI_VMU_NATIVE_HEAP_AMBIGUOUS;
            if(!result) continue;
            if(++calls>1u) return KUI_VMU_NATIVE_HEAP_AMBIGUOUS;
            chosen=trial;
        }
        if(calls!=1u) return KUI_VMU_NATIVE_HEAP_AMBIGUOUS;
        found=chosen;
    }
    *out=found;return KUI_VMU_NATIVE_HEAP_FOUND;
}
/* Evaluate only a bounded immutable integer argument expression. All
 * memory reads must name immutable words inside the supplied executable;
 * unknown instructions, branch outcomes and caller state are never inferred. The
 * one admitted conditional form is a forward TST guard with known operands;
 * its destination cannot skip the target load or call. A failure spin is
 * never followed, and taken backward or unknown control flow refuses. The
 * eventual live initializer hook independently checks both arguments and PR.
 * This admits equivalent SDK startup expressions (BSS-tail or fixed arena),
 * rather than a title-specific instruction/register arrangement. */
static bool call_expression(const uint8_t *p,size_t n,const struct kui_vmu_native_heap *h,
    size_t begin,size_t target_load,size_t call,unsigned target,uint32_t *first,uint32_t *bytes) {
    uint32_t value[16]={0},known=0;bool condition=false,condition_known=false,target_loaded=false;
    for(size_t at=begin;at<=call+2u;at+=2u) {
        if(at==call) {
            if(!target_loaded || !(known&(1u<<target)) || value[target]!=h->initializer_target) return false;
            continue;
        }
        uint16_t op=h16(p+at);unsigned r=(op>>8)&15u,m=(op>>4)&15u;
        uint32_t bit=1u<<r,other=1u<<m;
        if(op==0x0009u) continue;
        /* Argument evaluation cannot authorize changing the inherited stack.
         * Only the explicit displacement store below may name R15. */
        bool stack_store=(op&0xff00u)==0x1f00u || (op&0xff0fu)==0x2f02u;
        if(r==15u && !stack_store) return false;
        /* The installer replaces this literal with its low hook. Preserve
         * the transport itself, not just the original computed target: a
         * later mask/add could otherwise transform the replacement pointer. */
        if(at>target_load && at<call && r==target && (op&0xff00u)!=0x1f00u) return false;
        if((op&0xf000u)==0xd000u) {
            uint32_t pool=((h->load+(uint32_t)at+4u)&~3u)+4u*(op&255u);
            /* The selected word changes to the hook at installation. No
             * earlier/other read may feed arguments or a conditional from
             * its original value. Compare physical aliases as well. */
            if(at!=target_load && phys(pool)==phys(h->initializer_slot)) return false;
            if(!literal(p,n,h->load,at,r,&value[r])) return false;
            known|=bit;
            if(at==target_load) {
                if(r!=target || value[r]!=h->initializer_target) return false;
                target_loaded=true;
            }
        } else if((op&0xf000u)==0xe000u) {
            value[r]=(uint32_t)(int32_t)(int8_t)op;known|=bit;
        } else if((op&0xf000u)==0x7000u) {
            if(target_loaded && r==target) return false;
            if(!(known&bit)) return false;
            value[r]+=(uint32_t)(int32_t)(int8_t)op;
        } else if((op&0xf00fu)==0x6002u) {
            size_t slot;
            if((target_loaded && m==target) || !(known&other) ||
               phys(value[m])==phys(h->initializer_slot) ||
               !index(value[m],h->load,n,4u,&slot)) return false;
            value[r]=h32(p+slot);known|=bit;
        } else if((op&0xf00fu)==0x6003u) {
            if(target_loaded && m==target) return false;
            if(!(known&other)) return false;
            value[r]=value[m];known|=bit;
        } else if(stack_store) {
            /* Reviewed startup spills a computed boundary to its own stack.
             * MOV.L Rm,@R15 is exactly the zero-displacement encoding of
             * MOV.L Rm,@(0,R15); neither encoding changes R15. The same
             * known-source and no-replacement-target rules apply to both.
             * No stack loads are accepted, so this cannot supply knowledge
             * to the argument expression or follow an unproved pointer. */
            if((target_loaded && m==target) || !(known&other)) return false;
        } else if((op&0xf00fu)==0x2008u) {
            if(target_loaded && (r==target || m==target)) return false;
            if((known&(bit|other))!=(bit|other)) return false;
            condition=(value[r]&value[m])==0u;condition_known=true;
        } else if((op&0xff00u)==0x8900u || (op&0xff00u)==0x8b00u) {
            if(!condition_known || at>=target_load) return false;
            if(condition==((op&0xff00u)==0x8900u)) {
                int32_t delta=4+2*(int32_t)(int8_t)op;
                if(delta<=0 || (size_t)delta>target_load-at) return false;
                at+=(size_t)delta-2u;
            }
        } else if((op&0xf00fu)==0x2009u || (op&0xf00fu)==0x200bu ||
                  (op&0xf00fu)==0x3008u || (op&0xf00fu)==0x300cu) {
            if(target_loaded && (r==target || m==target)) return false;
            if((known&(bit|other))!=(bit|other)) return false;
            if((op&0xf00fu)==0x2009u) value[r]&=value[m];
            else if((op&0xf00fu)==0x200bu) value[r]|=value[m];
            else if((op&0xf00fu)==0x3008u) value[r]-=value[m];
            else value[r]+=value[m];
        } else return false;
    }
    if((known&0x30u)!=0x30u || !span(value[4],32u) || value[5]<64u ||
       ((value[4]|value[5])&31u) || !span(value[4],value[5])) return false;
    *first=value[4];*bytes=value[5];return true;
}
static int call_discover(const void *image,size_t n,
    const struct kui_vmu_native_heap *h,struct kui_vmu_native_heap_call *out) {
    if(!image || !out || !metadata(h) || !h->initializer_slot || n!=h->image_bytes ||
       overlaps((uintptr_t)image,n,(uintptr_t)out,sizeof(*out))) return false;
    const uint8_t *p=image;struct kui_vmu_native_heap_call chosen={0};unsigned found=0;
    for(size_t at=0;at<n && n-at>=6u;at+=2u) {
        uint16_t op=h16(p+at);uint32_t target;
        unsigned reg=(op>>8)&15u;
        if((op&0xf000u)!=0xd000u || (reg!=0u && reg!=2u && reg!=3u) ||
           phys(((h->load+(uint32_t)at+4u)&~3u)+4u*(op&255u))!=phys(h->initializer_slot) ||
           !literal(p,n,h->load,at,reg,&target) || target!=h->initializer_target) continue;
        for(size_t call=at+2u;call<=at+16u && call<n && n-call>=4u;call+=2u) {
            if(h16(p+call)!=(0x400bu|(reg<<8))) continue;
            bool resolved=false;uint32_t first=0,bytes=0;
            size_t earliest=call<48u?0u:call-48u;
            for(size_t start=earliest;start<=at;start+=2u) {
                uint32_t got_first,got_bytes;
                if(!call_expression(p,n,h,start,at,call,reg,&got_first,&got_bytes)) continue;
                if(resolved && (first!=got_first || bytes!=got_bytes)) return -1;
                first=got_first;bytes=got_bytes;resolved=true;
            }
            if(!resolved) continue;
            if(++found>1u) return -1;
            chosen=(struct kui_vmu_native_heap_call){h->load+(uint32_t)at,
                h->load+(uint32_t)call,h->load+(uint32_t)call+4u,first,bytes};
        }
    }
    if(!found) return false;
    /* Patching a shared pool word could change an earlier conditional even
     * when this bounded expression starts after that conditional. Require
     * the selected literal to have exactly its chosen encoded PC-relative
     * consumer across the entire image, and no immutable address-of-slot
     * word that could supply an indirect consumer. This is deliberately
     * conservative: coincidental data matches also prevent admission. It is
     * not a whole-program proof of synthesized indirect memory accesses. */
    unsigned consumers=0u;
    for(size_t at=0;at<n && n-at>=2u;at+=2u) {
        if(!(at&3u) && n-at>=4u && span(h32(p+at),4u) &&
           phys(h32(p+at))==phys(h->initializer_slot)) return false;
        uint16_t op=h16(p+at);
        if((op&0xf000u)!=0xd000u ||
           phys(((h->load+(uint32_t)at+4u)&~3u)+4u*(op&255u))!=phys(h->initializer_slot)) continue;
        if(h->load+(uint32_t)at!=chosen.load || ++consumers>1u) return false;
    }
    if(consumers!=1u) return false;
    *out=chosen;return true;
}
bool kui_vmu_native_heap_call_discover(const void *image,size_t n,
    const struct kui_vmu_native_heap *h,struct kui_vmu_native_heap_call *out) {
    return call_discover(image,n,h,out)==1;
}
struct fixed_plan {
    uint32_t image_bytes;uint8_t sha256[32];
    uint32_t load,call,slot,target,begin,bytes;
};
static const struct fixed_plan fixed_plans[]={
    /* Crazy Taxi 2, US MK-51136 V1.004: the game's init calls the SDK
     * hardware init once, which installs the interrupt vector the SCI route
     * needs; an arena inside the 1 MiB both console RAM maps left unused
     * (docs/evidence/native-ct2-ram-map-jgr-console-2026-10-28.json). */
    {1452148u,{0x80,0x57,0xa8,0xe3,0x76,0x36,0xad,0x78,0xb1,0xfc,0x70,0xee,0x59,0x0b,0xd3,0x14,
               0x68,0x65,0x46,0xb1,0x3f,0xfe,0x10,0x06,0x23,0xd8,0x6b,0x57,0x2a,0xec,0x53,0xc1},
     UINT32_C(0x8c01aa8a),UINT32_C(0x8c01aa8c),UINT32_C(0x8c01aaf8),UINT32_C(0x0c14b128),
     UINT32_C(0x8cf00000),UINT32_C(0x00080000)},
#ifdef KUI_VMU_NATIVE_HEAP_TEST_FIXED_PLAN
    KUI_VMU_NATIVE_HEAP_TEST_FIXED_PLAN,
#endif
};
/* An even code address inside the image with need bytes from it. */
static bool code(uint32_t a,uint32_t base,size_t n,size_t need,size_t *at) {
    uint32_t p=phys(a),b=phys(base);
    if((a&1u) || !span(a,2u) || p<b || p-b>n || need>n-(p-b)) return false;
    *at=p-b;return true;
}
bool kui_vmu_native_heap_fixed_plan(const void *image,size_t n,const uint8_t sha256[32],
    const struct kui_vmu_native_heap *h,struct kui_vmu_native_heap_call *out,
    struct kui_vmu_native_heap_fixed_hook *hook) {
    if(!image || !sha256 || !out || !metadata(h) || h->initializer_slot || h->initializer_target ||
       n!=h->image_bytes || overlaps((uintptr_t)image,n,(uintptr_t)out,sizeof(*out)) ||
       (hook && (overlaps((uintptr_t)image,n,(uintptr_t)hook,sizeof(*hook)) ||
                 overlaps((uintptr_t)out,sizeof(*out),(uintptr_t)hook,sizeof(*hook))))) return false;
    const struct fixed_plan *plan=NULL;
    for(unsigned i=0;i<sizeof(fixed_plans)/sizeof(fixed_plans[0]);i++)
        if(fixed_plans[i].image_bytes==n && !memcmp(fixed_plans[i].sha256,sha256,32u)) plan=&fixed_plans[i];
    if(!plan) return false;
    const uint8_t *p=image;size_t load,call,inside;
    /* The borrowed call: MOV.L @(disp,PC),Rn; JSR @Rn; NOP, reading exactly
     * the planned literal, which still names the planned target, itself
     * code in the image. */
    if(!code(plan->load,h->load,n,6u,&load) || !code(plan->call,h->load,n,4u,&call) ||
       call!=load+2u || !index(plan->slot,h->load,n,4u,&inside) ||
       !code(plan->target,h->load,n,2u,&inside)) return false;
    uint16_t op=h16(p+load);unsigned reg=(op>>8)&15u;uint32_t value;
    if((op&0xf000u)!=0xd000u || phys(((h->load+(uint32_t)load+4u)&~3u)+4u*(op&255u))!=phys(plan->slot) ||
       !literal(p,n,h->load,load,reg,&value) || value!=plan->target ||
       h16(p+call)!=(0x400bu|(reg<<8)) || h16(p+call+2u)!=0x0009u) return false;
    /* As for an initializer slot: one encoded consumer across the image and
     * no immutable word holding the slot's address. */
    unsigned consumers=0u;
    for(size_t at=0;at<n && n-at>=2u;at+=2u) {
        if(!(at&3u) && n-at>=4u && span(h32(p+at),4u) && phys(h32(p+at))==phys(plan->slot)) return false;
        uint16_t word=h16(p+at);
        if((word&0xf000u)!=0xd000u ||
           phys(((h->load+(uint32_t)at+4u)&~3u)+4u*(word&255u))!=phys(plan->slot)) continue;
        if(at!=load || ++consumers>1u) return false;
    }
    /* The arena lies in RAM outside the image and the allocator's control
     * cells and dispatch table; stage placement is checked separately. */
    uint32_t begin=phys(plan->begin),image_begin=phys(h->load),control=phys(h->sentinel)-4u;
    if(consumers!=1u || ((plan->begin|plan->bytes)&31u) || plan->bytes<128u ||
       !span(plan->begin,plan->bytes) ||
       (begin<image_begin+(uint32_t)n && image_begin<begin+plan->bytes) ||
       (begin<control+0x34u && control<begin+plan->bytes) ||
       (begin<phys(h->dispatch_table)+8u && phys(h->dispatch_table)<begin+plan->bytes)) return false;
    *out=(struct kui_vmu_native_heap_call){plan->load,plan->call,plan->call+4u,plan->begin,plan->bytes};
    if(hook) *hook=(struct kui_vmu_native_heap_fixed_hook){plan->slot,plan->target};
    return true;
}
static bool read_is(kui_vmu_native_heap_read read,void *ctx,uint32_t at,uint32_t expected) {
    uint32_t value;return read && !(at&3u) && span(at,4u) && read(ctx,at,&value) && value==expected;
}
static bool tail_geometry(const struct kui_vmu_native_heap_lease *l) {
    return l->payload==l->begin+l->heap_bytes-l->lease_bytes && l->header==l->payload-32u;
}
static bool bottom_geometry(const struct kui_vmu_native_heap_lease *l) {
    return l->header==l->begin && l->payload==l->begin+32u;
}
static bool identity(const struct kui_vmu_native_heap_lease *l,
    kui_vmu_native_heap_read read,void *ctx,uint32_t initializations) {
    const struct kui_vmu_native_heap *h=&l->owner;
    return metadata(h) && l->version==KUI_VMU_NATIVE_HEAP_VERSION && l->bytes==sizeof(*l) &&
        initializations==1u && l->initializations==1u && !(l->begin&31u) &&
        !(l->heap_bytes&31u) && span(l->begin,l->heap_bytes) &&
        l->lease_bytes && !(l->lease_bytes&31u) && l->heap_bytes>=64u &&
        l->lease_bytes<=l->heap_bytes-64u && l->units==l->lease_bytes/32u+1u &&
        l->remaining==l->heap_bytes/32u-l->units &&
        (tail_geometry(l) || bottom_geometry(l)) &&
        /* Allocator control must not live inside its own managed arena. */
        !(phys(h->sentinel)<phys(l->begin)+l->heap_bytes &&
          phys(l->begin)<phys(h->sentinel)+0x30u) &&
        !(phys(h->dispatch_cell)>=phys(l->begin) &&
          phys(h->dispatch_cell)<phys(l->begin)+l->heap_bytes) &&
        !(phys(h->dispatch_table)<phys(l->begin)+l->heap_bytes &&
          phys(l->begin)<phys(h->dispatch_table)+8u) &&
        !(phys(h->load)<phys(l->begin)+l->heap_bytes &&
          phys(l->begin)<phys(h->load)+h->image_bytes) &&
        read_is(read,ctx,h->begin_cell,l->begin) && read_is(read,ctx,h->begin_copy_cell,l->begin) &&
        read_is(read,ctx,h->bytes_cell,l->heap_bytes) &&
        read_is(read,ctx,h->dispatch_cell,h->dispatch_table) &&
        read_is(read,ctx,h->dispatch_table,h->allocate) &&
        read_is(read,ctx,h->dispatch_table+4u,h->release) &&
        read_is(read,ctx,h->sentinel+4u,0u);
}
static bool fresh(const struct kui_vmu_native_heap_lease *l,
    kui_vmu_native_heap_read read,void *ctx,uint32_t initializations) {
    return identity(l,read,ctx,initializations) &&
        read_is(read,ctx,l->owner.sentinel,l->begin) &&
        read_is(read,ctx,l->owner.cursor_cell,l->owner.sentinel) &&
        read_is(read,ctx,l->begin,l->owner.sentinel) &&
        read_is(read,ctx,l->begin+4u,l->heap_bytes/32u);
}
bool kui_vmu_native_heap_fresh(const struct kui_vmu_native_heap *h,
    kui_vmu_native_heap_read read,void *ctx,uint32_t begin,uint32_t bytes,
    uint32_t initializations,uint32_t lease_bytes,struct kui_vmu_native_heap_lease *out) {
    if(!out) return false;
    struct kui_vmu_native_heap_lease l={0};
    if(h) l.owner=*h;
    l.version=KUI_VMU_NATIVE_HEAP_VERSION;l.bytes=sizeof(l);l.phase=1u;
    l.initializations=initializations;l.begin=begin;l.heap_bytes=bytes;l.lease_bytes=lease_bytes;
    if(bytes>=64u && lease_bytes && !(lease_bytes&31u) && lease_bytes<=bytes-64u && span(begin,bytes)) {
        l.units=lease_bytes/32u+1u;l.remaining=bytes/32u-l.units;
        l.payload=begin+bytes-lease_bytes;l.header=l.payload-32u;
    }
    if(!fresh(&l,read,ctx,initializations)) {memset(out,0,sizeof(*out));return false;}
    *out=l;return true;
}
bool kui_vmu_native_heap_preallocate(const struct kui_vmu_native_heap_lease *l,
    kui_vmu_native_heap_read read,void *ctx,uint32_t initializations) {
    return l && l->phase==1u && fresh(l,read,ctx,initializations);
}
bool kui_vmu_native_heap_postallocate(struct kui_vmu_native_heap_lease *l,
    kui_vmu_native_heap_read read,void *ctx,uint32_t initializations,uint32_t actual) {
    if(!l || l->phase!=1u || !identity(l,read,ctx,initializations) || !tail_geometry(l) || actual!=l->payload ||
       !read_is(read,ctx,l->owner.sentinel,l->begin) ||
       !read_is(read,ctx,l->owner.cursor_cell,l->owner.sentinel) ||
       !read_is(read,ctx,l->begin,l->owner.sentinel) ||
       !read_is(read,ctx,l->begin+4u,l->remaining) || !read_is(read,ctx,l->header+4u,l->units)) {
        if(l) l->phase=0u;
        return false;
    }
    l->phase=2u;return true;
}
bool kui_vmu_native_heap_bottom_fresh(const struct kui_vmu_native_heap *h,
    kui_vmu_native_heap_read read,void *ctx,uint32_t begin,uint32_t bytes,
    uint32_t initializations,uint32_t lease_bytes,struct kui_vmu_native_heap_lease *out) {
    if(!out) return false;
    struct kui_vmu_native_heap_lease l;
    if(!kui_vmu_native_heap_fresh(h,read,ctx,begin,bytes,initializations,lease_bytes,&l)) {
        memset(out,0,sizeof(*out));return false;
    }
    l.header=begin;l.payload=begin+32u;
    if(!fresh(&l,read,ctx,initializations)) {memset(out,0,sizeof(*out));return false;}
    *out=l;return true;
}
bool kui_vmu_native_heap_bottom_placeholder(struct kui_vmu_native_heap_lease *l,
    kui_vmu_native_heap_read read,void *ctx,uint32_t initializations,uint32_t actual) {
    if(!l || l->phase!=1u || !identity(l,read,ctx,initializations) || !bottom_geometry(l) ||
       actual!=l->payload+l->lease_bytes+32u ||
       !read_is(read,ctx,l->owner.sentinel,l->begin) ||
       !read_is(read,ctx,l->owner.cursor_cell,l->owner.sentinel) ||
       !read_is(read,ctx,l->begin,l->owner.sentinel) ||
       !read_is(read,ctx,l->begin+4u,l->units) ||
       !read_is(read,ctx,actual-28u,l->remaining)) {
        if(l) l->phase=0u;
        return false;
    }
    l->phase=3u;return true;
}
bool kui_vmu_native_heap_bottom_allocated(struct kui_vmu_native_heap_lease *l,
    kui_vmu_native_heap_read read,void *ctx,uint32_t initializations,uint32_t actual) {
    if(!l || l->phase!=3u || !identity(l,read,ctx,initializations) || !bottom_geometry(l) ||
       actual!=l->payload ||
       !read_is(read,ctx,l->owner.sentinel,l->owner.sentinel) ||
       !read_is(read,ctx,l->owner.cursor_cell,l->owner.sentinel) ||
       !read_is(read,ctx,l->header+4u,l->units) ||
       !read_is(read,ctx,l->payload+l->lease_bytes+4u,l->remaining)) {
        if(l) l->phase=0u;
        return false;
    }
    l->phase=2u;return true;
}
bool kui_vmu_native_heap_bottom_ready(const struct kui_vmu_native_heap_lease *l,
    kui_vmu_native_heap_read read,void *ctx,uint32_t initializations) {
    if(!l || !kui_vmu_native_heap_lease_identity_valid(l,read,ctx,initializations) ||
       !bottom_geometry(l)) return false;
    uint32_t upper=l->payload+l->lease_bytes;
    return read_is(read,ctx,l->owner.sentinel,upper) &&
        read_is(read,ctx,l->owner.cursor_cell,l->owner.sentinel) &&
        read_is(read,ctx,upper,l->owner.sentinel) &&
        read_is(read,ctx,upper+4u,l->remaining);
}
bool kui_vmu_native_heap_lease_identity_valid(const struct kui_vmu_native_heap_lease *l,
    kui_vmu_native_heap_read read,void *ctx,uint32_t initializations) {
    return l && l->phase==2u && identity(l,read,ctx,initializations) &&
        read_is(read,ctx,l->header+4u,l->units);
}
bool kui_vmu_native_heap_lease_valid_with_release(const struct kui_vmu_native_heap_lease *l,
    kui_vmu_native_heap_read read,void *ctx,uint32_t initializations,uint32_t payload,
    uint32_t *release_bytes) {
    if(!kui_vmu_native_heap_lease_identity_valid(l,read,ctx,initializations)) return false;
    uint32_t alias=l->begin&UINT32_C(0xe0000000),release_begin=0u,release_end=0u;
    if(payload) {
        uint32_t at=phys(payload),begin=phys(l->begin),end=begin+l->heap_bytes,units;
        if((payload&UINT32_C(0xe0000000))!=alias || (at&31u) ||
           at<begin+32u || at>end) return false;
        release_begin=at-32u;
        uint32_t guard=phys(l->header),guard_end=phys(l->payload)+l->lease_bytes;
        if((at>=guard && at<guard_end) || !read(ctx,(release_begin|alias)+4u,&units) ||
           !units || units>(end-release_begin)/32u) return false;
        release_end=release_begin+units*32u;
        if(release_begin<guard_end && guard<release_end) return false;
    }
    uint32_t node,cursor;bool cursor_seen=false;
    if(!read(ctx,l->owner.sentinel,&node) || !read(ctx,l->owner.cursor_cell,&cursor)) return false;
    if(cursor==l->owner.sentinel) cursor_seen=true;
    uint32_t previous_end=0u;
    for(unsigned i=0;i<KUI_VMU_NATIVE_HEAP_FREE_LIMIT;i++) {
        if(node==l->owner.sentinel) {
            if(cursor_seen && release_bytes) *release_bytes=release_end-release_begin;
            return cursor_seen;
        }
        uint32_t at=phys(node),begin=phys(l->begin),units,next;
        if((payload && (node&UINT32_C(0xe0000000))!=alias) ||
           (node&31u) || !span(node,8u) || at<begin || at-begin>=l->heap_bytes ||
           (previous_end && at<previous_end) || !read(ctx,node+4u,&units) || !units ||
           units>(l->heap_bytes-(at-begin))/32u ||
           (at<phys(l->payload)+l->lease_bytes && phys(l->header)<at+units*32u) ||
           (payload && at<release_end && release_begin<at+units*32u) ||
           !read(ctx,node,&next)) return false;
        if(node==cursor) cursor_seen=true;
        previous_end=at+units*32u;node=next;
    }
    return false;
}
bool kui_vmu_native_heap_lease_valid(const struct kui_vmu_native_heap_lease *l,
    kui_vmu_native_heap_read read,void *ctx,uint32_t initializations) {
    return kui_vmu_native_heap_lease_valid_with_release(l,read,ctx,initializations,0u,NULL);
}
#include "vmu_native_heap_bottom_code.inc"
