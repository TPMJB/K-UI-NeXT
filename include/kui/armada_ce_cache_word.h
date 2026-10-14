/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_CACHE_WORD_H
#define KUI_ARMADA_CE_CACHE_WORD_H
#include "kui/armada_ce_ram_bind.h"

/* SH7750 normal 16 KiB operand cache: four virtual index colors, physical
 * address tag bits 28:10, V bit 0 and U bit 1. Reserved tag bits are ignored.
 * The array callback MUST execute its longword MOV in P2, with interrupts
 * masked and eight instructions before returning to cached code. It admits
 * only these read-only array addresses and ordinary uncached main RAM.
 * No cache maintenance, TLB operation or native CE API occurs here. */
static inline enum kui_armada_ce_ram_bind_status kui_armada_ce_cache_word(
    uint32_t va,uint32_t p1,uint32_t page,uint32_t ccr,
    kui_armada_ce_ram_read32 read32,void *context,uint32_t *out) {
    uint32_t checked,tag[4],value=0,again;
    if(!out) return KUI_ARMADA_CE_RAM_BIND_ARGUMENT;
    *out=0;
    if(!read32 || (va&3u) || va>=0x80000000u ||
       !kui_armada_ce_ram_table_p1(p1,&checked) || checked!=p1 ||
       (va&0xfffu)!=(p1&0xfffu) || !(page&1u) ||
       (page&0xfffff000u)!=(p1&0x1ffff000u))
        return KUI_ARMADA_CE_RAM_BIND_ARGUMENT;
    if((ccr&0xa1u)!=1u) return KUI_ARMADA_CE_RAM_BIND_CACHE;
    const uint32_t index=p1&0xfe0u,wanted=p1&0x1ffffc00u;
    const unsigned color=(va>>12)&3u;
    bool hit=false;
    for(unsigned i=0;i<4u;i++) {
        if(!read32(context,0xf4000000u|index|(i<<12),&tag[i]))
            return KUI_ARMADA_CE_RAM_BIND_READ;
        tag[i]&=0x1ffffc03u;
        if((tag[i]&1u) && (tag[i]&0x1ffffc00u)==wanted) {
            /* A dirty alternate alias cannot prove the value a subsequent
             * native read through this VA would observe. Fail closed. */
            if((tag[i]&2u) && (!(page&8u) || i!=color))
                return KUI_ARMADA_CE_RAM_BIND_CACHE;
            if((page&8u) && i==color) hit=true;
        }
    }
    const uint32_t address=hit?(0xf5000000u|index|(color<<12)|(p1&0x1cu)):
                               (p1|0x20000000u);
    if(!read32(context,address,&value)) return KUI_ARMADA_CE_RAM_BIND_READ;
    for(unsigned i=0;i<4u;i++) {
        if(!read32(context,0xf4000000u|index|(i<<12),&again))
            return KUI_ARMADA_CE_RAM_BIND_READ;
        if((again&0x1ffffc03u)!=tag[i]) return KUI_ARMADA_CE_RAM_BIND_CHANGED;
    }
    if(!read32(context,address,&again)) return KUI_ARMADA_CE_RAM_BIND_READ;
    if(again!=value) return KUI_ARMADA_CE_RAM_BIND_CHANGED;
    *out=value;
    return KUI_ARMADA_CE_RAM_BIND_OK;
}
#endif
