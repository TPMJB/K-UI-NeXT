/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_MAPLE_STATE_H
#define KUI_ARMADA_CE_MAPLE_STATE_H
#include "kui/armada_ce_maple_proof.h"
#include "kui/armada_ce_maple_owner_pointer.h"
#if KUI_ARMADA_CE_MAPLE_HEADERS
#include "kui/armada_ce_maple_headers.h"
#endif
#define KUI_ARMADA_CE_MAPLE_STATE_BYTES UINT32_C(0x198)
#define KUI_ARMADA_CE_MAPLE_STATE_TX_BYTES UINT32_C(0x1000)
#define KUI_ARMADA_CE_MAPLE_STATE_RX_BYTES UINT32_C(0x2000)
/* All statuses use the proof enum. Success is metadata observation only.
 * counts: status, all reads, steps (1 prior guards,2 extension,4 TX,8 RX,
 *         16 all final witnesses), raw extension pointer, explicit owner VA.
 * extension: first P1 word, page word, shutdown, raw MMIO VA, source bytes198.
 * tx: raw bus, raw VA, raw last, raw end, resolved P1 buffer start.
 * rx: raw low physical, raw high physical, raw VA, bias, resolved P1 start.
 * maps: TX/RX-first/RX-second page words, raw TTB slot0 and owner entries.
 * fault: reason, address, observed, expected, detail.
 * There is no extension self/magic/size member. Page alignment, RX high0 and
 * uncached buffer PTEs are conservative admission restrictions for this probe,
 * not assertions about every legal CE allocation. Raw values survive refusal.
 * With HEADERS disabled, transfer buffers are mapped only. The isolated
 * HEADERS build additionally samples bounded ordinary headers through P2;
 * final witnesses below must still pass. Coherent reads use extension-field
 * VAs. A corrupt root can alias a
 * different allocation before its metadata supplies buffer addresses; the
 * overlap checks then refuse that observation. Allocation identity/lifetime
 * is not established. The MMIO VA is never followed. Header mode follows
 * only bounded ordinary descriptors and receive header destinations. */
