/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_image.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct file { char name[KUI_GAME_NAME_CAP]; uint64_t bytes; unsigned tag; };
struct fixture {
    struct file files[KUI_GAME_TRACK_MAX];
    unsigned count, stats, reads, fail_stat, fail_read;
    enum kui_game_result failure;
    bool bad_mode, bad_sync, truncated;
    uint64_t last_offset;
    size_t last_size;
};

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while(0)

static struct file *find(struct fixture *fixture, const char *name) {
    for(unsigned i = 0; i < fixture->count; ++i)
        if(!strcmp(name, fixture->files[i].name)) return &fixture->files[i];
    return NULL;
}

static enum kui_game_result stat_file(void *ctx, const char *name, uint64_t *bytes) {
    struct fixture *fixture = ctx;
    if(++fixture->stats == fixture->fail_stat) return fixture->failure;
    struct file *file = find(fixture, name);
    if(!file) return KUI_GAME_NOT_FOUND;
    *bytes = file->bytes;
    return KUI_GAME_OK;
}

static unsigned char raw_byte(unsigned tag, uint64_t offset) {
    unsigned local = (unsigned)(offset % KUI_GAME_RAW_BYTES);
    if(local == 0 || local == 11) return 0;
    if(local < 11) return 255;
    if(local == 15) return 1;
    return (unsigned char)((tag * 53u + (offset / KUI_GAME_RAW_BYTES) * 7u + local) % 251u);
}

static enum kui_game_result read_file(void *ctx, const char *name, uint64_t offset,
                                      void *out, size_t size) {
    struct fixture *fixture = ctx;
    fixture->last_offset = offset;
    fixture->last_size = size;
    if(++fixture->reads == fixture->fail_read) return fixture->failure;
    struct file *file = find(fixture, name);
    if(!file) return KUI_GAME_NOT_FOUND;
    if(offset > file->bytes || size > file->bytes - offset || fixture->truncated)
        return KUI_GAME_IO;
    unsigned char *data = out;
    for(size_t i = 0; i < size; ++i) {
        data[i] = raw_byte(file->tag, offset + i);
        if(fixture->bad_mode && (offset + i) % KUI_GAME_RAW_BYTES == 15) data[i] = 2;
        if(fixture->bad_sync && (offset + i) % KUI_GAME_RAW_BYTES == 5) data[i] = 0;
    }
    return KUI_GAME_OK;
}

static void add_file(struct fixture *fixture, const char *name, uint32_t sectors) {
    struct file *file = &fixture->files[fixture->count];
    CHECK(strlen(name) < sizeof(file->name));
    memcpy(file->name, name, strlen(name) + 1u);
    file->bytes = (uint64_t)sectors * KUI_GAME_RAW_BYTES;
    file->tag = fixture->count + 1u;
    ++fixture->count;
}

static void init(struct fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    add_file(fixture, "track01.bin", 4);
    add_file(fixture, "track02.raw", 2);
    add_file(fixture, "Track 03.bin", 4);
    add_file(fixture, "track04.bin", 3);
}

static const char descriptor[] =
    "4\r\n1 0 4 2352 track01.bin 0\r\n2 4 0 2352 track02.raw 0\r\n"
    "3 45000 4 2352 \"Track 03.bin\" 0\r\n4 45004 4 2352 track04.bin 0\r\n";

static enum kui_game_result open_image(struct fixture *fixture, const char *gdi,
                                       struct kui_game_image *image) {
    struct kui_game_file_ops ops = {fixture, stat_file, read_file};
    return kui_game_image_open(gdi, strlen(gdi), &ops, image);
}

static bool all_is(const void *data, size_t bytes, unsigned char value) {
    const unsigned char *p = data;
    for(size_t i = 0; i < bytes; ++i) if(p[i] != value) return false;
    return true;
}

static void expect_raw(const unsigned char *bytes, unsigned tag, uint32_t sector,
                       unsigned count) {
    for(size_t i = 0; i < (size_t)count * KUI_GAME_RAW_BYTES; ++i)
        assert(bytes[i] == raw_byte(tag, (uint64_t)sector * KUI_GAME_RAW_BYTES + i));
    ++checks;
}

static void expect_data(const unsigned char *bytes, unsigned tag, uint32_t sector,
                        unsigned count) {
    for(unsigned i = 0; i < count; ++i)
        for(unsigned j = 0; j < KUI_GAME_DATA_BYTES; ++j)
            assert(bytes[(size_t)i * KUI_GAME_DATA_BYTES + j] ==
                   raw_byte(tag, (uint64_t)(sector + i) * KUI_GAME_RAW_BYTES + 16u + j));
    ++checks;
}

