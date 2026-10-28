/* SPDX-License-Identifier: GPL-3.0-only */
/* Independently checked filesystem fixtures. No owner save data is embedded. */
#include "kui/vmu_image_subset.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { BYTES = 131072, BLOCK = 512, FREE = 0xfffc, END = 0xfffa };
static _Alignas(32) uint8_t source[BYTES], destination[BYTES];
static uint8_t before[BYTES], expected[BYTES];
static struct kui_vmu_image_workspace work;
static struct kui_vmu_image_summary summary;

static unsigned get16(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}
static void put16(uint8_t *p, unsigned value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}
static uint8_t *entry(uint8_t *image, unsigned slot) {
    assert(slot < 208u);
    return image + (253u - slot / 16u) * 512u + (slot % 16u) * 32u;
}
static int read_image(void *context, uint16_t block, void *out) {
    assert(block < 256u);
    memcpy(out, (const uint8_t *)context + block * 512u, 512u);
    return 0;
}
static void validate(const uint8_t *image, unsigned files, unsigned blocks,
                     unsigned games) {
    struct kui_vmu_image_workspace scratch;
    struct kui_vmu_image_summary result;
    struct kui_vmu_image_io io = {(void *)image, read_image, NULL, NULL};
    assert(!kui_vmu_image_validate(&io, BYTES, &scratch, &result));
    assert(result.files == files && result.used_blocks == blocks);
    assert(result.free_blocks == 200u - blocks && result.game_files == games);
}
static void reset(void) {
    for(unsigned b = 0; b < 256u; ++b)
        assert(!kui_vmu_image_blank_block((uint16_t)b, source + b * 512u));
    /* Dirty unused data and inactive entries must not leak to the new card. */
    for(unsigned b = 0; b < 241u; ++b)
        for(unsigned p = 0; p < 512u; ++p)
            source[b * 512u + p] = (uint8_t)(b * 19u + p * 7u + 1u);
    for(unsigned slot = 0; slot < 208u; ++slot) {
        memset(entry(source, slot), 0x96, 28u);
        entry(source, slot)[0] = 0;
    }
    uint8_t *root = source + 255u * 512u;
    for(unsigned p = 17u; p < 512u; ++p) {
        if(p >= 0x46u && p < 0x52u) continue;
        root[p] = (uint8_t)(p * 41u + 0x73u);
    }
    root[16] = 1;
    put16(root + 0x4eu, 123u);
    memset(destination, 0xa5, sizeof(destination));
    memset(&work, 0x5c, sizeof(work));
    summary = (struct kui_vmu_image_summary){999,999,999,999};
}
static void add(unsigned slot, const char *name, const unsigned *chain,
                unsigned count, unsigned game, unsigned header) {
    uint8_t *e = entry(source, slot);
    memset(e, 0, 32u);
    e[0] = game ? 0xcc : 0x33;
    e[1] = 0xff; /* The original protection flag must survive. */
    put16(e + 2, chain[0]);
    assert(strlen(name) <= 12u);
    memcpy(e + 4, name, strlen(name));
    for(unsigned i = 16u; i < 24u; ++i) e[i] = (uint8_t)(slot + i * 13u);
    put16(e + 24, count); put16(e + 26, header);
    for(unsigned i = 0; i < count; ++i) {
        assert(chain[i] < 200u);
        put16(source + 254u * 512u + chain[i] * 2u,
              i + 1u < count ? chain[i + 1u] : END);
        /* Deliberately opaque/unknown packages: prefix, header, CRC, final
         * block slack and every byte are independent of package parsers. */
        for(unsigned p = 0; p < 512u; ++p)
            source[chain[i] * 512u + p] = (uint8_t)(slot * 29u + i * 17u + p);
    }
}
/* Independent expected-byte construction starts from canonical blank blocks,
 * walks source chains and explicitly preserves only selected bytes. */
static unsigned make_expected(const uint16_t *slots, size_t count) {
    for(unsigned b = 0; b < 256u; ++b)
        assert(!kui_vmu_image_blank_block((uint16_t)b, expected + b * 512u));
    memcpy(expected + 255u * 512u, source + 255u * 512u, 512u);
    unsigned retained = 0;
    for(size_t i = 0; i < count; ++i) {
        const uint8_t *e = entry(source, slots[i]);
        memcpy(entry(expected, slots[i]), e, 32u);
        unsigned at = get16(e + 2);
        unsigned blocks = get16(e + 24);
        for(unsigned n = 0; n < blocks; ++n) {
            assert(at < 200u);
            memcpy(expected + at * 512u, source + at * 512u, 512u);
            memcpy(expected + 254u * 512u + at * 2u,
                   source + 254u * 512u + at * 2u, 2u);
            at = get16(source + 254u * 512u + at * 2u);
            ++retained;
        }
        assert(at == END);
    }
    return retained;
}
static void construct(const uint16_t *slots, size_t count, unsigned games) {
    uint16_t original_slots[208];
    assert(count <= 208u);
    memcpy(original_slots, slots, count * sizeof(*slots));
    memcpy(before, source, BYTES);
    unsigned retained = make_expected(slots, count);
    assert(!kui_vmu_image_subset(source, BYTES, destination, BYTES,
                                 slots, count, &work, &summary));
    assert(!memcmp(source, before, BYTES));
    assert(!memcmp(slots, original_slots, count * sizeof(*slots)));
    assert(!memcmp(destination, expected, BYTES));
    assert(summary.files == count && summary.used_blocks == retained);
    assert(summary.free_blocks == 200u - retained && summary.game_files == games);
    validate(destination, (unsigned)count, retained, games);
}

