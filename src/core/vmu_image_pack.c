/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_image_pack.h"
#include <string.h>

enum { FAT = 254, DIR_FIRST = 253, END = 0xfffa };

struct span {
    uintptr_t begin, end;
};

static unsigned le16(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static void put16(uint8_t *p, unsigned value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static int span_for(const void *p, size_t bytes, struct span *span) {
    uintptr_t begin = (uintptr_t)p;
    if(!p || bytes > UINTPTR_MAX - begin) return 0;
    span->begin = begin;
    span->end = begin + bytes;
    return 1;
}

static int overlap(struct span a, struct span b) {
    return a.begin < b.end && b.begin < a.end;
}

/* Keep name interpretation identical to production admission. The completed
 * card is also passed through that validator before success is returned. */
static int name_key(const uint8_t *name, uint8_t key[12]) {
    unsigned bytes = 12;
    while(bytes && (!name[bytes - 1] || name[bytes - 1] == ' ')) --bytes;
    if(!bytes) return 0;
    for(unsigned i = 0; i < bytes; ++i)
        if(name[i] < 32 || name[i] == 127) return 0;
    memset(key, 0, 12);
    memcpy(key, name, bytes);
    return 1;
}

static int prevalidate(const struct kui_vmu_image_file *files, size_t count,
    void *destination, size_t bytes, struct kui_vmu_image_workspace *work,
    struct kui_vmu_image_summary *summary) {
    struct span outputs[3], descriptors;
    unsigned blocks_used = 0, games = 0;
    if(!files || !count || count > KUI_VMU_IMAGE_USER_BLOCKS ||
       bytes != KUI_VMU_IMAGE_BYTES ||
       !span_for(destination, bytes, outputs) ||
       !span_for(work, sizeof(*work), outputs + 1) ||
       !span_for(summary, sizeof(*summary), outputs + 2) ||
       !span_for(files, count * sizeof(*files), &descriptors))
        return KUI_VMU_IMAGE_ARGUMENT;
    for(unsigned i = 0; i < 3; ++i) {
        if(overlap(outputs[i], descriptors)) return KUI_VMU_IMAGE_ARGUMENT;
        for(unsigned j = 0; j < i; ++j)
            if(overlap(outputs[i], outputs[j])) return KUI_VMU_IMAGE_ARGUMENT;
    }
    /* This pass is entirely read-only, including all caller output buffers.
     * Pairwise name checking keeps preparation off the caller's workspace. */
    for(size_t i = 0; i < count; ++i) {
        const uint8_t *entry = files[i].entry;
        unsigned blocks = le16(entry + 24);
        uint8_t key[12];
        struct span payload;
        /* The copy-protect byte is copied as written, whatever its value. */
        if((entry[0] != 0x33 && entry[0] != 0xcc) || !blocks ||
           blocks > KUI_VMU_IMAGE_USER_BLOCKS ||
           le16(entry + 26) >= blocks ||
           entry[28] || entry[29] || entry[30] || entry[31] ||
           files[i].bytes != (size_t)blocks * KUI_VMU_IMAGE_BLOCK_BYTES ||
           !name_key(entry + 4, key))
            return KUI_VMU_IMAGE_FILESYSTEM;
        if(entry[0] == 0xcc && ++games > 1)
            return KUI_VMU_IMAGE_FILESYSTEM;
        blocks_used += blocks;
        if(blocks_used > KUI_VMU_IMAGE_USER_BLOCKS)
            return KUI_VMU_IMAGE_FILESYSTEM;
        if(!span_for(files[i].payload, files[i].bytes, &payload))
            return KUI_VMU_IMAGE_ARGUMENT;
        for(unsigned output = 0; output < 3; ++output)
            if(overlap(outputs[output], payload))
                return KUI_VMU_IMAGE_ARGUMENT;
        for(size_t j = 0; j < i; ++j) {
            uint8_t previous[12];
            if(!name_key(files[j].entry + 4, previous) ||
               !memcmp(key, previous, sizeof(key)))
                return KUI_VMU_IMAGE_FILESYSTEM;
        }
    }
    return KUI_VMU_IMAGE_OK;
}

static int read_block(void *context, uint16_t block, void *data) {
    const uint8_t *image = context;
    if(block >= KUI_VMU_IMAGE_BLOCKS) return -1;
    memcpy(data, image + (size_t)block * KUI_VMU_IMAGE_BLOCK_BYTES,
        KUI_VMU_IMAGE_BLOCK_BYTES);
    return 0;
}

int kui_vmu_image_pack(const struct kui_vmu_image_file *files, size_t count,
    void *destination, size_t bytes, struct kui_vmu_image_workspace *work,
    struct kui_vmu_image_summary *summary) {
    int result = prevalidate(files, count, destination, bytes, work, summary);
    if(result) return result;

    uint8_t *image = destination;
    uint8_t *fat = image + FAT * KUI_VMU_IMAGE_BLOCK_BYTES;
    unsigned next_data = KUI_VMU_IMAGE_USER_BLOCKS;
    for(unsigned block = 0; block < KUI_VMU_IMAGE_BLOCKS; ++block)
        kui_vmu_image_blank_block((uint16_t)block,
            image + (size_t)block * KUI_VMU_IMAGE_BLOCK_BYTES);
    for(size_t i = 0; i < count; ++i) {
        const struct kui_vmu_image_file *file = files + i;
        unsigned blocks = le16(file->entry + 24);
        int game = file->entry[0] == 0xcc;
        unsigned first = game ? 0 : next_data - 1;
        uint8_t *entry = image +
            (DIR_FIRST - i / 16) * KUI_VMU_IMAGE_BLOCK_BYTES + (i % 16) * 32;
        memcpy(entry, file->entry, 32);
        put16(entry + 2, first);
        for(unsigned n = 0; n < blocks; ++n) {
            unsigned at = game ? n : first - n;
            unsigned next = n + 1 == blocks ? END : (game ? at + 1 : at - 1);
            memcpy(image + (size_t)at * KUI_VMU_IMAGE_BLOCK_BYTES,
                (const uint8_t *)file->payload +
                    (size_t)n * KUI_VMU_IMAGE_BLOCK_BYTES,
                KUI_VMU_IMAGE_BLOCK_BYTES);
            put16(fat + at * 2, next);
        }
        if(!game) next_data -= blocks;
    }
    struct kui_vmu_image_summary verified;
    const struct kui_vmu_image_io io = {image, read_block, NULL, NULL};
    if(kui_vmu_image_validate(&io, bytes, work, &verified))
        return KUI_VMU_IMAGE_VERIFY;
    *summary = verified;
    return KUI_VMU_IMAGE_OK;
}