static void happy_paths(void) {
    struct fixture fixture;
    struct kui_game_image image;
    init(&fixture);
    CHECK(open_image(&fixture, descriptor, &image) == KUI_GAME_OK);
    CHECK(fixture.stats == 4 && fixture.reads == 0 && image.count == 4);
    CHECK(image.tracks[2].start_lba == 45000 && image.tracks[2].end_lba == 45004);
    CHECK(image.tracks[3].file_bytes == 3u * KUI_GAME_RAW_BYTES);
    CHECK(!strcmp(image.tracks[2].name, "Track 03.bin"));
    unsigned char out[4u * KUI_GAME_RAW_BYTES + 1u];
    memset(out, 0xa5, sizeof(out));
    CHECK(kui_game_image_read(&image, 45002, 4, KUI_GAME_SECTOR_RAW, out, sizeof(out)) == KUI_GAME_OK);
    CHECK(fixture.reads == 2);
    expect_raw(out, 3, 2, 2);
    expect_raw(out + 2u * KUI_GAME_RAW_BYTES, 4, 0, 2);
    CHECK(out[sizeof(out) - 1u] == 0xa5);
    fixture.reads = 0;
    memset(out, 0xa5, sizeof(out));
    CHECK(kui_game_image_read(&image, 45002, 4, KUI_GAME_SECTOR_MODE1, out, sizeof(out)) == KUI_GAME_OK);
    CHECK(fixture.reads == 4 && fixture.last_offset == KUI_GAME_RAW_BYTES);
    expect_data(out, 3, 2, 2);
    expect_data(out + 2u * KUI_GAME_DATA_BYTES, 4, 0, 2);
    CHECK(all_is(out + 4u * KUI_GAME_DATA_BYTES, sizeof(out) - 4u * KUI_GAME_DATA_BYTES, 0xa5));
    CHECK(kui_game_image_read(&image, 4, 2, KUI_GAME_SECTOR_RAW, out, sizeof(out)) == KUI_GAME_OK);
    expect_raw(out, 2, 0, 2);
    CHECK(kui_game_image_read(&image, 45006, 1, KUI_GAME_SECTOR_MODE1, out, sizeof(out)) == KUI_GAME_OK);
    expect_data(out, 4, 2, 1);
    CHECK(kui_game_image_check(&image, 45000, 7, KUI_GAME_SECTOR_MODE1) == KUI_GAME_OK);
    CHECK(kui_game_image_check(&image, 0, 6, KUI_GAME_SECTOR_RAW) == KUI_GAME_OK);
}

static void preflight(void) {
    struct fixture fixture;
    struct kui_game_image image;
    init(&fixture);
    CHECK(open_image(&fixture, descriptor, &image) == KUI_GAME_OK);
    struct request { uint32_t lba, count; enum kui_game_sector_format format; size_t size; enum kui_game_result error; };
    static const struct request requests[] = {
        {3, 2, KUI_GAME_SECTOR_MODE1, 4096, KUI_GAME_AUDIO},
        {4, 1, KUI_GAME_SECTOR_MODE1, 2048, KUI_GAME_AUDIO},
        {5, 44996, KUI_GAME_SECTOR_RAW, 9408, KUI_GAME_GAP},
        {6, 1, KUI_GAME_SECTOR_RAW, 2352, KUI_GAME_GAP},
        {44999, 2, KUI_GAME_SECTOR_MODE1, 4096, KUI_GAME_GAP},
        {45007, 1, KUI_GAME_SECTOR_RAW, 2352, KUI_GAME_RANGE},
        {45006, 2, KUI_GAME_SECTOR_RAW, 4704, KUI_GAME_RANGE},
        {UINT32_MAX, 2, KUI_GAME_SECTOR_RAW, 4704, KUI_GAME_RANGE},
        {1, UINT32_MAX, KUI_GAME_SECTOR_MODE1, 9408, KUI_GAME_RANGE},
        {0, 0, KUI_GAME_SECTOR_RAW, 0, KUI_GAME_INVALID},
        {0, 1, (enum kui_game_sector_format)99, 2352, KUI_GAME_INVALID},
        {0, 1, KUI_GAME_SECTOR_RAW, 2351, KUI_GAME_INVALID},
        {0, 2, KUI_GAME_SECTOR_MODE1, 4095, KUI_GAME_INVALID}
    };
    unsigned char out[9408];
    memset(out, 0xa5, sizeof(out));
    for(unsigned i = 0; i < sizeof(requests) / sizeof(requests[0]); ++i) {
        const struct request *request = &requests[i];
        CHECK(kui_game_image_read(&image, request->lba, request->count, request->format,
                                 out, request->size) == request->error);
        CHECK(fixture.reads == 0 && all_is(out, sizeof(out), 0xa5));
    }
    CHECK(kui_game_image_read(&image, 0, 1, KUI_GAME_SECTOR_RAW, NULL, 2352) == KUI_GAME_INVALID);
    CHECK(fixture.reads == 0);
    image.count = 100;
    CHECK(kui_game_image_check(&image, 0, 1, KUI_GAME_SECTOR_RAW) == KUI_GAME_INVALID);
    image.count = 4;
    image.tracks[1].end_lba = image.tracks[1].start_lba;
    CHECK(kui_game_image_check(&image, 0, 1, KUI_GAME_SECTOR_RAW) == KUI_GAME_INVALID);
}

