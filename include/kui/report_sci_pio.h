/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef KUI_REPORT_SCI_PIO_H
#define KUI_REPORT_SCI_PIO_H
#include "sd_reader.h"
#include <stdint.h>

enum kui_report_sci_pio_result {
    KUI_REPORT_SCI_PIO_OK,
    KUI_REPORT_SCI_PIO_ARGUMENT,
    KUI_REPORT_SCI_PIO_STATE,
    KUI_REPORT_SCI_PIO_CONTEXT,
    KUI_REPORT_SCI_PIO_IDLE,
    KUI_REPORT_SCI_PIO_TIMEOUT,
    KUI_REPORT_SCI_PIO_RESTORE,
    KUI_REPORT_SCI_PIO_IDENTITY
};
enum kui_report_sci_pio_stage {
    KUI_REPORT_SCI_PIO_STAGE_NONE,
    KUI_REPORT_SCI_PIO_STAGE_ACQUIRE_CONTEXT,
    KUI_REPORT_SCI_PIO_STAGE_WAKE,
    KUI_REPORT_SCI_PIO_STAGE_IDLE,
    KUI_REPORT_SCI_PIO_STAGE_SETUP,
    KUI_REPORT_SCI_PIO_STAGE_SPEED,
    KUI_REPORT_SCI_PIO_STAGE_SELECT,
    KUI_REPORT_SCI_PIO_STAGE_OWNER,
    KUI_REPORT_SCI_PIO_STAGE_TRANSFER_CONTEXT,
    KUI_REPORT_SCI_PIO_STAGE_SCR,
    KUI_REPORT_SCI_PIO_STAGE_TDRE,
    KUI_REPORT_SCI_PIO_STAGE_RDRF,
    KUI_REPORT_SCI_PIO_STAGE_CLEAR,
    KUI_REPORT_SCI_PIO_STAGE_TEND,
    KUI_REPORT_SCI_PIO_STAGE_RELEASE_CONTEXT,
    KUI_REPORT_SCI_PIO_STAGE_RELEASE_TEND,
    KUI_REPORT_SCI_PIO_STAGE_RESTORE,
    KUI_REPORT_SCI_PIO_STAGE_STANDBY
};
/* Zero exactly once in private P2 RAM, then init. Do not copy or mutate a
 * live state. The first fault persists across release and later attempts.
 * Caller excludes the GD/audio reader for the complete lease; this object
 * does not acquire that logical guard or stop a reader's CMD18 stream. */
struct kui_report_sci_pio {
    uintptr_t self;
    uint32_t magic,work,fault,mode;
    uint16_t data;
    uint8_t scr,smr,brr,ptr,standby,acquired,slow,selected,captured,stage;
    /* First fault only: stage<<16 | observed SCR<<8 | observed SSR. No SCI
     * sample is attempted for an invalid context/owner or while in standby;
     * those cases retain a stage-only word. Cleanup never replaces it. */
    uint32_t detail;
};
#ifdef __sh__
_Static_assert(sizeof(struct kui_report_sci_pio)==36u,"Report PIO diagnostic ABI");
#endif
int kui_report_sci_pio_init(struct kui_report_sci_pio *);
/* Terminal acquire/release require MD1, BL0, IMASK15. A failed acquisition
 * which has acquired==1 still requires release, including after a fault.
 * A source-owned idle module may be woken from standby before SCI sampling;
 * a partial failed wake is restored without unsnapshotted SCI accesses. A
 * captured module unexpectedly stopped at release retains acquired=1 and
 * returns a fault without SCI accesses: restoration remains unverified. */
int kui_report_sci_pio_acquire(struct kui_report_sci_pio *);
bool kui_report_sci_pio_healthy(const struct kui_report_sci_pio *);
int kui_report_sci_pio_release(struct kui_report_sci_pio *);
/* Fills only ctx/select/transfer/ticks. No block DMA or implicit begin/end.
 * Byte operations require MD1, BL0 and IMASK15; the caller supplies the
 * exclusive, permanently stopped terminal context. ticks is nominal
 * 12.5-MHz wire work, not elapsed wall time. Byte waits retain IMASK15;
 * select preserves every caller SR bit across its GPIO read/modify/write. */
int kui_report_sci_pio_bus(struct kui_report_sci_pio *,
    struct kui_loader_sd_bus *);
/* Fresh read-only CMD10/CMD9 identity, data CRCs checked. The adopted card
 * must use this exact bus and retain its accepted ready/capacity/HC values.
 * No reset, initialization, OCR change or filesystem access occurs here.
 * Capacity comes from CSD; hc is checked against the accepted addressing mode.
 * Protocol deadlines use bounded nominal wire-work, never an elapsed clock. */
int kui_report_sci_pio_identity(struct kui_report_sci_pio *,struct kui_loader_sd *,
    uint8_t cid[16],uint64_t *sectors,uint32_t *high_capacity);
#endif
