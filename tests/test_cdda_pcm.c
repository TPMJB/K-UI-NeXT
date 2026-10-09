/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_pcm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(test) do { ++checks; assert(test); } while(0)
#define SECTORS 4u
#define FRAMES (SECTORS * KUI_CDDA_PCM_FRAMES_PER_SECTOR)
struct fixture {
    uint8_t bytes[SECTORS * 2448u + 1024u];
    uint32_t length;
    unsigned reads, fail_on;
    size_t shortest_read;
};
static struct fixture fixture;
static int16_t left[FRAMES + 2u], right[FRAMES + 2u];

static int16_t expected(uint32_t frame, unsigned channel) {
    static const int16_t edges[] = {-32768, -1, 0, 1, 32767};
    if(frame < 5u) return edges[channel ? 4u - frame : frame];
    uint32_t bits = (frame * (channel ? 541u : 137u) + channel * 18377u) & 65535u;
    return (int16_t)((int32_t)bits - 32768);
}
static void put_sample(uint8_t *out, int16_t value, bool big_endian) {
    uint16_t bits = (uint16_t)value;
    out[big_endian ? 1 : 0] = (uint8_t)bits;
    out[big_endian ? 0 : 1] = (uint8_t)(bits >> 8);
}
static bool read_at(void *ctx, uint32_t offset, uint8_t *out, size_t bytes) {
    struct fixture *f = ctx;
    CHECK(out && !((uintptr_t)out & 31u));
    CHECK(!(offset & 511u) && bytes && bytes <= 512u);
    CHECK(offset <= f->length && bytes <= f->length - offset);
    ++f->reads;
    if(f->reads == f->fail_on) return false;
    if(bytes < f->shortest_read) f->shortest_read = bytes;
    memcpy(out, f->bytes + offset, bytes);
    return true;
}
static struct kui_cdda_pcm_source make_source(uint32_t offset, uint32_t stride,
    bool big_endian, uint32_t suffix) {
    memset(&fixture, 0, sizeof(fixture));
    memset(fixture.bytes, 0xac, sizeof(fixture.bytes));
    fixture.length = offset + stride * SECTORS + suffix;
    fixture.shortest_read = 512u;
    CHECK(fixture.length <= sizeof(fixture.bytes));
    for(uint32_t frame = 0; frame < FRAMES; ++frame) {
        uint32_t at = offset + frame / 588u * stride + frame % 588u * 4u;
        put_sample(fixture.bytes + at, expected(frame, 0), big_endian);
        put_sample(fixture.bytes + at + 2u, expected(frame, 1), big_endian);
    }
    return (struct kui_cdda_pcm_source){fixture.length, offset, stride, SECTORS, big_endian};
}
static void compare(uint32_t first, size_t count) {
    for(size_t i = 0; i < count; ++i) {
        CHECK(left[i + 1u] == expected(first + (uint32_t)i, 0));
        CHECK(right[i + 1u] == expected(first + (uint32_t)i, 1));
    }
    CHECK(left[0] == 913 && right[0] == -913);
    CHECK(left[count + 1u] == 913 && right[count + 1u] == -913);
}
static void guard(void) {
    for(unsigned i = 0; i < FRAMES + 2u; ++i) { left[i] = 913; right[i] = -913; }
}
static void sweep(void) {
    static const uint32_t offsets[] = {0, 1, 3, 507, 509, 511, 513};
    static const size_t capacities[] = {1, 2, 127, 128, 129, 589, SIZE_MAX};
    for(unsigned be = 0; be < 2; ++be)
        for(unsigned raw_sub = 0; raw_sub < 2; ++raw_sub)
            for(unsigned prefix = 0; prefix < sizeof(offsets) / sizeof(offsets[0]); ++prefix)
                for(unsigned cap = 0; cap < sizeof(capacities) / sizeof(capacities[0]); ++cap) {
                    struct kui_cdda_pcm_source source = make_source(offsets[prefix], raw_sub ? 2448u : 2352u,
                        be != 0, cap & 1u ? 17u : 0u);
                    struct kui_cdda_pcm c;
                    CHECK(kui_cdda_pcm_init(&c, &source, read_at, &fixture) == KUI_CDDA_PCM_OK);
                    CHECK(kui_cdda_pcm_total_frames(&c) == FRAMES);
                    CHECK(!fixture.reads && c.position == 0);
                    size_t done = 71;
                    CHECK(kui_cdda_pcm_read_frames(&c, NULL, NULL, 0, &done) == KUI_CDDA_PCM_OK);
                    CHECK(done == 0 && c.position == 0 && !fixture.reads);
                    while(c.position < FRAMES) {
                        guard();
                        uint32_t first = c.position;
                        CHECK(kui_cdda_pcm_read_frames(&c, left + 1, right + 1, capacities[cap], &done) == KUI_CDDA_PCM_OK);
                        CHECK(done && done <= capacities[cap] && done <= FRAMES - first);
                        CHECK(c.position == first + done);
                        compare(first, done);
                    }
                    unsigned reads = fixture.reads;
                    CHECK(kui_cdda_pcm_read_frames(&c, left + 1, right + 1, 128, &done) == KUI_CDDA_PCM_EOF);
                    CHECK(done == 0 && c.position == FRAMES && fixture.reads == reads);
                    CHECK(kui_cdda_pcm_read_frames(&c, NULL, NULL, 0, &done) == KUI_CDDA_PCM_OK);
                    CHECK(done == 0 && fixture.reads == reads);
                    /* PCM ends before the 96-byte subchannel trailer. A final
                     * short cache fill is only required when PCM reaches it. */
                    if(!raw_sub && (fixture.length & 511u) < 496u) CHECK(fixture.shortest_read < 512u);
                }
}
static void seeks_and_errors(void) {
    struct kui_cdda_pcm_source source = make_source(511, 2448, true, 13);
    struct kui_cdda_pcm c;
    CHECK(kui_cdda_pcm_init(&c, &source, read_at, &fixture) == KUI_CDDA_PCM_OK);
    static const uint32_t starts[] = {0, 1, 127, 128, 587, 588, 589, FRAMES - 2u, FRAMES - 1u};
    for(unsigned i = 0; i < sizeof(starts) / sizeof(starts[0]); ++i) {
        size_t done;
        CHECK(kui_cdda_pcm_seek(&c, starts[i]) == KUI_CDDA_PCM_OK);
        guard();
        CHECK(kui_cdda_pcm_read_frames(&c, left + 1, right + 1, 129, &done) == KUI_CDDA_PCM_OK);
        compare(starts[i], done);
        CHECK(c.position == starts[i] + done);
    }
    CHECK(kui_cdda_pcm_seek(&c, FRAMES) == KUI_CDDA_PCM_OK);
    CHECK(kui_cdda_pcm_seek(&c, FRAMES + 1u) == KUI_CDDA_PCM_INVALID && c.position == FRAMES);
    CHECK(kui_cdda_pcm_seek(&c, UINT32_MAX) == KUI_CDDA_PCM_INVALID && c.position == FRAMES);
    CHECK(kui_cdda_pcm_seek(&c, 0) == KUI_CDDA_PCM_OK);
    size_t done = 71;
    CHECK(kui_cdda_pcm_read_frames(&c, NULL, right, 1, &done) == KUI_CDDA_PCM_INVALID);
    CHECK(done == 0 && c.position == 0);
    CHECK(kui_cdda_pcm_read_frames(&c, left, NULL, 1, &done) == KUI_CDDA_PCM_INVALID);
    CHECK(kui_cdda_pcm_read_frames(&c, left, right, 1, NULL) == KUI_CDDA_PCM_INVALID);
    /* A mid-request I/O error commits no decoded frames. Retrying must produce
     * exactly the original request, irrespective of the populated cache. */
    CHECK(kui_cdda_pcm_init(&c, &source, read_at, &fixture) == KUI_CDDA_PCM_OK);
    fixture.reads = 0; fixture.fail_on = 2;
    CHECK(kui_cdda_pcm_read_frames(&c, left + 1, right + 1, 128, &done) == KUI_CDDA_PCM_IO);
    CHECK(done == 0 && c.position == 0 && fixture.reads == 2);
    fixture.fail_on = 0;
    guard();
    CHECK(kui_cdda_pcm_read_frames(&c, left + 1, right + 1, 128, &done) == KUI_CDDA_PCM_OK);
    CHECK(done == 128 && c.position == 128);
    compare(0, done);
    /* Prefetching 512 bytes must not move the frame cursor past the one asked. */
    source = make_source(0, 2352, false, 0);
    CHECK(kui_cdda_pcm_init(&c, &source, read_at, &fixture) == KUI_CDDA_PCM_OK);
    CHECK(kui_cdda_pcm_read_frames(&c, left, right, 1, &done) == KUI_CDDA_PCM_OK);
    CHECK(done == 1 && c.position == 1 && c.cache_bytes == 512 && fixture.reads == 1);
}
static void invalid_sources(void) {
    struct kui_cdda_pcm_source valid = {2352u, 0, 2352u, 1, false};
    struct kui_cdda_pcm_source bad[] = {
        {0, 0, 2352u, 1, false}, {2352u, 2353u, 2352u, 1, false},
        {2352u, 1, 2352u, 1, false}, {2352u, 0, 2352u, 0, false},
        {2352u, 0, 0, 1, false}, {2352u, 0, 2048u, 1, false},
        {2352u, 0, 2336u, 1, false}, {2447u, 0, 2448u, 1, false},
        {UINT32_MAX, 0, 2352u, UINT32_MAX, false},
        {UINT32_MAX, UINT32_MAX - 2351u, 2352u, 1, false},
        {UINT32_MAX, UINT32_MAX, 2352u, 1, false}
    };
    struct kui_cdda_pcm c;
    for(unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK(kui_cdda_pcm_init(&c, &valid, read_at, &fixture) == KUI_CDDA_PCM_OK);
        CHECK(kui_cdda_pcm_init(&c, bad + i, read_at, &fixture) == KUI_CDDA_PCM_INVALID);
        CHECK(!kui_cdda_pcm_total_frames(&c));
        CHECK(kui_cdda_pcm_seek(&c, 0) == KUI_CDDA_PCM_INVALID);
        size_t done = 91;
        CHECK(kui_cdda_pcm_read_frames(&c, left, right, 1, &done) == KUI_CDDA_PCM_INVALID && done == 0);
    }
    CHECK(kui_cdda_pcm_init(NULL, &valid, read_at, NULL) == KUI_CDDA_PCM_INVALID);
    CHECK(kui_cdda_pcm_init(&c, NULL, read_at, NULL) == KUI_CDDA_PCM_INVALID);
    CHECK(kui_cdda_pcm_init(&c, &valid, NULL, NULL) == KUI_CDDA_PCM_INVALID);
    CHECK(kui_cdda_pcm_total_frames(NULL) == 0);
    CHECK(kui_cdda_pcm_seek(NULL, 0) == KUI_CDDA_PCM_INVALID);
    size_t done = 17;
    CHECK(kui_cdda_pcm_read_frames(NULL, left, right, 1, &done) == KUI_CDDA_PCM_INVALID && done == 0);
}
static bool high_read(void *ctx, uint32_t offset, uint8_t *out, size_t bytes) {
    const struct kui_cdda_pcm_source *source = ctx;
    CHECK(!(offset & 511u) && !((uintptr_t)out & 31u));
    CHECK(bytes && bytes <= 512u && offset <= source->file_bytes && bytes <= source->file_bytes - offset);
    for(size_t i = 0; i < bytes; ++i) {
        uint32_t at = offset + (uint32_t)i;
        if(at < source->backing_offset) out[i] = 0xff;
        else {
            uint32_t relative = at - source->backing_offset;
            uint32_t sector_byte = relative % source->sector_stride;
            if(sector_byte >= 2352u) out[i] = 0xac;
            else {
                uint32_t frame = relative / source->sector_stride * 588u + sector_byte / 4u;
                uint16_t value = (uint16_t)expected(frame, sector_byte % 4u >= 2u);
                unsigned shift = source->big_endian ? (1u - (sector_byte & 1u)) * 8u : (sector_byte & 1u) * 8u;
                out[i] = (uint8_t)(value >> shift);
            }
        }
    }
    return true;
}
static void integer_limits(void) {
    for(unsigned sub = 0; sub < 2; ++sub) {
        uint32_t stride = sub ? 2448u : 2352u;
        struct kui_cdda_pcm_source source = {UINT32_MAX, UINT32_MAX - stride, stride, 1, sub != 0};
        struct kui_cdda_pcm c;
        CHECK(kui_cdda_pcm_init(&c, &source, high_read, &source) == KUI_CDDA_PCM_OK);
        guard();
        size_t done;
        CHECK(kui_cdda_pcm_read_frames(&c, left + 1, right + 1, SIZE_MAX, &done) == KUI_CDDA_PCM_OK);
        CHECK(done == 588u && c.position == 588u);
        compare(0, done);
        CHECK(kui_cdda_pcm_read_frames(&c, left, right, 1, &done) == KUI_CDDA_PCM_EOF);
        source.backing_offset = 0;
        source.sectors = UINT32_MAX / stride;
        CHECK(kui_cdda_pcm_init(&c, &source, high_read, &source) == KUI_CDDA_PCM_OK);
        uint32_t last = kui_cdda_pcm_total_frames(&c) - 1u;
        CHECK(kui_cdda_pcm_seek(&c, last) == KUI_CDDA_PCM_OK);
        CHECK(kui_cdda_pcm_read_frames(&c, left, right, 1, &done) == KUI_CDDA_PCM_OK);
        CHECK(done == 1 && left[0] == expected(last, 0) && right[0] == expected(last, 1));
    }
}
int main(void) {
    sweep(); seeks_and_errors(); invalid_sources(); integer_limits();
    printf("PASS CDDA PCM: %u checks; byte order, planar signed output, offsets/cache/sector boundaries, subchannels, seeks, transactional I/O, EOF and uint32 bounds\n", checks);
    return 0;
}