static void syntax(void) {
    static const struct { const char *text; enum kui_game_result result; } cases[] = {
        {"0\n", KUI_GAME_SYNTAX}, {"100\n", KUI_GAME_SYNTAX},
        {"4294967296\n", KUI_GAME_SYNTAX}, {"-1\n", KUI_GAME_SYNTAX},
        {"1\n", KUI_GAME_SYNTAX}, {"1\n1 0 4 2352 track01.bin 0\n\n", KUI_GAME_SYNTAX},
        {"1\n2 0 4 2352 track01.bin 0", KUI_GAME_SYNTAX},
        {"1\n1 0 4 2352 track01.bin 0 trailing", KUI_GAME_SYNTAX},
        {"1\n1 0 4 2352 track01.bin 0\n2 4 0 2352 track02.raw 0", KUI_GAME_SYNTAX},
        {"1\n1 0 4 2352 \"unterminated 0", KUI_GAME_SYNTAX},
        {"1\n1 0 4 2352 ../track01.bin 0", KUI_GAME_SYNTAX},
        {"1\n1 0 4 2352 sub/track01.bin 0", KUI_GAME_SYNTAX},
        {"1\n1 0 4 2352 sub\\track01.bin 0", KUI_GAME_SYNTAX},
        {"1\n1 0 4 2352 0:track01.bin 0", KUI_GAME_SYNTAX},
        {"1\n1 0 4 2352 . 0", KUI_GAME_SYNTAX},
        {"1\n1 0 4 2352 \"track01.bin.\" 0", KUI_GAME_SYNTAX},
        {"1\n1 0 4 2352 \"track01.bin \" 0", KUI_GAME_SYNTAX},
        {"1\n1 0 4 2352 \" track01.bin\" 0", KUI_GAME_SYNTAX},
        {"1\n1 0 4 2352 \"track01.bin\"0", KUI_GAME_SYNTAX},
        {"1\n1 0 4 2352 \"track\t01.bin\" 0", KUI_GAME_SYNTAX},
        {"1\n1 0 4 2352 \"track\377.bin\" 0", KUI_GAME_SYNTAX},
        {"1\n1 0 4 2352 track?01.bin 0", KUI_GAME_SYNTAX},
        {"1\n1 0 4 2352 track01.bin -1", KUI_GAME_SYNTAX},
        {"1\n1 4294967296 4 2352 track01.bin 0", KUI_GAME_SYNTAX},
        {"1\n1 0 5 2352 track01.bin 0", KUI_GAME_UNSUPPORTED},
        {"1\n1 0 4 2048 track01.bin 0", KUI_GAME_UNSUPPORTED},
        {"1\n1 0 4 2352 track01.bin 2352", KUI_GAME_UNSUPPORTED},
        {"1\n1 719850 4 2352 track01.bin 0", KUI_GAME_RANGE},
        {"2\n1 4 4 2352 track01.bin 0\n2 0 0 2352 track02.raw 0", KUI_GAME_OVERLAP},
        {"2\n1 0 4 2352 track01.bin 0\n2 0 0 2352 track02.raw 0", KUI_GAME_OVERLAP},
        {"2\n1 0 4 2352 track01.bin 0\n2 4 4 2352 TRACK01.BIN 0", KUI_GAME_OVERLAP}
    };
    struct fixture fixture;
    struct kui_game_image image;
    init(&fixture);
    memset(&image, 0xa5, sizeof(image));
    for(unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        CHECK(open_image(&fixture, cases[i].text, &image) == cases[i].result);
        CHECK(fixture.stats == 0 && fixture.reads == 0);
        CHECK(all_is(&image, sizeof(image), 0xa5));
    }
    struct kui_game_file_ops ops = {&fixture, stat_file, read_file};
    const unsigned char embedded_nul[] = "1\n1 0 4 2352 track\0.bin 0";
    CHECK(kui_game_image_open(embedded_nul, sizeof(embedded_nul) - 1u, &ops, &image) == KUI_GAME_SYNTAX);
    CHECK(kui_game_image_open(descriptor, KUI_GAME_GDI_LIMIT + 1u, &ops, &image) == KUI_GAME_INVALID);
    CHECK(kui_game_image_open(descriptor, 0, &ops, &image) == KUI_GAME_INVALID);
    CHECK(kui_game_image_open(NULL, 1, &ops, &image) == KUI_GAME_INVALID);
    CHECK(kui_game_image_open(descriptor, 1, NULL, &image) == KUI_GAME_INVALID);
    CHECK(kui_game_image_open(descriptor, 1, &ops, NULL) == KUI_GAME_INVALID);
    CHECK(fixture.stats == 0 && fixture.reads == 0 && all_is(&image, sizeof(image), 0xa5));
    for(size_t i = 0; i < sizeof(descriptor) - 1u; ++i) {
        char broken[sizeof(descriptor)];
        memcpy(broken, descriptor, sizeof(broken));
        broken[i] = 0;
        CHECK(kui_game_image_open(broken, sizeof(broken) - 1u, &ops, &image) == KUI_GAME_SYNTAX);
        CHECK(fixture.stats == 0 && all_is(&image, sizeof(image), 0xa5));
    }
    CHECK(open_image(&fixture, "1\n1\t0 4 2352 track01.bin 0", &image) == KUI_GAME_OK);
}

