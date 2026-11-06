/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_ce_return_loader.h"
#include "kui/retail_image.h"
#include "kui/cold_load.h"
#include "native_return_hardware.h"
#include "smash_return_fault_display.h"
#include "smash_ce_return_raw_image.h"
#include <string.h>
__asm__(".global __kui_smash_ce_return_controller\n.set __kui_smash_ce_return_controller,1\n");
__asm__(".global __kui_smash_ce_return_integrity_mask\n.set __kui_smash_ce_return_integrity_mask,1\n");
__asm__(".global __kui_smash_ce_return_trace_revision\n.set __kui_smash_ce_return_trace_revision,1\n");
__asm__(".global __kui_smash_ce_return_raw_image\n.set __kui_smash_ce_return_raw_image,1\n");
#if KUI_SMASH_SWIRL_VMU
__asm__(".global __kui_smash_swirl_private_return\n.set __kui_smash_swirl_private_return,1\n");
#endif

struct kui_smash_ce_return_runtime kui_smash_ce_return_runtime
    __attribute__((used,section(".data.smash_ce_return_runtime")))={0};
static uint32_t validated,waits;
uint32_t kui_smash_ce_return_previous_e0;
#if KUI_SMASH_SWIRL_VMU
#define CE_RETURN_PROFILE 2u
#define CE_RETURN_OWNER KUI_SMASH_CARD_ROLE_SWIRL
#define CE_RETURN_CALLER UINT32_C(0x01d2190e)
#define CE_RETURN_PRIVATE_CALLER UINT32_C(0x000419a6)
#else
#define CE_RETURN_PROFILE 1u
#define CE_RETURN_OWNER KUI_SMASH_CARD_ROLE_VC2
#define CE_RETURN_CALLER UINT32_C(0x01d21902)
#endif
extern const uint8_t __smash_ce_return_begin[] __asm__("__smash_ce_return_begin");
extern const uint8_t __smash_ce_return_binary_end[] __asm__("__smash_ce_return_binary_end");
extern void kui_smash_ce_return_mmu_off(void);
extern void kui_smash_ce_return_ram_clear(void);

