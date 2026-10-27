/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_MAPLE_OBSERVE_H
#define KUI_ARMADA_CE_MAPLE_OBSERVE_H
#include "kui/armada_ce_ram_bind.h"

/* Independently reconstructed metadata from the exact Armada US kernel.
 * ROM identities and geometry are observations, never ownership, executable
 * leases or permission to call/patch a driver. No module section is followed. */
#define KUI_ARMADA_CE_MAPLE_MODULE_HEAD UINT32_C(0x8c145b24)
#define KUI_ARMADA_CE_MAPLE_PROCESS_BASE UINT32_C(0x8c14e6e0)
#define KUI_ARMADA_CE_MAPLE_PROCESS_STRIDE UINT32_C(0x9c)
#define KUI_ARMADA_CE_MAPLE_CURRENT_PROCESS UINT32_C(0x8c145890)
#define KUI_ARMADA_CE_MAPLE_HEAP_LOW UINT32_C(0x8c14d430)
#define KUI_ARMADA_CE_MAPLE_HEAP_HIGH UINT32_C(0x8c14d434)
#define KUI_ARMADA_CE_MAPLE_ROM_TOC UINT32_C(0x8c141614)
#define KUI_ARMADA_CE_MAPLEDEV_ROM_TOC UINT32_C(0x8c1416f4)
#define KUI_ARMADA_CE_MAPLE_NOMINAL UINT32_C(0x01dd0000)
#define KUI_ARMADA_CE_MAPLEDEV_NOMINAL UINT32_C(0x01d20000)
#define KUI_ARMADA_CE_MAPLE_SPAN UINT32_C(0xa000)
#define KUI_ARMADA_CE_MAPLEDEV_SPAN UINT32_C(0x10000)
#define KUI_ARMADA_CE_MAPLE_MAX_NODES 64u
#define KUI_ARMADA_CE_MAPLE_READ_LIMIT 1024u
#define KUI_ARMADA_CE_MAPLE_SAMPLE_LIMIT 32u
#define KUI_ARMADA_CE_MAPLE_TTB UINT32_C(0xff000008)
#define KUI_ARMADA_CE_MAPLE_PTEH UINT32_C(0xff000000)
#define KUI_ARMADA_CE_MAPLE_CCR UINT32_C(0xff00001c)
#define KUI_ARMADA_CE_MAPLE_MDSTAR UINT32_C(0xa05f6c04)
#define KUI_ARMADA_CE_MAPLE_MDTSEL UINT32_C(0xa05f6c10)
#define KUI_ARMADA_CE_MAPLE_MDEN UINT32_C(0xa05f6c14)
#define KUI_ARMADA_CE_MAPLE_MDST UINT32_C(0xa05f6c18)
#define KUI_ARMADA_CE_MAPLE_ISTNRM UINT32_C(0xa05f6900)

enum kui_armada_ce_maple_status {
    KUI_ARMADA_CE_MAPLE_DORMANT,
    KUI_ARMADA_CE_MAPLE_OK,
    KUI_ARMADA_CE_MAPLE_MISSING,
    KUI_ARMADA_CE_MAPLE_ARGUMENT,
    KUI_ARMADA_CE_MAPLE_CONTEXT,
    KUI_ARMADA_CE_MAPLE_HEAP,
    KUI_ARMADA_CE_MAPLE_NODE,
    KUI_ARMADA_CE_MAPLE_CYCLE,
    KUI_ARMADA_CE_MAPLE_LIMIT,
    KUI_ARMADA_CE_MAPLE_MODULE,
    KUI_ARMADA_CE_MAPLE_READ,
    KUI_ARMADA_CE_MAPLE_BUDGET,
    KUI_ARMADA_CE_MAPLE_CHANGED
};

/* Six five-word authored rows. Values from a refused observation remain
 * diagnostic metadata; only counts[4] says whether the observation was stable.
 * counts: samples attempted, stable samples committed (including MISSING),
 *         refused samples, reads attempted in last sample, last status.
 * cpu: TTB, PTEH, CCR, current PROCESS address, module-list head.
 * maple/mapledev: node, explicit-slot base, image span, section count,
 *                attachment mask. These do not identify a callable owner.
 * registers: MDSTAR, MDTSEL, MDEN, MDST, ASIC ISTNRM. They are separate passive
 *            read instants, not an atomic DMA transaction or packet capture.
 * fault: status/reason, address, observed, expected, detail (field/node index).
 * A stable MISSING row is legitimate: unrelated/external DLLs stay unrelated.
 */
