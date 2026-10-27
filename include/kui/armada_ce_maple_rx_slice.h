/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_MAPLE_RX_SLICE_H
#define KUI_ARMADA_CE_MAPLE_RX_SLICE_H
#include "kui/armada_ce_maple_traffic.h"

/* Exact-source provenance for one already-built port-A parent/A2 response.
 * Caller holds the accepted DMA-off fence and IMASK15 for the entire query,
 * staging, final query and publication. This is not a lock or allocator.
 * A live unit -> included function -> pooled RX pointer is the owner contract;
 * pool+54 is spare freelist-record storage, NOT an active-allocation list.
 * No guest word is written and no driver/API/allocator callback is invoked. */
#define KUI_ARMADA_CE_MAPLE_RX_SLICE_READ_LIMIT 2048u
#define KUI_ARMADA_CE_MAPLE_RX_SLICE_LINE_LIMIT 16u
enum kui_armada_ce_maple_rx_slice_status {
    KUI_ARMADA_CE_MAPLE_RX_SLICE_OK,
    KUI_ARMADA_CE_MAPLE_RX_SLICE_ARGUMENT,
    KUI_ARMADA_CE_MAPLE_RX_SLICE_READ,
    KUI_ARMADA_CE_MAPLE_RX_SLICE_BUDGET,
    KUI_ARMADA_CE_MAPLE_RX_SLICE_MAPPING,
    KUI_ARMADA_CE_MAPLE_RX_SLICE_CACHE,
    KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED,
    KUI_ARMADA_CE_MAPLE_RX_SLICE_METADATA,
    KUI_ARMADA_CE_MAPLE_RX_SLICE_CAPACITY,
    KUI_ARMADA_CE_MAPLE_RX_SLICE_ALIAS
};
struct kui_armada_ce_maple_rx_slice {
    uint32_t unit_va,function_va,rx_va,rx_bus,capacity,bucket_bytes;
    uint32_t dispatch,command,expected_response,payload_words,included,pending_irp;
    uint32_t unit_index,function_index,function_count,rx_header;
    uint32_t descriptor[4]; /* byte offset, control, RX bus, wire request */
};
struct kui_armada_ce_maple_rx_slice_scratch {
    uint32_t reads,error[5],map_count,line_count;
    struct kui_armada_ce_maple_mapping map[12];
    struct kui_armada_ce_maple_rx_slice_line {
        uint32_t va,p1,page,used,word[8];
    } line[KUI_ARMADA_CE_MAPLE_RX_SLICE_LINE_LIMIT];
};
struct kui_armada_ce_maple_rx_slice_io {
    kui_armada_ce_ram_read32 p1,cache,p2;void *context;
    const struct kui_armada_ce_maple_traffic_scratch *capture;
    struct kui_armada_ce_maple_rx_slice_scratch *w;
    uint32_t ttb,process,ccr,slot;
};
static inline uint32_t kui_armada_ce_maple_rx_slice_bucket(uint32_t bytes) {
    if(!bytes || bytes>1020u || (bytes&3u)) return 0;
    return bytes>512u?1024u:bytes>256u?512u:bytes>64u?256u:bytes>32u?64u:32u;
}
static inline bool kui_armada_ce_maple_rx_slice_fail(struct kui_armada_ce_maple_rx_slice_io *io,
    unsigned why,uint32_t at,uint32_t actual,uint32_t expected,uint32_t detail) {
    if(!io->w->error[0]) {io->w->error[0]=why;io->w->error[1]=at;io->w->error[2]=actual;
        io->w->error[3]=expected;io->w->error[4]=detail;}return false;
}
static inline bool kui_armada_ce_maple_rx_slice_read(struct kui_armada_ce_maple_rx_slice_io *io,
    kui_armada_ce_ram_read32 read,uint32_t at,uint32_t *v) {
    if(io->w->reads>=KUI_ARMADA_CE_MAPLE_RX_SLICE_READ_LIMIT)
        return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_BUDGET,at,io->w->reads,
            KUI_ARMADA_CE_MAPLE_RX_SLICE_READ_LIMIT,0);
    ++io->w->reads;if(read(io->context,at,v)) return true;
    return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_READ,at,0,0,0);
}
static inline bool kui_armada_ce_maple_rx_slice_table(void *context,uint32_t at,uint32_t *v) {
    struct kui_armada_ce_maple_rx_slice_io *io=context;uint32_t checked;
    if((!kui_armada_ce_ram_table_p1(at,&checked) || kui_armada_ce_maple_traffic_owned(checked,4u)) &&
       at!=0xff000008u && at!=0xff00001cu)
        return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_MAPPING,at,0,0,0);
    return kui_armada_ce_maple_rx_slice_read(io,io->p1,at,v);
}
static inline bool kui_armada_ce_maple_rx_slice_cache(void *context,uint32_t at,uint32_t *v) {
    struct kui_armada_ce_maple_rx_slice_io *io=context;uint32_t checked;
    bool tag=(at&0xffffc01fu)==0xf4000000u,data=(at&0xffffc003u)==0xf5000000u;
    bool ram=(at&0xe0000000u)==0xa0000000u && kui_armada_ce_ram_p1(at,&checked) &&
        !kui_armada_ce_maple_traffic_owned(checked,4u);
    if(!tag && !data && !ram)
        return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_CACHE,at,0,0,0);
    return kui_armada_ce_maple_rx_slice_read(io,io->cache,at,v);
}
/* One coherent 32-byte metadata snapshot. No page-table/P1 read, guest/API
 * callback or blocking operation can occur between the first and last tag
 * scan. Fresh tags choose the presently coherent cache/P2 source every time;
 * a later snapshot may observe an ordinary eviction/writeback without
 * requiring cache-tag identity to survive across page-table walks. */
