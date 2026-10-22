/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef KUI_ARMADA_CE_VMU_SD_PIO_H
#define KUI_ARMADA_CE_VMU_SD_PIO_H
#include "sd_reader.h"
#include <stdint.h>

#ifndef KUI_ARMADA_CE_VMU_WRITE_RUNTIME
#define KUI_ARMADA_CE_VMU_WRITE_RUNTIME 0
#endif
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME != 0 && KUI_ARMADA_CE_VMU_WRITE_RUNTIME != 1
#error "KUI_ARMADA_CE_VMU_WRITE_RUNTIME must be 0 or 1"
#endif
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME || defined(KUI_ARMADA_CE_VMU_SD_PIO_TEST)
enum kui_armada_ce_vmu_sd_pio_result {
    KUI_ARMADA_CE_VMU_SD_PIO_OK,
    KUI_ARMADA_CE_VMU_SD_PIO_ARGUMENT,
    KUI_ARMADA_CE_VMU_SD_PIO_STATE,
    KUI_ARMADA_CE_VMU_SD_PIO_CONTEXT,
    KUI_ARMADA_CE_VMU_SD_PIO_IDLE,
    KUI_ARMADA_CE_VMU_SD_PIO_TIMEOUT,
    KUI_ARMADA_CE_VMU_SD_PIO_RESTORE
};
enum kui_armada_ce_vmu_sd_pio_stage {
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_NONE,
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_ACQUIRE_CONTEXT,
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_WAKE,
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_IDLE,
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_SETUP,
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_SPEED,
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_SELECT,
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_OWNER,
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_TRANSFER_CONTEXT,
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_SCR,
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_TDRE,
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_RDRF,
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_CLEAR,
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_TEND,
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_RELEASE_CONTEXT,
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_RELEASE_TEND,
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_RESTORE,
    KUI_ARMADA_CE_VMU_SD_PIO_STAGE_STANDBY
};
/* Zero exactly once in private P2 RAM, then init. Do not copy or mutate a
 * live state. The first fault persists across release and later attempts.
 * Caller excludes the GD/audio reader for the complete lease; this object
 * does not acquire that logical guard or stop a reader's CMD18 stream. */
struct kui_armada_ce_vmu_sd_pio {
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
_Static_assert(sizeof(struct kui_armada_ce_vmu_sd_pio)==36u,"CE PIO diagnostic ABI");
#endif
int kui_armada_ce_vmu_sd_pio_init(struct kui_armada_ce_vmu_sd_pio *);
/* Native acquire/release require MD1, BL0, IMASK15. A failed acquisition
 * which has acquired==1 still requires release, including after a fault.
 * A source-owned idle module may be woken from standby before SCI sampling;
 * a partial failed wake is restored without unsnapshotted SCI accesses. A
 * captured module unexpectedly stopped at release retains acquired=1 and
 * returns a fault without SCI accesses: restoration remains unverified. */
int kui_armada_ce_vmu_sd_pio_acquire(struct kui_armada_ce_vmu_sd_pio *);
bool kui_armada_ce_vmu_sd_pio_healthy(const struct kui_armada_ce_vmu_sd_pio *);
int kui_armada_ce_vmu_sd_pio_release(struct kui_armada_ce_vmu_sd_pio *);
/* Fills only ctx/select/transfer/ticks. No block DMA or implicit begin/end.
 * Byte operations require MD1, BL0 and IMASK0 or15; the caller supplies the
 * exclusive ordinary-worker context. ticks is nominal 12.5-MHz wire work,
 * not elapsed wall time. Only select's GPIO read/modify/write briefly masks
 * interrupts and restores their exact previous state; wire waits do not. */
int kui_armada_ce_vmu_sd_pio_bus(struct kui_armada_ce_vmu_sd_pio *,
    struct kui_loader_sd_bus *);
#endif
#endif
