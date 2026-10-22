/* SPDX-License-Identifier: GPL-3.0-only */
/* Installer is temporary. The worker copies every retained object by value. */
#include "kui/vmu_native_passive.h"
#include "kui/vmu_native_layout.h"
#include "kui/vmu_native_patch.h"
#include "kui/vmu_native_worker.h"
#include "kui/vmu_native_relocate.h"
__asm__(".section .rodata.native_worker,\"a\"\n.balign 32\n"
    ".global __native_worker_blob_begin\n__native_worker_blob_begin:\n.incbin \"" KUI_NATIVE_WORKER_BIN "\"\n"
    ".global __native_worker_blob_end\n__native_worker_blob_end:\n.balign 4\n"
    ".global __native_worker_reloc_begin\n__native_worker_reloc_begin:\n.incbin \"" KUI_NATIVE_WORKER_RELOC "\"\n"
    ".global __native_worker_reloc_end\n__native_worker_reloc_end:\n.previous\n");
extern const uint8_t __native_worker_blob_begin[] __asm__("__native_worker_blob_begin");
extern const uint8_t __native_worker_blob_end[] __asm__("__native_worker_blob_end");
extern const uint8_t __native_worker_reloc_begin[] __asm__("__native_worker_reloc_begin");
extern const uint8_t __native_worker_reloc_end[] __asm__("__native_worker_reloc_end");
extern void kui_native_passive_publish(uint32_t,uint32_t);
/* Publish code from P2. Invalidate the I-cache only after all P1 dirty lines
 * in the exact patched span have reached RAM. Preserve the owner's CCR. */
__asm__(".section .text.native_passive_publish,\"ax\"\n.align 2\n"
    ".global _kui_native_passive_publish\n_kui_native_passive_publish:\n"
    "mov.l 1f,r0\njmp @r0\nnop\n.align 2\n1: .long 2f+0x20000000\n2:\n"
    "mov #-32,r0\nand r0,r4\n3:\ncmp/hs r5,r4\nbt 4f\n"
    "ocbp @r4\nadd #32,r4\nbra 3b\nnop\n4:\n"
    "mov.l 5f,r0\nmov.l @r0,r1\nmov.l 6f,r2\nor r2,r1\nmov.l r1,@r0\n"
    "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nrts\nnop\n.align 2\n"
    "5: .long 0xff00001c\n6: .long 0x00000800\n.previous\n");
