/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/deflate.h"
#include <string.h>

/* Independently implemented from RFC 1951 sections 3.1--3.2, not another
 * loader's source. Canonical trees retain counts and four-bit code widths;
 * fixed trees use the RFC's arithmetic ranges instead of a lookup table.
 * Dynamic lookup scans the bounded alphabet only after finding a code group.
 * This deliberately trades some dynamic-tree speed for resident RAM. */
struct input {
    kui_zso_read_fn read;
    void *context;
    uint32_t at, end, hold;
    unsigned bits;
};
static enum kui_zso_result bits(struct input *in, unsigned count, unsigned *out) {
    while(in->bits < count) {
        if(in->at == in->end) return KUI_ZSO_INVALID;
        uint8_t byte;
        enum kui_zso_result result = in->read(in->context, in->at, &byte, 1u);
        if(result != KUI_ZSO_OK) return result;
        ++in->at;
        in->hold |= (uint32_t)byte << in->bits;
        in->bits += 8u;
    }
    *out = in->hold & ((1u << count) - 1u);
    in->hold >>= count;
    in->bits -= count;
    return KUI_ZSO_OK;
}
static unsigned width(const uint8_t *widths, unsigned index) {
    return (widths[index >> 1] >> ((index & 1u) * 4u)) & 15u;
}
static void set_width(uint8_t *widths, unsigned index, unsigned value) {
    if(index & 1u) widths[index >> 1] |= (uint8_t)(value << 4);
    else widths[index >> 1] = (uint8_t)value;
}
static enum kui_zso_result tree(uint16_t *counts, const uint8_t *widths,
    unsigned base, unsigned size, unsigned maximum, unsigned empty) {
    memset(counts, 0, (maximum + 1u) * sizeof(*counts));
    unsigned total = 0;
    for(unsigned i = 0; i < size; ++i) {
        unsigned n = width(widths, base + i);
        if(n > maximum) return KUI_ZSO_INVALID;
        if(n) { ++counts[n]; ++total; }
    }
    if(!total) return empty ? KUI_ZSO_OK : KUI_ZSO_INVALID;
    unsigned left = 1;
    for(unsigned n = 1; n <= maximum; ++n) {
        left <<= 1;
        if(counts[n] > left) return KUI_ZSO_INVALID; /* Oversubscribed. */
        left -= counts[n];
    }
    /* Only a one-bit singleton may leave an incomplete literal/distance tree.
     * A code-length tree must be complete (maximum7). */
    return left && (maximum == 7u || total != 1u || counts[1] != 1u) ?
        KUI_ZSO_INVALID : KUI_ZSO_OK;
}
static enum kui_zso_result symbol(struct input *in, const uint16_t *counts,
    const uint8_t *widths, unsigned base, unsigned size, unsigned maximum,
    unsigned *out) {
    unsigned code = 0, first = 0;
    for(unsigned n = 1; n <= maximum; ++n) {
        unsigned bit;
        enum kui_zso_result result = bits(in, 1u, &bit);
        if(result != KUI_ZSO_OK) return result;
        code = (code << 1) | bit;
        if(code >= first && code - first < counts[n]) {
            unsigned ordinal = code - first;
            for(unsigned i = 0; i < size; ++i) if(width(widths, base + i) == n) {
                if(!ordinal) { *out = i; return KUI_ZSO_OK; }
                --ordinal;
            }
            return KUI_ZSO_INVALID;
        }
        first = (first + counts[n]) << 1;
    }
    return KUI_ZSO_INVALID;
}
static enum kui_zso_result dynamic(struct input *in,
    struct kui_deflate_workspace *work, unsigned *literal_count,
    unsigned *distance_count) {
    static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    unsigned literals, distances, codes;
    enum kui_zso_result result = bits(in, 5u, &literals);
    if(result != KUI_ZSO_OK) return result;
    result = bits(in, 5u, &distances);
    if(result != KUI_ZSO_OK) return result;
    result = bits(in, 4u, &codes);
    if(result != KUI_ZSO_OK) return result;
    literals += 257u; distances += 1u; codes += 4u;
    if(literals > 286u) return KUI_ZSO_INVALID;
    memset(work->code_widths, 0, sizeof(work->code_widths));
    for(unsigned i = 0; i < codes; ++i) {
        unsigned value;
        result = bits(in, 3u, &value);
        if(result != KUI_ZSO_OK) return result;
        /* Code-length order is not sequential, so assign both nibble values. */
        unsigned n = order[i], shift = (n & 1u) * 4u;
        work->code_widths[n >> 1] |= (uint8_t)(value << shift);
    }
    result = tree(work->code_counts, work->code_widths, 0, 19u, 7u, 0);
    if(result != KUI_ZSO_OK) return result;
    unsigned at = 0, previous = 0, size = literals + distances;
    while(at < size) {
        unsigned value, count = 1;
        result = symbol(in, work->code_counts, work->code_widths, 0, 19u, 7u, &value);
        if(result != KUI_ZSO_OK) return result;
        if(value >= 16u) {
            if(value == 16u && !at) return KUI_ZSO_INVALID;
            unsigned extra;
            result = bits(in, value == 18u ? 7u : value == 17u ? 3u : 2u, &extra);
            if(result != KUI_ZSO_OK) return result;
            count = extra + (value == 18u ? 11u : 3u);
            value = value == 16u ? previous : 0u;
        }
        if(count > size - at) return KUI_ZSO_INVALID;
        previous = value;
        while(count--) set_width(work->widths, at++, value);
    }
    if(!width(work->widths, 256u)) return KUI_ZSO_INVALID;
    result = tree(work->counts[0], work->widths, 0, literals, 15u, 0);
    if(result != KUI_ZSO_OK) return result;
    result = tree(work->counts[1], work->widths, literals, distances, 15u, 1);
    if(result != KUI_ZSO_OK) return result;
    *literal_count = literals; *distance_count = distances;
    return KUI_ZSO_OK;
}
static unsigned reversed(unsigned value, unsigned count) {
    unsigned out = 0;
    do { out = (out << 1) | (value & 1u); value >>= 1; } while(--count);
    return out;
}
static enum kui_zso_result fixed(struct input *in, unsigned *out) {
    unsigned code, extra;
    enum kui_zso_result result = bits(in, 7u, &code);
    if(result != KUI_ZSO_OK) return result;
    code = reversed(code, 7u);
    if(code < 24u) { *out = code + 256u; return KUI_ZSO_OK; }
    result = bits(in, 1u, &extra);
    if(result != KUI_ZSO_OK) return result;
    code = (code << 1) | extra;
    if(code < 192u) { *out = code - 48u; return KUI_ZSO_OK; }
    if(code < 200u) { *out = code - 192u + 280u; return KUI_ZSO_OK; }
    result = bits(in, 1u, &extra);
    if(result != KUI_ZSO_OK) return result;
    *out = ((code << 1) | extra) - 400u + 144u;
    return KUI_ZSO_OK;
}
enum kui_zso_result kui_deflate_decode(kui_zso_read_fn read, void *context,
    uint32_t start, uint32_t end, uint8_t out[KUI_ZSO_BLOCK_BYTES],
    struct kui_deflate_workspace *work, uint32_t *consumed) {
    if(!read || !out || !work || !consumed || start >= end ||
       end - start > KUI_ZSO_MAX_SPAN) return KUI_ZSO_INVALID;
    struct input in = {read, context, start, end, 0, 0};
    unsigned done = 0, final;
    do {
        unsigned type;
        enum kui_zso_result result = bits(&in, 1u, &final);
        if(result != KUI_ZSO_OK) return result;
        result = bits(&in, 2u, &type);
        if(result != KUI_ZSO_OK) return result;
        if(type == 3u) return KUI_ZSO_INVALID;
        if(!type) {
            unsigned count, inverse;
            in.hold = 0; in.bits = 0; /* Stored blocks begin on a byte boundary. */
            result = bits(&in, 16u, &count);
            if(result != KUI_ZSO_OK) return result;
            result = bits(&in, 16u, &inverse);
            if(result != KUI_ZSO_OK) return result;
            if((count ^ inverse) != 65535u || count > KUI_ZSO_BLOCK_BYTES - done ||
               count > in.end - in.at) return KUI_ZSO_INVALID;
            if(count) {
                result = read(context, in.at, out + done, count);
                if(result != KUI_ZSO_OK) return result;
                in.at += count; done += count;
            }
            continue;
        }
        unsigned literals = 0, distances = 0;
        if(type == 2u) {
            result = dynamic(&in, work, &literals, &distances);
            if(result != KUI_ZSO_OK) return result;
        }
        for(;;) {
            unsigned value;
            result = type == 1u ? fixed(&in, &value) :
                symbol(&in, work->counts[0], work->widths, 0, literals, 15u, &value);
            if(result != KUI_ZSO_OK) return result;
            if(value < 256u) {
                if(done == KUI_ZSO_BLOCK_BYTES) return KUI_ZSO_INVALID;
                out[done++] = (uint8_t)value;
                continue;
            }
            if(value == 256u) break;
            if(value > 285u) return KUI_ZSO_INVALID;
            unsigned count, extra = value < 265u ? 0u : (value - 261u) >> 2;
            if(value == 285u) count = 258u;
            else {
                result = bits(&in, extra, &count);
                if(result != KUI_ZSO_OK) return result;
                count += extra ? (1u << (extra + 2u)) + 3u + ((value - 265u) & 3u) * (1u << extra) : value - 254u;
            }
            if(count > KUI_ZSO_BLOCK_BYTES - done) return KUI_ZSO_INVALID;
            if(type == 1u) {
                result = bits(&in, 5u, &value);
                if(result != KUI_ZSO_OK) return result;
                value = reversed(value, 5u);
            } else result = symbol(&in, work->counts[1], work->widths,
                                    literals, distances, 15u, &value);
            if(result != KUI_ZSO_OK) return result;
            if(value > 29u) return KUI_ZSO_INVALID;
            unsigned distance;
            extra = value < 4u ? 0u : (value >> 1) - 1u;
            result = bits(&in, extra, &distance);
            if(result != KUI_ZSO_OK) return result;
            distance += extra ? (1u << (extra + 1u)) + 1u + (value & 1u) * (1u << extra) : value + 1u;
            if(distance > done) return KUI_ZSO_INVALID;
            while(count--) { out[done] = out[done - distance]; ++done; }
        }
    } while(!final);
    if(done != KUI_ZSO_BLOCK_BYTES) return KUI_ZSO_INVALID;
    *consumed = in.at - start;
    return KUI_ZSO_OK;
}
