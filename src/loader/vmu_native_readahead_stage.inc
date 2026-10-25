/* SPDX-License-Identifier: GPL-3.0-only */
/* Cold installation only. Retain owned low operations and auxiliary lease
 * pointers by value; no pointer into this temporary stage survives. */
#include "kui/native_readahead.h"
#if !KUI_NATIVE_READAHEAD || !KUI_NATIVE_SDK_SCI_ROUTE || !KUI_VMU_NATIVE_BACKGROUND || !KUI_VMU_NATIVE_CACHED_CODE
#error Read-ahead installation requires its isolated cached native SDK SCI route
#endif
__asm__(".global __kui_native_readahead_stage\n"
        ".set __kui_native_readahead_stage,1\n"
        ".section .rodata.native_ra_aux,\"a\"\n.balign 32\n"
        ".global _native_ra_aux_blob_start\n_native_ra_aux_blob_start:\n.incbin \"" KUI_NATIVE_RA_AUX_BIN "\"\n"
        ".global _native_ra_aux_blob_end\n_native_ra_aux_blob_end:\n.balign 4\n"
        ".global _native_ra_aux_reloc_start\n_native_ra_aux_reloc_start:\n.incbin \"" KUI_NATIVE_RA_AUX_RELOC "\"\n"
        ".global _native_ra_aux_reloc_end\n_native_ra_aux_reloc_end:\n.previous\n");
extern const uint8_t native_ra_aux_blob_start[] __asm__("_native_ra_aux_blob_start");
extern const uint8_t native_ra_aux_blob_end[] __asm__("_native_ra_aux_blob_end");
extern const uint8_t native_ra_aux_reloc_start[] __asm__("_native_ra_aux_reloc_start");
extern const uint8_t native_ra_aux_reloc_end[] __asm__("_native_ra_aux_reloc_end");

