/* SPDX-License-Identifier: GPL-3.0-only */
/* Separate passive observer. The ordinary retail translation unit is intact. */
#if !KUI_VMU_MAPLE_OBSERVE || !KUI_RETAIL_LOW_RESIDENT || defined(KUI_RETAIL_CE) || defined(KUI_RETAIL_ASYNC)
#error VMU observation resident requires its isolated synchronous native build
#endif
/* Admit declarations before the call-only observation macros. The physical
 * storage/image functions and their acquire/read/stop/release order remain
 * the ordinary implementation below. */
#include "kui/retail_gd.h"
#include "kui/retail_image.h"
#include "retail_storage.h"
#include "retail_display.h"
#include "kui/vmu_maple_observe_io.h"
#if KUI_VMU_READONLY_PROOF < 2 && !KUI_VMU_NATIVE_PERSISTENT_PROBE
static void observe_display_restore(const struct retail_display_state *);
#endif
#if !KUI_VMU_NATIVE_PERSISTENT_PROBE
#define kui_retail_storage_acquire(...) \
    KUI_VMU_OBSERVE_IO_ACQUIRE(service.diag.read_flags, \
        kui_retail_storage_acquire(__VA_ARGS__))
#define kui_retail_image_read(...) \
    KUI_VMU_OBSERVE_IO_IMAGE(service.diag.read_flags, \
        kui_retail_image_read(__VA_ARGS__))
#endif
#if KUI_VMU_READONLY_PROOF < 2 && !KUI_VMU_NATIVE_PERSISTENT_PROBE
#define retail_display_restore observe_display_restore
#endif
#define kui_retail_resident_init kui_vmu_observe_base_init
#include "retail_resident.c"
#undef kui_retail_resident_init
#undef retail_display_restore
#undef kui_retail_image_read
#undef kui_retail_storage_acquire
#include "kui/vmu_maple_observe_boot.h"
#include "kui/vmu_maple_observe_profile.h"

#if !KUI_RETAIL_GD_REJECTION_DETAILS
#error Observer I/O telemetry requires the standard native diagnostic fields
#endif

#if KUI_VMU_READONLY_PROOF < 2 && !KUI_VMU_NATIVE_PERSISTENT_PROBE
/* The SD-backed worker has its own storage diagnostics. Keep these optional
 * extra native-GD rows in passive/blank-card modes. SD-backed modes need
 * their low code bytes and C frame for the lease and restoration checks. The
 * ordinary native fault page remains present in every mode. */
static void observe_display_restore(const struct retail_display_state *state) {
    retail_display_restore(state);
    if(service.error != KUI_GD_ERROR_IO) return;
    /* Accepted reads have flags=0. In this separate observer only, the
     * diagnostic field retains the returned image enum until the terminal
     * screen. No post-read execution branch consumes it; REQUEST resets it.
     * Acquisition failure retains UINT32_MAX instead of a stale image result.
     * The native terminal header owns cache words0..8. Reuse its tail only
     * after I/O has stopped/released and will never resume. */
    typedef uint32_t observe_word __attribute__((__may_alias__));
    observe_word *row=(observe_word *)(void *)image.block+9;
    const volatile struct kui_vmu_observe_boot_control *c=
        (const volatile struct kui_vmu_observe_boot_control *)
        ((uintptr_t)&kui_vmu_observe_boot_control+UINT32_C(0x20000000));
    row[0]=service.diag.read_flags==UINT32_MAX?(uint32_t)card_result:0u;
    row[1]=(uint32_t)card_result;row[2]=service.diag.read_flags;
    row[3]=service.lba;row[4]=service.count;
    /* Capture both rows before rendering so no service/control pointers
     * need to survive a display call on the tightly bounded low stack. */
    row[5]=service.destination;row[6]=service.completed_bytes;
    row[7]=(c->status<<16)|(c->error&UINT32_C(0xffff));
    row[8]=c->lease;row[9]=*(volatile const uint32_t *)(uintptr_t)0xff00001cu;
    retail_display_values("ACQ SD IMAGE REQLBA REQCOUNT",row,5u);
    retail_display_values("DEST DONE BOOTERR LEASE CCR",row+5,5u);
}
#endif