static inline bool kui_armada_ce_maple_rx_slice_line_read(
    struct kui_armada_ce_maple_rx_slice_io *io,uint32_t va,uint32_t p1,
    uint32_t page,uint32_t mask,uint32_t words[8]) {
#if KUI_ARMADA_CE_CACHE_SNAPSHOT_NATIVE
    /* The native bulk leaf also repeats used data words after its final
     * tag scan. All page, packet and final-query witnesses remain outside
     * this uninterrupted snapshot exactly as in the reference path. */
    unsigned status=kui_armada_ce_cache_line(va,p1,page,io->ccr,mask,
        kui_armada_ce_maple_rx_slice_cache,io,words);
    if(status) return kui_armada_ce_maple_rx_slice_fail(io,
        status==KUI_ARMADA_CE_RAM_BIND_CHANGED?KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED:KUI_ARMADA_CE_MAPLE_RX_SLICE_CACHE,
        va,status,KUI_ARMADA_CE_RAM_BIND_OK,mask);
    return true;
#else
    uint32_t tag[4],again,checked;
    if((va&31u) || (p1&31u) || va>=0x80000000u ||
       !kui_armada_ce_ram_table_p1(p1,&checked) || checked!=p1 ||
       (va&0xfffu)!=(p1&0xfffu) || !(page&1u) ||
       (page&0xfffff000u)!=(p1&0x1ffff000u) || (io->ccr&0xa1u)!=1u ||
       !mask || (mask&~255u))
        return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_CACHE,va,p1,page,mask);
    const uint32_t index=p1&0xfe0u,wanted=p1&0x1ffffc00u;
    const unsigned color=(va>>12)&3u;bool hit=false;
    for(unsigned i=0;i<4u;i++) {
        if(!kui_armada_ce_maple_rx_slice_cache(io,0xf4000000u|index|(i<<12),&tag[i])) return false;
        tag[i]&=0x1ffffc03u;
        if((tag[i]&1u) && (tag[i]&0x1ffffc00u)==wanted) {
            if((tag[i]&2u) && (!(page&8u) || i!=color))
                return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_CACHE,
                    va,tag[i],page,i);
            if((page&8u) && i==color) hit=true;
        }
    }
    uint32_t address=hit?(0xf5000000u|index|(color<<12)):(p1|0x20000000u);
    for(unsigned i=0;i<8u;i++) if(mask&(1u<<i))
        if(!kui_armada_ce_maple_rx_slice_cache(io,address+4u*i,&words[i])) return false;
    for(unsigned i=0;i<4u;i++) {
        uint32_t at=0xf4000000u|index|(i<<12);
        if(!kui_armada_ce_maple_rx_slice_cache(io,at,&again)) return false;
        if((again&0x1ffffc03u)!=tag[i])
            return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED,
                at,again,tag[i],i);
    }
    return true;
