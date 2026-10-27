/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_standalone_reader.h"

static void standalone_publish(uint32_t address) {
    __asm__ volatile("ocbp @%0"::"r"(address&~UINT32_C(31)):"memory");
}
void __attribute__((noipa)) kui_native_standalone_bind(void) {
    const volatile struct kui_native_standalone_source *source=
        (const void *)R.vector100;
    uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr)::"memory");
    /* Before SDK initialization bootstrap reads remain foreground-only.
     * Once a route has been installed, a changed/reset route is never
     * overwritten: sdk_sci_ready disables our SCI delivery on loss. */
    if((sr&UINT32_C(0x400000f0))!=UINT32_C(0x400000f0) ||
       kui_retail_hook_active!=1u ||
       R.vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX] ||
       R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX] ||
       source->magic!=KUI_NATIVE_STANDALONE_SOURCE_MAGIC ||
       source->vector!=KUI_NATIVE_STANDALONE_SOURCE_VECTOR ||
       source->normal!=KUI_NATIVE_STANDALONE_SOURCE_NORMAL ||
       source->fpu!=KUI_NATIVE_STANDALONE_SOURCE_FPU ||
       source->original!=KUI_NATIVE_STANDALONE_SOURCE_DEFAULT ||
       R.vector600[KUI_NATIVE_SDK_SCI_DEFAULT_INDEX]!=source->original ||
       vbr_get()!=KUI_NATIVE_STANDALONE_SOURCE_VBR ||
       !R.shared.service.initialized || R.shared.service.pending ||
       R.shared.service.executing || e.active || e.in_irq || e.hooked ||
       e.release.armed || e.release.depth || R.shared.port.fault ||
       e.opened!=(uint32_t)R.shared.port.acquired ||
       R.shared.stream.state>1u || kui_sci_stream_busy())return;
    const uint32_t owner=KUI_NATIVE_STANDALONE_SOURCE_VBR;
    const volatile uint32_t *original=(const void *)(uintptr_t)source->vector;
    for(unsigned word=0u;word<24u;++word) {
        if(word==20u)continue; /* Copied vector's original mutable R0 scratch. */
        uint32_t live=rd32(owner+UINT32_C(0x600)+4u*word);
        if(word==22u) {
            if((live!=source->normal && live!=source->fpu) ||
               (original[word]!=source->normal && original[word]!=source->fpu))return;
        } else if(live!=original[word])return;
    }
    const uint32_t slots=owner+KUI_NATIVE_SDK_SCI_ERI_SLOT_OFFSET;
    if(rd32(slots)!=source->original || rd32(slots+4u)!=source->original)return;
    const uint32_t callback=(uint32_t)(uintptr_t)kui_retail_sdk_sci_entry;
    R.vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX]=owner;
    standalone_publish((uint32_t)(uintptr_t)&R.vector600[KUI_NATIVE_SDK_SCI_DEFAULT_INDEX]);
    *(volatile uint32_t *)(uintptr_t)slots=callback;
    *(volatile uint32_t *)(uintptr_t)(slots+4u)=callback;
    standalone_publish(slots);standalone_publish(slots+4u);
    if(rd32(slots|UINT32_C(0x20000000))!=callback ||
       rd32((slots+4u)|UINT32_C(0x20000000))!=callback)return;
    R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]=KUI_NATIVE_SDK_SCI_COOKIE;
    standalone_publish((uint32_t)(uintptr_t)&R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]);
}
