/* SPDX-License-Identifier: GPL-3.0-only */
/* Isolated native allocator adapter and separately enabled worker probes. */
#if !KUI_VMU_NATIVE_HEAP_PROBE || KUI_VMU_READONLY_PROOF != 0
#error Native heap adapter requires its isolated native build
#endif
#include "kui/vmu_native_mode.h"
#include "kui/vmu_native_background.h"
#include "kui/vmu_native_startup_stack.h"
#include "kui/vmu_native_toggle.h"
__asm__(".global __kui_native_owner_stack_ip_stage\n"
        ".set __kui_native_owner_stack_ip_stage,1\n");
_Static_assert(KUI_VMU_NATIVE_OWNER_STACK_LOW-52u>
    (KUI_RETAIL_LOW_HOOK_STACK&UINT32_C(0x1fffffff))+32u,
    "Native inherited owner frame must clear retained reader/private stack");
#if KUI_VMU_NATIVE_CACHE_PROBE
__asm__(".section .rodata.native_runtime_mode,\"a\"\n.balign 4\n"
    ".global __native_runtime_mode_start\n"
    ".type __native_runtime_mode_start,@object\n"
    "__native_runtime_mode_start:\n.ascii \"KUINVM01\"\n.long 1,32,0\n.zero 12\n"
    ".size __native_runtime_mode_start,32\n"
    ".global __native_runtime_mode_end\n__native_runtime_mode_end:\n.previous\n");
extern const uint8_t native_runtime_mode_wire[] __asm__("__native_runtime_mode_start");
static uint32_t native_runtime_mode(void) __attribute__((noipa));
#else
static uint32_t native_runtime_mode(void) {return KUI_VMU_NATIVE_MODE_NORMAL;}
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
#include "retail_storage.h"
static enum kui_loader_sd_result native_persist_storage_init(struct kui_retail_storage *,uint32_t) __attribute__((noipa));
#define kui_retail_storage_init native_persist_storage_init
#endif
#define kui_retail_stage_relay kui_native_heap_base_relay
#include "retail_stage.c"
#undef kui_retail_stage_relay
#if KUI_NATIVE_SPLIT_CONTROLLER_GENERAL && KUI_NATIVE_GENERAL_LEAN_LAYOUT
__asm__(".global __kui_native_general_lean_stage_layout\n"
        ".set __kui_native_general_lean_stage_layout,1\n");
#if KUI_NATIVE_READAHEAD
__asm__(".global __kui_native_vmu_toggle_stage\n"
        ".set __kui_native_vmu_toggle_stage,1\n");
const struct kui_vmu_native_toggle_capability kui_vmu_native_toggle_capability
    __attribute__((used,section(".rodata.vmu_native_toggle")))={
    KUI_VMU_NATIVE_TOGGLE_MAGIC0,KUI_VMU_NATIVE_TOGGLE_MAGIC1,1u,32u,7u,
    UINT32_C(0x51000),UINT32_C(0x30000),1u};
#endif
#endif
#if KUI_NATIVE_STANDARD_VMU_ISOLATION || KUI_NATIVE_READER_ONLY_ISOLATION || KUI_NATIVE_METADATA_CACHE_ISOLATION || KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION
#if KUI_NATIVE_VMU_ONLY
#if KUI_NATIVE_VMU_ONLY_FULL_SUBMIT
__asm__(".global __kui_native_vmu_only_full_submit_stage\n"
        ".set __kui_native_vmu_only_full_submit_stage,1\n");
#endif
__asm__(".global __kui_native_vmu_only_transport_stage\n"
        ".set __kui_native_vmu_only_transport_stage," KUI_VMU_ONLY_TRANSPORT_TEXT "\n");
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES==UINT32_C(0x1c000),
    "VMU-only allocation contains exactly its worker and stack");
__asm__(".global __kui_native_vmu_only_stage\n"
        ".set __kui_native_vmu_only_stage,1\n"
        ".global __kui_native_isolation_lease_bytes\n"
        ".set __kui_native_isolation_lease_bytes,0x1c000\n");
#elif KUI_NATIVE_GENERAL_LEAN_LAYOUT
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES==UINT32_C(0x51000),
    "General isolation owns its exact compact lease");
__asm__(".global __kui_native_isolation_lease_bytes\n"
        ".set __kui_native_isolation_lease_bytes,0x51000\n");
#else
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES==UINT32_C(0x55000),
    "Diagnostic isolation retains its exact complete lease");
__asm__(".global __kui_native_isolation_lease_bytes\n"
        ".set __kui_native_isolation_lease_bytes,0x55000\n");
#endif
#endif
#if KUI_NATIVE_STANDARD_VMU_ISOLATION
__asm__(".global __kui_native_standard_vmu_isolation_stage\n"
        ".set __kui_native_standard_vmu_isolation_stage,1\n");
#endif
#if KUI_NATIVE_READER_ONLY_ISOLATION
__asm__(".global __kui_native_reader_only_isolation_stage\n"
        ".set __kui_native_reader_only_isolation_stage,1\n");
#endif
#if KUI_NATIVE_RA_TICK_OFF_ISOLATION
__asm__(".global __kui_native_ra_tick_off_isolation_stage\n"
        ".set __kui_native_ra_tick_off_isolation_stage,1\n");
