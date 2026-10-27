/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kui/armada_ce_maple_rx_slice.h"
static const uint32_t TTB=0x8c1458a0u,PROC=0x8c14e8b4u,EXT=0x08030a60u;
static const uint32_t TX=0x0cfd3000u,RX=0x0cfd1000u,UNIT=0x08034000u,FN=0x08035000u;
struct fixture {struct {uint32_t at,value,visits;} cell[256];
    unsigned cells,calls,table,coherent,packet,fail,mutate,mutate_visit;uint32_t replacement;} f;
static struct kui_armada_ce_maple_traffic_scratch capture;
static struct kui_armada_ce_maple_traffic_snapshot traffic;
static struct kui_armada_ce_maple_rx_slice_scratch scratch;
static struct kui_armada_ce_maple_rx_slice out;
static unsigned checks;
#if KUI_ARMADA_CE_CACHE_SNAPSHOT_NATIVE
#define EXPECTED_QUERY_READS 330u
#define EXPECTED_COHERENT_READS 228u
#define EXPECTED_NEIGHBOUR_VISITS 2u
#define FINAL_DATA_VISIT 3u
#else
#define EXPECTED_QUERY_READS 264u
#define EXPECTED_COHERENT_READS 162u
#define EXPECTED_NEIGHBOUR_VISITS 1u
#define FINAL_DATA_VISIT 2u
#endif
#define CHECK(c) do {++checks;assert(c);} while(0)
static uint32_t backing(uint32_t at) {
    if(at>=0x08030000u && at<0x08031000u) return 0xacf93000u+at-0x08030000u;
    if(at>=0x08030000u && at<0x08037000u) return 0xacf90000u+at-0x08030000u;
    return at;
}
static unsigned put(uint32_t at,uint32_t value) {
    at=backing(at);
    for(unsigned i=0;i<f.cells;i++) if(f.cell[i].at==at) {f.cell[i].value=value;return i;}
    assert(f.cells<256u);unsigned i=f.cells++;f.cell[i].at=at;f.cell[i].value=value;return i;
}
static bool read32(void *context,uint32_t at,uint32_t *value) {
    CHECK(context==&f && !(at&3u));++f.calls;if(f.calls==f.fail) return false;
    if(at<0xa0000000u || at==0xff000008u || at==0xff00001cu) ++f.table;
    else if(at>=0xacfd1000u && at<0xacfd4000u) ++f.packet;
    else ++f.coherent;
    for(unsigned i=0;i<f.cells;i++) if(f.cell[i].at==at) {
        ++f.cell[i].visits;
        if(f.mutate==i+1u && f.cell[i].visits==f.mutate_visit) f.cell[i].value=f.replacement;
        *value=f.cell[i].value;return true;
    }
    if((at&0xffffc01fu)==0xf4000000u) {*value=0;return true;}
    /* Line snapshots also read neighbouring words that are not part of the
     * owner's metadata proof. Only used words are retained as witnesses. */
    if(at>=0xacf93000u && at<0xacf97000u) {*value=0;return true;}
    if((at&0xffffc003u)==0xf5000000u) {*value=0;return true;}
    fprintf(stderr,"unexpected read %08x\n",at);assert(0);return false;
}
#if KUI_ARMADA_CE_CACHE_SNAPSHOT_NATIVE
struct kui_armada_ce_cache_snapshot_job kui_armada_ce_cache_snapshot_job;
extern unsigned kui_test_armada_ce_cache_snapshot_reference(uint32_t,uint32_t,
    uint32_t,uint32_t,kui_armada_ce_ram_read32,void *,uint32_t[8]);
