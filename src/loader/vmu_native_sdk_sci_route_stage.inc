/* SPDX-License-Identifier: GPL-3.0-only */
/* Cold, exact-owner publication. No stage pointer survives this function. */
#include "kui/native_sdk_sci_route.h"
#include "kui/native_sdk_sci_runtime.h"
/* Use the checked console layout without enabling an asynchronous storage
 * implementation in the temporary stage translation unit. */
#ifndef KUI_RETAIL_ASYNC
#define KUI_SCI_ROUTE_STAGE_LAYOUT 1
#define KUI_RETAIL_ASYNC 1
#define KUI_RETAIL_GD_ASYNC 1
#define KUI_RETAIL_TRANSPORT 1
#define KUI_SCI_SD_NO_BLOCK 1
#endif
#include "retail_async.h"
#ifdef KUI_SCI_ROUTE_STAGE_LAYOUT
#undef KUI_RETAIL_ASYNC
#undef KUI_RETAIL_GD_ASYNC
#undef KUI_RETAIL_TRANSPORT
#undef KUI_SCI_SD_NO_BLOCK
#undef KUI_SCI_ROUTE_STAGE_LAYOUT
#endif
__asm__(".global __kui_native_sdk_sci_route_stage\n"
        ".set __kui_native_sdk_sci_route_stage,1\n"
        ".global __kui_native_sdk_sci_multi_owner_stage\n"
        ".set __kui_native_sdk_sci_multi_owner_stage,1\n");
#if KUI_NATIVE_SDK_SCI_STRUCTURAL
__asm__(".global __kui_native_sdk_sci_structural_stage\n"
        ".set __kui_native_sdk_sci_structural_stage,1\n");
/* Admission may discover a relocated SDK table, but no part of that live
 * routing region may occupy loader code, its low stack, or the new lease. */
static bool native_sdk_sci_route_disjoint(uint32_t vbr,uint32_t payload) {
    if(!kui_native_sdk_sci_vbr_address(vbr))return false;
    uint32_t begin=vbr&UINT32_C(0x1fffffff),end=begin+KUI_NATIVE_SDK_SCI_VBR_SPAN;
    uint32_t lease=payload&UINT32_C(0x1fffffff);
    uint32_t low=KUI_RETAIL_RESIDENT_ADDRESS&UINT32_C(0x1fffffff);
    uint32_t low_end=KUI_RETAIL_HOOK_STACK&UINT32_C(0x1fffffff);
    uint32_t stage=KUI_RETAIL_STAGE_ADDRESS&UINT32_C(0x1fffffff);
    uint32_t stage_end=(uint32_t)(uintptr_t)__retail_stage_bss_end&UINT32_C(0x1fffffff);
    return !(begin<lease+KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES && lease-32u<end) &&
        !(begin<low_end && low<end) && stage_end>=stage &&
        !(begin<stage_end && stage<end);
}
#endif

static void __attribute__((noipa)) native_sdk_sci_route_bind(uint32_t payload) {
    struct kui_native_sdk_sci_runtime_exports exports;
    uint32_t descriptor,sr,vbr;
    if(resident_bytes<KUI_NATIVE_SDK_SCI_DESCRIPTOR_ENTRY_OFFSET+4u ||
       resident_bytes<sizeof(exports))native_report(10u,1u);
    memcpy(&descriptor,resident_blob+KUI_NATIVE_SDK_SCI_DESCRIPTOR_ENTRY_OFFSET,4u);
    if((descriptor&3u) || descriptor<KUI_RETAIL_RESIDENT_ADDRESS+40u ||
       descriptor-KUI_RETAIL_RESIDENT_ADDRESS>resident_bytes-sizeof(exports))
        native_report(10u,2u);
    memcpy(&exports,resident_blob+(descriptor-KUI_RETAIL_RESIDENT_ADDRESS),sizeof(exports));
    if(exports.magic!=KUI_NATIVE_SDK_SCI_DESCRIPTOR_MAGIC ||
       exports.version!=KUI_NATIVE_SDK_SCI_DESCRIPTOR_VERSION ||
       exports.bytes!=sizeof(exports) || (exports.callback&1u) ||
       exports.callback<KUI_RETAIL_RESIDENT_ADDRESS+40u ||
       exports.callback-KUI_RETAIL_RESIDENT_ADDRESS>=resident_bytes ||
       (exports.region_storage&3u) ||
       exports.region_storage<KUI_RETAIL_RESIDENT_ADDRESS+resident_bytes ||
       /* The temporary stage is built without KUI_RETAIL_ASYNC. Its generic
        * RESIDENT_LIMIT therefore names the standard reader, not this linked
        * background resident. Use the already selected reader's bound and
        * require the exact existing low asynchronous reservation. */
       resident_limit!=KUI_RETAIL_LOW_ASYNC_LIMIT ||
       exports.region_storage>resident_limit-4u ||
       memcmp((const void *)(uintptr_t)descriptor,&exports,sizeof(exports)))
        native_report(10u,3u);
    uint32_t address=((payload&UINT32_C(0x1fffffff))|UINT32_C(0x80000000))+
        KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET;
    if(!kui_vmu_native_background_region_address(payload,address) ||
       native_word(exports.region_storage)!=address ||
       control()->status!=KUI_VMU_OBS_BOOT_INSTALLED ||
       control()->lease!=(payload&UINT32_C(0x1fffffff)))native_report(10u,4u);
    kui_retail_async_region_type *r=(void *)(uintptr_t)address;
    /* The low map binder already proved hook_active==0. Between it and here
     * the installer runs straight through under IMASK15, with SCI masked and
     * no active game/receiver/loan. Refuse any unexpected retained state. */
    if(!r->shared.service.initialized || r->shared.service.pending ||
       r->shared.service.executing || r->engine.active || r->engine.in_irq ||
       r->engine.hooked || r->engine.release.armed || r->engine.release.depth ||
       r->engine.native_loan_state || r->shared.stream.state>1u ||
       r->shared.port.fault ||
       r->engine.opened!=(uint32_t)r->shared.port.acquired ||
       r->vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX] ||
       r->vector600[KUI_NATIVE_SDK_SCI_DEFAULT_INDEX] ||
       r->vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX])native_report(10u,5u);
    __asm__ volatile("stc sr,%0":"=r"(sr)::"memory");
    __asm__ volatile("stc vbr,%0":"=r"(vbr)::"memory");
