/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_menu_return_loader.h"
#include "kui/retail_image.h"
#include "kui/cold_load.h"
#include "native_return_hardware.h"
#include "smash_return_fault_display.h"
#include <string.h>
struct kui_smash_menu_return_runtime kui_smash_menu_return_runtime
    __attribute__((used,section(".data.smash_menu_return_runtime")))={0};
#if KUI_SMASH_CARD_SESSION
__asm__(".global __kui_smash_card_session_flow\n.set __kui_smash_card_session_flow,1\n");
#endif
#if KUI_SMASH_PHASE_DIAGNOSTIC
__asm__(".global __kui_smash_return_phase_video_transport\n.set __kui_smash_return_phase_video_transport,1\n");
#endif
static struct kui_smash_menu_return_seed captured;
static uint32_t smash_return_waits;
static uint8_t probe[512] __attribute__((aligned(32)));
volatile uint32_t kui_smash_menu_return_diagnostic[4];
extern const uint8_t __smash_menu_return_begin[] __asm__("__smash_menu_return_begin");
extern const uint8_t __smash_menu_return_binary_end[] __asm__("__smash_menu_return_binary_end");
static void purge(uint32_t begin,uint32_t end) {
    for(uint32_t p=begin&~31u;p<end;p+=32u)__asm__ volatile("ocbp @%0"::"r"(p):"memory");
}
static void __attribute__((noreturn)) stopped(uint32_t reason,uint32_t value) {
    kui_smash_menu_return_diagnostic[0]=UINT32_C(0x4d525453);
    kui_smash_menu_return_diagnostic[1]=reason;kui_smash_menu_return_diagnostic[2]=value;
    /* Keep the current scanout. An inherited renderer could switch to an
     * obsolete framebuffer and hide the terminal reason we just drew. */
    kui_smash_return_fault_display(reason,value);
    for(;;)__asm__ volatile("nop":::"memory");
}
static uint32_t code_crc(void) {
    const uint8_t *runtime=(const void *)&kui_smash_menu_return_runtime;
    const uint32_t zero=0u;
    uint32_t crc=kui_retail_crc32(0,__smash_menu_return_begin,(size_t)(runtime-__smash_menu_return_begin));
    for(unsigned i=0;i<sizeof(kui_smash_menu_return_runtime)/4u;i++)crc=kui_retail_crc32(crc,&zero,4u);
    runtime+=sizeof(kui_smash_menu_return_runtime);
    crc=kui_retail_crc32(crc,runtime,(size_t)(__smash_menu_return_binary_end-runtime));
    return kui_smash_menu_return_runtime_crc(crc,&kui_smash_menu_return_runtime);
}
static void restore_firmware(void) {
    const uint32_t *f=kui_smash_menu_return_runtime.firmware;
    const uint32_t address[9]={0xac0000bcu,0xac0000c0u,0xac0000e0u,
        0xac001000u,0xac001004u,0xac001008u,0xac0010f0u,0xac0010f4u,0xac0010f8u};
    for(unsigned i=0;i<9u;i++)*(volatile uint32_t *)(uintptr_t)address[i]=f[i];
}
int kui_smash_menu_return_main(const uint32_t frame[21]) {
    uintptr_t f=(uintptr_t)frame&UINT32_C(0x1fffffff);
    if(!frame || f<UINT32_C(0x0c00c000) || f>UINT32_C(0x0d000000)-84u)stopped(1u,(uint32_t)f);
    struct kui_smash_menu_return_runtime *r=&kui_smash_menu_return_runtime;
    if(!kui_smash_menu_return_seed_valid(&r->returning) || !r->code_crc32 || !r->raw_crc32 ||
#if KUI_SMASH_PHASE_DIAGNOSTIC
       !kui_smash_phase_video_valid(&r->phase_video) ||
#endif
       (r->retire_entry&1u) || r->retire_entry<UINT32_C(0x8c010050) ||
       r->retire_entry>=UINT32_C(0x8cfe0000))stopped(2u,r->retire_entry);
    if(*(volatile uint32_t *)(uintptr_t)UINT32_C(0xff000010)&1u)stopped(3u,0u);
    if(*(volatile uint32_t *)(uintptr_t)UINT32_C(0xff00001c)&UINT32_C(0x80))stopped(3u,1u);
    purge(UINT32_C(0x8c008100),UINT32_C(0x8c00b800));
    if(code_crc()!=r->code_crc32 || kui_retail_crc32(0,(const void *)(uintptr_t)UINT32_C(0xac008100),512u)!=r->raw_crc32)
        stopped(4u,code_crc());
    kui_smash_menu_return_diagnostic[3]=UINT32_C(0x101);
    kui_smash_return_progress(UINT32_C(0x101),0u);
    int retired=((int (*)(void))(uintptr_t)r->retire_entry)();
    if(!retired) {
        /* The reset may be a tail call from main, so never return to an old
         * CRT cleanup. Assembly parks on the original owner stack and lets
         * its IRQ callbacks finish before retrying this same guard. */
        if((frame[4]&UINT32_C(0x10000000)) || (frame[4]&UINT32_C(0xf0))==UINT32_C(0xf0))
            stopped(12u,frame[4]);
        if(++smash_return_waits>KUI_SMASH_MENU_RETURN_WAIT_MAX)stopped(13u,smash_return_waits);
        return -1;
    }
    if(retired!=1)stopped(6u,(uint32_t)retired);
    /* Reproduce the original return helper's parser/setter inputs while
     * all old SDK text is still live. These exact source helpers are pure
     * integer memory operations; no cleanup, allocation or I/O is called. */
    uint8_t *context=(void *)(uintptr_t)UINT32_C(0x8c19ee60);
    uint8_t *ip=(void *)(uintptr_t)UINT32_C(0x8c0080f0);
    ((void (*)(void *,const char *))(uintptr_t)UINT32_C(0x8c1598c0))(context,(const char *)context+64u);
    int folder=((int (*)(const char *))(uintptr_t)UINT32_C(0x8c1ad2c8))((const char *)context+0x340u);
    int mode=((int (*)(uint32_t))(uintptr_t)UINT32_C(0x8c1ad31a))(((uint32_t *)(void *)context)[6]);
    captured=r->returning;
    if(folder || mode || !kui_smash_menu_return_seed_capture(&captured,context+256u,ip))stopped(5u,(uint32_t)folder);
    /* From this successful retirement onward no old SDK callback resumes.
     * Drain owned hardware before replacing executable or firmware bytes. */
    if(!kui_native_return_quiesce())stopped(7u,0u);
    const struct kui_smash_menu_loader_seed *s=&captured.outer;
    uint32_t (*read_card)(uint32_t,uint8_t *,uint32_t)=
        (uint32_t (*)(uint32_t,uint8_t *,uint32_t))(uintptr_t)UINT32_C(0xac008100);
    kui_smash_menu_return_diagnostic[3]=UINT32_C(0x105);
    kui_smash_return_progress(UINT32_C(0x105),s->first_lba);
    uint32_t result=read_card(s->first_lba,probe,1u);
    if(result)stopped(8u,result);
    kui_smash_menu_return_diagnostic[3]=UINT32_C(0x106);
    kui_smash_return_progress(UINT32_C(0x106),0u);
    uint8_t *out=(void *)(uintptr_t)UINT32_C(0xac010000);memcpy(out,probe,512u);
    if(s->first_blocks>1u && kui_cold_load_call3((uintptr_t)read_card,
       s->first_lba+1u,(uintptr_t)(out+512u),s->first_blocks-1u))stopped(9u,1u);
    if(s->block_count>s->first_blocks && kui_cold_load_call3((uintptr_t)read_card,
       s->second_lba,(uintptr_t)(out+s->first_blocks*512u),s->block_count-s->first_blocks))
        stopped(9u,2u);
    kui_smash_menu_return_diagnostic[3]=UINT32_C(0x107);
    kui_smash_return_progress(UINT32_C(0x107),0u);
    uint32_t crc=kui_cold_load_crc32(0,out,s->padded_bytes);
    if(crc!=s->payload_crc32)stopped(10u,crc);
    kui_smash_menu_return_diagnostic[3]=UINT32_C(0x108);
    kui_smash_return_progress(UINT32_C(0x108),0u);
    struct kui_smash_menu_return_seed *target=(void *)(out+s->menu_seed_offset);
    if(!kui_smash_menu_return_seed_blank(target))stopped(11u,s->menu_seed_offset);
#if KUI_SMASH_PHASE_DIAGNOSTIC
    const uint32_t video_offset=s->menu_seed_offset+sizeof(*target);
    if(video_offset>s->payload_bytes || sizeof(r->phase_video)>s->payload_bytes-video_offset)
        stopped(15u,s->payload_bytes);
    struct kui_smash_phase_video_seed *video=(void *)(out+video_offset);
    if(!kui_smash_phase_video_blank(video) || !kui_smash_phase_video_valid(&r->phase_video))
        stopped(15u,video_offset);
#endif
    memcpy(target,&captured,sizeof(captured));
    if(!kui_smash_menu_return_seed_valid(target))stopped(11u,0u);
#if KUI_SMASH_PHASE_DIAGNOSTIC
    memcpy(video,&r->phase_video,sizeof(*video));
    if(!kui_smash_phase_video_valid(video) || memcmp(video,&r->phase_video,sizeof(*video)))
        stopped(15u,video_offset);
#endif
#if KUI_SMASH_CARD_SESSION
    /* This branch is reachable only after successful terminal retirement and
     * hardware quiescence. No old callback resumes after the authority moves. */
    if(s->payload_bytes<KUI_SMASH_CARD_RECEIPT_OFFSET+KUI_SMASH_CARD_RECEIPT_BYTES)
        stopped(14u,s->payload_bytes);
    memcpy(out+KUI_SMASH_CARD_RECEIPT_OFFSET,&r->card_receipt,KUI_SMASH_CARD_RECEIPT_BYTES);
#endif
    kui_smash_menu_return_diagnostic[3]=UINT32_C(0x109);
    kui_smash_return_progress(UINT32_C(0x109),0u);
    restore_firmware();
    kui_smash_menu_return_diagnostic[3]=UINT32_C(0x10a);
    kui_smash_return_progress(UINT32_C(0x10a),0u);
    kui_native_return_enter();
    __builtin_unreachable();
}
