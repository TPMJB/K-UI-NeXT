/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/crc16.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef uint16_t (*crc_fn)(uint16_t, const void *, size_t);
static const crc_fn variants[] = {kui_crc16_table, kui_crc16_slice2, kui_crc16_nibble};
static const char *names[] = {"table", "slice2", "nibble"};
static uint32_t rng = 0x2545f491u;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

int main(void) {
    /* Published check values: XMODEM (start 0) and CCITT-FALSE (start 0xffff). If the
     * reference itself were wrong, everything below would agree on the wrong answer. */
    assert(kui_crc16_ref(0, "123456789", 9) == 0x31c3);
    assert(kui_crc16_ref(0xffff, "123456789", 9) == 0x29b1);
    assert(kui_crc16_ref(0x1234, "", 0) == 0x1234);       /* nothing in, state unchanged */
    for(unsigned v = 0; v < 3; ++v) {
        assert(variants[v](0, "123456789", 9) == 0x31c3);
        assert(variants[v](0xffff, "123456789", 9) == 0x29b1);
        assert(variants[v](0x1234, "", 0) == 0x1234);
    }

    /* Every variant equals the reference on random data: every length up to a few
     * blocks (so the odd-byte tail of slice2 is hit), random starts, and every pointer
     * alignment (the variants read bytes, so none may depend on it). */
    static uint8_t data[4096 + 8];
    for(unsigned i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)next();
    for(unsigned round = 0; round < 3000; ++round) {
        size_t offset = next() & 3, length = round < 1600 ? round : next() % 2600;
        uint16_t start = (uint16_t)next();
        uint16_t expected = kui_crc16_ref(start, data + offset, length);
        for(unsigned v = 0; v < 3; ++v)
            if(variants[v](start, data + offset, length) != expected) {
                fprintf(stderr, "%s differs: offset=%zu length=%zu start=%04x\n", names[v], offset, length, start);
                return 1;
            }
    }

    /* Calls chain, at odd and even split points: f(f(s, a), b) == f(s, a + b). */
    for(unsigned round = 0; round < 500; ++round) {
        size_t total = 1 + next() % 1500, split = next() % (total + 1);
        uint16_t start = (uint16_t)next(), whole = kui_crc16_ref(start, data, total);
        for(unsigned v = 0; v < 3; ++v)
            assert(variants[v](variants[v](start, data, split), data + split, total - split) == whole);
    }

    /* What the SD driver actually does: 512-byte blocks, each from state 0. */
    for(unsigned block = 0; block < 8; ++block) {
        uint16_t expected = kui_crc16_ref(0, data + block * 512, 512);
        for(unsigned v = 0; v < 3; ++v) assert(variants[v](0, data + block * 512, 512) == expected);
    }
    puts("PASS crc16: KOS's function, table, slice2 and nibble agree (check values, lengths, starts, alignment, chaining, SD blocks)");
    return 0;
}
