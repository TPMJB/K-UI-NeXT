/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ZSO_H
#define KUI_ZSO_H
#include <stddef.h>
#include <stdint.h>

#define KUI_ZSO_HEADER_BYTES 24u
#define KUI_ZSO_BLOCK_BYTES 2048u
#define KUI_ZSO_MAX_SHIFT 9u
#define KUI_ZSO_MAX_SPAN 4096u
#define KUI_ZSO_MAX_BYTES UINT32_C(0x80000000)

enum kui_zso_result {
    KUI_ZSO_OK, KUI_ZSO_INVALID, KUI_ZSO_UNSUPPORTED,
    KUI_ZSO_IO, KUI_ZSO_CANCELLED, KUI_ZSO_RANGE
};
/* Physical container and decoded ISO lengths are deliberately distinct.
 * index_bytes includes block_count+1 little-endian u32 entries at offset24.
 * Only standard LZ4 ZISO v1 with independent 2048-byte blocks is admitted.
 * Historical LZO ZSO shares its magic: no codec guessing/fallback occurs. */
struct kui_zso_info {
    uint32_t file_bytes, total_bytes, block_count, index_bytes;
    uint8_t shift;
};
/* Read exactly size physical bytes or return an error. A successful short
 * read is forbidden. No call exceeds the admitted file range. The callback
 * owns cancellation and may split/cache physical 512-byte blocks. No source
 * pointer/context is retained by any API. Sources stay immutable after index
 * validation. There is no heap, complete-index or compressed-input buffer. */
typedef enum kui_zso_result (*kui_zso_read_fn)(void *context, uint32_t offset,
                                            void *out, size_t size);
/* Validate only the fixed header and declared geometry; info unchanged on
 * error. Separately validate the entire index before publishing an image. */
enum kui_zso_result kui_zso_header_parse(const uint8_t header[KUI_ZSO_HEADER_BYTES],
                                       uint64_t file_bytes, struct kui_zso_info *info);
/* Bounded-page scan checks every offset, index exclusion and terminal/tail
 * bounds. Does not decompress payload blocks. */
enum kui_zso_result kui_zso_validate_index(const struct kui_zso_info *info,
                                          kui_zso_read_fn read, void *context);
/* Read two index entries, recheck their bounds/span, then decode exactly one
 * whole block directly into out. Raw blocks use the same bounded reader.
 * Alignment padding is permitted only below 1<<shift bytes. LZ4 validates
 * match/literal lengths and canonical end conditions. On error out may be
 * partially changed; it must never be used as valid decoded data then. */
enum kui_zso_result kui_zso_read_block(const struct kui_zso_info *info,
                                     uint32_t block, kui_zso_read_fn read,
                                     void *context, uint8_t out[KUI_ZSO_BLOCK_BYTES]);
const char *kui_zso_result_name(enum kui_zso_result result);
#endif
