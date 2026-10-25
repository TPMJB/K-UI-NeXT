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
        ".set __kui_native_sdk_sci_route_stage,1\n");

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
       exports.region_storage>KUI_RETAIL_RESIDENT_LIMIT-4u ||
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
    struct kui_native_sdk_sci_route_plan plan;
    int admitted=kui_native_sdk_sci_route_admit((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
        exec_bytes,KUI_RETAIL_EXEC_ADDRESS,native_driver.image_sha256,vbr,sr,exports.callback,
        native_read,NULL,&plan);
    if(admitted!=1)native_report(10u,admitted==0?6u:7u);
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