struct kui_armada_ce_maple_state_snapshot {
    uint32_t counts[5],extension[5],tx[5],rx[5],maps[5],fault[5];
};
_Static_assert(sizeof(struct kui_armada_ce_maple_state_snapshot)==120u,"CE Maple state rows ABI");
struct kui_armada_ce_maple_state_workspace {
    struct kui_armada_ce_maple_proof_workspace guard;
    struct kui_armada_ce_maple_proof_snapshot guard_out;
    struct kui_armada_ce_maple_mapping extension[10],buffer[3];
    uint32_t word[10];
};
static inline bool kui_armada_ce_maple_state_fail(struct kui_armada_ce_maple_state_workspace *w,
    enum kui_armada_ce_maple_proof_status status,uint32_t at,uint32_t actual,uint32_t expected,uint32_t detail) {
    (void)kui_armada_ce_maple_proof_fault(&w->guard,status,at,actual,expected,detail);return false;
}
static inline bool kui_armada_ce_maple_state_map(struct kui_armada_ce_maple_state_workspace *w,
    uint32_t ttb,uint32_t va,struct kui_armada_ce_maple_mapping *m) {
    enum kui_armada_ce_ram_bind_status status=kui_armada_ce_maple_resolve(ttb,va,
        kui_armada_ce_maple_proof_read,&w->guard,m);
    if(status==KUI_ARMADA_CE_RAM_BIND_OK) return true;
    if(!w->guard.failed) (void)kui_armada_ce_maple_state_fail(w,status==KUI_ARMADA_CE_RAM_BIND_CHANGED?
        KUI_ARMADA_CE_MAPLE_PROOF_CHANGED:KUI_ARMADA_CE_MAPLE_PROOF_MAPPING,va,(uint32_t)status,0,m->page_word);
    return false;
}
static inline bool kui_armada_ce_maple_state_map_repeat(struct kui_armada_ce_maple_state_workspace *w,
    uint32_t ttb,const struct kui_armada_ce_maple_mapping *before,bool read_value,uint32_t expected) {
    struct kui_armada_ce_maple_mapping m;
    if(!kui_armada_ce_maple_state_map(w,ttb,before->virtual_address,&m)) return false;
    const uint32_t *a=(const uint32_t *)(const void *)&m,*b=(const uint32_t *)(const void *)before;
    for(unsigned i=0;i<8u;i++) if(a[i]!=b[i])
        return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_CHANGED,m.virtual_address,a[i],b[i],i);
    if(read_value) {
        uint32_t value;
        if(!kui_armada_ce_maple_proof_value(&w->guard,&m,&value)) return false;
        if(value!=expected) return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_CHANGED,m.virtual_address,value,expected,8);
    }
    return true;
}
static inline bool kui_armada_ce_maple_state_prior(struct kui_armada_ce_maple_state_workspace *w,
    const struct kui_armada_ce_maple_snapshot *s,const struct kui_armada_ce_maple_workspace *old,
    const struct kui_armada_ce_maple_proof_snapshot *p,const struct kui_armada_ce_maple_proof_workspace *pw,
    struct kui_armada_ce_maple_state_snapshot *out,bool first) {
    static const uint32_t cpu_at[7]={KUI_ARMADA_CE_MAPLE_TTB,KUI_ARMADA_CE_MAPLE_PTEH,KUI_ARMADA_CE_MAPLE_CCR,
        KUI_ARMADA_CE_MAPLE_CURRENT_PROCESS,KUI_ARMADA_CE_MAPLE_HEAP_LOW,KUI_ARMADA_CE_MAPLE_HEAP_HIGH,KUI_ARMADA_CE_MAPLE_MODULE_HEAD};
    uint32_t p1,process=p->owner[0],slot=p->owner[1];
    if((s->counts[4]!=KUI_ARMADA_CE_MAPLE_OK && s->counts[4]!=KUI_ARMADA_CE_MAPLE_MISSING) ||
       p->counts[0]!=KUI_ARMADA_CE_MAPLE_PROOF_OK || p->counts[4]!=31u ||
       p->counts[2]!=KUI_ARMADA_CE_MAPLE_PROOF_EXPECTED_CRC || p->counts[3]!=p->counts[2] ||
       p->counts[1]!=pw->reads || pw->reads>KUI_ARMADA_CE_MAPLE_PROOF_READ_LIMIT ||
       !old->nodes || old->nodes>KUI_ARMADA_CE_MAPLE_MAX_NODES || old->reads!=s->counts[3] ||
       old->reads>KUI_ARMADA_CE_MAPLE_READ_LIMIT || !s->maple[0] || s->maple[4]!=p->owner[3] ||
       !kui_armada_ce_ram_table_p1(pw->cpu[0],&p1) || (pw->cpu[2]&0xa1u)!=1u ||
       !kui_armada_ce_ram_table_p1(pw->cpu[4],&p1) || pw->cpu[5]<=pw->cpu[4] ||
       (pw->cpu[5]&3u) || pw->cpu[5]>0x8d000000u || pw->cpu[6]!=old->node[0].address ||
       process!=pw->cpu[3] || process!=s->cpu[3] || process<KUI_ARMADA_CE_MAPLE_PROCESS_BASE ||
       process>KUI_ARMADA_CE_MAPLE_PROCESS_BASE+31u*KUI_ARMADA_CE_MAPLE_PROCESS_STRIDE ||
       (process-KUI_ARMADA_CE_MAPLE_PROCESS_BASE)%KUI_ARMADA_CE_MAPLE_PROCESS_STRIDE)
        return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,process,p->counts[0],KUI_ARMADA_CE_MAPLE_PROOF_OK,0);
    uint32_t index=(process-KUI_ARMADA_CE_MAPLE_PROCESS_BASE)/KUI_ARMADA_CE_MAPLE_PROCESS_STRIDE;
    if(slot!=((index+1u)<<25) || slot!=pw->process[1] || p->owner[2]!=pw->process[2] ||
       (pw->process[0]&255u)!=index || !pw->process[2] || pw->process[2]==UINT32_MAX ||
       !(p->owner[3]&(UINT32_C(1)<<index)) || !((pw->process[3]>>(16u*(index&1u)))&0xffffu) ||
       p->root[2]!=pw->word[8] || p->root[4]!=pw->word[9])
        return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_PROCESS,process,slot,(index+1u)<<25,index);
    for(unsigned i=0;i<7u;i++) {
        if((i<4u && pw->cpu[i]!=s->cpu[i]) || (i==6u && pw->cpu[i]!=s->cpu[4]))
            return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,cpu_at[i],pw->cpu[i],i==6u?s->cpu[4]:s->cpu[i],i);
        if(!kui_armada_ce_maple_proof_repeat(&w->guard,cpu_at[i],pw->cpu[i],i)) return false;
        w->guard.cpu[i]=pw->cpu[i];
    }
    for(unsigned i=0;i<old->nodes;i++) {
        uint32_t node=old->node[i].address;
        if(!kui_armada_ce_ram_table_p1(node,&p1) || node<pw->cpu[4] || node>=pw->cpu[5] ||
           pw->cpu[5]-node<180u || old->node[i].field[0]!=node ||
           old->node[i].field[1]!=(i+1u<old->nodes?old->node[i+1u].address:0u))
            return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,node,pw->cpu[4],pw->cpu[5],i);
    }
    uint32_t root0,owner_entry;
    if(!kui_armada_ce_maple_proof_read(&w->guard,pw->cpu[0],&root0) ||
       !kui_armada_ce_maple_proof_read(&w->guard,pw->cpu[0]+4u*(slot>>25),&owner_entry)) return false;
    if(first) {out->maps[3]=root0;out->maps[4]=owner_entry;}
    if(root0!=owner_entry || (!first && (root0!=out->maps[3] || owner_entry!=out->maps[4])))
        return kui_armada_ce_maple_state_fail(w,first?KUI_ARMADA_CE_MAPLE_PROOF_PROCESS:KUI_ARMADA_CE_MAPLE_PROOF_CHANGED,
            pw->cpu[0],root0,owner_entry,slot>>25);
    uint32_t process_at[4]={process,process+12u,process+8u,s->maple[0]+16u+4u*(index/2u)};
    for(unsigned i=0;i<4u;i++) if(!kui_armada_ce_maple_proof_repeat(&w->guard,process_at[i],pw->process[i],i)) return false;
    if(!kui_armada_ce_maple_proof_list(&w->guard,old)) return false;
    for(unsigned i=0;i<10u;i++)
        if(!kui_armada_ce_maple_state_map_repeat(w,pw->cpu[0],&pw->mapping[i],true,pw->word[i])) return false;
    if(!first) for(unsigned i=0;i<7u;i++)
        if(!kui_armada_ce_maple_proof_repeat(&w->guard,cpu_at[i],pw->cpu[i],i)) return false;
    return true;
}
static inline bool kui_armada_ce_maple_state_body(struct kui_armada_ce_maple_state_workspace *w,
    const struct kui_armada_ce_maple_snapshot *s,const struct kui_armada_ce_maple_workspace *old,
    const struct kui_armada_ce_maple_proof_snapshot *p,const struct kui_armada_ce_maple_proof_workspace *pw,
    struct kui_armada_ce_maple_state_snapshot *out) {
    static const uint32_t offset[10]={0,0x84,0x88,0x8c,0x90,0x100,0xc0,0xc4,0xcc,0xd8};
    out->counts[3]=p->root[2];out->extension[4]=KUI_ARMADA_CE_MAPLE_STATE_BYTES;
    if(!kui_armada_ce_maple_state_prior(w,s,old,p,pw,out,true)) return false;
    out->counts[2]|=1u;
    uint32_t explicit_va;
    if(!kui_armada_ce_maple_owner_pointer(out->counts[3],p->owner[1],KUI_ARMADA_CE_MAPLE_STATE_BYTES,&explicit_va))
        return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_PROCESS,p->owner[4],out->counts[3],p->owner[1],KUI_ARMADA_CE_MAPLE_STATE_BYTES);
    out->counts[4]=explicit_va;
    for(unsigned i=0;i<10u;i++) {
        if(!kui_armada_ce_maple_state_map(w,pw->cpu[0],explicit_va+offset[i],&w->extension[i]) ||
           !kui_armada_ce_maple_proof_value(&w->guard,&w->extension[i],&w->word[i])) return false;
        uint32_t v=w->word[i];
        if(i==0u) {out->extension[0]=w->extension[i].physical_address;out->extension[1]=w->extension[i].page_word;out->extension[2]=v;}
        if(i>=1u && i<=4u) out->tx[i-1u]=v;
        if(i==5u) out->extension[3]=v;
        if(i>=6u) out->rx[i-6u]=v;
    }
    out->counts[2]|=2u;
    if(out->extension[2])
        return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,explicit_va,out->extension[2],0,0);
    uint32_t tx,rx,last,end;
    if(!kui_armada_ce_maple_owner_pointer(out->tx[1],p->owner[1],KUI_ARMADA_CE_MAPLE_STATE_TX_BYTES,&tx) || (tx&0xfffu))
        return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_PROCESS,explicit_va+0x88u,out->tx[1],p->owner[1],0x1000u);
    if(out->tx[0]<0x0c010000u || out->tx[0]>0x0cfff000u || (out->tx[0]&0xfffu))
        return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_MAPPING,explicit_va+0x84u,out->tx[0],0x0c010000u,0x1000u);
    if(explicit_va<tx+0x1000u && tx<explicit_va+KUI_ARMADA_CE_MAPLE_STATE_BYTES)
        return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,explicit_va,explicit_va,tx,0x1000u);
    if(out->tx[2] || out->tx[3]) {
        if(!kui_armada_ce_maple_owner_pointer(out->tx[2],p->owner[1],12u,&last) ||
           !kui_armada_ce_maple_owner_pointer(out->tx[3],p->owner[1],4u,&end) ||
           last<tx || end<tx+12u || end>tx+0x1000u || last>end-12u)
            return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,explicit_va+0x8cu,out->tx[2],out->tx[3],12u);
    }
    bool tx_ok=kui_armada_ce_maple_state_map(w,pw->cpu[0],tx,&w->buffer[0]);
    out->maps[0]=w->buffer[0].page_word;out->tx[4]=w->buffer[0].physical_address;
    if(!tx_ok) return false;
    if((out->maps[0]&8u) || out->tx[4]!=(out->tx[0]|0x80000000u))
        return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_MAPPING,tx,out->tx[4],out->tx[0]|0x80000000u,out->maps[0]);
    out->counts[2]|=4u;
    if(!kui_armada_ce_maple_owner_pointer(out->rx[2],p->owner[1],KUI_ARMADA_CE_MAPLE_STATE_RX_BYTES,&rx) || (rx&0xfffu))
        return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_PROCESS,explicit_va+0xccu,out->rx[2],p->owner[1],0x2000u);
    if(out->rx[1] || out->rx[0]<0x0c010000u || out->rx[0]>0x0cffe000u || (out->rx[0]&0xfffu) ||
       out->rx[3]+out->rx[2]!=out->rx[0])
        return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_MAPPING,explicit_va+0xc0u,out->rx[0],out->rx[2]+out->rx[3],out->rx[1]);
    if(explicit_va<rx+0x2000u && rx<explicit_va+KUI_ARMADA_CE_MAPLE_STATE_BYTES)
        return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,explicit_va,explicit_va,rx,0x2000u);
    if((tx<rx+0x2000u && rx<tx+0x1000u) ||
       (out->tx[0]<out->rx[0]+0x2000u && out->rx[0]<out->tx[0]+0x1000u))
        return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,explicit_va+0xccu,rx,tx,out->rx[0]);
    /* Only now are both raw physical ranges admitted. The ten coherent
     * metadata reads above already occurred; reject aliasing before repeating
     * metadata or using its buffer mappings in further proof steps. */
    for(unsigned i=0;i<10u;i++) {
        uint32_t physical=w->extension[i].physical_address&0x1fffffffu;
        if(physical>=out->tx[0] && physical-out->tx[0]<0x1000u)
            return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,
                w->extension[i].virtual_address,physical,out->tx[0],i);
        if(physical>=out->rx[0] && physical-out->rx[0]<0x2000u)
            return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,
                w->extension[i].virtual_address,physical,out->rx[0],i);
    }
    for(unsigned i=0;i<2u;i++) {
        bool ok=kui_armada_ce_maple_state_map(w,pw->cpu[0],rx+i*0x1000u,&w->buffer[i+1u]);
        out->maps[i+1u]=w->buffer[i+1u].page_word;
        if(!i) out->rx[4]=w->buffer[i+1u].physical_address;
        if(!ok) return false;
        if((out->maps[i+1u]&8u) || w->buffer[i+1u].physical_address!=((out->rx[0]+i*0x1000u)|0x80000000u))
            return kui_armada_ce_maple_state_fail(w,KUI_ARMADA_CE_MAPLE_PROOF_MAPPING,rx+i*0x1000u,
                w->buffer[i+1u].physical_address,(out->rx[0]+i*0x1000u)|0x80000000u,out->maps[i+1u]);
    }
    out->counts[2]|=8u;