#endif
}
/* Only a new line needs mapping/range admission and a coherent snapshot.
 * Keep this larger frame outside the repeated-word hit path below. */
static bool __attribute__((noinline,noclone)) kui_armada_ce_maple_rx_slice_word_miss(struct kui_armada_ce_maple_rx_slice_io *io,
    uint32_t va,uint32_t *v) {
    struct kui_armada_ce_maple_mapping map;uint32_t tx=io->capture->word[1],rx=io->capture->word[6];
    uint32_t page_va=va&~0xfffu,physical,page;unsigned index=0,status=0;
    while(index<io->w->map_count && io->w->map[index].virtual_address!=page_va) ++index;
    /* Only this uninterrupted IMASK15/DMA-off query owns these witnesses.
     * Resolve a metadata page once, retain its exact table words, and reuse
     * that backing for coherent word reads. The final query loop re-walks
     * every retained page before success; no mapping survives the next
     * query's map_count reset. No CE/API callback may run in this interval. */
    if(index<io->w->map_count) {
        /* Retain the original full mapping witness in its private record.
         * A page hit needs these two scalars, not a32-byte stack copy. */
        physical=io->w->map[index].physical_address|(va&0xfffu);
        page=io->w->map[index].page_word;
    } else {
        if(index>=12u) return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_BUDGET,va,index,12u,0);
        status=kui_armada_ce_maple_resolve(io->ttb,va,kui_armada_ce_maple_rx_slice_table,io,&map);
        physical=map.physical_address;page=map.page_word;
    }
    uint32_t line_p1=physical&~31u,bus=line_p1&0x1fffffffu;
    /* Every later hit may use any captured word in this line. Admit the
     * entire line now, including a forged/non-page-aligned pool boundary,
     * before reading it. Difference comparisons avoid endpoint overflow. */
    if(status || kui_armada_ce_maple_traffic_owned(line_p1,32u) ||
       (bus>=tx?bus-tx<0x1000u:tx-bus<32u) ||
       (bus>=rx?bus-rx<0x2000u:rx-bus<32u))
        return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_MAPPING,va,status,page,0);
    if(index==io->w->map_count) {
        io->w->map[index]=map;
        io->w->map[index].virtual_address=page_va;
        io->w->map[index].physical_address&=~0xfffu;++io->w->map_count;
    }
    unsigned line=io->w->line_count;
    if(line>=KUI_ARMADA_CE_MAPLE_RX_SLICE_LINE_LIMIT)
        return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_BUDGET,
            va,line,KUI_ARMADA_CE_MAPLE_RX_SLICE_LINE_LIMIT,1);
    struct kui_armada_ce_maple_rx_slice_line *l=&io->w->line[line];
    l->va=va&~31u;l->p1=line_p1;l->page=page;l->used=0;
    if(!kui_armada_ce_maple_rx_slice_line_read(io,l->va,l->p1,l->page,255u,l->word)) return false;
    ++io->w->line_count;
    unsigned word=(va&31u)/4u;l->used|=1u<<word;*v=l->word[word];
    return true;
}
static inline bool kui_armada_ce_maple_rx_slice_word(struct kui_armada_ce_maple_rx_slice_io *io,
    uint32_t va,uint32_t *v) {
    if((va&3u) || va>=0x80000000u)
        return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_CACHE,va,0,0,0);
    uint32_t line_va=va&~31u;
    for(unsigned i=0;i<io->w->line_count;i++) {
        struct kui_armada_ce_maple_rx_slice_line *l=&io->w->line[i];
        if(l->va==line_va) {
            /* This whole line was admitted and coherently captured during
             * this same uninterrupted query. The final mapping rewalk and
             * fresh used-word snapshots still validate it before success. */
            unsigned word=(va&31u)/4u;l->used|=1u<<word;*v=l->word[word];return true;
        }
    }
    return kui_armada_ce_maple_rx_slice_word_miss(io,va,v);
}
static inline bool kui_armada_ce_maple_rx_slice_pointer(struct kui_armada_ce_maple_rx_slice_io *io,
    uint32_t raw,uint32_t bytes,uint32_t *va) {
    if(kui_armada_ce_maple_owner_pointer(raw,io->slot,bytes,va)) return true;
    return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_METADATA,raw,bytes,io->slot,0);
}
static inline bool kui_armada_ce_maple_rx_slice_witness(struct kui_armada_ce_maple_rx_slice_io *io,
    const uint32_t *row) {
    const struct kui_armada_ce_maple_traffic_scratch *c=io->capture;uint32_t v;
    static const uint32_t at[3]={0xff000008u,0x8c145890u,0xff00001cu};
    const uint32_t expected[3]={io->ttb,io->process,io->ccr};
    for(unsigned i=0;i<3u;i++) {
        if(!kui_armada_ce_maple_rx_slice_table(io,at[i],&v)) return false;
        if(v!=expected[i]) return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED,at[i],v,expected[i],i);
    }
    for(unsigned i=0;i<2u;i++) {
        uint32_t address=io->ttb+(i?4u*(io->slot>>25):0u);
        if(!kui_armada_ce_maple_rx_slice_table(io,address,&v)) return false;
        if(v!=c->map[0].slot_value) return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED,address,v,c->map[0].slot_value,i);
    }
    for(unsigned i=0;i<4u;i++) {
        struct kui_armada_ce_maple_mapping map;
        unsigned status=kui_armada_ce_maple_resolve(io->ttb,c->map[i].virtual_address,
            kui_armada_ce_maple_rx_slice_table,io,&map);
        if(status) return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_MAPPING,c->map[i].virtual_address,status,0,i);
        const uint32_t *a=(const uint32_t *)(const void *)&map,*b=(const uint32_t *)(const void *)&c->map[i];
        for(unsigned j=0;j<8u;j++) if(a[j]!=b[j])
            return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED,map.virtual_address,a[j],b[j],j);
        if(i && (map.page_word&8u)) return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_MAPPING,map.virtual_address,map.page_word,0,i);
    }
    static const uint32_t offset[9]={0,0x84,0x88,0x8c,0x90,0xc0,0xc4,0xcc,0xd8};
    static const unsigned field[9]={0,1,2,3,4,6,7,8,9};
    for(unsigned i=0;i<9u;i++) {
        uint32_t address=c->map[0].virtual_address+offset[i];
        if(!kui_armada_ce_maple_rx_slice_word(io,address,&v)) return false;
        if(v!=c->word[field[i]]) return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED,address,v,c->word[field[i]],i);
    }
    for(unsigned i=0;i<4u;i++) {
        uint32_t bus=i<3u?c->word[1]+row[0]+4u*i:row[2];
        if(!kui_armada_ce_maple_rx_slice_read(io,io->p2,bus|0xa0000000u,&v)) return false;
        if(v!=row[i+1u]) return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED,bus,v,row[i+1u],i);
    }
    return true;
}
static inline bool kui_armada_ce_maple_rx_slice_pass(struct kui_armada_ce_maple_rx_slice_io *io,
    const uint32_t *row,uint32_t min_bytes,struct kui_armada_ce_maple_rx_slice *out) {
    uint32_t unitraw,countword,unit,fnraw,fn,rxraw,explicit_rx,flags;
    unsigned unitindex=((row[3]>>8)&63u)==32u?0u:2u;bool found=false;
    if(!kui_armada_ce_maple_rx_slice_word(io,io->capture->map[0].virtual_address+4u+4u*unitindex,&unitraw) ||
       !kui_armada_ce_maple_rx_slice_pointer(io,unitraw,28u,&unit) ||
       !kui_armada_ce_maple_rx_slice_word(io,unit+8u,&countword)) return false;
    uint32_t count=countword&255u;
    if(!count || count>3u) return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_METADATA,unit+8u,count,3u,0);
    for(unsigned i=0;i<count;i++) {
        if(!kui_armada_ce_maple_rx_slice_word(io,unit+12u+4u*i,&fnraw) ||
           !kui_armada_ce_maple_rx_slice_pointer(io,fnraw,0xf4u,&fn) ||
           !kui_armada_ce_maple_rx_slice_word(io,fn+12u,&rxraw)) return false;
        if(!rxraw || rxraw+io->capture->word[9]!=row[2]) continue;
        if(found) return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_ALIAS,fn,rxraw,row[2],i);
        found=true;
        if(!kui_armada_ce_maple_rx_slice_word(io,fn,&out->dispatch) ||
           !kui_armada_ce_maple_rx_slice_word(io,fn+16u,&out->capacity) ||
           !kui_armada_ce_maple_rx_slice_word(io,fn+20u,&flags) ||
           !kui_armada_ce_maple_rx_slice_word(io,fn+28u,&out->pending_irp) ||
           !kui_armada_ce_maple_rx_slice_word(io,fn+32u,&explicit_rx)) return false;
        out->bucket_bytes=kui_armada_ce_maple_rx_slice_bucket(out->capacity);
        if(!out->bucket_bytes || out->capacity<min_bytes)
            return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_CAPACITY,fn+16u,out->capacity,min_bytes,0);
        if(explicit_rx || !kui_armada_ce_maple_rx_slice_pointer(io,rxraw,out->capacity,&out->rx_va))
            return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_METADATA,fn+32u,explicit_rx,0,0);
        uint32_t pool=io->capture->word[6],poolva=io->capture->map[2].virtual_address;
        if(row[2]<pool || row[2]-pool>0x2000u-out->bucket_bytes ||
           (row[2]&0xfffu)>0x1000u-out->bucket_bytes ||
           out->rx_va!=poolva+(row[2]-pool) || (row[2]&31u))
            return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_MAPPING,out->rx_va,row[2],pool,0);
        out->command=flags&255u;out->expected_response=(flags>>8)&255u;
        out->payload_words=(flags>>16)&255u;out->included=flags>>24;
        if(out->included!=1u || out->command!=(row[3]&255u) || out->payload_words!=(row[3]>>24))
            return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_METADATA,fn+20u,flags,row[3],0);
        if(unitindex==2u && (out->dispatch==2u?(!out->pending_irp || out->expected_response!=0u):
           (out->dispatch!=0u || out->command!=1u || out->expected_response!=5u ||
            out->payload_words || out->capacity!=116u || out->pending_irp)))
            return kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_METADATA,fn,out->dispatch,out->pending_irp,flags);
        out->unit_va=unit;out->function_va=fn;out->rx_bus=row[2];
        out->unit_index=unitindex;out->function_index=i;out->function_count=count;out->rx_header=row[4];
        for(unsigned j=0;j<4u;j++) out->descriptor[j]=row[j];
    }
    return found || kui_armada_ce_maple_rx_slice_fail(io,KUI_ARMADA_CE_MAPLE_RX_SLICE_METADATA,unit,row[2],0,0);
}
/* Output remains all-zero on refusal. Success proves requested capacity, not
 * that an arbitrary response shape is correct. Publisher separately validates
 * parent/controller protocol or target timeout and protocol response length.
 * Re-run after staging and compare the entire output before any guest store.
 * Only portA unit0 and unit2 wire requests are accepted; initial ext+94 parent
 * discovery has no live function and is deliberately refused. */
