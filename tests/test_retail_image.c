/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_image.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(struct kui_retail_image) <= 560, "one physical block cache only");
static struct kui_retail_manifest manifest, decoded, backup, empty;
static struct kui_retail_image image;
static uint8_t card[2048u * 512u], wire[KUI_RETAIL_IMAGE_WIRE_BYTES];
static uint8_t clean_wire[KUI_RETAIL_IMAGE_WIRE_BYTES], output[64u * 2352u + 1];
static uint32_t calls, fail_call;
static uint32_t edge_source, run_calls;
static struct { uint32_t lba, available; } run_trace[512];
static unsigned checks;
#define CHECK(test) do { ++checks; assert(test); } while(0)

static uint8_t source(uint32_t track, uint32_t file_byte) {
    uint32_t sector = file_byte / 2352u, inside = file_byte % 2352u;
    if(track != 1) {
        if(inside == 0 || inside == 11) return 0;
        if(inside < 11) return 255;
        if(inside == 15) return 1;
    }
    return (uint8_t)(track * 91u + sector * 53u + inside * 11u + (inside >> 8));
}
static int read_block(void *context, uint32_t lba, uint8_t out[512]) {
    CHECK(context == card);
    CHECK(lba >= manifest.partition_start && lba < manifest.partition_end);
    bool allocated = false;
    for(uint32_t i = 0; i < manifest.extent_count; ++i)
        if(lba >= manifest.extents[i].card_lba &&
           lba - manifest.extents[i].card_lba < manifest.extents[i].blocks)
            allocated = true;
    CHECK(allocated);
    ++calls;
    if(fail_call && calls == fail_call) { memset(out, 0xdd, 512); return -1; }
    if(lba == UINT32_MAX) { CHECK(edge_source != 0); lba = edge_source; }
    CHECK(lba < 2048);
    memcpy(out, card + lba * 512u, 512);
    return 0;
}
static int read_run(void *context, uint32_t lba, uint32_t available, uint8_t out[512]) {
    CHECK(available != 0 && run_calls < sizeof(run_trace) / sizeof(run_trace[0]));
    CHECK((uint64_t)lba + available <= manifest.card_sectors);
    CHECK(lba >= manifest.partition_start && (uint64_t)lba + available <= manifest.partition_end);
    bool bounded = false;
    for(uint32_t i = 0; i < manifest.extent_count; ++i) {
        const struct kui_retail_extent *e = &manifest.extents[i];
        if(lba >= e->card_lba && lba - e->card_lba < e->blocks) {
            CHECK(available <= e->blocks - (lba - e->card_lba));
            bounded = true;
        }
    }
    CHECK(bounded);
    run_trace[run_calls].lba = lba;
    run_trace[run_calls++].available = available;
    return read_block(context, lba, out);
}
static void fixture(bool fragmented) {
    memset(&manifest, 0, sizeof(manifest));
    memset(card, 0xf3, sizeof(card));
    manifest.card_sectors = 2048;
    manifest.partition_start = 50; manifest.partition_end = 2000;
    manifest.track_count = 4;
    manifest.session_lba = 45000; manifest.boot_lba = 45001; manifest.boot_bytes = 4567;
    manifest.gdi_crc32 = 0x89abcdef;
    manifest.boot_crc32 = 0x10293847; manifest.ip_crc32 = 0x67584930;
    strcpy(manifest.title, "Original retail image test");
    strcpy(manifest.product, "KUITEST"); strcpy(manifest.region, "JUE");
    strcpy(manifest.bootfile, "1ST_READ.BIN");
    static const uint32_t starts[4] = {0, 3, 45000, 45070};
    static const uint32_t ends[4] = {3, 5, 45070, 45072};
    uint32_t physical_index = 0;
    for(unsigned i = 0; i < 4; ++i) {
        struct kui_retail_track *t = &manifest.tracks[i];
        *t = (struct kui_retail_track){i + 1, starts[i], ends[i], i == 1 ? 0u : 4u,
                                      manifest.extent_count, 0};
        uint32_t bytes = (t->end_lba - t->start_lba) * 2352u;
        uint32_t blocks = (bytes + 511u) / 512u;
        for(uint32_t n = 0; n < blocks;) {
            uint32_t take = fragmented && blocks - n > 3 ? 3 : blocks - n;
            uint32_t physical = fragmented ? 100u + physical_index * 3u : 100u + physical_index;
            CHECK(manifest.extent_count < KUI_RETAIL_IMAGE_EXTENTS);
            manifest.extents[manifest.extent_count++] =
                (struct kui_retail_extent){n, physical, take};
            ++t->extent_count;
            for(uint32_t p = 0; p < take * 512u; ++p)
                if(n * 512u + p < bytes) card[physical * 512u + p] = source(i, n * 512u + p);
            n += take; physical_index += take;
        }
    }
    CHECK(kui_retail_manifest_validate(&manifest) == KUI_GAME_OK);
    calls = fail_call = edge_source = run_calls = 0;
    CHECK(kui_retail_image_init(&image, &manifest, read_block, card) == KUI_GAME_OK);
}
static void put32(uint8_t *p, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8u * i));
}
static void refresh_crc(void) {
    memset(wire + 16, 0, 4);
    put32(wire + 16, kui_retail_crc32(0, wire, sizeof(wire)));
}
static void wire_tests(void) {
    fixture(true);
    CHECK(kui_retail_crc32(0, "123456789", 9) == 0xcbf43926);
    CHECK(kui_retail_crc32(kui_retail_crc32(0, "1234", 4), "56789", 5) == 0xcbf43926);
    CHECK(kui_retail_crc32(0xabcdef01, NULL, 0) == 0xabcdef01);
    CHECK(kui_retail_manifest_encode(&manifest, wire) == KUI_GAME_OK);
    CHECK(!memcmp(wire, "KUIRTI01", 8));
    CHECK(wire[8] == 1 && wire[12] == 0 && wire[13] == 16 && wire[14] == 0);
    CHECK(wire[20] == 4 && wire[32] == 0 && wire[33] == 8 && wire[68] == 0xef);
    CHECK(wire[256] == 0x47 && wire[259] == 0x10 && wire[260] == 0x30 && wire[263] == 0x67);
    CHECK(kui_retail_manifest_decode(wire, &decoded) == KUI_GAME_OK);
    CHECK(!memcmp(&decoded, &manifest, sizeof(manifest)));
    memcpy(clean_wire, wire, sizeof(wire));
    /* Every byte is covered by the CRC, including unused space. */
    for(unsigned i = 0; i < sizeof(wire); ++i) {
        wire[i] ^= 1;
        CHECK(kui_retail_manifest_decode(wire, &decoded) != KUI_GAME_OK);
        CHECK(!memcmp(&decoded, &empty, sizeof(decoded)));
        wire[i] ^= 1;
    }
    /* Valid CRC cannot bless noncanonical fields, unused entries or text tails. */
    const unsigned reserved[] = {28, 31, 264, 319, 344, 351, 320 + 4 * 32,
        832 + manifest.extent_count * 12, 2368, 4095,
        72 + sizeof("Original retail image test"), 200 + sizeof("KUITEST"),
        216 + sizeof("1ST_READ.BIN"), 240 + sizeof("JUE")};
    for(unsigned i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i) {
        wire[reserved[i]] = 1; refresh_crc();
        CHECK(kui_retail_manifest_decode(wire, &decoded) == KUI_GAME_INVALID);
        CHECK(!memcmp(&decoded, &empty, sizeof(decoded)));
        memcpy(wire, clean_wire, sizeof(wire));
    }
    const unsigned fields[] = {8, 12, 20, 24};
    const uint32_t values[] = {2, 4097, 17, 129};
    for(unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        put32(wire + fields[i], values[i]); refresh_crc();
        CHECK(kui_retail_manifest_decode(wire, &decoded) == KUI_GAME_INVALID);
        CHECK(!memcmp(&decoded, &empty, sizeof(decoded)));
        memcpy(wire, clean_wire, sizeof(wire));
    }
    put32(wire + 832 + 4, 0); refresh_crc();
    CHECK(kui_retail_manifest_decode(wire, &decoded) == KUI_GAME_RANGE);
    CHECK(!memcmp(&decoded, &empty, sizeof(decoded)));
    memcpy(wire, clean_wire, sizeof(wire));
    manifest.extent_count = 129;
    CHECK(kui_retail_manifest_encode(&manifest, wire) == KUI_GAME_INVALID);
    CHECK(!memcmp(wire, clean_wire, sizeof(wire)));
    CHECK(kui_retail_manifest_encode(NULL, wire) == KUI_GAME_INVALID);
    CHECK(!memcmp(wire, clean_wire, sizeof(wire)));
    CHECK(kui_retail_manifest_encode(&manifest, NULL) == KUI_GAME_INVALID);
    CHECK(kui_retail_manifest_decode(NULL, &decoded) == KUI_GAME_INVALID);
    CHECK(!memcmp(&decoded, &empty, sizeof(decoded)));
    CHECK(kui_retail_manifest_decode(wire, NULL) == KUI_GAME_INVALID);
}
static void invalid_map_tests(void) {
    fixture(true); backup = manifest;
#define BAD(field, value) do { manifest = backup; manifest.field = (value); \
    CHECK(kui_retail_manifest_validate(&manifest) != KUI_GAME_OK); } while(0)
    BAD(track_count, 0); BAD(track_count, 17); BAD(extent_count, 0);
    BAD(extent_count, 129); BAD(card_sectors, 0);
    BAD(card_sectors, UINT64_C(0x100000001)); BAD(partition_start, 2000);
    BAD(partition_start, UINT64_MAX); BAD(partition_end, 2049); BAD(partition_end, 101);
    BAD(tracks[0].number, 2); BAD(tracks[1].start_lba, 2);
    BAD(tracks[1].control, 1); BAD(tracks[0].end_lba, 0);
    BAD(tracks[3].end_lba, KUI_GAME_LBA_LIMIT + 1);
    BAD(tracks[3].end_lba, UINT32_MAX);
    BAD(tracks[2].first_extent, 0); BAD(tracks[2].first_extent, UINT32_MAX);
    BAD(tracks[2].extent_count, UINT32_MAX); BAD(tracks[3].extent_count, 0);
    BAD(extents[0].file_block, 1); BAD(extents[0].blocks, 0);
    BAD(extents[0].blocks, UINT32_MAX); BAD(extents[0].card_lba, 49);
    BAD(extents[0].card_lba, UINT32_MAX); BAD(extents[1].file_block, 0);
    BAD(extents[1].card_lba, 100); BAD(extents[1].card_lba, 102);
    BAD(extents[30].card_lba, manifest.extents[0].card_lba);
    BAD(extents[manifest.extent_count - 1].blocks, 2);
    BAD(session_lba, 45001); BAD(session_lba, 0);
    BAD(boot_lba, 4); BAD(boot_lba, 45072); BAD(boot_lba, UINT32_MAX);
    BAD(boot_bytes, 0); BAD(boot_bytes, KUI_RETAIL_IMAGE_BOOT_MAX + 1);
    BAD(boot_bytes, UINT32_MAX);
    BAD(title[0], 0); BAD(title[2], '\n'); BAD(bootfile[0], 0);
    manifest = backup; memset(manifest.title, 'a', sizeof(manifest.title));
    CHECK(kui_retail_manifest_validate(&manifest) == KUI_GAME_INVALID);
    /* Zero CRCs are legal values; they are protected by the wire CRC. */
    manifest = backup; manifest.boot_crc32 = manifest.ip_crc32 = manifest.gdi_crc32 = 0;
    CHECK(kui_retail_manifest_validate(&manifest) == KUI_GAME_OK);
    manifest = backup;
    /* Maximum physical block is representable; exclusive end requires u64. */
    manifest.card_sectors = manifest.partition_end = UINT64_C(0x100000000);
    manifest.extents[manifest.extent_count - 1].card_lba = UINT32_MAX;
    CHECK(kui_retail_manifest_validate(&manifest) == KUI_GAME_OK);
    manifest.extents[0].card_lba = UINT32_MAX - 2;
    CHECK(kui_retail_manifest_validate(&manifest) == KUI_GAME_OVERLAP);
    manifest.extents[0].card_lba = UINT32_MAX - 1;
    CHECK(kui_retail_manifest_validate(&manifest) == KUI_GAME_RANGE);
