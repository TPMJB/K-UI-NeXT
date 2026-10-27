/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_TERMINAL_DISPATCH_H
#define KUI_VMU_NATIVE_TERMINAL_DISPATCH_H
#include "kui/vmu_native_exit.h"
#include "kui/vmu_native_heap.h"

/* One complete application allocator table whose four argument-only wrappers
 * share a fatal handler. The source proof establishes that handler's existing
 * path to an admitted SDK exit. Runtime permission additionally requires its
 * installed terminal patch, so none of its logging or cleanup calls can run.
 * These exact opcodes and relative layout describe a structural family, not
 * a title/address exception. Every callback table word remains checked. */
struct kui_vmu_native_terminal_dispatch_word {uint32_t offset,value,mask;};
static const uint32_t kui_vmu_native_terminal_dispatch_entries[4]={0x26u,0x7au,0x80u,0x88u};
static const struct kui_vmu_native_terminal_dispatch_word
kui_vmu_native_terminal_dispatch_words[8]={
    {0x26u,UINT32_C(0xd4126543),UINT32_MAX},
    {0x2au,UINT32_C(0x0009afe9),UINT32_MAX},
    {0x7au,UINT32_C(0xafc0d417),UINT32_MAX},
    {0x7eu,UINT32_C(0xd417e5ff),UINT32_MAX},
    {0x82u,UINT32_C(0xafbc2f56),UINT32_MAX},
    {0x86u,UINT32_C(0xd4167f04),UINT32_MAX},
    {0x8au,UINT32_C(0xafb82f56),UINT32_MAX},
    {0x8eu,UINT32_C(0x00007f04),UINT32_C(0x0000ffff)}
};
static inline bool kui_vmu_native_terminal_dispatch_span(
    const struct kui_vmu_native_exit *e,uint32_t address,uint32_t bytes) {
    uint32_t area=address&UINT32_C(0xe0000000),at=address&UINT32_C(0x1fffffff);
    uint32_t first=e->load&UINT32_C(0x1fffffff);
    uint32_t source_area=e->load&UINT32_C(0xe0000000);
    return (area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000)) &&
        (source_area==0u || source_area==UINT32_C(0x80000000) || source_area==UINT32_C(0xa0000000)) &&
        first>=UINT32_C(0x0c000000) && first<UINT32_C(0x0d000000) &&
        e->image_bytes<=UINT32_C(0x0d000000)-first && at>=first &&
        at-first<=e->image_bytes && bytes<=e->image_bytes-(at-first);
}
static inline bool kui_vmu_native_terminal_dispatch_tagged(const struct kui_vmu_native_exit *e) {
    return e && e->version==KUI_VMU_NATIVE_EXIT_VERSION && e->bytes==sizeof(*e) &&
        e->family==KUI_VMU_NATIVE_EXIT_TERMINAL_DISPATCH && e->count==3u &&
        !(e->load&3u) && !(e->terminal_dispatch.table&3u) &&
        (e->entries[2]&3u)==2u &&
        kui_vmu_native_terminal_dispatch_span(e,e->terminal_dispatch.table,16u) &&
        kui_vmu_native_terminal_dispatch_span(e,e->entries[2]&~3u,0x94u);
}
/* raw_read must supply coherent aligned words, without normalization. Only
 * already admitted main-RAM image words are requested here. */
static inline bool kui_vmu_native_terminal_dispatch_live(
    const struct kui_vmu_native_exit *e,kui_vmu_native_heap_read raw_read,void *context,
    uint32_t low_terminal) {
    if(!kui_vmu_native_terminal_dispatch_tagged(e) || !raw_read) return false;
    uint32_t common=e->entries[2],value;
    for(unsigned i=0;i<4u;i++)
        if(!raw_read(context,e->terminal_dispatch.table+4u*i,&value) ||
           value!=common+kui_vmu_native_terminal_dispatch_entries[i]) return false;
    for(unsigned i=0;i<8u;i++) {
        const struct kui_vmu_native_terminal_dispatch_word *w=&kui_vmu_native_terminal_dispatch_words[i];
        if(!raw_read(context,common+w->offset,&value) || (value&w->mask)!=w->value) return false;
    }
    uint16_t patch[6];
    if(!kui_vmu_native_patch_encode(common,low_terminal,patch)) return false;
    for(unsigned i=0;i<6u;i++) {
        uint32_t at=common+2u*i;
        if(!raw_read(context,at&~3u,&value) ||
           (uint16_t)(value>>((at&2u)*8u))!=patch[i]) return false;
    }
    return true;
}
/* Normalize only the one routing word after proving the alternate table and
 * its installed terminal hook. All other identity/free-list reads, unknown
 * pointers and uninstalled/changed callbacks retain their original values. */
static inline bool kui_vmu_native_terminal_dispatch_normalize(
    const struct kui_vmu_native_exit *e,kui_vmu_native_heap_read raw_read,void *context,
    uint32_t low_terminal,uint32_t address,uint32_t dispatch_cell,uint32_t default_table,
    uint32_t *value) {
    if(!value || !e || e->family!=KUI_VMU_NATIVE_EXIT_TERMINAL_DISPATCH ||
       address!=dispatch_cell || *value==default_table ||
       *value!=e->terminal_dispatch.table ||
       !kui_vmu_native_terminal_dispatch_live(e,raw_read,context,low_terminal)) return false;
    *value=default_table;return true;
}
#endif