#endif
#if KUI_NATIVE_E1_WAIT_CAPTURE
__asm__(".global __kui_native_e1_wait_capture_stage\n"
        ".set __kui_native_e1_wait_capture_stage,1\n");
#endif
#if KUI_NATIVE_MAPLE_BYPASS_ISOLATION
__asm__(".global __kui_native_maple_bypass_isolation_stage\n"
        ".set __kui_native_maple_bypass_isolation_stage,1\n");
#endif
#if KUI_NATIVE_MAPLE_DELAY_ISOLATION
__asm__(".global __kui_native_maple_delay_isolation_stage\n"
        ".set __kui_native_maple_delay_isolation_stage,1\n");
#endif
#if KUI_NATIVE_MAPLE_DELAY_COMPLETE_ONLY_ISOLATION
__asm__(".global __kui_native_maple_delay_complete_only_isolation_stage\n"
        ".set __kui_native_maple_delay_complete_only_isolation_stage,1\n");
#endif
#if KUI_NATIVE_MAPLE_DELAY_SUBMIT_ONLY_ISOLATION
__asm__(".global __kui_native_maple_delay_submit_only_isolation_stage\n"
        ".set __kui_native_maple_delay_submit_only_isolation_stage,1\n");
#endif
#if KUI_NATIVE_MAPLE_DELAY_POST_START_ISOLATION
__asm__(".global __kui_native_maple_delay_post_start_isolation_stage\n"
        ".set __kui_native_maple_delay_post_start_isolation_stage,1\n");
#endif
#if KUI_NATIVE_METADATA_CACHE_ISOLATION
__asm__(".global __kui_native_metadata_cache_isolation_stage\n"
        ".set __kui_native_metadata_cache_isolation_stage,1\n");
#endif
#if KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION
__asm__(".global __kui_native_split_controller_isolation_stage\n"
        ".set __kui_native_split_controller_isolation_stage,1\n");
#endif
#if KUI_NATIVE_SPLIT_CONTROLLER_GENERAL
__asm__(".global __kui_native_split_controller_general_stage\n"
        ".set __kui_native_split_controller_general_stage,1\n");
#endif
#if KUI_NATIVE_SPLIT_CONTROLLER_PREFETCH
__asm__(".global __kui_native_split_controller_prefetch_stage\n"
        ".set __kui_native_split_controller_prefetch_stage,1\n");
#endif
#if KUI_NATIVE_SPLIT_CONTROLLER_STREAM_GUARD
__asm__(".global __kui_native_split_controller_stream_guard_stage\n"
        ".set __kui_native_split_controller_stream_guard_stage,1\n");
#endif
#if KUI_NATIVE_CURRENT_BOTTOM_HEAP
#define KUI_NATIVE_COLD_PROGRESS_STAGE 1
__asm__(".global __kui_native_cold_progress_stage\n"
        ".set __kui_native_cold_progress_stage,1\n");
#else
#define KUI_NATIVE_COLD_PROGRESS_STAGE 0
#endif
#if KUI_VMU_NATIVE_CACHE_PROBE
static uint32_t native_runtime_mode(void) {
    int mode=kui_vmu_native_mode_decode(native_runtime_mode_wire,KUI_VMU_NATIVE_MODE_BYTES);
    if(mode<0) stopped("NATIVE VMU RUNTIME MODE REFUSED",(uint32_t)mode);
#if !KUI_NATIVE_SPLIT_CONTROLLER_GENERAL || !KUI_NATIVE_GENERAL_LEAN_LAYOUT || !KUI_NATIVE_READAHEAD
    if(mode==KUI_VMU_NATIVE_MODE_READER_ONLY)
        stopped("VMU OFF NEEDS MATCHING GENERAL READER",(uint32_t)mode);
#else
    /* Retain the capability in the linked module; the launcher checks this
     * exact record before it writes the immutable mode slot. */
    if(mode==KUI_VMU_NATIVE_MODE_READER_ONLY &&
       ((volatile const struct kui_vmu_native_toggle_capability *)&kui_vmu_native_toggle_capability)->off_mode!=7u)
        stopped("VMU OFF CAPABILITY REFUSED",(uint32_t)mode);
#endif
#if KUI_VMU_NATIVE_BACKGROUND
    /* The Background manifest moves into the live worker reservation.
     * These diagnostics either skip that allocation or permit its reclaim. */
    if(mode==KUI_VMU_NATIVE_MODE_CONTROL || mode==KUI_VMU_NATIVE_MODE_LEASE_ONLY)
        stopped("BACKGROUND VMU REQUIRES LIVE WORKER RESERVATION",(uint32_t)mode);
#endif
    return (uint32_t)mode;
}
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
#undef kui_retail_storage_init
#include "vmu_native_persist_stage.inc"
#endif
#include "kui/vmu_native_discovery.h"
#include "kui/vmu_native_heap.h"
#if KUI_NATIVE_CURRENT_BOTTOM_HEAP
#include "kui/vmu_native_heap_budget.h"
#endif
#include "kui/vmu_native_program.h"
#include "kui/vmu_native_stage_placement.h"
#include "kui/vmu_maple_observe_boot.h"
#include "kui/retail_observe.h"
#include "kui/hash.h"
#include "vmu_maple_observe_symbols.h"
const uint32_t kui_vmu_maple_profile=1u;
extern uint32_t kui_vmu_native_heap_original;
static struct kui_vmu_native_discovery native_driver;
static struct kui_vmu_native_heap native_heap;
static struct kui_vmu_native_heap_call native_call;
static uint8_t native_heap_code_sha[32];
/* The relay's ordinary high stack is retired before the game calls its heap
 * initializer. Its post-initializer callback must use separately protected
 * stage memory: a legal tail allocation may contain the old relay stack. */
