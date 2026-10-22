/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/resident_image.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct kui_resident_manifest manifest, decoded, backup, empty;
static struct kui_resident_image image;
static uint8_t card[2048u * 512u], wire[KUI_RESIDENT_IMAGE_WIRE_BYTES];
static uint8_t clean_wire[KUI_RESIDENT_IMAGE_WIRE_BYTES], output[64u * 2352u];
static uint32_t calls, fail_call;
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
    ++calls;
    if(fail_call && calls == fail_call) { memset(out, 0xdd, 512); return -1; }
    memcpy(out, card + lba * 512u, 512);
    return 0;
}
static void fixture(bool fragmented) {
    memset(&manifest, 0, sizeof(manifest));
    memset(card, 0xf3, sizeof(card));
    manifest.card_sectors = 2048;
    manifest.partition_start = 50; manifest.partition_end = 2000;
    manifest.track_count = 4; manifest.sample_count = 2;
    manifest.session_lba = 45000; manifest.boot_lba = 45001; manifest.boot_bytes = 4567;
    manifest.gdi_crc32 = 0x89abcdef;
    strcpy(manifest.title, "Original resident image test");
    strcpy(manifest.product, "KUITEST"); strcpy(manifest.region, "JUE");
    strcpy(manifest.bootfile, "1ST_READ.BIN");
    static const uint32_t starts[4] = {0, 3, 45000, 45070};
    static const uint32_t ends[4] = {3, 5, 45070, 45072};
    uint32_t physical_index = 0;
    for(unsigned i = 0; i < 4; ++i) {
        struct kui_resident_track *t = &manifest.tracks[i];
        *t = (struct kui_resident_track){i + 1, starts[i], ends[i], i == 1 ? 0u : 4u,
                                        manifest.extent_count, 0};
        uint32_t bytes = (t->end_lba - t->start_lba) * 2352u;
        uint32_t blocks = (bytes + 511u) / 512u;
        for(uint32_t n = 0; n < blocks;) {
            uint32_t take = fragmented ? 1 : blocks;
            uint32_t physical = fragmented ? 100u + physical_index * 3u : 100u + physical_index;
            manifest.extents[manifest.extent_count++] =
                (struct kui_resident_extent){n, physical, take};
            ++t->extent_count;
            for(uint32_t p = 0; p < take * 512u; ++p)
                if(n * 512u + p < bytes) card[physical * 512u + p] = source(i, n * 512u + p);
            n += take; physical_index += take;
        }
    }
    manifest.samples[0] = (struct kui_resident_sample){0, 1, KUI_GAME_SECTOR_MODE1, 0};
    manifest.samples[1] = (struct kui_resident_sample){45069, 2, KUI_GAME_SECTOR_RAW, 0};
    CHECK(kui_resident_manifest_validate(&manifest) == KUI_GAME_OK);
    calls = fail_call = 0;
    CHECK(kui_resident_image_init(&image, &manifest, read_block, card) == KUI_GAME_OK);
    for(unsigned i = 0; i < manifest.sample_count; ++i) {
        struct kui_resident_sample *s = &manifest.samples[i];
        CHECK(kui_resident_image_read(&image, s->lba, s->count,
            (enum kui_game_sector_format)s->format, output, sizeof(output)) == KUI_GAME_OK);
        manifest.samples[i].crc32 = kui_resident_crc32(0, output,
            s->count * (s->format == KUI_GAME_SECTOR_RAW ? 2352u : 2048u));
    }
    calls = fail_call = 0; image.blocks_read = 0;
}
static void put32(uint8_t *p, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8u * i));
}
static void refresh_crc(void) {
    memset(wire + 16, 0, 4);
    put32(wire + 16, kui_resident_crc32(0, wire, sizeof(wire)));
}
static void wire_tests(void) {
    fixture(true);
    CHECK(kui_resident_crc32(0, "123456789", 9) == 0xcbf43926);
    CHECK(kui_resident_crc32(kui_resident_crc32(0, "1234", 4), "56789", 5) == 0xcbf43926);
    CHECK(kui_resident_manifest_encode(&manifest, wire) == KUI_GAME_OK);
    CHECK(!memcmp(wire, "KUIG4I01", 8));
    CHECK(wire[8] == 1 && wire[12] == 0 && wire[13] == 0 && wire[14] == 1);
    CHECK(wire[20] == 4 && wire[32] == 0 && wire[33] == 8 && wire[68] == 0xef);
    CHECK(kui_resident_manifest_decode(wire, &decoded) == KUI_GAME_OK);
    CHECK(!memcmp(&decoded, &manifest, sizeof(manifest)));
    memcpy(clean_wire, wire, sizeof(wire));
    const unsigned offsets[] = {0, 7, 8, 12, 16, 20, 24, 28, 32, 39, 40, 48,
        56, 60, 64, 68, 72, 199, 200, 216, 240, 255, 256, 319, 320, 344,
        3488, 52640, 52896, 65535};
    for(unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        wire[offsets[i]] ^= 1;
        CHECK(kui_resident_manifest_decode(wire, &decoded) != KUI_GAME_OK);
        CHECK(!memcmp(&decoded, &empty, sizeof(decoded)));
        wire[offsets[i]] ^= 1;
    }
    for(unsigned i = 0; i < 100; ++i) {
        unsigned offset = (i * 619u + 401u) % sizeof(wire);
        wire[offset] ^= 0x80;
        CHECK(kui_resident_manifest_decode(wire, &decoded) != KUI_GAME_OK);
        wire[offset] ^= 0x80;
    }
    /* Unknown fields and noncanonical tails remain rejected with valid CRC. */
    const unsigned reserved[] = {256, 319, 344, 320 + 4 * 32,
        3488 + manifest.extent_count * 12, 52640 + 2 * 16, 65535};
    for(unsigned i = 0; i < sizeof(reserved) / sizeof(reserved[0]); ++i) {
        wire[reserved[i]] = 1; refresh_crc();
        CHECK(kui_resident_manifest_decode(wire, &decoded) == KUI_GAME_INVALID);
        memcpy(wire, clean_wire, sizeof(wire));
    }
    wire[72 + strlen(manifest.title) + 1] = 'X'; refresh_crc();
    CHECK(kui_resident_manifest_decode(wire, &decoded) == KUI_GAME_INVALID);
    memcpy(wire, clean_wire, sizeof(wire));
    put32(wire + 24, 4097); refresh_crc();
    CHECK(kui_resident_manifest_decode(wire, &decoded) == KUI_GAME_INVALID);
    memcpy(wire, clean_wire, sizeof(wire));
    /* Selection identity is protected together with the track mapping. */
    wire[68] ^= 1;
    CHECK(kui_resident_manifest_decode(wire, &decoded) == KUI_GAME_INVALID);
    memcpy(wire, clean_wire, sizeof(wire));
    manifest.extent_count = 4097;
    CHECK(kui_resident_manifest_encode(&manifest, wire) == KUI_GAME_INVALID);
    CHECK(!memcmp(wire, clean_wire, sizeof(wire)));
    CHECK(kui_resident_manifest_decode(NULL, &decoded) == KUI_GAME_INVALID);
    CHECK(!memcmp(&decoded, &empty, sizeof(decoded)));
}
static void invalid_map_tests(void) {
    fixture(true); backup = manifest;
#define BAD(field, value) do { manifest = backup; manifest.field = (value); \
    CHECK(kui_resident_manifest_validate(&manifest) != KUI_GAME_OK); } while(0)
    BAD(track_count, 0); BAD(track_count, 100); BAD(extent_count, 0);
    BAD(sample_count, 0); BAD(sample_count, 17); BAD(card_sectors, 0);
    BAD(card_sectors, UINT64_C(0x100000001)); BAD(partition_start, 2000);
    BAD(partition_end, 2049); BAD(partition_end, 101);
    BAD(tracks[0].number, 2); BAD(tracks[1].start_lba, 2);
    BAD(tracks[1].control, 1); BAD(tracks[0].end_lba, 0);
    BAD(tracks[3].end_lba, KUI_GAME_LBA_LIMIT + 1);
    BAD(tracks[2].first_extent, 0); BAD(tracks[2].extent_count, UINT32_MAX);
    BAD(extents[0].file_block, 1); BAD(extents[0].blocks, 0);
    BAD(extents[0].blocks, UINT32_MAX); BAD(extents[0].card_lba, 49);
    BAD(extents[1].file_block, 0); BAD(extents[1].card_lba, 100);
    BAD(extents[30].card_lba, manifest.extents[0].card_lba);
    BAD(session_lba, 45001); BAD(boot_lba, 4); BAD(boot_lba, 45072);
    BAD(boot_bytes, 0); BAD(boot_bytes, 16u * 1024u * 1024u + 1);
    BAD(samples[0].count, 65); BAD(samples[0].format, 2);
    BAD(samples[0].lba, 9); BAD(samples[0].lba, 3);
    BAD(title[0], 0); BAD(title[2], '\n'); BAD(bootfile[0], 0);
    manifest = backup; memset(manifest.title, 'a', sizeof(manifest.title));
    CHECK(kui_resident_manifest_validate(&manifest) == KUI_GAME_INVALID);
    manifest = backup;
    /* Last card block is representable; exclusive partition/card end is u64. */
    manifest.card_sectors = manifest.partition_end = UINT64_C(0x100000000);
    manifest.extents[0].card_lba = UINT32_MAX;
    CHECK(kui_resident_manifest_validate(&manifest) == KUI_GAME_OK);
    manifest.extents[1].card_lba = UINT32_MAX;
    CHECK(kui_resident_manifest_validate(&manifest) == KUI_GAME_OVERLAP);