static void file_sizes_and_failures(void) {
    struct fixture fixture;
    struct kui_game_image image;
    init(&fixture);
    memset(&image, 0xa5, sizeof(image));
    uint64_t sizes[] = {0, 2351, 2353, UINT64_MAX, UINT64_MAX - UINT64_MAX % KUI_GAME_RAW_BYTES};
    for(unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        fixture.files[0].bytes = sizes[i];
        enum kui_game_result result = open_image(&fixture, descriptor, &image);
        CHECK(result == KUI_GAME_FILE_SIZE || result == KUI_GAME_RANGE);
        CHECK(all_is(&image, sizeof(image), 0xa5));
    }
    fixture.files[0].bytes = 5u * KUI_GAME_RAW_BYTES;
    CHECK(open_image(&fixture, descriptor, &image) == KUI_GAME_OVERLAP);
    fixture.files[0].bytes = 4u * KUI_GAME_RAW_BYTES;
    fixture.count = 3;
    CHECK(open_image(&fixture, descriptor, &image) == KUI_GAME_NOT_FOUND);
    CHECK(all_is(&image, sizeof(image), 0xa5));
    fixture.count = 4;
    fixture.stats = 0;
    fixture.fail_stat = 3;
    fixture.failure = KUI_GAME_CANCELLED;
    CHECK(open_image(&fixture, descriptor, &image) == KUI_GAME_CANCELLED);
    CHECK(fixture.stats == 3 && all_is(&image, sizeof(image), 0xa5));
    fixture.fail_stat = 0;
    CHECK(open_image(&fixture, descriptor, &image) == KUI_GAME_OK);
    unsigned char out[4096];
    memset(out, 0xa5, sizeof(out));
    fixture.truncated = true;
    CHECK(kui_game_image_read(&image, 0, 1, KUI_GAME_SECTOR_MODE1, out, sizeof(out)) == KUI_GAME_IO);
    CHECK(all_is(out, sizeof(out), 0xa5));
    fixture.truncated = false;
    fixture.bad_mode = true;
    CHECK(kui_game_image_read(&image, 0, 1, KUI_GAME_SECTOR_MODE1, out, sizeof(out)) == KUI_GAME_MODE);
    CHECK(all_is(out, sizeof(out), 0xa5));
    fixture.bad_mode = false;
    fixture.bad_sync = true;
    CHECK(kui_game_image_read(&image, 0, 1, KUI_GAME_SECTOR_MODE1, out, sizeof(out)) == KUI_GAME_MODE);
    CHECK(all_is(out, sizeof(out), 0xa5));
    fixture.bad_sync = false;
    fixture.reads = 0;
    fixture.fail_read = 2;
    fixture.failure = KUI_GAME_CANCELLED;
    CHECK(kui_game_image_read(&image, 0, 2, KUI_GAME_SECTOR_MODE1, out, sizeof(out)) == KUI_GAME_CANCELLED);
    CHECK(fixture.reads == 2);
    expect_data(out, 1, 0, 1);
    CHECK(all_is(out + KUI_GAME_DATA_BYTES, KUI_GAME_DATA_BYTES, 0xa5));
}

