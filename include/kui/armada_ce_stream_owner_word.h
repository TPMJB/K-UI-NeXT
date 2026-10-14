/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_STREAM_OWNER_WORD_H
#define KUI_ARMADA_CE_STREAM_OWNER_WORD_H
#include "kui/armada_ce_ram_bind.h"

enum kui_armada_ce_stream_owner_status {
    KUI_ARMADA_CE_STREAM_OWNER_OK,
    KUI_ARMADA_CE_STREAM_OWNER_ARGUMENT,
    KUI_ARMADA_CE_STREAM_OWNER_CONTEXT,
    KUI_ARMADA_CE_STREAM_OWNER_READ,
    KUI_ARMADA_CE_STREAM_OWNER_PROCESS,
    KUI_ARMADA_CE_STREAM_OWNER_TTB,
    KUI_ARMADA_CE_STREAM_OWNER_SLOT,
    KUI_ARMADA_CE_STREAM_OWNER_BLOCK,
    KUI_ARMADA_CE_STREAM_OWNER_PAGE,
    KUI_ARMADA_CE_STREAM_OWNER_DESTINATION,
    KUI_ARMADA_CE_STREAM_OWNER_CHANGED,
    KUI_ARMADA_CE_STREAM_OWNER_NATIVE_READ
};

/* Comparisons and division legitimately change SH T/Q/M. Stability here
 * concerns privileged mode, register bank, BL and the IRQ mask. The caller
 * still restores its entire saved SR after the bounded transaction. */
static inline bool kui_armada_ce_stream_owner_sr_stable(uint32_t before,uint32_t after) {
    return ((before^after)&UINT32_C(0x700000f0))==0u;
}

/* Retained COM references and the exact admitted owner context permit a
 * native virtual read. The physical mapping remains a stability witness;
 * it is never an alias from which this helper samples the object's value.
 * Caller MUST mask IRQs for the entire bounded transaction, retain this TTB
 * and the native owner's access keys, and recheck actual TTB/SR before
 * restoring IRQs. No allocation, API, cache array/maintenance or TLB write
 * occurs here. read_table receives only canonical P1 metadata addresses;
 * read_native receives only the admitted explicit owner VA. */
#ifndef KUI_ARMADA_CE_STREAM_OWNER_WORD_SCOPE
#define KUI_ARMADA_CE_STREAM_OWNER_WORD_SCOPE static inline
#endif
KUI_ARMADA_CE_STREAM_OWNER_WORD_SCOPE enum kui_armada_ce_stream_owner_status
kui_armada_ce_stream_owner_word(uint32_t ttb,uint32_t sr,uint32_t process,
    uint32_t slot,uint32_t handle,uint32_t va,kui_armada_ce_ram_read32 read_table,
    kui_armada_ce_ram_read32 read_native,void *context,
    struct kui_armada_ce_ram_binding *out) {
    const uint32_t process_base=UINT32_C(0x8c14e6e0),process_stride=UINT32_C(0x9c);
    uint32_t current,actual_slot,actual_handle,value;
    struct kui_armada_ce_ram_binding before,after;
    if(!out) return KUI_ARMADA_CE_STREAM_OWNER_ARGUMENT;
    out->physical_address=out->value=out->page_word=0;
    if(!read_table || !read_native || process<process_base ||
       process>=process_base+32u*process_stride || (process-process_base)%process_stride ||
       slot!=((1u+(process-process_base)/process_stride)<<25) ||
       !handle || handle==UINT32_MAX || (va&3u) || va>=UINT32_C(0x80000000) ||
       (va&UINT32_C(0xfe000000))!=slot || (va&UINT32_C(0x01ffffff))<UINT32_C(0x10000))
        return KUI_ARMADA_CE_STREAM_OWNER_ARGUMENT;
    /* MD1, BL/RB0, IRQs masked. FD and other thread state are retained. */
    if((sr&UINT32_C(0x700000f0))!=UINT32_C(0x400000f0))
        return KUI_ARMADA_CE_STREAM_OWNER_CONTEXT;
    if(!read_table(context,UINT32_C(0x8c145890),&current) ||
       !read_table(context,process+12u,&actual_slot) ||
       !read_table(context,process+8u,&actual_handle)) return KUI_ARMADA_CE_STREAM_OWNER_READ;
    if(current!=process || actual_slot!=slot || actual_handle!=handle)
        return KUI_ARMADA_CE_STREAM_OWNER_PROCESS;
    /* CCR0 deliberately requests the established walk's mapping-only result:
     * it returns CACHE with a complete stable mapping and makes no physical
     * value read. The native callback below owns value observation instead. */
    enum kui_armada_ce_ram_bind_status mapped=kui_armada_ce_ram_bind(
        ttb,va,0u,read_table,context,&before);
    if(mapped!=KUI_ARMADA_CE_RAM_BIND_CACHE) {
        switch(mapped) {
        case KUI_ARMADA_CE_RAM_BIND_TTB: return KUI_ARMADA_CE_STREAM_OWNER_TTB;
        case KUI_ARMADA_CE_RAM_BIND_SLOT: return KUI_ARMADA_CE_STREAM_OWNER_SLOT;
        case KUI_ARMADA_CE_RAM_BIND_BLOCK: return KUI_ARMADA_CE_STREAM_OWNER_BLOCK;
        case KUI_ARMADA_CE_RAM_BIND_PAGE: return KUI_ARMADA_CE_STREAM_OWNER_PAGE;
        case KUI_ARMADA_CE_RAM_BIND_DESTINATION: return KUI_ARMADA_CE_STREAM_OWNER_DESTINATION;
        case KUI_ARMADA_CE_RAM_BIND_CHANGED: return KUI_ARMADA_CE_STREAM_OWNER_CHANGED;
        default: return KUI_ARMADA_CE_STREAM_OWNER_READ;
        }
    }
    if(!read_native(context,va,&value)) return KUI_ARMADA_CE_STREAM_OWNER_NATIVE_READ;
    mapped=kui_armada_ce_ram_bind(ttb,va,0u,read_table,context,&after);
    if(mapped!=KUI_ARMADA_CE_RAM_BIND_CACHE || before.page_word!=after.page_word ||
       before.physical_address!=after.physical_address)
        return KUI_ARMADA_CE_STREAM_OWNER_CHANGED;
    if(!read_table(context,UINT32_C(0x8c145890),&current) ||
       !read_table(context,process+12u,&actual_slot) ||
       !read_table(context,process+8u,&actual_handle)) return KUI_ARMADA_CE_STREAM_OWNER_READ;
    if(current!=process || actual_slot!=slot || actual_handle!=handle)
        return KUI_ARMADA_CE_STREAM_OWNER_CHANGED;
    *out=after;out->value=value;return KUI_ARMADA_CE_STREAM_OWNER_OK;
}
#endif
