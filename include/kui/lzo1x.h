/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_LZO1X_H
#define KUI_LZO1X_H
#include "kui/zso.h"

/* Decode one independent LZO1X stream to exactly 2048 bytes. This is the
 * historical LZO codec, not standard ZSO's LZ4 codec; callers select it
 * explicitly. No dictionary, heap or compressed-input buffer is used.
 * Every read stays inside [start,end). Literals go directly into out;
 * matches may overlap, but must refer to bytes already produced here.
 * The canonical 0x11,0,0 end marker is required. On success consumed is
 * the number of bytes from start through that marker, so the container
 * caller can validate any alignment padding. All pointers are required.
 * On failure consumed is unchanged and partially changed out is invalid.
 * Callback errors, including cancellation, are returned unchanged. */
enum kui_zso_result kui_lzo1x_decode(kui_zso_read_fn read, void *context,
    uint32_t start, uint32_t end, uint8_t out[KUI_ZSO_BLOCK_BYTES],
    uint32_t *consumed);
#endif
