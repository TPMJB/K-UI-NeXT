/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VC2_CE_VMU_WRITE_PROBE_H
#define KUI_VC2_CE_VMU_WRITE_PROBE_H
#include "kui/armada_ce_vmu_write.h"
#include <stdbool.h>

#ifndef KUI_VC2_VMU_WRITE_PROBE
#define KUI_VC2_VMU_WRITE_PROBE 0
#endif
#if KUI_VC2_VMU_WRITE_PROBE != 0 && KUI_VC2_VMU_WRITE_PROBE != 1
#error "VC2 volatile write probe must be explicitly disabled or enabled"
#endif

#define KUI_VC2_VMU_WRITE_PROBE_MAGIC UINT32_C(0x31505756) /* VWP1 */
#define KUI_VC2_VMU_WRITE_PROBE_WORDS 20u
#define KUI_VC2_VMU_WRITE_PROBE_COMMIT_STOP 7u

/* One volatile transaction, never a saved card or storage provider. Zero
 * once, initialize in its final location and serialize the caller. The
 * ordinary protocol validates and acknowledges WRITE phases0..3 in RAM.
 * Its commit callback NEVER returns OK and performs NO storage I/O.
 * An exact complete phase4 transaction becomes private DEFERRED2 with no
 * response bytes. The owner MUST then stop before the original CE consumer;
 * this probe has no worker, continuation, persistence or final commit ACK.
 * No caller-owned request, reply or data pointer is retained. */
struct kui_vc2_ce_vmu_write_probe {
    const struct kui_vc2_ce_vmu_write_probe *self;
    struct kui_armada_ce_vmu_write endpoint;
    uint32_t magic,started,handling,pending;
    uint32_t phase_acks[4],commit_attempts,staged_crc,refused;
    uint32_t last_header,last_location,last_capacity;
};
_Static_assert(sizeof(struct kui_vc2_ce_vmu_write_probe)<=768u,
    "VC2 probe retains one512-byte transaction, never a full card");

int kui_vc2_ce_vmu_write_probe_init(struct kui_vc2_ce_vmu_write_probe *);
/* Existing backend results1/0/negative; private2 only for the complete
 * matching phase4 transaction. Private2 always sets produced=0. Capacity
 * failure leaves state/reply bytes unchanged and sets produced=0; invalid
 * spans or aliases leave all output and state unchanged. */
int kui_vc2_ce_vmu_write_probe_handle(struct kui_vc2_ce_vmu_write_probe *,
    const uint8_t *,size_t,uint8_t *,size_t,size_t *);
/* False for an all-zero/uninitialized state. This is a terminal checkpoint
 * witness, not evidence of a durable or even volatile committed block. */
bool kui_vc2_ce_vmu_write_probe_pending(const struct kui_vc2_ce_vmu_write_probe *);
/* Fixed20-word snapshot, no live guest/storage reads:
 * magic,started,requests,reads,WRITE_ACKs,commit_attempts,block,phase_mask,
 * staged_crc,pending,phase_ACK0,1,2,3,durable_commits,busy,refused,
 * last_request_header,last_location,last_private_reply_capacity.
 * Output must be disjoint from state. Invalid/uninitialized states yield
 * zero words; invalid output spans/overlap leave the output untouched. */
void kui_vc2_ce_vmu_write_probe_snapshot(const struct kui_vc2_ce_vmu_write_probe *,
    uint32_t out[KUI_VC2_VMU_WRITE_PROBE_WORDS]);
#endif