#if KUI_ARMADA_CE_MAPLE_HEADERS
    /* These mappings admit reads only of ordinary descriptor headers and
     * pooled response word0. The remaining witnesses bracket this capture.
     * VBlank DMA is autonomous: repeated headers do not establish idleness. */
    (void)kui_armada_ce_maple_headers(out->tx,out->rx,out->maps,
        kui_armada_ce_maple_proof_cache_read,&w->guard,&w->guard.headers);
    if(w->guard.failed) return false;
#endif
    for(unsigned i=0;i<10u;i++) if(!kui_armada_ce_maple_state_map_repeat(w,pw->cpu[0],&w->extension[i],true,w->word[i])) return false;
    for(unsigned i=0;i<3u;i++) if(!kui_armada_ce_maple_state_map_repeat(w,pw->cpu[0],&w->buffer[i],false,0)) return false;
    if(!kui_armada_ce_maple_state_prior(w,s,old,p,pw,out,false)) return false;
    out->counts[2]|=16u;return true;
}
static inline enum kui_armada_ce_maple_proof_status kui_armada_ce_maple_state(
    const struct kui_armada_ce_maple_snapshot *s,const struct kui_armada_ce_maple_workspace *old,
    const struct kui_armada_ce_maple_proof_snapshot *p,const struct kui_armada_ce_maple_proof_workspace *pw,
    kui_armada_ce_ram_read32 read32,kui_armada_ce_ram_read32 read_cache,void *context,
    struct kui_armada_ce_maple_state_workspace *w,struct kui_armada_ce_maple_state_snapshot *out) {
    if(!s || !old || !p || !pw || !read32 || !read_cache || !w || !out) return KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT;
    const void *input[4]={s,old,p,pw};static const size_t bytes[4]={sizeof(*s),sizeof(*old),sizeof(*p),sizeof(*pw)};
    if(kui_armada_ce_maple_proof_overlap((uintptr_t)(void *)w,sizeof(*w),(uintptr_t)(void *)out,sizeof(*out))) return KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT;
    for(unsigned i=0;i<4u;i++) if(kui_armada_ce_maple_proof_overlap((uintptr_t)(void *)w,sizeof(*w),(uintptr_t)input[i],bytes[i]) ||
       kui_armada_ce_maple_proof_overlap((uintptr_t)(void *)out,sizeof(*out),(uintptr_t)input[i],bytes[i])) return KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT;
    kui_armada_ce_maple_zero(out,sizeof(*out));kui_armada_ce_maple_zero(&w->guard_out,sizeof(w->guard_out));
#if KUI_ARMADA_CE_MAPLE_HEADERS
    kui_armada_ce_maple_zero(&w->guard.headers,sizeof(w->guard.headers));
#endif
    w->guard.read32=read32;w->guard.read_cache=read_cache;w->guard.context=context;w->guard.out=&w->guard_out;
    w->guard.reads=w->guard.failed=0;
    bool valid=kui_armada_ce_maple_state_body(w,s,old,p,pw,out);
    out->counts[0]=valid?KUI_ARMADA_CE_MAPLE_PROOF_OK:w->guard.failed;out->counts[1]=w->guard.reads;
    for(unsigned i=0;i<5u;i++) out->fault[i]=w->guard_out.fault[i];
#if KUI_ARMADA_CE_MAPLE_HEADERS
    if(!valid && w->guard.headers.counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_OK) {
        w->guard.headers.counts[0]=KUI_ARMADA_CE_MAPLE_PROOF_CHANGED;
        for(unsigned i=0;i<5u;i++) w->guard.headers.fault[i]=out->fault[i];
    }
#endif
    return (enum kui_armada_ce_maple_proof_status)out->counts[0];
}
#endif