static uint32_t read32(uint32_t at) {return *(volatile const uint32_t *)(uintptr_t)at;}
static void __attribute__((noreturn)) stopped(uint32_t reason,uint32_t detail) {
    /* Draw into the active scanout; the old native renderer has obsolete
     * video state and destroys the reader manifest. Never enter it here. */
    kui_smash_return_fault_display(UINT32_C(0xce010000)|reason,detail);
    for(;;)__asm__ volatile("nop":::"memory");
}
static uint32_t code_crc(void) {
    const uint8_t *p=(const void *)&kui_smash_ce_return_runtime;
    const uint32_t zero=0u;
    uint32_t crc=kui_retail_crc32(0,__smash_ce_return_begin,(size_t)(p-__smash_ce_return_begin));
    for(unsigned i=0u;i<sizeof(kui_smash_ce_return_runtime)/4u;i++)crc=kui_retail_crc32(crc,&zero,4u);
    p+=sizeof(kui_smash_ce_return_runtime);
    crc=kui_retail_crc32(crc,p,(size_t)(__smash_ce_return_binary_end-p));
    return kui_smash_ce_return_runtime_crc(crc,&kui_smash_ce_return_runtime);
}
static void publish_low(void) {
    /* The CE GD gateway can have left dirty P1 state beside its P2 view.
     * These exact normal-RAM aliases are admitted only with index mode off. */
    /* High helper objects need no alias conversion: its accepted linker
     * places runtime/loan/continuation/fence in P2 .workspace, and the view
     * generator binds those exact BSS symbols and sizes. Immutable P1 code
     * therefore names P2 private data. The pre-NK installer publishes all
     * old P1 aliases before it initializes that P2 workspace. */
    for(uint32_t at=UINT32_C(0x8c004000);at<UINT32_C(0x8c007ea0);at+=32u)
        __asm__ volatile("ocbp @%0"::"r"(at):"memory");
}
static uint32_t canonical_raw_crc(void) {
    const uint32_t zero=0u;
    uint32_t value=kui_retail_crc32(0,kui_smash_ce_return_raw_prefix,
        sizeof(kui_smash_ce_return_raw_prefix));
    for(unsigned n=0u;n<KUI_SMASH_CE_RETURN_RAW_ZERO_BYTES/4u;++n)
        value=kui_retail_crc32(value,&zero,4u);
    return value;
}
static uint32_t runtime_failures(void) {
    const struct kui_smash_ce_return_runtime *r=&kui_smash_ce_return_runtime;
    /* Keep every admission predicate, and identify live failures instead of
     * collapsing all of them to detail0. All reads have fixed owned bounds;
     * even a corrupt helper_bytes field cannot supply a CRC length. */
    uint32_t bad=0u;
#define CHECK(bit,expression) bad|=(uint32_t)!(expression)<<(bit)
    CHECK(0,r->profile==CE_RETURN_PROFILE);
    CHECK(1,r->helper_bytes==41152u);
    CHECK(2,r->helper_crc32!=0u);
    CHECK(3,r->code_crc32!=0u);
    CHECK(4,r->raw_crc32!=0u);
    CHECK(5,kui_smash_menu_return_seed_valid(&r->returning));
    CHECK(6,r->returning.flags==KUI_SMASH_MENU_RETURN_CONTEXT);
    CHECK(7,kui_smash_phase_video_valid(&r->phase_video));
    CHECK(8,kui_smash_card_receipt_owner(&r->card_receipt)==CE_RETURN_OWNER);
    CHECK(9,r->card_receipt.sequence<UINT32_MAX);
    CHECK(10,code_crc()==r->code_crc32);
    /* The initialized controller carries the current accepted260-byte raw
     * image; its252-byte tail is canonical zero padding. Check every byte
     * against the launcher's sealed full512 CRC without a new low lease. */
    CHECK(11,canonical_raw_crc()==r->raw_crc32);
    CHECK(12,kui_cold_load_crc32(0,(const void *)(uintptr_t)UINT32_C(0xacfc4000),41152u)==r->helper_crc32);
#undef CHECK
    return bad;
}
static uint32_t __attribute__((noinline,noclone)) restore_raw(void) {
    /* Cache retirement must precede this copy: a later dirty P1 eviction
     * must never replace freshly checked P2 code. Expected CRC remains the
     * original sealed value; changed live bootstrap bytes supply no trust. */
    void *target=(void *)(uintptr_t)UINT32_C(0xac008100);
    memcpy(target,kui_smash_ce_return_raw_prefix,sizeof(kui_smash_ce_return_raw_prefix));
    for(unsigned i=KUI_SMASH_CE_RETURN_RAW_PREFIX_BYTES/4u;i<128u;++i)
        ((volatile uint32_t *)target)[i]=0u;
    return kui_cold_load_crc32(0,target,512u);
}
static void restore_firmware(void) {
    const uint32_t address[9]={0xac0000bcu,0xac0000c0u,0xac0000e0u,
        0xac001000u,0xac001004u,0xac001008u,0xac0010f0u,0xac0010f4u,0xac0010f8u};
    for(unsigned i=0u;i<9u;i++)*(volatile uint32_t *)(uintptr_t)address[i]=kui_smash_ce_return_runtime.firmware[i];
}
int kui_smash_ce_return_main(const uint32_t frame[21]) {
    struct kui_smash_ce_return_runtime *r=&kui_smash_ce_return_runtime;
    uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr)::"memory");
    /* All interpretation of the original virtual frame precedes MMU-off.
     * Only the original source-selected SetKMode(1)/E0 reset is admitted. */
    uintptr_t sp=(uintptr_t)frame+84u;
    bool ce_stack=sp>=UINT32_C(0x10000) && sp<UINT32_C(0x80000000);
    bool ram_stack=sp>=UINT32_C(0x8c010000) && sp<=UINT32_C(0x8d000000);
    if(!frame || (sp&3u) || (!ce_stack && !ram_stack) ||
       (sr&UINT32_C(0x500000f0))!=UINT32_C(0x400000f0) ||
       (frame[4]&UINT32_C(0x500000f0))!=UINT32_C(0x40000000))
        stopped(1u,(uint32_t)sp);
    uint32_t caller=frame[5]&UINT32_C(0x81ffffff);
#if KUI_SMASH_SWIRL_VMU
    if(frame[16]!=1u || caller!=CE_RETURN_CALLER) {
        /* Original SAMPLE loads MAIGO, sets IP.FE=1 and installs8300 as
         * Swirl's private return callback. The late controller deliberately
         * replaces MAIGO there, so its argument0 is not an E0 function0.
         * Without this source-bound distinction it reaches the real BIOS. */
        if(caller!=CE_RETURN_PRIVATE_CALLER)stopped(2u,caller);
        uint32_t callback=read32(UINT32_C(0xac00e00c));
        uint8_t flag=*(volatile const uint8_t *)(uintptr_t)UINT32_C(0xac0080fe);
        if(frame[16]==0u) {
            if(flag!=1u || callback!=UINT32_C(0x8c008300))stopped(2u,callback);
        } else if(frame[16]==3u) {
            /* The original non-MAIGO fallback copies this exact32-byte
             * thunk, sets command3 and records the IP base before E0. */
            if(flag==1u || callback!=UINT32_C(0xac00e100) ||
               /* The thunk just wrote this word through P1. Observe that
                * same live alias; terminal cache retirement publishes it. */
               read32(UINT32_C(0x8c000064))!=UINT32_C(0x8c008000) ||
               kui_retail_crc32(0u,(const void *)(uintptr_t)UINT32_C(0xac00e100),32u)!=UINT32_C(0xe66470fc))
                stopped(2u,callback);
        } else stopped(2u,frame[16]);
    }
