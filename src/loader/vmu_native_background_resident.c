/* SPDX-License-Identifier: GPL-3.0-only */
/* Opt-in native Background SCI reader with the prepared native VMU lease.
 * Standard builds retain their independently linked synchronous resident. */
#if !KUI_VMU_NATIVE_BACKGROUND || !KUI_VMU_NATIVE_PERSISTENT_PROBE || !KUI_RETAIL_ASYNC || defined(KUI_RETAIL_CE) || !KUI_RETAIL_LOW_RESIDENT || !KUI_GENERAL_SCI_RESUME_FIX || !KUI_RETAIL_NATIVE_DRIVE_STATE || !KUI_RETAIL_NATIVE_DISC_CHECK
#error Native Background VMU resident requires its separate native SCI build
#endif
__asm__(".global __kui_vmu_native_background_resident\n"
        ".set __kui_vmu_native_background_resident,1\n");
#define kui_retail_resident_init kui_vmu_observe_background_base_init
#include "retail_resident.c"
#undef kui_retail_resident_init
#undef reader
#include "kui/vmu_maple_observe_boot.h"
#include "kui/vmu_sd_lease.h"
#include "kui/vmu_native_async_loan.h"
#include "kui/vmu_native_background.h"
#if KUI_NATIVE_GENERAL_LEAN_LAYOUT
__asm__(".global __kui_native_general_lean_resident_layout\n"
        ".set __kui_native_general_lean_resident_layout,1\n");
#endif

/* NATIVE_RESIDENT_GUARDS_BEGIN
 * Pure admission predicates are also compiled by the host adversarial test.
 * The low control retains physical payload addresses; all worker entries and
 * mutable end use P2. The cached build admits P1 code only; legacy workers
 * execute from P2. No temporary-stage pointer is a runtime dependency. */
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
#define NATIVE_RESIDENT_WORKER_BYTES KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES
#define NATIVE_RESIDENT_HEADER_BYTES 80u
#else
#define NATIVE_RESIDENT_WORKER_BYTES UINT32_C(0x10000)
#define NATIVE_RESIDENT_HEADER_BYTES 64u
#endif
static bool native_resident_lease(const volatile struct kui_vmu_observe_boot_control *c,
    uint32_t *begin,uint32_t *end) {
    uint32_t lease=c->lease;
    if((lease&31u) || lease<UINT32_C(0x0c010020) ||
       lease>UINT32_C(0x0d000000)-KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES) return false;
    *begin=lease-32u;*end=lease+KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES;return true;
}
static bool native_resident_destination(const volatile struct kui_vmu_observe_boot_control *c,
    uint32_t address,uint32_t bytes) {
    uint32_t area=address&UINT32_C(0xe0000000),physical=address&UINT32_C(0x1fffffff);
    if((area!=0u && area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000)) ||
       !bytes || physical<UINT32_C(0x0c000000) || physical>=UINT32_C(0x0d000000) ||
       bytes>UINT32_C(0x0d000000)-physical) return false;
    uint32_t status=c->status;
    /* Bootstrap reads precede the allocator hook. No lease has been
     * published yet, so ordinary resident mapping supplies their guards. */
    if(status<=KUI_VMU_OBS_BOOT_INITIALIZING) return !c->lease;
    uint32_t begin,end;
    if(status!=KUI_VMU_OBS_BOOT_INSTALLED ||
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
       (c->installer|c->stage_stack) ||
#else
       c->installer || c->stage_stack ||
#endif
       !native_resident_lease(c,&begin,&end) ||
       c->worker_end!=(c->lease|UINT32_C(0xa0000000))+NATIVE_RESIDENT_WORKER_BYTES) return false;
    return physical>=end || physical+bytes<=begin;
}
static bool native_resident_terminal(const volatile struct kui_vmu_observe_boot_control *c) {
    uint32_t terminal=c->terminal;
    uint32_t begin,end;
    if(c->status!=KUI_VMU_OBS_BOOT_INSTALLED ||
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
       (c->installer|c->stage_stack) ||
#else
       c->installer || c->stage_stack ||
#endif
       !native_resident_lease(c,&begin,&end)) return false;
    (void)begin;(void)end;
#if KUI_VMU_NATIVE_CACHED_CODE
    uint32_t worker=c->lease|UINT32_C(0x80000000);
    const uint32_t end_bias=UINT32_C(0x20000000)+NATIVE_RESIDENT_WORKER_BYTES;
#else
    uint32_t worker=c->lease|UINT32_C(0xa0000000);
    const uint32_t end_bias=NATIVE_RESIDENT_WORKER_BYTES;
#endif
    return c->worker_end==worker+end_bias && !(terminal&1u) &&
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
        terminal-worker-NATIVE_RESIDENT_HEADER_BYTES<NATIVE_RESIDENT_WORKER_BYTES-NATIVE_RESIDENT_HEADER_BYTES;
#else
        terminal>=worker+NATIVE_RESIDENT_HEADER_BYTES && terminal<c->worker_end;
#endif
}
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
/* The stage publishes one typed relocated callback after arming. The pure
 * low guard verifies its exact pin plus the complete runtime control shape;
 * the code alias must match this build; no stage pointer is valid. */
