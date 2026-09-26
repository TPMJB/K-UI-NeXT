/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/pvr_texture.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Synthetic textures only. The reference encoder twiddles with the
 * incremental Morton masks described in KallistiOS utils/pvrtex/pvr_texture.c,
 * a different method from the decoder's bit spreading. */
struct morton { uint32_t x_mask, y_mask, x_inc, y_inc; };
static uint32_t low_mask(unsigned bits) { return bits ? 0xffffffffu >> (32u - bits) : 0u; }
static unsigned bits_of(unsigned v) { unsigned n = 0; while(v) { ++n; v >>= 1; } return n; }
static struct morton morton_init(unsigned w, unsigned h) {
    unsigned xb = bits_of(w), yb = bits_of(h), shared = xb < yb ? xb : yb;
    unsigned span = (shared - 1u) * 2u;
    struct morton m;
    m.x_mask = (0xaaaaaaaau & low_mask(span)) | low_mask(xb - shared) << span;
    m.y_mask = (0x55555555u & low_mask(span)) | low_mask(yb - shared) << span;
    m.x_inc = 2u | ~m.x_mask;
    m.y_inc = 1u | ~m.y_mask;
    return m;
}
/* Linear w*h 16-bit texels to twiddled order. */
static void twiddle16(const uint16_t *linear, unsigned w, unsigned h, uint16_t *out) {
    struct morton m = morton_init(w, h);
    uint32_t ym = 0;
    for(unsigned y = 0; y < h; ++y) {
        uint32_t xm = 0;
        for(unsigned x = 0; x < w; ++x) {
            out[xm | ym] = linear[y * w + x];
            xm = (xm + m.x_inc) & m.x_mask;
        }
        ym = (ym + m.y_inc) & m.y_mask;
    }
}
static void untwiddle16(const uint16_t *twiddled, unsigned w, unsigned h, uint16_t *out) {
    struct morton m = morton_init(w, h);
    uint32_t ym = 0;
    for(unsigned y = 0; y < h; ++y) {
        uint32_t xm = 0;
        for(unsigned x = 0; x < w; ++x) {
            out[y * w + x] = twiddled[xm | ym];
            xm = (xm + m.x_inc) & m.x_mask;
        }
        ym = (ym + m.y_inc) & m.y_mask;
    }
}
static uint16_t pattern(unsigned x, unsigned y, unsigned seed) {
    return (uint16_t)(((x * 7u + y * 3u + seed) & 31u) << 11 | ((x ^ (y * 5u + seed)) & 63u) << 5 |
        ((x + 2u * y + seed * 3u) & 31u));
}
/* Expanding n bits to 8 repeats the value's bits from the top down, so full
 * intensity stays full; written as a bit loop, unlike the decoder. */
