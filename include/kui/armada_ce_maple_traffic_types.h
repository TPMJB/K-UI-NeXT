/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_MAPLE_TRAFFIC_TYPES_H
#define KUI_ARMADA_CE_MAPLE_TRAFFIC_TYPES_H
#include <stdint.h>
#define KUI_ARMADA_CE_MAPLE_TRAFFIC_MAGIC UINT32_C(0x3152544d) /* MTR1 */
#define KUI_ARMADA_CE_MAPLE_TRAFFIC_RECORD_LIMIT 16u
#define KUI_ARMADA_CE_MAPLE_TRAFFIC_READ_LIMIT 512u
/* Each command count is an observed subunit descriptor, not a unique request.
 * first/last contain the first-ever/latest admitted subunit header, across the
 * whole session. Controller-only visits never erase these rows. No payload or
 * DMA ownership is represented. Fault rows describe the last visit only. */
struct kui_armada_ce_maple_traffic_snapshot {
    uint32_t counts[5],commands[5],first[5],last[5],metadata[5],fault[5];
};
_Static_assert(sizeof(struct kui_armada_ce_maple_traffic_snapshot)==120u,"MTR1 six rows");
#endif