#else
    if(frame[16]!=1u || caller!=CE_RETURN_CALLER)stopped(2u,caller);
#endif
    if(!(read32(UINT32_C(0xff000010))&1u) ||
       (read32(UINT32_C(0xff00001c))&UINT32_C(0xa1))!=1u)stopped(3u,0u);
    if(!validated) {
        /* Includes every mutable transported byte and both exact code
         * images before any private-state address is followed. */
        uint32_t bad=runtime_failures();
        if(bad)stopped(4u,bad);
        validated=1u;
    }
    publish_low();
    int idle=kui_smash_ce_return_idle(&r->view);
    if(idle<0)stopped(5u,(uint32_t)idle);
    if(!idle) {
        if(++waits>KUI_SMASH_CE_RETURN_WAIT_MAX)stopped(6u,waits);
        return -1;
    }
    /* The final check and freeze are atomic against old CE scheduling. No
     * pending completion is cancelled; BUSY never changes helper state. */
    /* The final CRC is read-only, IRQs remain masked and the admitted
     * private helper workspace has no hardware writer. The just-observed
     * idle state therefore remains stable until the following freeze. */
    uint32_t bad=runtime_failures();
    if(bad)stopped(7u,bad);
    *(volatile uint32_t *)(uintptr_t)r->view.freeze=1u;
    /* Stop VBlank retrigger before waiting for the final physical Maple
     * transfer. This is now a terminal owner, so it never restores MDEN. */
    *(volatile uint32_t *)(uintptr_t)UINT32_C(0xa05f6c14)=0u;
    if(read32(UINT32_C(0xa05f6c14)) || !kui_native_return_quiesce())stopped(8u,0u);
    kui_smash_ce_return_mmu_off();
    uint32_t restored_crc=restore_raw();
    if(restored_crc!=r->raw_crc32)stopped(13u,restored_crc);
    /* Original MAIGO retires all main RAM above the low firmware area.
     * Next-title SAMPLE clears only below0x8ce00000, so match that retirement
     * while the authenticated controller and its stack still live low. */
    kui_smash_ce_return_ram_clear();
    const struct kui_smash_menu_loader_seed *s=&r->returning.outer;
    uint32_t (*read_card)(uint32_t,uint8_t *)=
        (uint32_t (*)(uint32_t,uint8_t *))(uintptr_t)UINT32_C(0xac008100);
    uint8_t *out=(void *)(uintptr_t)UINT32_C(0xac010000);
    for(uint32_t block=0u;block<s->block_count;block++) {
        uint32_t lba=block<s->first_blocks?s->first_lba+block:s->second_lba+block-s->first_blocks;
        /* These aligned incoming bytes remain unexecuted and unauthorised
         * until the complete capsule CRC below; no intermediate copy is
         * needed. A failed read always parks without entering the payload. */
        uint32_t result=read_card(lba,out+block*512u);
        if(result)stopped(9u,result);
    }
    uint32_t crc=kui_cold_load_crc32(0,out,s->padded_bytes);
    if(crc!=s->payload_crc32)stopped(10u,crc);
    struct kui_smash_menu_return_seed *target=(void *)(out+s->menu_seed_offset);
    uint32_t vo=s->menu_seed_offset+sizeof(*target);
    if(vo>s->payload_bytes || sizeof(r->phase_video)>s->payload_bytes-vo ||
       !kui_smash_menu_return_seed_blank(target) ||
       !kui_smash_phase_video_blank((const void *)(out+vo)) ||
       s->payload_bytes<KUI_SMASH_CARD_RECEIPT_OFFSET+KUI_SMASH_CARD_RECEIPT_BYTES ||
       !kui_smash_card_receipt_blank((const void *)(out+KUI_SMASH_CARD_RECEIPT_OFFSET)))stopped(11u,vo);
    memcpy(target,&r->returning,sizeof(*target));
    memcpy(out+vo,&r->phase_video,sizeof(r->phase_video));
    memcpy(out+KUI_SMASH_CARD_RECEIPT_OFFSET,&r->card_receipt,sizeof(r->card_receipt));
    if(memcmp(target,&r->returning,sizeof(*target)) || memcmp(out+vo,&r->phase_video,sizeof(r->phase_video)) ||
       memcmp(out+KUI_SMASH_CARD_RECEIPT_OFFSET,&r->card_receipt,sizeof(r->card_receipt)))stopped(12u,0u);
    restore_firmware();
    kui_native_return_enter();
    __builtin_unreachable();
}