static void related_groups_and_unknown_packages(void) {
    reset();
    const unsigned config[] = {199,50}, save1[] = {77,9,111};
    const unsigned save2[] = {18,82,14,26}, other[] = {104,105};
    const unsigned same_prefix[] = {53};
    add(0, "SETUP.CFG", config, 2, 0, 1);
    add(16, "CHAPTER.001", save1, 3, 0, 0);
    add(207, "OTHER-NAME", save2, 4, 0, 3);
    add(15, "MYSTERY.VMS", other, 2, 0, 1);
    add(200, "CHAPTER.002", same_prefix, 1, 0, 0);
    validate(source, 5, 12, 0);
    const uint16_t group[] = {207,0,16};
    construct(group, 3, 0);
    uint8_t first[BYTES]; memcpy(first, destination, BYTES);
    const uint16_t reordered[] = {16,207,0};
    construct(reordered, 3, 0);
    assert(!memcmp(first, destination, BYTES));
    /* A matching prefix cannot infer an extra save; arbitrary unknown
     * packages can also be selected without CRC/header normalization. */
    const uint16_t unknown[] = {15}; construct(unknown, 1, 0);
}

static void mini_game_prefix(void) {
    reset();
    const unsigned game[] = {0,1,2,3,4}, save[] = {122,33}, removed[] = {49};
    add(15, "MINI-GAME", game, 5, 1, 3);
    add(16, "MINI-DATA", save, 2, 0, 1);
    add(207, "UNRELATED", removed, 1, 0, 0);
    const uint16_t chosen[] = {16,15}; construct(chosen, 2, 1);
    assert(!memcmp(destination, source, 5u * 512u));
    assert(get16(entry(destination, 15) + 26) == 3u);
    const uint16_t data_only[] = {16}; construct(data_only, 1, 0);
    for(unsigned p = 0; p < 5u * 512u; ++p) assert(!destination[p]);
}

static void directory_and_capacity_boundaries(void) {
    /* Every directory slot is selected at least once, including 15/16/207. */
    for(unsigned slot = 0; slot < 208u; ++slot) {
        reset(); const unsigned chain[] = {199,50,3};
        add(slot, "BOUNDARY", chain, 3, 0, 2);
        uint16_t chosen = (uint16_t)slot; construct(&chosen, 1, 0);
    }
    reset(); unsigned full[200];
    for(unsigned i = 0; i < 200u; ++i) full[i] = 199u - i;
    add(207, "FULL-CARD", full, 200, 0, 199);
    uint16_t chosen = 207; construct(&chosen, 1, 0);
    assert(summary.free_blocks == 0);
    reset(); uint16_t all[200];
    for(unsigned i = 0; i < 200u; ++i) {
        char name[13]; (void)snprintf(name, sizeof(name), "FILE%03u", i);
        add(i + 8u, name, &i, 1, 0, 0); all[i] = (uint16_t)(i + 8u);
    }
    construct(all, 200, 0);
    assert(summary.files == 200u && summary.used_blocks == 200u);
}

