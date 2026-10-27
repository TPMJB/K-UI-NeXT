/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_MAPLE_MAPPING_H
#define KUI_ARMADA_CE_MAPLE_MAPPING_H
#include "kui/armada_ce_ram_bind.h"

struct kui_armada_ce_maple_mapping {
    uint32_t virtual_address,slot_address,slot_value,block_address,block_value;
    uint32_t page_address,page_word,physical_address;
};

/* Exact Armada ordinary committed-MEMBLOCK geometry, independently observed
 * in NK0x8c01259c. This resolves an explicit-slot VA to main RAM only; it
 * never samples that backing word or grants native access/ownership. P1
 * table pointers retain their original cache domain. Positive/special block
 * words remain diagnostic values and are never dereferenced. At most six
 * callbacks run. Raw fields survive refusal; physical_address is published
 * only after every underlying table word repeats unchanged. */
static inline enum kui_armada_ce_ram_bind_status kui_armada_ce_maple_resolve(
    uint32_t ttb,uint32_t va,kui_armada_ce_ram_read32 read32,void *context,
    struct kui_armada_ce_maple_mapping *out) {
    uint32_t root,slot,block,checked,candidate=0,again;
    uint32_t address[3],value[3];unsigned count=0;
    enum kui_armada_ce_ram_bind_status status=KUI_ARMADA_CE_RAM_BIND_OK;
    if(!out) return KUI_ARMADA_CE_RAM_BIND_ARGUMENT;
#if KUI_ARMADA_CE_CACHE_SNAPSHOT_NATIVE
    /* Only the isolated native snapshot helper uses this aligned clear.
     * Explicit volatile member stores preserve every zeroed witness while
     * avoiding32 uncached byte-store loop iterations and aliasing casts. */
    _Static_assert(sizeof(struct kui_armada_ce_maple_mapping)==8u*sizeof(uint32_t),"mapping word clear ABI");
    _Static_assert(_Alignof(struct kui_armada_ce_maple_mapping)>=_Alignof(uint32_t),"mapping word clear alignment");
    volatile struct kui_armada_ce_maple_mapping *clean=out;
    clean->virtual_address=0;clean->slot_address=0;clean->slot_value=0;clean->block_address=0;
    clean->block_value=0;clean->page_address=0;clean->page_word=0;clean->physical_address=0;
#else
    /* Volatile byte stores keep this freestanding helper independent of
     * compiler-generated memset/library calls. */
    for(size_t i=0;i<sizeof(*out);i++) ((volatile unsigned char *)(void *)out)[i]=0;
#endif
    out->virtual_address=va;
    if(!read32 || (va&3u) || va<UINT32_C(0x02000000) || va>=UINT32_C(0x80000000))
        return KUI_ARMADA_CE_RAM_BIND_ARGUMENT;
    if(!kui_armada_ce_ram_table_p1(ttb,&root)) return KUI_ARMADA_CE_RAM_BIND_TTB;
    out->slot_address=root+4u*(va>>25);
    if(!kui_armada_ce_ram_table_p1(out->slot_address,&checked))
        return KUI_ARMADA_CE_RAM_BIND_TTB;
    if(!read32(context,out->slot_address,&out->slot_value)) return KUI_ARMADA_CE_RAM_BIND_READ;
    address[count]=out->slot_address;value[count++]=out->slot_value;
    if(!kui_armada_ce_ram_table_p1(out->slot_value,&slot)) {
        status=KUI_ARMADA_CE_RAM_BIND_SLOT;goto repeat;
    }
    out->block_address=slot+4u*((va>>16)&0x1ffu);
    if(!kui_armada_ce_ram_table_p1(out->block_address,&checked)) {
        status=KUI_ARMADA_CE_RAM_BIND_SLOT;goto repeat;
    }
    if(!read32(context,out->block_address,&out->block_value)) return KUI_ARMADA_CE_RAM_BIND_READ;
    address[count]=out->block_address;value[count++]=out->block_value;
    if(!(out->block_value&UINT32_C(0x80000000)) ||
       !kui_armada_ce_ram_table_p1(out->block_value,&block)) {
        status=KUI_ARMADA_CE_RAM_BIND_BLOCK;goto repeat;
    }
    out->page_address=block+12u+4u*((va>>12)&15u);
    if(!kui_armada_ce_ram_table_p1(out->page_address,&checked)) {
        status=KUI_ARMADA_CE_RAM_BIND_BLOCK;goto repeat;
    }
    if(!read32(context,out->page_address,&out->page_word)) return KUI_ARMADA_CE_RAM_BIND_READ;
    address[count]=out->page_address;value[count++]=out->page_word;
    if(!(out->page_word&1u)) status=KUI_ARMADA_CE_RAM_BIND_PAGE;
    else if(!kui_armada_ce_ram_p1((out->page_word&UINT32_C(0xfffff000))|
        (va&0xfffu)|UINT32_C(0x80000000),&candidate))
        status=KUI_ARMADA_CE_RAM_BIND_DESTINATION;
repeat:
    for(unsigned i=0;i<count;i++) {
        if(!read32(context,address[i],&again)) return KUI_ARMADA_CE_RAM_BIND_READ;
        if(again!=value[i]) return KUI_ARMADA_CE_RAM_BIND_CHANGED;
    }
    if(status==KUI_ARMADA_CE_RAM_BIND_OK) out->physical_address=candidate;
    return status;
}
#endif
