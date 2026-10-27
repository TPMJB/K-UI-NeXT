/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_VMU_RUNTIME_H
#define KUI_ARMADA_CE_VMU_RUNTIME_H
#include "kui/armada_ce_vmu_layout.h"
#include "kui/vmu_durable.h"
#include <stdbool.h>

#ifndef KUI_ARMADA_CE_VMU_WRITE_RUNTIME
#define KUI_ARMADA_CE_VMU_WRITE_RUNTIME 0
#endif
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME != 0 && KUI_ARMADA_CE_VMU_WRITE_RUNTIME != 1
#error "CE writable runtime must be explicitly disabled or enabled"
#endif
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME && (!KUI_ARMADA_CE_MAPLE_WRITE_LAYOUT || !KUI_ARMADA_CE_MAPLE_WRITE_STAGING)
#error "CE writable runtime requires its expanded layout and write staging"
#endif

#define KUI_ARMADA_CE_VMU_SERVICES_MAGIC UINT32_C(0x31535256)
#define KUI_ARMADA_CE_VMU_SERVICES_VERSION 1u
#define KUI_ARMADA_CE_VMU_SERVICES_BYTES 32u
#define KUI_ARMADA_CE_VMU_SERVICES_P2 KUI_ARMADA_CE_VMU_SERVICES_ADDRESS

/* Stage-owned publication of exact resident entrypoints. No function or guest
 * pointer is discovered at run time. The stage obtains these from the native
 * admitted resident descriptor, checks its code bounds and publishes the seal
 * only after preparation, reader release and installation all succeed. */
struct kui_armada_ce_vmu_runtime_services {
    uint32_t magic,version,bytes,loan_begin,loan_end,reserved[2],seal;
};
_Static_assert(sizeof(struct kui_armada_ce_vmu_runtime_services)==32u,
    "CE retained service binding ABI");
int kui_armada_ce_vmu_runtime_services_seal(struct kui_armada_ce_vmu_runtime_services *);
int kui_armada_ce_vmu_runtime_services_validate(const struct kui_armada_ce_vmu_runtime_services *);

#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME || defined(KUI_ARMADA_CE_VMU_RUNTIME_TEST)
extern struct kui_vmu_durable_handoff kui_armada_ce_vmu_runtime_handoff;
extern struct kui_armada_ce_vmu_runtime_services kui_armada_ce_vmu_runtime_services;

/* No I/O. Called only after the native worker/owner query is admitted, under
 * its short interrupt mask. The first successful binding is permanent. */
int kui_armada_ce_vmu_runtime_bind(uint32_t owner_extension);
/* Publisher backend: private DEFERRED2/zero bytes never reaches CE. A real
 * error remains a canonical FILEERR; only the native continuation can service
 * a pending job and obtain a fresh whole owner/query proof before retry. */
int kui_armada_ce_vmu_runtime_handle(void *,const uint8_t *,size_t,
    uint8_t *,size_t,size_t *);
/* Fixed shim entry, actual kernel/IMASK0/BL0 on the retained4KiB worker stack,
 * same native ordinary worker and live extension; no held Maple fence. */
int kui_armada_ce_vmu_runtime_work(uint32_t owner_extension);
/* Source-specific platform witness, supplied by the native continuation.
 * Called under a short mask; never waits, allocates, changes state or does I/O. */
bool kui_armada_ce_maple_runtime_owner(uint32_t owner_extension);
/* Fixed source import boundary. Runs on the original CE worker stack after
 * a pre-acquisition BUSY, never on the freestanding I/O stack. Waits 1 ms on
 * this worker's own admitted thread handle; only WAIT_TIMEOUT is accepted. */
int kui_armada_ce_maple_runtime_wait(uint32_t owner_extension);
#endif
#endif
