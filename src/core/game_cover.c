/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_cover.h"
#include "kui/core.h"
#include "kui/destination.h"

#include <string.h>

/* Header layout, little-endian: magic, version, source, the three edges,
 * both stamps, reserved zeros, then NUL-padded strings and a CRC32 (same
 * convention as kui_crc32) over bytes 0..507. */
#define MAGIC "KUICOVER"
#define VERSION 1u
enum { AT_SOURCE = 12, AT_EDGES = 16, AT_GDI = 24, AT_USER = 36, AT_TITLE = 64,
    AT_PRODUCT = 196, AT_VERSION = 208, AT_REGION = 216, AT_PATH = 232, AT_CRC = 508 };

unsigned kui_cover_edge(enum kui_cover_size size) {
    return size == KUI_COVER_SIZE_LARGE ? KUI_COVER_LARGE :
        size == KUI_COVER_SIZE_MEDIUM ? KUI_COVER_MEDIUM : KUI_COVER_SMALL;
}
size_t kui_cover_offset(enum kui_cover_size size) {
    size_t at = KUI_COVER_HEADER_BYTES;
    if(size == KUI_COVER_SIZE_LARGE) return at;
    at += 2u * KUI_COVER_LARGE * KUI_COVER_LARGE;
    if(size == KUI_COVER_SIZE_MEDIUM) return at;
    return at + 2u * KUI_COVER_MEDIUM * KUI_COVER_MEDIUM;
}

static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8u * i)); }
static void put64(uint8_t *p, uint64_t v) { for(unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(v >> (8u * i)); }
static unsigned get16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32; }
static bool printable(const char *s, size_t cap) {
    const char *end = memchr(s, 0, cap);
    if(!end) return false;
    for(; s < end; ++s) if((unsigned char)*s < 32u || (unsigned char)*s > 126u) return false;
    return true;
}
/* A field holds its text, then zeros to its end; nothing else is accepted. */
static bool field_read(char *out, const uint8_t *in, size_t cap) {
    const uint8_t *end = memchr(in, 0, cap);
    if(!end) return false;
    for(const uint8_t *p = end; p < in + cap; ++p) if(*p) return false;
    memcpy(out, in, cap);
    return printable(out, cap);
}
static void stamp_write(uint8_t *p, const struct kui_cover_stamp *s) {
    put64(p, s->bytes); put16(p + 8, s->date); put16(p + 10, s->time);
}
static void stamp_read(const uint8_t *p, struct kui_cover_stamp *s) {
    s->bytes = get64(p); s->date = (uint16_t)get16(p + 8); s->time = (uint16_t)get16(p + 10);
}
static size_t bounded(const char *s, size_t cap) {
    const char *end = memchr(s, 0, cap);
    return end ? (size_t)(end - s) : cap;
}
static bool zero(const uint8_t *p, size_t n) {
    for(size_t i = 0; i < n; ++i) if(p[i]) return false;
    return true;
}
static bool record_valid(const struct kui_cover_record *r) {
    return r && r->source <= KUI_COVER_SOURCE_USER &&
        printable(r->gdi_path, sizeof(r->gdi_path)) && r->gdi_path[0] == '/' &&
        printable(r->title, sizeof(r->title)) && printable(r->product, sizeof(r->product)) &&
        printable(r->version, sizeof(r->version)) && printable(r->region, sizeof(r->region)) &&
        (r->source != KUI_COVER_SOURCE_USER || r->user.bytes != 0);
}

