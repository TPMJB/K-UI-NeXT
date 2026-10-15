/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_LEASE_H
#define KUI_TOY_PILOT_LEASE_H

#include "kui/toy_pilot_bus.h"
#include <stdint.h>

/* Register one tracked tail allocation in the admitted game's live sound
 * heap. The canary must finish its bounded G2 write before any heap metadata
 * changes. Failure leaves both metadata and *address unchanged. */
enum kui_toy_pilot_bus_result kui_toy_pilot_lease_allocate(
    uint32_t bytes,uint32_t alignment,uint32_t *address);

#endif