static unsigned repeat_bits(unsigned v, unsigned bits) {
    unsigned out = 0;
    for(unsigned i = 0; i < 8; ++i) out |= ((v >> (bits - 1u - i % bits)) & 1u) << (7u - i);
    return out;
}
static void rgba_of(uint16_t t, unsigned format, uint8_t px[4]) {
    unsigned r, g, b, a;
    if(format == 1) {
        r = repeat_bits(t >> 11, 5); g = repeat_bits((t >> 5) & 63u, 6); b = repeat_bits(t & 31u, 5); a = 255;
    } else if(format == 0) {
        r = repeat_bits((t >> 10) & 31u, 5); g = repeat_bits((t >> 5) & 31u, 5);
        b = repeat_bits(t & 31u, 5); a = (t & 0x8000u) ? 255u : 0u;
    } else {
        r = repeat_bits((t >> 8) & 15u, 4); g = repeat_bits((t >> 4) & 15u, 4);
        b = repeat_bits(t & 15u, 4); a = repeat_bits(t >> 12, 4);
    }
    px[0] = (uint8_t)r; px[1] = (uint8_t)g; px[2] = (uint8_t)b; px[3] = (uint8_t)a;
}
/* Replication stays within one step of exact rounding at every value. */
static void check_expansion(void) {
    for(unsigned bits = 4; bits <= 6; ++bits) {
        unsigned top = (1u << bits) - 1u;
        assert(repeat_bits(0, bits) == 0 && repeat_bits(top, bits) == 255);
        for(unsigned v = 0; v <= top; ++v) {
            int exact = (int)((v * 255u + top / 2u) / top), got = (int)repeat_bits(v, bits);
            assert(got - exact <= 1 && exact - got <= 1);
        }
    }
}
struct file { uint8_t *bytes; size_t size; };
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
/* Chunk length counts everything after the fourcc and length fields. */
static struct file make_file(bool gbix, unsigned format, unsigned layout, unsigned w, unsigned h,
                             const uint8_t *payload, size_t payload_size) {
    size_t head = gbix ? 16u : 0u;
    struct file f = {calloc(1, head + 16u + payload_size), head + 16u + payload_size};
    assert(f.bytes);
    if(gbix) { memcpy(f.bytes, "GBIX", 4); put32(f.bytes + 4, 8); put32(f.bytes + 8, 1234); }
    uint8_t *p = f.bytes + head;
    memcpy(p, "PVRT", 4);
    put32(p + 4, (uint32_t)(8u + payload_size));
    p[8] = (uint8_t)format; p[9] = (uint8_t)layout;
    put16(p + 12, w); put16(p + 14, h);
    memcpy(p + 16, payload, payload_size);
    return f;
}
static void expect_image(const struct file *f, unsigned w, unsigned h, const uint16_t *linear,
                         unsigned format, bool opaque) {
    struct kui_pvr_info info;
    assert(kui_pvr_parse(f->bytes, f->size, &info) == KUI_PVR_OK);
    assert(info.width == w && info.height == h);
    uint8_t *rgba = malloc((size_t)w * h * 4u);
    assert(rgba);
    assert(kui_pvr_decode(f->bytes, f->size, &info, rgba) == KUI_PVR_OK);
    for(unsigned i = 0; i < w * h; ++i) {
        uint8_t want[4]; rgba_of(linear[i], format, want);
        if(opaque) want[3] = 255;
        assert(!memcmp(rgba + i * 4u, want, 4));
    }
    free(rgba);
}
static void write_texels(uint8_t *out, const uint16_t *texels, size_t count) {
    for(size_t i = 0; i < count; ++i) put16(out + i * 2u, texels[i]);
}

static void square(unsigned format, unsigned layout, unsigned edge, size_t mip_bytes, bool gbix) {
    size_t n = (size_t)edge * edge;
    uint16_t *linear = malloc(n * 2u), *tw = malloc(n * 2u);
    uint8_t *payload = calloc(1, mip_bytes + n * 2u);
    assert(linear && tw && payload);
    for(unsigned y = 0; y < edge; ++y) for(unsigned x = 0; x < edge; ++x) {
        uint16_t t = pattern(x, y, layout);
        if(format != 1 && (x + y) % 3u == 0) t |= 0x8000u; /* Some visible alpha. */
        linear[y * edge + x] = t;
    }
    twiddle16(linear, edge, edge, tw);
    memset(payload, 0x5a, mip_bytes); /* Smaller levels: never decoded. */
    write_texels(payload + mip_bytes, tw, n);
    struct file f = make_file(gbix, format, layout, edge, edge, payload, mip_bytes + n * 2u);
    expect_image(&f, edge, edge, linear, format, false);
    /* Every shorter file is rejected without reading past its end. */
    for(size_t size = 0; size < f.size; size += size < 64 ? 1u : 97u) {
        struct kui_pvr_info info;
        uint8_t *copy = malloc(size ? size : 1u);
        memcpy(copy, f.bytes, size);
        assert(kui_pvr_parse(copy, size, &info) != KUI_PVR_OK);
        free(copy);
    }
    free(f.bytes); free(payload); free(tw); free(linear);
}
static void rectangle(unsigned w, unsigned h, bool stride) {
    size_t n = (size_t)w * h;
    uint16_t *linear = malloc(n * 2u), *order = malloc(n * 2u);
    uint8_t *payload = malloc(n * 2u);
    for(unsigned y = 0; y < h; ++y) for(unsigned x = 0; x < w; ++x) linear[y * w + x] = pattern(x, y, w);
    if(stride) memcpy(order, linear, n * 2u);
    else twiddle16(linear, w, h, order);
    write_texels(payload, order, n);
    struct file f = make_file(false, 1, stride ? 9 : 13, w, h, payload, n * 2u);
    expect_image(&f, w, h, linear, 1, false);
    free(f.bytes); free(payload); free(order); free(linear);
}
/* VQ as KallistiOS expands it: each index yields its codebook entry's four
 * texels in a row of the twiddled stream, then the stream is untwiddled. */