bool kui_cover_header_encode(uint8_t out[KUI_COVER_HEADER_BYTES], const struct kui_cover_record *r) {
    if(!out || !record_valid(r)) return false;
    memset(out, 0, KUI_COVER_HEADER_BYTES);
    memcpy(out, MAGIC, 8);
    put32(out + 8, VERSION);
    put32(out + AT_SOURCE, (uint32_t)r->source);
    put16(out + AT_EDGES, KUI_COVER_LARGE); put16(out + AT_EDGES + 2, KUI_COVER_MEDIUM);
    put16(out + AT_EDGES + 4, KUI_COVER_SMALL);
    stamp_write(out + AT_GDI, &r->gdi); stamp_write(out + AT_USER, &r->user);
    memcpy(out + AT_TITLE, r->title, strlen(r->title));
    memcpy(out + AT_PRODUCT, r->product, strlen(r->product));
    memcpy(out + AT_VERSION, r->version, strlen(r->version));
    memcpy(out + AT_REGION, r->region, strlen(r->region));
    memcpy(out + AT_PATH, r->gdi_path, strlen(r->gdi_path));
    put32(out + AT_CRC, kui_crc32(0, out, AT_CRC));
    return true;
}
bool kui_cover_header_decode(struct kui_cover_record *r, const uint8_t in[KUI_COVER_HEADER_BYTES]) {
    if(!r) return false;
    memset(r, 0, sizeof(*r));
    struct kui_cover_record d;
    memset(&d, 0, sizeof(d));
    if(!in || memcmp(in, MAGIC, 8) || get32(in + 8) != VERSION ||
       get32(in + AT_CRC) != kui_crc32(0, in, AT_CRC) || get32(in + AT_SOURCE) > KUI_COVER_SOURCE_USER ||
       get16(in + AT_EDGES) != KUI_COVER_LARGE || get16(in + AT_EDGES + 2) != KUI_COVER_MEDIUM ||
       get16(in + AT_EDGES + 4) != KUI_COVER_SMALL || !zero(in + AT_EDGES + 6, 2) ||
       !zero(in + 48, AT_TITLE - 48) || !zero(in + AT_TITLE + KUI_COVER_TITLE_CAP, AT_PRODUCT - AT_TITLE - KUI_COVER_TITLE_CAP) ||
       !zero(in + AT_PRODUCT + 11, AT_VERSION - AT_PRODUCT - 11) ||
       !zero(in + AT_VERSION + 7, AT_REGION - AT_VERSION - 7) ||
       !zero(in + AT_REGION + 9, AT_PATH - AT_REGION - 9) ||
       !zero(in + AT_PATH + KUI_COVER_PATH_CAP, AT_CRC - AT_PATH - KUI_COVER_PATH_CAP))
        return false;
    d.source = (enum kui_cover_source)get32(in + AT_SOURCE);
    stamp_read(in + AT_GDI, &d.gdi); stamp_read(in + AT_USER, &d.user);
    if(!field_read(d.title, in + AT_TITLE, sizeof(d.title)) ||
       !field_read(d.product, in + AT_PRODUCT, sizeof(d.product)) ||
       !field_read(d.version, in + AT_VERSION, sizeof(d.version)) ||
       !field_read(d.region, in + AT_REGION, sizeof(d.region)) ||
       !field_read(d.gdi_path, in + AT_PATH, sizeof(d.gdi_path)) || !record_valid(&d)) return false;
    *r = d;
    return true;
}