struct kui_armada_ce_maple_snapshot {
    uint32_t counts[5],cpu[5],maple[5],mapledev[5],registers[5],fault[5];
};
_Static_assert(sizeof(struct kui_armada_ce_maple_snapshot)==120u,"passive CE Maple report ABI");

/* The complete list is witnessed and repeated, including unrelated nodes.
 * These ten offsets deliberately exclude +0x88 and every pointed-to section,
 * DMA descriptor, response buffer and VMU payload. Keep this workspace static
 * in the retained controller, outside the guarded 2 KiB GD hook stack. */
#define KUI_ARMADA_CE_MAPLE_FIELDS 10u
struct kui_armada_ce_maple_node {uint32_t address,field[KUI_ARMADA_CE_MAPLE_FIELDS];};
struct kui_armada_ce_maple_workspace {
    struct kui_armada_ce_maple_node node[KUI_ARMADA_CE_MAPLE_MAX_NODES];
    uint32_t reads,nodes;
};

static inline void kui_armada_ce_maple_zero(void *object,size_t bytes) {
    uint32_t *p=(uint32_t *)object;
    for(size_t i=0;i<bytes/sizeof(*p);i++) p[i]=0;
}
static inline enum kui_armada_ce_maple_status kui_armada_ce_maple_fault(
    struct kui_armada_ce_maple_snapshot *s,enum kui_armada_ce_maple_status status,
    uint32_t address,uint32_t observed,uint32_t expected,uint32_t detail) {
    s->fault[0]=(uint32_t)status;s->fault[1]=address;s->fault[2]=observed;
    s->fault[3]=expected;s->fault[4]=detail;return status;
}
/* Callback addresses are either canonical aligned P1 main RAM or exactly
 * these eight read-only CPU/Maple/ASIC registers. No range admits +0x88. */
