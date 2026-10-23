/* SPDX-License-Identifier: GPL-3.0-only */
/* Installer is temporary. The worker copies every retained object by value. */
#include "kui/vmu_native_passive.h"
#include "kui/vmu_native_layout.h"
#include "kui/vmu_native_patch.h"
#include "kui/vmu_native_worker.h"
#include "kui/vmu_native_relocate.h"
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
#include "vmu_native_evolution_fault_stage.inc"
#endif
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
#if KUI_VMU_NATIVE_CACHED_CODE
    /* begin is the P2 export header; only the P1 alias is executable. */
    begin-=UINT32_C(0x20000000);
#endif
    return !(entry&1u) && entry>=begin+sizeof(struct kui_vmu_native_passive_exports) && entry<begin+bytes;
}
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
#include "vmu_native_evolution_trace_stage.inc"
#endif
static struct kui_vmu_native_layout passive_layout;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
static struct kui_vmu_native_exit native_exits;
static uint16_t persistent_exit_windows[KUI_VMU_NATIVE_EXIT_ENTRIES][6];
#endif
#if KUI_VMU_NATIVE_READONLY_PROBE
static uint16_t readonly_init_window[6],readonly_release_window[10];
static bool readonly_release_replay_valid(void) {
    uint32_t address=native_heap.release|0x80000000u;
    const uint16_t *op=(const void *)(uintptr_t)address;
    if(op[0]!=0x0002u || (op[1]&0xff00u)!=0x9300u || op[2]!=0x4009u ||
       op[3]!=0x4009u || op[4]!=0xc90fu || op[5]!=0x6703u ||
       op[6]!=0x0002u || op[7]!=0x2039u || op[8]!=0xcbf0u || op[9]!=0x400eu) return false;
    uint32_t literal=address+6u+2u*(op[1]&255u);
    return literal>=KUI_RETAIL_EXEC_ADDRESS && literal-KUI_RETAIL_EXEC_ADDRESS<=exec_bytes-2u &&
        *(const uint16_t *)(uintptr_t)literal==0xff0fu;
}
static void readonly_patch_entry(uint32_t address,uint32_t target) {
    address=(address&0x1fffffffu)|0x80000000u;
    uint16_t patch[6];
    if(!kui_vmu_native_patch_encode(address,target,patch)) native_report(9u,8u);
    memcpy((void *)(uintptr_t)address,patch,sizeof(patch));
    kui_native_passive_publish(address,address+sizeof(patch));
    if(memcmp((const void *)(uintptr_t)(address|0x20000000u),patch,sizeof(patch))) native_report(9u,6u);
}
#endif
#if KUI_VMU_NATIVE_READONLY_PROBE
static void native_passive_capture_windows(void) {
    memcpy(readonly_init_window,(const void *)(uintptr_t)native_heap.initializer,sizeof(readonly_init_window));
    memcpy(readonly_release_window,(const void *)(uintptr_t)native_heap.release,sizeof(readonly_release_window));
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    for(unsigned i=0;i<native_exits.count;i++)
        memcpy(persistent_exit_windows[i],(const void *)(uintptr_t)native_exits.entries[i],12u);
#endif
}
#endif
static void native_passive_prepare(void) {
#if KUI_VMU_NATIVE_READONLY_PROBE
    const struct kui_vmu_native_patch_window windows[3]={{native_driver.submit,12u},
        {native_heap.initializer,12u},{native_heap.release,20u}};
    if(((native_heap.initializer|native_heap.release)&3u) || !readonly_release_replay_valid()) native_report(2u,12u);
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(!kui_vmu_native_exit_discover_guarded((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,KUI_RETAIL_EXEC_ADDRESS,windows,sizeof(windows)/sizeof(windows[0]),
       &native_exits)) native_report(2u,13u);
#else
    if(!kui_vmu_native_patch_no_interiors((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,KUI_RETAIL_EXEC_ADDRESS,windows,sizeof(windows)/sizeof(windows[0]))) native_report(2u,12u);
#endif
    native_passive_capture_windows();
#else
    if(!kui_vmu_native_patch_no_interior((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,KUI_RETAIL_EXEC_ADDRESS,native_driver.submit,12u)) native_report(2u,11u);
#endif
}
#if KUI_VMU_NATIVE_CACHE_PROBE
#include "kui/vmu_native_cache_seed.h"
#include "kui/vmu_native_resolution_cache.h"
__asm__(".section .rodata.native_resolution_seed,\"a\"\n.balign 32\n"
    ".global __native_resolution_cache_seed_start\n"
    ".type __native_resolution_cache_seed_start,@object\n"
    "__native_resolution_cache_seed_start:\n.ascii \"KUINVS01\"\n.long 1,1120,0,1024\n.zero 1096\n"
    ".size __native_resolution_cache_seed_start,1120\n"
    ".global __native_resolution_cache_seed_end\n__native_resolution_cache_seed_end:\n.previous\n");
extern const uint8_t native_resolution_seed[] __asm__("__native_resolution_cache_seed_start");
static struct kui_vmu_native_resolution native_cached_resolution;
static bool __attribute__((noipa)) native_resolution_replay(void) {
    const uint8_t *wire,*digest;
    if(!kui_vmu_native_cache_seed_read(native_resolution_seed,KUI_VMU_NATIVE_CACHE_SEED_BYTES,&wire,&digest) ||
       !kui_vmu_native_resolution_cache_replay((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,KUI_RETAIL_EXEC_ADDRESS,wire,KUI_VMU_NATIVE_RESOLUTION_CACHE_BYTES,digest,&native_cached_resolution))
        return false;
    native_driver=native_cached_resolution.driver;passive_layout=native_cached_resolution.layout;
    native_heap=native_cached_resolution.heap;native_call=native_cached_resolution.call;
    native_exits=native_cached_resolution.exits;
    if(((native_heap.initializer|native_heap.release)&3u) || !readonly_release_replay_valid() ||
       !allocation_safe(native_call.entry_begin,native_call.entry_bytes)) return false;
    native_passive_capture_windows();return true;
}
#endif
/* Diagnostic A/B: retain the exact armed worker allocation without leaving
 * any guest callback into it. The original initializer may later reclaim the
 * reservation; the low GD guard remains conservative, but cannot call a stale
 * worker. Ordinary card preparation/recovery has already run unchanged. */
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
/* Keep the immutable launch selector separate from the worker's diagnostic
 * policy. This never changes reservation or storage ownership admission. */
static uint32_t __attribute__((noipa)) native_runtime_diagnostics(void) {
    uint32_t mode=native_runtime_mode();
    return mode==KUI_VMU_NATIVE_MODE_PROFILE?KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE:
        mode==KUI_VMU_NATIVE_MODE_HOOKS_ONLY?KUI_VMU_NATIVE_DIAGNOSTICS_HOOKS_ONLY:
        mode==KUI_VMU_NATIVE_MODE_RAW_CHAIN?KUI_VMU_NATIVE_DIAGNOSTICS_RAW_CHAIN:
        KUI_VMU_NATIVE_DIAGNOSTICS_NORMAL;
}
static void __attribute__((noipa)) native_passive_lease_only(
    struct kui_vmu_native_heap_lease *lease,const struct kui_vmu_native_passive_exports *e) {
    for(unsigned i=0;i<native_exits.count;i++)
        if(memcmp((const void *)(uintptr_t)native_exits.entries[i],persistent_exit_windows[i],12u))
            native_report(9u,7u);
    uint32_t slot=(native_heap.initializer_slot&UINT32_C(0x1fffffff))|UINT32_C(0x80000000);
    *(volatile uint32_t *)(uintptr_t)slot=native_heap.initializer_target;
    native_publish(slot);
    if(native_word(slot|UINT32_C(0x20000000))!=native_heap.initializer_target ||
       !kui_vmu_native_heap_lease_valid(lease,native_read,NULL,1u) ||
       native_word(native_driver.completion_slot)!=native_driver.consumer)
        native_report(9u,9u);
    volatile struct kui_vmu_observe_boot_control *c=control();
    c->lease=lease->payload&UINT32_C(0x1fffffff);c->worker_end=e->memory_end;
    c->terminal=0;c->sd_transaction=0;c->installer=0;c->stage_stack=0;
    c->status=KUI_VMU_OBS_BOOT_INSTALLED;
    retail_display_line("RAM RESERVATION ONLY / NO VIRTUAL VMU");
    retail_display_line("NO IN-GAME SAVE WRITES - STARTING GAME");
}
#endif
static void native_passive_run(struct kui_vmu_native_heap_lease *lease) {
    const uint32_t span=KUI_VMU_NATIVE_PASSIVE_SPAN;
    uint32_t load=(lease->payload&UINT32_C(0x1fffffff))|UINT32_C(0x80000000),dest=load+UINT32_C(0x20000000);
    uint32_t bytes=(uint32_t)(__native_worker_blob_end-__native_worker_blob_begin);
    uint32_t reloc_bytes=(uint32_t)(__native_worker_reloc_end-__native_worker_reloc_begin);
    if(bytes<sizeof(struct kui_vmu_native_passive_exports) || bytes>=span-0x1000u || (native_driver.submit&3u) ||
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
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(!native_persist_ready ||
       kui_vmu_persist_stage_handoff_matches(&native_persist_profile,&native_persist_handoff))
        native_report(8u,5u);
    cfg.durable=native_persist_handoff;cfg.sd_lease=KUI_VMU_OBSERVE_LOW_SD_LEASE;
    cfg.low_terminal=KUI_VMU_OBSERVE_LOW_RETURN_HOOK;cfg.low_heap_hook=KUI_VMU_OBSERVE_LOW_HEAP_HOOK;
    cfg.exits=native_exits;
    cfg.diagnostics=native_runtime_diagnostics();
#endif
    retail_display_restore(&display);retail_display_line("RELOCATING PASSIVE MAPLE WORKER");
    for(uint32_t at=load;at<load+span;at+=32u)
        __asm__ volatile("ocbp @%0"::"r"(at):"memory");
    int relocated=kui_vmu_native_relocate(__native_worker_blob_begin,bytes,
        __native_worker_reloc_begin,reloc_bytes,(void *)(uintptr_t)dest,span,load);
    if(relocated) native_report(8u,(uint32_t)relocated);
    memset((void *)(uintptr_t)(dest+bytes),0,span-bytes);
    const struct kui_vmu_native_passive_exports *e=(const void *)(uintptr_t)dest;
    if(e->magic!=KUI_VMU_NATIVE_PASSIVE_MAGIC || e->version!=KUI_VMU_NATIVE_PASSIVE_VERSION || e->bytes!=sizeof(*e) ||
       !passive_entry(e->arm,dest,bytes) || !passive_entry(e->submit_hook,dest,bytes) ||
       !passive_entry(e->complete_hook,dest,bytes) || !passive_entry(e->terminal,dest,bytes) ||
       e->bss_begin!=dest+bytes || e->bss_end<e->bss_begin || e->bss_end>e->stack_bottom ||
       e->stack_bottom!=dest+span-0x1000u || e->stack_top!=dest+span || e->memory_end!=e->stack_top ||
       (e->resume_cell&3u) || (e->consumer_cell&3u) || e->resume_cell==e->consumer_cell ||
       e->resume_cell<dest+64u || e->resume_cell>dest+bytes-4u ||
       e->consumer_cell<dest+64u || e->consumer_cell>dest+bytes-4u)
        native_report(8u,2u);
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(!passive_entry(e->sd_transaction,dest,bytes) ||
       !passive_entry(e->runtime_fault_hook,dest,bytes) ||
       !passive_entry(e->allocation_failure_hook,dest,bytes) ||
       !e->trace_exports) native_report(8u,6u);
#endif
#if KUI_VMU_NATIVE_READONLY_PROBE
    if(!passive_entry(e->release_hook,dest,bytes) || (e->release_resume_cell&3u) ||
       e->release_resume_cell<dest+64u || e->release_resume_cell>dest+bytes-4u ||
       e->release_resume_cell==e->resume_cell || e->release_resume_cell==e->consumer_cell ||
       *(const uint32_t *)(uintptr_t)e->release_resume_cell) native_report(8u,4u);
    *(uint32_t *)(uintptr_t)e->release_resume_cell=native_heap.release+20u;
#else
    if(e->reserved[0] || e->reserved[1]) native_report(8u,2u);
#endif
    for(unsigned i=0;i<16u;i++) ((volatile uint32_t *)(uintptr_t)e->stack_bottom)[i]=UINT32_C(0x4b535431);
    if(*(const uint32_t *)(uintptr_t)e->resume_cell || *(const uint32_t *)(uintptr_t)e->consumer_cell)
        native_report(8u,3u);
    *(uint32_t *)(uintptr_t)e->resume_cell=native_driver.resume;
    *(uint32_t *)(uintptr_t)e->consumer_cell=native_driver.consumer;
#if KUI_VMU_NATIVE_CACHED_CODE
    /* The lease's P1 lines were purged before the P2 copy. Publish the new
     * instructions once, after relocation and initialization, before entry.
     * This preserves the game's cache mode; mutable state stays in P2. */
    kui_native_passive_publish(load,load+bytes);
#endif
    uint32_t unused=0;
    if(kui_vmu_native_worker_call(e->arm,e->stack_top,
       (const struct kui_vmu_native_worker_config *)(const void *)&cfg,
       (struct kui_vmu_native_worker_result *)(void *)&unused)) native_report(9u,1u);
    if(!kui_vmu_native_heap_lease_valid(lease,native_read,NULL,1u) ||
       native_word(native_heap.initializer_slot)!=KUI_VMU_OBSERVE_LOW_HEAP_HOOK ||
       native_word(native_driver.completion_slot)!=native_driver.consumer) native_report(9u,2u);
    static const uint16_t original[6]={0x9028u,0x63e2u,0x023eu,0x12c6u,0x9029u,0x63e2u};
    if(memcmp((const void *)(uintptr_t)native_driver.submit,original,sizeof(original))) native_report(9u,3u);
#if KUI_VMU_NATIVE_READONLY_PROBE
    if(memcmp((const void *)(uintptr_t)native_heap.initializer,readonly_init_window,sizeof(readonly_init_window)) ||
       memcmp((const void *)(uintptr_t)native_heap.release,readonly_release_window,sizeof(readonly_release_window)) ||
       !readonly_release_replay_valid()) native_report(9u,5u);
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(native_runtime_mode()==KUI_VMU_NATIVE_MODE_LEASE_ONLY) {
        native_passive_lease_only(lease,e);return;
    }
#endif
    /* The first real initializer has returned and our lease is allocated.
     * Every later direct/indirect entry now stops before reclaiming it. */
    readonly_patch_entry(native_heap.initializer,KUI_VMU_OBSERVE_LOW_RETURN_HOOK);
    readonly_patch_entry(native_heap.release,e->release_hook);
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    for(unsigned i=0;i<native_exits.count;i++) {
        if(memcmp((const void *)(uintptr_t)native_exits.entries[i],persistent_exit_windows[i],12u))
            native_report(9u,7u);
        readonly_patch_entry(native_exits.entries[i],KUI_VMU_OBSERVE_LOW_RETURN_HOOK);
    }
#endif
#endif
    const uint16_t patch[6]={0xd001u,0x402bu,0x0009u,0x0009u,
        (uint16_t)e->submit_hook,(uint16_t)(e->submit_hook>>16)};
    memcpy((void *)(uintptr_t)native_driver.submit,patch,sizeof(patch));
    kui_native_passive_publish(native_driver.submit,native_driver.resume);
    *(volatile uint32_t *)(uintptr_t)native_driver.completion_slot=e->complete_hook;
    native_publish(native_driver.completion_slot);
    if(native_word(native_driver.completion_slot|0x20000000u)!=e->complete_hook ||
       memcmp((const void *)(uintptr_t)(native_driver.submit|0x20000000u),patch,sizeof(patch)))
        native_report(9u,4u);
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    /* Exact Evolution revision only: replace its existing malloc-failure
     * arm with a private diagnostic. Successful allocations are untouched. */
    if(native_evolution_fault_install(&native_driver,(void *)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,KUI_RETAIL_EXEC_ADDRESS,e->allocation_failure_hook,
       kui_native_passive_publish)<0) native_report(9u,0x45u);
    if(native_evolution_trace_install(e,dest,bytes)<0) native_report(9u,0x46u);
#endif
    volatile struct kui_vmu_observe_boot_control *c=control();
    c->worker_end=e->memory_end;c->terminal=e->terminal;c->lease=lease->payload&UINT32_C(0x1fffffff);
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    c->sd_transaction=e->sd_transaction;
#endif
    c->installer=0;c->stage_stack=0;c->status=KUI_VMU_OBS_BOOT_INSTALLED;
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    if(cfg.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_RAW_CHAIN)
        retail_display_line("ACTIVE HOOKS / ORIGINAL MAPLE DMA LIST");
    else if(cfg.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_HOOKS_ONLY)
        retail_display_line("ALL HOOKS ACTIVE / VIRTUAL VMU OFF");
    else retail_display_line("PERSISTENT VIRTUAL A1 ARMED - STARTING GAME");
#elif KUI_VMU_NATIVE_READONLY_PROBE
    retail_display_line("READ ONLY VIRTUAL A1 ARMED - STARTING GAME");
#else
    retail_display_line("PASSIVE WORKER ARMED - STARTING GAME");
#endif
}
