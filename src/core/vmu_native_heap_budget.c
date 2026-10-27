/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_heap_budget.h"
#include <string.h>

static uint16_t le16(const uint8_t *p) {return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t le32(const uint8_t *p) {return le16(p)|(uint32_t)le16(p+2u)<<16;}
static uint32_t physical(uint32_t a) {return a&UINT32_C(0x1fffffff);}
static bool ram(uint32_t a,uint32_t bytes) {
    uint32_t p=physical(a),alias=a&UINT32_C(0xe0000000);
    return (alias==0u || alias==UINT32_C(0x80000000) || alias==UINT32_C(0xa0000000)) &&
        p>=UINT32_C(0x0c000000) && p<UINT32_C(0x0d000000) &&
        bytes<=UINT32_C(0x0d000000)-p;
}
struct source {const uint8_t *p;size_t bytes;uint32_t load;};
static bool overlaps(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(an>UINTPTR_MAX-x || bn>UINTPTR_MAX-y)return true;
    if(x<y+bn && y<x+an)return true;
#if UINTPTR_MAX > UINT32_MAX
    if(x>UINT32_MAX || y>UINT32_MAX)return false;
#endif
    return an<=UINT32_MAX && bn<=UINT32_MAX &&
        ram((uint32_t)x,(uint32_t)an) && ram((uint32_t)y,(uint32_t)bn) &&
        physical((uint32_t)x)<physical((uint32_t)y)+bn &&
        physical((uint32_t)y)<physical((uint32_t)x)+an;
}
static const uint8_t *at(const struct source *s,uint32_t address,size_t bytes) {
    uint32_t p=physical(address),base=physical(s->load);
    if(!ram(address,(uint32_t)bytes) || p<base || p-base>s->bytes ||
       bytes>s->bytes-(p-base))return NULL;
    return s->p+(p-base);
}
static bool word(const struct source *s,uint32_t address,uint16_t expected) {
    const uint8_t *p=at(s,address,2u);return p && le16(p)==expected;
}
static bool long_literal(const struct source *s,uint32_t pc,unsigned reg,
    uint32_t *slot,uint32_t *value) {
    const uint8_t *p=at(s,pc,2u);if(!p)return false;
    uint16_t op=le16(p);
    if((op&0xff00u)!=(0xd000u|(reg<<8)))return false;
    uint32_t address=((pc+4u)&~3u)+4u*(op&255u);
    p=at(s,address,4u);if(!p || (address&3u))return false;
    if(slot)*slot=address;
    *value=le32(p);return true;
}
static bool short_literal(const struct source *s,uint32_t pc,unsigned reg,
    uint32_t *value) {
    const uint8_t *p=at(s,pc,2u);if(!p)return false;
    uint16_t op=le16(p);
    if((op&0xff00u)!=(0x9000u|(reg<<8)))return false;
    p=at(s,pc+4u+2u*(op&255u),2u);if(!p)return false;
    *value=(uint32_t)(int32_t)(int16_t)le16(p);return true;
}
struct instruction {uint8_t offset;uint16_t word;};
static bool instructions(const struct source *s,uint32_t pc,
    const struct instruction *code,size_t count) {
    for(size_t i=0u;i<count;i++)if(!word(s,pc+code[i].offset,code[i].word))return false;
    return true;
}
/* Every executable instruction in the bounded caller is covered here or by
 * an independently decoded PC-relative operand below. Addresses and reserve
 * sizes are roles, not title/version literals. */
static const struct instruction parent_code[]={
    {0x00,0x2fe6},{0x02,0x2fd6},{0x04,0x4f22},{0x06,0x7ffc},
    {0x0a,0xede0},{0x0e,0x60e2},{0x10,0x65e2},{0x14,0x201b},
    {0x18,0x20d9},{0x1a,0x2539},{0x1c,0x6403},{0x1e,0x252b},
    {0x22,0x3508},{0x24,0x2f02},{0x26,0x75e0},{0x28,0x420b},
    {0x2a,0x7420},{0x2c,0x62e2},{0x32,0x2239},{0x34,0x63e2},
    {0x38,0x230b},{0x3c,0x23d9},{0x3e,0x221b},{0x40,0x3238},
    {0x44,0x3248},{0x46,0x323c},{0x48,0x6e23},{0x4c,0x2ed9},
    {0x4e,0x420b},{0x50,0x64e3},{0x54,0x6403},{0x56,0x65e3},
    {0x58,0x2302},{0x5a,0x7f04},{0x5c,0x4f26},{0x60,0x6df6},
    {0x62,0x422b},{0x64,0x6ef6}
};
static const struct instruction child_code[]={
    {0x02,0x6753},{0x04,0x6543},{0x06,0x77e0},{0x08,0xe3e0},
    {0x0a,0x2522},{0x0c,0x2739},{0x10,0xe220},{0x12,0xe600},
    {0x14,0x347c},{0x16,0x1561},{0x18,0x1572},{0x1a,0x1563},
    {0x1c,0x1544},{0x1e,0x1566},{0x20,0x1545},{0x22,0x1547},
    {0x24,0x2412},{0x26,0x1461},{0x28,0x1422},{0x2a,0x1453},
    {0x2c,0x1464},{0x2e,0x1465},{0x30,0x1456},{0x32,0x000b},
    {0x34,0x1447}
};
static bool reserve_unique(const struct source *s,uint32_t address,uint32_t consumer) {
    unsigned references=0u;uint32_t target=physical(address);
    for(size_t i=0u;i+2u<=s->bytes;i+=2u) {
        uint32_t pc=s->load+(uint32_t)i;uint16_t op=le16(s->p+i);
        if((op&0xf000u)==0xd000u &&
           physical(((pc+4u)&~3u)+4u*(op&255u))==target) {
            if(physical(pc)!=physical(consumer) || ++references!=1u)return false;
        }
        if((op&0xf000u)==0x9000u) {
            uint32_t p=physical(pc+4u+2u*(op&255u));
            if(p==target || p==target+2u)return false;
        }
        if((op&0xff00u)==0xc700u &&
           physical(((pc+4u)&~3u)+4u*(op&255u))==target)return false;
        int32_t displacement=0;bool branch=false;
        if((op&0xf000u)==0xa000u || (op&0xf000u)==0xb000u) {
            displacement=(int32_t)(op&0xfffu);
            if(displacement&0x800)displacement-=0x1000;
            branch=true;
        } else if((op&0xff00u)==0x8900u || (op&0xff00u)==0x8b00u ||
                  (op&0xff00u)==0x8d00u || (op&0xff00u)==0x8f00u) {
            displacement=(int32_t)(int8_t)op;branch=true;
        }
        if(branch) {
            uint32_t p=physical(pc+4u+2u*(uint32_t)displacement);
            if(p==target || p==target+2u)return false;
        }
        if(!(i&3u) && i+4u<=s->bytes) {
            uint32_t value=le32(s->p+i);
            if(ram(value,4u) && physical(value)==target)return false;
        }
    }
    return references==1u;
}
static bool disjoint(uint32_t address,uint32_t bytes,uint32_t other,uint32_t other_bytes) {
    uint32_t a=physical(address),b=physical(other);
    return a+bytes<=b || b+other_bytes<=a;
}
enum kui_vmu_native_heap_budget_result kui_vmu_native_heap_budget_prepare(
    const void *image,size_t bytes,const struct kui_vmu_native_heap *h,
    const struct kui_vmu_native_heap_call *call,kui_vmu_native_heap_read read,
    void *context,uint32_t lease_cost,struct kui_vmu_native_heap_budget *out) {
    if(!out || overlaps(image,bytes,out,sizeof(*out)) ||
       overlaps(h,sizeof(*h),out,sizeof(*out)) ||
       overlaps(call,sizeof(*call),out,sizeof(*out)))return KUI_VMU_NATIVE_HEAP_BUDGET_INVALID;
    memset(out,0,sizeof(*out));
    if(!image || !h || !call || !read || bytes<2u || bytes>UINT32_C(0xc00000) ||
       bytes>UINTPTR_MAX-(uintptr_t)image || h->version!=KUI_VMU_NATIVE_HEAP_VERSION ||
       h->bytes!=sizeof(*h) || h->family!=KUI_VMU_NATIVE_HEAP_TAIL32 ||
       h->layout!=KUI_VMU_NATIVE_HEAP_DISPATCH2 || h->image_bytes>bytes ||
       (h->load&3u) || !ram(h->load,(uint32_t)bytes) || (call->load&1u) ||
       (h->sentinel&3u) || !ram(h->sentinel,0x30u) ||
       h->dispatch_cell!=h->sentinel-4u || !ram(h->dispatch_cell,4u) ||
       h->begin_cell!=h->sentinel+0x24u || h->bytes_cell!=h->sentinel+0x28u ||
       h->begin_copy_cell!=h->sentinel+0x2cu || (h->dispatch_table&3u) ||
       !ram(h->dispatch_table,8u) || (h->allocate&1u) || !ram(h->allocate,2u) ||
       (call->return_pr&1u) || call->return_pr!=call->call+4u ||
       !lease_cost || (lease_cost&31u) || lease_cost>call->entry_bytes ||
       (call->entry_begin&31u) || (call->entry_bytes&31u) ||
       !ram(call->entry_begin,call->entry_bytes))return KUI_VMU_NATIVE_HEAP_BUDGET_INVALID;
    struct source s={image,bytes,h->load};uint32_t resume=call->return_pr;
    if(!at(&s,h->dispatch_table,8u) || !at(&s,call->load,2u))
        return KUI_VMU_NATIVE_HEAP_BUDGET_INVALID;
    if(!at(&s,resume,0x3au))return KUI_VMU_NATIVE_HEAP_BUDGET_NONE;
    /* Multiple separated shape anchors identify partial family mutations
     * without mistaking every ordinary SDK caller for a malformed match. */
    unsigned shape=word(&s,resume,0x62e2u)+word(&s,resume+8u,0x63e2u)+
        word(&s,resume+0x14u,0x3238u)+word(&s,resume+0x1cu,0x6e23u)+
        word(&s,resume+0x2cu,0x65e3u)+word(&s,resume+0x36u,0x422bu);
    if(shape<4u)return KUI_VMU_NATIVE_HEAP_BUDGET_NONE;
    uint32_t start=resume-0x2cu;
    if(call->load!=start+0x20u ||
       !instructions(&s,start,parent_code,sizeof(parent_code)/sizeof(parent_code[0])))
        return KUI_VMU_NATIVE_HEAP_BUDGET_INVALID;
    uint32_t break_cell,p1,area,end,init_slot,init,reserve_slot,reserve,delta;
    uint32_t allocate,allocate_slot,pool_cell,pool_slot,child,child_slot;
    uint32_t role,begin,heap_bytes,begin_copy,raw_break,live;
    if(!long_literal(&s,start+0x08u,14u,NULL,&break_cell) || (break_cell&3u) ||
       !long_literal(&s,start+0x0cu,1u,NULL,&p1) || p1!=UINT32_C(0x80000000) ||
       !long_literal(&s,start+0x12u,3u,NULL,&area) || area!=UINT32_C(0xe0000000) ||
       !long_literal(&s,start+0x16u,2u,NULL,&end) || end!=UINT32_C(0x0d000000) ||
       !long_literal(&s,start+0x20u,2u,&init_slot,&init) || init_slot!=h->initializer_slot ||
       !long_literal(&s,start+0x2eu,3u,NULL,&role) || role!=area ||
       !long_literal(&s,start+0x30u,0u,NULL,&role) || role!=p1 ||
       !long_literal(&s,start+0x36u,1u,NULL,&role) || role!=end ||
       !long_literal(&s,start+0x3au,4u,&reserve_slot,&reserve) ||
       !short_literal(&s,start+0x42u,3u,&delta) || delta!=UINT32_C(0xfffffbe0) ||
       !long_literal(&s,start+0x4au,2u,&allocate_slot,&allocate) || (allocate&1u) ||
       !long_literal(&s,start+0x52u,3u,&pool_slot,&pool_cell) || (pool_cell&3u) ||
       !long_literal(&s,start+0x5eu,2u,&child_slot,&child) || (child&1u) ||
       reserve_slot!=init_slot+4u || allocate_slot!=reserve_slot+4u ||
       pool_slot!=reserve_slot+8u || child_slot!=reserve_slot+12u ||
       !disjoint(reserve_slot,4u,start,0x66u) ||
       !disjoint(reserve_slot,4u,child,0x36u) ||
       !disjoint(reserve_slot,4u,allocate,10u) ||
       !disjoint(reserve_slot,4u,h->initializer,0x1d0u) ||
       !disjoint(reserve_slot,4u,h->dispatch_table,8u) ||
       !long_literal(&s,allocate,1u,NULL,&role) || role!=h->dispatch_cell ||
       !word(&s,allocate+2u,0x6012u) || !word(&s,allocate+4u,0x6002u) ||
       !word(&s,allocate+6u,0x402bu) || !word(&s,allocate+8u,0x0009u) ||
       !instructions(&s,child,child_code,sizeof(child_code)/sizeof(child_code[0])) ||
       !long_literal(&s,child,2u,NULL,&role) || role!=UINT32_C(0x6e6d0008) ||
       !long_literal(&s,child+0x0eu,1u,NULL,&role) || role!=UINT32_C(0x6e6d0000) ||
       !reserve_unique(&s,reserve_slot,start+0x3au) ||
       !ram(break_cell,4u) || !ram(pool_cell,4u) ||
       !at(&s,break_cell,4u) || !read(context,break_cell,&raw_break) ||
       !read(context,h->begin_cell,&begin) || !read(context,h->bytes_cell,&heap_bytes) ||
       !read(context,h->begin_copy_cell,&begin_copy) ||
       !read(context,pool_cell,&live) || live ||
       !read(context,h->dispatch_cell,&role) || role!=h->dispatch_table ||
       !read(context,h->dispatch_table,&role) || physical(role)!=physical(h->allocate))
        return KUI_VMU_NATIVE_HEAP_BUDGET_INVALID;
    (void)init; /* Its unique word currently contains the already-proved hook. */
    uint32_t aligned=(raw_break|p1)&~31u,limit=(raw_break&area)|end;
    if(raw_break!=le32(at(&s,break_cell,4u)) || !ram(raw_break,1u) ||
       aligned>UINT32_MAX-32u || begin!=aligned+32u || begin_copy!=begin ||
       begin!=call->entry_begin || heap_bytes!=call->entry_bytes ||
       limit<begin || limit-begin!=heap_bytes ||
       physical(pool_cell)<physical(h->load)+bytes ||
       physical(pool_cell)>=physical(begin) || !reserve ||
       reserve>UINT32_MAX-lease_cost || heap_bytes>UINT32_MAX-32u ||
       reserve>heap_bytes+32u || UINT32_C(0x420)>heap_bytes+32u-reserve)
        return KUI_VMU_NATIVE_HEAP_BUDGET_INVALID;
    uint32_t before=(heap_bytes+32u-reserve-UINT32_C(0x420))&~31u;
    if(before<64u || before>heap_bytes-32u || lease_cost>before-64u)
        return KUI_VMU_NATIVE_HEAP_BUDGET_INVALID;
    uint32_t after=before-lease_cost;
    if(after<64u || (after&31u))return KUI_VMU_NATIVE_HEAP_BUDGET_INVALID;
    *out=(struct kui_vmu_native_heap_budget){reserve_slot,reserve,reserve+lease_cost,before,after};
    return KUI_VMU_NATIVE_HEAP_BUDGET_FOUND;
}