#undef BAD
}
static void compare(uint32_t lba, uint32_t count, enum kui_game_sector_format format) {
    memset(output, 0x77, sizeof(output));
    CHECK(kui_retail_image_read(&image, lba, count, format, output, sizeof(output)) == KUI_GAME_OK);
    unsigned stride = format == KUI_GAME_SECTOR_RAW ? 2352u : 2048u;
    for(uint32_t n = 0; n < count; ++n) {
        unsigned t = 0;
        while(manifest.tracks[t].end_lba <= lba + n) ++t;
        uint32_t base = (lba + n - manifest.tracks[t].start_lba) * 2352u;
        if(format == KUI_GAME_SECTOR_MODE1) base += 16;
        for(unsigned j = 0; j < stride; ++j) CHECK(output[n * stride + j] == source(t, base + j));
    }
    CHECK(output[count * stride] == 0x77);
}
static void reader_tests(bool fragmented) {
    fixture(fragmented);
    compare(0, 3, KUI_GAME_SECTOR_RAW);
    compare(0, 3, KUI_GAME_SECTOR_MODE1);
    compare(2, 2, KUI_GAME_SECTOR_RAW); /* Data/audio boundary across two files. */
    compare(3, 2, KUI_GAME_SECTOR_RAW); /* Final block contains nonfile padding. */
    compare(45000, 64, KUI_GAME_SECTOR_MODE1);
    compare(45000, 64, KUI_GAME_SECTOR_RAW);
    compare(45069, 3, KUI_GAME_SECTOR_MODE1);
    compare(45071, 1, KUI_GAME_SECTOR_RAW);
    for(unsigned i = 0; i < 20; ++i) compare(45000 + i * 31 % 70, 1, KUI_GAME_SECTOR_MODE1);
    CHECK(image.blocks_read == calls);
    unsigned before = calls;
    compare(45071, 1, KUI_GAME_SECTOR_RAW);
    CHECK(calls > before); /* A last-block cache cannot satisfy a whole sector. */
    before = calls;
    compare(1, 1, KUI_GAME_SECTOR_MODE1);
    CHECK(calls == before + 5); /* Header+payload share the one-block cache. */
    before = calls;
    compare(1, 1, KUI_GAME_SECTOR_RAW);
    CHECK(calls == before + 6); /* MODE1 omitted the final parity-only block. */
    before = calls;
    memset(output, 0x77, sizeof(output));
    CHECK(kui_retail_image_read(&image, 2, 2, KUI_GAME_SECTOR_MODE1, output, sizeof(output)) == KUI_GAME_AUDIO);
    CHECK(kui_retail_image_read(&image, 4, 2, KUI_GAME_SECTOR_RAW, output, sizeof(output)) == KUI_GAME_GAP);
    CHECK(kui_retail_image_read(&image, 45071, 2, KUI_GAME_SECTOR_RAW, output, sizeof(output)) == KUI_GAME_RANGE);
    CHECK(kui_retail_image_read(&image, UINT32_MAX, 2, KUI_GAME_SECTOR_RAW, output, sizeof(output)) == KUI_GAME_RANGE);
    CHECK(kui_retail_image_read(&image, 45000, 65, KUI_GAME_SECTOR_RAW, output, sizeof(output)) == KUI_GAME_RANGE);
    CHECK(kui_retail_image_read(&image, 45000, 0, KUI_GAME_SECTOR_RAW, output, sizeof(output)) == KUI_GAME_INVALID);
    CHECK(kui_retail_image_read(&image, 45000, 1, (enum kui_game_sector_format)7, output, sizeof(output)) == KUI_GAME_INVALID);
    CHECK(kui_retail_image_read(&image, 45000, 1, KUI_GAME_SECTOR_RAW, output, 2351) == KUI_GAME_RANGE);
    CHECK(kui_retail_image_read(&image, 45000, 1, KUI_GAME_SECTOR_MODE1, output, 2047) == KUI_GAME_RANGE);
    CHECK(kui_retail_image_read(&image, 45000, 1, KUI_GAME_SECTOR_RAW, NULL, sizeof(output)) == KUI_GAME_INVALID);
    CHECK(kui_retail_image_check(NULL, 0, 1, KUI_GAME_SECTOR_RAW) == KUI_GAME_INVALID);
    CHECK(calls == before);
    for(size_t i = 0; i < sizeof(output); ++i) CHECK(output[i] == 0x77);
    /* Header IO failure cannot touch caller output, payload failures invalidate
     * poisoned callback data; both recover on the following fresh command. */
    fail_call = calls + 1;
    CHECK(kui_retail_image_read(&image, 45000, 1, KUI_GAME_SECTOR_MODE1, output, sizeof(output)) == KUI_GAME_IO);
    CHECK(!image.cache_valid);
    for(size_t i = 0; i < sizeof(output); ++i) CHECK(output[i] == 0x77);
    fail_call = calls + 3;
    CHECK(kui_retail_image_read(&image, 45000, 3, KUI_GAME_SECTOR_MODE1, output, sizeof(output)) == KUI_GAME_IO);
    CHECK(!image.cache_valid);
    fail_call = 0; compare(45000, 3, KUI_GAME_SECTOR_MODE1);
    uint32_t physical = manifest.extents[0].card_lba;
    static const uint32_t corrupt[] = {0, 1, 5, 10, 11, 15};
    for(unsigned i = 0; i < sizeof(corrupt) / sizeof(corrupt[0]); ++i) {
        card[physical * 512u + corrupt[i]] ^= 1;
        /* Mutating the fixture starts a new immutable media session. */
        CHECK(kui_retail_image_init(&image, &manifest, read_block, card) == KUI_GAME_OK);
        CHECK(!image.cache_valid);
        memset(output, 0x77, sizeof(output));
        CHECK(kui_retail_image_read(&image, 0, 1, KUI_GAME_SECTOR_MODE1, output, sizeof(output)) == KUI_GAME_MODE);
        CHECK(output[0] == 0x77 && output[2047] == 0x77);
        CHECK(kui_retail_image_read(&image, 0, 1, KUI_GAME_SECTOR_RAW, output, sizeof(output)) == KUI_GAME_OK);
        card[physical * 512u + corrupt[i]] ^= 1;
    }
    CHECK(kui_retail_image_init(&image, &manifest, NULL, card) == KUI_GAME_INVALID);
    CHECK(image.manifest == NULL && image.read_block == NULL);
    CHECK(kui_retail_image_read(&image, 0, 1, KUI_GAME_SECTOR_RAW, output, sizeof(output)) == KUI_GAME_INVALID);
    CHECK(kui_retail_image_init(NULL, &manifest, read_block, card) == KUI_GAME_INVALID);
}
static void sequential_cache_tests(bool fragmented) {
    static const uint32_t chunks[] = {1, 2, 8};
    static const enum kui_game_sector_format formats[] = {
        KUI_GAME_SECTOR_MODE1, KUI_GAME_SECTOR_RAW
    };
    for(unsigned format = 0; format < sizeof(formats) / sizeof(formats[0]); ++format) {
        for(unsigned chunk = 0; chunk < sizeof(chunks) / sizeof(chunks[0]); ++chunk) {
            for(unsigned streaming = 0; streaming < 2; ++streaming) {
                fixture(fragmented);
                CHECK(!image.cache_valid && !image.blocks_read && !calls);
                if(streaming) image.read_run = read_run;
                for(uint32_t done = 0; done < 32; done += chunks[chunk])
                    compare(45000 + done, chunks[chunk], formats[format]);
                /* 32 raw sectors occupy exactly 147 physical blocks. All formats
                 * and chunk sizes must reach each block once, including when the
                 * extents are fragmented. compare checks every returned payload
                 * byte and the destination canary after each separate read call. */
                CHECK(calls == 147 && image.blocks_read == 147);
                CHECK(run_calls == (streaming ? 147u : 0u));
                CHECK(image.cache_valid);
                /* New initialization discards the prior session's cached block. */
                CHECK(kui_retail_image_init(&image, &manifest, read_block, card) == KUI_GAME_OK);
                CHECK(!image.cache_valid && !image.blocks_read && !image.read_run);
            }
        }
    }
}
static void run_span(uint32_t at, uint32_t physical, uint32_t blocks) {
    CHECK(at + blocks <= run_calls);
    for(uint32_t i = 0; i < blocks; ++i) {
        CHECK(run_trace[at + i].lba == physical + i);
        CHECK(run_trace[at + i].available == blocks - i);
    }
}
static void run_span_tests(void) {
    fixture(false); image.read_run = read_run;
    uint32_t physical = manifest.extents[manifest.tracks[2].first_extent].card_lba;
    compare(45000, 2, KUI_GAME_SECTOR_MODE1);
    CHECK(run_calls == 9); run_span(0, physical, 9);
    /* Request scope includes the second sector, never its parity-only tail. */
    run_calls = 0;
    compare(45001, 1, KUI_GAME_SECTOR_MODE1);
    CHECK(run_calls == 5); run_span(0, physical + 4, 5);
    run_calls = 0;
    compare(45001, 1, KUI_GAME_SECTOR_RAW);
    CHECK(run_calls == 6); run_span(0, physical + 4, 6);
    /* Its last block is reused by the next RAW request without a callback. */
    run_calls = 0;
    compare(45002, 2, KUI_GAME_SECTOR_RAW);
    CHECK(run_calls == 9); run_span(0, physical + 10, 9);
    run_calls = 0;
    compare(45000, 8, KUI_GAME_SECTOR_MODE1);
    CHECK(run_calls == 37); run_span(0, physical, 37);
    /* Separate tracks bound runs even when their physical blocks are adjacent.
     * MODE1 also omits a final parity-only block in the first track. */
    run_calls = 0;
    compare(45069, 3, KUI_GAME_SECTOR_MODE1);
    uint32_t following = manifest.extents[manifest.tracks[3].first_extent].card_lba;
    CHECK(run_calls == 14);
    run_span(0, physical + 316, 5); run_span(5, following, 9);
    run_calls = 0;
    compare(45069, 3, KUI_GAME_SECTOR_RAW);
    CHECK(run_calls == 16);
    run_span(0, physical + 316, 6); run_span(6, following, 10);

    fixture(true); image.read_run = read_run;
    uint32_t first = manifest.tracks[2].first_extent;
    compare(45000, 2, KUI_GAME_SECTOR_RAW);
    CHECK(run_calls == 10);
    for(uint32_t i = 0; i < 3; ++i)
        run_span(i * 3, manifest.extents[first + i].card_lba, 3);
    run_span(9, manifest.extents[first + 3].card_lba, 1);

    /* A declared extent boundary still ends a run without a physical gap. */
    fixture(false);
    first = manifest.tracks[2].first_extent;
    struct kui_retail_extent original = manifest.extents[first];
    memmove(&manifest.extents[first + 2], &manifest.extents[first + 1],
        (manifest.extent_count - first - 1) * sizeof(manifest.extents[0]));
    manifest.extents[first].blocks = 4;
    manifest.extents[first + 1] = (struct kui_retail_extent){
        4, original.card_lba + 4, original.blocks - 4};
    ++manifest.extent_count; ++manifest.tracks[2].extent_count;
    ++manifest.tracks[3].first_extent;
    CHECK(kui_retail_image_init(&image, &manifest, read_block, card) == KUI_GAME_OK);
    image.read_run = read_run;
    compare(45000, 2, KUI_GAME_SECTOR_MODE1);
    CHECK(run_calls == 9);
    run_span(0, original.card_lba, 4); run_span(4, original.card_lba + 4, 5);

    /* A partial final file block at UINT32_MAX cannot advertise a wrapped run. */
    fixture(true);
    struct kui_retail_extent *last = &manifest.extents[manifest.extent_count - 1];
    CHECK(last->blocks == 1);
    edge_source = last->card_lba;
    last->card_lba = UINT32_MAX;
    manifest.card_sectors = manifest.partition_end = UINT64_C(0x100000000);
    CHECK(kui_retail_image_init(&image, &manifest, read_block, card) == KUI_GAME_OK);
    image.read_run = read_run;
    compare(45071, 1, KUI_GAME_SECTOR_RAW);
    CHECK(run_calls == 6);
    CHECK(run_trace[5].lba == UINT32_MAX && run_trace[5].available == 1);
}
static void run_failure_tests(void) {
    fixture(false); image.read_run = read_run;
    memset(output, 0x77, sizeof(output));
    CHECK(kui_retail_image_read(&image, 45000, 2, KUI_GAME_SECTOR_MODE1,
        output, 4095) == KUI_GAME_RANGE);
    CHECK(kui_retail_image_read(&image, 2, 2, KUI_GAME_SECTOR_MODE1,
        output, sizeof(output)) == KUI_GAME_AUDIO);
    CHECK(run_calls == 0 && calls == 0);
    fail_call = 3;
    CHECK(kui_retail_image_read(&image, 45000, 2, KUI_GAME_SECTOR_MODE1,
        output, sizeof(output)) == KUI_GAME_IO);
    CHECK(run_calls == 3 && calls == 3 && !image.cache_valid);
    CHECK(run_trace[0].available == 9 && run_trace[2].available == 7);
    fail_call = 0; run_calls = 0;
    compare(45000, 2, KUI_GAME_SECTOR_MODE1);
    CHECK(run_calls == 9 && image.cache_valid);
    uint32_t cached = image.cached_lba;
    run_calls = 0;
    CHECK(kui_retail_image_read(&image, 45071, 2, KUI_GAME_SECTOR_RAW,
        output, sizeof(output)) == KUI_GAME_RANGE);
    CHECK(!run_calls && image.cache_valid && image.cached_lba == cached);

    fixture(false);
    uint32_t physical = manifest.extents[manifest.tracks[2].first_extent].card_lba;
    card[physical * 512u + 15] = 2;
    CHECK(kui_retail_image_init(&image, &manifest, read_block, card) == KUI_GAME_OK);
    image.read_run = read_run;
    memset(output, 0x77, sizeof(output));
    CHECK(kui_retail_image_read(&image, 45000, 2, KUI_GAME_SECTOR_MODE1,
        output, sizeof(output)) == KUI_GAME_MODE);
    /* A mode error can return early with unused advertised blocks. The caller
     * must stop its transport; the image layer must consume no further blocks. */
    CHECK(run_calls == 1 && calls == 1 && run_trace[0].available == 9);
    for(size_t i = 0; i < sizeof(output); ++i) CHECK(output[i] == 0x77);
}
static void maximum_map_tests(void) {
    fixture(false);
    memset(manifest.tracks, 0, sizeof(manifest.tracks));
    memset(manifest.extents, 0, sizeof(manifest.extents));
    manifest.card_sectors = manifest.partition_end = 100000;
    manifest.track_count = KUI_RETAIL_IMAGE_TRACKS;
    manifest.session_lba = manifest.boot_lba = 45000; manifest.boot_bytes = 2048;
    for(uint32_t i = 0; i < KUI_RETAIL_IMAGE_TRACKS; ++i) {
        uint32_t start = i < 2 ? i : 45000 + i - 2;
        manifest.tracks[i] = (struct kui_retail_track){i + 1, start, start + 1, 4, i, 1};
        manifest.extents[i] = (struct kui_retail_extent){0, 100 + i * 7, 5};
    }
    manifest.extent_count = KUI_RETAIL_IMAGE_TRACKS;
    CHECK(kui_retail_manifest_encode(&manifest, wire) == KUI_GAME_OK);
    CHECK(kui_retail_manifest_decode(wire, &decoded) == KUI_GAME_OK);
    CHECK(!memcmp(&manifest, &decoded, sizeof(manifest)));
    /* All 128 fragmented extents, including a final partly used block. */
    memset(manifest.tracks, 0, sizeof(manifest.tracks));
    memset(manifest.extents, 0, sizeof(manifest.extents));
    manifest.track_count = 1;
    manifest.tracks[0] = (struct kui_retail_track){1, 45000, 46000, 4, 0, 128};
    manifest.extent_count = 128;
    uint32_t total = (1000u * 2352u + 511u) / 512u, file_block = 0;
    for(uint32_t i = 0; i < 128; ++i) {
        uint32_t blocks = i == 127 ? total - file_block : 1;
        manifest.extents[i] = (struct kui_retail_extent){file_block, 100 + i * 3, blocks};
        file_block += blocks;
    }
    CHECK(kui_retail_manifest_encode(&manifest, wire) == KUI_GAME_OK);
    CHECK(kui_retail_manifest_decode(wire, &decoded) == KUI_GAME_OK);
    CHECK(!memcmp(&manifest, &decoded, sizeof(manifest)));
    /* Entire bounded disc / 12MiB boot exercise arithmetic without huge files. */
    manifest.tracks[0].end_lba = KUI_GAME_LBA_LIMIT;
    manifest.tracks[0].extent_count = manifest.extent_count = 1;
    memset(manifest.extents, 0, sizeof(manifest.extents));
    total = ((KUI_GAME_LBA_LIMIT - 45000) * 2352u + 511u) / 512u;
    manifest.extents[0] = (struct kui_retail_extent){0, 100, total};
    manifest.card_sectors = manifest.partition_end = (uint64_t)total + 100;
    manifest.boot_bytes = KUI_RETAIL_IMAGE_BOOT_MAX;
    CHECK(kui_retail_manifest_encode(&manifest, wire) == KUI_GAME_OK);
    CHECK(kui_retail_manifest_decode(wire, &decoded) == KUI_GAME_OK);
    CHECK(kui_retail_image_check(&decoded, KUI_GAME_LBA_LIMIT - 1, 1, KUI_GAME_SECTOR_RAW) == KUI_GAME_OK);
    CHECK(kui_retail_image_check(&decoded, KUI_GAME_LBA_LIMIT - 1, 2, KUI_GAME_SECTOR_RAW) == KUI_GAME_RANGE);
}
static void header_sector(uint8_t raw[KUI_GAME_RAW_BYTES], const uint8_t address[3]) {
    memset(raw, 0x5a, KUI_GAME_RAW_BYTES);
    raw[0] = 0; memset(raw + 1, 255, 10); raw[11] = 0;
    memcpy(raw + 12, address, 3); raw[15] = 1;
}
static void header_tests(void) {
    /* Addresses written out independently: LBA 45021 is FAD 45171 = 10:02:21;
     * LBA 0 is 00:02:00; LBA 500000 is FAD 500150 = 111:08:50, whose minute
     * carries into the tens nibble (0xb1) like recovery_sector.c. */
    const struct { uint32_t lba; uint8_t address[3]; } cases[] = {
        {45021, {0x10, 0x02, 0x21}}, {0, {0x00, 0x02, 0x00}},
        {500000, {0xb1, 0x08, 0x50}}, {KUI_GAME_LBA_LIMIT - 1u, {0xf9, 0x59, 0x74}}
    };
    uint8_t raw[KUI_GAME_RAW_BYTES];
    for(unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        header_sector(raw, cases[i].address);
        CHECK(kui_retail_sector_header(raw, cases[i].lba) == KUI_RETAIL_HEADER_OK);
        /* The data, EDC and ECC fields are deliberately not examined. */
        raw[16] ^= 0xff; raw[2064] ^= 0xff; raw[2300] ^= 0xff;
        CHECK(kui_retail_sector_header(raw, cases[i].lba) == KUI_RETAIL_HEADER_OK);
        CHECK(kui_retail_sector_header(raw, cases[i].lba + 1u) == KUI_RETAIL_HEADER_ADDRESS);
        if(cases[i].lba) CHECK(kui_retail_sector_header(raw, cases[i].lba - 1u) == KUI_RETAIL_HEADER_ADDRESS);
        for(unsigned byte = 12; byte < 15; ++byte) {
            raw[byte] ^= 0x01;
            CHECK(kui_retail_sector_header(raw, cases[i].lba) == KUI_RETAIL_HEADER_ADDRESS);
            raw[byte] ^= 0x01;
        }
        raw[15] = 2;
        CHECK(kui_retail_sector_header(raw, cases[i].lba) == KUI_RETAIL_HEADER_MODE);
        raw[15] = 1;
        for(unsigned byte = 0; byte < 12; ++byte) {
            raw[byte] ^= 0x80;
            CHECK(kui_retail_sector_header(raw, cases[i].lba) == KUI_RETAIL_HEADER_SYNC);
            raw[byte] ^= 0x80;
        }
    }
    header_sector(raw, cases[0].address);
    CHECK(kui_retail_sector_header(raw, KUI_GAME_LBA_LIMIT) == KUI_RETAIL_HEADER_ADDRESS);
}
int main(void) {
    header_tests();
    wire_tests(); invalid_map_tests(); reader_tests(false); reader_tests(true);
    sequential_cache_tests(false); sequential_cache_tests(true);
    run_span_tests(); run_failure_tests();
    maximum_map_tests();
    printf("retail image: %u checks passed (canonical wire, fragmented bounds, cached runs, IO)\n", checks);
    return 0;
}
