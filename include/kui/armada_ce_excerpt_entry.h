/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_EXCERPT_ENTRY_H
#define KUI_ARMADA_CE_EXCERPT_ENTRY_H
#include <stdint.h>

/* Authored resident-to-controller ABI. The request belongs to the resident,
 * is static direct RAM, and contains the service's accepted parameter copy.
 * The GD lock stays held from the deferred-slot check through publication. */
struct kui_armada_ce_excerpt_request {
    uint32_t first,last,repeat,deferred_available;
};
struct kui_armada_ce_excerpt_entry_snapshot {
    /* STREAM terminal snapshot: accepted, binder status, source bytes (low32),
     * recoveries and native worker status. Legacy excerpt retains its binder
     * read count, queued count and PIO skips in the last three positions. */
    uint32_t binding[5];
    uint32_t context[5]; /* Restored caller SR/FPSCR, current, owner, device. */
    uint32_t playback[5]; /* Adapter status, attempts, Play OK, refused, HRESULT. */
    uint32_t buffer[5]; /* Buffer, caps flags, bytes, free voices, PCM CRC32. */
    uint32_t cursor[5]; /* Command, first, last, play cursor, write cursor. */
    uint32_t fault[5]; /* Module reason, node, observed, expected, detail. */
    uint32_t deferred;
};
_Static_assert(sizeof(struct kui_armada_ce_excerpt_entry_snapshot)==124u,
               "Excerpt entry has six five-word rows and one callback");
/* function=UINT32_MAX takes an integer-only local snapshot. No hardware/guest read or
 * native API is made. An accepted REQUEST can publish only an authored
 * callback, never invoke native CE methods while the resident lock is held. */
extern const struct kui_armada_ce_excerpt_entry_snapshot *kui_armada_ce_bind_probe(
    uint32_t function,uint32_t command,int32_t result,uint32_t request_address);
#endif
