/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_standalone_admission.h"
#include "kui/native_standalone_reader.h"
#include "kui/native_sdk_sci_runtime.h"
#include "standalone_symbols.h"
__asm__(".global __kui_native_standalone_reader_stage\n"
        ".set __kui_native_standalone_reader_stage,1\n"
        ".global __kui_native_standalone_doa2_stage\n"
        ".set __kui_native_standalone_doa2_stage,1\n");
static uint32_t standalone_word(uint32_t address) {
    return *(volatile const uint32_t *)(uintptr_t)address;
}
static void __attribute__((noipa)) standalone_source_check(void) {
    if(!kui_native_standalone_image_admit((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,exec_bytes))
        stopped("STANDALONE READER NEEDS MATCHED DOA2",exec_bytes);
}
/* The relay has already restored/verified the original executable and the
 * complete low code. Both caches are off and IRQs masked. The route remains
 * unarmed; the later binder separately requires complete SCI/GD quiescence.
 * Only game-code roles survive; no temporary stage pointer is retained. */
static void __attribute__((noipa)) standalone_source_publish(void) {
    uint32_t sr,ccr;
    __asm__ volatile("stc sr,%0":"=r"(sr)::"memory");
    ccr=standalone_word(UINT32_C(0xff00001c));
    const uint32_t low=KUI_RETAIL_RESIDENT_ADDRESS,region=KUI_STANDALONE_REGION;
    const struct kui_native_sdk_sci_runtime_exports expected={
        KUI_NATIVE_SDK_SCI_DESCRIPTOR_MAGIC,KUI_NATIVE_SDK_SCI_DESCRIPTOR_VERSION,
        KUI_NATIVE_SDK_SCI_DESCRIPTOR_BYTES,KUI_STANDALONE_CALLBACK,KUI_STANDALONE_REGION_POINTER};
    if((sr&UINT32_C(0x400000f0))!=UINT32_C(0x400000f0) || (ccr&UINT32_C(0x101)) ||
       resident_limit!=KUI_RETAIL_LOW_ASYNC_LIMIT ||
       KUI_STANDALONE_BSS_END>KUI_RETAIL_LOW_ASYNC_LIMIT || region<low+resident_bytes ||
       region>KUI_STANDALONE_BSS_END-UINT32_C(0x760) ||
       KUI_STANDALONE_EXPORTS<low+40u ||
       KUI_STANDALONE_EXPORTS+sizeof(expected)>low+resident_bytes ||
       standalone_word(low+KUI_NATIVE_SDK_SCI_DESCRIPTOR_ENTRY_OFFSET)!=KUI_STANDALONE_EXPORTS ||
       memcmp((const void *)(uintptr_t)KUI_STANDALONE_EXPORTS,&expected,sizeof(expected)) ||
       standalone_word(KUI_STANDALONE_REGION_POINTER)!=region)
        stopped("STANDALONE SOURCE PUBLICATION REFUSED",1u);
    volatile uint32_t *out=(volatile void *)(uintptr_t)(region|UINT32_C(0x20000000));
    for(unsigned i=0u;i<5u;i++)if(out[i])stopped("STANDALONE SOURCE STATE CHANGED",i);
    if(out[KUI_NATIVE_SDK_SCI_COOKIE_OFFSET/4u] ||
       out[KUI_NATIVE_SDK_SCI_DEFAULT_OFFSET/4u] ||
       out[KUI_NATIVE_SDK_SCI_VBR_OFFSET/4u])
        stopped("STANDALONE ROUTE ALREADY OWNED",2u);
    out[0]=KUI_NATIVE_STANDALONE_SOURCE_VECTOR;
    out[1]=KUI_NATIVE_STANDALONE_SOURCE_NORMAL;
    out[2]=KUI_NATIVE_STANDALONE_SOURCE_FPU;
    out[3]=KUI_NATIVE_STANDALONE_SOURCE_DEFAULT;
    out[KUI_NATIVE_SDK_SCI_DEFAULT_OFFSET/4u]=KUI_NATIVE_STANDALONE_SOURCE_DEFAULT;
    __asm__ volatile("":::"memory");
    out[4]=KUI_NATIVE_STANDALONE_SOURCE_MAGIC;
    if(out[4]!=KUI_NATIVE_STANDALONE_SOURCE_MAGIC ||
       out[KUI_NATIVE_SDK_SCI_DEFAULT_OFFSET/4u]!=KUI_NATIVE_STANDALONE_SOURCE_DEFAULT)
        stopped("STANDALONE SOURCE READBACK FAILED",3u);
}
