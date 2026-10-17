/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_SERVICE_H
#define KUI_VMU_SERVICE_H
#include "kui/vmu_backing.h"
#include "kui/vmu_emulation.h"

enum kui_vmu_service_result {
    KUI_VMU_SERVICE_OK=0,
    KUI_VMU_SERVICE_BUSY=1,
    KUI_VMU_SERVICE_ARGUMENT=-1,
    KUI_VMU_SERVICE_STORAGE=-2,
    KUI_VMU_SERVICE_BLOCKED=-3
};
#define KUI_VMU_SERVICE_HANDLE_NOT_READY (-3)
#define KUI_VMU_SERVICE_HANDLE_REENTRANT (-4)

/* Zero before first use. No allocation or full-card RAM mirror. Configuration
 * maps/identity are copied by the owned backing/store; bus context, card,
 * stream and hooks must remain resident as required by vmu_backing.h.
 * Private work state is exposed solely for static placement. Do not memcpy or
 * move an initialized service: protocol callbacks refer to its own address. */
struct kui_vmu_service {
    struct kui_vmu_backing backing;
    struct kui_vmu_store store;
    kui_vmu_emulation_t protocol;
    int last_backing_result,last_store_result;
    bool ready,faulted,handling;
};
/* Pre-handoff only: validate/configure backing, reserve the whole bus, recover
 * the journal, release, then enable protocol handling. BUSY means no store
 * entry; a zero-initialized/unfaulted instance may retry init after contention.
 * Requires allow_writes and a resident bus healthy hook, so release faults are
 * observable before a final save acknowledgement. Recursive init is blocked.
 * Any storage failure latches fault; this instance can never initialize again.
 * New controlled card initialization/recovery requires a fresh service object.
 * Configuration arrays are needed only until this call returns. */
int kui_vmu_service_init(struct kui_vmu_service *,
    const struct kui_vmu_backing_bus *,const struct kui_vmu_backing_config *,
    uint8_t maple_address);
/* Same frame API/return values as kui_vmu_emulation_handle; additionally -3
 * means not initialized and -4 rejects recursive entry without touching the
 * outer call. Read/commit callbacks reserve before entering the store: initial
 * contention produces Maple AGAIN without blocking the store. A commit ACK
 * requires both durable store completion and successful bus release.
 * Storage faults persist through Maple RESET; reads/new commits then return
 * FILEERR without storage I/O. This does not install a Maple/game hook. */
int kui_vmu_service_handle(struct kui_vmu_service *,const uint8_t *request,
    size_t request_bytes,uint8_t *response,size_t response_capacity,
    size_t *response_bytes);
#endif