static bool passive_entry(uint32_t entry,uint32_t begin,uint32_t bytes) {
    return !(entry&1u) && entry>=begin+64u && entry<begin+bytes;
}
static struct kui_vmu_native_layout passive_layout;
static void native_passive_prepare(void) {
    if(!kui_vmu_native_patch_no_interior((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,KUI_RETAIL_EXEC_ADDRESS,native_driver.submit,12u)) native_report(2u,11u);
    if(!kui_vmu_native_layout_discover((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,&native_driver,&passive_layout)) native_report(2u,10u);
}
static void native_passive_run(struct kui_vmu_native_heap_lease *lease) {
    uint32_t load=lease->payload|0x80000000u,dest=load+0x20000000u;
    uint32_t bytes=(uint32_t)(__native_worker_blob_end-__native_worker_blob_begin);
    uint32_t reloc_bytes=(uint32_t)(__native_worker_reloc_end-__native_worker_reloc_begin);
    if(bytes<64u || bytes>=0xf000u || (native_driver.submit&3u) ||
       !kui_vmu_native_heap_lease_valid(lease,native_read,NULL,1u)) native_report(8u,1u);
    struct kui_vmu_native_passive_config cfg;memset(&cfg,0,sizeof(cfg));
    cfg.version=1u;cfg.bytes=sizeof(cfg);cfg.driver=native_driver;cfg.lease=*lease;
    cfg.layout=passive_layout;
    cfg.tx_base=cfg.layout.tx_base;
    cfg.runtime=(struct kui_vmu_native_runtime_config){.version=1u,.bytes=sizeof(cfg.runtime),
        .lease_address=load,.lease_bytes=lease->lease_bytes,.lease_header_bytes=32u,
        .required_lease_bytes=0x30000u,.gun_flag=cfg.layout.gun_flag,
        .timer1_callback=cfg.layout.timer1_callback,.timer2_callback=cfg.layout.timer2_callback,
        .rx_layout=cfg.layout.rx_layout,.rx_base=cfg.layout.rx_base};
    memcpy(cfg.display,display.regs,sizeof(cfg.display));
    retail_display_restore(&display);retail_display_line("RELOCATING PASSIVE MAPLE WORKER");
    for(uint32_t at=load;at<load+0x10000u;at+=32u)
        __asm__ volatile("ocbp @%0"::"r"(at):"memory");
    int relocated=kui_vmu_native_relocate(__native_worker_blob_begin,bytes,
        __native_worker_reloc_begin,reloc_bytes,(void *)(uintptr_t)dest,0x10000u,load);
    if(relocated) native_report(8u,(uint32_t)relocated);
    memset((void *)(uintptr_t)(dest+bytes),0,0x10000u-bytes);
    const struct kui_vmu_native_passive_exports *e=(const void *)(uintptr_t)dest;
    if(e->magic!=KUI_VMU_NATIVE_PASSIVE_MAGIC || e->version!=1u || e->bytes!=sizeof(*e) ||
       !passive_entry(e->arm,dest,bytes) || !passive_entry(e->submit_hook,dest,bytes) ||
       !passive_entry(e->complete_hook,dest,bytes) || !passive_entry(e->terminal,dest,bytes) ||
       e->bss_begin!=dest+bytes || e->bss_end<e->bss_begin || e->bss_end>e->stack_bottom ||
       e->stack_bottom!=dest+0xf000u || e->stack_top!=dest+0x10000u || e->memory_end!=e->stack_top ||
       (e->resume_cell&3u) || (e->consumer_cell&3u) || e->resume_cell==e->consumer_cell ||
       e->resume_cell<dest+64u || e->resume_cell>dest+bytes-4u ||
       e->consumer_cell<dest+64u || e->consumer_cell>dest+bytes-4u || e->reserved[0] || e->reserved[1])
        native_report(8u,2u);
    for(unsigned i=0;i<16u;i++) ((volatile uint32_t *)(uintptr_t)e->stack_bottom)[i]=UINT32_C(0x4b535431);
    if(*(const uint32_t *)(uintptr_t)e->resume_cell || *(const uint32_t *)(uintptr_t)e->consumer_cell)
        native_report(8u,3u);
    *(uint32_t *)(uintptr_t)e->resume_cell=native_driver.resume;
    *(uint32_t *)(uintptr_t)e->consumer_cell=native_driver.consumer;
    uint32_t unused=0;
    if(kui_vmu_native_worker_call(e->arm,e->stack_top,
       (const struct kui_vmu_native_worker_config *)(const void *)&cfg,
       (struct kui_vmu_native_worker_result *)(void *)&unused)) native_report(9u,1u);
    if(!kui_vmu_native_heap_lease_valid(lease,native_read,NULL,1u) ||
       native_word(native_heap.initializer_slot)!=KUI_VMU_OBSERVE_LOW_HEAP_HOOK ||
       native_word(native_driver.completion_slot)!=native_driver.consumer) native_report(9u,2u);
    static const uint16_t original[6]={0x9028u,0x63e2u,0x023eu,0x12c6u,0x9029u,0x63e2u};
    if(memcmp((const void *)(uintptr_t)native_driver.submit,original,sizeof(original))) native_report(9u,3u);
    const uint16_t patch[6]={0xd001u,0x402bu,0x0009u,0x0009u,
        (uint16_t)e->submit_hook,(uint16_t)(e->submit_hook>>16)};
    memcpy((void *)(uintptr_t)native_driver.submit,patch,sizeof(patch));
    kui_native_passive_publish(native_driver.submit,native_driver.resume);
    *(volatile uint32_t *)(uintptr_t)native_driver.completion_slot=e->complete_hook;
    native_publish(native_driver.completion_slot);
    if(native_word(native_driver.completion_slot|0x20000000u)!=e->complete_hook ||
       memcmp((const void *)(uintptr_t)(native_driver.submit|0x20000000u),patch,sizeof(patch)))
        native_report(9u,4u);
    volatile struct kui_vmu_observe_boot_control *c=control();
    c->worker_end=e->memory_end;c->terminal=e->terminal;c->lease=lease->payload;
    c->installer=0;c->stage_stack=0;c->status=KUI_VMU_OBS_BOOT_INSTALLED;
    retail_display_line("PASSIVE WORKER ARMED - STARTING GAME");
}