static uint8_t native_installer_stack[UINT32_C(0x10000)] __attribute__((aligned(32)));
extern const uint8_t __retail_stage_bss_end[] __asm__("__retail_stage_bss_end");
static bool __attribute__((noipa)) allocation_safe(uint32_t begin,uint32_t bytes) {
#if KUI_NATIVE_CURRENT_BOTTOM_HEAP
    return kui_vmu_native_stage_bottom_allocation_safe(&native_heap,exec_bytes,
        KUI_RETAIL_STAGE_ADDRESS,(uint32_t)(uintptr_t)__retail_stage_bss_end,begin,bytes);
#else
    return kui_vmu_native_stage_allocation_safe(&native_heap,exec_bytes,
        KUI_RETAIL_STAGE_ADDRESS,(uint32_t)(uintptr_t)__retail_stage_bss_end,begin,bytes);
#endif
}
static void native_report(uint32_t phase,uint32_t detail) __attribute__((noreturn));
#if KUI_NATIVE_DUAL_READERS
#include "vmu_maple_observe_legacy_symbols.h"
/* Each low reader is linked separately. Its entry addresses travel together
 * from that reader's typed ELF symbols; a valid X address is not a Y entry.
 * The tuple remains immutable temporary-stage data and never grants runtime
 * ownership or enlarges the retained worker reservation. */
struct native_low_bindings {
    uint32_t heap_hook,return_hook,control,heap_original,sd_lease,background_bind;
};
static const struct native_low_bindings native_low_bindings_table[2]={
    {KUI_VMU_OBSERVE_LOW_HEAP_HOOK,KUI_VMU_OBSERVE_LOW_RETURN_HOOK,
     KUI_VMU_OBSERVE_LOW_CONTROL,KUI_VMU_NATIVE_HEAP_ORIGINAL,
     KUI_VMU_OBSERVE_LOW_SD_LEASE,KUI_VMU_NATIVE_BACKGROUND_BIND},
    {LEGACY_KUI_VMU_OBSERVE_LOW_HEAP_HOOK,LEGACY_KUI_VMU_OBSERVE_LOW_RETURN_HOOK,
     LEGACY_KUI_VMU_OBSERVE_LOW_CONTROL,LEGACY_KUI_VMU_NATIVE_HEAP_ORIGINAL,
     LEGACY_KUI_VMU_OBSERVE_LOW_SD_LEASE,LEGACY_KUI_VMU_NATIVE_BACKGROUND_BIND}
};
static const char native_low_bindings_refused[]="DUAL LOW CALLBACK READER REFUSED";
__asm__(".global __kui_native_dual_low_callbacks_stage\n"
        ".set __kui_native_dual_low_callbacks_stage,1\n");