#undef BAD
}
static void compare(uint32_t lba, uint32_t count, enum kui_game_sector_format format) {
    memset(output, 0x77, sizeof(output));
    CHECK(kui_resident_image_read(&image, lba, count, format, output, sizeof(output)) == KUI_GAME_OK);
    unsigned stride = format == KUI_GAME_SECTOR_RAW ? 2352u : 2048u;
    for(uint32_t n = 0; n < count; ++n) {
        unsigned t = 0;
        while(manifest.tracks[t].end_lba <= lba + n) ++t;
        uint32_t base = (lba + n - manifest.tracks[t].start_lba) * 2352u;
        if(format == KUI_GAME_SECTOR_MODE1) base += 16;
        for(unsigned j = 0; j < stride; ++j) CHECK(output[n * stride + j] == source(t, base + j));
    }
    if(count * stride < sizeof(output)) CHECK(output[count * stride] == 0x77);
}
static void reader_tests(bool fragmented) {
    fixture(fragmented);
    compare(0, 3, KUI_GAME_SECTOR_RAW);
    compare(0, 3, KUI_GAME_SECTOR_MODE1);
    compare(2, 2, KUI_GAME_SECTOR_RAW); /* Data/audio boundary across two files. */
    compare(3, 2, KUI_GAME_SECTOR_RAW); /* Final block has nonfile padding. */
    compare(45000, 64, KUI_GAME_SECTOR_MODE1);
    compare(45069, 3, KUI_GAME_SECTOR_MODE1);
    compare(45071, 1, KUI_GAME_SECTOR_RAW);
    for(unsigned i = 0; i < 20; ++i) compare(45000 + i * 31 % 70, 1, KUI_GAME_SECTOR_MODE1);
    CHECK(image.blocks_read == calls);
    unsigned before = calls;
    compare(45071, 1, KUI_GAME_SECTOR_RAW);
    CHECK(calls > before); /* Must not satisfy a fresh command from stale cache. */
    before = calls;
    memset(output, 0x77, sizeof(output));
    CHECK(kui_resident_image_read(&image, 2, 2, KUI_GAME_SECTOR_MODE1, output, sizeof(output)) == KUI_GAME_AUDIO);
    CHECK(kui_resident_image_read(&image, 4, 2, KUI_GAME_SECTOR_RAW, output, sizeof(output)) == KUI_GAME_GAP);
    CHECK(kui_resident_image_read(&image, 45071, 2, KUI_GAME_SECTOR_RAW, output, sizeof(output)) == KUI_GAME_RANGE);
    CHECK(kui_resident_image_read(&image, UINT32_MAX, 2, KUI_GAME_SECTOR_RAW, output, sizeof(output)) == KUI_GAME_RANGE);
    CHECK(kui_resident_image_read(&image, 45000, 65, KUI_GAME_SECTOR_RAW, output, sizeof(output)) == KUI_GAME_RANGE);
    CHECK(kui_resident_image_read(&image, 45000, 0, KUI_GAME_SECTOR_RAW, output, sizeof(output)) == KUI_GAME_INVALID);
    CHECK(kui_resident_image_read(&image, 45000, 1, (enum kui_game_sector_format)7, output, sizeof(output)) == KUI_GAME_INVALID);
    CHECK(kui_resident_image_read(&image, 45000, 1, KUI_GAME_SECTOR_RAW, output, 2351) == KUI_GAME_RANGE);
    CHECK(calls == before);
    for(size_t i = 0; i < sizeof(output); ++i) CHECK(output[i] == 0x77);
    fail_call = calls + 3;
    CHECK(kui_resident_image_read(&image, 45000, 3, KUI_GAME_SECTOR_MODE1, output, sizeof(output)) == KUI_GAME_IO);
    CHECK(!image.cache_valid);
    fail_call = 0; compare(45000, 3, KUI_GAME_SECTOR_MODE1);
    /* Deliberate media corruption is detected against launcher reference CRC. */
    uint32_t physical = manifest.extents[0].card_lba;
    card[physical * 512u + 25] ^= 1;
    CHECK(kui_resident_image_read(&image, 0, 1, KUI_GAME_SECTOR_MODE1, output, sizeof(output)) == KUI_GAME_OK);
    CHECK(kui_resident_crc32(0, output, 2048) != manifest.samples[0].crc32);
    card[physical * 512u + 25] ^= 1;
    card[physical * 512u + 15] = 2;
    CHECK(kui_resident_image_read(&image, 0, 1, KUI_GAME_SECTOR_MODE1, output, sizeof(output)) == KUI_GAME_MODE);
    CHECK(kui_resident_image_read(&image, 0, 1, KUI_GAME_SECTOR_RAW, output, sizeof(output)) == KUI_GAME_OK);
    CHECK(kui_resident_image_init(&image, &manifest, NULL, card) == KUI_GAME_INVALID);
    CHECK(image.manifest == NULL && image.read_block == NULL);
}
static void maximum_map_tests(void) {
    fixture(false);
    memset(manifest.tracks, 0, sizeof(manifest.tracks));
    memset(manifest.extents, 0, sizeof(manifest.extents));
    manifest.card_sectors = manifest.partition_end = 100000;
    manifest.track_count = 99; manifest.extent_count = 0;
    manifest.sample_count = 1;
    memset(manifest.samples, 0, sizeof(manifest.samples));
    manifest.samples[0] = (struct kui_resident_sample){45000, 1, KUI_GAME_SECTOR_MODE1, 0};
    manifest.session_lba = manifest.boot_lba = 45000; manifest.boot_bytes = 2048;
    for(uint32_t i = 0; i < 99; ++i) {
        uint32_t start = i < 2 ? i : 45000 + i - 2;
        manifest.tracks[i] = (struct kui_resident_track){i + 1, start, start + 1, 4, i, 1};
        manifest.extents[i] = (struct kui_resident_extent){0, 100 + i * 7, 5};
    }
    manifest.extent_count = 99;
    CHECK(kui_resident_manifest_encode(&manifest, wire) == KUI_GAME_OK);
    CHECK(kui_resident_manifest_decode(wire, &decoded) == KUI_GAME_OK);
    CHECK(!memcmp(&manifest, &decoded, sizeof(manifest)));
    /* 4096 fragmented extents, including a final partly used card block. */
    memset(manifest.tracks, 0, sizeof(manifest.tracks));
    memset(manifest.extents, 0, sizeof(manifest.extents));
    manifest.track_count = 1;
    manifest.tracks[0] = (struct kui_resident_track){1, 45000, 46000, 4, 0, 4096};
    manifest.extent_count = 4096;
    uint32_t total = (1000u * 2352u + 511u) / 512u, file_block = 0;
    for(uint32_t i = 0; i < 4096; ++i) {
        uint32_t blocks = i == 4095 ? total - file_block : 1;
        manifest.extents[i] = (struct kui_resident_extent){file_block, 100 + i * 3, blocks};
        file_block += blocks;
    }
    CHECK(kui_resident_manifest_encode(&manifest, wire) == KUI_GAME_OK);
    CHECK(kui_resident_manifest_decode(wire, &decoded) == KUI_GAME_OK);
    CHECK(!memcmp(&manifest, &decoded, sizeof(manifest)));
}
int main(void) {
    wire_tests(); invalid_map_tests(); reader_tests(false); reader_tests(true);
    maximum_map_tests();
    printf("resident image: %u checks passed (wire, fragmentation, boundaries, identity, IO)\n", checks);
    return 0;
}