bool kui_cover_key(const char *name, char out[KUI_COVER_PATH_CAP]) {
    if(!out) return false;
    out[0] = 0;
    if(!name) return false;
    size_t n = bounded(name, KUI_COVER_PATH_CAP);
    /* The Games list hides dot names; a key never starts with one. */
    if(n >= KUI_COVER_PATH_CAP || name[0] == '.') return false;
    if(n > 4 && name[n - 4] == '.' && (name[n - 3] | 32) == 'g' && (name[n - 2] | 32) == 'd' &&
       (name[n - 1] | 32) == 'i') n -= 4;
    /* Room for the longest companion name, "<key>.jpeg". */
    char longest[KUI_COVER_PATH_CAP + 5];
    memcpy(longest, name, n);
    memcpy(longest + n, ".jpeg", 6);
    if(!n || !kui_destination_name_valid(longest)) return false;
    memcpy(out, name, n);
    out[n] = 0;
    if(kui_destination_name_valid(out)) return true;
    out[0] = 0;
    return false;
}
void kui_cover_display_title(const char *title, const char *fallback, char out[KUI_COVER_TITLE_CAP]) {
    if(!out) return;
    size_t used = 0;
    bool space = false;
    for(const char *p = title; p && *p && used + 1 < KUI_COVER_TITLE_CAP; ++p) {
        if(*p == ' ') { space = used > 0; continue; }
        if(space && used + 2 < KUI_COVER_TITLE_CAP) out[used++] = ' ';
        space = false;
        out[used++] = *p;
    }
    out[used] = 0;
    /* Titles in Japanese character sets arrive as runs of '?'. */
    if(used && !strstr(out, "??")) return;
    char key[KUI_COVER_PATH_CAP];
    const char *name = fallback && kui_cover_key(fallback, key) ? key : fallback ? fallback : "";
    size_t n = bounded(name, KUI_COVER_TITLE_CAP - 1u);
    memcpy(out, name, n);
    out[n] = 0;
}

/* Premultiplied sums; weights are exact integers, so every result rounds once. */
struct sum { uint64_t r, g, b, a, total; };
static void add(struct sum *s, const uint8_t *px, unsigned channels, uint64_t weight) {
    uint64_t a = channels == 4 ? px[3] : 255u;
    s->r += weight * px[0] * a; s->g += weight * px[1] * a; s->b += weight * px[2] * a;
    s->a += weight * a; s->total += weight;
}
static unsigned blend(uint64_t premultiplied, uint64_t alpha, uint64_t total, unsigned background) {
    uint64_t scale = 255u * total;
    return (unsigned)((premultiplied + background * (scale - alpha) + scale / 2u) / scale);
}
static uint16_t finish(const struct sum *s) {
    unsigned r = blend(s->r, s->a, s->total, 8u), g = blend(s->g, s->a, s->total, 12u),
        b = blend(s->b, s->a, s->total, 33u);
    return (uint16_t)((r * 31u + 127u) / 255u << 11 | (g * 63u + 127u) / 255u << 5 | (b * 31u + 127u) / 255u);
}
static void fit(unsigned w, unsigned h, unsigned edge, unsigned *dw, unsigned *dh) {
    if(w >= h) {
        *dw = edge;
        *dh = (unsigned)(((uint64_t)edge * h + w / 2u) / w);
    } else {
        *dh = edge;
        *dw = (unsigned)(((uint64_t)edge * w + h / 2u) / h);
    }
    if(!*dw) *dw = 1;
    if(!*dh) *dh = 1;
}
/* Source pixel i spans [i*d, (i+1)*d) and destination pixel k spans
 * [k*s, (k+1)*s) in units of 1/(s*d) of the image: overlaps are exact. */
