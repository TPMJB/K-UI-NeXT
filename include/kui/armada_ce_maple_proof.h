/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_MAPLE_PROOF_H
#define KUI_ARMADA_CE_MAPLE_PROOF_H
#include "kui/armada_ce_maple_observe.h"
#include "kui/armada_ce_maple_mapping.h"
#include "kui/armada_ce_cache_word.h"
#include "kui/armada_ce_maple_headers_types.h"

/* Exact owner source fingerprint, independently reproducible from the ROM.
 * Only identity constants are distributed, never private instruction bytes. */
#define KUI_ARMADA_CE_MAPLE_PROOF_CODE_RVA UINT32_C(0x5b0c)
#define KUI_ARMADA_CE_MAPLE_PROOF_CODE_BYTES 32u
#define KUI_ARMADA_CE_MAPLE_PROOF_CODE_CRC UINT32_C(0x7bd7ff55)
#if KUI_ARMADA_CE_MAPLE_HOOK
#include "kui/armada_ce_maple_hook.h"
#endif
#if KUI_ARMADA_CE_MAPLE_FENCE
#define KUI_ARMADA_CE_MAPLE_PROOF_EXPECTED_CRC KUI_ARMADA_CE_MAPLE_PROOF_CODE_CRC
#elif KUI_ARMADA_CE_MAPLE_HOOK
#define KUI_ARMADA_CE_MAPLE_PROOF_EXPECTED_CRC KUI_ARMADA_CE_MAPLE_HOOK_PATCHED_CRC
#else
#define KUI_ARMADA_CE_MAPLE_PROOF_EXPECTED_CRC KUI_ARMADA_CE_MAPLE_PROOF_CODE_CRC
#endif
#define KUI_ARMADA_CE_MAPLE_PROOF_ROOT_RVA UINT32_C(0x72b8)
#define KUI_ARMADA_CE_MAPLE_PROOF_READ_LIMIT 2048u
#define KUI_ARMADA_CE_MAPLE_PROOF_WORDS 10u
enum kui_armada_ce_maple_proof_status {
    KUI_ARMADA_CE_MAPLE_PROOF_DORMANT,
    KUI_ARMADA_CE_MAPLE_PROOF_OK,
    KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT,
    KUI_ARMADA_CE_MAPLE_PROOF_METADATA,
    KUI_ARMADA_CE_MAPLE_PROOF_PROCESS,
    KUI_ARMADA_CE_MAPLE_PROOF_MAPPING,
    KUI_ARMADA_CE_MAPLE_PROOF_CACHE,
    KUI_ARMADA_CE_MAPLE_PROOF_READ,
    KUI_ARMADA_CE_MAPLE_PROOF_BUDGET,
    KUI_ARMADA_CE_MAPLE_PROOF_CHANGED,
    KUI_ARMADA_CE_MAPLE_PROOF_CODE
};
/* A stable observation only: no code execution, ownership or hook lease.
 * counts: status, all underlying reads, expected CRC, actual CRC, completed
 *         steps mask (1 metadata,2 process,4 code,8 owner root,16 canonical root).
 * code: VA, raw slot value, raw block value, raw page word, resolved P1 word.
 * owner: current PROCESS, explicit slot, handle, MAPLE attach mask, owner root VA.
 * root: owner P1 word, owner page word, owner root value, canonical P1 word,
 *       canonical root value. Root values are never followed or normalized.
 * context: code/owner-root/canonical-root mapping statuses (UINT32_MAX means
 *          unattempted), first code word, canonical root VA.
 * fault: proof reason, address, observed, expected, detail.
 * Raw mapping fields survive refusal. counts[0] alone grants proof success. */
