/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_RAM_BIND_H
#define KUI_ARMADA_CE_RAM_BIND_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Exact Armada US kernel profile. These are virtual holder addresses, not
 * fixed physical addresses and not permission to call the DLL functions. */
#define KUI_ARMADA_CE_HEAP_NOMINAL UINT32_C(0x01d550ac)
#define KUI_ARMADA_CE_HEAP_REAL UINT32_C(0x03d550ac)
#define KUI_ARMADA_CE_SOUND_RAM_NOMINAL UINT32_C(0x01d56118)
#define KUI_ARMADA_CE_SOUND_RAM_REAL UINT32_C(0x03d56118)
#define KUI_ARMADA_CE_SOUND_REF_NOMINAL UINT32_C(0x01d550d0)
#define KUI_ARMADA_CE_SOUND_REF_REAL UINT32_C(0x03d550d0)

enum kui_armada_ce_ram_bind_status {
    KUI_ARMADA_CE_RAM_BIND_OK,
    KUI_ARMADA_CE_RAM_BIND_ARGUMENT,
    KUI_ARMADA_CE_RAM_BIND_TTB,
    KUI_ARMADA_CE_RAM_BIND_SLOT,
    KUI_ARMADA_CE_RAM_BIND_BLOCK,
    KUI_ARMADA_CE_RAM_BIND_PAGE,
    KUI_ARMADA_CE_RAM_BIND_DESTINATION,
    KUI_ARMADA_CE_RAM_BIND_READ,
    KUI_ARMADA_CE_RAM_BIND_CHANGED,
    KUI_ARMADA_CE_RAM_BIND_CACHE /* 9: map resolved; holder was not read. */
};
struct kui_armada_ce_ram_binding {
    uint32_t physical_address; /* Canonical P1 alias of a main-RAM word. */
    uint32_t value;            /* Published pointer/value, never dereferenced. */
    uint32_t page_word;        /* CE's software page entry for the holder. */
};
typedef bool (*kui_armada_ce_ram_read32)(void *, uint32_t, uint32_t *);
typedef enum kui_armada_ce_ram_bind_status (*kui_armada_ce_ram_read_mapped)(
    void *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t *);

/* Bound direct object aliases to word-aligned main RAM above the resident.
 * Page-table inputs have a separate stricter P1-only check below. AICA/G2, MMIO, low reserved
 * RAM, P0 aliases and P3/kernel virtual addresses never reach the callback. */
static inline bool kui_armada_ce_ram_p1(uint32_t address, uint32_t *p1) {
    const uint32_t segment=address&UINT32_C(0xe0000000);
    const uint32_t physical=address&UINT32_C(0x1fffffff);
    if((address&3u) || (segment!=UINT32_C(0x80000000) && segment!=UINT32_C(0xa0000000)) ||
       physical<UINT32_C(0x0c010000) || physical>UINT32_C(0x0cfffffc)) return false;
    *p1=physical|UINT32_C(0x80000000);
    return true;
}

/* CE's reviewed software walk reads its table pointers in their original
 * cache domain. Never turn a supplied P2 table pointer into a P1 cached read;
 * this observer supports only the proven P1 table domain. */
static inline bool kui_armada_ce_ram_table_p1(uint32_t address,uint32_t *p1) {
    return (address&UINT32_C(0xe0000000))==UINT32_C(0x80000000) &&
        kui_armada_ce_ram_p1(address,p1);
}

/* Independently authored from the exact kernel's bounded software-walk
 * contract at 0x8c01259c. Only an ordinary committed MEMBLOCK page is
 * accepted. Special positive mappings, demand paging and access-key changes
 * are not emulated. This reads physical backing; it does not grant a CE
 * virtual permission or establish sound ownership. A holder value is read
 * only for admitted normal16KiB cache mode (OCE1/OIX0/ORA0), a cached CE page
 * and matching VA/P1 index colors, or an explicitly supplied coherent
 * mapped-word reader. The ordinary wrapper has no such reader: CACHE then
 * preserves the resolved mapping without sampling it. No path here purges
 * cache. The caller must retain
 * its original CE TTB for the duration (the GD hook's SR/MMUCR changes alone
 * do not replace it). All accesses use the supplied read-only callback. */
