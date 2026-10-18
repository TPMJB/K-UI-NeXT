/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DEFLATE_H
#define KUI_DEFLATE_H
#include "kui/zso.h"

/* Caller-owned temporary state, reusable after each call. Packed code widths
 * keep the detached reader's memory bounded without a heap or a 32 KiB window.
 * No source context, output pointer or decoded data is retained here. */
struct kui_deflate_workspace {
    uint16_t counts[2][16], code_counts[8];
    uint8_t widths[160], code_widths[10];
};

/* Decode one independent raw RFC 1951 DEFLATE stream to exactly 2048 bytes.
 * Stored, fixed and dynamic Huffman blocks, including multiple DEFLATE blocks,
 * are supported. The admitted physical span is at most KUI_ZSO_MAX_SPAN.
 * Every read stays within [start,end); matches can use only this call's output.
 * On success consumed includes the byte containing the final end-of-block bit,
 * leaving container alignment padding for the caller to check. Remaining bits
 * in that byte are ignored as RFC 1951 permits. All pointers except context are
 * required; workspace must not overlap out. Callback failures are unchanged.
 * On failure consumed is unchanged and partially changed out is invalid.
 * Calls using the same workspace must not overlap. */
enum kui_zso_result kui_deflate_decode(kui_zso_read_fn read, void *context,
    uint32_t start, uint32_t end, uint8_t out[KUI_ZSO_BLOCK_BYTES],
    struct kui_deflate_workspace *workspace, uint32_t *consumed);
#endif
