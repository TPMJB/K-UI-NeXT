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
#if KUI_VMU_READONLY_PROOF < 2
static void observe_display_restore(const struct retail_display_state *);
#endif
#define kui_retail_storage_acquire(...) \
    KUI_VMU_OBSERVE_IO_ACQUIRE(service.diag.read_flags, \
        kui_retail_storage_acquire(__VA_ARGS__))
#define kui_retail_image_read(...) \
    KUI_VMU_OBSERVE_IO_IMAGE(service.diag.read_flags, \
        kui_retail_image_read(__VA_ARGS__))
#if KUI_VMU_READONLY_PROOF < 2
#define retail_display_restore observe_display_restore
#endif
#define kui_retail_resident_init kui_vmu_observe_base_init
#include "retail_resident.c"
#undef kui_retail_resident_init
#undef retail_display_restore
#undef kui_retail_image_read
#undef kui_retail_storage_acquire
#include "kui/vmu_maple_observe_boot.h"
#include "kui/vmu_toy_observe_admission.h"
#if KUI_VMU_READONLY_PROOF >= 2
#include "kui/vmu_maple_observe.h"
#include "vmu_sd_lease.inc"
#endif

#if !KUI_RETAIL_GD_REJECTION_DETAILS
#error Observer I/O telemetry requires the standard native diagnostic fields
#endif

#if KUI_VMU_READONLY_PROOF < 2
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

static uint8_t *observe_map(void *context,uint32_t address,uint32_t bytes,int writing) {
    if(!kui_vmu_toy_observe_destination(address,bytes)) return NULL;
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

void kui_vmu_observe_low_failure(void) __attribute__((noreturn));
void kui_vmu_observe_low_failure(void) {
    const volatile struct kui_vmu_observe_boot_control *c=
        (const volatile struct kui_vmu_observe_boot_control *)
        ((uintptr_t)&kui_vmu_observe_boot_control+UINT32_C(0x20000000));
    /* A validated worker entry is published only after its initialization
     * succeeded. A late heap reset stops without destroying the live lease. */
    uint32_t terminal=c->terminal;
    if(c->status==KUI_VMU_OBS_BOOT_INSTALLED && !(terminal&1u) &&
       terminal>=KUI_VMU_TOY_LEASE_BEGIN && terminal<KUI_VMU_TOY_WORKER_END)
        ((void (*)(void))(uintptr_t)terminal)();
    retail_display_restore(&display);
    retail_display_line("TOY MAPLE OBSERVER STOPPED");
#if KUI_VMU_READONLY_PROOF >= 2
    /* Terminal state is immutable under the masked failure entry. These four
     * P2 words are already adjacent; avoid a diagnostic-only stack copy. */
    retail_display_values("STATE LEASE ERROR HEAP",
        (const uint32_t *)(uintptr_t)&c->status,4u);
#else
    retail_display_values("STATE ERROR LEASE HEAP",(uint32_t[4]){
        c->status,c->error,c->lease,c->heap_begin},4u);
#endif
    for(;;) __asm__ volatile("nop");
}