static void upper_bounds(void) {
    struct fixture fixture = {0};
    struct kui_game_image image;
    char gdi[KUI_GAME_GDI_LIMIT];
    size_t used = (size_t)snprintf(gdi, sizeof(gdi), "99\n");
    for(unsigned i = 0; i < KUI_GAME_TRACK_MAX; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "track%02u.bin", i + 1u);
        add_file(&fixture, name, 1);
        int count = snprintf(gdi + used, sizeof(gdi) - used, "%u %u 4 2352 %s 0\n", i + 1u, i, name);
        CHECK(count > 0 && (size_t)count < sizeof(gdi) - used);
        used += (size_t)count;
    }
    CHECK(open_image(&fixture, gdi, &image) == KUI_GAME_OK);
    CHECK(image.count == 99 && fixture.stats == 99);
    CHECK(kui_game_image_check(&image, 0, 99, KUI_GAME_SECTOR_MODE1) == KUI_GAME_OK);
    memset(&fixture, 0, sizeof(fixture));
    add_file(&fixture, "track01.bin", KUI_GAME_LBA_LIMIT);
    CHECK(open_image(&fixture, "1\n1 0 4 2352 track01.bin 0", &image) == KUI_GAME_OK);
    unsigned char out[KUI_GAME_RAW_BYTES];
    CHECK(kui_game_image_read(&image, KUI_GAME_LBA_LIMIT - 1u, 1,
                             KUI_GAME_SECTOR_RAW, out, sizeof(out)) == KUI_GAME_OK);
    CHECK(fixture.last_offset == (uint64_t)(KUI_GAME_LBA_LIMIT - 1u) * KUI_GAME_RAW_BYTES);
    CHECK(fixture.last_size == sizeof(out));
    expect_raw(out, 1, KUI_GAME_LBA_LIMIT - 1u, 1);
    fixture.files[0].bytes += KUI_GAME_RAW_BYTES;
    CHECK(open_image(&fixture, "1\n1 0 4 2352 track01.bin 0", &image) == KUI_GAME_RANGE);
    fixture.files[0].bytes = KUI_GAME_RAW_BYTES;
    CHECK(open_image(&fixture, "1\n1 719849 4 2352 track01.bin 0", &image) == KUI_GAME_OK);
    CHECK(kui_game_image_check(&image, 719848, 1, KUI_GAME_SECTOR_RAW) == KUI_GAME_RANGE);
    CHECK(kui_game_image_check(&image, 719849, 1, KUI_GAME_SECTOR_RAW) == KUI_GAME_OK);
    memset(&fixture, 0, sizeof(fixture));
    char name[KUI_GAME_NAME_CAP + 1u];
    memset(name, 'a', sizeof(name));
    name[KUI_GAME_NAME_CAP - 1u] = 0;
    add_file(&fixture, name, 1);
    snprintf(gdi, sizeof(gdi), "1\n1 0 4 2352 %s 0\n", name);
    CHECK(open_image(&fixture, gdi, &image) == KUI_GAME_OK);
    name[KUI_GAME_NAME_CAP - 1u] = 'a';
    name[KUI_GAME_NAME_CAP] = 0;
    snprintf(gdi, sizeof(gdi), "1\n1 0 4 2352 %s 0\n", name);
    CHECK(open_image(&fixture, gdi, &image) == KUI_GAME_SYNTAX);
    for(int result = KUI_GAME_OK; result <= KUI_GAME_MODE; ++result)
        CHECK(strcmp(kui_game_result_name((enum kui_game_result)result), "Unknown image error"));
    CHECK(!strcmp(kui_game_result_name((enum kui_game_result)999), "Unknown image error"));
}

int main(void) {
    happy_paths();
    preflight();
    syntax();
    file_sizes_and_failures();
    upper_bounds();
    printf("game image: %u checks passed\n", checks);
    return 0;
}