static void failure(int result, const uint16_t *slots, size_t count) {
    memcpy(before, source, BYTES);
    memset(destination, 0xa5, BYTES);
    summary = (struct kui_vmu_image_summary){999,999,999,999};
    assert(kui_vmu_image_subset(source, BYTES, destination, BYTES,
                                slots, count, &work, &summary) == result);
    assert(!memcmp(source, before, BYTES));
    for(unsigned p = 0; p < BYTES; ++p) assert(destination[p] == 0xa5);
    assert(!summary.files && !summary.used_blocks && !summary.free_blocks &&
           !summary.game_files);
}
static void small_fixture(void) {
    reset();
    const unsigned chosen[] = {199,50,3}, unchosen[] = {14,72};
    add(0, "KEEP", chosen, 3, 0, 1);
    add(207, "REMOVE", unchosen, 2, 0, 0);
}
static void strict_admission_and_selection(void) {
    const uint16_t selected[] = {0};
    const uint16_t invalid[][2] = {{208,0},{65535,0},{0,0},{1,0},{0,208},{0,1}};
    for(unsigned i = 0; i < 6u; ++i) {
        size_t count = i == 2u || i >= 4u ? 2u : 1u;
        small_fixture(); failure(KUI_VMU_IMAGE_SUBSET_SELECTION, invalid[i], count);
    }
    /* Names, copy-protect bytes and entry bytes 0x1c-0x1f are what games
     * wrote: a card holding odd ones still admits, and its subset keeps them. */
    for(unsigned mutation = 0; mutation < 4u; ++mutation) {
        small_fixture(); uint8_t *e = entry(source, 207);
        switch(mutation) {
        case 0: memcpy(e + 4, entry(source, 0) + 4, 12u); break; /* duplicate name */
        case 1: e[1] = 1; entry(source, 0)[1] = 0x10; break;
        case 2: entry(source, 206)[31] = 1; entry(source, 0)[28] = 0xa5; break;
        case 3: e[4] = 0x1f; entry(source, 0)[5] = 0x7f; break;
        default: assert(0);
        }
        construct(selected, 1, 0);
        assert(!memcmp(entry(destination, 0), entry(source, 0), 32u));
    }
    /* Malformed structure outside the selected file still blocks admission. */
    for(unsigned mutation = 0; mutation < 16u; ++mutation) {
        if(mutation == 6u || mutation == 7u || mutation == 9u || mutation == 14u) continue;
        small_fixture(); uint8_t *e = entry(source, 207);
        uint8_t *fat = source + 254u * 512u;
        int result = KUI_VMU_IMAGE_FILESYSTEM;
        switch(mutation) {
        case 0: put16(fat + 72u * 2u, 14); break; /* cycle */
        case 1: put16(e + 24, 1); break; /* length mismatch */
        case 2: put16(e + 2, 200); break;
        case 3: put16(fat + 14u * 2u, FREE); break;
        case 4: put16(fat + 88u * 2u, END); break; /* orphan */
        case 5: put16(e + 2, 199); break; /* shared chain */
        case 6: memcpy(e + 4, entry(source, 0) + 4, 12u); break;
        case 7: e[1] = 1; break;
        case 8: put16(e + 26, 2); break;
        case 9: entry(source, 206)[31] = 1; break; /* inactive trailer */
        case 10: e[0] = 0xcc; break; /* invalid game block prefix */
        case 11: put16(fat + 200u * 2u, END); break;
        case 12: put16(source + 255u * 512u + 0x50u, 201); result = KUI_VMU_IMAGE_GEOMETRY; break;
        case 13: source[255u * 512u] = 0; result = KUI_VMU_IMAGE_GEOMETRY; break;
        case 14: e[4] = 0x1f; break;
        case 15: put16(e + 24, 0); break;
        default: assert(0);
        }
        failure(result, selected, 1);
    }
    reset(); failure(KUI_VMU_IMAGE_SUBSET_SELECTION, selected, 1);
}