static inline bool kui_armada_ce_maple_read_address(uint32_t address) {
    uint32_t p1;
    return kui_armada_ce_ram_table_p1(address,&p1) ||
        address==KUI_ARMADA_CE_MAPLE_TTB || address==KUI_ARMADA_CE_MAPLE_PTEH ||
        address==KUI_ARMADA_CE_MAPLE_CCR || address==KUI_ARMADA_CE_MAPLE_MDSTAR ||
        address==KUI_ARMADA_CE_MAPLE_MDTSEL || address==KUI_ARMADA_CE_MAPLE_MDEN ||
        address==KUI_ARMADA_CE_MAPLE_MDST || address==KUI_ARMADA_CE_MAPLE_ISTNRM;
}
static inline enum kui_armada_ce_maple_status kui_armada_ce_maple_read(
    struct kui_armada_ce_maple_workspace *w,struct kui_armada_ce_maple_snapshot *s,
    kui_armada_ce_ram_read32 read32,void *context,uint32_t address,uint32_t *value) {
    if(!kui_armada_ce_maple_read_address(address))
        return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_NODE,address,0,0,0);
    if(w->reads>=KUI_ARMADA_CE_MAPLE_READ_LIMIT)
        return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_BUDGET,address,w->reads,
            KUI_ARMADA_CE_MAPLE_READ_LIMIT,w->nodes);
    ++w->reads;
    if(!read32(context,address,value))
        return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_READ,address,0,0,w->nodes);
    return KUI_ARMADA_CE_MAPLE_OK;
}
static inline enum kui_armada_ce_maple_status kui_armada_ce_maple_repeat(
    struct kui_armada_ce_maple_workspace *w,struct kui_armada_ce_maple_snapshot *s,
    kui_armada_ce_ram_read32 read32,void *context,uint32_t address,uint32_t expected,
    uint32_t detail) {
    uint32_t value=0;
    enum kui_armada_ce_maple_status status=kui_armada_ce_maple_read(w,s,read32,context,address,&value);
    if(status!=KUI_ARMADA_CE_MAPLE_OK) return status;
    return value==expected?KUI_ARMADA_CE_MAPLE_OK:
        kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_CHANGED,address,value,expected,detail);
}
static inline enum kui_armada_ce_maple_status kui_armada_ce_maple_module(
    struct kui_armada_ce_maple_snapshot *s,const struct kui_armada_ce_maple_node *n,
    uint32_t row[5],uint32_t nominal,uint32_t span) {
    /* self,next,attach,base,flags,toc,kind,count,span,sections */
    const uint32_t *f=n->field;
    if(row[0]) return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_MODULE,n->address,
        n->address,row[0],0);
    row[0]=n->address;row[1]=f[3];row[2]=f[8];row[3]=f[7]&0xffffu;row[4]=f[2];
    /* The established CE binder accepts explicit process-slot module bases,
     * never an inferred current-slot alias for a low nominal pointer. */
    if(f[3]<0x02000000u || f[3]>=0x80000000u || (f[3]&0x01ffffffu)!=nominal)
        return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_MODULE,n->address+80u,f[3],nominal,3);
    if(f[8]!=span)
        return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_MODULE,n->address+124u,f[8],span,8);
    if((f[7]&0xffffu)!=3u)
        return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_MODULE,n->address+112u,f[7]&0xffffu,3,7);
    if(!f[2])
        return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_MODULE,n->address+12u,0,1,2);
    /* Retain the pointer only as metadata. No section-table read follows it. */
    if((f[9]&3u) || f[9]<f[3] || f[9]-f[3]>span-3u*28u)
        return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_MODULE,n->address+176u,f[9],f[3],9);
    return KUI_ARMADA_CE_MAPLE_OK;
}
static inline enum kui_armada_ce_maple_status kui_armada_ce_maple_capture(
    struct kui_armada_ce_maple_workspace *w,struct kui_armada_ce_maple_snapshot *s,
    kui_armada_ce_ram_read32 read32,void *context) {
    static const uint32_t context_address[7]={KUI_ARMADA_CE_MAPLE_TTB,KUI_ARMADA_CE_MAPLE_PTEH,
        KUI_ARMADA_CE_MAPLE_CCR,KUI_ARMADA_CE_MAPLE_CURRENT_PROCESS,KUI_ARMADA_CE_MAPLE_HEAP_LOW,
        KUI_ARMADA_CE_MAPLE_HEAP_HIGH,KUI_ARMADA_CE_MAPLE_MODULE_HEAD};
    static const uint32_t offset[KUI_ARMADA_CE_MAPLE_FIELDS]={0u,4u,12u,80u,84u,96u,100u,112u,124u,176u};
    static const uint32_t registers[5]={KUI_ARMADA_CE_MAPLE_MDSTAR,KUI_ARMADA_CE_MAPLE_MDTSEL,
        KUI_ARMADA_CE_MAPLE_MDEN,KUI_ARMADA_CE_MAPLE_MDST,KUI_ARMADA_CE_MAPLE_ISTNRM};
    uint32_t captured[7],p1,node;
    enum kui_armada_ce_maple_status status;
    for(unsigned i=0;i<7u;i++) {
        captured[i]=0;
        status=kui_armada_ce_maple_read(w,s,read32,context,context_address[i],&captured[i]);
        if(i<4u) s->cpu[i]=captured[i];
        if(i==6u) s->cpu[4]=captured[i];
        if(status!=KUI_ARMADA_CE_MAPLE_OK) return status;
    }
    if(!kui_armada_ce_ram_table_p1(captured[0],&p1))
        return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_CONTEXT,context_address[0],captured[0],0x8c010000u,0);
    if((captured[2]&0xa1u)!=1u)
        return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_CONTEXT,context_address[2],captured[2],1,2);
    if(captured[3]<KUI_ARMADA_CE_MAPLE_PROCESS_BASE ||
       captured[3]>KUI_ARMADA_CE_MAPLE_PROCESS_BASE+31u*KUI_ARMADA_CE_MAPLE_PROCESS_STRIDE ||
       (captured[3]-KUI_ARMADA_CE_MAPLE_PROCESS_BASE)%KUI_ARMADA_CE_MAPLE_PROCESS_STRIDE)
        return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_CONTEXT,context_address[3],captured[3],KUI_ARMADA_CE_MAPLE_PROCESS_BASE,3);
    /* Read only the five fixed, passive registers once CPU context is
     * admitted. A later heap/module refusal still retains these raw instants.
     * Zero can also mean unsampled when a prior context/read check failed. */
    for(unsigned i=0;i<5u;i++) {
        status=kui_armada_ce_maple_read(w,s,read32,context,registers[i],&s->registers[i]);
        if(status!=KUI_ARMADA_CE_MAPLE_OK) return status;
    }
    if(!kui_armada_ce_ram_table_p1(captured[4],&p1) || (captured[5]&3u) ||
       captured[5]<=captured[4] || captured[5]>0x8d000000u)
        return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_HEAP,context_address[4],captured[4],captured[5],0);
    for(node=captured[6];node;) {
        if(w->nodes==KUI_ARMADA_CE_MAPLE_MAX_NODES)
            return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_LIMIT,node,w->nodes,KUI_ARMADA_CE_MAPLE_MAX_NODES,0);
        if(!kui_armada_ce_ram_table_p1(node,&p1) || node<captured[4] || node>=captured[5] || captured[5]-node<180u)
            return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_NODE,node,captured[4],captured[5],w->nodes);
        for(unsigned i=0;i<w->nodes;i++) if(w->node[i].address==node)
            return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_CYCLE,node,node,0,w->nodes);
        struct kui_armada_ce_maple_node *n=&w->node[w->nodes++];n->address=node;
        for(unsigned i=0;i<KUI_ARMADA_CE_MAPLE_FIELDS;i++) {
            status=kui_armada_ce_maple_read(w,s,read32,context,node+offset[i],&n->field[i]);
            if(status!=KUI_ARMADA_CE_MAPLE_OK) return status;
        }
        if(n->field[0]!=node)
            return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_NODE,node,n->field[0],node,0);
        /* +96 is a TOC only for kind3. External kind2 candidates are never
         * identified by a name pointer, shape, guessed TOC or callable address. */
        if((n->field[6]&255u)==3u && n->field[5]==KUI_ARMADA_CE_MAPLE_ROM_TOC) {
            status=kui_armada_ce_maple_module(s,n,s->maple,KUI_ARMADA_CE_MAPLE_NOMINAL,KUI_ARMADA_CE_MAPLE_SPAN);
            if(status!=KUI_ARMADA_CE_MAPLE_OK) return status;
        } else if((n->field[6]&255u)==3u && n->field[5]==KUI_ARMADA_CE_MAPLEDEV_ROM_TOC) {
            status=kui_armada_ce_maple_module(s,n,s->mapledev,KUI_ARMADA_CE_MAPLEDEV_NOMINAL,KUI_ARMADA_CE_MAPLEDEV_SPAN);
            if(status!=KUI_ARMADA_CE_MAPLE_OK) return status;
        }
        node=n->field[1];
    }
    /* MMIO values may change naturally; only the kernel/list/context
     * witnesses are repeated. No register is acknowledged, reset or written. */
    for(unsigned i=0;i<w->nodes;i++) for(unsigned j=0;j<KUI_ARMADA_CE_MAPLE_FIELDS;j++) {
        status=kui_armada_ce_maple_repeat(w,s,read32,context,w->node[i].address+offset[j],w->node[i].field[j],j);
        if(status!=KUI_ARMADA_CE_MAPLE_OK) return status;
    }
    for(unsigned i=0;i<7u;i++) {
        status=kui_armada_ce_maple_repeat(w,s,read32,context,context_address[i],captured[i],i);
        if(status!=KUI_ARMADA_CE_MAPLE_OK) return status;
    }
    if(!s->maple[0] || !s->mapledev[0])
        return kui_armada_ce_maple_fault(s,KUI_ARMADA_CE_MAPLE_MISSING,KUI_ARMADA_CE_MAPLE_MODULE_HEAD,
            s->maple[0],s->mapledev[0],w->nodes);
    return KUI_ARMADA_CE_MAPLE_OK;
}
/* Exactly one bounded sample. A capped state is returned untouched, so even
 * reporting after the cap cannot read RAM/MMIO or change the last evidence.
 * The caller supplies static, nonoverlapping workspace and snapshot storage. */
