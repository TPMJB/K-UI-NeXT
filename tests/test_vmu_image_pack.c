/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent filesystem fixtures; opaque payloads include arbitrary padding. */
#include "kui/vmu_image_pack.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { BYTES = 131072, BLOCK = 512, PAYLOAD_BYTES = 200 * BLOCK,
       FREE = 0xfffc, END = 0xfffa };
static _Alignas(max_align_t) uint8_t destination[BYTES], payload[PAYLOAD_BYTES];
static uint8_t expected[BYTES], destination_before[BYTES];
static uint8_t payload_before[PAYLOAD_BYTES];
static struct kui_vmu_image_file files[201], files_before[201];
static struct kui_vmu_image_workspace work, work_before;
static struct kui_vmu_image_summary summary, summary_before;

static unsigned get16(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}
static void put16(uint8_t *p, unsigned value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
}
static uint8_t *entry(uint8_t *image, unsigned slot) {
    assert(slot < 208u);
    return image + (253u - slot / 16u) * BLOCK + (slot % 16u) * 32u;
}
static int read_image(void *context, uint16_t block, void *out) {
    assert(block < 256u);
    memcpy(out, (const uint8_t *)context + block * BLOCK, BLOCK);
    return 0;
}
static void reset(void) {
    memset(destination, 0xa5, sizeof(destination));
    memset(&work, 0x5c, sizeof(work));
    memset(&summary, 0x79, sizeof(summary));
    memset(files, 0, sizeof(files));
    for(unsigned i = 0; i < sizeof(payload); ++i)
        payload[i] = (uint8_t)(i * 29u + i / BLOCK * 17u + 3u);
}
static void add(unsigned slot, const char *name, unsigned blocks,
                unsigned game, unsigned header, size_t payload_offset) {
    assert(slot < 201u && strlen(name) <= 12u);
    struct kui_vmu_image_file *file = files + slot;
    file->entry[0] = game ? 0xcc : 0x33;
    file->entry[1] = slot & 1u ? 0 : 0xff;
    /* A source first block carries no allocation constraint for this builder. */
    put16(file->entry + 2, 117u + slot);
    memcpy(file->entry + 4, name, strlen(name));
    for(unsigned i = 16u; i < 24u; ++i)
        file->entry[i] = (uint8_t)(slot * 19u + i * 7u);
    put16(file->entry + 24, blocks);
    put16(file->entry + 26, header);
    file->payload = payload + payload_offset;
    file->bytes = (size_t)blocks * BLOCK;
}
static void remember(void) {
    memcpy(destination_before, destination, sizeof(destination));
    memcpy(payload_before, payload, sizeof(payload));
    memcpy(files_before, files, sizeof(files));
    memcpy(&work_before, &work, sizeof(work));
    memcpy(&summary_before, &summary, sizeof(summary));
}
static void unchanged(void) {
    assert(!memcmp(destination_before, destination, sizeof(destination)));
    assert(!memcmp(payload_before, payload, sizeof(payload)));
    assert(!memcmp(files_before, files, sizeof(files)));
    assert(!memcmp(&work_before, &work, sizeof(work)));
    assert(!memcmp(&summary_before, &summary, sizeof(summary)));
}
static void reject_call(const struct kui_vmu_image_file *input, size_t count,
    void *out, size_t bytes, struct kui_vmu_image_workspace *scratch,
    struct kui_vmu_image_summary *result, int wanted) {
    remember();
    assert(kui_vmu_image_pack(input, count, out, bytes, scratch, result) == wanted);
    unchanged();
}
static void reject(size_t count, int wanted) {
    reject_call(files, count, destination, BYTES, &work, &summary, wanted);
}
/* Independently build the entire expected image, including all zero regions,
 * original directory bytes, descending chains and minigame prefix. */