static bool native_resident_transaction(const volatile struct kui_vmu_observe_boot_control *c,
    uintptr_t target) {
    uint32_t worker=c->lease|
#if KUI_VMU_NATIVE_CACHED_CODE
        UINT32_C(0x80000000);
#else
        UINT32_C(0xa0000000);
#endif
    return native_resident_terminal(c) && (
#if KUI_NATIVE_COOP_PUMP && !KUI_NATIVE_PACED_CHECK && !KUI_NATIVE_IRQ_REARM
        !target ||
#endif
        (target==c->sd_transaction && !(target&1u) &&
        (uint32_t)target-worker-NATIVE_RESIDENT_HEADER_BYTES<NATIVE_RESIDENT_WORKER_BYTES-NATIVE_RESIDENT_HEADER_BYTES));
}
#endif
/* NATIVE_RESIDENT_GUARDS_END */

void kui_vmu_observe_low_failure(void) __attribute__((noreturn));
static uint8_t *native_background_map(void *context,uint32_t address,uint32_t bytes,int writing) {
    const volatile struct kui_vmu_observe_boot_control *c=
        (const volatile struct kui_vmu_observe_boot_control *)
        ((uintptr_t)&kui_vmu_observe_boot_control+UINT32_C(0x20000000));
    if(!native_resident_destination(c,address,bytes)) kui_vmu_observe_low_failure();
    return map_guest(context,address,bytes,writing);
}
int kui_vmu_native_background_init(const struct kui_retail_manifest *prepared,
    const struct kui_retail_storage *prepared_card,uint32_t original_gd_vector,
    const struct retail_display_state *saved_display,void *workspace) {
    uint32_t temporary=(uint32_t)(uintptr_t)prepared,area=(uint32_t)(uintptr_t)workspace;
    if(!prepared || !kui_vmu_native_background_stage_map_address(temporary) ||
       !kui_vmu_native_background_stage_region_address(area) ||
       (area<temporary+KUI_VMU_NATIVE_BACKGROUND_MAP_BYTES &&
        temporary<area+KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES) ||
       prepared->storage_transport!=KUI_STORAGE_SCI ||
       (prepared->reader!=KUI_RETAIL_READER_ASYNC && prepared->reader!=KUI_RETAIL_READER_ASYNC_EAGER))
        return KUI_RETAIL_RESIDENT_ARGUMENT;
    memset(workspace,0,KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES);
    kui_retail_async_region_storage=workspace;
    int result=kui_vmu_observe_background_base_init(prepared,prepared_card,original_gd_vector,saved_display);
    if(result) return result;
    service.ops.map=native_background_map;
    /* The owner's startup may invalidate its data cache. Unlike the normal
     * fixed low BSS, this temporary region is separately owned by the stage;
     * publish its vectors, engine, descriptor and complete service together. */
    purge(area,KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES);
    return result;
}
/* Runs on the worker's 4 KiB stack with IMASK15, never the 352-byte GD stack.
 * The native async loan owns the GD lock for this complete transaction. */
