/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_cover.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t rgb565(unsigned r, unsigned g, unsigned b) {
    return (uint16_t)((r * 31u + 127u) / 255u << 11 | (g * 63u + 127u) / 255u << 5 | (b * 31u + 127u) / 255u);
}
static struct kui_cover_record sample(void) {
    struct kui_cover_record r;
    memset(&r, 0, sizeof(r));
    strcpy(r.gdi_path, "/Games/Dead or Alive 2/disc.gdi");
    strcpy(r.title, "DEAD OR ALIVE 2");
    strcpy(r.product, "T3601N");
    strcpy(r.version, "V1.100");
    strcpy(r.region, "U");
    r.gdi = (struct kui_cover_stamp){1185765648ull, 0x5b3a, 0x7c21};
    r.user = (struct kui_cover_stamp){40213u, 0x5b3b, 0x0101};
    r.source = KUI_COVER_SOURCE_USER;
    return r;
}
static void put32(uint8_t *p, uint32_t v) { for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
/* Re-seal an edited header so only the edited rule can reject it. */
static bool decodes_after(uint8_t *h, size_t at, uint8_t value) {
    h[at] = value;
    uint32_t crc = 0xffffffffu;
    for(size_t i = 0; i < 508; ++i) {
        crc ^= h[i];
        for(unsigned k = 0; k < 8; ++k) crc = crc & 1u ? crc >> 1 ^ 0xedb88320u : crc >> 1;
    }
    put32(h + 508, ~crc);
    struct kui_cover_record out;
    return kui_cover_header_decode(&out, h);
}
static void headers(void) {
    assert(kui_cover_edge(KUI_COVER_SIZE_LARGE) == 160 && kui_cover_edge(KUI_COVER_SIZE_MEDIUM) == 104 &&
        kui_cover_edge(KUI_COVER_SIZE_SMALL) == 56);
    assert(kui_cover_offset(KUI_COVER_SIZE_LARGE) == 512);
    assert(kui_cover_offset(KUI_COVER_SIZE_MEDIUM) == 512 + 51200);
    assert(kui_cover_offset(KUI_COVER_SIZE_SMALL) == 512 + 51200 + 21632);
    assert(KUI_COVER_FILE_BYTES == 512 + 51200 + 21632 + 6272);
    struct kui_cover_record r = sample(), back;
    uint8_t h[KUI_COVER_HEADER_BYTES], copy[KUI_COVER_HEADER_BYTES];
    assert(kui_cover_header_encode(h, &r));
    assert(!memcmp(h, "KUICOVER", 8));
    assert(kui_cover_header_decode(&back, h));
    assert(!strcmp(back.gdi_path, r.gdi_path) && !strcmp(back.title, r.title) &&
        !strcmp(back.product, r.product) && !strcmp(back.version, r.version) &&
        !strcmp(back.region, r.region) && back.source == r.source &&
        back.gdi.bytes == r.gdi.bytes && back.gdi.date == r.gdi.date && back.gdi.time == r.gdi.time &&
        back.user.bytes == r.user.bytes && back.user.date == r.user.date && back.user.time == r.user.time);
    /* Any single changed byte fails the CRC or a structural rule. */
    for(size_t i = 0; i < sizeof(h); ++i) {
        memcpy(copy, h, sizeof(h));
        copy[i] ^= 0x41u;
        assert(!kui_cover_header_decode(&back, copy));
        assert(back.gdi_path[0] == 0 && back.source == KUI_COVER_SOURCE_NONE);
    }
    /* Structural rules hold even under a matching CRC. */
    memcpy(copy, h, sizeof(h)); assert(decodes_after(copy, 64, 'D'));   /* Same byte: still valid. */
    memcpy(copy, h, sizeof(h)); assert(!decodes_after(copy, 8, 2));     /* Version. */
    memcpy(copy, h, sizeof(h)); assert(!decodes_after(copy, 12, 3));    /* Source. */
    memcpy(copy, h, sizeof(h)); assert(!decodes_after(copy, 18, 112));  /* Edge. */
    memcpy(copy, h, sizeof(h)); assert(!decodes_after(copy, 50, 1));    /* Reserved. */
    memcpy(copy, h, sizeof(h)); assert(!decodes_after(copy, 64 + 20, 'x')); /* Text after NUL. */
    memcpy(copy, h, sizeof(h)); assert(!decodes_after(copy, 64 + 3, 7));    /* Control character. */
    memcpy(copy, h, sizeof(h)); assert(!decodes_after(copy, 232, 'G'));     /* Path not rooted. */
    memcpy(copy, h, sizeof(h)); assert(!decodes_after(copy, 500, 1));
    memcpy(copy, h, sizeof(h)); memset(copy + 64, 'A', 129); assert(!decodes_after(copy, 64, 'A'));
    memcpy(copy, h, sizeof(h)); memset(copy + 36, 0, 12); assert(!decodes_after(copy, 36, 0)); /* User art, no stamp. */
    /* Records that cannot be stored are refused. */
    r = sample(); r.title[3] = '\t'; assert(!kui_cover_header_encode(h, &r));
    r = sample(); r.gdi_path[0] = 'G'; assert(!kui_cover_header_encode(h, &r));
    r = sample(); memset(r.product, 'P', sizeof(r.product)); assert(!kui_cover_header_encode(h, &r));
    r = sample(); r.user.bytes = 0; assert(!kui_cover_header_encode(h, &r)); /* User art needs its stamp. */
    r = sample(); r.source = KUI_COVER_SOURCE_NONE; /* An unusable image of the owner's is remembered. */
    assert(kui_cover_header_encode(h, &r) && kui_cover_header_decode(&back, h) && back.user.bytes == 40213u);
    r.user.bytes = 0; assert(kui_cover_header_encode(h, &r) && kui_cover_header_decode(&back, h));
    assert(back.source == KUI_COVER_SOURCE_NONE && !strcmp(back.title, "DEAD OR ALIVE 2"));
    assert(!kui_cover_header_encode(NULL, &r) && !kui_cover_header_encode(h, NULL));
    assert(!kui_cover_header_decode(&back, NULL) && !kui_cover_header_decode(NULL, h));
}
static void names(void) {
    char key[KUI_COVER_PATH_CAP], text[KUI_COVER_TITLE_CAP];
    assert(kui_cover_key("Dead or Alive 2", key) && !strcmp(key, "Dead or Alive 2"));
    assert(kui_cover_key("doa2.gdi", key) && !strcmp(key, "doa2"));
    assert(kui_cover_key("DOA2.GDI", key) && !strcmp(key, "DOA2"));
    assert(kui_cover_key("Grandia II (Disc 1).gdi", key) && !strcmp(key, "Grandia II (Disc 1)"));
    assert(!kui_cover_key(".gdi", key) && key[0] == 0);
    assert(!kui_cover_key("a:b", key) && !kui_cover_key("con", key) && !kui_cover_key(NULL, key));
    assert(!kui_cover_key("trailing.", key) && !kui_cover_key("", key));
    char name[200];
    memset(name, 'n', sizeof(name));
    name[122] = 0; assert(kui_cover_key(name, key) && strlen(key) == 122);
    name[122] = 'n'; name[123] = 0; assert(!kui_cover_key(name, key));
    kui_cover_display_title("  DEAD   OR ALIVE  2  ", "doa2.gdi", text);
    assert(!strcmp(text, "DEAD OR ALIVE 2"));
    kui_cover_display_title("", "doa2.gdi", text); assert(!strcmp(text, "doa2"));
    kui_cover_display_title(NULL, "Folder Name", text); assert(!strcmp(text, "Folder Name"));
    kui_cover_display_title("????????", "Sakura Taisen", text); assert(!strcmp(text, "Sakura Taisen"));
    kui_cover_display_title("WHO WANTS TO BE A MILLIONAIRE?", "x", text);
    assert(!strcmp(text, "WHO WANTS TO BE A MILLIONAIRE?"));
    char longest[200];
    memset(longest, 'T', sizeof(longest)); longest[199] = 0;
    kui_cover_display_title(longest, "x", text);
    assert(strlen(text) == KUI_COVER_TITLE_CAP - 1);
}
static uint8_t *image(unsigned w, unsigned h, unsigned ch, unsigned r, unsigned g, unsigned b, unsigned a) {
    uint8_t *px = malloc((size_t)w * h * ch);
    for(size_t i = 0; i < (size_t)w * h; ++i) {
        px[i * ch] = (uint8_t)r; px[i * ch + 1] = (uint8_t)g; px[i * ch + 2] = (uint8_t)b;
        if(ch == 4) px[i * ch + 3] = (uint8_t)a;
    }
    return px;
}
static void scaling(void) {
    static uint16_t out[KUI_COVER_PIXELS];
    const uint16_t navy = KUI_COVER_BACKGROUND;
    assert(rgb565(8, 12, 33) == navy);
    /* A flat square stays flat at every size. */
    uint8_t *px = image(256, 256, 3, 200, 40, 90, 255);
    for(unsigned s = 0; s < 3; ++s) {
        unsigned edge = kui_cover_edge((enum kui_cover_size)s);
        assert(kui_cover_scale(px, 256, 256, 3, out, edge));
        for(unsigned i = 0; i < edge * edge; ++i) assert(out[i] == rgb565(200, 40, 90));
    }
    free(px);
    /* Wide and tall art is centred on the background. */
    px = image(200, 100, 3, 255, 0, 0, 255);
    assert(kui_cover_scale(px, 200, 100, 3, out, 160));
    for(unsigned y = 0; y < 160; ++y) for(unsigned x = 0; x < 160; ++x)
        assert(out[y * 160 + x] == (y >= 40 && y < 120 ? rgb565(255, 0, 0) : navy));
    free(px);
    px = image(100, 300, 3, 0, 255, 0, 255);
    assert(kui_cover_scale(px, 100, 300, 3, out, 160));
    for(unsigned y = 0; y < 160; ++y) for(unsigned x = 0; x < 160; ++x)
        assert(out[y * 160 + x] == (x >= 53 && x < 106 ? rgb565(0, 255, 0) : navy));
    free(px);
    /* 2:1 is an exact average; a checkerboard becomes mid grey. */
    px = image(4, 4, 3, 0, 0, 0, 255);
    for(unsigned i = 0; i < 16; ++i) if(((i % 4) + (i / 4)) % 2) memset(px + i * 3, 255, 3);
    assert(kui_cover_scale(px, 4, 4, 3, out, 2));
    for(unsigned i = 0; i < 4; ++i) assert(out[i] == rgb565(128, 128, 128));
    free(px);
    /* Transparency shows the background; half alpha blends toward it. */
    px = image(8, 8, 4, 255, 255, 255, 0);
    assert(kui_cover_scale(px, 8, 8, 4, out, 8));
    for(unsigned i = 0; i < 64; ++i) assert(out[i] == navy);
    free(px);
    px = image(8, 8, 4, 255, 255, 255, 128);
    assert(kui_cover_scale(px, 8, 8, 4, out, 4));
    unsigned r = (255u * 128u + 8u * 127u + 127u) / 255u, g = (255u * 128u + 12u * 127u + 127u) / 255u,
        b = (255u * 128u + 33u * 127u + 127u) / 255u;
    for(unsigned i = 0; i < 16; ++i) assert(out[i] == rgb565(r, g, b));
    free(px);
    /* Enlarging interpolates between pixel centres and keeps the ends. */
    px = image(2, 2, 3, 0, 0, 0, 255);
    memset(px + 3, 255, 3); memset(px + 9, 255, 3);
    assert(kui_cover_scale(px, 2, 2, 3, out, 4));
    const double want[4] = {0.0, 63.75, 191.25, 255.0};
    for(unsigned y = 0; y < 4; ++y) for(unsigned x = 0; x < 4; ++x) {
        unsigned v = (unsigned)floor(want[x] + 0.5);
        assert(out[y * 4 + x] == rgb565(v, v, v));
    }
    free(px);
    assert(!kui_cover_scale(NULL, 4, 4, 3, out, 4));
    px = image(4, 4, 3, 1, 2, 3, 255);
    assert(!kui_cover_scale(px, 0, 4, 3, out, 4) && !kui_cover_scale(px, 4, 4, 2, out, 4) &&
        !kui_cover_scale(px, 4, 4, 3, out, 0) && !kui_cover_scale(px, 4, 4, 3, out, 161) &&
        !kui_cover_scale(px, 4, 4, 3, NULL, 4));
    free(px);
}
static void reduction(void) {
    /* Constant 2x2 blocks survive halving exactly. */
    unsigned w = 1280, h = 960;
    uint8_t *px = malloc((size_t)w * h * 3);
    for(unsigned y = 0; y < h; ++y) for(unsigned x = 0; x < w; ++x) {
        uint8_t *p = px + ((size_t)y * w + x) * 3;
        p[0] = (uint8_t)(x / 4u); p[1] = (uint8_t)(y / 4u); p[2] = (uint8_t)((x / 4u + y / 4u) & 255u);
    }
    static uint16_t direct[KUI_COVER_PIXELS], reduced[KUI_COVER_PIXELS];
    assert(kui_cover_scale(px, w, h, 3, direct, 160));
    kui_cover_reduce(px, &w, &h, 3);
    assert(w == 320 && h == 240);
    for(unsigned y = 0; y < h; ++y) for(unsigned x = 0; x < w; ++x) {
        const uint8_t *p = px + ((size_t)y * w + x) * 3;
        assert(p[0] == (uint8_t)x && p[1] == (uint8_t)y && p[2] == ((x + y) & 255u));
    }
    assert(kui_cover_scale(px, w, h, 3, reduced, 160));
    assert(!memcmp(direct, reduced, sizeof(direct)));
    free(px);
    /* Art already near cover size is left alone. */
    w = 256; h = 256;
    px = image(w, h, 4, 1, 2, 3, 4);
    kui_cover_reduce(px, &w, &h, 4);
    assert(w == 256 && h == 256);
    free(px);
    w = 4000; h = 10;
    px = image(w, h, 3, 9, 9, 9, 255);
    kui_cover_reduce(px, &w, &h, 3);
    assert(w == 1000 && h == 2);
    free(px);
}

int main(void) {
    headers();
    names();
    scaling();
    reduction();
    puts("PASS game cover records and scaling");
    return 0;
}
