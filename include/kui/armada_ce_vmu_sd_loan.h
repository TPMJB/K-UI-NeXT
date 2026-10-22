/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_VMU_SD_LOAN_H
#define KUI_ARMADA_CE_VMU_SD_LOAN_H
#include <stdbool.h>
#include <stdint.h>

#ifndef KUI_ARMADA_CE_VMU_WRITE_RUNTIME
#define KUI_ARMADA_CE_VMU_WRITE_RUNTIME 0
#endif
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME != 0 && KUI_ARMADA_CE_VMU_WRITE_RUNTIME != 1
#error "CE VMU WRITE runtime must be explicitly disabled or enabled"
#endif

enum kui_armada_ce_vmu_loan_result {
    KUI_ARMADA_CE_VMU_LOAN_OK,
    KUI_ARMADA_CE_VMU_LOAN_BUSY,
    KUI_ARMADA_CE_VMU_LOAN_FAULT,
    KUI_ARMADA_CE_VMU_LOAN_CONTEXT
};
enum kui_armada_ce_vmu_loan_state {
    KUI_ARMADA_CE_VMU_LOAN_IDLE,
    KUI_ARMADA_CE_VMU_LOAN_STOPPING,
    KUI_ARMADA_CE_VMU_LOAN_WRITER,
    KUI_ARMADA_CE_VMU_LOAN_QUARANTINED
};
#define KUI_ARMADA_CE_VMU_LOAN_DESCRIPTOR_MAGIC UINT32_C(0x564c4f31)
#define KUI_ARMADA_CE_VMU_LOAN_DESCRIPTOR_VERSION 1u
#define KUI_ARMADA_CE_VMU_LOAN_DESCRIPTOR_BYTES 20u
struct kui_loader_sd;

/* Resident entry descriptor: five 32-bit words in the target ABI. The stage
 * verifies the immutable resident before admitting these P1 function entries.
 * Both functions require actual IMASK0/BL clear. The fixed VMU provider also
 * proves its ordinary Maple worker and private stack before calling here.
 * BUSY means no loan/stop/acquisition occurred. A successful begin owns the
 * singleton until exactly one end. After an acquired or uncertain fault the
 * reader/audio/card remain quarantined until restart; ERROR is never BUSY.
 *
 * The GD entry lock is released during unmasked storage I/O. Its real GD
 * REQUEST still copies/validates parameters and returns a positive pending
 * token; reader/IRQ/audio storage work is separately inhibited by the loan. */
enum kui_armada_ce_vmu_loan_result kui_retail_async_vmu_loan_begin(
    struct kui_loader_sd *copied_card);
enum kui_armada_ce_vmu_loan_result kui_retail_async_vmu_loan_end(bool healthy);

#endif