struct kui_armada_ce_maple_proof_snapshot {
    uint32_t counts[5],code[5],owner[5],root[5],context[5],fault[5];
};
_Static_assert(sizeof(struct kui_armada_ce_maple_proof_snapshot)==120u,"CE Maple proof rows ABI");
struct kui_armada_ce_maple_proof_workspace {
    /* State uses only the guard's IO/context fields and CPU witnesses. Its
     * mapping array is deliberately reused for retained header observations;
     * the separate, original proof workspace remains immutable. */
    union {
        struct kui_armada_ce_maple_mapping mapping[KUI_ARMADA_CE_MAPLE_PROOF_WORDS];
        struct kui_armada_ce_maple_headers_snapshot headers;
    };
    uint32_t word[KUI_ARMADA_CE_MAPLE_PROOF_WORDS],cpu[7],process[4];
    kui_armada_ce_ram_read32 read32,read_cache;
    void *context;
    struct kui_armada_ce_maple_proof_snapshot *out;
    uint32_t reads,failed;
};
static inline enum kui_armada_ce_maple_proof_status kui_armada_ce_maple_proof_fault(
    struct kui_armada_ce_maple_proof_workspace *w,enum kui_armada_ce_maple_proof_status status,
    uint32_t address,uint32_t observed,uint32_t expected,uint32_t detail) {
    uint32_t *f=w->out->fault;
    f[0]=(uint32_t)status;f[1]=address;f[2]=observed;f[3]=expected;f[4]=detail;
    w->failed=(uint32_t)status;return status;
}
static inline bool kui_armada_ce_maple_proof_count(struct kui_armada_ce_maple_proof_workspace *w,
    uint32_t address) {
    if(w->reads>=KUI_ARMADA_CE_MAPLE_PROOF_READ_LIMIT) {
        (void)kui_armada_ce_maple_proof_fault(w,KUI_ARMADA_CE_MAPLE_PROOF_BUDGET,address,w->reads,
            KUI_ARMADA_CE_MAPLE_PROOF_READ_LIMIT,0);return false;
    }
    ++w->reads;return true;
}
static inline bool kui_armada_ce_maple_proof_read(void *context,uint32_t address,uint32_t *value) {
    struct kui_armada_ce_maple_proof_workspace *w=context;uint32_t p1;
    if(!value || (!kui_armada_ce_ram_table_p1(address,&p1) && address!=KUI_ARMADA_CE_MAPLE_TTB &&
       address!=KUI_ARMADA_CE_MAPLE_PTEH && address!=KUI_ARMADA_CE_MAPLE_CCR)) {
        (void)kui_armada_ce_maple_proof_fault(w,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,address,0,0,0);return false;
    }
    if(!kui_armada_ce_maple_proof_count(w,address)) return false;
    if(!w->read32(w->context,address,value)) {
        (void)kui_armada_ce_maple_proof_fault(w,KUI_ARMADA_CE_MAPLE_PROOF_READ,address,0,0,0);return false;
    }
    return true;
}
static inline bool kui_armada_ce_maple_proof_cache_read(void *context,uint32_t address,uint32_t *value) {
    struct kui_armada_ce_maple_proof_workspace *w=context;uint32_t p1;
    const bool tag=(address&UINT32_C(0xffffc01f))==UINT32_C(0xf4000000);
    const bool data=(address&UINT32_C(0xffffc003))==UINT32_C(0xf5000000);
    const bool ram=(address&UINT32_C(0xe0000000))==UINT32_C(0xa0000000) &&
        kui_armada_ce_ram_p1(address,&p1);
    if(!value || (!tag && !data && !ram)) {
        (void)kui_armada_ce_maple_proof_fault(w,KUI_ARMADA_CE_MAPLE_PROOF_CACHE,address,0,0,0);return false;
    }
    if(!kui_armada_ce_maple_proof_count(w,address)) return false;
    if(!w->read_cache(w->context,address,value)) {
        (void)kui_armada_ce_maple_proof_fault(w,KUI_ARMADA_CE_MAPLE_PROOF_READ,address,0,0,0);return false;
    }
    return true;
}
static inline bool kui_armada_ce_maple_proof_repeat(struct kui_armada_ce_maple_proof_workspace *w,
    uint32_t address,uint32_t expected,uint32_t detail) {
    uint32_t value;
    if(!kui_armada_ce_maple_proof_read(w,address,&value)) return false;
    if(value==expected) return true;
    (void)kui_armada_ce_maple_proof_fault(w,KUI_ARMADA_CE_MAPLE_PROOF_CHANGED,address,value,expected,detail);
    return false;
}
static inline bool kui_armada_ce_maple_proof_list(struct kui_armada_ce_maple_proof_workspace *w,
    const struct kui_armada_ce_maple_workspace *old) {
    static const uint32_t offset[10]={0,4,12,80,84,96,100,112,124,176};
    for(unsigned i=0;i<old->nodes;i++) for(unsigned j=0;j<10u;j++)
        if(!kui_armada_ce_maple_proof_repeat(w,old->node[i].address+offset[j],old->node[i].field[j],j)) return false;
    return true;
}
static inline uint32_t kui_armada_ce_maple_proof_crc_word(uint32_t crc,uint32_t word) {
    for(unsigned i=0;i<4u;i++) {
        crc^=(word>>(i*8u))&255u;
        for(unsigned bit=0;bit<8u;bit++) crc=(crc>>1)^((0u-(crc&1u))&UINT32_C(0xedb88320));
    }
    return crc;
}
static inline bool kui_armada_ce_maple_proof_value(struct kui_armada_ce_maple_proof_workspace *w,
    const struct kui_armada_ce_maple_mapping *m,uint32_t *out) {
    enum kui_armada_ce_ram_bind_status status=kui_armada_ce_cache_word(m->virtual_address,m->physical_address,
        m->page_word,w->cpu[2],kui_armada_ce_maple_proof_cache_read,w,out);
    if(status==KUI_ARMADA_CE_RAM_BIND_OK) return true;
    if(!w->failed) (void)kui_armada_ce_maple_proof_fault(w,status==KUI_ARMADA_CE_RAM_BIND_CHANGED?
        KUI_ARMADA_CE_MAPLE_PROOF_CHANGED:KUI_ARMADA_CE_MAPLE_PROOF_CACHE,m->virtual_address,
        (uint32_t)status,KUI_ARMADA_CE_RAM_BIND_OK,m->page_word);
    return false;
}
static inline bool kui_armada_ce_maple_proof_word(struct kui_armada_ce_maple_proof_workspace *w,
    unsigned index,uint32_t va,unsigned mapping_status_column) {
    struct kui_armada_ce_maple_mapping *m=&w->mapping[index];
    enum kui_armada_ce_ram_bind_status status=kui_armada_ce_maple_resolve(w->cpu[0],va,
        kui_armada_ce_maple_proof_read,w,m);
    if(mapping_status_column<3u) w->out->context[mapping_status_column]=(uint32_t)status;
    if(index==0u) {
        uint32_t *r=w->out->code;r[0]=m->virtual_address;r[1]=m->slot_value;
        r[2]=m->block_value;r[3]=m->page_word;r[4]=m->physical_address;
    }
    if(status!=KUI_ARMADA_CE_RAM_BIND_OK) {
        if(!w->failed) (void)kui_armada_ce_maple_proof_fault(w,status==KUI_ARMADA_CE_RAM_BIND_CHANGED?
            KUI_ARMADA_CE_MAPLE_PROOF_CHANGED:KUI_ARMADA_CE_MAPLE_PROOF_MAPPING,va,(uint32_t)status,0,index);
        return false;
    }
    return kui_armada_ce_maple_proof_value(w,m,&w->word[index]);
}
static inline bool kui_armada_ce_maple_proof_body(struct kui_armada_ce_maple_proof_workspace *w,
    const struct kui_armada_ce_maple_snapshot *s,const struct kui_armada_ce_maple_workspace *old) {
    static const uint32_t address[7]={KUI_ARMADA_CE_MAPLE_TTB,KUI_ARMADA_CE_MAPLE_PTEH,
        KUI_ARMADA_CE_MAPLE_CCR,KUI_ARMADA_CE_MAPLE_CURRENT_PROCESS,KUI_ARMADA_CE_MAPLE_HEAP_LOW,
        KUI_ARMADA_CE_MAPLE_HEAP_HIGH,KUI_ARMADA_CE_MAPLE_MODULE_HEAD};
    uint32_t p1;
    if((s->counts[4]!=KUI_ARMADA_CE_MAPLE_OK && s->counts[4]!=KUI_ARMADA_CE_MAPLE_MISSING) ||
       !s->counts[0] || !s->counts[1] || !s->maple[0] || !old->nodes || old->nodes>KUI_ARMADA_CE_MAPLE_MAX_NODES ||
       old->reads!=s->counts[3] || old->reads>KUI_ARMADA_CE_MAPLE_READ_LIMIT) {
        (void)kui_armada_ce_maple_proof_fault(w,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,0,s->counts[4],
            KUI_ARMADA_CE_MAPLE_OK,old->nodes);return false;
    }
    for(unsigned i=0;i<7u;i++) {
        if(!kui_armada_ce_maple_proof_read(w,address[i],&w->cpu[i])) return false;
        if((i<4u && w->cpu[i]!=s->cpu[i]) || (i==6u && w->cpu[i]!=s->cpu[4])) {
            (void)kui_armada_ce_maple_proof_fault(w,KUI_ARMADA_CE_MAPLE_PROOF_CHANGED,address[i],w->cpu[i],
                i==6u?s->cpu[4]:s->cpu[i],i);return false;
        }
    }
    if(!kui_armada_ce_ram_table_p1(w->cpu[0],&p1) || (w->cpu[2]&0xa1u)!=1u ||
       !kui_armada_ce_ram_table_p1(w->cpu[4],&p1) || (w->cpu[5]&3u) ||
       w->cpu[5]<=w->cpu[4] || w->cpu[5]>0x8d000000u || w->cpu[6]!=old->node[0].address) {
        (void)kui_armada_ce_maple_proof_fault(w,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,address[4],w->cpu[4],w->cpu[5],0);return false;
    }
    const struct kui_armada_ce_maple_node *module=NULL;
    for(unsigned i=0;i<old->nodes;i++) {
        const struct kui_armada_ce_maple_node *n=&old->node[i];uint32_t node=n->address;
        if(!kui_armada_ce_ram_table_p1(node,&p1) || node<w->cpu[4] || node>=w->cpu[5] || w->cpu[5]-node<180u ||
           n->field[0]!=node || n->field[1]!=(i+1u<old->nodes?old->node[i+1u].address:0u)) {
            (void)kui_armada_ce_maple_proof_fault(w,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,node,n->field[0],node,i);return false;
        }
        if(node==s->maple[0]) module=n;
    }
    if(!module || (module->field[6]&255u)!=3u || module->field[5]!=KUI_ARMADA_CE_MAPLE_ROM_TOC ||
       module->field[3]!=s->maple[1] || module->field[8]!=s->maple[2] ||
       (module->field[7]&0xffffu)!=s->maple[3] || module->field[2]!=s->maple[4] ||
       s->maple[1]<0x02000000u || s->maple[1]>=0x80000000u ||
       (s->maple[1]&0x01ffffffu)!=KUI_ARMADA_CE_MAPLE_NOMINAL || s->maple[2]!=KUI_ARMADA_CE_MAPLE_SPAN ||
       s->maple[3]!=3u || !s->maple[4] || (module->field[9]&3u) || module->field[9]<s->maple[1] ||
       module->field[9]-s->maple[1]>KUI_ARMADA_CE_MAPLE_SPAN-3u*28u) {
        (void)kui_armada_ce_maple_proof_fault(w,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,s->maple[0],s->maple[1],
            KUI_ARMADA_CE_MAPLE_NOMINAL,0);return false;
    }
    if(!kui_armada_ce_maple_proof_list(w,old)) return false;
    w->out->counts[4]|=1u;
    uint32_t process=w->cpu[3];
    if(process<KUI_ARMADA_CE_MAPLE_PROCESS_BASE ||
       process>KUI_ARMADA_CE_MAPLE_PROCESS_BASE+31u*KUI_ARMADA_CE_MAPLE_PROCESS_STRIDE ||
       (process-KUI_ARMADA_CE_MAPLE_PROCESS_BASE)%KUI_ARMADA_CE_MAPLE_PROCESS_STRIDE) {
        (void)kui_armada_ce_maple_proof_fault(w,KUI_ARMADA_CE_MAPLE_PROOF_PROCESS,address[3],process,KUI_ARMADA_CE_MAPLE_PROCESS_BASE,0);return false;
    }
    uint32_t index=(process-KUI_ARMADA_CE_MAPLE_PROCESS_BASE)/KUI_ARMADA_CE_MAPLE_PROCESS_STRIDE;
    uint32_t ref_at=s->maple[0]+16u+4u*(index/2u);
    uint32_t process_address[4]={process,process+12u,process+8u,ref_at};
    for(unsigned i=0;i<4u;i++) if(!kui_armada_ce_maple_proof_read(w,process_address[i],&w->process[i])) return false;
    uint32_t slot=(index+1u)<<25;
    uint32_t *owner=w->out->owner;owner[0]=process;owner[1]=w->process[1];owner[2]=w->process[2];owner[3]=s->maple[4];
    if((w->process[0]&255u)!=index || w->process[1]!=slot || !w->process[2] || w->process[2]==UINT32_MAX ||
       !(s->maple[4]&(UINT32_C(1)<<index)) || !((w->process[3]>>(16u*(index&1u)))&0xffffu)) {
        (void)kui_armada_ce_maple_proof_fault(w,KUI_ARMADA_CE_MAPLE_PROOF_PROCESS,process,w->process[1],slot,index);return false;
    }
    owner[4]=slot|(KUI_ARMADA_CE_MAPLE_NOMINAL+KUI_ARMADA_CE_MAPLE_PROOF_ROOT_RVA);
    w->out->context[4]=s->maple[1]+KUI_ARMADA_CE_MAPLE_PROOF_ROOT_RVA;
    w->out->counts[4]|=2u;
    uint32_t crc=UINT32_MAX;
    for(unsigned i=0;i<8u;i++) {
        if(!kui_armada_ce_maple_proof_word(w,i,s->maple[1]+KUI_ARMADA_CE_MAPLE_PROOF_CODE_RVA+4u*i,i?3u:0u)) return false;
        if(!i) w->out->context[3]=w->word[i];
        crc=kui_armada_ce_maple_proof_crc_word(crc,w->word[i]);
    }
    w->out->counts[3]=crc^UINT32_MAX;
    if(w->out->counts[3]!=KUI_ARMADA_CE_MAPLE_PROOF_EXPECTED_CRC) {
        (void)kui_armada_ce_maple_proof_fault(w,KUI_ARMADA_CE_MAPLE_PROOF_CODE,w->out->code[0],w->out->counts[3],
            KUI_ARMADA_CE_MAPLE_PROOF_EXPECTED_CRC,KUI_ARMADA_CE_MAPLE_PROOF_CODE_BYTES);return false;
    }
    w->out->counts[4]|=4u;
    if(!kui_armada_ce_maple_proof_word(w,8u,owner[4],1u)) return false;
    w->out->root[0]=w->mapping[8].physical_address;w->out->root[1]=w->mapping[8].page_word;w->out->root[2]=w->word[8];
    w->out->counts[4]|=8u;
    if(!kui_armada_ce_maple_proof_word(w,9u,w->out->context[4],2u)) return false;
    w->out->root[3]=w->mapping[9].physical_address;w->out->root[4]=w->word[9];w->out->counts[4]|=16u;
    for(unsigned i=0;i<KUI_ARMADA_CE_MAPLE_PROOF_WORDS;i++) {
        struct kui_armada_ce_maple_mapping again;
        enum kui_armada_ce_ram_bind_status status=kui_armada_ce_maple_resolve(w->cpu[0],w->mapping[i].virtual_address,
            kui_armada_ce_maple_proof_read,w,&again);
        if(status!=KUI_ARMADA_CE_RAM_BIND_OK) {
            if(!w->failed) (void)kui_armada_ce_maple_proof_fault(w,KUI_ARMADA_CE_MAPLE_PROOF_CHANGED,
                w->mapping[i].virtual_address,(uint32_t)status,0,i);return false;
        }
        const uint32_t *a=(const uint32_t *)(const void *)&again,*b=(const uint32_t *)(const void *)&w->mapping[i];
        for(unsigned j=0;j<8u;j++) if(a[j]!=b[j]) {
            (void)kui_armada_ce_maple_proof_fault(w,KUI_ARMADA_CE_MAPLE_PROOF_CHANGED,
                w->mapping[i].virtual_address,a[j],b[j],j);return false;
        }
        uint32_t value;
        if(!kui_armada_ce_maple_proof_value(w,&again,&value)) return false;
        if(value!=w->word[i]) {
            (void)kui_armada_ce_maple_proof_fault(w,KUI_ARMADA_CE_MAPLE_PROOF_CHANGED,
                again.virtual_address,value,w->word[i],i);return false;
        }
    }
    for(unsigned i=0;i<4u;i++) if(!kui_armada_ce_maple_proof_repeat(w,process_address[i],w->process[i],i)) return false;
    if(!kui_armada_ce_maple_proof_list(w,old)) return false;
    for(unsigned i=0;i<7u;i++) if(!kui_armada_ce_maple_proof_repeat(w,address[i],w->cpu[i],i)) return false;
    return true;
}
static inline bool kui_armada_ce_maple_proof_overlap(uintptr_t a,size_t as,uintptr_t b,size_t bs) {
    return a>=b?a-b<bs:b-a<as;
}
/* Caller provides separate static workspaces. Runtime owns the existing GD
 * trigger/cap and report-only gating. This helper never alters the collector,
 * follows a root value, calls CE, dereferences a CE VA or writes guest/hardware.
 * read_cache must satisfy cache_word's P2 array-instruction/IRQ prerequisites. */