static void vq(unsigned layout, unsigned edge, unsigned entries, bool mip) {
    size_t blocks = (size_t)edge * edge / 4u;
    size_t chain = mip ? blocks * 4u / 3u + 1u : blocks;
    uint8_t codebook[256 * 8];
    for(unsigned e = 0; e < 256; ++e) for(unsigned k = 0; k < 4; ++k)
        put16(codebook + e * 8u + k * 2u, pattern(e, k, 9));
    uint8_t *payload = malloc(entries * 8u + chain);
    memcpy(payload, codebook, entries * 8u);
    uint8_t *indices = payload + entries * 8u;
    memset(indices, 0xee, chain - blocks);
    uint8_t *top = indices + chain - blocks;
    for(size_t k = 0; k < blocks; ++k) top[k] = (uint8_t)((k * 37u + 11u) % (entries + (entries < 256u)));
    uint16_t *stream = malloc(blocks * 8u), *linear = malloc(blocks * 8u);
    for(size_t k = 0; k < blocks; ++k) for(unsigned t = 0; t < 4; ++t)
        stream[k * 4u + t] = top[k] < entries ? pattern(top[k], t, 9) : 0u;
    untwiddle16(stream, edge, edge, linear);
    struct file f = make_file(true, 1, layout, edge, edge, payload, entries * 8u + chain);
    struct kui_pvr_info info;
    assert(kui_pvr_parse(f.bytes, f.size, &info) == KUI_PVR_OK);
    assert(info.vq && info.codebook_entries == entries && info.mipmaps == mip);
    expect_image(&f, edge, edge, linear, 1, false);
    free(f.bytes); free(stream); free(linear); free(payload);
}
static void alpha_rules(void) {
    /* ARGB1555 with every alpha bit clear reads as opaque artwork. */
    uint16_t linear[64], tw[64];
    for(unsigned i = 0; i < 64; ++i) linear[i] = (uint16_t)(pattern(i % 8u, i / 8u, 1) & 0x7fffu);
    twiddle16(linear, 8, 8, tw);
    uint8_t payload[128];
    write_texels(payload, tw, 64);
    struct file f = make_file(false, 0, 1, 8, 8, payload, sizeof(payload));
    expect_image(&f, 8, 8, linear, 0, true);
    free(f.bytes);
    /* The 8-bit "palette" format in a direct layout reads as ARGB1555. */
    linear[5] |= 0x8000u;
    twiddle16(linear, 8, 8, tw);
    write_texels(payload, tw, 64);
    f = make_file(false, 6, 1, 8, 8, payload, sizeof(payload));
    struct kui_pvr_info info;
    assert(kui_pvr_parse(f.bytes, f.size, &info) == KUI_PVR_OK && info.pixel_format == 0);
    expect_image(&f, 8, 8, linear, 0, false);
    free(f.bytes);
}
static enum kui_pvr_status parse_header(unsigned format, unsigned layout, unsigned w, unsigned h, size_t payload) {
    uint8_t *data = calloc(1, payload + 1u);
    struct file f = make_file(false, format, layout, w, h, data, payload);
    struct kui_pvr_info info;
    enum kui_pvr_status status = kui_pvr_parse(f.bytes, f.size, &info);
    if(status != KUI_PVR_OK) {
        struct kui_pvr_info zero;
        memset(&zero, 0, sizeof(zero));
        assert(!memcmp(&info, &zero, sizeof(info)));
    }
    free(f.bytes); free(data);
    return status;
}
static void rejects(void) {
    struct kui_pvr_info info;
    uint8_t junk[64] = "PVRX";
    assert(kui_pvr_parse(junk, sizeof(junk), &info) == KUI_PVR_NOT_PVR);
    assert(kui_pvr_parse(NULL, 64, &info) == KUI_PVR_NOT_PVR);
    assert(kui_pvr_parse(junk, 64, NULL) == KUI_PVR_NOT_PVR);
    memcpy(junk, "GBIX", 4); put32(junk + 4, 1000);
    assert(kui_pvr_parse(junk, sizeof(junk), &info) == KUI_PVR_TRUNCATED);
    put32(junk + 4, 8); memcpy(junk + 16, "PVRT", 4);
    put32(junk + 20, 4); /* Chunk too short for its own type fields. */
    assert(kui_pvr_parse(junk, sizeof(junk), &info) == KUI_PVR_TRUNCATED);
    assert(parse_header(1, 5, 64, 64, 8192) == KUI_PVR_UNSUPPORTED);  /* Palettized. */
    assert(parse_header(1, 7, 64, 64, 8192) == KUI_PVR_UNSUPPORTED);
    assert(parse_header(1, 10, 64, 32, 8192) == KUI_PVR_UNSUPPORTED); /* Not a hardware layout. */
    assert(parse_header(3, 1, 64, 64, 8192) == KUI_PVR_UNSUPPORTED);  /* YUV. */
    assert(parse_header(4, 1, 64, 64, 8192) == KUI_PVR_UNSUPPORTED);  /* Bump map. */
    assert(parse_header(5, 1, 64, 64, 8192) == KUI_PVR_UNSUPPORTED);
    assert(parse_header(1, 1, 48, 48, 8192) == KUI_PVR_SIZE);         /* Not a power of two. */
    assert(parse_header(1, 1, 4, 4, 32) == KUI_PVR_SIZE);
    assert(parse_header(1, 1, 2048, 2048, 64) == KUI_PVR_SIZE);
    assert(parse_header(1, 1, 64, 32, 8192) == KUI_PVR_SIZE);         /* Square layout, not square. */
    assert(parse_header(1, 3, 64, 32, 8192) == KUI_PVR_SIZE);
    assert(parse_header(1, 13, 64, 24, 8192) == KUI_PVR_SIZE);
    assert(parse_header(1, 9, 40, 16, 40 * 16 * 2) == KUI_PVR_OK);
    assert(parse_header(1, 9, 40, 16, 40 * 16 * 2 - 1) == KUI_PVR_TRUNCATED);
    assert(parse_header(1, 3, 64, 64, 2048 + 1023) == KUI_PVR_TRUNCATED);
    /* A mipmapped chunk that claims more bytes than the file holds. */
    uint8_t payload[16 * 16 * 2 + 64] = {0};
    struct file f = make_file(false, 1, 2, 16, 16, payload, sizeof(payload));
    put32(f.bytes + 4, (uint32_t)(8u + sizeof(payload) + 2u));
    assert(kui_pvr_parse(f.bytes, f.size, &info) == KUI_PVR_TRUNCATED);
    /* Decoding insists on the parse of the very same bytes. */
    put32(f.bytes + 4, (uint32_t)(8u + sizeof(payload)));
    assert(kui_pvr_parse(f.bytes, f.size, &info) == KUI_PVR_OK);
    uint8_t rgba[16 * 16 * 4];
    struct kui_pvr_info other = info;
    other.data_offset -= 2;
    assert(kui_pvr_decode(f.bytes, f.size, &other, rgba) == KUI_PVR_NOT_PVR);
    assert(kui_pvr_decode(f.bytes, f.size, &info, NULL) == KUI_PVR_NOT_PVR);
    assert(kui_pvr_decode(f.bytes, f.size, NULL, rgba) == KUI_PVR_NOT_PVR);
    free(f.bytes);
    for(int s = KUI_PVR_OK; s <= KUI_PVR_SIZE; ++s) assert(kui_pvr_status_text((enum kui_pvr_status)s)[0]);
}

int main(void) {
    check_expansion();
    square(1, 1, 64, 0, true);          /* RGB565, twiddled, GBIX. */
    square(1, 1, 256, 0, false);
    square(0, 2, 32, 2u * 342u, false);  /* ARGB1555 mipmaps, one padding texel. */
    square(0, 2, 32, 2u * 344u, true);   /* Hardware-style three-texel padding. */
    square(2, 18, 64, 2u * 1366u, false);/* ARGB4444, alternate mipmap code. */
    rectangle(64, 16, false);
    rectangle(16, 64, false);
    rectangle(40, 16, true);
    vq(3, 64, 256, false);
    vq(4, 32, 256, true);
    vq(16, 32, 32, false);               /* Small VQ: indices past 32 read as zero. */
    vq(17, 64, 256, true);
    vq(16, 16, 16, false);
    alpha_rules();
    rejects();
    puts("PASS pvr texture decoder");
    return 0;
}
