/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_TABLE_TRACE_H
#define KUI_ARMADA_CE_TABLE_TRACE_H
#include "kui/armada_ce_ram_bind.h"

/* The kernel initializes 64 TTB address-space entries. Their roles are not
 * equivalent to 64 processes. Slot0 may alias a current process elsewhere. */
#define KUI_ARMADA_CE_TTB_SLOTS 64u
#define KUI_ARMADA_CE_EMPTY_SLOT_TABLE UINT32_C(0x8c012b0c)
struct kui_armada_ce_table_trace {
    uint32_t slot_value,block_address,block_value;
};

/* Read only the selected TTB word and its second-level table word. Preserve
 * raw unsupported block values; never dereference them. OK means only an
 * aligned negative P1 main-RAM candidate, not an initialized MEMBLOCK or a
 * sound owner. Refused/changed snapshots retain the observed raw fields.
 * Each table pointer stays in its original P1 domain; no alias conversion,
 * MEMBLOCK/page/holder read, MMU write or CE call occurs here. At most four
 * callbacks run; a refused slot still rechecks its root word for stability. */
static inline enum kui_armada_ce_ram_bind_status kui_armada_ce_trace_table(
    uint32_t ttb,uint32_t virtual_address,kui_armada_ce_ram_read32 read32,
    void *context,struct kui_armada_ce_table_trace *out) {
    uint32_t root,slot_at,slot,slot_p1,block_address,block,unused,again;
    if(!out) return KUI_ARMADA_CE_RAM_BIND_ARGUMENT;
    out->slot_value=out->block_address=out->block_value=0;
    if(!read32 || (virtual_address&3u) || virtual_address>=UINT32_C(0x80000000))
        return KUI_ARMADA_CE_RAM_BIND_ARGUMENT;
    if(!kui_armada_ce_ram_table_p1(ttb,&root) ||
       !kui_armada_ce_ram_table_p1(root+4u*(virtual_address>>25),&slot_at))
        return KUI_ARMADA_CE_RAM_BIND_TTB;
    if(!read32(context,slot_at,&slot)) return KUI_ARMADA_CE_RAM_BIND_READ;
    out->slot_value=slot;
    if(!kui_armada_ce_ram_table_p1(slot,&slot_p1) ||
       !kui_armada_ce_ram_table_p1(slot_p1+4u*((virtual_address>>16)&0x1ffu),&block_address)) {
        if(!read32(context,slot_at,&again)) return KUI_ARMADA_CE_RAM_BIND_READ;
        return again==slot?KUI_ARMADA_CE_RAM_BIND_SLOT:KUI_ARMADA_CE_RAM_BIND_CHANGED;
    }
    out->block_address=block_address;
    if(!read32(context,block_address,&block)) return KUI_ARMADA_CE_RAM_BIND_READ;
    out->block_value=block;
    if(!read32(context,slot_at,&again)) return KUI_ARMADA_CE_RAM_BIND_READ;
    if(again!=slot) return KUI_ARMADA_CE_RAM_BIND_CHANGED;
    if(!read32(context,block_address,&again)) return KUI_ARMADA_CE_RAM_BIND_READ;
    if(again!=block) return KUI_ARMADA_CE_RAM_BIND_CHANGED;
    return kui_armada_ce_ram_table_p1(block,&unused)?KUI_ARMADA_CE_RAM_BIND_OK:
        KUI_ARMADA_CE_RAM_BIND_BLOCK;
}
#endif
