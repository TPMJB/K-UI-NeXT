/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_REGION_H
#define KUI_DISC_REGION_H
#include "kui/probe.h"

#define KUI_DISC_REGION_RECORD_BYTES 64u
#define KUI_DISC_REGION_PATH_A "0:/KUI/disc/region-a.bin"
#define KUI_DISC_REGION_PATH_B "0:/KUI/disc/region-b.bin"

/* Exclusive GUI storage operations: connect, mount, close/unmount and
 * disconnect on every acquired path. Both absent slots default to enabled,
 * preserving the accepted direct-disc boot behavior. One complete valid slot
 * survives a torn replacement; I/O errors, two damaged slots and conflicting
 * equal sequences refuse. load leaves *enabled unchanged on any failure.
 * No disc reads, card lookup, virtual-VMU access or console-region assumptions.
 * NULL log/cancel are permitted. */
bool kui_disc_region_load(bool *enabled,kui_log_fn log,kui_cancel_fn cancel);
/* Write only the inactive slot, sync/close and reread every byte. Cancellation
 * is honored until opening that slot for write; the bounded64B update then
 * completes verification and cleanup. An unconfirmed save can leave the exact
 * old or exact requested value, and never modifies the prior valid slot. */
bool kui_disc_region_save(bool enabled,kui_log_fn log,kui_cancel_fn cancel);
/* Read-only actual console flash region through KOS flashrom_get_region().
 * Returns 'J', 'U', 'E', or0 when unknown/error. This does not inspect language,
 * video timing, the inserted disc or a previously cached preference. */
char kui_disc_region_console(void);
#endif
