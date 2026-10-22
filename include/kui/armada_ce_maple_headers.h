/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_MAPLE_HEADERS_H
#define KUI_ARMADA_CE_MAPLE_HEADERS_H
#include "kui/armada_ce_maple_headers_types.h"
#include "kui/armada_ce_maple_proof.h"
/* Retained ordinary-list headers only. No payload word is inspected.
 * counts: status, callback reads, captured descriptors, list bytes, pass mask.
 * rows: TX byte offset, descriptor W0, RX bus, request header, response header.
 * fault: reason, address, observed, expected, detail (descriptor*4+word).
 * Inputs must be from the immediately preceding admitted state observation. The
 * caller must repeat its ownership, mappings and extension witnesses after
 * capture, revoking success if they changed. Two matching header passes are
 * observational consistency only: neither DMA idleness nor allocation life.
 * Only the admitted uncached physical ranges are read, using their P2 aliases.
 * An explicit RX destination outside the pool and special descriptor formats
 * are conservatively refused. No CE API, DMA control or guest write occurs. */
static inline enum kui_armada_ce_maple_proof_status kui_armada_ce_maple_headers_fault(
    struct kui_armada_ce_maple_headers_snapshot *out,
    enum kui_armada_ce_maple_proof_status why,uint32_t at,uint32_t actual,uint32_t expected,uint32_t detail) {
    out->counts[0]=(uint32_t)why;
    out->fault[0]=(uint32_t)why;out->fault[1]=at;out->fault[2]=actual;
    out->fault[3]=expected;out->fault[4]=detail;return why;
}
static inline bool kui_armada_ce_maple_headers_read(kui_armada_ce_ram_read32 read32,void *context,
    struct kui_armada_ce_maple_headers_snapshot *out,uint32_t bus,uint32_t *value,uint32_t detail) {
    ++out->counts[1];
    if(read32(context,bus|UINT32_C(0xa0000000),value)) return true;
    (void)kui_armada_ce_maple_headers_fault(out,KUI_ARMADA_CE_MAPLE_PROOF_READ,
        bus|UINT32_C(0xa0000000),0,0,detail);return false;
}
static inline enum kui_armada_ce_maple_proof_status kui_armada_ce_maple_headers(
    const uint32_t tx_row[5],const uint32_t rx_row[5],const uint32_t maps[5],kui_armada_ce_ram_read32 read32,
    void *context,struct kui_armada_ce_maple_headers_snapshot *out) {
    if(!tx_row || !rx_row || !maps || !read32 || !out) return KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT;
    const uint32_t *input[3]={tx_row,rx_row,maps};
    for(unsigned i=0;i<3u;i++) if(kui_armada_ce_maple_proof_overlap((uintptr_t)input[i],20u,
        (uintptr_t)out,sizeof(*out))) return KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT;
    kui_armada_ce_maple_zero(out,sizeof(*out));
    const uint32_t tx=tx_row[0],rx=rx_row[0],raw=tx_row[1];
    const uint32_t last=tx_row[2]-raw,end=tx_row[3]-raw;
    out->counts[3]=end;
    if(tx<0x0c010000u || tx>0x0cfff000u || (tx&0xfffu) ||
       rx<0x0c010000u || rx>0x0cffe000u || (rx&0xfffu) || rx_row[1] ||
       (tx<rx+0x2000u && rx<tx+0x1000u) ||
       tx_row[4]!=(tx|0x80000000u) || rx_row[4]!=(rx|0x80000000u) ||
       (maps[0]&0x1ffff009u)!=(tx|1u) ||
       (maps[1]&0x1ffff009u)!=(rx|1u) ||
       (maps[2]&0x1ffff009u)!=((rx+0x1000u)|1u) ||
       !raw || (raw&3u) || !tx_row[2] || !tx_row[3] ||
       (last&3u) || (end&3u) || end<12u || end>0x1000u || last>end-12u)
        return kui_armada_ce_maple_headers_fault(out,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,
            tx,last,end,0);
    uint32_t offset=0;
    while(offset<end) {
        unsigned n=out->counts[2];
        if(n>=KUI_ARMADA_CE_MAPLE_HEADERS_MAX)
            return kui_armada_ce_maple_headers_fault(out,KUI_ARMADA_CE_MAPLE_PROOF_BUDGET,
                tx+offset,n,KUI_ARMADA_CE_MAPLE_HEADERS_MAX,0);
        uint32_t *row=out->rows[n];row[0]=offset;
        if(end-offset<12u)
            return kui_armada_ce_maple_headers_fault(out,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,
                tx+offset,end-offset,12u,n*4u);
        for(unsigned j=0;j<3u;j++)
            if(!kui_armada_ce_maple_headers_read(read32,context,out,tx+offset+4u*j,&row[j+1u],n*4u+j))
                return (enum kui_armada_ce_maple_proof_status)out->counts[0];
        const uint32_t w0=row[1],w1=row[2],w2=row[3],words=w0&255u,port=(w0>>16)&3u;
        const uint32_t destination=(w2>>8)&255u,unit=destination&63u;
        const uint32_t bytes=12u+words*4u;
        if((w0&~UINT32_C(0x800300ff)) || (w2>>24)!=words ||
           ((w2>>16)&255u)!=(port<<6) || (destination&192u)!=(port<<6) ||
           !unit || (unit&(unit-1u)) ||
           ((w0>>31)!=(offset==last)) || bytes>end-offset ||
           ((offset==last)!=(bytes==end-offset)))
            return kui_armada_ce_maple_headers_fault(out,KUI_ARMADA_CE_MAPLE_PROOF_METADATA,
                tx+offset,w0,w2,n*4u);
        if((w1&3u) || w1<rx || w1-rx>0x1ffcu)
            return kui_armada_ce_maple_headers_fault(out,KUI_ARMADA_CE_MAPLE_PROOF_MAPPING,
                tx+offset+4u,w1,rx,n*4u+1u);
        if(!kui_armada_ce_maple_headers_read(read32,context,out,w1,&row[4],n*4u+3u))
            return (enum kui_armada_ce_maple_proof_status)out->counts[0];
        ++out->counts[2];offset+=bytes;
    }
    out->counts[4]=1u;
    for(unsigned n=0;n<out->counts[2];n++) {
        const uint32_t *row=out->rows[n];
        for(unsigned j=0;j<4u;j++) {
            uint32_t bus=j<3u?tx+row[0]+4u*j:row[2],value;
            if(!kui_armada_ce_maple_headers_read(read32,context,out,bus,&value,n*4u+j))
                return (enum kui_armada_ce_maple_proof_status)out->counts[0];
            if(value!=row[j+1u]) return kui_armada_ce_maple_headers_fault(out,KUI_ARMADA_CE_MAPLE_PROOF_CHANGED,
                bus|UINT32_C(0xa0000000),value,row[j+1u],n*4u+j);
        }
    }
    out->counts[4]=3u;out->counts[0]=KUI_ARMADA_CE_MAPLE_PROOF_OK;
    return KUI_ARMADA_CE_MAPLE_PROOF_OK;
}
#endif
