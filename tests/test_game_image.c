/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_image.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct file { char name[KUI_GAME_NAME_CAP]; uint64_t bytes; unsigned tag, stride; const unsigned char *content; };
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
    if(file->content) {
        memcpy(out, file->content + offset, size);
        return KUI_GAME_OK;
    }
    unsigned char *data = out;
    for(size_t i = 0; i < size; ++i) {
        uint64_t at = offset + i;
        if(file->stride == KUI_GAME_DATA_BYTES)
            at = at / KUI_GAME_DATA_BYTES * KUI_GAME_RAW_BYTES + 16u + at % KUI_GAME_DATA_BYTES;
        data[i] = raw_byte(file->tag, at);
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
    file->stride = KUI_GAME_RAW_BYTES;
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
        {"1\n1 0 0 2048 track01.bin 0", KUI_GAME_UNSUPPORTED},
        {"1\n1 0 4 2336 track01.bin 0", KUI_GAME_UNSUPPORTED},
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

static void cooked_tracks(void) {
    struct fixture fixture;
    struct kui_game_image image;
    init(&fixture);
    fixture.files[2].stride = KUI_GAME_DATA_BYTES;
    fixture.files[2].bytes = 4u * KUI_GAME_DATA_BYTES;
    const char mixed[] =
        "4\n1 0 4 2352 track01.bin 0\n2 4 0 2352 track02.raw 0\n"
        "3 45000 4 2048 \"Track 03.bin\" 0\n4 45004 4 2352 track04.bin 0\n";
    CHECK(open_image(&fixture, mixed, &image) == KUI_GAME_OK);
    CHECK(image.tracks[2].sector_bytes == 2048 && image.tracks[2].end_lba == 45004);
    unsigned char out[4u * KUI_GAME_RAW_BYTES + 1u];
    memset(out, 0xa5, sizeof(out));
    CHECK(kui_game_image_read(&image, 45001, 3, KUI_GAME_SECTOR_MODE1, out, sizeof(out)) == KUI_GAME_OK);
    expect_data(out, 3, 1, 3);
    CHECK(fixture.reads == 1 && fixture.last_offset == 2048 && fixture.last_size == 6144);
    CHECK(out[6144] == 0xa5);
    fixture.reads = 0;
    CHECK(kui_game_image_read(&image, 45002, 4, KUI_GAME_SECTOR_MODE1, out, sizeof(out)) == KUI_GAME_OK);
    expect_data(out, 3, 2, 2);
    expect_data(out + 4096, 4, 0, 2);
    CHECK(fixture.reads == 3); /* one cooked span, two raw sectors */
    memset(out, 0xa5, sizeof(out));
    fixture.reads = 0;
    CHECK(kui_game_image_read(&image, 45003, 2, KUI_GAME_SECTOR_RAW, out, sizeof(out)) == KUI_GAME_UNSUPPORTED);
    CHECK(fixture.reads == 0 && all_is(out, sizeof(out), 0xa5));
    CHECK(kui_game_image_read(&image, 4, 2, KUI_GAME_SECTOR_RAW, out, sizeof(out)) == KUI_GAME_OK);
    expect_raw(out, 2, 0, 2);
    /* File length uses the declared stride, and opened metadata is guarded. */
    fixture.files[2].bytes = 4u * 2048u - 1u;
    CHECK(open_image(&fixture, mixed, &image) == KUI_GAME_FILE_SIZE);
    image.tracks[2].sector_bytes = 2049;
    CHECK(kui_game_image_check(&image, 45000, 1, KUI_GAME_SECTOR_MODE1) == KUI_GAME_INVALID);
}


static enum kui_game_result named(struct fixture *fixture, const char *name,
                                  struct kui_game_image *image) {
    struct kui_game_file_ops ops = {fixture, stat_file, read_file};
    return kui_game_image_open_named(name, &ops, image);
}
static void add_content(struct fixture *fixture, const char *name,
                         const unsigned char *data, size_t bytes) {
    add_file(fixture, name, 1);
    struct file *file = &fixture->files[fixture->count - 1u];
    file->bytes = bytes;
    file->content = data;
}
static void offsets(void) {
    struct fixture fixture = {0};
    struct kui_game_image image;
    add_file(&fixture, "shared.bin", 6);
    const char shared[] = "2\n1 0 4 2352 shared.bin 2352\n2 2 4 2352 shared.bin 7056\n";
    CHECK(open_image(&fixture, shared, &image) == KUI_GAME_OK);
    CHECK(image.tracks[0].file_offset == 2352u && image.tracks[0].end_lba == 2);
    CHECK(image.tracks[1].file_offset == 7056u && image.tracks[1].end_lba == 5);
    CHECK(image.tracks[0].file_bytes == 6u * 2352u);
    unsigned char out[2u * 2048u];
    CHECK(kui_game_image_read(&image, 1, 2, KUI_GAME_SECTOR_MODE1, out, sizeof(out)) == KUI_GAME_OK);
    expect_data(out, 1, 2, 2);
    CHECK(fixture.last_offset == 7056u);
    CHECK(open_image(&fixture, "1\n1 0 4 2352 shared.bin 14112", &image) == KUI_GAME_FILE_SIZE);
    CHECK(open_image(&fixture, "1\n1 0 4 2352 shared.bin 1", &image) == KUI_GAME_FILE_SIZE);
    CHECK(open_image(&fixture, "2\n1 0 4 2352 shared.bin 7056\n2 2 4 2352 shared.bin 2352", &image) == KUI_GAME_OVERLAP);
}
static void put16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void put32(unsigned char *p, uint32_t v) {
    for(unsigned i = 0; i < 4u; ++i) p[i] = (unsigned char)(v >> (i * 8u));
}
static void both32_test(unsigned char *p, uint32_t v) {
    put32(p, v);
    for(unsigned i = 0; i < 4u; ++i) p[4u + i] = (unsigned char)(v >> ((3u - i) * 8u));
}
static void raw_sector(unsigned char *p, unsigned stride, unsigned mode, unsigned tag) {
    memset(p, 0, stride);
    unsigned sub = stride == 2336u ? 0u : 16u;
    if(stride != 2336u) {
        memset(p + 1u, 255, 10u);
        p[13] = 2u; /* First raw sector address = FAD150 = LBA0. */
        p[15] = (unsigned char)mode;
    }
    unsigned data = mode == 1u ? 16u : sub + 8u;
    for(unsigned i = 0; i < 2048u; ++i) p[data + i] = (unsigned char)(i + tag);
}
static void cue_and_modes(void) {
    static const char cue[] =
        "REM SESSION 01\nFILE \"shared.bin\" BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n"
        "REM SESSION 02\nTRACK 02 MODE2/2352\nPREGAP 02:35:69\n"
        "INDEX 00 00:00:04\nINDEX 01 00:00:06\n";
    unsigned char data[10u * 2352u], out[2352];
    memset(data, 0x77, sizeof(data));
    for(unsigned i = 4; i < 10; ++i) raw_sector(data + i * 2352u, 2352u, 2u, i);
    struct fixture fixture = {0};
    struct kui_game_image image;
    add_content(&fixture, "shared.bin", data, sizeof(data));
    add_content(&fixture, "Game.CUE", (const unsigned char *)cue, sizeof(cue) - 1u);
    CHECK(named(&fixture, "Game.CUE", &image) == KUI_GAME_OK);
    static const unsigned char unicode_cue[] =
        "\xef\xbb\xbfREM\nREM ThisRemarkIsLongerThanTheGeometryKeywordBuffer\n"
        "TITLE \"\xe6\x97\xa5\xe6\x9c\xac\"\nFILE shared.bin BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:04\n";
    add_content(&fixture, "unicode.cue", unicode_cue, sizeof(unicode_cue) - 1u);
    struct kui_game_image unicode_image;
    CHECK(named(&fixture, "unicode.cue", &unicode_image) == KUI_GAME_OK);
    CHECK(image.format == KUI_GAME_IMAGE_CUE && image.data_lba == 11700u && !image.scrambled && image.cd_image);
    CHECK(image.count == 2 && image.tracks[0].end_lba == 4);
    CHECK(image.tracks[1].file_offset == 6u * 2352u && image.tracks[1].end_lba == 11704u);
    CHECK(image.tracks[1].data_offset == 24u && image.tracks[1].sector_mode == 2u);
    CHECK(kui_game_image_read(&image, 11700, 1, KUI_GAME_SECTOR_MODE1, out, sizeof(out)) == KUI_GAME_OK);
    CHECK(!memcmp(out, data + 6u * 2352u + 24u, 2048u));
    CHECK(kui_game_image_check(&image, 4, 1, KUI_GAME_SECTOR_MODE1) == KUI_GAME_GAP);
    CHECK(kui_game_image_read(&image, 0, 1, KUI_GAME_SECTOR_RAW, out, sizeof(out)) == KUI_GAME_OK);
    CHECK(all_is(out, sizeof(out), 0x77));
    data[6u * 2352u + 18u] = data[6u * 2352u + 22u] = 0x20;
    memset(out, 0xa5, sizeof(out));
    CHECK(kui_game_image_read(&image, 11700, 1, KUI_GAME_SECTOR_MODE1, out, sizeof(out)) == KUI_GAME_MODE);
    CHECK(all_is(out, sizeof(out), 0xa5));
    data[6u * 2352u + 18u] = data[6u * 2352u + 22u] = 0;
    data[6u * 2352u + 16u] = 1;
    CHECK(kui_game_image_read(&image, 11700, 1, KUI_GAME_SECTOR_MODE1, out, sizeof(out)) == KUI_GAME_MODE);
    for(unsigned stride = 2336u; stride <= 2448u; stride += 112u) {
        unsigned char sector[2448];
        raw_sector(sector, stride, 2u, 42u);
        char text[128];
        int length = snprintf(text, sizeof(text), "REM KUI SCRAMBLED 1\nFILE data.bin BINARY\nTRACK 01 MODE2/%u\nINDEX 01 00:00:00\n", stride);
        memset(&fixture, 0, sizeof(fixture));
        add_content(&fixture, "data.bin", sector, stride);
        add_content(&fixture, "game.cue", (const unsigned char *)text, (size_t)length);
        CHECK(named(&fixture, "game.cue", &image) == KUI_GAME_OK);
        CHECK(image.scrambled && image.tracks[0].sector_bytes == stride);
        CHECK(kui_game_image_read(&image, 0, 1, KUI_GAME_SECTOR_MODE1, out, sizeof(out)) == KUI_GAME_OK);
        CHECK(!memcmp(out, sector + (stride == 2336u ? 8u : 24u), 2048u));
        CHECK(kui_game_image_check(&image, 0, 1, KUI_GAME_SECTOR_RAW) ==
              (stride == 2336u ? KUI_GAME_UNSUPPORTED : KUI_GAME_OK));
        if(stride == 2448u) {
            CHECK(kui_game_image_read(&image, 0, 1, KUI_GAME_SECTOR_RAW, out, sizeof(out)) == KUI_GAME_OK);
            CHECK(!memcmp(out, sector, 2352u));
        }
    }
    static const struct { const char *text; enum kui_game_result result; } bad[] = {
        {"FILE data.bin BINARY\nTRACK 01 MODE2/2352\n", KUI_GAME_SYNTAX},
        {"FILE ../data.bin BINARY\n", KUI_GAME_SYNTAX},
        {"FILE data.bin WAVE\n", KUI_GAME_UNSUPPORTED},
        {"FILE data.bin BINARY\nTRACK 01 MODE2/2324\nINDEX 01 00:00:00\n", KUI_GAME_UNSUPPORTED},
        {"FILE data.bin BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\nINDEX 01 00:00:00\n", KUI_GAME_SYNTAX},
        {"FILE data.bin BINARY\nTRACK 01 MODE2/2352\nINDEX 00 00:00:02\nINDEX 01 00:00:01\n", KUI_GAME_SYNTAX},
        {"FILE data.bin BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:60:00\n", KUI_GAME_SYNTAX},
        {"FILE data.bin BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:75\n", KUI_GAME_SYNTAX},
        {"FILE data.bin BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:03\n", KUI_GAME_FILE_SIZE},
        {"FILE data.bin BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\nTRACK 02 MODE2/2352\nINDEX 01 00:00:00\n", KUI_GAME_OVERLAP},
        {"REM SESSION 03\n", KUI_GAME_UNSUPPORTED},
        {"REM KUI SCRAMBLED 2\n", KUI_GAME_SYNTAX}
    };
    for(unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        memset(&fixture, 0, sizeof(fixture));
        add_file(&fixture, "data.bin", 2);
        add_content(&fixture, "bad.cue", (const unsigned char *)bad[i].text, strlen(bad[i].text));
        memset(&image, 0xa5, sizeof(image));
        CHECK(named(&fixture, "bad.cue", &image) == bad[i].result);
        CHECK(all_is(&image, sizeof(image), 0xa5));
    }
}

static void cue_sessions(void) {
    static const struct { const char *cue; uint32_t base, audio_end; bool shared; } cases[] = {
        {"REM SESSION 1\nFILE audio.bin BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\nREM SESSION 2\nFILE data.bin BINARY\nTRACK 02 MODE1/2048\nINDEX 01 00:00:00\n", 11700, 450, false},
        {"REM SESSION 1\nFILE audio.bin BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\nREM LEAD-OUT 01:30:00\nREM SESSION 2\nREM LEAD-IN 01:00:00\nREM PREGAP 00:02:00\nFILE data.bin BINARY\nTRACK 02 MODE1/2048\nINDEX 01 00:00:00\n", 11850, 450, false},
        {"REM SINGLE-DENSITY AREA\nFILE audio.bin BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\nREM HIGH-DENSITY AREA\nFILE data.bin BINARY\nTRACK 02 MODE1/2048\nINDEX 01 00:00:00\n", 45000, 450, false},
        {"REM SESSION 1\nFILE audio.bin BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\nREM LEAD-OUT 00:06:00\nREM SESSION 2\nTRACK 02 MODE1/2352\nINDEX 01 02:36:00\n", 11700, 450, true},
        {"FILE audio.bin BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\nFILE data.bin BINARY\nTRACK 02 MODE1/2048\nPREGAP 09:54:00\nINDEX 01 00:00:00\n", 45000, 450, false}
    };
    for(unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        struct fixture fixture = {0};
        struct kui_game_image image;
        add_file(&fixture, "audio.bin", cases[i].shared ? 11704u : 450u);
        add_file(&fixture, "data.bin", 4u);
        fixture.files[1].bytes = 4u * 2048u;
        add_content(&fixture, "game.cue", (const unsigned char *)cases[i].cue, strlen(cases[i].cue));
        CHECK(named(&fixture, "game.cue", &image) == KUI_GAME_OK);
        CHECK(image.data_lba == cases[i].base && image.tracks[0].end_lba == cases[i].audio_end);
        CHECK(image.cd_image == (i != 2u));
        CHECK(image.tracks[1].start_lba == cases[i].base && image.tracks[1].end_lba == cases[i].base + 4u);
        CHECK(kui_game_image_check(&image, 450u, 1u, KUI_GAME_SECTOR_MODE1) == KUI_GAME_GAP);
        if(cases[i].shared) CHECK(image.tracks[1].file_offset == 11700u * 2352u);
    }
    static const char img[] = "REM SESSION 1\nFILE disc.img BINARY\nTRACK 01 AUDIO\nINDEX 01 00:00:00\nREM LEAD-OUT 00:06:00\nREM SESSION 2\nTRACK 02 MODE1/2352\nINDEX 01 02:36:00\n";
    struct fixture fixture = {0};
    struct kui_game_image image;
    add_file(&fixture, "disc.img", 11704u);
    add_content(&fixture, "unsafe.cue", (const unsigned char *)img, sizeof(img) - 1u);
    memset(&image, 0xa5, sizeof(image));
    CHECK(named(&fixture, "unsafe.cue", &image) == KUI_GAME_UNSUPPORTED);
    CHECK(all_is(&image, sizeof(image), 0xa5));

}

static void singles(void) {
    unsigned char iso[64u * 2048u] = {0}, raw[2u * 2352u], out[2048];
    unsigned char *pvd = iso + 16u * 2048u;
    pvd[0] = 1; memcpy(pvd + 1, "CD001", 5); pvd[6] = 1;
    unsigned char *root = pvd + 156u;
    root[0] = 34; both32_test(root + 2u, 45020u); both32_test(root + 10u, 2048u);
    root[25] = 2; root[28] = root[31] = 1; root[32] = 1;
    memcpy(iso + 20u * 2048u, root, 34);
    struct fixture fixture = {0};
    struct kui_game_image image;
    add_content(&fixture, "test.iso", iso, sizeof(iso));
    CHECK(named(&fixture, "test.iso", &image) == KUI_GAME_OK);
    CHECK(image.format == KUI_GAME_IMAGE_ISO && image.data_lba == 45000 && !image.scrambled && !image.cd_image);
    CHECK(image.tracks[0].start_lba == 45000u && image.tracks[0].end_lba == 45064u);
    CHECK(kui_game_image_read(&image, 45016u, 1, KUI_GAME_SECTOR_MODE1, out, sizeof(out)) == KUI_GAME_OK);
    CHECK(!memcmp(out, pvd, sizeof(out)));
    both32_test(root + 2u, 11720u); memcpy(iso + 20u * 2048u, root, 34);
    CHECK(named(&fixture, "test.iso", &image) == KUI_GAME_OK && image.data_lba == 11700u && image.cd_image);
    both32_test(root + 2u, 20u); memcpy(iso + 20u * 2048u, root, 34);
    CHECK(named(&fixture, "test.iso", &image) == KUI_GAME_OK && image.data_lba == 0);
    iso[20u * 2048u] = 0;
    memset(&image, 0xa5, sizeof(image));
    CHECK(named(&fixture, "test.iso", &image) == KUI_GAME_UNSUPPORTED);
    CHECK(all_is(&image, sizeof(image), 0xa5));
    raw_sector(raw, 2352u, 1u, 15u); raw_sector(raw + 2352u, 2352u, 1u, 16u);
    memset(&fixture, 0, sizeof(fixture));
    add_content(&fixture, "test.img", raw, sizeof(raw));
    CHECK(named(&fixture, "test.img", &image) == KUI_GAME_OK);
    CHECK(image.format == KUI_GAME_IMAGE_RAW && image.data_lba == 0 && image.tracks[0].end_lba == 2u);
    CHECK(kui_game_image_read(&image, 1u, 1, KUI_GAME_SECTOR_MODE1, out, sizeof(out)) == KUI_GAME_OK);
    CHECK(!memcmp(out, raw + 2352u + 16u, sizeof(out)));
    raw[12] = 0x10; raw[13] = 0x02;
    CHECK(named(&fixture, "test.img", &image) == KUI_GAME_OK && image.data_lba == 45000u);
    raw[12] = 0xfa;
    CHECK(named(&fixture, "test.img", &image) == KUI_GAME_MODE);
    raw[12] = 0;
    fixture.files[0].bytes--;
    CHECK(named(&fixture, "test.img", &image) == KUI_GAME_FILE_SIZE);
    CHECK(!kui_game_image_name_supported("../game.cdi"));
    CHECK(!kui_game_image_name_supported("game.chd"));
    CHECK(kui_game_image_name_supported("Game.CDI"));
    CHECK(!strcmp(kui_game_image_format_name(KUI_GAME_IMAGE_CDI), "CDI"));
    CHECK(named(&fixture, "game.chd", &image) == KUI_GAME_UNSUPPORTED);
}
static size_t cdi_track(unsigned char *out, unsigned version, uint32_t lba,
                         unsigned mode, unsigned code) {
    static const unsigned char marker[20] = {
        0,0,1,0,0,0,255,255,255,255,0,0,1,0,0,0,255,255,255,255
    };
    size_t at = 0;
    memset(out, 0, 256);
    at += 4; memcpy(out + at, marker, 20); at += 20;
    at += 4; out[at++] = 0; at += 19; at += 4; at += 2;
    put32(out + at, 1u); put32(out + at + 4u, 2u);
    put32(out + at + 14u, mode); put32(out + at + 30u, lba + 149u);
    put32(out + at + 34u, 3u); put32(out + at + 54u, code);
    at += 87u;
    if(version != 4u) at += 9u;
    return at;
}
static void cdi_containers(void) {
    for(unsigned version = 4; version <= 6; ++version) {
        unsigned char container[32768] = {0}, out[2048];
        const unsigned payload = 3u * 2352u + 3u * 2336u;
        size_t at = payload;
        put16(container + at, 2); at += 2;
        put16(container + at, 1); at += 2;
        size_t marker1 = at + 4u;
        at += cdi_track(container + at, version, 0, 0, 2);
        at += version == 4u ? 12u : 13u;
        put16(container + at, 1); at += 2;
        at += cdi_track(container + at, version, 11700, 2, 1);
        at += version == 4u ? 12u : 13u;
        put32(container + at, 0x80000000u | version);
        put32(container + at + 4u, version == 6u ? (uint32_t)(at + 8u - payload) : payload);
        at += 8u;
        raw_sector(container + 3u * 2352u + 2336u, 2336u, 2u, 23u);
        raw_sector(container + 3u * 2352u + 2u * 2336u, 2336u, 2u, 24u);
        struct fixture fixture = {0};
        struct kui_game_image image;
        add_content(&fixture, "test.cdi", container, at);
        CHECK(named(&fixture, "test.cdi", &image) == KUI_GAME_OK);
        CHECK(image.format == KUI_GAME_IMAGE_CDI && image.count == 2 && image.data_lba == 11700 && !image.scrambled && image.cd_image);
        CHECK(image.tracks[0].file_offset == 2352u && image.tracks[0].end_lba == 2u);
        CHECK(image.tracks[1].file_offset == 3u * 2352u + 2336u && image.tracks[1].sector_bytes == 2336u);
        CHECK(image.tracks[1].file_bytes == at);
        CHECK(kui_game_image_read(&image, 11700u, 1, KUI_GAME_SECTOR_MODE1, out, sizeof(out)) == KUI_GAME_OK);
        CHECK(!memcmp(out, container + image.tracks[1].file_offset + 8u, sizeof(out)));
        container[marker1] = 1;
        memset(&image, 0xa5, sizeof(image));
        CHECK(named(&fixture, "test.cdi", &image) == KUI_GAME_SYNTAX);
        CHECK(all_is(&image, sizeof(image), 0xa5));
        container[marker1] = 0;
        fixture.files[0].bytes = at - 1u;
        CHECK(named(&fixture, "test.cdi", &image) != KUI_GAME_OK);
        CHECK(all_is(&image, sizeof(image), 0xa5));
        fixture.files[0].bytes = at;
        fixture.reads = 0; fixture.fail_read = 3; fixture.failure = KUI_GAME_CANCELLED;
        CHECK(named(&fixture, "test.cdi", &image) == KUI_GAME_CANCELLED);
        CHECK(all_is(&image, sizeof(image), 0xa5));
    }
}


static void cdi_sector_codes_and_pregap(void) {
    const unsigned pregap = 150u, total = 152u;
    for(unsigned code = 0; code <= 4u; ++code) {
        unsigned stride = code == 0u ? 2048u : code == 1u ? 2336u :
                          code == 2u ? 2352u : 2448u;
        unsigned mode = code == 0u ? 1u : 2u;
        size_t payload = (size_t)total * stride;
        unsigned char *container = calloc(1, payload + 512u);
        CHECK(container != NULL);
        size_t at = payload;
        put16(container + at, 1u); at += 2u;
        put16(container + at, 1u); at += 2u;
        unsigned char *description = container + at;
        at += cdi_track(description, 6u, 0u, mode, code);
        put32(description + 54u, pregap);
        put32(description + 84u, 0u); /* CDI pregap starts at FAD0. */
        put32(description + 88u, total);
        at += 13u;
        put32(container + at, 0x80000006u);
        put32(container + at + 4u, (uint32_t)(at + 8u - payload));
        at += 8u;
        if(code == 0u) memset(container + (size_t)pregap * stride, 42, 2048u);
        else raw_sector(container + (size_t)pregap * stride, stride, mode, 42u);
        struct fixture fixture = {0};
        struct kui_game_image image;
        add_content(&fixture, "pregap.cdi", container, at);
        CHECK(named(&fixture, "pregap.cdi", &image) ==
              (code == 3u ? KUI_GAME_UNSUPPORTED : KUI_GAME_OK));
        if(code != 3u) {
            CHECK(image.data_lba == 0u && image.tracks[0].file_offset == (size_t)pregap * stride);
            CHECK(image.tracks[0].sector_bytes == stride && image.tracks[0].end_lba == 2u);
            unsigned char out[2048];
            CHECK(kui_game_image_read(&image, 0u, 1u, KUI_GAME_SECTOR_MODE1, out, sizeof(out)) == KUI_GAME_OK);
            CHECK(!memcmp(out, container + image.tracks[0].file_offset + image.tracks[0].data_offset, sizeof(out)));
        }
        free(container);
    }
}

static void discovery_layout(void) {
    struct kui_game_image image;
    memset(&image,0xa5,sizeof(image));
    CHECK(kui_game_gdi_layout(descriptor,sizeof(descriptor)-1u,&image)==KUI_GAME_OK);
    CHECK(image.count==4u && !image.files.stat && !image.files.read && !image.files.ctx);
    CHECK(image.tracks[2].start_lba==45000u && image.tracks[2].control==4u &&
        image.tracks[2].sector_bytes==2352u && image.tracks[2].data_offset==16u);
    CHECK(!strcmp(image.tracks[2].name,"Track 03.bin"));
    for(unsigned i=0;i<image.count;++i)
        CHECK(image.tracks[i].end_lba==0u && image.tracks[i].file_bytes==0u);
    /* Discovery can pair descriptors before touching potentially large or
     * absent backing tracks. File existence/length belongs to inspection. */
    const char cooked[]="1\n1 45000 4 2048 missing.iso 512\n";
    CHECK(kui_game_gdi_layout(cooked,sizeof(cooked)-1u,&image)==KUI_GAME_OK);
    CHECK(image.tracks[0].sector_bytes==2048u && image.tracks[0].file_offset==512u);
    const char *bad[]={"1\n2 0 4 2352 track.bin 0\n", "1\n1 0 4 2352 ../track.bin 0\n",
        "1\n1 0 4 2352 track.bin 0 junk\n", "2\n1 0 4 2352 track.bin 0\n"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        memset(&image,0xa5,sizeof(image));
        CHECK(kui_game_gdi_layout(bad[i],strlen(bad[i]),&image)!=KUI_GAME_OK);
        CHECK(all_is(&image,sizeof(image),0xa5));
    }
    memset(&image,0xa5,sizeof(image));
    CHECK(kui_game_gdi_layout(NULL,1u,&image)==KUI_GAME_INVALID);
    CHECK(kui_game_gdi_layout(descriptor,0u,&image)==KUI_GAME_INVALID);
    CHECK(kui_game_gdi_layout(descriptor,KUI_GAME_GDI_LIMIT+1u,&image)==KUI_GAME_INVALID);
    CHECK(kui_game_gdi_layout(descriptor,sizeof(descriptor)-1u,NULL)==KUI_GAME_INVALID);
    CHECK(all_is(&image,sizeof(image),0xa5));
}
int main(void) {
    discovery_layout();
    happy_paths();
    preflight();
    syntax();
    file_sizes_and_failures();
    upper_bounds();
    cooked_tracks();
    offsets();
    cue_and_modes();
    cue_sessions();
    singles();
    cdi_containers();
    cdi_sector_codes_and_pregap();
    printf("game image: %u checks passed\n", checks);
    return 0;
}
