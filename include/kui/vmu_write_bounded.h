/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_WRITE_BOUNDED_H
#define KUI_VMU_WRITE_BOUNDED_H
#include <stdint.h>
#include <dc/maple.h>

/* Quarantined transport prototype: no production target links this file. */
enum kui_vmu_write_stage {
    KUI_VMU_WRITE_ARGUMENT=1,KUI_VMU_WRITE_LOCK=2,KUI_VMU_WRITE_DEVICE=3,
    KUI_VMU_WRITE_CANCEL=4,KUI_VMU_WRITE_FRAME=5,KUI_VMU_WRITE_HOOK=6,
    KUI_VMU_WRITE_QUEUE=7,KUI_VMU_WRITE_WAIT=8,KUI_VMU_WRITE_HEADER=9,
    KUI_VMU_WRITE_RESPONSE=10,KUI_VMU_WRITE_LENGTH=11,
    KUI_VMU_WRITE_COMPLETE=12,KUI_VMU_WRITE_TAINTED=13
};
enum kui_vmu_write_outcome {
    KUI_VMU_WRITE_NOT_SENT=0,
    KUI_VMU_WRITE_UNCERTAIN=1,
    KUI_VMU_WRITE_ACKNOWLEDGED=2
};
enum kui_vmu_write_result {
    KUI_VMU_WRITE_OK=0,KUI_VMU_WRITE_INVALID=-1,KUI_VMU_WRITE_BUSY=-2,
    KUI_VMU_WRITE_CANCELLED=-3,KUI_VMU_WRITE_FAILED=-4,
    KUI_VMU_WRITE_TIMEOUT=-5,KUI_VMU_WRITE_REFUSED=-6
};
struct kui_vmu_write_diagnostic {
    uint32_t block,phase,stage,outcome;
    uint32_t submitted_mask,completed_mask,acknowledged_mask;
    uint32_t frame_state,queued,lock_attempts,quarantined,tainted;
    uint32_t expected_function,expected_location;
    uint32_t response_destination,response_source,data_len;
    int result,wait_result,response;
};
typedef int (*kui_vmu_write_cancel_fn)(void *);

/* Synchronous, non-reentrant prototype for an independently verified standard
 * 512-byte/four-write-phase VMU (media discovery is outside this helper).
 * Call from ordinary thread context, outside IRQ handlers, with IRQ delivery
 * available. Cancel callbacks must return promptly and never block. The 100ms
 * deadlines apply separately to each of the two frame-lock acquisitions and
 * each of the five command waits; they are not a 100ms whole-call deadline.
 * Caller holds the VMU filesystem mutex,
 * keeps the Maple subsystem and both device objects alive, and excludes custom
 * target-address command producers and changes to the Maple DMA IRQ handler.
 * The helper locks controller unit 0 and the target frame, intercepts only its
 * own completed frame before KOS can retry AGAIN, and forwards other frames.
 * Payload is copied before queueing; late callbacks retain no caller pointers.
 * Cancel is sampled before each command, never retained after return.
 * The five commands are BWRITE phases 0..3, then BSYNC phase 4; each requires
 * reply 7 with zero data words and the exact source/destination addresses.
 * ACKNOWLEDGED is a protocol result, not a persistence or atomicity guarantee.
 * On any possibly sent failure, the prototype latches tainted until reboot,
 * holds the two frame locks, and refuses further writes. A SENT timeout also
 * retains its IRQ wrapper until the late DMA completes; a permanently hung
 * DMA remains quarantined. No retry, recovery read, reset, or extra sync occurs.
 * Caller must stop the experiment and must not unload/shut down Maple while
 * quarantined. No valuable VMU should be used for prototype acceptance.
 * The output diagnostic is a separate caller-owned buffer, copied
 * synchronously and never changed later. An overlapping payload/diagnostic
 * is refused without changing either buffer. */
int kui_vmu_write_bounded(maple_device_t *device,uint16_t block,
    const uint8_t payload[512],kui_vmu_write_cancel_fn cancel,void *context,
    struct kui_vmu_write_diagnostic *diagnostic);
int kui_vmu_write_bounded_quarantined(void);
int kui_vmu_write_bounded_tainted(void);
#endif