static void arguments_and_overlaps(void) {
    small_fixture(); const uint16_t chosen[] = {0};
    memcpy(before, source, BYTES);
    memset(destination, 0xa5, BYTES);
    struct kui_vmu_image_workspace original_work = work;
    struct kui_vmu_image_summary original_summary = summary;
#define BAD(src, sn, dst, dn, slots, count, wk, sm) \
    assert(kui_vmu_image_subset((src),(sn),(dst),(dn),(slots),(count),(wk),(sm)) == KUI_VMU_IMAGE_ARGUMENT)
    BAD(NULL,BYTES,destination,BYTES,chosen,1,&work,&summary);
    BAD(source,BYTES,NULL,BYTES,chosen,1,&work,&summary);
    BAD(source,BYTES,destination,BYTES,NULL,1,&work,&summary);
    BAD(source,BYTES,destination,BYTES,chosen,0,&work,&summary);
    BAD(source,BYTES,destination,BYTES,chosen,209,&work,&summary);
    BAD(source,BYTES,destination,BYTES,chosen,1,NULL,&summary);
    BAD(source,BYTES,destination,BYTES,chosen,1,&work,NULL);
    const size_t bad_sizes[] = {0, BYTES-1u, BYTES+1u, SIZE_MAX};
    for(unsigned i = 0; i < sizeof(bad_sizes)/sizeof(*bad_sizes); ++i) {
        BAD(source,bad_sizes[i],destination,BYTES,chosen,1,&work,&summary);
        BAD(source,BYTES,destination,bad_sizes[i],chosen,1,&work,&summary);
    }
    BAD(source,BYTES,source,BYTES,chosen,1,&work,&summary);
    BAD(source,BYTES,destination,BYTES,chosen,1,
        (struct kui_vmu_image_workspace *)(void *)source,&summary);
    BAD(source,BYTES,destination,BYTES,chosen,1,&work,
        (struct kui_vmu_image_summary *)(void *)source);
    BAD(source,BYTES,destination,BYTES,chosen,1,
        (struct kui_vmu_image_workspace *)(void *)destination,&summary);
    BAD(source,BYTES,destination,BYTES,chosen,1,&work,
        (struct kui_vmu_image_summary *)(void *)destination);
    BAD(source,BYTES,destination,BYTES,(const uint16_t *)(const void *)destination,1,&work,&summary);
    BAD(source,BYTES,destination,BYTES,(const uint16_t *)(const void *)&work,1,&work,&summary);
    BAD(source,BYTES,destination,BYTES,(const uint16_t *)(const void *)&summary,1,&work,&summary);
    BAD(source,BYTES,destination,BYTES,chosen,1,&work,
        (struct kui_vmu_image_summary *)(void *)&work);
    BAD(source,BYTES,destination,BYTES,chosen,1,
        (struct kui_vmu_image_workspace *)(void *)(source+BYTES-32u),&summary);
    BAD(source,BYTES,destination,BYTES,chosen,1,
        (struct kui_vmu_image_workspace *)(void *)(destination+BYTES-32u),&summary);
    BAD(source,BYTES,destination,BYTES,
        (const uint16_t *)(const void *)(destination+BYTES-2u),2,&work,&summary);
    BAD(source,BYTES,destination,BYTES,chosen,1,&work,
        (struct kui_vmu_image_summary *)(void *)((uint8_t *)&work+sizeof(work)-4u));
    BAD((const void *)(UINTPTR_MAX-5u),BYTES,destination,BYTES,chosen,1,&work,&summary);
    BAD(source,BYTES,(void *)(UINTPTR_MAX-5u),BYTES,chosen,1,&work,&summary);
    BAD(source,BYTES,destination,BYTES,(const uint16_t *)(UINTPTR_MAX-1u),2,&work,&summary);
    BAD(source,BYTES,destination,BYTES,chosen,1,
        (struct kui_vmu_image_workspace *)(UINTPTR_MAX-1u),&summary);
    BAD(source,BYTES,destination,BYTES,chosen,1,&work,
        (struct kui_vmu_image_summary *)(UINTPTR_MAX-1u));
    assert(!memcmp(before, source, BYTES));
    for(unsigned p = 0; p < BYTES; ++p) assert(destination[p] == 0xa5);
    assert(!memcmp(&work, &original_work, sizeof(work)));
    assert(!memcmp(&summary, &original_summary, sizeof(summary)));

    static _Alignas(32) uint8_t arena[3u * BYTES];
    static uint8_t arena_before[sizeof(arena)];
    uint8_t *middle = arena + BYTES;
    memset(arena, 0x34, sizeof(arena));
    memcpy(middle, source, BYTES);
    memcpy(arena_before, arena, sizeof(arena));
    const int offsets[] = {-BYTES+1,-1024,-1,0,1,1024,BYTES-1};
    for(unsigned i = 0; i < sizeof(offsets)/sizeof(*offsets); ++i) {
        BAD(middle,BYTES,middle+offsets[i],BYTES,chosen,1,&work,&summary);
        assert(!memcmp(arena, arena_before, sizeof(arena)));
    }
    /* Touching boundaries are disjoint, and therefore valid. */
    assert(!kui_vmu_image_subset(middle,BYTES,arena,BYTES,chosen,1,&work,&summary));
    assert(!memcmp(middle, source, BYTES));
    assert(!kui_vmu_image_subset(middle,BYTES,middle+BYTES,BYTES,chosen,1,&work,&summary));
    assert(!memcmp(middle, source, BYTES));
    /* Read-only selection may reside inside a source free/reserved block. */
    static uint16_t source_with_slots[BYTES / sizeof(uint16_t)];
    memcpy(source_with_slots, source, BYTES);
    uint16_t *embedded = source_with_slots + 200u * 256u;
    *embedded = 0;
    memcpy(before, source_with_slots, BYTES);
    assert(!kui_vmu_image_subset(source_with_slots,BYTES,destination,BYTES,
                                 embedded,1,&work,&summary));
    assert(!memcmp(before, source_with_slots, BYTES));
    validate(destination, 1, 3, 0);
#undef BAD
}

int main(void) {
    related_groups_and_unknown_packages();
    mini_game_prefix();
    directory_and_capacity_boundaries();
    strict_admission_and_selection();
    arguments_and_overlaps();
    puts("PASS VMU subset: explicit groups, 208 slots, full payload/root metadata, mini-game prefix, strict admission, immutable source and overlap rejection");
    return 0;
}