static inline enum kui_armada_ce_maple_rx_slice_status kui_armada_ce_maple_rx_slice_query(
    const struct kui_armada_ce_maple_traffic_scratch *capture,unsigned records,unsigned index,
    uint32_t ttb,uint32_t process,uint32_t ccr,uint32_t min_bytes,
    kui_armada_ce_ram_read32 p1,kui_armada_ce_ram_read32 cache,kui_armada_ce_ram_read32 p2,void *context,
    struct kui_armada_ce_maple_rx_slice_scratch *w,struct kui_armada_ce_maple_rx_slice *out) {
    if(!out) return KUI_ARMADA_CE_MAPLE_RX_SLICE_ARGUMENT;
    for(unsigned i=0;i<sizeof(*out)/4u;i++) ((volatile uint32_t *)(void *)out)[i]=0;
    if(!capture || !w || !p1 || !cache || !p2 || !records || records>16u || index>=records ||
       !min_bytes || min_bytes>1020u || (min_bytes&3u) ||
       process<0x8c14e6e0u || process>0x8c14e6e0u+31u*0x9cu || (process-0x8c14e6e0u)%0x9cu ||
       (ccr&0xa1u)!=1u) return KUI_ARMADA_CE_MAPLE_RX_SLICE_ARGUMENT;
    w->reads=0;w->map_count=0;w->line_count=0;for(unsigned i=0;i<5u;i++) w->error[i]=0;
    struct kui_armada_ce_maple_rx_slice_io io={p1,cache,p2,context,capture,w,ttb,process,ccr,
        (((process-0x8c14e6e0u)/0x9cu)+1u)<<25};
    const uint32_t *row=capture->row[index];uint32_t unit=(row[3]>>8)&255u;
    uint32_t end=capture->word[4]-capture->word[2],rx=capture->word[6];
    if(capture->word[0] || (capture->map[0].virtual_address&0xfe000000u)!=io.slot ||
       (row[1]&0x00030000u) || ((row[3]>>16)&255u) || (unit!=32u && unit!=2u) ||
       (row[0]&3u) || end>4096u || end<12u || row[0]>end-12u ||
       row[2]<rx || row[2]-rx>0x1ffcu)
        return KUI_ARMADA_CE_MAPLE_RX_SLICE_ARGUMENT;
    struct kui_armada_ce_maple_rx_slice a;
    bool ok=kui_armada_ce_maple_rx_slice_witness(&io,row) &&
        kui_armada_ce_maple_rx_slice_pass(&io,row,min_bytes,&a);
    if(ok) for(unsigned i=0;i<records;i++) if(i!=index) {
        uint32_t other=capture->row[i][2];
        if(other>=a.rx_bus && other-a.rx_bus<a.bucket_bytes) {ok=kui_armada_ce_maple_rx_slice_fail(&io,
            KUI_ARMADA_CE_MAPLE_RX_SLICE_ALIAS,other,a.rx_bus,a.bucket_bytes,i);break;}
    }
    if(ok) ok=kui_armada_ce_maple_rx_slice_witness(&io,row);
    if(ok) for(unsigned i=0;i<w->map_count;i++) {
        struct kui_armada_ce_maple_mapping map;
        unsigned status=kui_armada_ce_maple_resolve(ttb,w->map[i].virtual_address,kui_armada_ce_maple_rx_slice_table,&io,&map);
        if(status) {ok=kui_armada_ce_maple_rx_slice_fail(&io,KUI_ARMADA_CE_MAPLE_RX_SLICE_MAPPING,w->map[i].virtual_address,status,0,i);break;}
        const uint32_t *x=(const uint32_t *)(const void *)&map,*y=(const uint32_t *)(const void *)&w->map[i];
        for(unsigned j=0;j<8u;j++) if(x[j]!=y[j]) {ok=kui_armada_ce_maple_rx_slice_fail(&io,
            KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED,map.virtual_address,x[j],y[j],j);break;}
        if(!ok) break;
    }
    /* All P1 page-table walks are now finished. Validate every used metadata
     * word from fresh coherent line snapshots. Own P1 table reads may have
     * evicted an earlier cache line; matching data is accepted after fresh
     * tag/source selection, while a changed data word or unstable/ambiguous
     * alias refuses the entire query. No witness survives this query. */
    if(ok) for(unsigned i=0;i<w->line_count;i++) {
        struct kui_armada_ce_maple_rx_slice_line *l=&w->line[i];uint32_t words[8];
        if(!kui_armada_ce_maple_rx_slice_line_read(&io,l->va,l->p1,l->page,l->used,words)) {ok=false;break;}
        for(unsigned j=0;j<8u;j++) if((l->used&(1u<<j)) && words[j]!=l->word[j]) {
            ok=kui_armada_ce_maple_rx_slice_fail(&io,KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED,
                l->va+4u*j,words[j],l->word[j],j);break;
        }
        if(!ok) break;
    }
    if(!ok) return (enum kui_armada_ce_maple_rx_slice_status)(w->error[0]?w->error[0]:KUI_ARMADA_CE_MAPLE_RX_SLICE_METADATA);
    const uint32_t *x=(const uint32_t *)(const void *)&a;
    for(unsigned i=0;i<sizeof(a)/4u;i++) ((uint32_t *)(void *)out)[i]=x[i];
    return KUI_ARMADA_CE_MAPLE_RX_SLICE_OK;
}
#endif