static uint64_t overlap(uint64_t i, uint64_t k, uint64_t s, uint64_t d) {
    uint64_t lo = i * d > k * s ? i * d : k * s, hi = (i + 1u) * d < (k + 1u) * s ? (i + 1u) * d : (k + 1u) * s;
    return hi > lo ? hi - lo : 0;
}
static uint16_t shrink(const uint8_t *px, unsigned w, unsigned h, unsigned ch,
                       unsigned dx, unsigned dy, unsigned dw, unsigned dh) {
    struct sum s = {0};
    unsigned x0 = (unsigned)((uint64_t)dx * w / dw), x1 = (unsigned)(((uint64_t)dx + 1u) * w / dw);
    unsigned y0 = (unsigned)((uint64_t)dy * h / dh), y1 = (unsigned)(((uint64_t)dy + 1u) * h / dh);
    if(x1 < w) ++x1;
    if(y1 < h) ++y1;
    for(unsigned y = y0; y < y1; ++y) {
        uint64_t wy = overlap(y, dy, h, dh);
        if(!wy) continue;
        for(unsigned x = x0; x < x1; ++x) {
            uint64_t wx = overlap(x, dx, w, dw);
            if(wx) add(&s, px + ((size_t)y * w + x) * ch, ch, wx * wy);
        }
    }
    return finish(&s);
}
/* Pixel centres map to pixel centres; edges clamp. 16-bit fractions. */
static void place(unsigned k, unsigned src, unsigned dst, unsigned *i0, unsigned *i1, uint64_t *t) {
    int64_t f = (int64_t)(((2u * (uint64_t)k + 1u) * src << 16) / (2u * dst)) - 32768;
    if(f < 0) f = 0;
    if(f > ((int64_t)src - 1) << 16) f = ((int64_t)src - 1) << 16;
    *i0 = (unsigned)(f >> 16);
    *i1 = *i0 + 1u < src ? *i0 + 1u : *i0;
    *t = (uint64_t)f & 0xffffu;
}
static uint16_t enlarge(const uint8_t *px, unsigned w, unsigned h, unsigned ch,
                        unsigned dx, unsigned dy, unsigned dw, unsigned dh) {
    unsigned x0, x1, y0, y1;
    uint64_t tx, ty;
    place(dx, w, dw, &x0, &x1, &tx);
    place(dy, h, dh, &y0, &y1, &ty);
    struct sum s = {0};
    add(&s, px + ((size_t)y0 * w + x0) * ch, ch, (65536u - tx) * (65536u - ty));
    add(&s, px + ((size_t)y0 * w + x1) * ch, ch, tx * (65536u - ty));
    add(&s, px + ((size_t)y1 * w + x0) * ch, ch, (65536u - tx) * ty);
    add(&s, px + ((size_t)y1 * w + x1) * ch, ch, tx * ty);
    return finish(&s);
}
bool kui_cover_scale(const uint8_t *px, unsigned w, unsigned h, unsigned ch, uint16_t *out, unsigned edge) {
    if(!px || !out || !w || !h || (ch != 3 && ch != 4) || !edge || edge > KUI_COVER_LARGE ||
       w > 8192u || h > 8192u) return false;
    unsigned dw, dh;
    fit(w, h, edge, &dw, &dh);
    for(unsigned i = 0; i < edge * edge; ++i) out[i] = KUI_COVER_BACKGROUND;
    unsigned ox = (edge - dw) / 2u, oy = (edge - dh) / 2u;
    bool smaller = w >= dw && h >= dh;
    for(unsigned y = 0; y < dh; ++y)
        for(unsigned x = 0; x < dw; ++x)
            out[(oy + y) * edge + ox + x] = smaller ? shrink(px, w, h, ch, x, y, dw, dh) :
                enlarge(px, w, h, ch, x, y, dw, dh);
    return true;
}
void kui_cover_reduce(uint8_t *px, unsigned *width, unsigned *height, unsigned ch) {
    if(!px || !width || !height || (ch != 3 && ch != 4)) return;
    unsigned w = *width, h = *height, dw, dh;
    for(;;) {
        fit(w, h, KUI_COVER_LARGE, &dw, &dh);
        if(w < 4u * dw || h < 4u * dh || w < 2u || h < 2u) break;
        unsigned nw = w / 2u, nh = h / 2u;
        /* Each output lies at or before the inputs still to be read. */
        for(unsigned y = 0; y < nh; ++y)
            for(unsigned x = 0; x < nw; ++x) {
                const uint8_t *a = px + ((size_t)2u * y * w + 2u * x) * ch, *b = a + (size_t)w * ch;
                uint8_t *o = px + ((size_t)y * nw + x) * ch;
                for(unsigned c = 0; c < ch; ++c)
                    o[c] = (uint8_t)((a[c] + a[ch + c] + b[c] + b[ch + c] + 2u) / 4u);
            }
        w = nw; h = nh;
    }
    *width = w; *height = h;
}