static void construct(unsigned count, unsigned blocks, unsigned games) {
    for(unsigned b = 0; b < 256u; ++b)
        assert(!kui_vmu_image_blank_block((uint16_t)b, expected + b * BLOCK));
    unsigned high = 199u;
    for(unsigned i = 0; i < count; ++i) {
        const struct kui_vmu_image_file *file = files + i;
        unsigned length = get16(file->entry + 24);
        unsigned first = file->entry[0] == 0xcc ? 0u : high;
        memcpy(entry(expected, i), file->entry, 32u);
        put16(entry(expected, i) + 2, first);
        for(unsigned n = 0; n < length; ++n) {
            unsigned at = file->entry[0] == 0xcc ? n : high--;
            unsigned next = file->entry[0] == 0xcc ? n + 1u : high;
            memcpy(expected + at * BLOCK,
                   (const uint8_t *)file->payload + n * BLOCK, BLOCK);
            put16(expected + 254u * BLOCK + at * 2u,
                  n + 1u == length ? END : next);
        }
    }
    remember();
    assert(!kui_vmu_image_pack(files, count, destination, BYTES, &work, &summary));
    assert(!memcmp(destination, expected, BYTES));
    assert(!memcmp(payload_before, payload, sizeof(payload)));
    assert(!memcmp(files_before, files, sizeof(files)));
    assert(summary.files == count && summary.used_blocks == blocks);
    assert(summary.game_files == games && summary.free_blocks == 200u - blocks);
    struct kui_vmu_image_workspace verify_work;
    struct kui_vmu_image_summary verify_summary;
    struct kui_vmu_image_io io = {destination, read_image, NULL, NULL};
    assert(!kui_vmu_image_validate(&io, BYTES, &verify_work, &verify_summary));
    assert(!memcmp(&summary, &verify_summary, sizeof(summary)));
}
static void preservation(void) {
    reset();
    add(0, "DATA.ONE", 4, 0, 2, 0);
    add(1, "DATA.TWO", 3, 0, 1, 4u * BLOCK);
    add(2, "TWELVE-BYTES", 1, 0, 0, 7u * BLOCK);
    /* Recognizable bytes in an internal CRC position must remain opaque. */
    payload[2u * BLOCK + 0x46u] = 0x12;
    payload[2u * BLOCK + 0x47u] = 0x34;
    construct(3, 8, 0);
    assert(get16(entry(destination, 0) + 2) == 199u);
    assert(get16(entry(destination, 1) + 2) == 195u);
    assert(get16(destination + 254u * BLOCK + 199u * 2u) == 198u);

    /* Read-only inputs may share their payload storage. */
    reset();
    add(0, "REUSED.A", 2, 0, 1, 0);
    add(1, "REUSED.B", 2, 0, 0, 0);
    construct(2, 4, 0);
    /* Payload bytes may even occupy the read-only descriptor array. */
    reset();
    add(0, "DESCRIPTORS", 1, 0, 0, 0);
    files[0].payload = files;
    construct(1, 1, 0);
    reset();
    add(0, "BEFORE.GAME", 2, 0, 1, 0);
    add(1, "MINI.GAME", 5, 1, 3, 2u * BLOCK);
    add(2, "AFTER.GAME", 1, 0, 0, 7u * BLOCK);
    construct(3, 8, 1);
    assert(get16(entry(destination, 1) + 2) == 0u);
    assert(get16(destination + 254u * BLOCK + 0u * 2u) == 1u);
    assert(get16(destination + 254u * BLOCK + 4u * 2u) == END);
    assert(get16(entry(destination, 2) + 2) == 197u);
    /* A copy-protect byte other than 0x00/0xff (Crazy Taxi 2: 0x10) is kept. */
    reset();
    add(0, "C_TAXI02.SYS", 2, 0, 0, 0);
    files[0].entry[1] = 0x10;
    construct(1, 2, 0);
    assert(entry(destination, 0)[1] == 0x10u);
    /* Names, duplicates and entry bytes 0x1c-0x1f are copied as written. */
    reset();
    add(0, "SAME", 1, 0, 0, 0);
    add(1, "SAME", 1, 0, 0, BLOCK);
    add(2, "BAD\x1fNAME\x7f", 1, 0, 0, 2u * BLOCK);
    add(3, "", 1, 0, 0, 3u * BLOCK);
    memset(files[1].entry + 8, ' ', 8);
    for(unsigned i = 28u; i < 32u; ++i) files[2].entry[i] = (uint8_t)(0xa0u + i);
    construct(4, 4, 0);
    assert(!memcmp(entry(destination, 2) + 28, files[2].entry + 28, 4u));
}
static void capacity(void) {
    reset(); add(0, "FULL.DATA", 200, 0, 199, 0); construct(1, 200, 0);
    reset(); add(0, "FULL.MINI", 200, 1, 199, 0); construct(1, 200, 1);
    reset();
    for(unsigned i = 0; i < 200u; ++i) {
        char name[13];
        assert(snprintf(name, sizeof(name), "FILE%03u", i) == 7);
        add(i, name, 1, 0, 0, i * BLOCK);
    }
    construct(200, 200, 0);
    assert(get16(entry(destination, 199) + 2) == 0u);
    assert(!memcmp(entry(destination, 200), (uint8_t[32]){0}, 32u));
    reset();
    add(0, "BIG.MINI", 199, 1, 1, 0);
    add(1, "ONE.DATA", 1, 0, 0, 199u * BLOCK);
    construct(2, 200, 1);
}
static void invalid_files(void) {
    reset(); add(0, "VALID", 2, 0, 1, 0);
    files[0].entry[0] = 0; reject(1, KUI_VMU_IMAGE_FILESYSTEM);
    files[0].entry[0] = 0x34; reject(1, KUI_VMU_IMAGE_FILESYSTEM);
    files[0].entry[0] = 0x33;
    files[0].entry[1] = 0xff;
    put16(files[0].entry + 26, 2); reject(1, KUI_VMU_IMAGE_FILESYSTEM);
    put16(files[0].entry + 26, 1);
    --files[0].bytes; reject(1, KUI_VMU_IMAGE_FILESYSTEM);
    files[0].bytes += 2; reject(1, KUI_VMU_IMAGE_FILESYSTEM);
    --files[0].bytes;
    put16(files[0].entry + 24, 0); reject(1, KUI_VMU_IMAGE_FILESYSTEM);
    put16(files[0].entry + 24, 201); reject(1, KUI_VMU_IMAGE_FILESYSTEM);
    put16(files[0].entry + 24, 2);
    reset(); add(0, "MINI.A", 1, 1, 0, 0); add(1, "MINI.B", 1, 1, 0, BLOCK);
    reject(2, KUI_VMU_IMAGE_FILESYSTEM);
    reset(); add(0, "CAPACITY.A", 200, 0, 0, 0); add(1, "CAPACITY.B", 1, 0, 0, 0);
    reject(2, KUI_VMU_IMAGE_FILESYSTEM);
}
static void arguments_and_aliases(void) {
    reset(); add(0, "VALID", 1, 0, 0, 0);
    reject(0, KUI_VMU_IMAGE_ARGUMENT); reject(201, KUI_VMU_IMAGE_ARGUMENT);
    reject(SIZE_MAX, KUI_VMU_IMAGE_ARGUMENT);
    reject_call(NULL, 1, destination, BYTES, &work, &summary, KUI_VMU_IMAGE_ARGUMENT);
    reject_call(files, 1, NULL, BYTES, &work, &summary, KUI_VMU_IMAGE_ARGUMENT);
    reject_call(files, 1, destination, BYTES - 1u, &work, &summary, KUI_VMU_IMAGE_ARGUMENT);
    reject_call(files, 1, destination, BYTES + 1u, &work, &summary, KUI_VMU_IMAGE_ARGUMENT);
    reject_call(files, 1, destination, BYTES, NULL, &summary, KUI_VMU_IMAGE_ARGUMENT);
    reject_call(files, 1, destination, BYTES, &work, NULL, KUI_VMU_IMAGE_ARGUMENT);
    files[0].payload = NULL; reject(1, KUI_VMU_IMAGE_ARGUMENT);
    files[0].payload = (const void *)(UINTPTR_MAX - 255u);
    reject(1, KUI_VMU_IMAGE_ARGUMENT);
    files[0].payload = payload;

    /* Each writable range is forbidden from touching another writable range,
     * the descriptor array or any payload, including a boundary-byte overlap. */
    reject_call(files, 1, destination, BYTES,
        (struct kui_vmu_image_workspace *)(void *)destination, &summary, KUI_VMU_IMAGE_ARGUMENT);
    reject_call(files, 1, destination, BYTES, &work,
        (struct kui_vmu_image_summary *)(void *)(destination + BYTES - sizeof(summary)), KUI_VMU_IMAGE_ARGUMENT);
    reject_call(files, 1, destination, BYTES, &work,
        (struct kui_vmu_image_summary *)(void *)&work, KUI_VMU_IMAGE_ARGUMENT);
    reject_call(files, 1, files, BYTES, &work, &summary, KUI_VMU_IMAGE_ARGUMENT);
    reject_call(files, 1, destination, BYTES,
        (struct kui_vmu_image_workspace *)(void *)files, &summary, KUI_VMU_IMAGE_ARGUMENT);
    reject_call(files, 1, destination, BYTES, &work,
        (struct kui_vmu_image_summary *)(void *)files, KUI_VMU_IMAGE_ARGUMENT);
    files[0].payload = destination + BYTES - 1u; reject(1, KUI_VMU_IMAGE_ARGUMENT);
    files[0].payload = &work; reject(1, KUI_VMU_IMAGE_ARGUMENT);
    files[0].payload = &summary; reject(1, KUI_VMU_IMAGE_ARGUMENT);
    files[0].payload = payload;
    reject_call(files, 1, payload, BYTES, &work, &summary, KUI_VMU_IMAGE_ARGUMENT);
    reject_call(files, 1, destination, BYTES,
        (struct kui_vmu_image_workspace *)(void *)payload, &summary, KUI_VMU_IMAGE_ARGUMENT);
    reject_call(files, 1, destination, BYTES, &work,
        (struct kui_vmu_image_summary *)(void *)payload, KUI_VMU_IMAGE_ARGUMENT);
}
int main(void) {
    preservation(); capacity(); invalid_files(); arguments_and_aliases();
    puts("vmu image pack tests passed");
    return 0;
}
