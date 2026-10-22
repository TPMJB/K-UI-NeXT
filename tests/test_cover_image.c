/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cover_image.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fixtures/cover_images.h"

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t crc(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffffu;
    for(size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for(unsigned k = 0; k < 8; ++k) c = c & 1u ? c >> 1 ^ 0xedb88320u : c >> 1;
    }
    return ~c;
}
static enum kui_cover_image_status decode(const uint8_t *data, size_t size, unsigned *w, unsigned *h, unsigned *ch,
                                          uint8_t **px) {
    enum kui_cover_image_status s = kui_cover_image_decode(data, size, px, w, h, ch);
    if(s != KUI_COVER_IMAGE_OK) assert(!*px && !*w && !*h && !*ch);
    return s;
}
int main(void) {
    uint8_t *px; unsigned w, h, ch;
    assert(decode(cover_png_rgb, sizeof(cover_png_rgb), &w, &h, &ch, &px) == KUI_COVER_IMAGE_OK);
    assert(w == 64 && h == 48 && ch == 3);
    for(unsigned y = 0; y < h; ++y) for(unsigned x = 0; x < w; ++x) {
        const uint8_t *p = px + (y * w + x) * 3;
        assert(p[0] == x * 4 && p[1] == y * 5 && p[2] == (x + y) * 2);
    }
    kui_cover_image_free(px);
    assert(decode(cover_png_rgba, sizeof(cover_png_rgba), &w, &h, &ch, &px) == KUI_COVER_IMAGE_OK);
    assert(w == 16 && h == 16 && ch == 4);
    for(unsigned y = 0; y < h; ++y) for(unsigned x = 0; x < w; ++x) {
        const uint8_t *p = px + (y * w + x) * 4;
        assert(p[0] == x * 16 && p[1] == 255 - y * 16 && p[2] == 128 && p[3] == (x + y) * 8);
    }
    kui_cover_image_free(px);
    /* Lossy, so close rather than exact. */
    assert(decode(cover_jpeg, sizeof(cover_jpeg), &w, &h, &ch, &px) == KUI_COVER_IMAGE_OK);
    assert(w == 64 && h == 48 && ch == 3);
    for(unsigned y = 0; y < h; ++y) for(unsigned x = 0; x < w; ++x) {
        const uint8_t *p = px + (y * w + x) * 3;
        int want[3] = {(int)(x * 4), (int)(y * 5), (int)((x + y) * 2)};
        for(unsigned c = 0; c < 3; ++c) assert(abs(p[c] - want[c]) <= 12);
    }
    kui_cover_image_free(px);
    /* Every truncation fails cleanly (sanitizers watch the reads). */
    for(size_t n = 0; n < sizeof(cover_png_rgb); n += n < 64 ? 1u : 37u) {
        uint8_t *copy = malloc(n ? n : 1);
        memcpy(copy, cover_png_rgb, n);
        enum kui_cover_image_status s = decode(copy, n, &w, &h, &ch, &px);
        assert(s == (n < 8 ? KUI_COVER_IMAGE_UNSUPPORTED : KUI_COVER_IMAGE_CORRUPT));
        free(copy);
    }
    for(size_t n = 3; n < sizeof(cover_jpeg); n += 29u) {
        uint8_t *copy = malloc(n);
        memcpy(copy, cover_jpeg, n);
        enum kui_cover_image_status s = decode(copy, n, &w, &h, &ch, &px);
        if(s == KUI_COVER_IMAGE_OK) kui_cover_image_free(px); /* A JPEG may end early and still decode. */
        else assert(s == KUI_COVER_IMAGE_CORRUPT || s == KUI_COVER_IMAGE_UNSUPPORTED);
        free(copy);
    }
    /* Size limits are checked from the header, before any decoding. */
    /* The header scan stops at the first IDAT, so an empty one suffices. */
    uint8_t big[64] = {0};
    memcpy(big, cover_png_rgb, 33);
    put32(big + 16, 2000); put32(big + 20, 2000);
    put32(big + 29, crc(big + 12, 17));
    memcpy(big + 37, "IDAT", 4);
    put32(big + 41, crc(big + 37, 4));
    assert(decode(big, 45, &w, &h, &ch, &px) == KUI_COVER_IMAGE_TOO_LARGE);
    put32(big + 16, 1000); put32(big + 20, 1000);
    put32(big + 29, crc(big + 12, 17));
    assert(decode(big, 45, &w, &h, &ch, &px) == KUI_COVER_IMAGE_CORRUPT); /* Allowed size, no data. */
    assert(decode(cover_png_rgb, KUI_COVER_IMAGE_MAX_BYTES + 1u, &w, &h, &ch, &px) == KUI_COVER_IMAGE_TOO_LARGE);
    const uint8_t gif[16] = "GIF89a";
    assert(decode(gif, sizeof(gif), &w, &h, &ch, &px) == KUI_COVER_IMAGE_UNSUPPORTED);
    assert(decode(NULL, 100, &w, &h, &ch, &px) == KUI_COVER_IMAGE_UNSUPPORTED);
    assert(kui_cover_image_decode(cover_png_rgb, sizeof(cover_png_rgb), NULL, &w, &h, &ch) == KUI_COVER_IMAGE_CORRUPT);
    for(int s = KUI_COVER_IMAGE_OK; s <= KUI_COVER_IMAGE_MEMORY; ++s)
        assert(kui_cover_image_status_text((enum kui_cover_image_status)s)[0]);
    puts("PASS cover image decoding");
    return 0;
}
