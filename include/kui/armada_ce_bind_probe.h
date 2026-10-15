/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_BIND_PROBE_H
#define KUI_ARMADA_CE_BIND_PROBE_H
#include <stdint.h>

#define KUI_ARMADA_CE_BIND_ADDRESS 0x8c004000u
#define KUI_ARMADA_CE_BIND_LIMIT 0x8c007c00u
/* Exactly four five-word rows. Values are snapshots, never owned leases. */
struct kui_armada_ce_bind_snapshot {
    uint32_t cpu[5]; /* TTB, PTEH, PLAY count, CCR, negative-block mask 0..31. */
    uint32_t nominal[5]; /* Slot value, block address/value, status, mask 32..63. */
    uint32_t real[5]; /* Slot value, block address/value, status, negative count. */
    uint32_t candidate[5]; /* Selected VA, slot, block address/value, status. */
};
_Static_assert(sizeof(struct kui_armada_ce_bind_snapshot) == 80u,
               "Binding report has four five-word rows");
typedef const struct kui_armada_ce_bind_snapshot *(*kui_armada_ce_bind_entry)(
    uint32_t function, uint32_t command, int32_t result, uint32_t sample);
extern const struct kui_armada_ce_bind_snapshot *kui_armada_ce_bind_probe(
    uint32_t function,uint32_t command,int32_t result,uint32_t sample);

/* Entry accepts only sample=1 for an accepted PLAY/PLAY2. sample=0 returns
 * the already recorded snapshot without MMIO, guest reads or allocation. */
#endif
