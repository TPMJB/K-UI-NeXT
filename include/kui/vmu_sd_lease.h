/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_SD_LEASE_H
#define KUI_VMU_SD_LEASE_H

struct kui_loader_sd;
enum kui_vmu_sd_lease_result {
    KUI_VMU_SD_LEASE_OK=0,
    KUI_VMU_SD_LEASE_BUSY=1,
    KUI_VMU_SD_LEASE_FAULT=-1
};

/* A scoped copy of the resident's initialized card descriptor, borrowing its
 * existing SCI bus. The callback and context belong to the admitted worker.
 * Do not retain loan, initialize/adopt/shutdown a card, call bus.begin/end,
 * release the bus or change IRQ state. Modes2/3 permit bounded read-only
 * commands. The separately compiled mode4 permits the exact admitted worker
 * callback to perform bounded PIO writes ONLY through its prepared physical
 * image/journal map, holding this one lease across the complete transaction.
 * Mode4 has no resident TX-DMA writer or persistent pointer to this loan.
 * Return zero only after identity/data/durability verification;
 * any other result after acquisition is a permanent session fault.
 * This interface exports no address of the resident's mutable card or port. */
typedef int (*kui_vmu_sd_transaction)(void *context,struct kui_loader_sd *loan);

/* Isolated SD-backed exact-owner proofs. Called on the worker's private 4KiB stack
 * with IMASK15 already held; leaves the caller's interrupt mask unchanged.
 * Uses the SAME cached P1 GD-entry lock/card/SCI instance as the game reader.
 * The exact callback must match the worker transaction pinned by the stage.
 * BUSY is possible only before acquisition, with no wire traffic or release.
 * After acquisition all paths release once, check the persistent bus fault,
 * and poison the resident card on failure. No reinitialization is attempted.
 * A successful callback is acknowledged only after successful restoration. */
int kui_vmu_observe_sd_lease(kui_vmu_sd_transaction,void *context);

#endif