static inline enum kui_armada_ce_ram_bind_status kui_armada_ce_ram_bind_with_reader(
    uint32_t ttb, uint32_t virtual_address, uint32_t ccr, kui_armada_ce_ram_read32 read32,
    kui_armada_ce_ram_read_mapped mapped32,void *context, struct kui_armada_ce_ram_binding *out) {
    uint32_t root,slot_at,slot,slot_p1,block_at,block,block_p1,page_at,page,dest,value,again;
    if(!out) return KUI_ARMADA_CE_RAM_BIND_ARGUMENT;
    out->physical_address=out->value=out->page_word=0;
    if(!read32 || (virtual_address&3u) || virtual_address>=UINT32_C(0x80000000))
        return KUI_ARMADA_CE_RAM_BIND_ARGUMENT;
    if(!kui_armada_ce_ram_table_p1(ttb,&root) ||
       !kui_armada_ce_ram_table_p1(root+4u*(virtual_address>>25),&slot_at))
        return KUI_ARMADA_CE_RAM_BIND_TTB;
    if(!read32(context,slot_at,&slot)) return KUI_ARMADA_CE_RAM_BIND_READ;
    if(!kui_armada_ce_ram_table_p1(slot,&slot_p1) ||
       !kui_armada_ce_ram_table_p1(slot_p1+4u*((virtual_address>>16)&0x1ffu),&block_at))
        return KUI_ARMADA_CE_RAM_BIND_SLOT;
    if(!read32(context,block_at,&block)) return KUI_ARMADA_CE_RAM_BIND_READ;
    if(!(block&UINT32_C(0x80000000)) || !kui_armada_ce_ram_table_p1(block,&block_p1) ||
       !kui_armada_ce_ram_table_p1(block_p1+12u+4u*((virtual_address>>12)&15u),&page_at))
        return KUI_ARMADA_CE_RAM_BIND_BLOCK;
    if(!read32(context,page_at,&page)) return KUI_ARMADA_CE_RAM_BIND_READ;
    if(!(page&1u)) return KUI_ARMADA_CE_RAM_BIND_PAGE;
    if(!kui_armada_ce_ram_p1((page&UINT32_C(0xfffff000))|
       (virtual_address&0xfffu)|UINT32_C(0x80000000),&dest))
        return KUI_ARMADA_CE_RAM_BIND_DESTINATION;
    const bool coherent=(ccr&0xa1u)==1u && (page&8u) &&
        !((virtual_address^dest)&0x3000u);
    value=0;
    enum kui_armada_ce_ram_bind_status sample=coherent?KUI_ARMADA_CE_RAM_BIND_OK:KUI_ARMADA_CE_RAM_BIND_CACHE;
    if(coherent && !read32(context,dest,&value)) return KUI_ARMADA_CE_RAM_BIND_READ;
    if(!coherent && mapped32 && (ccr&0xa1u)==1u) {
        sample=mapped32(context,virtual_address,dest,page,ccr,&value);
        if(sample!=KUI_ARMADA_CE_RAM_BIND_OK) value=0;
    }
    /* Fail closed if any mapping changes while the holder is sampled. */
    if(!read32(context,slot_at,&again)) return KUI_ARMADA_CE_RAM_BIND_READ;
    if(again!=slot) return KUI_ARMADA_CE_RAM_BIND_CHANGED;
    if(!read32(context,block_at,&again)) return KUI_ARMADA_CE_RAM_BIND_READ;
    if(again!=block) return KUI_ARMADA_CE_RAM_BIND_CHANGED;
    if(!read32(context,page_at,&again)) return KUI_ARMADA_CE_RAM_BIND_READ;
    if(again!=page) return KUI_ARMADA_CE_RAM_BIND_CHANGED;
    out->physical_address=dest;out->value=value;out->page_word=page;
    return sample;
}
/* Ordinary observers retain their original strict cached-color policy. Only
 * the isolated audible controller supplies a reviewed coherent fallback. */
static inline enum kui_armada_ce_ram_bind_status kui_armada_ce_ram_bind(
    uint32_t ttb,uint32_t va,uint32_t ccr,kui_armada_ce_ram_read32 read32,
    void *context,struct kui_armada_ce_ram_binding *out) {
    return kui_armada_ce_ram_bind_with_reader(ttb,va,ccr,read32,NULL,context,out);
}
#endif
