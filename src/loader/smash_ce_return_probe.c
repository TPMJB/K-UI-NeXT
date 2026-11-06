/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_ce_return_probe.h"
#include "kui/smash_menu_collection.h"
#include "kui/vc2_ce_maple_profile.h"
#include "kui/retail_image.h"
#include "kui/cold_load.h"
#include <string.h>
struct kui_smash_ce_return_probe_runtime kui_smash_ce_return_probe_runtime
    __attribute__((used,section(".data.smash_ce_return_runtime")))={0};
struct kui_smash_ce_return_witness kui_smash_ce_return_probe_witness;
extern const uint8_t __smash_ce_return_begin[] __asm__("__smash_ce_return_begin");
extern const uint8_t __smash_ce_return_binary_end[] __asm__("__smash_ce_return_binary_end");
static uint32_t read32(uint32_t at) {return *(volatile const uint32_t *)(uintptr_t)at;}
static uint32_t code_crc(void) {
    const uint8_t *p=(const void *)&kui_smash_ce_return_probe_runtime;
    const uint32_t zero=0;
    uint32_t crc=kui_retail_crc32(0,__smash_ce_return_begin,(size_t)(p-__smash_ce_return_begin));
    for(unsigned i=0;i<sizeof(kui_smash_ce_return_probe_runtime)/4u;i++)crc=kui_retail_crc32(crc,&zero,4u);
    p+=sizeof(kui_smash_ce_return_probe_runtime);
    return kui_retail_crc32(crc,p,(size_t)(__smash_ce_return_binary_end-p));
}
void __attribute__((noreturn)) kui_smash_ce_return_probe_main(uint32_t command,uint32_t caller,uint32_t sr,uint32_t sp) {
    struct kui_smash_ce_return_witness *w=&kui_smash_ce_return_probe_witness;
    w->magic=KUI_SMASH_CE_RETURN_MAGIC;w->abi=KUI_SMASH_CE_RETURN_ABI;w->attempts=1u;
    w->command=command;w->caller=caller;w->entry_sr=sr;w->entry_sp=sp;
    __asm__ volatile("stc sr,%0":"=r"(w->actual_sr));
    w->refused=KUI_SMASH_CE_RETURN_UNAVAILABLE;
    struct kui_smash_ce_return_probe_runtime *r=&kui_smash_ce_return_probe_runtime;
    /* These are immutable owned BOOT1 aliases, not CE virtual pointers. No
     * caller stack, process object, packet, mailbox or save data is followed. */
    uint32_t ccr=read32(UINT32_C(0xff00001c));
    if(ccr&UINT32_C(0x80))goto parked;
    for(uint32_t at=UINT32_C(0x8c008100);at<UINT32_C(0x8c00b800);at+=32u)
        __asm__ volatile("ocbp @%0"::"r"(at):"memory");
    if(!kui_smash_menu_return_seed_valid(&r->returning) ||
       r->returning.flags!=KUI_SMASH_MENU_RETURN_CONFIGURED ||
       r->profile!=(KUI_SMASH_SWIRL_VMU?KUI_SMASH_MENU_CHILD_SWIRL:KUI_SMASH_MENU_CHILD_VC2) ||
       !r->code_crc32 || kui_smash_ce_return_probe_crc(code_crc(),r)!=r->code_crc32 ||
       r->helper_bytes<16u || r->helper_bytes>UINT32_C(0xa200) ||
       r->report_entry<UINT32_C(0xac004000) || r->report_entry>=UINT32_C(0xac007ea0) || (r->report_entry&1u) ||
       r->trace_address<UINT32_C(0xac004000) || r->trace_address>UINT32_C(0xac007ea0)-148u || (r->trace_address&3u))
        goto parked;
    w->mmucr=read32(UINT32_C(0xff000010));w->ttb=read32(UINT32_C(0xff000008));w->ccr=ccr;
    const uint32_t reset_pr=KUI_SMASH_SWIRL_VMU?UINT32_C(0x01d2190e):UINT32_C(0x01d21902);
    if(command!=1u || (caller&UINT32_C(0x81ffffff))!=reset_pr)w->refused|=KUI_SMASH_CE_RETURN_SOURCE;
    if((sr&UINT32_C(0x500000f0))!=UINT32_C(0x40000000))w->refused|=KUI_SMASH_CE_RETURN_CONTEXT;
    /* The entire immutable helper was admitted before its original CE
     * kernel ran. Recheck that exact code before interpreting source-owned
     * KData scalars. This does not authorize a high call or retirement. */
    if(kui_cold_load_crc32(0,(const void *)(uintptr_t)UINT32_C(0xacfc4000),r->helper_bytes)!=r->helper_crc32)
        w->refused|=KUI_SMASH_CE_RETURN_RUNTIME;
    else if((w->mmucr&1u) && (w->ccr&UINT32_C(0xa1))==1u) {
        w->thread=read32(KUI_VC2_CE_CURRENT_THREAD_P1);
        w->process=read32(KUI_VC2_CE_CURRENT_PROCESS_P1);
    } else w->refused|=KUI_SMASH_CE_RETURN_MMU;
    w->runtime=w->loan=w->maple=UINT32_MAX; /* No authority API was invoked. */
    w->mden_before=read32(UINT32_C(0xa05f6c14));w->mdst_before=read32(UINT32_C(0xa05f6c18));
    w->mden_after=w->mden_before;w->mdst_after=w->mdst_before;w->retired=0u;
    w->virtual_address=KUI_SMASH_SWIRL_VMU?1u:2u;
    /* Preserve the existing three-page terminal renderer and its low-stack
     * geometry. The first page now identifies this exact terminal context. */
    volatile uint32_t *trace=(void *)(uintptr_t)r->trace_address;
    const uint32_t words[16]={command,caller,sr,sp,w->actual_sr,w->mmucr,w->ttb,w->ccr,
        w->thread,w->process,r->profile,r->helper_crc32,r->raw_crc32,r->code_crc32,w->refused,UINT32_C(0x53434552)};
    for(unsigned i=0;i<16u;i++)trace[i]=words[i];
    ((void (*)(uint32_t,uint32_t,uint32_t))(uintptr_t)r->report_entry)(command,caller,sp);
parked:
    for(;;)__asm__ volatile("nop":::"memory");
}
