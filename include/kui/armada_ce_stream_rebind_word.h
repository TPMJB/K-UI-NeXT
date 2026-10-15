/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_STREAM_REBIND_WORD_H
#define KUI_ARMADA_CE_STREAM_REBIND_WORD_H
#include "kui/armada_ce_stream_owner_word.h"

/* Worker-only full-binder value observation. The full binder admits the
 * exact shared DLL ranges before invoking this helper; expected_p1/page are
 * its mapping witnesses. Values come from the native owner's VA, including
 * shared slot1 DLL code, never from a physical alias. Caller masks IRQs and
 * verifies actual TTB/SR before and after this bounded transaction. */
static inline enum kui_armada_ce_stream_owner_status
kui_armada_ce_stream_rebind_word(uint32_t ttb,uint32_t sr,uint32_t process,
    uint32_t slot,uint32_t handle,uint32_t va,uint32_t expected_p1,
    uint32_t expected_page,kui_armada_ce_ram_read32 read_table,
    kui_armada_ce_ram_read32 read_native,void *context,
    struct kui_armada_ce_ram_binding *out) {
    const uint32_t process_base=KUI_ARMADA_CE_LIVE_PROCESS_BASE,process_stride=KUI_ARMADA_CE_LIVE_PROCESS_STRIDE;
    uint32_t current,actual_slot,actual_handle,value;
    struct kui_armada_ce_ram_binding before,after;
    if(!out) return KUI_ARMADA_CE_STREAM_OWNER_ARGUMENT;
    out->physical_address=out->value=out->page_word=0;
    const uint32_t domain=va&UINT32_C(0xfe000000);
    if(!read_table || !read_native || process<process_base ||
       process>=process_base+32u*process_stride || (process-process_base)%process_stride ||
       slot!=((1u+(process-process_base)/process_stride)<<25) ||
       !handle || handle==UINT32_MAX || (va&3u) || va>=UINT32_C(0x80000000) ||
       (domain!=slot && domain!=UINT32_C(0x02000000)) ||
       (va&UINT32_C(0x01ffffff))<UINT32_C(0x10000))
        return KUI_ARMADA_CE_STREAM_OWNER_ARGUMENT;
    if((sr&UINT32_C(0x700000f0))!=UINT32_C(0x400000f0))
        return KUI_ARMADA_CE_STREAM_OWNER_CONTEXT;
    if(!read_table(context,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,&current) ||
       !read_table(context,process+12u,&actual_slot) ||
       !read_table(context,process+8u,&actual_handle)) return KUI_ARMADA_CE_STREAM_OWNER_READ;
    if(current!=process || actual_slot!=slot || actual_handle!=handle)
        return KUI_ARMADA_CE_STREAM_OWNER_PROCESS;
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
    if(before.physical_address!=expected_p1 || before.page_word!=expected_page)
        return KUI_ARMADA_CE_STREAM_OWNER_CHANGED;
    if(!read_native(context,va,&value)) return KUI_ARMADA_CE_STREAM_OWNER_NATIVE_READ;
    mapped=kui_armada_ce_ram_bind(ttb,va,0u,read_table,context,&after);
    if(mapped!=KUI_ARMADA_CE_RAM_BIND_CACHE || before.page_word!=after.page_word ||
       before.physical_address!=after.physical_address)
        return KUI_ARMADA_CE_STREAM_OWNER_CHANGED;
    if(!read_table(context,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,&current) ||
       !read_table(context,process+12u,&actual_slot) ||
       !read_table(context,process+8u,&actual_handle)) return KUI_ARMADA_CE_STREAM_OWNER_READ;
    if(current!=process || actual_slot!=slot || actual_handle!=handle)
        return KUI_ARMADA_CE_STREAM_OWNER_CHANGED;
    *out=after;out->value=value;return KUI_ARMADA_CE_STREAM_OWNER_OK;
}
#endif