static inline enum kui_armada_ce_maple_status kui_armada_ce_maple_observe(
    struct kui_armada_ce_maple_workspace *w,struct kui_armada_ce_maple_snapshot *s,
    kui_armada_ce_ram_read32 read32,void *context) {
    if(!s || !w || !read32) return KUI_ARMADA_CE_MAPLE_ARGUMENT;
    if(s->counts[0]>=KUI_ARMADA_CE_MAPLE_SAMPLE_LIMIT)
        return (enum kui_armada_ce_maple_status)s->counts[4];
    uintptr_t sa=(uintptr_t)(void *)s,wa=(uintptr_t)(void *)w;
    if((sa>=wa && sa-wa<sizeof(*w)) || (sa<wa && wa-sa<sizeof(*s)))
        return KUI_ARMADA_CE_MAPLE_ARGUMENT;
    ++s->counts[0];
    for(unsigned i=0;i<5u;i++) s->cpu[i]=s->maple[i]=s->mapledev[i]=s->registers[i]=s->fault[i]=0;
    w->reads=w->nodes=0;
    enum kui_armada_ce_maple_status status=kui_armada_ce_maple_capture(w,s,read32,context);
    if(status==KUI_ARMADA_CE_MAPLE_OK || status==KUI_ARMADA_CE_MAPLE_MISSING) ++s->counts[1];
    else ++s->counts[2];
    s->counts[3]=w->reads;s->counts[4]=(uint32_t)status;
    return status;
}
#endif
