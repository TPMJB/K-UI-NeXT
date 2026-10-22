/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kui/armada_ce_maple_state.h"
#include "kui/armada_ce_maple_headers.h"
struct fixture {
    struct {uint32_t at,value,visits;} cell[40];
    unsigned cells,calls,fail,mutate,mutate_visit;
    uint32_t replacement;
} f;
static struct kui_armada_ce_maple_state_snapshot state;
static struct kui_armada_ce_maple_headers_snapshot out;
static const uint32_t tx=0x0cfd3000u,rx=0x0cfd1000u,raw=0x00130000u;
static void put(uint32_t bus,uint32_t value) {
    for(unsigned i=0;i<f.cells;i++) if(f.cell[i].at==(bus|0xa0000000u)) {f.cell[i].value=value;return;}
    assert(f.cells<40u);f.cell[f.cells].at=bus|0xa0000000u;f.cell[f.cells].value=value;++f.cells;
}
static bool read_header(void *context,uint32_t address,uint32_t *value) {
    assert(context==&f && (address&0xe0000000u)==0xa0000000u && !(address&3u));
    ++f.calls;if(f.calls==f.fail) return false;
    for(unsigned i=0;i<f.cells;i++) if(f.cell[i].at==address) {
        ++f.cell[i].visits;
        if(f.mutate==i+1u && f.cell[i].visits==f.mutate_visit) f.cell[i].value=f.replacement;
        *value=f.cell[i].value;return true;
    }
    fprintf(stderr,"Unexpected payload/unadmitted read %08x\n",address);assert(0);return false;
}
static void descriptor(unsigned n,uint32_t offset,uint32_t words,bool last) {
    uint32_t port=n&3u;
    put(tx+offset,(last?0x80000000u:0u)|(port<<16)|words);
    put(tx+offset+4u,rx+n*32u);
    put(tx+offset+8u,(words<<24)|(port<<22)|((port<<6|32u)<<8)|1u);
    put(rx+n*32u,0x10002008u+n);
}
static void prepare(unsigned descriptors) {
    memset(&f,0,sizeof(f));memset(&state,0,sizeof(state));memset(&out,0,sizeof(out));
    state.counts[0]=1;state.counts[2]=31;
    state.tx[0]=tx;state.tx[1]=raw;state.tx[4]=tx|0x80000000u;
    state.rx[0]=rx;state.rx[2]=0x00100000u;state.rx[4]=rx|0x80000000u;
    state.maps[0]=tx|0x175u;state.maps[1]=rx|0x175u;state.maps[2]=(rx+0x1000u)|0x175u;
    uint32_t offset=0;
    for(unsigned n=0;n<descriptors;n++) {
        descriptor(n,offset,n==0u?1u:0u,n+1u==descriptors);
        state.tx[2]=raw+offset;offset+=n==0u?16u:12u;
    }
    state.tx[3]=raw+offset;
}
static enum kui_armada_ce_maple_proof_status run_headers(void) {
    struct kui_armada_ce_maple_state_snapshot saved=state;
    enum kui_armada_ce_maple_proof_status result=kui_armada_ce_maple_headers(state.tx,state.rx,state.maps,read_header,&f,&out);
    assert(!memcmp(&state,&saved,sizeof(state)));
    assert(out.counts[0]==(uint32_t)result && out.counts[1]==f.calls && f.calls<=64u);
    return result;
}
static void success(void) {
    for(unsigned n=1;n<=8u;n++) {
        prepare(n);assert(run_headers()==KUI_ARMADA_CE_MAPLE_PROOF_OK);
        assert(out.counts[1]==n*8u && out.counts[2]==n && out.counts[3]==n*12u+4u && out.counts[4]==3u);
        assert(out.rows[0][0]==0 && out.rows[0][3]==0x01002001u);
        for(unsigned i=0;i<f.cells;i++) assert(f.cell[i].visits==2u);
    }
    prepare(1);put(tx+4u,rx+0x1ffcu);put(rx+0x1ffcu,UINT32_MAX);
    assert(run_headers()==KUI_ARMADA_CE_MAPLE_PROOF_OK && out.rows[0][4]==UINT32_MAX);
    static const uint32_t units[]={1,2,4,8,16,32};
    for(unsigned i=0;i<6u;i++) {
        prepare(1);put(tx+8u,0x01000001u|(units[i]<<8));assert(run_headers()==KUI_ARMADA_CE_MAPLE_PROOF_OK);
    }
    /* Payload length changes the next header address; its words are absent
     * from the fixture and any attempt to read them fails immediately. */
    prepare(1);descriptor(0,0,255u,true);state.tx[3]=raw+1032u;
    assert(run_headers()==KUI_ARMADA_CE_MAPLE_PROOF_OK && f.calls==8u);
    prepare(1);
    for(unsigned n=0;n<4u;n++) descriptor(n,n*1024u,253u,n==3u);
    state.tx[2]=raw+3072u;state.tx[3]=raw+4096u;
    assert(run_headers()==KUI_ARMADA_CE_MAPLE_PROOF_OK && out.counts[3]==4096u);
}
static void fail_reads(void) {
    for(unsigned n=1;n<=64u;n++) {
        prepare(8);f.fail=n;assert(run_headers()==KUI_ARMADA_CE_MAPLE_PROOF_READ && f.calls==n);
    }
    for(unsigned cell=0;cell<32u;cell++) {
        prepare(8);f.mutate=cell+1u;f.mutate_visit=2u;f.replacement=f.cell[cell].value^1u;
        assert(run_headers()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
        assert(out.counts[4]==1u && out.counts[2]==8u);
    }
}
static void malformed(void) {
    static const struct {unsigned word;uint32_t value;unsigned status;} invalid[]={
        {0,0x80000201u,KUI_ARMADA_CE_MAPLE_PROOF_METADATA}, /* special light-gun */
        {0,0x80040001u,KUI_ARMADA_CE_MAPLE_PROOF_METADATA},
        {0,0x00000001u,KUI_ARMADA_CE_MAPLE_PROOF_METADATA}, /* missing last */
        {0,0x800000ffu,KUI_ARMADA_CE_MAPLE_PROOF_METADATA},
        {1,0x0cfd1001u,KUI_ARMADA_CE_MAPLE_PROOF_MAPPING},
        {1,0x0cfd0ffcu,KUI_ARMADA_CE_MAPLE_PROOF_MAPPING},
        {1,0x0cfd3000u,KUI_ARMADA_CE_MAPLE_PROOF_MAPPING},
        {1,0x8cfd1000u,KUI_ARMADA_CE_MAPLE_PROOF_MAPPING},
        {2,0x00002001u,KUI_ARMADA_CE_MAPLE_PROOF_METADATA},
        {2,0x01012001u,KUI_ARMADA_CE_MAPLE_PROOF_METADATA},
        {2,0x01006001u,KUI_ARMADA_CE_MAPLE_PROOF_METADATA},
        {2,0x01000001u,KUI_ARMADA_CE_MAPLE_PROOF_METADATA},
        {2,0x01000301u,KUI_ARMADA_CE_MAPLE_PROOF_METADATA}
    };
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        prepare(1);put(tx+invalid[i].word*4u,invalid[i].value);
        assert(run_headers()==(enum kui_armada_ce_maple_proof_status)invalid[i].status);
        assert(f.calls<=3u); /* Reject before following any bad RX destination. */
    }
    prepare(4);put(tx,0x80000001u);assert(run_headers()==KUI_ARMADA_CE_MAPLE_PROOF_METADATA);
    prepare(4);state.tx[2]+=4u;assert(run_headers()==KUI_ARMADA_CE_MAPLE_PROOF_METADATA);
    prepare(1);state.tx[3]+=4u;assert(run_headers()==KUI_ARMADA_CE_MAPLE_PROOF_METADATA);
    prepare(1);state.tx[3]-=4u;assert(run_headers()==KUI_ARMADA_CE_MAPLE_PROOF_METADATA);
    prepare(9);assert(run_headers()==KUI_ARMADA_CE_MAPLE_PROOF_BUDGET && out.counts[2]==8u);
    /* A trailing fragment is refused without reading it. */
    prepare(1);put(tx,1u);state.tx[2]=raw+8u;state.tx[3]=raw+20u;
    assert(run_headers()==KUI_ARMADA_CE_MAPLE_PROOF_METADATA);
}
static void invalid_state(void) {
    static const struct {unsigned group,index;uint32_t value;} invalid[]={
        {1,0,0},{1,0,0x0cfd3004u},{1,4,0},
        {2,0,0},{2,0,0x0cfff000u},{2,0,0x0cfd3000u},{2,1,1},{2,4,0},
        {3,0,0},{3,0,0x0cfd317du},{3,1,0x0cfd2175u},{3,2,0x0cfd1175u},
        {1,1,0},{1,1,0x00130002u},{1,2,0},{1,2,0x0012fffcu},
        {1,2,0x00130001u},{1,3,0},{1,3,0x0013000bu},{1,3,0x00131004u}
    };
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        prepare(1);uint32_t *groups[]={state.counts,state.tx,state.rx,state.maps};
        groups[invalid[i].group][invalid[i].index]=invalid[i].value;
        assert(run_headers()==KUI_ARMADA_CE_MAPLE_PROOF_METADATA && f.calls==0);
    }
    prepare(1);struct kui_armada_ce_maple_headers_snapshot prior=out;
    assert(kui_armada_ce_maple_headers(NULL,state.rx,state.maps,read_header,&f,&out)==KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT);
    assert(kui_armada_ce_maple_headers(state.tx,state.rx,state.maps,NULL,&f,&out)==KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT);
    assert(kui_armada_ce_maple_headers(state.tx,state.rx,state.maps,read_header,&f,NULL)==KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT);
    assert(kui_armada_ce_maple_headers((const void *)&out,state.rx,state.maps,read_header,&f,&out)==KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT);
    assert(!memcmp(&prior,&out,sizeof(out)) && f.calls==0);
}
int main(void) {
    success();fail_reads();malformed();invalid_state();
    puts("CE Maple headers: bounded two-pass ordinary TX/RX headers, all read failures and mutations passed");return 0;
}
