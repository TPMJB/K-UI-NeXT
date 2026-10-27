/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_image_subset.h"
#include <string.h>

enum { SUBSET_FAT = 254, SUBSET_ROOT = 255, SUBSET_DIR_FIRST = 253,
       SUBSET_FREE = 0xfffc };

struct subset_reader { const uint8_t *bytes; };

static int subset_read(void *context, uint16_t block, void *out) {
    const struct subset_reader *reader = context;
    if(block >= KUI_VMU_IMAGE_BLOCKS) return -1;
    memcpy(out, reader->bytes + (size_t)block * 512u, 512u);
    return 0;
}

static int subset_overlap(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return an && bn && (x <= y ? y - x < an : x - y < bn);
}

static unsigned subset_le16(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static size_t subset_entry_offset(unsigned slot) {
    return (size_t)(SUBSET_DIR_FIRST - slot / 16u) * 512u +
        (size_t)(slot % 16u) * 32u;
}

int kui_vmu_image_subset(const void *source, size_t source_bytes,
    void *destination, size_t destination_bytes,
    const uint16_t *slots, size_t slot_count,
    struct kui_vmu_image_workspace *work,
    struct kui_vmu_image_summary *summary) {
    if(!source || !destination || !slots || !work || !summary ||
       source_bytes != KUI_VMU_IMAGE_BYTES ||
       destination_bytes != KUI_VMU_IMAGE_BYTES ||
       !slot_count || slot_count > KUI_VMU_IMAGE_DIRECTORY_ENTRIES)
        return KUI_VMU_IMAGE_ARGUMENT;

    const void *read_only[] = {source, slots};
    const size_t read_sizes[] = {source_bytes, slot_count * sizeof(*slots)};
    const void *mutable[] = {destination, work, summary};
    const size_t mutable_sizes[] = {
        destination_bytes, sizeof(*work), sizeof(*summary)
    };
    for(unsigned i = 0; i < 2u; ++i)
        if((uintptr_t)read_only[i] > UINTPTR_MAX - (read_sizes[i] - 1u))
            return KUI_VMU_IMAGE_ARGUMENT;
    for(unsigned i = 0; i < 3u; ++i)
        if((uintptr_t)mutable[i] > UINTPTR_MAX - (mutable_sizes[i] - 1u))
            return KUI_VMU_IMAGE_ARGUMENT;
    /* Check before clearing even summary: it might alias the source. */
    for(unsigned i = 0; i < 3u; ++i) {
        for(unsigned j = 0; j < 2u; ++j)
            if(subset_overlap(mutable[i], mutable_sizes[i],
                              read_only[j], read_sizes[j]))
                return KUI_VMU_IMAGE_ARGUMENT;
        for(unsigned j = 0; j < i; ++j)
            if(subset_overlap(mutable[i], mutable_sizes[i],
                              mutable[j], mutable_sizes[j]))
                return KUI_VMU_IMAGE_ARGUMENT;
    }

    memset(summary, 0, sizeof(*summary));
    const uint8_t *input = source;
    uint8_t *output = destination;
    struct subset_reader reader = {input};
    struct kui_vmu_image_io io = {&reader, subset_read, NULL, NULL};
    struct kui_vmu_image_summary admitted;
    int result = kui_vmu_image_validate(&io, source_bytes, work, &admitted);
    if(result) return result;

    uint8_t chosen[KUI_VMU_IMAGE_DIRECTORY_ENTRIES] = {0};
    struct kui_vmu_image_summary expected = {0, 0, 0, 0};
    memset(work->used, 0, sizeof(work->used));
    for(size_t i = 0; i < slot_count; ++i) {
        unsigned slot = slots[i];
        if(slot >= KUI_VMU_IMAGE_DIRECTORY_ENTRIES || chosen[slot])
            return KUI_VMU_IMAGE_SUBSET_SELECTION;
        const uint8_t *entry = input + subset_entry_offset(slot);
        if(!entry[0]) return KUI_VMU_IMAGE_SUBSET_SELECTION;
        chosen[slot] = 1;
        ++expected.files;
        if(entry[0] == 0xcc) ++expected.game_files;
        unsigned blocks = subset_le16(entry + 24);
        unsigned at = subset_le16(entry + 2);
        /* Full admission above proves every traversed block is in 0..199,
         * distinct, and belongs only to this occupied directory entry. */
        for(unsigned n = 0; n < blocks; ++n) {
            work->used[at] = 1;
            ++expected.used_blocks;
            at = subset_le16(work->fat + at * 2u);
        }
    }
    expected.free_blocks = KUI_VMU_IMAGE_USER_BLOCKS - expected.used_blocks;

    /* Only a fully admitted, explicitly selected source reaches mutation. */
    memset(output, 0, destination_bytes);
    uint8_t *fat = output + SUBSET_FAT * 512u;
    memcpy(fat, work->fat, 512u);
    for(unsigned block = 0; block < KUI_VMU_IMAGE_USER_BLOCKS; ++block) {
        if(work->used[block])
            memcpy(output + block * 512u, input + block * 512u, 512u);
        else {
            fat[block * 2u] = (uint8_t)SUBSET_FREE;
            fat[block * 2u + 1u] = (uint8_t)(SUBSET_FREE >> 8);
        }
    }
    for(unsigned slot = 0; slot < KUI_VMU_IMAGE_DIRECTORY_ENTRIES; ++slot) {
        if(chosen[slot]) {
            size_t offset = subset_entry_offset(slot);
            memcpy(output + offset, input + offset, 32u);
        }
    }
    memcpy(output + SUBSET_ROOT * 512u, input + SUBSET_ROOT * 512u, 512u);

    reader.bytes = output;
    struct kui_vmu_image_summary verified;
    result = kui_vmu_image_validate(&io, destination_bytes, work, &verified);
    if(result || verified.files != expected.files ||
       verified.game_files != expected.game_files ||
       verified.used_blocks != expected.used_blocks ||
       verified.free_blocks != expected.free_blocks)
        return KUI_VMU_IMAGE_VERIFY;
    *summary = verified;
    return KUI_VMU_IMAGE_OK;
}
