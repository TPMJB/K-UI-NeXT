/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/zso.h"
#include <string.h>

/* Independent implementation of the ZSO container and LZ4 block formats:
 * unknownbrackets/maxcso README_ZSO.md; lz4/lz4 doc/lz4_Block_format.md.
 * No frame decoder, dependent dictionary, LZO dialect or codec guessing. */
static uint32_t little32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int valid(const struct kui_zso_info *info) {
    return info && info->total_bytes && info->total_bytes <= KUI_ZSO_MAX_BYTES &&
        !(info->total_bytes % KUI_ZSO_BLOCK_BYTES) &&
        info->block_count == info->total_bytes / KUI_ZSO_BLOCK_BYTES &&
        info->index_bytes == (info->block_count + 1u) * 4u &&
        info->shift <= KUI_ZSO_MAX_SHIFT &&
        info->file_bytes >= KUI_ZSO_HEADER_BYTES + info->index_bytes;
}
enum kui_zso_result kui_zso_header_parse(const uint8_t header[KUI_ZSO_HEADER_BYTES],
    uint64_t file_bytes, struct kui_zso_info *info) {
    if(!header || !info) return KUI_ZSO_INVALID;
    if(memcmp(header, "ZISO", 4) || little32(header + 4) != KUI_ZSO_HEADER_BYTES ||
       header[20] != 1u || little32(header + 16) != KUI_ZSO_BLOCK_BYTES ||
       header[21] > KUI_ZSO_MAX_SHIFT) return KUI_ZSO_UNSUPPORTED;
    if(header[22] || header[23]) return KUI_ZSO_INVALID;
    uint64_t total = (uint64_t)little32(header + 8) | (uint64_t)little32(header + 12) << 32;
    if(!total || total % KUI_ZSO_BLOCK_BYTES) return KUI_ZSO_INVALID;
    if(total > KUI_ZSO_MAX_BYTES || file_bytes > UINT32_MAX) return KUI_ZSO_RANGE;
    struct kui_zso_info candidate = {
        .file_bytes = (uint32_t)file_bytes, .total_bytes = (uint32_t)total,
        .block_count = (uint32_t)(total / KUI_ZSO_BLOCK_BYTES), .shift = header[21]
    };
    candidate.index_bytes = (candidate.block_count + 1u) * 4u;
    if(!valid(&candidate)) return KUI_ZSO_RANGE;
    *info = candidate;
    return KUI_ZSO_OK;
}
static enum kui_zso_result position(const struct kui_zso_info *info,
    uint32_t entry, uint32_t *offset) {
    uint32_t value = entry & UINT32_C(0x7fffffff);
    /* Equivalent to the wider shifted bound, without a 64-bit shift helper
     * and its resident call frame on SH-4. Callers have validated shift. */
    if(value > (UINT32_MAX >> info->shift)) return KUI_ZSO_RANGE;
    value <<= info->shift;
    if(value < KUI_ZSO_HEADER_BYTES + info->index_bytes || value > info->file_bytes)
        return KUI_ZSO_RANGE;
    *offset = value;
    return KUI_ZSO_OK;
}
static enum kui_zso_result pair(const struct kui_zso_info *info,
    uint32_t entry, uint32_t next, uint32_t *start, uint32_t *end) {
    enum kui_zso_result result = position(info, entry, start);
    if(result != KUI_ZSO_OK) return result;
    result = position(info, next, end);
    if(result != KUI_ZSO_OK) return result;
    if(*end <= *start || *end - *start > KUI_ZSO_MAX_SPAN) return KUI_ZSO_RANGE;
    if((entry & UINT32_C(0x80000000)) &&
       (*end - *start < KUI_ZSO_BLOCK_BYTES ||
        *end - *start >= KUI_ZSO_BLOCK_BYTES + (1u << info->shift)))
        return KUI_ZSO_RANGE;
    return KUI_ZSO_OK;
}
enum kui_zso_result kui_zso_validate_index(const struct kui_zso_info *info,
    kui_zso_read_fn read, void *context) {
    if(!valid(info) || !read) return KUI_ZSO_INVALID;
    /* Preparation/stage only. The detached reader needs only its chosen pair. */
    uint8_t page[128];
    uint32_t previous = 0, terminal = 0;
    for(uint32_t first = 0; first <= info->block_count;) {
        uint32_t count = info->block_count + 1u - first;
        if(count > sizeof(page) / 4u) count = sizeof(page) / 4u;
        enum kui_zso_result result = read(context, KUI_ZSO_HEADER_BYTES + first * 4u,
                                         page, (size_t)count * 4u);
        if(result != KUI_ZSO_OK) return result;
        for(uint32_t i = 0; i < count; ++i) {
            uint32_t entry = little32(page + i * 4u), start;
            if(first || i) result = pair(info, previous, entry, &start, &terminal);
            else result = position(info, entry, &terminal);
            if(result != KUI_ZSO_OK) return result;
            previous = entry;
        }
        first += count;
    }
    return info->file_bytes - terminal < (1u << info->shift) ? KUI_ZSO_OK : KUI_ZSO_RANGE;
}
struct input {
    kui_zso_read_fn read;
    void *context;
    uint32_t at, end;
};
static enum kui_zso_result take(struct input *input, void *out, uint32_t count) {
    if(count > input->end - input->at) return KUI_ZSO_INVALID;
    if(!count) return KUI_ZSO_OK;
    enum kui_zso_result result = input->read(input->context, input->at, out, count);
    if(result == KUI_ZSO_OK) input->at += count;
    return result;
}
static enum kui_zso_result length(struct input *input, uint32_t base, uint32_t *out) {
    uint32_t value = base;
    if(base == 15u) {
        uint8_t extra;
        do {
            enum kui_zso_result result = take(input, &extra, 1u);
            if(result != KUI_ZSO_OK) return result;
            if(extra > KUI_ZSO_BLOCK_BYTES - value) return KUI_ZSO_INVALID;
            value += extra;
        } while(extra == 255u);
    }
    *out = value;
    return KUI_ZSO_OK;
}
enum kui_zso_result kui_zso_read_block(const struct kui_zso_info *info,
    uint32_t block, kui_zso_read_fn read, void *context, uint8_t out[KUI_ZSO_BLOCK_BYTES]) {
    if(!valid(info) || !read || !out) return KUI_ZSO_INVALID;
    if(block >= info->block_count) return KUI_ZSO_RANGE;
    uint8_t entries[8];
    enum kui_zso_result result = read(context, KUI_ZSO_HEADER_BYTES + block * 4u,
                                     entries, sizeof(entries));
    if(result != KUI_ZSO_OK) return result;
    uint32_t entry = little32(entries), start, end;
    result = pair(info, entry, little32(entries + 4), &start, &end);
    if(result != KUI_ZSO_OK) return result;
    if(entry & UINT32_C(0x80000000)) return read(context, start, out, KUI_ZSO_BLOCK_BYTES);
    struct input input = {read, context, start, end};
    uint32_t done = 0, last_match = UINT32_MAX;
    for(;;) {
        uint8_t token;
        result = take(&input, &token, 1u);
        if(result != KUI_ZSO_OK) return result;
        uint32_t literals;
        result = length(&input, token >> 4, &literals);
        if(result != KUI_ZSO_OK) return result;
        if(literals > KUI_ZSO_BLOCK_BYTES - done) return KUI_ZSO_INVALID;
        result = take(&input, out + done, literals);
        if(result != KUI_ZSO_OK) return result;
        done += literals;
        if(done == KUI_ZSO_BLOCK_BYTES) {
            if(literals < 5u || (last_match != UINT32_MAX && last_match > KUI_ZSO_BLOCK_BYTES - 12u) ||
               input.end - input.at >= (1u << info->shift)) return KUI_ZSO_INVALID;
            return KUI_ZSO_OK;
        }
        uint8_t distance_bytes[2];
        result = take(&input, distance_bytes, 2u);
        if(result != KUI_ZSO_OK) return result;
        uint32_t distance = distance_bytes[0] | (uint32_t)distance_bytes[1] << 8, count;
        if(!distance || distance > done) return KUI_ZSO_INVALID;
        result = length(&input, token & 15u, &count);
        if(result != KUI_ZSO_OK) return result;
        count += 4u;
        if(count > KUI_ZSO_BLOCK_BYTES - done) return KUI_ZSO_INVALID;
        last_match = done;
        /* Byte order implements overlapping matches, including distance1. */
        while(count--) { out[done] = out[done - distance]; ++done; }
    }
}
const char *kui_zso_result_name(enum kui_zso_result result) {
    switch(result) {
    case KUI_ZSO_OK: return "OK";
    case KUI_ZSO_INVALID: return "Invalid standard LZ4 ZSO block or header";
    case KUI_ZSO_UNSUPPORTED: return "Unsupported ZSO format; standard LZ4 v1 with 2048-byte blocks required";
    case KUI_ZSO_IO: return "ZSO read failed";
    case KUI_ZSO_CANCELLED: return "Cancelled";
    case KUI_ZSO_RANGE: return "ZSO container outside admitted bounds";
    default: return "Unknown ZSO error";
    }
}
