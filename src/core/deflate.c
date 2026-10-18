/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/deflate.h"
#include <string.h>

/* Independently implemented from RFC 1951 sections 3.1--3.2, not another
 * loader's source. Canonical trees retain counts and four-bit code widths;
 * fixed trees use the RFC's arithmetic ranges instead of a lookup table.
 * Dynamic lookup scans the bounded alphabet only after finding a code group.
 * This deliberately trades some dynamic-tree speed for resident RAM. */
static unsigned bits(struct kui_deflate_input *in, unsigned count) {
    if(in->error != KUI_ZSO_OK) return 0;
    while(in->bits < count) {
        if(in->at == in->end) { in->error = KUI_ZSO_INVALID; return 0; }
        uint8_t byte;
        in->error = in->read(in->context, in->at, &byte, 1u);
        if(in->error != KUI_ZSO_OK) return 0;
        ++in->at;
        in->hold |= (uint32_t)byte << in->bits;
        in->bits += 8u;
    }
    unsigned out = in->hold & ((1u << count) - 1u);
    in->hold >>= count;
    in->bits -= count;
    return out;
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
    for(unsigned n = 0; n <= maximum; ++n) counts[n] = 0;
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
static unsigned symbol(struct kui_deflate_input *in, const uint16_t *counts,
    const uint8_t *widths, unsigned base, unsigned size, unsigned maximum) {
    in->code = 0; in->first = 0; in->maximum = maximum;
    in->counts = counts; in->widths = widths; in->base = base; in->size = size;
    for(in->depth = 1; in->depth <= in->maximum; ++in->depth) {
        unsigned bit = bits(in, 1u);
        if(in->error != KUI_ZSO_OK) return UINT32_MAX;
        in->code = (in->code << 1) | bit;
        if(in->code >= in->first && in->code - in->first < in->counts[in->depth]) {
            unsigned ordinal = in->code - in->first;
            for(unsigned i = 0; i < in->size; ++i) if(width(in->widths, in->base + i) == in->depth) {
                if(!ordinal) return i;
                --ordinal;
            }
            return UINT32_MAX;
        }
        in->first = (in->first + in->counts[in->depth]) << 1;
    }
    return UINT32_MAX;
}
static unsigned dynamic(struct kui_deflate_input *in,
    struct kui_deflate_workspace *work, unsigned *literal_count,
    unsigned *distance_count) {
    static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    unsigned literals = bits(in, 5u), distances = bits(in, 5u), codes = bits(in, 4u);
    literals += 257u; distances += 1u; codes += 4u;
    if(literals > 286u || in->error != KUI_ZSO_OK) return 0;
    memset(work->code_widths, 0, sizeof(work->code_widths));
    for(unsigned i = 0; i < codes; ++i) {
        unsigned value = bits(in, 3u);
        /* Code-length order is not sequential, so assign both nibble values. */
        unsigned n = order[i], shift = (n & 1u) * 4u;
        work->code_widths[n >> 1] |= (uint8_t)(value << shift);
    }
    /* The literal counts are not needed until all code widths are decoded,
     * so this first array temporarily holds the code-length tree counts. */
    if(in->error != KUI_ZSO_OK ||
       tree(work->literal_counts, work->code_widths, 0, 19u, 7u, 0) != KUI_ZSO_OK) return 0;
    unsigned at = 0, previous = 0, size = literals + distances;
    while(at < size) {
        unsigned value = symbol(in, work->literal_counts, work->code_widths, 0, 19u, 7u), count = 1;
        if(value == UINT32_MAX) return 0;
        if(value >= 16u) {
            if(value == 16u && !at) return 0;
            unsigned extra = bits(in, value == 18u ? 7u : value == 17u ? 3u : 2u);
            count = extra + (value == 18u ? 11u : 3u);
            value = value == 16u ? previous : 0u;
        }
        if(count > size - at || in->error != KUI_ZSO_OK) return 0;
        previous = value;
        while(count--) set_width(work->widths, at++, value);
    }
    if(!width(work->widths, 256u) ||
       tree(work->literal_counts, work->widths, 0, literals, 15u, 0) != KUI_ZSO_OK ||
       tree(work->distance_counts, work->widths, literals, distances, 15u, 1) != KUI_ZSO_OK) return 0;
    *literal_count = literals; *distance_count = distances;
    return 1;
}
static unsigned reversed(unsigned value, unsigned count) {
    unsigned out = 0;
    do { out = (out << 1) | (value & 1u); value >>= 1; } while(--count);
    return out;
}
static unsigned fixed(struct kui_deflate_input *in) {
    unsigned code = bits(in, 7u), extra;
    if(in->error != KUI_ZSO_OK) return UINT32_MAX;
    code = reversed(code, 7u);
    if(code < 24u) return code + 256u;
    extra = bits(in, 1u);
    code = (code << 1) | extra;
    if(code < 192u) return code - 48u;
    if(code < 200u) return code - 192u + 280u;
    extra = bits(in, 1u);
    return ((code << 1) | extra) - 400u + 144u;
}
enum kui_zso_result kui_deflate_decode(kui_zso_read_fn read, void *context,
    uint32_t start, uint32_t end, uint8_t out[KUI_ZSO_BLOCK_BYTES],
    struct kui_deflate_workspace *work, uint32_t *consumed) {
    if(!read || !out || !work || !consumed || start >= end ||
       end - start > KUI_ZSO_MAX_SPAN) return KUI_ZSO_INVALID;
    struct kui_deflate_input *in = &work->input;
    in->read = read; in->context = context; in->at = start; in->end = end;
    in->hold = 0; in->bits = 0; in->error = KUI_ZSO_OK;
    work->done = 0;
    do {
        unsigned header = bits(in, 3u); work->type = header >> 1;
        work->final = header & 1u;
        if(work->type == 3u || in->error != KUI_ZSO_OK) goto invalid;
        if(!work->type) {
            in->hold = 0; in->bits = 0; /* Stored blocks begin on a byte boundary. */
            unsigned count = bits(in, 16u), inverse = bits(in, 16u);
            if((count ^ inverse) != 65535u || count > KUI_ZSO_BLOCK_BYTES - work->done ||
               count > in->end - in->at || in->error != KUI_ZSO_OK) goto invalid;
            if(count) {
                in->error = in->read(in->context, in->at, out + work->done, count);
                if(in->error != KUI_ZSO_OK) goto invalid;
                in->at += count; work->done += count;
            }
            continue;
        }
        if(work->type == 2u) {
            if(!dynamic(in, work, &work->literals, &work->distances)) goto invalid;
        }
        for(;;) {
            unsigned value = work->type == 1u ? fixed(in) :
                symbol(in, work->literal_counts, work->widths, 0, work->literals, 15u);
            if(value == UINT32_MAX || in->error != KUI_ZSO_OK) goto invalid;
            if(value < 256u) {
                if(work->done == KUI_ZSO_BLOCK_BYTES) goto invalid;
                out[work->done++] = (uint8_t)value;
                continue;
            }
            if(value == 256u) break;
            if(value > 285u) goto invalid;
            unsigned count, extra = value < 265u ? 0u : (value - 261u) >> 2;
            if(value == 285u) count = 258u;
            else {
                count = bits(in, extra);
                count += extra ? ((4u + ((value - 265u) & 3u)) << extra) + 3u : value - 254u;
            }
            if(count > KUI_ZSO_BLOCK_BYTES - work->done) goto invalid;
            if(work->type == 1u) {
                value = bits(in, 5u);
                value = reversed(value, 5u);
            } else value = symbol(in, work->distance_counts, work->widths,
                                    work->literals, work->distances, 15u);
            if(value > 29u) goto invalid;
            extra = value < 4u ? 0u : (value >> 1) - 1u;
            unsigned distance = bits(in, extra);
            distance += extra ? ((2u + (value & 1u)) << extra) + 1u : value + 1u;
            if(distance > work->done || in->error != KUI_ZSO_OK) goto invalid;
            while(count--) { out[work->done] = out[work->done - distance]; ++work->done; }
        }
    } while(!work->final);
    if(work->done != KUI_ZSO_BLOCK_BYTES) goto invalid;
    *consumed = in->at - start;
    in->read = NULL; in->context = NULL;
    return KUI_ZSO_OK;
invalid:
    in->read = NULL; in->context = NULL;
    return in->error == KUI_ZSO_OK ? KUI_ZSO_INVALID : in->error;
}