static inline enum kui_armada_ce_maple_proof_status kui_armada_ce_maple_proof(
    const struct kui_armada_ce_maple_snapshot *s,const struct kui_armada_ce_maple_workspace *old,
    kui_armada_ce_ram_read32 read32,kui_armada_ce_ram_read32 read_cache,void *context,
    struct kui_armada_ce_maple_proof_workspace *w,struct kui_armada_ce_maple_proof_snapshot *out) {
    if(!s || !old || !read32 || !read_cache || !w || !out) return KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT;
    uintptr_t a=(uintptr_t)(void *)w,b=(uintptr_t)(void *)out;
    if(kui_armada_ce_maple_proof_overlap(a,sizeof(*w),b,sizeof(*out)) ||
       kui_armada_ce_maple_proof_overlap(a,sizeof(*w),(uintptr_t)(const void *)s,sizeof(*s)) ||
       kui_armada_ce_maple_proof_overlap(a,sizeof(*w),(uintptr_t)(const void *)old,sizeof(*old)) ||
       kui_armada_ce_maple_proof_overlap(b,sizeof(*out),(uintptr_t)(const void *)s,sizeof(*s)) ||
       kui_armada_ce_maple_proof_overlap(b,sizeof(*out),(uintptr_t)(const void *)old,sizeof(*old)))
        return KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT;
    kui_armada_ce_maple_zero(out,sizeof(*out));
    w->read32=read32;w->read_cache=read_cache;w->context=context;w->out=out;w->reads=w->failed=0;
    out->counts[2]=KUI_ARMADA_CE_MAPLE_PROOF_EXPECTED_CRC;
    for(unsigned i=0;i<3u;i++) out->context[i]=UINT32_MAX;
    bool valid=kui_armada_ce_maple_proof_body(w,s,old);
    out->counts[0]=valid?KUI_ARMADA_CE_MAPLE_PROOF_OK:w->failed;out->counts[1]=w->reads;
    return (enum kui_armada_ce_maple_proof_status)out->counts[0];
}
#endif
