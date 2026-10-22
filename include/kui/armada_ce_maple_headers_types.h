/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_MAPLE_HEADERS_TYPES_H
#define KUI_ARMADA_CE_MAPLE_HEADERS_TYPES_H
#include <stdint.h>
#define KUI_ARMADA_CE_MAPLE_HEADERS_MAX 8u
struct kui_armada_ce_maple_headers_snapshot {
    uint32_t counts[5],rows[KUI_ARMADA_CE_MAPLE_HEADERS_MAX][5],fault[5];
};
_Static_assert(sizeof(struct kui_armada_ce_maple_headers_snapshot)<=320u,"CE header workspace fits mapping scratch");
#endif
