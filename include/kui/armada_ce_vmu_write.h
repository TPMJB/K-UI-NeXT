/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_VMU_WRITE_H
#define KUI_ARMADA_CE_VMU_WRITE_H
#include "kui/vmu_emulation.h"

enum kui_armada_ce_vmu_write_result {
    KUI_ARMADA_CE_VMU_WRITE_HANDLED=1,
    KUI_ARMADA_CE_VMU_WRITE_REFUSED=0,
    KUI_ARMADA_CE_VMU_WRITE_ARGUMENT=-1,
    KUI_ARMADA_CE_VMU_WRITE_CAPACITY=-2,
    KUI_ARMADA_CE_VMU_WRITE_INTERNAL=-3
};

/* Protocol component only: no transport, storage provider or production hook.
 * The explicit callbacks retain the generic engine's durable OK / immutable
 * BUSY / idempotent-error contract. Neither callback may retain a buffer.
 * A phase ACK promises receipt; only a phase4 final ACK reports commit OK.
 * This adapter never substitutes a queued or RAM-only ACK for durable OK.
 *
 * Zero once, initialize in its final location, then never move, copy, edit or
 * reinitialize it. The caller serializes the endpoint and owns the callback
 * context for its lifetime. Public fields permit static placement/inspection,
 * not mutation. The one512-byte transaction is the embedded protocol stage. */
struct kui_armada_ce_vmu_write {
    const struct kui_armada_ce_vmu_write *self;
    kui_vmu_emulation_t protocol;
    struct kui_vmu_emulation_io io;
    uint32_t magic,started,handling;
    uint32_t requests,reads,write_phases,commits,read_finalizations;
    uint32_t busy,io_errors,refused;
};
_Static_assert(sizeof(struct kui_armada_ce_vmu_write)<=640u,
    "CE writable protocol must retain only one bounded transaction");

/* No I/O. io and state must be disjoint. A started instance is never reset. */
int kui_armada_ce_vmu_write_init(struct kui_armada_ce_vmu_write *,
    const struct kui_vmu_emulation_io *);

/* Exact A2/host0 only. INFO/ALLINFO/MEDIA/READ preserve native shapes; WRITE
 * is140 bytes/34 words, phases0..3; SYNC phase4 commits the complete matching
 * block. CE read finalization phase0/1 requires a matching successful READ
 * and no active write, invokes no callback, and can return a4-byte ACK.
 * Phase3 write-finalization is refused until independently established.
 * RESET/KILL/LCD/other shapes never enter the unrestricted engine.
 *
 * Buffers/length/state must have valid nonwrapping spans and be pairwise
 * disjoint. Invalid spans/overlap/state fail before any write, including to
 * response_bytes. Capacity failure changes no protocol state or counters.
 * WRITE/phase4 SYNC reserve8 private output bytes for possible FILEERR, even
 * when success is4 bytes. Only response_bytes bytes form the response; a
 * failing reader may have modified private unused response bytes.
 *
 * Malformed addressed reads/mutations and mismatched finalizations revoke
 * prior-read evidence, preserving staged write bytes. Other endpoints do
 * not revoke it. IO BUSY/error preserve immutable attempted-write identity;
 * the backing provider owns permanent-fault/recovery policy. Reentry refuses
 * a second engine invocation. Publication/ownership and lost-ACK handling
 * remain the separate transport's responsibility. */
int kui_armada_ce_vmu_write_handle(struct kui_armada_ce_vmu_write *,
    const uint8_t *,size_t,uint8_t *,size_t,size_t *);
#endif
