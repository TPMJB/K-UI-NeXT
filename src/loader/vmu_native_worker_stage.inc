/* SPDX-License-Identifier: GPL-3.0-only */
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
static void worker_hash(uint32_t address,uint32_t bytes,uint8_t digest[32]) {
    struct kui_sha256 h;kui_sha256_init(&h);
    kui_sha256_update(&h,(const void *)(uintptr_t)address,bytes);kui_sha256_digest(&h,digest);
}
static void native_worker_run(struct kui_vmu_native_heap_lease *) __attribute__((noreturn));
static void native_worker_run(struct kui_vmu_native_heap_lease *lease) {
    uint32_t load=lease->payload|0x80000000u,dest=load+0x20000000u;
    uint32_t bytes=(uint32_t)(__native_worker_blob_end-__native_worker_blob_begin);
    uint32_t reloc_bytes=(uint32_t)(__native_worker_reloc_end-__native_worker_reloc_begin);
    if(bytes<64u || bytes>KUI_VMU_NATIVE_WORKER_BYTES ||
       !kui_vmu_native_heap_lease_valid(lease,native_read,NULL,1u)) native_report(8u,1u);
    retail_display_restore(&display);retail_display_line("RELOCATING WORKER INTO OWNED MEMORY");
    /* Purge old cached lease lines, then exclusively use P2 for worker code,
     * BSS and stack. Owner cache mode and the allocation header stay intact. */
    for(uint32_t at=load;at<load+KUI_VMU_NATIVE_WORKER_BYTES;at+=32u)
        __asm__ volatile("ocbp @%0"::"r"(at):"memory");
    int relocated=kui_vmu_native_relocate(__native_worker_blob_begin,bytes,
        __native_worker_reloc_begin,reloc_bytes,(void *)(uintptr_t)dest,0x10000u,load);
    if(relocated) native_report(8u,(uint32_t)relocated);
    memset((void *)(uintptr_t)(dest+bytes),0,0x10000u-bytes);
    const struct kui_vmu_native_worker_exports *e=(const void *)(uintptr_t)dest;
    if(e->magic!=KUI_VMU_NATIVE_WORKER_MAGIC || e->version!=1u || e->bytes!=sizeof(*e) ||
       (e->entry&1u) || e->entry<dest+64u || e->entry>=dest+bytes ||
       e->bss_begin<dest+bytes || e->bss_begin>e->bss_end || e->bss_end>e->stack_bottom ||
       e->stack_bottom!=dest+0xf000u || e->stack_top!=dest+0x10000u || e->memory_end!=e->stack_top)
        native_report(8u,2u);
    for(unsigned i=0;i<7u;i++) if(e->reserved[i]) native_report(8u,3u);
    for(unsigned i=0;i<16u;i++) ((volatile uint32_t *)(uintptr_t)e->stack_bottom)[i]=UINT32_C(0x4b535431);
    const struct kui_vmu_native_worker_config config={1u,sizeof(config),load,lease->lease_bytes,0x10000u,1u,lease};
    struct kui_vmu_native_worker_result result;memset(&result,0,sizeof(result));
    uint8_t before[32],after[32];worker_hash(dest,bytes,before);
    retail_display_line("RUNNING RELOCATED VMU PROTOCOL");
    int executed=kui_vmu_native_worker_call(e->entry,e->stack_top,&config,&result);
    uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr));
    worker_hash(dest,bytes,after);
    if(executed || result.status || result.version!=1u || result.bytes!=sizeof(result) ||
       ((sr>>4)&15u)!=15u || memcmp(before,after,sizeof(before)) || result.self_address!=dest ||
       !result.lease_valid || result.requests!=259u || result.reads!=256u || result.refused!=1u ||
       result.image_crc!=KUI_VMU_NATIVE_WORKER_BLANK_CRC ||
       !kui_vmu_native_heap_lease_valid(lease,native_read,NULL,1u))
        native_report(9u,executed?(uint32_t)executed:5u);
    for(unsigned i=0;i<16u;i++) if(((volatile uint32_t *)(uintptr_t)e->stack_bottom)[i]!=UINT32_C(0x4b535431))
        native_report(9u,4u);
    retail_display_restore(&display);
    retail_display_line("AUTOMATIC RELOCATED VMU WORKER");
    retail_display_line("NO MAPLE HOOKS / NO SAVE WRITES");
    retail_display_hex("PHASE",10u);retail_display_hex("ALLOCATED PAYLOAD",lease->payload);
    retail_display_hex("RELOCATED ENTRY",e->entry);retail_display_hex("WORKER STACK",result.entry_sp);
    retail_display_hex("PROTOCOL REQUESTS",result.requests);retail_display_hex("BLOCKS READ",result.reads);
    retail_display_hex("BLANK IMAGE CRC",result.image_crc);
    stopped("WORKER VERIFIED - PHOTO THEN POWER OFF",10u);
}