int __attribute__((noipa,section(".vmu_sd_lease"))) kui_vmu_observe_sd_lease(
    kui_vmu_sd_transaction callback,void *context) {
    const volatile struct kui_vmu_observe_boot_control *c=
        (const volatile struct kui_vmu_observe_boot_control *)
        ((uintptr_t)&kui_vmu_observe_boot_control+UINT32_C(0x20000000));
    uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr)::"memory");
    if((sr&UINT32_C(0xf0))!=UINT32_C(0xf0) ||
       !native_resident_transaction(c,(uintptr_t)callback)) return KUI_VMU_SD_LEASE_FAULT;
#if KUI_NATIVE_COOP_PUMP && !KUI_NATIVE_PACED_CHECK && !KUI_NATIVE_IRQ_REARM
    /* Explicit non-storage command on the existing typed worker gateway.
     * Serialized on the worker's stack after its physical controller proof;
     * no loan, virtual reply, bus acquisition or game interrupt setup occurs. */
    if(!callback) {
        if((uintptr_t)context!=KUI_RETAIL_NATIVE_PUMP_COOKIE) return KUI_VMU_SD_LEASE_FAULT;
        if(kui_retail_hook_active || kui_retail_async_region.engine.native_loan_state)
            return KUI_VMU_SD_LEASE_BUSY;
        kui_retail_async_region.engine.in_irq=2u;
        (void)kui_retail_async_irq();
        return KUI_VMU_SD_LEASE_OK;
    }
#endif
    struct kui_loader_sd loan;
    int result=kui_retail_native_async_loan_begin(&loan);
    if(result==KUI_VMU_NATIVE_ASYNC_LOAN_BUSY) return KUI_VMU_SD_LEASE_BUSY;
    if(result!=KUI_VMU_NATIVE_ASYNC_LOAN_OK) return KUI_VMU_SD_LEASE_FAULT;
    result=kui_retail_storage_acquire(&card);
    if(result==KUI_LOADER_SD_NOT_READY || result==KUI_LOADER_SD_UNSUPPORTED) {
        /* SCI refusal precedes every register write. Close the loan cleanly
         * and let the worker retry after the guest's DMA ownership ends. */
        return kui_retail_native_async_loan_end(true)==KUI_VMU_NATIVE_ASYNC_LOAN_OK?
            KUI_VMU_SD_LEASE_BUSY:KUI_VMU_SD_LEASE_FAULT;
    }
    if(result==KUI_LOADER_SD_OK) {
        result=callback(context,&loan);
        if(!loan.ready || !kui_sci_sd_healthy()) result=KUI_VMU_SD_LEASE_FAULT;
        kui_retail_storage_release(&card);
    }
    bool healthy=!result && !kui_sci_sd_faulted();
    if(kui_retail_native_async_loan_end(healthy)!=KUI_VMU_NATIVE_ASYNC_LOAN_OK)
        return KUI_VMU_SD_LEASE_FAULT;
    return healthy?KUI_VMU_SD_LEASE_OK:KUI_VMU_SD_LEASE_FAULT;
}
void kui_vmu_observe_low_failure(void) {
    const volatile struct kui_vmu_observe_boot_control *c=
        (const volatile struct kui_vmu_observe_boot_control *)
        ((uintptr_t)&kui_vmu_observe_boot_control+UINT32_C(0x20000000));
    if(native_resident_terminal(c)) ((void (*)(void))(uintptr_t)c->terminal)();
    retail_display_restore(&display);
    for(;;) __asm__ volatile("nop");
}
