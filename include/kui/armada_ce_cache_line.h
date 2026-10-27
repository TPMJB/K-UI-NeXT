/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_CACHE_LINE_H
#define KUI_ARMADA_CE_CACHE_LINE_H
#include "kui/armada_ce_ram_bind.h"
#if KUI_ARMADA_CE_CACHE_SNAPSHOT_NATIVE
#include "kui/armada_ce_cache_snapshot.h"
#endif

/* Reference masked 32-byte coherent read for the normal SH7750 operand
 * cache. Caller serializes the complete operation with interrupts masked.
 * The callback performs only the admitted P2 RAM/cache-array loads; it must
 * not enter P1 code, touch a P1 stack, walk tables, or call CE between scans.
 * Every array load executes in P2 with the required eight instructions
 * before cached execution resumes. No cache state is modified here.
 *
 * As in cache_word, sample four tags, data, four tags, repeated data. A
 * dirty alternate alias refuses. Matching tags and repeated used words
 * prove this snapshot's consistency, not allocation or DMA ownership.
 * Each call selects a fresh cache/P2 source; no tag survives another call.
 * Only requested word bits are read; the whole output is zero on refusal. */
static inline enum kui_armada_ce_ram_bind_status kui_armada_ce_cache_line(
    uint32_t va,uint32_t p1,uint32_t page,uint32_t ccr,uint32_t mask,
    kui_armada_ce_ram_read32 read32,void *context,uint32_t out[8]) {
    uint32_t checked;
    if(!out) return KUI_ARMADA_CE_RAM_BIND_ARGUMENT;
    for(unsigned i=0;i<8u;i++) out[i]=0;
    if(!read32 || (va&31u) || (p1&31u) || va>=0x80000000u ||
       !kui_armada_ce_ram_table_p1(p1,&checked) || checked!=p1 ||
       (va&0xfffu)!=(p1&0xfffu) || !(page&1u) ||
       (page&0xfffff000u)!=(p1&0x1ffff000u) || !mask || (mask&~255u))
        return KUI_ARMADA_CE_RAM_BIND_ARGUMENT;
    if((ccr&0xa1u)!=1u) return KUI_ARMADA_CE_RAM_BIND_CACHE;
#if KUI_ARMADA_CE_CACHE_SNAPSHOT_NATIVE
    return kui_armada_ce_cache_line_native(va,p1,page,mask,read32,context,out);
#else
    uint32_t tag[4],word[8],again;
    const uint32_t index=p1&0xfe0u,wanted=p1&0x1ffffc00u;
    const unsigned color=(va>>12)&3u;bool hit=false;
    for(unsigned i=0;i<4u;i++) {
        if(!read32(context,0xf4000000u|index|(i<<12),&tag[i]))
            return KUI_ARMADA_CE_RAM_BIND_READ;
        tag[i]&=0x1ffffc03u;
        if((tag[i]&1u) && (tag[i]&0x1ffffc00u)==wanted) {
            if((tag[i]&2u) && (!(page&8u) || i!=color))
                return KUI_ARMADA_CE_RAM_BIND_CACHE;
            if((page&8u) && i==color) hit=true;
        }
    }
    const uint32_t address=hit?(0xf5000000u|index|(color<<12)):(p1|0x20000000u);
    for(unsigned i=0;i<8u;i++) if(mask&(1u<<i))
        if(!read32(context,address+4u*i,&word[i])) return KUI_ARMADA_CE_RAM_BIND_READ;
    for(unsigned i=0;i<4u;i++) {
        if(!read32(context,0xf4000000u|index|(i<<12),&again))
            return KUI_ARMADA_CE_RAM_BIND_READ;
        if((again&0x1ffffc03u)!=tag[i]) return KUI_ARMADA_CE_RAM_BIND_CHANGED;
    }
    for(unsigned i=0;i<8u;i++) if(mask&(1u<<i)) {
        if(!read32(context,address+4u*i,&again)) return KUI_ARMADA_CE_RAM_BIND_READ;
        if(again!=word[i]) return KUI_ARMADA_CE_RAM_BIND_CHANGED;
    }
    for(unsigned i=0;i<8u;i++) if(mask&(1u<<i)) out[i]=word[i];
    return KUI_ARMADA_CE_RAM_BIND_OK;
#endif
}
#endif
