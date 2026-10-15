/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/lzo1x.h"

/* Independent byte-stream state machine for the LZO1X token grammar.
 * The public miniLZO distribution is used as a format reference and test
 * oracle only. Explicit counts and distances avoid forming an invalid
 * pointer before validating a match. */
struct input {
    kui_zso_read_fn read;
    void *context;
    uint32_t at, end;
};
static enum kui_zso_result take(struct input *in, void *out, uint32_t count) {
    if(count > in->end - in->at) return KUI_ZSO_INVALID;
    if(!count) return KUI_ZSO_OK;
    enum kui_zso_result result = in->read(in->context, in->at, out, count);
    if(result == KUI_ZSO_OK) in->at += count;
    return result;
}
static enum kui_zso_result extended(struct input *in, uint32_t base, uint32_t *count) {
    uint8_t extra;
    do {
        enum kui_zso_result result = take(in, &extra, 1);
        if(result != KUI_ZSO_OK) return result;
        uint32_t add = extra ? extra : 255u;
        if(add > KUI_ZSO_BLOCK_BYTES - base) return KUI_ZSO_INVALID;
        base += add;
    } while(!extra);
    *count = base;
    return KUI_ZSO_OK;
}
enum kui_zso_result kui_lzo1x_decode(kui_zso_read_fn read, void *context,
    uint32_t start, uint32_t end, uint8_t out[KUI_ZSO_BLOCK_BYTES],
    uint32_t *consumed) {
    if(!read || !out || !consumed || start >= end) return KUI_ZSO_INVALID;
    struct input in = {read, context, start, end};
    uint32_t done = 0, count = 0;
    /* A zero state admits a literal run; one follows a long literal run
     * (whose short token has the special 2049+ distance); two admits only
     * a match after the preceding match's attached one-to-three literals. */
    unsigned state = 0;
    uint8_t token, low, high;
    enum kui_zso_result result = take(&in, &token, 1);
    if(result != KUI_ZSO_OK) return result;
    if(token > 17u) {
        count = token - 17u;
        state = count < 4u ? 2u : 1u;
        goto literals;
    }
    for(;;) {
        if(!state && token < 16u) {
            count = token;
            if(!count) {
                result = extended(&in, 15u, &count);
                if(result != KUI_ZSO_OK) return result;
            }
            count += 3u;
            state = 1;
            goto literals;
        }
        uint32_t distance, trailing;
        if(token < 16u) {
            result = take(&in, &low, 1);
            if(result != KUI_ZSO_OK) return result;
            distance = 1u + (token >> 2) + ((uint32_t)low << 2);
            count = 2;
            if(state == 1u) { distance += 2048u; count = 3; }
            trailing = token & 3u;
        } else if(token >= 64u) {
            result = take(&in, &low, 1);
            if(result != KUI_ZSO_OK) return result;
            distance = 1u + ((token >> 2) & 7u) + ((uint32_t)low << 3);
            count = (token >> 5) + 1u;
            trailing = token & 3u;
        } else {
            count = token & (token >= 32u ? 31u : 7u);
            if(!count) {
                result = extended(&in, token >= 32u ? 31u : 7u, &count);
                if(result != KUI_ZSO_OK) return result;
            }
            result = take(&in, &low, 1);
            if(result != KUI_ZSO_OK) return result;
            result = take(&in, &high, 1);
            if(result != KUI_ZSO_OK) return result;
            distance = (low >> 2) + ((uint32_t)high << 6);
            trailing = low & 3u;
            if(token >= 32u) ++distance;
            else {
                distance += (uint32_t)(token & 8u) << 11;
                if(!distance) {
                    if(token != 17u || low || done != KUI_ZSO_BLOCK_BYTES)
                        return KUI_ZSO_INVALID;
                    *consumed = in.at - start;
                    return KUI_ZSO_OK;
                }
                distance += 16384u;
            }
            count += 2u;
        }
        if(distance > done || count > KUI_ZSO_BLOCK_BYTES - done)
            return KUI_ZSO_INVALID;
        /* Increasing byte order also implements distance-one repetitions. */
        while(count--) { out[done] = out[done - distance]; ++done; }
        count = trailing;
        state = count ? 2u : 0u;
literals:
        if(count > KUI_ZSO_BLOCK_BYTES - done) return KUI_ZSO_INVALID;
        result = take(&in, out + done, count);
        if(result != KUI_ZSO_OK) return result;
        done += count;
        result = take(&in, &token, 1);
        if(result != KUI_ZSO_OK) return result;
    }
}