#if KUI_NATIVE_SDK_SCI_STRUCTURAL
    if(!native_sdk_sci_route_disjoint(vbr,payload))native_report(10u,7u);
#endif
    struct kui_native_sdk_sci_route_plan plan;
    int admitted=kui_native_sdk_sci_route_admit((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
        exec_bytes,KUI_RETAIL_EXEC_ADDRESS,native_driver.image_sha256,vbr,sr,exports.callback,
        native_read,NULL,&plan);
#if KUI_NATIVE_SDK_SCI_STRUCTURAL
    /* A known original with changed context must retain its exact refusal.
     * Only an unsupported identity may attempt the separately proved family. */
    if(admitted==0)admitted=kui_native_sdk_sci_route_discover(
        (const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,exec_bytes,KUI_RETAIL_EXEC_ADDRESS,
        vbr,sr,exports.callback,native_read,NULL,&plan);
#endif
    if(admitted!=1)native_report(10u,admitted==0?6u:7u);
    /* Both independently fingerprinted owners use this exact SDK table
     * layout. The unchanged low readiness predicate checks these same
     * addresses; a future owner with another layout must not be published
     * merely because its cold admission produced a well-formed plan. */
    if(plan.version!=KUI_NATIVE_SDK_SCI_ROUTE_VERSION || plan.bytes!=sizeof(plan) ||
       plan.vbr!=
#if KUI_NATIVE_SDK_SCI_STRUCTURAL
           vbr ||
       plan.eri_slot!=vbr+KUI_NATIVE_SDK_SCI_ERI_SLOT_OFFSET ||
       plan.rxi_slot!=vbr+KUI_NATIVE_SDK_SCI_RXI_SLOT_OFFSET ||
#else
           KUI_NATIVE_SDK_SCI_OWNER_VBR ||
       plan.eri_slot!=KUI_NATIVE_SDK_SCI_OWNER_VBR+KUI_NATIVE_SDK_SCI_ERI_SLOT_OFFSET ||
       plan.rxi_slot!=KUI_NATIVE_SDK_SCI_OWNER_VBR+KUI_NATIVE_SDK_SCI_RXI_SLOT_OFFSET ||
#endif
       plan.callback!=exports.callback)native_report(10u,7u);
    /* Preserve exact old SCI callback and original VBR first. The cookie is
     * still zero, so neither an IRQ nor a later GD entry can treat a partial
     * table publication as a live route. Other callback slots are untouched. */
    r->vector600[KUI_NATIVE_SDK_SCI_DEFAULT_INDEX]=plan.previous;
    r->vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX]=plan.vbr;
    native_publish(address+KUI_NATIVE_SDK_SCI_DEFAULT_OFFSET);
    *(volatile uint32_t *)(uintptr_t)plan.eri_slot=plan.callback;
    *(volatile uint32_t *)(uintptr_t)plan.rxi_slot=plan.callback;
    native_publish(plan.eri_slot);native_publish(plan.rxi_slot);
    if(native_word(plan.eri_slot|UINT32_C(0x20000000))!=plan.callback ||
       native_word(plan.rxi_slot|UINT32_C(0x20000000))!=plan.callback ||
       native_word((address+KUI_NATIVE_SDK_SCI_DEFAULT_OFFSET)|UINT32_C(0x20000000))!=plan.previous ||
       native_word((address+KUI_NATIVE_SDK_SCI_VBR_OFFSET)|UINT32_C(0x20000000))!=plan.vbr)
        native_report(10u,8u);
    r->vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]=KUI_NATIVE_SDK_SCI_COOKIE;
    native_publish(address+KUI_NATIVE_SDK_SCI_COOKIE_OFFSET);
    if(native_word((address+KUI_NATIVE_SDK_SCI_COOKIE_OFFSET)|UINT32_C(0x20000000))!=
       KUI_NATIVE_SDK_SCI_COOKIE)native_report(10u,9u);
    retail_display_line("SDK SCI ROUTE READY / LEVEL 1");
}
