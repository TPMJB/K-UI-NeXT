/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Link the freestanding implementation with renamed symbols so libc and the
 * sanitizer runtime retain their own routines and provide an independent
 * reference. Check all alignment pairs, word boundaries, tails and canaries. */
void *kui_test_memcpy(void *out, const void *in, size_t bytes);

int main(void) {
    _Alignas(32) uint8_t source[160], original[160];
    _Alignas(32) uint8_t actual[160], expected[160];
    static const size_t lengths[] = {0, 1, 2, 3, 4, 5, 7, 8, 31, 32, 33, 64, 127};
    for(size_t i = 0; i < sizeof(source); ++i)
        original[i] = source[i] = (uint8_t)(i * 73u + 19u);
    for(unsigned src = 0; src < 4; ++src) {
        for(unsigned dst = 0; dst < 4; ++dst) {
            for(unsigned n = 0; n < sizeof(lengths) / sizeof(lengths[0]); ++n) {
                memset(actual, 0xad, sizeof(actual));
                memset(expected, 0xad, sizeof(expected));
                memcpy(expected + 16 + dst, source + 16 + src, lengths[n]);
                assert(kui_test_memcpy(actual + 16 + dst, source + 16 + src,
                                       lengths[n]) == actual + 16 + dst);
                assert(!memcmp(actual, expected, sizeof(actual)));
                assert(!memcmp(source, original, sizeof(source)));
            }
        }
    }
    puts("retail memcpy alignment, tails and canaries passed");
    return 0;
}