static const struct native_low_bindings * __attribute__((noipa)) native_low_bindings(void) {
    if(!kui_native_dual_reader_valid(manifest.reader,manifest.storage_transport))
        /* No selected control cell exists on this path. The detailed native
         * reporter reads that cell and must not be used recursively here. */
        stopped(native_low_bindings_refused,manifest.reader);
    return &native_low_bindings_table[kui_native_reader_uses_sdk(manifest.reader)?0u:1u];
}
#define NATIVE_LOW_HEAP_HOOK (native_low_bindings()->heap_hook)
#define NATIVE_LOW_RETURN_HOOK (native_low_bindings()->return_hook)
#define NATIVE_LOW_CONTROL (native_low_bindings()->control)
#define NATIVE_LOW_HEAP_ORIGINAL (native_low_bindings()->heap_original)
#define NATIVE_LOW_SD_LEASE (native_low_bindings()->sd_lease)
#define NATIVE_LOW_BACKGROUND_BIND (native_low_bindings()->background_bind)
#else
#define NATIVE_LOW_HEAP_HOOK KUI_VMU_OBSERVE_LOW_HEAP_HOOK
#define NATIVE_LOW_RETURN_HOOK KUI_VMU_OBSERVE_LOW_RETURN_HOOK
#define NATIVE_LOW_CONTROL KUI_VMU_OBSERVE_LOW_CONTROL
#define NATIVE_LOW_HEAP_ORIGINAL KUI_VMU_NATIVE_HEAP_ORIGINAL
#define NATIVE_LOW_SD_LEASE KUI_VMU_OBSERVE_LOW_SD_LEASE
#define NATIVE_LOW_BACKGROUND_BIND KUI_VMU_NATIVE_BACKGROUND_BIND
#endif
static volatile struct kui_vmu_observe_boot_control *control(void) {
    return (volatile struct kui_vmu_observe_boot_control *)(uintptr_t)
        (NATIVE_LOW_CONTROL+UINT32_C(0x20000000));
}
static uint32_t native_word(uint32_t address) {return *(volatile const uint32_t *)(uintptr_t)address;}
static void native_publish(uint32_t address) {
    __asm__ volatile("ocbp @%0"::"r"(address&~31u):"memory");
}
static void code_sha(uint8_t out[32]) {
    struct kui_sha256 h;kui_sha256_init(&h);
    kui_sha256_update(&h,(const void *)(uintptr_t)native_heap.initializer,0x1d0u);
    kui_sha256_digest(&h,out);
}
static bool native_read(void *context,uint32_t address,uint32_t *out) {
    (void)context;uint32_t area=address&0xe0000000u,p=address&0x1fffffffu;
    if((area!=0u && area!=0x80000000u && area!=0xa0000000u) ||
       (address&3u) || p<0x0c000000u || p>0x0cfffffcu) return false;
    *out=native_word(address);return true;
}
static void native_report(uint32_t phase,uint32_t detail) {
    volatile struct kui_vmu_observe_boot_control *c=control();
    c->error=detail;
    retail_display_restore(&display);
    retail_display_line("AUTOMATIC NATIVE ALLOCATOR PROBE");
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    retail_display_line("PERSISTENT VMU PREPARATION STOPPED");
#else
    retail_display_line("NO VIRTUAL VMU / NO SAVE WRITES");
#endif
    retail_display_hex("PHASE",phase);retail_display_hex("DETAIL",detail);
    retail_display_hex("SDK SUBMIT",native_driver.submit);
    retail_display_hex("INITIALIZER",native_heap.initializer);
    retail_display_hex("INITIALIZER SLOT",native_heap.initializer_slot);
    retail_display_hex("ALLOCATE",native_heap.allocate);
    retail_display_hex(phase==2u?"SCANNED HEAP BEGIN":"ENTRY HEAP BEGIN",phase==2u?native_call.entry_begin:c->heap_begin);
    retail_display_hex(phase==2u?"SCANNED HEAP BYTES":"ENTRY HEAP BYTES",phase==2u?native_call.entry_bytes:c->heap_bytes);
    if(phase!=2u) {
        retail_display_hex("ALLOCATED PAYLOAD",c->lease);
        retail_display_hex("ORIGINAL RETURN",c->original_pr);
    }
    stopped(phase==7u?"ALLOCATION VERIFIED - PHOTO THEN POWER OFF":"PROBE REFUSED - PHOTO THEN POWER OFF",phase);
}
#if KUI_NATIVE_DUAL_READERS
/* A reviewed fixed arena (a kind 2 record) borrows one startup call that
 * returns after the SDK hardware init has installed its interrupt vector,
 * instead of an initializer pointer; the heap record then names the borrowed
 * literal as its initializer slot. The trampoline below runs once, on the
 * game's stack. It first gives the literal the low heap hook, as an armed
 * initializer slot holds it, so a second call through the literal stops at
 * the terminal and the worker witnesses the usual hook. It then makes the
 * borrowed call with the caller's arguments; the target takes none on the
 * stack. Last, keeping the call's R0/R1 results, it calls the low heap hook
 * with the arena as the initializer's arguments: the hook initializes the
 * game's own, otherwise unused allocator there and runs the installer, whose
 * SDK SCI route needs that vector. Then it returns to the caller. */
static struct kui_vmu_native_heap_fixed_hook native_fixed_hook;
static uint32_t native_fixed;
struct native_fixed_call {uint32_t heap_hook,begin,bytes,target,slot;};
struct native_fixed_call native_fixed_call __attribute__((used));
extern const char kui_native_fixed_call_hook[] __asm__("_kui_native_fixed_call_hook");
extern const char kui_native_fixed_call_return[] __asm__("_kui_native_fixed_call_return");
__asm__(".section .text.native_fixed_call,\"ax\"\n.align 2\n"
    ".global _kui_native_fixed_call_hook\n.type _kui_native_fixed_call_hook,@function\n"
    "_kui_native_fixed_call_hook:\n"
    "sts.l pr,@-r15\nmov.l 1f,r0\nmov.l @r0,r1\nmov.l @(16,r0),r2\nmov.l r1,@r2\nocbp @r2\n"
    "mov.l @(12,r0),r0\njsr @r0\nnop\n"
    "mov.l r0,@-r15\nmov.l r1,@-r15\nmov.l 1f,r0\nmov.l @r0,r1\n"
    "mov.l @(4,r0),r4\nmov.l @(8,r0),r5\njsr @r1\nnop\n"
    ".global _kui_native_fixed_call_return\n_kui_native_fixed_call_return:\n"
    "mov.l @r15+,r1\nmov.l @r15+,r0\nlds.l @r15+,pr\nrts\nnop\n"
    ".align 2\n1: .long _native_fixed_call+0x20000000\n"
    ".size _kui_native_fixed_call_hook,.-_kui_native_fixed_call_hook\n.previous\n");
/* Select the reviewed plan for this exact image; a replayed record must
 * name exactly the planned call and arena. */
