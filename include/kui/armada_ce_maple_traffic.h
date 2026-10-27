/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_MAPLE_TRAFFIC_H
#define KUI_ARMADA_CE_MAPLE_TRAFFIC_H
#include "kui/armada_ce_maple_traffic_types.h"
#include "kui/armada_ce_maple_mapping.h"
#include "kui/armada_ce_maple_owner_pointer.h"
#include "kui/armada_ce_cache_word.h"
#include "kui/armada_ce_cache_line.h"
#include "kui/armada_ce_vmu_layout.h"
/* Pure, bounded sampler for the actual live completion argument. read_p1 is
 * also used for the fixed TTB/CCR CPU registers. read_cache executes pinned
 * read-only cache-array/P2 loads in P2, with interrupts masked and the required
 * post-load delay. Extension words are sampled through the admitted physical
 * mapping and cache tags, never through a CE virtual load or TLB refill.
 * Pool headers use admitted uncached P2 ranges.
 * Repeated values prove sampled consistency, never allocation/DMA ownership.
 * The runtime must serialize visits and place this scratch/C stack in its own
 * reservation; the source driver's stack and DMA buffers are never scratch. */
enum kui_armada_ce_maple_traffic_status {
    KUI_ARMADA_CE_MAPLE_TRAFFIC_OK,
    KUI_ARMADA_CE_MAPLE_TRAFFIC_ARGUMENT,
    KUI_ARMADA_CE_MAPLE_TRAFFIC_CONTEXT,
    KUI_ARMADA_CE_MAPLE_TRAFFIC_MAPPING,
    KUI_ARMADA_CE_MAPLE_TRAFFIC_METADATA,
    KUI_ARMADA_CE_MAPLE_TRAFFIC_READ,
    KUI_ARMADA_CE_MAPLE_TRAFFIC_CHANGED,
    KUI_ARMADA_CE_MAPLE_TRAFFIC_BUDGET,
    KUI_ARMADA_CE_MAPLE_TRAFFIC_CACHE
};
struct kui_armada_ce_maple_traffic_scratch {
    struct kui_armada_ce_maple_mapping map[4];
    uint32_t word[10],row[KUI_ARMADA_CE_MAPLE_TRAFFIC_RECORD_LIMIT][5];
    uint32_t reads,error[5];
};
_Static_assert(sizeof(struct kui_armada_ce_maple_traffic_scratch)==512u,"bounded traffic scratch");
struct kui_armada_ce_maple_traffic_io {
    kui_armada_ce_ram_read32 p1,cache,p2;
    void *context;
    struct kui_armada_ce_maple_traffic_scratch *w;
};
static inline uint32_t kui_armada_ce_maple_traffic_inc(uint32_t n) {return n==UINT32_MAX?n:n+1u;}
static inline bool kui_armada_ce_maple_traffic_fail(struct kui_armada_ce_maple_traffic_scratch *w,
    unsigned why,uint32_t at,uint32_t actual,uint32_t expected,uint32_t detail) {
    if(!w->error[0]) {w->error[0]=why;w->error[1]=at;w->error[2]=actual;w->error[3]=expected;w->error[4]=detail;}
    return false;
}
static inline bool kui_armada_ce_maple_traffic_owned(uint32_t p1,uint32_t bytes) {
#if KUI_ARMADA_CE_MAPLE_A2
    return p1<KUI_ARMADA_CE_VMU_RESERVE_END && p1+bytes>KUI_ARMADA_CE_VMU_RESERVE_BEGIN;
#elif KUI_ARMADA_CE_MAPLE_FENCE
    return p1<UINT32_C(0x8cfd0000) && p1+bytes>UINT32_C(0x8cfcd000);
#else
    return p1<UINT32_C(0x8cfd0000) && p1+bytes>UINT32_C(0x8cfce000);
#endif
}
static inline bool kui_armada_ce_maple_traffic_read(struct kui_armada_ce_maple_traffic_io *io,
    kui_armada_ce_ram_read32 read,uint32_t at,uint32_t *value) {
    if(io->w->reads>=KUI_ARMADA_CE_MAPLE_TRAFFIC_READ_LIMIT)
        return kui_armada_ce_maple_traffic_fail(io->w,KUI_ARMADA_CE_MAPLE_TRAFFIC_BUDGET,at,io->w->reads,512u,0);
    ++io->w->reads;
    if(read(io->context,at,value)) return true;
    return kui_armada_ce_maple_traffic_fail(io->w,KUI_ARMADA_CE_MAPLE_TRAFFIC_READ,at,0,0,0);
}
static inline bool kui_armada_ce_maple_traffic_table(void *context,uint32_t at,uint32_t *value) {
    struct kui_armada_ce_maple_traffic_io *io=context;uint32_t p1;
    if((!kui_armada_ce_ram_table_p1(at,&p1) || kui_armada_ce_maple_traffic_owned(p1,4u)) &&
       at!=UINT32_C(0xff000008) && at!=UINT32_C(0xff00001c))
        return kui_armada_ce_maple_traffic_fail(io->w,KUI_ARMADA_CE_MAPLE_TRAFFIC_MAPPING,at,0,0,0);
    return kui_armada_ce_maple_traffic_read(io,io->p1,at,value);
}
static inline bool kui_armada_ce_maple_traffic_map(struct kui_armada_ce_maple_traffic_io *io,
    uint32_t ttb,uint32_t va,struct kui_armada_ce_maple_mapping *map) {
    unsigned status=kui_armada_ce_maple_resolve(ttb,va,kui_armada_ce_maple_traffic_table,io,map);
    if(status || kui_armada_ce_maple_traffic_owned(map->physical_address,4u))
        return kui_armada_ce_maple_traffic_fail(io->w,KUI_ARMADA_CE_MAPLE_TRAFFIC_MAPPING,va,status,0,map->page_word);
    return true;
}
static inline bool kui_armada_ce_maple_traffic_repeat_map(struct kui_armada_ce_maple_traffic_io *io,
    uint32_t ttb,const struct kui_armada_ce_maple_mapping *before) {
    struct kui_armada_ce_maple_mapping now;
    if(!kui_armada_ce_maple_traffic_map(io,ttb,before->virtual_address,&now)) return false;
    const uint32_t *a=(const uint32_t *)(const void *)before,*b=(const uint32_t *)(const void *)&now;
    for(unsigned i=0;i<8u;i++) if(a[i]!=b[i])
        return kui_armada_ce_maple_traffic_fail(io->w,KUI_ARMADA_CE_MAPLE_TRAFFIC_CHANGED,before->virtual_address,b[i],a[i],i);
    return true;
}
static inline bool kui_armada_ce_maple_traffic_cache(void *context,uint32_t at,uint32_t *value) {
    struct kui_armada_ce_maple_traffic_io *io=context;uint32_t p1;
    bool tag=(at&UINT32_C(0xffffc01f))==UINT32_C(0xf4000000);
    bool data=(at&UINT32_C(0xffffc003))==UINT32_C(0xf5000000);
    bool ram=(at&UINT32_C(0xe0000000))==UINT32_C(0xa0000000) &&
        kui_armada_ce_ram_p1(at,&p1) && !kui_armada_ce_maple_traffic_owned(p1,4u);
    if(!tag && !data && !ram)
        return kui_armada_ce_maple_traffic_fail(io->w,KUI_ARMADA_CE_MAPLE_TRAFFIC_CACHE,at,0,0,0);
    return kui_armada_ce_maple_traffic_read(io,io->cache,at,value);
}
static inline bool kui_armada_ce_maple_traffic_meta(struct kui_armada_ce_maple_traffic_io *io,
    uint32_t va,uint32_t ccr,bool repeat) {
    static const uint32_t offset[10]={0,0x84,0x88,0x8c,0x90,0x100,0xc0,0xc4,0xcc,0xd8};
    const struct kui_armada_ce_maple_mapping *map=&io->w->map[0];
    unsigned visited=0;
    for(unsigned i=0;i<10u;i++) if(!(visited&(1u<<i))) {
        uint32_t line=(va+offset[i])&~31u,mask=0,group=0,words[8];
        for(unsigned j=i;j<10u;j++) if(((va+offset[j])&~31u)==line) {
            mask|=1u<<(((va+offset[j])&31u)/4u);group|=1u<<j;
        }
        /* This source's ten fields normally occupy four cache lines. A
         * single masked snapshot shares their tag scans while preserving
         * repeated data checks. Both metadata passes remain independent;
         * no cache source or value is retained across a table walk. */
        unsigned status=kui_armada_ce_cache_line(line,(map->physical_address+offset[i])&~31u,
            map->page_word,ccr,mask,kui_armada_ce_maple_traffic_cache,io,words);
        if(status) return kui_armada_ce_maple_traffic_fail(io->w,
            status==KUI_ARMADA_CE_RAM_BIND_CHANGED?KUI_ARMADA_CE_MAPLE_TRAFFIC_CHANGED:KUI_ARMADA_CE_MAPLE_TRAFFIC_CACHE,
            va+offset[i],status,KUI_ARMADA_CE_RAM_BIND_OK,i);
        for(unsigned j=i;j<10u;j++) if(group&(1u<<j)) {
            uint32_t v=words[((va+offset[j])&31u)/4u];
            if(repeat && v!=io->w->word[j])
                return kui_armada_ce_maple_traffic_fail(io->w,KUI_ARMADA_CE_MAPLE_TRAFFIC_CHANGED,
                    va+offset[j],v,io->w->word[j],j);
            if(!repeat) io->w->word[j]=v;
        }
        visited|=group;
    }
    return true;
}
static inline bool kui_armada_ce_maple_traffic_witness(struct kui_armada_ce_maple_traffic_io *io,
    uint32_t ttb,uint32_t process,uint32_t ccr,uint32_t slot,uint32_t expected_root,bool final) {
    static const uint32_t at[3]={0xff000008u,0x8c145890u,0xff00001cu};
    const uint32_t expected[3]={ttb,process,ccr};uint32_t root,owner;
    for(unsigned i=0;i<3u;i++) {uint32_t v;
        if(!kui_armada_ce_maple_traffic_table(io,at[i],&v)) return false;
        if(v!=expected[i]) return kui_armada_ce_maple_traffic_fail(io->w,KUI_ARMADA_CE_MAPLE_TRAFFIC_CHANGED,at[i],v,expected[i],i);
    }
    if(!kui_armada_ce_maple_traffic_table(io,ttb,&root) ||
       !kui_armada_ce_maple_traffic_table(io,ttb+4u*(slot>>25),&owner)) return false;
    if(root!=owner || (final && root!=expected_root))
        return kui_armada_ce_maple_traffic_fail(io->w,KUI_ARMADA_CE_MAPLE_TRAFFIC_CONTEXT,ttb,root,final?expected_root:owner,slot);
    return true;
}
static inline bool kui_armada_ce_maple_traffic_body(struct kui_armada_ce_maple_traffic_io *io,
    uint32_t raw,uint32_t ttb,uint32_t process,uint32_t ccr,unsigned *records) {
    struct kui_armada_ce_maple_traffic_scratch *w=io->w;
    uint32_t p1,ext,slot,root,txva,rxva;
    if(process<0x8c14e6e0u || process>0x8c14e6e0u+31u*0x9cu ||
       (process-0x8c14e6e0u)%0x9cu || (ccr&0xa1u)!=1u ||
       !kui_armada_ce_ram_table_p1(ttb,&p1) || kui_armada_ce_maple_traffic_owned(ttb,128u))
        return kui_armada_ce_maple_traffic_fail(w,KUI_ARMADA_CE_MAPLE_TRAFFIC_CONTEXT,process,ttb,ccr,0);
    slot=(((process-0x8c14e6e0u)/0x9cu)+1u)<<25;
    if(!kui_armada_ce_maple_owner_pointer(raw,slot,0x198u,&ext) || (ext&0xfffu)>0xe68u)
        return kui_armada_ce_maple_traffic_fail(w,KUI_ARMADA_CE_MAPLE_TRAFFIC_ARGUMENT,raw,slot,0x198u,0);
    if(!kui_armada_ce_maple_traffic_witness(io,ttb,process,ccr,slot,0,false) ||
       !kui_armada_ce_maple_traffic_table(io,ttb,&root) ||
       !kui_armada_ce_maple_traffic_map(io,ttb,ext,&w->map[0])) return false;
    if(kui_armada_ce_maple_traffic_owned(w->map[0].physical_address,0x198u))
        return kui_armada_ce_maple_traffic_fail(w,KUI_ARMADA_CE_MAPLE_TRAFFIC_MAPPING,ext,w->map[0].physical_address,0,0x198u);
    if(!kui_armada_ce_maple_traffic_meta(io,ext,ccr,false)) return false;
    uint32_t tx=w->word[1],rx=w->word[6];
    if(w->word[0] || tx<0x0c010000u || tx>0x0cfff000u || (tx&0xfffu) ||
       rx<0x0c010000u || rx>0x0cffe000u || (rx&0xfffu) || w->word[7] ||
       kui_armada_ce_maple_traffic_owned(tx|0x80000000u,0x1000u) ||
       kui_armada_ce_maple_traffic_owned(rx|0x80000000u,0x2000u) ||
       (tx<rx+0x2000u && rx<tx+0x1000u) ||
       !kui_armada_ce_maple_owner_pointer(w->word[2],slot,0x1000u,&txva) || (txva&0xfffu) ||
       !kui_armada_ce_maple_owner_pointer(w->word[8],slot,0x2000u,&rxva) || (rxva&0xfffu) ||
       w->word[9]+w->word[8]!=rx)
        return kui_armada_ce_maple_traffic_fail(w,KUI_ARMADA_CE_MAPLE_TRAFFIC_METADATA,ext,tx,rx,1);
    p1=w->map[0].physical_address&0x1fffffffu;
    if((p1<tx+0x1000u && tx<p1+0x198u) || (p1<rx+0x2000u && rx<p1+0x198u) ||
       (ext<txva+0x1000u && txva<ext+0x198u) ||
       (ext<rxva+0x2000u && rxva<ext+0x198u) || (txva<rxva+0x2000u && rxva<txva+0x1000u))
        return kui_armada_ce_maple_traffic_fail(w,KUI_ARMADA_CE_MAPLE_TRAFFIC_METADATA,ext,p1,tx,2);
    for(unsigned i=0;i<3u;i++) {
        uint32_t va=i?rxva+(i-1u)*0x1000u:txva;
        uint32_t bus=i?rx+(i-1u)*0x1000u:tx;
        if(!kui_armada_ce_maple_traffic_map(io,ttb,va,&w->map[i+1u])) return false;
        if((w->map[i+1u].page_word&8u) || w->map[i+1u].physical_address!=(bus|0x80000000u))
            return kui_armada_ce_maple_traffic_fail(w,KUI_ARMADA_CE_MAPLE_TRAFFIC_MAPPING,va,w->map[i+1u].physical_address,bus|0x80000000u,i);
    }
    const uint32_t last=w->word[3]-w->word[2],end=w->word[4]-w->word[2];
    if(!w->word[3] || !w->word[4] || (last&3u) || (end&3u) || end<12u || end>4096u || last>end-12u)
        return kui_armada_ce_maple_traffic_fail(w,KUI_ARMADA_CE_MAPLE_TRAFFIC_METADATA,tx,last,end,3);
    uint32_t offset=0;unsigned n=0;
    while(offset<end) {
        if(n>=KUI_ARMADA_CE_MAPLE_TRAFFIC_RECORD_LIMIT)
            return kui_armada_ce_maple_traffic_fail(w,KUI_ARMADA_CE_MAPLE_TRAFFIC_BUDGET,tx+offset,n,16u,0);
        if(end-offset<12u) return kui_armada_ce_maple_traffic_fail(w,KUI_ARMADA_CE_MAPLE_TRAFFIC_METADATA,tx+offset,end-offset,12u,n);
        uint32_t *row=w->row[n];row[0]=offset;
        for(unsigned j=0;j<3u;j++) if(!kui_armada_ce_maple_traffic_read(io,io->p2,(tx+offset+4u*j)|0xa0000000u,&row[j+1u])) return false;
        uint32_t a=row[1],b=row[2],c=row[3],port=(a>>16)&3u,unit=(c>>8)&63u,bytes=12u+4u*(a&255u);
        if((a&~0x800300ffu) || (c>>24)!=(a&255u) || ((c>>16)&255u)!=(port<<6) ||
           ((c>>8)&192u)!=(port<<6) || !unit || (unit&(unit-1u)) ||
           ((a>>31)!=(offset==last)) || bytes>end-offset || ((offset==last)!=(bytes==end-offset)))
            return kui_armada_ce_maple_traffic_fail(w,KUI_ARMADA_CE_MAPLE_TRAFFIC_METADATA,tx+offset,a,c,n);
        if((b&3u) || b<rx || b-rx>0x1ffcu)
            return kui_armada_ce_maple_traffic_fail(w,KUI_ARMADA_CE_MAPLE_TRAFFIC_MAPPING,tx+offset+4u,b,rx,n);
        if(!kui_armada_ce_maple_traffic_read(io,io->p2,b|0xa0000000u,&row[4])) return false;
        offset+=bytes;++n;
    }
    for(unsigned i=0;i<n;i++) for(unsigned j=0;j<4u;j++) {uint32_t v;
        const uint32_t *row=w->row[i];uint32_t bus=j<3u?tx+row[0]+4u*j:row[2];
        if(!kui_armada_ce_maple_traffic_read(io,io->p2,bus|0xa0000000u,&v)) return false;
        if(v!=row[j+1u]) return kui_armada_ce_maple_traffic_fail(w,KUI_ARMADA_CE_MAPLE_TRAFFIC_CHANGED,bus,v,row[j+1u],i*4u+j);
    }
    if(!kui_armada_ce_maple_traffic_meta(io,ext,ccr,true)) return false;
    for(unsigned i=0;i<4u;i++) if(!kui_armada_ce_maple_traffic_repeat_map(io,ttb,&w->map[i])) return false;
    if(!kui_armada_ce_maple_traffic_witness(io,ttb,process,ccr,slot,root,true)) return false;
    *records=n;return true;
}
static inline enum kui_armada_ce_maple_traffic_status kui_armada_ce_maple_traffic_visit(
    uint32_t rawext,uint32_t ttb,uint32_t process,uint32_t ccr,
    kui_armada_ce_ram_read32 read_p1,kui_armada_ce_ram_read32 read_cache,kui_armada_ce_ram_read32 read_p2,
    void *context,struct kui_armada_ce_maple_traffic_scratch *w,struct kui_armada_ce_maple_traffic_snapshot *out) {
    if(!w || !out || !read_p1 || !read_cache || !read_p2) return KUI_ARMADA_CE_MAPLE_TRAFFIC_ARGUMENT;
    uintptr_t a=(uintptr_t)(void *)w,b=(uintptr_t)(void *)out;
    if((a<=b?b-a<sizeof(*w):a-b<sizeof(*out))) return KUI_ARMADA_CE_MAPLE_TRAFFIC_ARGUMENT;
    w->reads=0;for(unsigned i=0;i<5u;i++) w->error[i]=0;
    if(!out->counts[0]) out->counts[0]=KUI_ARMADA_CE_MAPLE_TRAFFIC_MAGIC;
    out->counts[1]=kui_armada_ce_maple_traffic_inc(out->counts[1]);
    struct kui_armada_ce_maple_traffic_io io={read_p1,read_cache,read_p2,context,w};unsigned n=0;
    bool ok=out->counts[0]==KUI_ARMADA_CE_MAPLE_TRAFFIC_MAGIC && kui_armada_ce_maple_traffic_body(&io,rawext,ttb,process,ccr,&n);
    out->counts[4]=w->reads;
    if(!ok) {
        out->counts[3]=kui_armada_ce_maple_traffic_inc(out->counts[3]);
        if(!w->error[0]) (void)kui_armada_ce_maple_traffic_fail(w,KUI_ARMADA_CE_MAPLE_TRAFFIC_ARGUMENT,0,out->counts[0],KUI_ARMADA_CE_MAPLE_TRAFFIC_MAGIC,0);
        for(unsigned i=0;i<5u;i++) out->fault[i]=w->error[i];
        return (enum kui_armada_ce_maple_traffic_status)w->error[0];
    }
    out->counts[2]=kui_armada_ce_maple_traffic_inc(out->counts[2]);
    for(unsigned i=0;i<n;i++) {
        const uint32_t *row=w->row[i];if(((row[3]>>8)&63u)==32u) continue;
        unsigned command=row[3]&255u,index=command==1u?0u:command>=10u && command<=12u?command-9u:4u;
        out->commands[index]=kui_armada_ce_maple_traffic_inc(out->commands[index]);
        if(!out->first[3]) for(unsigned j=0;j<5u;j++) out->first[j]=row[j];
        for(unsigned j=0;j<5u;j++) out->last[j]=row[j];
    }
    out->metadata[0]=rawext;out->metadata[1]=w->word[1];out->metadata[2]=w->word[6];
    out->metadata[3]=w->word[3]-w->word[2];out->metadata[4]=w->word[4]-w->word[2];
    for(unsigned i=0;i<5u;i++) out->fault[i]=0;
    return KUI_ARMADA_CE_MAPLE_TRAFFIC_OK;
}
#endif