#if KUI_VMU_NATIVE_PASSIVE_PROBE
/* NATIVE_RESIDENT_GUARDS_BEGIN
 * Pure admission predicates are also compiled by the host adversarial test.
 * The low control retains physical payload addresses; all worker entries and
 * mutable end use P2. The cached build admits P1 code only; legacy workers
 * execute from P2. No temporary-stage pointer is a runtime dependency. */
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
#define NATIVE_RESIDENT_WORKER_BYTES UINT32_C(0x20000)
#define NATIVE_RESIDENT_HEADER_BYTES 80u
#else
#define NATIVE_RESIDENT_WORKER_BYTES UINT32_C(0x10000)
#define NATIVE_RESIDENT_HEADER_BYTES 64u
#endif
static bool native_resident_lease(const volatile struct kui_vmu_observe_boot_control *c,
    uint32_t *begin,uint32_t *end) {
    uint32_t lease=c->lease;
    if((lease&31u) || lease<UINT32_C(0x0c010020) ||
       lease>UINT32_C(0x0cfd0000)) return false;
    *begin=lease-32u;*end=lease+UINT32_C(0x30000);return true;
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
    return native_resident_terminal(c) && target==c->sd_transaction && !(target&1u) &&
        (uint32_t)target-worker-NATIVE_RESIDENT_HEADER_BYTES<NATIVE_RESIDENT_WORKER_BYTES-NATIVE_RESIDENT_HEADER_BYTES;
}
#endif
/* NATIVE_RESIDENT_GUARDS_END */
#endif

#if KUI_VMU_READONLY_PROOF >= 2 || KUI_VMU_NATIVE_PERSISTENT_PROBE
#include "kui/vmu_maple_observe.h"
#include "vmu_sd_lease.inc"
#endif

void kui_vmu_observe_low_failure(void) __attribute__((noreturn));
static uint8_t *observe_map(void *context,uint32_t address,uint32_t bytes,int writing) {
#if KUI_VMU_NATIVE_PASSIVE_PROBE
    const volatile struct kui_vmu_observe_boot_control *c=
        (const volatile struct kui_vmu_observe_boot_control *)
        ((uintptr_t)&kui_vmu_observe_boot_control+UINT32_C(0x20000000));
    if(!native_resident_destination(c,address,bytes)) {
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
        /* Failure-only probe: reuse the existing validated worker terminal.
         * CHECK and other small result mappings can fail without an I/O
         * error; stop here before any owner/cache/storage access. */
        kui_vmu_observe_low_failure();
#else
        return NULL;
#endif
    }
#else
    if(!kui_vmu_observe_destination(address,bytes)) return NULL;
#endif
    return map_guest(context,address,bytes,writing);
}

int kui_retail_resident_init(const struct kui_retail_manifest *prepared,
    const struct kui_retail_storage *prepared_card,uint32_t original_gd_vector,
    const struct retail_display_state *saved_display) {
    int result=kui_vmu_observe_base_init(prepared,prepared_card,original_gd_vector,saved_display);
    service.ops.map=observe_map;
    purge((uint32_t)(uintptr_t)&service,sizeof(service));
    return result;
}

void kui_vmu_observe_low_failure(void) {
    const volatile struct kui_vmu_observe_boot_control *c=
        (const volatile struct kui_vmu_observe_boot_control *)
        ((uintptr_t)&kui_vmu_observe_boot_control+UINT32_C(0x20000000));
    /* A validated worker entry is published only after its initialization
     * succeeded. A late heap reset stops without destroying the live lease. */
    uint32_t terminal=c->terminal;
#if KUI_VMU_NATIVE_PASSIVE_PROBE
    if(native_resident_terminal(c))
#else
    if(c->status==KUI_VMU_OBS_BOOT_INSTALLED && !(terminal&1u) &&
       terminal>=KUI_VMU_OBS_LEASE_BEGIN && terminal<KUI_VMU_OBS_WORKER_END)
#endif
        ((void (*)(void))(uintptr_t)terminal)();
    retail_display_restore(&display);
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    /* Valid installed workers own the detailed report above. An invalid
     * control cannot safely enter them: the restored build header is the
     * bounded fallback, followed by a permanent stop. */
#else
#if KUI_VMU_NATIVE_PASSIVE_PROBE
    retail_display_line("NATIVE MAPLE OBSERVER STOPPED");
#else
    retail_display_line(KUI_VMU_OBS_TITLE " MAPLE OBSERVER STOPPED");
#endif
#if KUI_VMU_READONLY_PROOF >= 2 || KUI_VMU_NATIVE_PERSISTENT_PROBE
    /* Terminal state is immutable under the masked failure entry. These four
     * P2 words are already adjacent; avoid a diagnostic-only stack copy. */
    retail_display_values("STATE LEASE ERROR HEAP",
        (const uint32_t *)(uintptr_t)&c->status,4u);
#else
    retail_display_values("STATE ERROR LEASE HEAP",(uint32_t[4]){
        c->status,c->error,c->lease,c->heap_begin},4u);
#endif
#endif
    for(;;) __asm__ volatile("nop");
}
