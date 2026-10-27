/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_MAPLE_FENCE_TYPES_H
#define KUI_ARMADA_CE_MAPLE_FENCE_TYPES_H
#include <stdint.h>
#define KUI_ARMADA_CE_MAPLE_FENCE_MAGIC UINT32_C(0x31464c4d) /* MLF1 */
#define KUI_ARMADA_CE_MAPLE_FENCE_POLL_LIMIT 64u
#define KUI_ARMADA_CE_MAPLE_FENCE_MDSTAR UINT32_C(0xa05f6c04)
#define KUI_ARMADA_CE_MAPLE_FENCE_MDTSEL UINT32_C(0xa05f6c10)
#define KUI_ARMADA_CE_MAPLE_FENCE_MDEN UINT32_C(0xa05f6c14)
#define KUI_ARMADA_CE_MAPLE_FENCE_MDST UINT32_C(0xa05f6c18)
/* Counts: magic, PRE visits, POST visits, held windows, confirmed restores.
 * A confirmed restore after a failed disable/poll is counted too, so restore
 * confirmations can exceed held windows. status: busy skips, refused phases,
 * peak poll attempts, current active state, historical reason bits.
 * before: MDEN, MDTSEL, MDST, MDSTAR, expected TX physical address.
 * held: MDEN after disable, idle after poll, POST MDEN/MDST/MDSTAR.
 * after: restored MDEN/MDST, previous mode at PRE/POST, latest extension.
 * fault: most recent refused phase (reason, address, actual, expected, detail).
 * Successful phases preserve historical flags/fault; no payload is present. */
struct kui_armada_ce_maple_fence_snapshot {
    uint32_t counts[5],status[5],before[5],held[5],after[5],fault[5];
};
_Static_assert(sizeof(struct kui_armada_ce_maple_fence_snapshot)==120u,"MLF1 six rows");
#endif