static bool __attribute__((noipa)) native_fixed_plan_bind(bool replayed) {
    struct kui_vmu_native_heap_call planned;
    if(native_heap.initializer_slot || native_heap.initializer_target ||
       !kui_vmu_native_heap_fixed_plan((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,exec_bytes,
           native_driver.image_sha256,&native_heap,&planned,&native_fixed_hook) ||
       (replayed && memcmp(&planned,&native_call,sizeof(planned)))) return false;
    native_call=planned;
    native_heap.initializer_slot=native_fixed_hook.slot;
    native_heap.initializer_target=native_heap.initializer;
    native_fixed=1u;return true;
}
/* The low heap hook returns into the trampoline for a fixed arena. */
static uint32_t native_hook_return(void) {
    return native_fixed?(uint32_t)(uintptr_t)kui_native_fixed_call_return&UINT32_C(0x1fffffff):
        native_call.return_pr&UINT32_C(0x1fffffff);
}
#else
/* Single-reader modules keep the initializer path only. */
#define native_hook_return() (native_call.return_pr&0x1fffffffu)
#endif
#if KUI_VMU_NATIVE_WORKER_PROBE
#include "vmu_native_worker_stage.inc"
#elif KUI_VMU_NATIVE_PASSIVE_PROBE
#include "vmu_native_passive_stage.inc"
#endif
#if KUI_NATIVE_CURRENT_BOTTOM_HEAP
__asm__(".global __kui_native_dispatch_read_gate_stage\n"
        ".set __kui_native_dispatch_read_gate_stage,1\n");
__asm__(".global __kui_native_bottom_heap_stage\n"
        ".set __kui_native_bottom_heap_stage,1\n");
__asm__(".global __kui_native_private_pool_budget_stage\n"
        ".set __kui_native_private_pool_budget_stage,1\n");
static void __attribute__((noipa)) native_budget_prepare(
    const struct kui_vmu_native_heap_lease *lease,struct kui_vmu_native_heap_budget *budget) {
    if(lease->lease_bytes>UINT32_MAX-32u ||
       kui_vmu_native_heap_budget_prepare((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
           exec_bytes,&native_heap,&native_call,native_read,NULL,
           lease->lease_bytes+32u,budget)<0)
        native_report(6u,0x64u);
}
static void __attribute__((noipa)) native_budget_publish(
    const struct kui_vmu_native_heap_lease *lease,const struct kui_vmu_native_heap_budget *budget) {
    if(!budget->reserve_address)return;
    if(native_word(native_heap.initializer_slot)!=NATIVE_LOW_HEAP_HOOK ||
       !kui_vmu_native_heap_lease_valid(lease,native_read,NULL,1u) ||
       native_word(budget->reserve_address)!=budget->reserve_before)
        native_report(6u,0x65u);
    *(volatile uint32_t *)(uintptr_t)budget->reserve_address=budget->reserve_after;
    native_publish(budget->reserve_address);
    if(native_word(budget->reserve_address|UINT32_C(0x20000000))!=budget->reserve_after)
        native_report(6u,0x66u);
}
static bool __attribute__((noipa)) native_allocator_unchanged(void) {
    uint32_t sr;uint8_t digest[32];
    __asm__ volatile("stc sr,%0":"=r"(sr)::"memory");
    code_sha(digest);
    return ((sr>>4)&15u)==15u && !memcmp(digest,native_heap_code_sha,sizeof(digest));
}
/* This transaction uses only the owner's original allocation/free entries.
 * The temporary upper allocation is never initialized. Its sole purpose is
 * to select the lower chunk while preserving the original upper free end. */
static void __attribute__((noipa)) native_allocate_bottom(
    struct kui_vmu_native_heap_lease *lease) {
    if(!kui_vmu_native_heap_bottom_code_valid(&native_heap,native_read,NULL))
        native_report(6u,0x63u);
    void *(*allocate)(uint32_t)=(void *(*)(uint32_t))(uintptr_t)native_heap.allocate;
    void (*release)(void *)=(void (*)(void *))(uintptr_t)native_heap.release;
    uint32_t temporary=(uint32_t)(uintptr_t)allocate(lease->heap_bytes-lease->lease_bytes-64u);
    if(!native_allocator_unchanged() ||
       !kui_vmu_native_heap_bottom_placeholder(lease,native_read,NULL,1u,temporary))
        native_report(6u,0x60u);
    uint32_t payload=(uint32_t)(uintptr_t)allocate(lease->lease_bytes),release_bytes=0u;
    if(!native_allocator_unchanged() ||
       !kui_vmu_native_heap_bottom_allocated(lease,native_read,NULL,1u,payload) ||
       !kui_vmu_native_heap_lease_valid_with_release(lease,native_read,NULL,1u,
           temporary,&release_bytes) || release_bytes!=lease->remaining*32u ||
       !kui_vmu_native_heap_bottom_code_valid(&native_heap,native_read,NULL))
        native_report(6u,0x61u);
    release((void *)(uintptr_t)temporary);
    if(!native_allocator_unchanged() ||
       !kui_vmu_native_heap_bottom_ready(lease,native_read,NULL,1u) ||
       !kui_vmu_native_heap_lease_valid(lease,native_read,NULL,1u))
        native_report(6u,0x62u);
    control()->lease=payload;
}
#endif
void kui_vmu_observe_stage_install(void) {
    volatile struct kui_vmu_observe_boot_control *c=control();uint8_t digest[32];
    uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr));
    if(((sr>>4)&15u)!=15u || c->status!=KUI_VMU_OBS_BOOT_INITIALIZING)
        native_report(3u,sr);
#if KUI_NATIVE_COLD_PROGRESS_STAGE
    retail_display_line("COLD 20 HEAP INITIALIZER RETURNED");
#endif
    code_sha(digest);
    if(memcmp(digest,native_heap_code_sha,sizeof(digest))) native_report(3u,native_heap.initializer);
    uint32_t begin=(c->heap_begin+31u)&~31u,bytes=c->heap_bytes&~31u;
    struct kui_vmu_native_heap_lease lease;
    if(c->heap_begin!=native_call.entry_begin || c->heap_bytes!=native_call.entry_bytes ||
       !allocation_safe(begin,bytes) ||
#if KUI_NATIVE_CURRENT_BOTTOM_HEAP
       !kui_vmu_native_heap_bottom_fresh(&native_heap,native_read,NULL,begin,bytes,1u,
#else
       !kui_vmu_native_heap_fresh(&native_heap,native_read,NULL,begin,bytes,1u,
#endif
           KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES,&lease))
        native_report(4u,begin);
    uint32_t pr=c->original_pr&0x1fffffffu,sp=c->entry_sp&0x1fffffffu;
    if((c->original_pr&1u) || pr!=native_hook_return() ||
       (sp&3u) || sp<KUI_VMU_NATIVE_OWNER_STACK_LOW || sp>KUI_VMU_NATIVE_OWNER_STACK_HIGH ||
       !kui_vmu_native_heap_preallocate(&lease,native_read,NULL,1u))
        native_report(5u,c->entry_sp);
#if KUI_NATIVE_COLD_PROGRESS_STAGE
    retail_display_line("COLD 21 HEAP ALLOCATION BEGIN");
#endif
#if KUI_NATIVE_CURRENT_BOTTOM_HEAP
    struct kui_vmu_native_heap_budget budget;
    native_budget_prepare(&lease,&budget);
    native_allocate_bottom(&lease);
    native_budget_publish(&lease,&budget);
#else
    void *(*allocate)(uint32_t)=(void *(*)(uint32_t))(uintptr_t)native_heap.allocate;
    c->lease=(uint32_t)(uintptr_t)allocate(lease.lease_bytes);
    __asm__ volatile("stc sr,%0":"=r"(sr));
    code_sha(digest);
    if(((sr>>4)&15u)!=15u || memcmp(digest,native_heap_code_sha,sizeof(digest)) ||
       !kui_vmu_native_heap_postallocate(&lease,native_read,NULL,1u,c->lease) ||
       !kui_vmu_native_heap_lease_valid(&lease,native_read,NULL,1u))
        native_report(6u,c->lease);
#endif
    c->status=KUI_VMU_OBS_BOOT_LEASED;
#if KUI_NATIVE_COLD_PROGRESS_STAGE
    retail_display_line("COLD 22 HEAP ALLOCATION VERIFIED");
#endif
#if KUI_VMU_NATIVE_WORKER_PROBE
    native_worker_run(&lease);
#elif KUI_VMU_NATIVE_PASSIVE_PROBE
    native_passive_run(&lease);
#else
    native_report(7u,lease.lease_bytes);
#endif
}
#if KUI_NATIVE_DUAL_READERS
static void __attribute__((noipa)) native_arm_heap_hook(const uint32_t *frame) {
    const struct native_low_bindings *low=native_low_bindings();
    volatile struct kui_vmu_observe_boot_control *c=(void *)(uintptr_t)
        (low->control+UINT32_C(0x20000000));
    for(unsigned i=0;i<sizeof(*c)/4u;i++)
        if(((volatile uint32_t *)c)[i]) native_report(2u,low->control+4u*i);
    volatile uint32_t *original=(volatile uint32_t *)(uintptr_t)
        (low->heap_original+UINT32_C(0x20000000));
    for(unsigned i=0;i<8u;i++) if(original[i]) native_report(2u,low->heap_original+4u*i);
    if(native_word(native_heap.initializer_slot)!=native_heap.initializer_target) native_report(2u,native_heap.initializer_slot);
    *original=native_heap.initializer_target;
    original[1]=native_call.entry_begin;original[2]=native_call.entry_bytes;
    original[3]=native_call.return_pr&0x1fffffffu;
    c->installer=(uint32_t)(uintptr_t)kui_vmu_observe_stage_install;
    c->stage_stack=(uint32_t)(uintptr_t)(native_installer_stack+sizeof(native_installer_stack)-32u);
    c->status=KUI_VMU_OBS_BOOT_ARMED;
    *(volatile uint32_t *)(uintptr_t)native_heap.initializer_slot=low->heap_hook;
    native_publish(native_heap.initializer_slot);
    if(native_word(native_heap.initializer_slot|UINT32_C(0x20000000))!=low->heap_hook)
        native_report(2u,native_heap.initializer_slot);
    ((uint32_t *)(uintptr_t)frame)[5]=low->return_hook;
}
/* native_arm_heap_hook's counterpart for a fixed arena. The borrowed literal
 * holds the planned target; it receives the trampoline, and the low heap hook
 * returns into the trampoline rather than to the borrowed call's caller. */
static void __attribute__((noipa)) native_arm_fixed_call(const uint32_t *frame) {
    const struct native_low_bindings *low=native_low_bindings();
    volatile struct kui_vmu_observe_boot_control *c=(void *)(uintptr_t)
        (low->control+UINT32_C(0x20000000));
    for(unsigned i=0;i<sizeof(*c)/4u;i++)
        if(((volatile uint32_t *)c)[i]) native_report(2u,low->control+4u*i);
    volatile uint32_t *original=(volatile uint32_t *)(uintptr_t)
        (low->heap_original+UINT32_C(0x20000000));
    for(unsigned i=0;i<8u;i++) if(original[i]) native_report(2u,low->heap_original+4u*i);
    uint32_t slot=native_heap.initializer_slot,hook=(uint32_t)(uintptr_t)kui_native_fixed_call_hook;
    if(!native_fixed || native_word(slot)!=native_fixed_hook.target) native_report(2u,slot);
    retail_display_line("FIXED RAM ARENA FROM REVIEWED RAM MAP");
    *original=native_heap.initializer_target;
    original[1]=native_call.entry_begin;original[2]=native_call.entry_bytes;
    original[3]=native_hook_return();
    native_fixed_call=(struct native_fixed_call){low->heap_hook,native_call.entry_begin,
        native_call.entry_bytes,native_fixed_hook.target,slot};
    c->installer=(uint32_t)(uintptr_t)kui_vmu_observe_stage_install;
    c->stage_stack=(uint32_t)(uintptr_t)(native_installer_stack+sizeof(native_installer_stack)-32u);
    c->status=KUI_VMU_OBS_BOOT_ARMED;
    *(volatile uint32_t *)(uintptr_t)slot=hook;
    native_publish(slot);
    if(native_word(slot|UINT32_C(0x20000000))!=hook) native_report(2u,slot);
    ((uint32_t *)(uintptr_t)frame)[5]=low->return_hook;
}
#endif
void kui_retail_stage_relay(const uint32_t *frame,uint32_t ccr) {
#if KUI_VMU_NATIVE_WORKER_PROBE || KUI_VMU_NATIVE_PASSIVE_PROBE
    retail_display_restore(&display);
    retail_display_line("BOOTSTRAP 2 RETURNED");
    retail_display_line("VERIFYING EXECUTABLE AND RESIDENT");
#endif
#if KUI_NATIVE_COLD_PROGRESS_STAGE
    retail_display_line("COLD 01 BASE RELAY BEGIN");
#endif
    kui_native_heap_base_relay(frame,ccr);
#if KUI_NATIVE_COLD_PROGRESS_STAGE
    retail_display_line("COLD 02 BASE RELAY VERIFIED");
#endif
    /* The matched control uses the same reader, bootstrap verification and
     * owner CPU/cache restoration, but never arms a native hook or lease.
     * Storage preparation has independently taken its no-VMU branch. */
    if(native_runtime_mode()==KUI_VMU_NATIVE_MODE_CONTROL) {
        retail_display_line("NATIVE VMU CONTROL / NO VIRTUAL CARD");
        return;
    }
    if((manifest.storage_transport!=KUI_STORAGE_SCI
#if KUI_NATIVE_VMU_ONLY
        && manifest.storage_transport!=KUI_STORAGE_SCIF
#endif
       ) ||
#if KUI_VMU_NATIVE_BACKGROUND
       (manifest.reader!=KUI_RETAIL_READER_ASYNC && manifest.reader!=KUI_RETAIL_READER_ASYNC_EAGER) ||
#else
       manifest.reader!=KUI_RETAIL_READER_STANDARD ||
#endif
       (manifest.flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED)) ||
       !kui_retail_observe_native_ip((const uint8_t *)(uintptr_t)KUI_RETAIL_IP_ADDRESS))
        native_report(1u,manifest.storage_transport);
/* The base relay has verified the entire loaded file, including any
     * appended bootstrap. Only a caller-proven reclaimed suffix may cease
     * being part of the live executable before immutable native discovery.
     * This changes neither the loaded bytes nor the source CRC contract. */
    size_t program_bytes;
#if KUI_NATIVE_STAGE_FAST_SCAN
    retail_display_line("VERIFYING EXECUTABLE LIFETIME EXTENT");
#endif
#if KUI_NATIVE_CACHED_EXTENT
    /* Stage-only wrapper: exact same pure proof, with operand caching only
     * during that proof. It publishes RAM before restoring the relay CCR. */
    extern bool kui_vmu_native_cached_extent(const void *,size_t,uint32_t,size_t *);
#define native_program_extent kui_vmu_native_cached_extent
#else
#define native_program_extent kui_vmu_native_program_extent
#endif
#if KUI_NATIVE_COLD_PROGRESS_STAGE
    retail_display_line("COLD 03 EXECUTABLE EXTENT BEGIN");
#endif
    if(!native_program_extent((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,KUI_RETAIL_EXEC_ADDRESS,&program_bytes)) native_report(2u,13u);
#undef native_program_extent
#if KUI_NATIVE_COLD_PROGRESS_STAGE
    retail_display_line("COLD 04 EXECUTABLE EXTENT VERIFIED");
#endif
    if(program_bytes!=exec_bytes) retail_display_line("VERIFIED RECLAIMED EXECUTABLE SUFFIX");
    exec_bytes=(uint32_t)program_bytes;
#if KUI_VMU_NATIVE_CACHE_PROBE
#if KUI_NATIVE_STAGE_FAST_SCAN
    retail_display_line("VERIFYING NATIVE CACHE AND IMAGE SHA256");
#endif
#if KUI_NATIVE_COLD_PROGRESS_STAGE
    retail_display_line("COLD 05 NATIVE RESOLUTION BEGIN");
#endif
    bool mapping_cached=native_resolution_replay();
    if(mapping_cached) retail_display_line("USING VERIFIED NATIVE MAPPING CACHE");
    else {
        retail_display_line("NO MATCHING NATIVE CACHE - SCANNING");
#endif
#if KUI_VMU_NATIVE_PASSIVE_PROBE
    retail_display_line("RESOLVING MAPLE DRIVER AND BUFFER LAYOUT");
    if(!kui_vmu_native_discover_layout((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,KUI_RETAIL_EXEC_ADDRESS,&native_driver,&passive_layout)) native_report(2u,10u);
    int driver=KUI_VMU_NATIVE_RESOLVED;
#else
#if KUI_VMU_NATIVE_WORKER_PROBE
    retail_display_line("SCANNING MAPLE DRIVER AND IMAGE HASH");
#endif
    int driver=kui_vmu_native_discover((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
        exec_bytes,KUI_RETAIL_EXEC_ADDRESS,&native_driver);
#endif
#if KUI_VMU_NATIVE_WORKER_PROBE || KUI_VMU_NATIVE_PASSIVE_PROBE
    retail_display_line("SCANNING ALLOCATOR FAMILY");
#endif
    int heap=kui_vmu_native_heap_discover((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
        exec_bytes,KUI_RETAIL_EXEC_ADDRESS,&native_heap);
#if KUI_NATIVE_DUAL_READERS
    if(driver!=KUI_VMU_NATIVE_RESOLVED || heap!=KUI_VMU_NATIVE_HEAP_FOUND)
        native_report(2u,heap==KUI_VMU_NATIVE_HEAP_FOUND?0u:(uint32_t)heap);
    if(!(native_heap.initializer_slot?kui_vmu_native_heap_call_discover((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
         exec_bytes,&native_heap,&native_call):native_fixed_plan_bind(false)) ||
       !allocation_safe(native_call.entry_begin,native_call.entry_bytes))
        native_report(2u,native_heap.initializer_slot);
#else
    if(driver!=KUI_VMU_NATIVE_RESOLVED || heap!=KUI_VMU_NATIVE_HEAP_FOUND ||
       !native_heap.initializer_slot) native_report(2u,heap==KUI_VMU_NATIVE_HEAP_FOUND?0u:(uint32_t)heap);
    if(!kui_vmu_native_heap_call_discover((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
       exec_bytes,&native_heap,&native_call) || !allocation_safe(native_call.entry_begin,native_call.entry_bytes))
        native_report(2u,native_heap.initializer_slot);
#endif
#if KUI_VMU_NATIVE_PASSIVE_PROBE
    /* Layout already shares the driver's one fresh immutable-image pass. */
    retail_display_line("CHECKING DISPLACED INSTRUCTION WINDOW");
    native_passive_prepare();
#endif
#if KUI_VMU_NATIVE_CACHE_PROBE
    }
#endif
    code_sha(native_heap_code_sha);
#if KUI_NATIVE_COLD_PROGRESS_STAGE
    retail_display_line("COLD 06 NATIVE RESOLUTION VERIFIED");
#endif
#if KUI_NATIVE_STAGE_FAST_SCAN
    retail_display_line("NATIVE DISCOVERY COMPLETE - ARMING HOOK");
#endif
#if KUI_NATIVE_DUAL_READERS
    if(native_fixed) native_arm_fixed_call(frame);
    else native_arm_heap_hook(frame);
#else
    volatile struct kui_vmu_observe_boot_control *c=control();
    for(unsigned i=0;i<sizeof(*c)/4u;i++)
        if(((volatile uint32_t *)c)[i]) native_report(2u,KUI_VMU_OBSERVE_LOW_CONTROL+4u*i);
    volatile uint32_t *original=(volatile uint32_t *)(uintptr_t)
        (KUI_VMU_NATIVE_HEAP_ORIGINAL+UINT32_C(0x20000000));
    for(unsigned i=0;i<8u;i++) if(original[i]) native_report(2u,KUI_VMU_NATIVE_HEAP_ORIGINAL+4u*i);
    if(native_word(native_heap.initializer_slot)!=native_heap.initializer_target) native_report(2u,native_heap.initializer_slot);
    *original=native_heap.initializer_target;
    original[1]=native_call.entry_begin;original[2]=native_call.entry_bytes;
    original[3]=native_call.return_pr&0x1fffffffu;
    c->installer=(uint32_t)(uintptr_t)kui_vmu_observe_stage_install;
    c->stage_stack=(uint32_t)(uintptr_t)(native_installer_stack+sizeof(native_installer_stack)-32u);
    c->status=KUI_VMU_OBS_BOOT_ARMED;
    *(volatile uint32_t *)(uintptr_t)native_heap.initializer_slot=KUI_VMU_OBSERVE_LOW_HEAP_HOOK;
    native_publish(native_heap.initializer_slot);
    if(native_word(native_heap.initializer_slot|UINT32_C(0x20000000))!=KUI_VMU_OBSERVE_LOW_HEAP_HOOK)
        native_report(2u,native_heap.initializer_slot);
    ((uint32_t *)(uintptr_t)frame)[5]=KUI_VMU_OBSERVE_LOW_RETURN_HOOK;
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    /* The fourth entry is already terminal before resuming the initial
     * executable, including if that executable itself enters the extra CRT
     * path before reaching the allocator hook. */
    if(!native_crt_bootstrap_prearm()) native_report(2u,0x48u);
#endif
}
