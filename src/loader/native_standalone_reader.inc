/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_standalone_reader.h"
#if KUI_NATIVE_STANDALONE_STRUCTURAL
#include "kui/retail_loader_layout.h"
#endif

static void standalone_publish(uint32_t address) {
#ifdef KUI_RETAIL_ASYNC_TEST
    extern void kui_native_standalone_test_publish(uint32_t);
    kui_native_standalone_test_publish(address);
#else
    __asm__ volatile("ocbp @%0"::"r"(address&~UINT32_C(31)):"memory");
#endif
}
void __attribute__((noipa)) kui_native_standalone_bind(void) {
    const volatile struct kui_native_standalone_source *source=
        (const void *)R.vector100;
#ifdef KUI_RETAIL_ASYNC_TEST
    extern uint32_t kui_native_standalone_test_sr(void);
    uint32_t sr=kui_native_standalone_test_sr();
#else
    uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr)::"memory");
#endif
    /* Before SDK initialization bootstrap reads remain foreground-only.
     * Once a route has been installed, a changed/reset route is never
     * overwritten: sdk_sci_ready disables our SCI delivery on loss. */
#if KUI_NATIVE_STANDALONE_STRUCTURAL
    /* The stage published the roles of the image-only SDK family proof. The
     * owner is the game's live VBR, above the firmware/resident area; the
     * source vector must lie in the loaded program. The live copy and both
     * still-default slots below decide whether that owner is ready. */
    const uint32_t owner=vbr_get();
#endif
    if((sr&UINT32_C(0x400000f0))!=UINT32_C(0x400000f0) ||
       kui_retail_hook_active!=1u ||
       R.vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX] ||
       R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX] ||
       source->magic!=KUI_NATIVE_STANDALONE_SOURCE_MAGIC ||
#if KUI_NATIVE_STANDALONE_STRUCTURAL
       (source->vector&3u) ||
       source->vector-KUI_RETAIL_EXEC_ADDRESS>KUI_RETAIL_EXEC_MAX_BYTES-96u ||
       R.vector600[KUI_NATIVE_SDK_SCI_DEFAULT_INDEX]!=source->original ||
       !kui_native_sdk_sci_vbr_address(owner) || owner<KUI_RETAIL_IP_ADDRESS ||
#else
       source->vector!=KUI_NATIVE_STANDALONE_SOURCE_VECTOR ||
       source->normal!=KUI_NATIVE_STANDALONE_SOURCE_NORMAL ||
       source->fpu!=KUI_NATIVE_STANDALONE_SOURCE_FPU ||
       source->original!=KUI_NATIVE_STANDALONE_SOURCE_DEFAULT ||
       R.vector600[KUI_NATIVE_SDK_SCI_DEFAULT_INDEX]!=source->original ||
       vbr_get()!=KUI_NATIVE_STANDALONE_SOURCE_VBR ||
#endif
       !R.shared.service.initialized || R.shared.service.pending ||
       R.shared.service.executing || e.active || e.in_irq || e.hooked ||
       e.release.armed || e.release.depth || R.shared.port.fault ||
       e.opened!=(uint32_t)R.shared.port.acquired ||
       R.shared.stream.state>1u || kui_sci_stream_busy())return;
#if !KUI_NATIVE_STANDALONE_STRUCTURAL
    const uint32_t owner=KUI_NATIVE_STANDALONE_SOURCE_VBR;
#endif
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