static bool native_ra_code(uint32_t entry,uint32_t load,uint32_t bytes) {
    return !(entry&1u) && entry>=load+UINT32_C(0x1000) && entry<load+bytes;
}
static bool native_ra_stack_valid(uint32_t bottom,uint32_t top) {
    const volatile uint32_t *low=(const void *)(uintptr_t)bottom;
    const volatile uint32_t *high=(const void *)(uintptr_t)(top-16u);
    for(unsigned i=0;i<4u;i++)
        if(low[i]!=UINT32_C(0x4b524147) || high[i]!=UINT32_C(0x4b524147))return false;
    return true;
}
static void __attribute__((noipa)) native_readahead_bind(uint32_t payload) {
    struct kui_native_ra_low_ops ops;uint32_t descriptor,sr;
    if(resident_bytes<KUI_NATIVE_RA_LOW_ENTRY_OFFSET+4u || resident_bytes<sizeof(ops))
        native_report(11u,1u);
    memcpy(&descriptor,resident_blob+KUI_NATIVE_RA_LOW_ENTRY_OFFSET,4u);
    if((descriptor&3u) || descriptor<KUI_RETAIL_RESIDENT_ADDRESS+44u ||
       descriptor-KUI_RETAIL_RESIDENT_ADDRESS>resident_bytes-sizeof(ops))
        native_report(11u,2u);
    memcpy(&ops,resident_blob+(descriptor-KUI_RETAIL_RESIDENT_ADDRESS),sizeof(ops));
    uint32_t storage=(uint32_t)(uintptr_t)ops.region_storage;
    if(ops.magic!=KUI_NATIVE_RA_OPS_MAGIC || ops.version!=KUI_NATIVE_RA_VERSION ||
       ops.bytes!=sizeof(ops) || (storage&3u) ||
       storage<KUI_RETAIL_RESIDENT_ADDRESS+resident_bytes ||
       resident_limit!=KUI_RETAIL_LOW_ASYNC_LIMIT || storage>resident_limit-4u ||
       native_word((KUI_RETAIL_RESIDENT_ADDRESS+KUI_NATIVE_RA_LOW_ENTRY_OFFSET)|UINT32_C(0x20000000))!=descriptor ||
       memcmp((const void *)(uintptr_t)(descriptor|UINT32_C(0x20000000)),&ops,sizeof(ops)))
        native_report(11u,3u);
    for(unsigned i=4u;i<sizeof(ops)/4u;i++) {
        uint32_t entry;memcpy(&entry,(const uint8_t *)&ops+4u*i,sizeof(entry));
        if((entry&1u) || entry<KUI_RETAIL_RESIDENT_ADDRESS+44u ||
           entry-KUI_RETAIL_RESIDENT_ADDRESS>=resident_bytes)
            native_report(11u,4u);
    }
    uint32_t lease=(payload&UINT32_C(0x1fffffff))|UINT32_C(0x80000000);
    uint32_t address=lease+KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET;
    if(KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES!=KUI_NATIVE_RA_LEASE_BYTES ||
       !kui_vmu_native_background_region_address(payload,address) ||
       native_word(storage)!=address || control()->status!=KUI_VMU_OBS_BOOT_INSTALLED ||
       control()->lease!=(payload&UINT32_C(0x1fffffff)))native_report(11u,5u);
    kui_retail_async_region_type *r=(void *)(uintptr_t)address;
    __asm__ volatile("stc sr,%0":"=r"(sr)::"memory");
    if(((sr>>4)&15u)!=15u || !r->shared.service.initialized ||
       r->shared.service.pending || r->shared.service.executing ||
       r->engine.active || r->engine.in_irq || r->engine.hooked ||
       r->engine.release.armed || r->engine.release.depth || r->engine.native_loan_state ||
       r->shared.stream.state>1u || r->shared.port.fault ||
       r->engine.opened!=(uint32_t)r->shared.port.acquired ||
       r->vector600[KUI_NATIVE_RA_ENTRY_INDEX] || r->vector600[KUI_NATIVE_RA_CONTEXT_INDEX] ||
       r->vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]!=KUI_NATIVE_SDK_SCI_COOKIE)
        native_report(11u,6u);
    uint32_t bytes=(uint32_t)(native_ra_aux_blob_end-native_ra_aux_blob_start);
    uint32_t reloc_bytes=(uint32_t)(native_ra_aux_reloc_end-native_ra_aux_reloc_start);
    const uint32_t *reloc=(const void *)native_ra_aux_reloc_start;
    if(bytes<UINT32_C(0x1002) || bytes>UINT32_C(0x5000) ||
       reloc_bytes<KUI_VMU_NATIVE_RELOCATE_HEADER_BYTES ||
       reloc[5]!=KUI_NATIVE_RA_AUX_SPAN || reloc[6]!=UINT32_C(0x8c421000))
        native_report(11u,7u);
    uint32_t load=lease+KUI_NATIVE_RA_AUX_OFFSET,dest=load+UINT32_C(0x20000000);
    /* Retire stale cache lines before P2 initializes either newly owned
     * arena. Cache blocks remain invalid until a CRC-validated producer copy. */
    for(uint32_t at=load;at<load+KUI_NATIVE_RA_AUX_SPAN;at+=32u)
        __asm__ volatile("ocbp @%0"::"r"(at):"memory");
    for(uint32_t at=lease+KUI_NATIVE_RA_DATA_OFFSET;
        at<lease+KUI_NATIVE_RA_DATA_OFFSET+KUI_NATIVE_RA_DATA_BYTES;at+=32u)
        __asm__ volatile("ocbp @%0"::"r"(at):"memory");
    int relocated=kui_vmu_native_relocate(native_ra_aux_blob_start,bytes,
        native_ra_aux_reloc_start,reloc_bytes,(void *)(uintptr_t)dest,
        KUI_NATIVE_RA_AUX_SPAN,load);
    if(relocated)native_report(11u,8u);
    memset((void *)(uintptr_t)(dest+bytes),0,KUI_NATIVE_RA_AUX_SPAN-bytes);
    const struct kui_native_ra_exports *e=(const void *)(uintptr_t)dest;
    uint32_t context=(uint32_t)(uintptr_t)e->context;
    if(e->magic!=KUI_NATIVE_RA_AUX_MAGIC || e->version!=KUI_NATIVE_RA_VERSION ||
       e->bytes!=sizeof(*e) ||
       !native_ra_code((uint32_t)(uintptr_t)e->visit,load,bytes) ||
       !native_ra_code((uint32_t)(uintptr_t)e->init,load,bytes) ||
       e->image_end!=load+bytes || (context&31u) ||
       context<dest+sizeof(*e) || context>dest+UINT32_C(0x1000)-sizeof(struct kui_native_ra_public) ||
       e->stack_bottom!=lease+UINT32_C(0x20000000)+KUI_NATIVE_RA_STACK_OFFSET ||
       e->stack_top!=lease+UINT32_C(0x20000000)+KUI_NATIVE_RA_STACK_END ||
       e->memory_end!=e->stack_top)native_report(11u,9u);
    for(unsigned i=0;i<6u;i++)if(e->reserved[i])native_report(11u,9u);
    for(unsigned i=0;i<4u;i++) {
        ((volatile uint32_t *)(uintptr_t)e->stack_bottom)[i]=UINT32_C(0x4b524147);
        ((volatile uint32_t *)(uintptr_t)(e->stack_top-16u))[i]=UINT32_C(0x4b524147);
    }
    kui_native_passive_publish(load,load+bytes);
    struct kui_native_ra_config cfg={KUI_NATIVE_RA_VERSION,sizeof(cfg),r,
        (const void *)(uintptr_t)descriptor,
        (void *)(uintptr_t)(lease+UINT32_C(0x20000000)+KUI_NATIVE_RA_DATA_OFFSET),
        KUI_NATIVE_RA_DATA_BYTES,lease&UINT32_C(0x1fffffff),KUI_NATIVE_RA_LEASE_BYTES};
    uint32_t unused=0;
    if(kui_vmu_native_worker_call((uint32_t)(uintptr_t)e->init,e->stack_top-32u,
       (const struct kui_vmu_native_worker_config *)(const void *)&cfg,
       (struct kui_vmu_native_worker_result *)(void *)&unused) ||
       !native_ra_stack_valid(e->stack_bottom,e->stack_top) ||
       ((const volatile struct kui_native_ra_public *)(uintptr_t)context)->busy)
        native_report(11u,10u);
    /* Publish context first. Entry zero is the disabled state throughout
     * relocation and initialization, so no partially bound call is possible. */
    r->vector600[KUI_NATIVE_RA_CONTEXT_INDEX]=context;
    native_publish(address+0x500u+4u*KUI_NATIVE_RA_CONTEXT_INDEX);
    if(native_word((address+0x500u+4u*KUI_NATIVE_RA_CONTEXT_INDEX)|UINT32_C(0x20000000))!=context)
        native_report(11u,11u);
    r->vector600[KUI_NATIVE_RA_ENTRY_INDEX]=(uint32_t)(uintptr_t)e->visit;
    native_publish(address+0x500u+4u*KUI_NATIVE_RA_ENTRY_INDEX);
    if(native_word((address+0x500u+4u*KUI_NATIVE_RA_ENTRY_INDEX)|UINT32_C(0x20000000))!=
       (uint32_t)(uintptr_t)e->visit)native_report(11u,12u);
    retail_display_line("BOUNDED READ AHEAD READY / 192 KIB CACHE");
}
