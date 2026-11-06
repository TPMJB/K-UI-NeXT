/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VC2_CE_VMU_LOAN_H
#define KUI_VC2_CE_VMU_LOAN_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct kui_loader_sd;
/* Binds only the native foreground ABI already admitted before NK. All low
 * pointers are source-bound P2 views; publication bounds are canonical P1.
 * Call once on the initializer's masked path after service ownership. No
 * port register, card byte, stream state or GD command changes here. */
int kui_vc2_ce_vmu_loan_bind(void *region,volatile uint32_t *hook_active,
    volatile uint32_t *hook_fault,uintptr_t publish_begin,uintptr_t publish_end);

/* Compatible result values:0 acquired,1 pre-acquisition BUSY,2 fault,
 *3 invalid execution context. Caller is the separately admitted ordinary
 * CE worker, MD1/BL0/IMASK0. A dedicated value2 holds the GD entry lock across
 * the transaction. Source-admitted GD REQUEST calls yield on their original
 * CE stack and retry after release; CHECK retains the driver's busy response.
 * No failed zero token is substituted for a concurrently arriving request.
 *
 * Begin admits only a completely closed foreground stream. It never stops
 * a stream, discards a block, steals a pending GD operation or does wire I/O.
 * A success copies only card protocol state; the writer substitutes its own
 * checked PIO callbacks. Exactlyone end follows every success, after the
 * writer has restored hardware. No caller-owned pointer is retained.
 * Failed/uncertain restoration permanently quarantines both reader and loan.
 * Begin/end mask only their bounded state/cache publication, restoring SR
 * exactly. Actual PIO/storage work remains unmasked, with the original
 * Maple fence owned by the runtime continuation throughout. */
int kui_vc2_ce_vmu_loan_begin(struct kui_loader_sd *copied_card);
int kui_vc2_ce_vmu_loan_end(bool healthy);
/* magic,state,claims,releases,busy,first_fault,region,last_result */
void kui_vc2_ce_vmu_loan_snapshot(uint32_t out[8]);

#endif