unsigned kui_armada_ce_cache_snapshot(struct kui_armada_ce_cache_snapshot_job *job) {
    CHECK(job==&kui_armada_ce_cache_snapshot_job);
    unsigned result=kui_test_armada_ce_cache_snapshot_reference(job->va,job->p1,
        job->page,job->mask,read32,&f,job->word);
    /* The separately tested machine leaf cannot return a callback READ
     * failure. Synthetic read failures still refuse this host route and
     * retain conservative precharged accounting. */
    if(result) {job->detail[0]=job->va;job->detail[1]=result;job->detail[2]=0;}
    return result==KUI_ARMADA_CE_RAM_BIND_OK?0u:
        result==KUI_ARMADA_CE_RAM_BIND_CACHE?1u:
        result==KUI_ARMADA_CE_RAM_BIND_CHANGED?2u:3u;
}
#include "armada-ce-cache-snapshot-wrapper.inc"
#endif
static void mapping(uint32_t va,uint32_t block,uint32_t physical,uint32_t flags) {
    put(0x8c100000u+4u*((va>>16)&511u),block);
    put(block+12u+4u*((va>>12)&15u),physical|flags);
}
static void request(unsigned index,uint32_t offset,uint32_t rx,unsigned command,unsigned words,bool last) {
    uint32_t control=(last?0x80000000u:0u)|words;
    put((TX+offset)|0xa0000000u,control);put((TX+offset+4u)|0xa0000000u,rx);
    put((TX+offset+8u)|0xa0000000u,(words<<24)|((index?2u:32u)<<8)|command);
}
static void function(unsigned i,uint32_t capacity,unsigned command,unsigned expected,unsigned words) {
    uint32_t fn=FN+0x100u*i,unit=UNIT+0x20u*i;
    put(EXT+4u+8u*i,unit&0x1ffffffu);put(unit+8u,1u);put(unit+12u,fn&0x1ffffffu);
    put(fn,i?0u:1u);put(fn+12u,0x00100000u+0x100u*i);put(fn+16u,capacity);
    put(fn+20u,0x01000000u|(words<<16)|(expected<<8)|command);
    put(fn+28u,0u);put(fn+32u,0u);
}
static void prepare(bool generic) {
    memset(&f,0,sizeof(f));memset(&capture,0,sizeof(capture));memset(&traffic,0,sizeof(traffic));
    memset(&scratch,0,sizeof(scratch));memset(&out,0,sizeof(out));
    put(0xff000008u,TTB);put(0xff00001cu,0x105u);put(0x8c145890u,PROC);
    put(TTB,0x8c100000u);put(TTB+16u,0x8c100000u);
    mapping(EXT,0x8c110000u,0x0cf93000u,0x17du);
    mapping(UNIT,0x8c110000u,0x0cf94000u,0x17du);
    mapping(FN,0x8c110000u,0x0cf95000u,0x17du);
    mapping(0x08130000u,0x8c120000u,TX,0x175u);
    mapping(0x08100000u,0x8c130000u,RX,0x175u);
    mapping(0x08101000u,0x8c130000u,RX+0x1000u,0x175u);
    put(EXT,0);put(EXT+0x84u,TX);put(EXT+0x88u,0x00130000u);
    put(EXT+0x8cu,0x00130010u);put(EXT+0x90u,generic?0x00130024u:0x0013001cu);
    put(EXT+0x100u,0x00120c00u);put(EXT+0xc0u,RX);put(EXT+0xc4u,0);
    put(EXT+0xccu,0x00100000u);put(EXT+0xd8u,0x0ced1000u);
    function(0,16u,9u,8u,1u);function(1,generic?524u:116u,generic?11u:1u,generic?0u:5u,generic?2u:0u);
    if(generic) {put(FN+0x100u,2u);put(FN+0x11cu,0x00040000u);}
    request(0,0,RX,9u,1u,false);request(1,16u,RX+0x100u,generic?11u:1u,generic?2u:0u,true);
    put(RX|0xa0000000u,0x03210008u);put((RX+0x100u)|0xa0000000u,UINT32_MAX);
    CHECK(kui_armada_ce_maple_traffic_visit(0x30a60u,TTB,PROC,0x105u,read32,read32,read32,&f,&capture,&traffic)==0);
    f.calls=0;for(unsigned i=0;i<f.cells;i++) f.cell[i].visits=0;
}
static unsigned run(unsigned index,unsigned needed) {
    memset(&out,0xa5,sizeof(out));f.calls=f.table=f.coherent=f.packet=0;
    unsigned result=kui_armada_ce_maple_rx_slice_query(&capture,2u,index,TTB,PROC,0x105u,needed,
        read32,read32,read32,&f,&scratch,&out);
#if KUI_ARMADA_CE_CACHE_SNAPSHOT_NATIVE
    CHECK(scratch.reads>=f.calls && scratch.reads<=KUI_ARMADA_CE_MAPLE_RX_SLICE_READ_LIMIT);
    if(!result) CHECK(scratch.reads==f.calls);
#else
    CHECK(scratch.reads==f.calls && f.calls<=KUI_ARMADA_CE_MAPLE_RX_SLICE_READ_LIMIT);
#endif
    if(result) {const unsigned char *p=(const void *)&out;for(unsigned i=0;i<sizeof(out);i++) CHECK(!p[i]);}
    return result;
}
static void valid(void) {
    prepare(false);CHECK(run(1,116u)==0);CHECK(out.rx_bus==RX+0x100u && out.capacity==116u && out.bucket_bytes==256u);
    /* Per-word resolver:660 reads; page reuse:462. Query-local coherent
     * line snapshots reduce360 coherent reads to162 while fresh final
     * snapshots retain every used data word and all mapping/packet proofs. */
    CHECK(f.calls==EXPECTED_QUERY_READS && f.table==94u && f.coherent==EXPECTED_COHERENT_READS && f.packet==8u &&
        scratch.map_count==3u && scratch.line_count==6u);
    CHECK(out.function_va==FN+0x100u && out.unit_va==UNIT+0x20u && out.unit_index==2u && out.dispatch==0u);
    CHECK(out.command==1u && out.expected_response==5u && !out.payload_words && out.included==1u && !out.pending_irp);
    CHECK(out.rx_header==UINT32_MAX && out.rx_va==0x08100100u && out.descriptor[3]==0x00000201u);
    prepare(false);CHECK(run(0,16u)==0 && out.dispatch==1u && out.capacity==16u && out.command==9u);
    CHECK(f.calls==EXPECTED_QUERY_READS && f.table==94u && f.coherent==EXPECTED_COHERENT_READS && f.packet==8u);
    prepare(true);CHECK(run(1,524u)==0 && out.bucket_bytes==1024u && out.pending_irp==0x40000u && out.dispatch==2u);
    CHECK(f.calls==EXPECTED_QUERY_READS && f.table==94u && f.coherent==EXPECTED_COHERENT_READS && f.packet==8u);
    /* No page witness is retained across public query calls. A newly valid
     * metadata PTE is re-resolved even when this scratch was used before. */
    put(0x8c110020u,0x0cf95175u);CHECK(run(1,524u)==0 && f.calls==EXPECTED_QUERY_READS);
    CHECK(scratch.map[2].virtual_address==0x08035000u && scratch.map[2].page_word==0x0cf95175u);
    /* One source function can span two metadata pages. Cached page bases
     * must restore each requested word's own offset and final witness. */
    prepare(false);uint32_t cross=0x08035ff0u;
    mapping(cross+16u,0x8c110000u,0x0cf96000u,0x17du);
    put(UNIT+0x2cu,cross&0x1ffffffu);put(cross,0u);put(cross+12u,0x00100100u);
    put(cross+16u,116u);put(cross+20u,0x01000501u);put(cross+28u,0u);put(cross+32u,0u);
    CHECK(run(1,116u)==0 && out.function_va==cross && scratch.map_count==4u && f.calls==EXPECTED_QUERY_READS+12u);
    static const uint32_t sizes[]={4,32,36,64,68,116,196,256,260,512,516,524,1020};
    static const uint32_t buckets[]={32,32,64,64,256,256,256,256,512,512,1024,1024,1024};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);i++) {
        CHECK(kui_armada_ce_maple_rx_slice_bucket(sizes[i])==buckets[i]);
        prepare(true);put(FN+0x110u,sizes[i]);CHECK(run(1,sizes[i])==0 && out.capacity==sizes[i]);
    }
    CHECK(!kui_armada_ce_maple_rx_slice_bucket(0) && !kui_armada_ce_maple_rx_slice_bucket(33) && !kui_armada_ce_maple_rx_slice_bucket(1024));
    /* Multiple source functions are permitted only with one matching RX. */
    prepare(false);put(UNIT+0x28u,2);put(UNIT+0x30u,(FN+0x200u)&0x1ffffffu);put(FN+0x20cu,0);
    CHECK(run(1,116)==0 && out.function_count==2u);
}
static void rejected(void) {
    static const struct {uint32_t at,value;} bad[]={
        {0x08030a6cu,0},{0x08034028u,0},{0x08034028u,4},{0x0803402cu,0},
        {0x0803510cu,0},{0x08035110u,0},{0x08035110u,1024},{0x08035110u,115},
        {0x08035114u,0x00000501u},{0x08035114u,0x02000501u},{0x08035114u,0x01000502u},
        {0x08035114u,0x01010501u},{0x08035120u,0x0cfd1100u},
        {0x8c110020u,0x0cfd1175u},{0x8c130010u,0x0cfd217du},
        {0xacfd3014u,0x0cfd1200u},{0xacfd1100u,0x1c020005u},{0x08030b38u,0x0ced0000u}
    };
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {prepare(false);put(bad[i].at,bad[i].value);unsigned result=run(1,116u);if(!result) fprintf(stderr,"bad case %u at %08x accepted\n",i,bad[i].at);CHECK(result!=0);}
    prepare(false);CHECK(run(1,196u)==KUI_ARMADA_CE_MAPLE_RX_SLICE_CAPACITY);
    prepare(false);capture.row[0][2]=RX+0x180u;CHECK(run(1,116)==KUI_ARMADA_CE_MAPLE_RX_SLICE_ALIAS);
    prepare(false);put(UNIT+0x28u,2);put(UNIT+0x30u,(FN+0x200u)&0x1ffffffu);put(FN+0x20cu,0x00100100u);
    CHECK(run(1,116)==KUI_ARMADA_CE_MAPLE_RX_SLICE_ALIAS);
    prepare(true);put(FN+0x11cu,0);CHECK(run(1,524)==KUI_ARMADA_CE_MAPLE_RX_SLICE_METADATA);
    prepare(false);put(FN+0x10cu,0x00100fc0u);put((TX+20u)|0xa0000000u,RX+0xfc0u);
    capture.row[1][2]=RX+0xfc0u;put((RX+0xfc0u)|0xa0000000u,UINT32_MAX);
    CHECK(run(1,116)==KUI_ARMADA_CE_MAPLE_RX_SLICE_MAPPING); /* bucket cannot cross its allocator page */
    prepare(false);uint32_t physical=0x8cf95110u;
    put(0xf4000000u|(physical&0xfe0u)|0x2000u,(physical&0x1ffffc00u)|3u);
    CHECK(run(1,116)==KUI_ARMADA_CE_MAPLE_RX_SLICE_CACHE);
}
static void changed_and_failed(void) {
    prepare(false);CHECK(run(1,116)==0);unsigned calls=f.calls;
    for(unsigned i=1;i<=calls;i++) {prepare(false);f.fail=i;CHECK(run(1,116)!=0 && f.calls==i);}
    prepare(true);unsigned cell=put(FN+0x110u,524u);f.mutate=cell+1u;f.mutate_visit=2u;f.replacement=1020;
    CHECK(run(1,524)==KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED);
    prepare(false);cell=put(EXT+0xcu,(UNIT+0x20u)&0x1ffffffu);f.mutate=cell+1u;f.mutate_visit=2u;f.replacement=0;
    CHECK(run(1,116)!=0);
    prepare(false);cell=put(0x8c130010u,0x0cfd2175u);f.mutate=cell+1u;f.mutate_visit=3u;f.replacement=0x0cfd217du;
    CHECK(run(1,116)!=0);
    prepare(false);cell=put(0x8c110020u,0x0cf9517du);f.mutate=cell+1u;f.mutate_visit=3u;f.replacement=0x0cf95175u;
    CHECK(run(1,116)==KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED);
    /* The metadata PTE is read twice at first resolution and twice only at
     * the final re-walk. Both final reads must reject a changed witness. */
    prepare(false);cell=put(0x8c110020u,0x0cf9517du);CHECK(run(1,116)==0 && f.cell[cell].visits==4u);
    prepare(false);cell=put(0x8c110020u,0x0cf9517du);f.mutate=cell+1u;f.mutate_visit=4u;f.replacement=0x0cf95175u;
    CHECK(run(1,116)!=0);
    prepare(false);struct kui_armada_ce_maple_rx_slice_io io={read32,read32,read32,&f,&capture,&scratch,TTB,PROC,0x105u,0x08000000u};
    scratch.reads=KUI_ARMADA_CE_MAPLE_RX_SLICE_READ_LIMIT;unsigned before=f.calls;uint32_t value;
    CHECK(!kui_armada_ce_maple_rx_slice_read(&io,read32,TTB,&value) && f.calls==before &&
        scratch.error[0]==KUI_ARMADA_CE_MAPLE_RX_SLICE_BUDGET);
}
static unsigned cached_target(void) {
    const uint32_t va=FN+0x100u,p1=0x8cf95100u,index=p1&0xfe0u,color=(va>>12)&3u;
    for(unsigned word=0;word<8u;word++) {
        uint32_t value=0,at=(p1|0x20000000u)+4u*word;
        for(unsigned i=0;i<f.cells;i++) if(f.cell[i].at==at) {value=f.cell[i].value;break;}
        put(0xf5000000u|index|(color<<12)|4u*word,value);
    }
    return put(0xf4000000u|index|(color<<12),(p1&0x1ffffc00u)|3u);
}
static void coherent_lines(void) {
    const uint32_t p1=0x8cf95100u,index=p1&0xfe0u,color=((FN+0x100u)>>12)&3u;
    /* Dirty data in the owner's cache color is authoritative, even when
     * uncached RAM is stale. The line snapshot must not pick stale P2 RAM. */
    prepare(false);unsigned tag=cached_target();put(FN+0x110u,0u);
    CHECK(run(1,116u)==0 && out.capacity==116u && f.coherent==EXPECTED_COHERENT_READS);
    /* Tags need only remain stable within each snapshot. A legitimate
     * eviction/writeback between initial and final snapshots is accepted
     * when fresh coherent reads observe exactly the same used metadata. */
    prepare(false);tag=cached_target();f.mutate=tag+1u;f.mutate_visit=3u;f.replacement=0;
    CHECK(run(1,116u)==0 && out.capacity==116u);
    /* Eviction with different backing bytes cannot reuse saved cache data. */
    prepare(false);tag=cached_target();put(FN+0x110u,0u);
    f.mutate=tag+1u;f.mutate_visit=3u;f.replacement=0;
    CHECK(run(1,116u)==KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED);
    /* An unstable initial or final tag scan rejects, rather than mixing
     * data sampled before/after a cache line is replaced. */
    prepare(false);tag=cached_target();f.mutate=tag+1u;f.mutate_visit=2u;f.replacement=0;
    CHECK(run(1,116u)==KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED);
    prepare(false);tag=cached_target();f.mutate=tag+1u;f.mutate_visit=4u;f.replacement=0;
    CHECK(run(1,116u)==KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED);
    /* A dirty alternate cache color remains ambiguous at either snapshot. */
    prepare(false);unsigned other=put(0xf4000000u|index|(((color+1u)&3u)<<12),0u);
    f.mutate=other+1u;f.mutate_visit=3u;f.replacement=(p1&0x1ffffc00u)|3u;
    CHECK(run(1,116u)==KUI_ARMADA_CE_MAPLE_RX_SLICE_CACHE);
    /* Used data must match even when tags themselves stay unchanged. */
    prepare(false);(void)cached_target();unsigned data=put(0xf5000000u|index|(color<<12)|16u,116u);
    f.mutate=data+1u;f.mutate_visit=2u;f.replacement=196u;
    CHECK(run(1,116u)==KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED);
    /* Unused neighbours in a captured line are not claimed as owner
     * metadata: only used fields must match at final validation. */
    prepare(false);unsigned unused=put(FN+0x104u,7u);CHECK(run(1,116u)==0);
    CHECK(f.cell[unused].visits==EXPECTED_NEIGHBOUR_VISITS);
    /* No line or cache source survives a public query: after an eviction,
     * the next query must observe a newly valid changed capacity in RAM. */
    prepare(true);tag=cached_target();CHECK(run(1,524u)==0 && out.capacity==524u);
    f.mutate=0;put(0xf4000000u|index|(color<<12),0u);put(FN+0x110u,1020u);
    CHECK(run(1,524u)==0 && out.capacity==1020u);
    /* A repeated word or a newly used neighbour from the same coherently
     * admitted line needs no page walk/snapshot. The neighbour becomes part
     * of the final used-word witness; the next public query resets the cache. */
    prepare(false);put(FN+0x104u,0x10203040u);CHECK(run(1,116u)==0);
    struct kui_armada_ce_maple_rx_slice_io hit_io={read32,read32,read32,&f,&capture,&scratch,TTB,PROC,0x105u,0x08000000u};
    uint32_t hit_value;unsigned old_calls=f.calls;f.fail=old_calls+1u;
    CHECK(kui_armada_ce_maple_rx_slice_word(&hit_io,FN+0x110u,&hit_value) && hit_value==116u);
    CHECK(kui_armada_ce_maple_rx_slice_word(&hit_io,FN+0x104u,&hit_value) && hit_value==0x10203040u);
    CHECK(f.calls==old_calls);
    bool marked=false;for(unsigned i=0;i<scratch.line_count;i++)
        if(scratch.line[i].va==FN+0x100u) marked=(scratch.line[i].used&2u)!=0u;
    CHECK(marked);
    /* Every metadata word consumed by the successful parent/A2 proof has
     * a fresh final witness. Corrupt each one exactly at that final read. */
    prepare(false);CHECK(run(1,116u)==0);
    uint32_t used_at[128],used_value[128];unsigned used_count=0;
    for(unsigned i=0;i<scratch.line_count;i++) for(unsigned j=0;j<8u;j++)
        if(scratch.line[i].used&(1u<<j)) {
            used_at[used_count]=scratch.line[i].va+4u*j;
            used_value[used_count++]=scratch.line[i].word[j];
        }
    CHECK(used_count==18u);
    for(unsigned i=0;i<used_count;i++) {
        prepare(false);unsigned cell=put(used_at[i],used_value[i]);
        f.mutate=cell+1u;f.mutate_visit=FINAL_DATA_VISIT;f.replacement=used_value[i]^1u;
        CHECK(run(1,116u)==KUI_ARMADA_CE_MAPLE_RX_SLICE_CHANGED);
    }
    struct kui_armada_ce_maple_rx_slice_io io={read32,read32,read32,&f,&capture,&scratch,TTB,PROC,0x105u,0x08000000u};
    uint32_t value;CHECK(!kui_armada_ce_maple_rx_slice_word(&io,FN+1u,&value));
}
static void admitted_whole_lines(void) {
    /* Public queries may receive a forged capture rather than the sampler's
     * aligned pools. A first requested word outside a pool must not permit
     * a whole-line capture/hit that includes another protected pool word. */
    const uint32_t line=backing(EXT)&0x1fffffffu;
    for(unsigned pool=0;pool<2u;pool++) {
        prepare(false);capture.word[pool?6u:1u]=line+24u;
        if(pool) capture.row[1][2]=line+0x100u; /* Keep the forged row within its forged RX span. */
        CHECK(run(1,116u)==KUI_ARMADA_CE_MAPLE_RX_SLICE_MAPPING && !f.coherent && !scratch.line_count);
        for(unsigned end=0;end<2u;end++) {
            prepare(false);uint32_t span=pool?0x2000u:0x1000u;
            capture.word[pool?6u:1u]=end?line+24u-span:line+24u;
            struct kui_armada_ce_maple_rx_slice_io io={read32,read32,read32,&f,&capture,&scratch,TTB,PROC,0x105u,0x08000000u};
            uint32_t value;f.calls=f.table=f.coherent=f.packet=0;
            CHECK(!kui_armada_ce_maple_rx_slice_word(&io,EXT+(end?28u:0u),&value));
            CHECK(scratch.error[0]==KUI_ARMADA_CE_MAPLE_RX_SLICE_MAPPING &&
                !f.coherent && !scratch.line_count && !scratch.map_count);
        }
    }
}
int main(void) {valid();rejected();changed_and_failed();coherent_lines();admitted_whole_lines();printf("CE RX slice: %u checks passed; query reads%u coherent%u; output %zu scratch %zu bytes; native=%u\n",checks,EXPECTED_QUERY_READS,EXPECTED_COHERENT_READS,sizeof(out),sizeof(scratch),
#if KUI_ARMADA_CE_CACHE_SNAPSHOT_NATIVE
    1u
#else
    0u
#endif
    );return 0;}
