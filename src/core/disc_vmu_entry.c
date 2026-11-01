/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_vmu_entry.h"
#include <string.h>

enum { PATHS=64, STEPS=64 };
enum kind { ORDINARY,REPLACES_PR,READS_PR,CONTROL,UNKNOWN };

static uint16_t word(const uint8_t *p) {return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static bool overlap(uintptr_t a,size_t an,uintptr_t b,size_t bn) {
    if(an>UINTPTR_MAX-a || bn>UINTPTR_MAX-b) return true;
    if(a<b+bn && b<a+an) return true;
#if UINTPTR_MAX>UINT32_MAX
    if(a>UINT32_MAX || b>UINT32_MAX) return false;
#endif
    uint32_t aa=(uint32_t)a&UINT32_C(0xe0000000),ba=(uint32_t)b&UINT32_C(0xe0000000);
    uint32_t ap=(uint32_t)a&UINT32_C(0x1fffffff),bp=(uint32_t)b&UINT32_C(0x1fffffff);
    bool ar=aa==0u || aa==UINT32_C(0x80000000) || aa==UINT32_C(0xa0000000);
    bool br=ba==0u || ba==UINT32_C(0x80000000) || ba==UINT32_C(0xa0000000);
    return ar && br && ap>=UINT32_C(0x0c000000) && bp>=UINT32_C(0x0c000000) &&
        ap<UINT32_C(0x0d000000) && bp<UINT32_C(0x0d000000) &&
        an<=UINT32_C(0x0d000000)-ap && bn<=UINT32_C(0x0d000000)-bp &&
        ap<bp+bn && bp<ap+an;
}
static bool refused(struct kui_disc_vmu_entry_failure *out,uint32_t load,
    uint32_t at,uint32_t opcode,uint32_t reason) {
    if(out) *out=(struct kui_disc_vmu_entry_failure){load+at,opcode,reason};
    return false;
}
static bool delayed(uint16_t op) {
    return (op>>12)==0xau || (op>>12)==0xbu || (op>>8)==0x8du ||
        (op>>8)==0x8fu || (op&0xf0ffu)==0x400bu ||
        (op&0xf0ffu)==0x402bu || (op&0xf0ffu)==0x0003u ||
        (op&0xf0ffu)==0x0023u || op==0x000bu || op==0x002bu;
}
/* Renesas SH-4 delay-slot restrictions, matching tools/sh4_delay_slots.py.
 * Opcode admission remains separate: legality alone never permits unknown
 * or privileged startup instructions. */
static bool slot_illegal(uint16_t op) {
    return (op>>12)==9u || (op>>12)==0xdu || (op>>8)==0xc7u || delayed(op) ||
        (op>>8)==0x89u || (op>>8)==0x8bu || (op>>8)==0xc3u ||
        (op&0xf0ffu)==0x400eu || (op&0xf0ffu)==0x4007u || op==0xfffdu;
}
static enum kind classify(uint16_t op) {
    uint16_t fixed=op&0xf0ffu;
    if(fixed==0x002au || fixed==0x4022u) return READS_PR;
    if(fixed==0x402au || fixed==0x4026u) return REPLACES_PR;
    if(delayed(op) || (op>>8)==0x89u || (op>>8)==0x8bu ||
       (op>>8)==0xc3u || op==0x001bu || op==0xfffdu) return CONTROL;
    switch(op>>12) {
    case 1:case 5:case 6:case 7:case 9:case 0xd:case 0xe:
        return ORDINARY;
    case 2:
        return (op&15u)!=3u?ORDINARY:UNKNOWN;
    case 3:
        return (op&15u)!=1u && (op&15u)!=9u?ORDINARY:UNKNOWN;
    case 8:
        switch(op>>8) {
        case 0x80:case 0x81:case 0x84:case 0x85:case 0x88:return ORDINARY;
        default:return UNKNOWN;
        }
    case 0xc:
        return ORDINARY; /* TRAPA was refused above; MOVA is slot-checked. */
    case 0:
        if(op==0x0008u || op==0x0018u || op==0x0028u || op==0x0019u || op==0x0009u)
            return ORDINARY;
        switch(fixed) {
        case 0x0002:case 0x0012:case 0x0022:case 0x0032:case 0x0042:
        case 0x000a:case 0x001a:case 0x005a:case 0x006a:case 0x0029:
        case 0x0083:case 0x0093:case 0x00a3:case 0x00b3:
            return ORDINARY;
        default:break;
        }
        switch(op&15u) {
        case 4:case 5:case 6:case 7:case 0xc:case 0xd:case 0xe:case 0xf:
            return ORDINARY;
        default:return UNKNOWN;
        }
    case 4:
        if((op&15u)==0xcu || (op&15u)==0xdu || (op&15u)==0xfu) return ORDINARY;
        switch(fixed) {
        case 0x4000:case 0x4001:case 0x4004:case 0x4005:case 0x4008:case 0x4009:
        case 0x4010:case 0x4011:case 0x4015:case 0x4018:case 0x4019:case 0x401b:
        case 0x4020:case 0x4021:case 0x4024:case 0x4025:case 0x4028:case 0x4029:
        case 0x4003:case 0x4013:case 0x4023:case 0x4033:case 0x4043:
        case 0x4002:case 0x4012:case 0x4052:case 0x4062:
        case 0x400e:case 0x401e:case 0x402e:case 0x403e:case 0x404e:
        case 0x4007:case 0x4017:case 0x4027:case 0x4037:case 0x4047:
        case 0x400a:case 0x401a:case 0x405a:case 0x406a:
        case 0x4006:case 0x4016:case 0x4056:case 0x4066:
            return ORDINARY;
        default:return UNKNOWN;
        }
    default:return UNKNOWN;
    }
}
static uint32_t reason(enum kind k) {
    return k==READS_PR?KUI_DISC_VMU_ENTRY_PR_READ:
        k==CONTROL?KUI_DISC_VMU_ENTRY_CONTROL:KUI_DISC_VMU_ENTRY_UNKNOWN;
}
static bool enqueue(uint32_t pending[PATHS],unsigned *count,uint32_t target,
    size_t bytes,struct kui_disc_vmu_entry_failure *failure,uint32_t load,
    uint32_t pc,uint16_t opcode) {
    if(target<=pc) return refused(failure,load,pc,opcode,KUI_DISC_VMU_ENTRY_BACKWARD);
    if(target>bytes || bytes-target<2u)
        return refused(failure,load,target,0u,KUI_DISC_VMU_ENTRY_BOUNDS);
    if(*count==PATHS) return refused(failure,load,pc,opcode,KUI_DISC_VMU_ENTRY_LIMIT);
    pending[(*count)++]=target;return true;
}
bool kui_disc_vmu_entry_drops_inherited_pr(const void *image,size_t bytes,
    uint32_t load,struct kui_disc_vmu_entry_failure *failure) {
    if(failure && image && overlap((uintptr_t)image,bytes,(uintptr_t)failure,sizeof(*failure))) return false;
    if(failure) memset(failure,0,sizeof(*failure));
    uint32_t area=load&UINT32_C(0xe0000000),p=load&UINT32_C(0x1fffffff);
    if(!image || bytes<2u || bytes>UINT32_C(0xc00000) || bytes>UINTPTR_MAX-(uintptr_t)image ||
       (load&1u) || (area!=0u && area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000)) ||
       p<UINT32_C(0x0c010000) || p>=UINT32_C(0x0d000000) || bytes>UINT32_C(0x0d000000)-p)
        return refused(failure,load,0u,0u,KUI_DISC_VMU_ENTRY_ARGUMENT);
    const uint8_t *code=image;
    uint32_t pending[PATHS]={0u},seen[PATHS];
    _Static_assert(sizeof(pending)+sizeof(seen)<=512u,"Entry path storage bound");
    unsigned count=1u,visited=0u,steps=0u;
    while(count) {
        uint32_t pc=pending[--count];bool known=false;
        for(unsigned i=0u;i<visited;i++) if(seen[i]==pc) {known=true;break;}
        if(known) continue;
        if(visited==PATHS || steps++==STEPS)
            return refused(failure,load,pc,0u,KUI_DISC_VMU_ENTRY_LIMIT);
        seen[visited++]=pc;
        if(pc>bytes || bytes-pc<2u) return refused(failure,load,pc,0u,KUI_DISC_VMU_ENTRY_BOUNDS);
        uint16_t op=word(code+pc);
        bool call=(op>>12)==0xbu || (op&0xf0ffu)==0x400bu;
        bool bra=(op>>12)==0xau;
        bool conditional=(op>>8)==0x89u || (op>>8)==0x8bu || (op>>8)==0x8du || (op>>8)==0x8fu;
        bool has_slot=call || bra || (conditional && ((op>>8)==0x8du || (op>>8)==0x8fu));
        bool slot_replaces=false;
        if(has_slot) {
            if(bytes-pc<4u) return refused(failure,load,pc+2u,0u,KUI_DISC_VMU_ENTRY_BOUNDS);
            uint16_t slot=word(code+pc+2u);enum kind k=classify(slot);
            if(steps++==STEPS) return refused(failure,load,pc+2u,slot,KUI_DISC_VMU_ENTRY_LIMIT);
            if(k==READS_PR) return refused(failure,load,pc+2u,slot,KUI_DISC_VMU_ENTRY_PR_READ);
            if(slot_illegal(slot)) return refused(failure,load,pc+2u,slot,KUI_DISC_VMU_ENTRY_CONTROL);
            if(k!=ORDINARY && k!=REPLACES_PR) return refused(failure,load,pc+2u,slot,reason(k));
            slot_replaces=k==REPLACES_PR;
        }
        if(call || slot_replaces) continue;
        if(bra || conditional) {
            int32_t displacement=bra?(int32_t)(op&0xfffu):(int32_t)(op&255u);
            if(bra?displacement&0x800:displacement&0x80) displacement-=bra?0x1000:0x100;
            int32_t target=(int32_t)pc+4+displacement*2;
            if(target<0 || !enqueue(pending,&count,(uint32_t)target,bytes,failure,load,pc,op)) {
                if(target<0) return refused(failure,load,pc,op,KUI_DISC_VMU_ENTRY_BACKWARD);
                return false;
            }
            if(conditional && !enqueue(pending,&count,pc+(has_slot?4u:2u),bytes,failure,load,pc,op)) return false;
            continue;
        }
        enum kind k=classify(op);
        if(k==REPLACES_PR) continue;
        if(k!=ORDINARY) return refused(failure,load,pc,op,reason(k));
        if(!enqueue(pending,&count,pc+2u,bytes,failure,load,pc,op)) return false;
    }
    return true;
}
