/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/boot_volume.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t disk[256][512];
static uint64_t device_blocks = 256;
static uint32_t fail_block = UINT32_MAX;
static unsigned reads;
static const uint8_t linux_guid[16] = {
    0xaf,0x3d,0xc6,0x0f,0x83,0x84,0x72,0x47,
    0x8e,0x79,0x3d,0x69,0xd8,0x47,0x7d,0xe4
};
static const uint8_t esp_guid[16] = {
    0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,
    0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b
};
static const uint8_t basic_guid[16] = {
    0xa2,0xa0,0xd0,0xeb,0xe5,0xb9,0x33,0x44,
    0x87,0xc0,0x68,0xb6,0xb7,0x26,0x99,0xc7
};
static void put32(uint8_t *p, uint32_t v) {
    for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (i * 8));
}
static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32));
}
static uint64_t blocks(void *ctx) { (void)ctx; return device_blocks; }
static int read_sector(void *ctx, uint32_t block, size_t count, uint8_t *data) {
    (void)ctx;
    assert(count == 1 && block < device_blocks && block < 256);
    ++reads;
    if(block == fail_block) return -1;
    memcpy(data, disk[block], 512);
    return 0;
}
static int write_sector(void *ctx, uint32_t block, size_t count, const uint8_t *data) {
    (void)ctx; (void)block; (void)count; (void)data;
    assert(!"boot selector must never write");
    return -1;
}
static int sync_disk(void *ctx) {
    (void)ctx; assert(!"boot selector must never sync"); return -1;
}
static const struct kui_media_ops media = {NULL, blocks, read_sector, write_sector, sync_disk};
static void clear_disk(void) {
    memset(disk, 0, sizeof(disk));
    device_blocks = 256; fail_block = UINT32_MAX; reads = 0;
}
static void mbr_entry(unsigned n, uint8_t type, uint32_t first, uint32_t count) {
    disk[0][510] = 0x55; disk[0][511] = 0xaa;
    uint8_t *p = disk[0] + 446 + n * 16;
    p[4] = type; put32(p + 8, first); put32(p + 12, count);
}
static void header_crc(unsigned block) {
    put32(disk[block] + 16, 0);
    put32(disk[block] + 16, kui_crc32(0, disk[block], 92));
}
static void make_header(unsigned block, unsigned other, unsigned table, uint32_t crc) {
    uint8_t *p = disk[block];
    memcpy(p, "EFI PART", 8);
    put32(p + 8, 0x10000); put32(p + 12, 92);
    put64(p + 24, block); put64(p + 32, other);
    put64(p + 40, 34); put64(p + 48, 222);
    p[56] = 0xa7;
    put64(p + 72, table); put32(p + 80, 128); put32(p + 84, 128);
    put32(p + 88, crc); header_crc(block);
}
static void table_commit(void) {
    memcpy(disk[223], disk[2], 32 * 512);
    uint32_t crc = kui_crc32(0, disk[2], 32 * 512);
    make_header(1, 255, 2, crc); make_header(255, 1, 223, crc);
}
static void gpt_entry(unsigned n, bool linux_type, uint32_t first, uint32_t last) {
    uint8_t *p = disk[2 + n / 4] + n % 4 * 128;
    if(linux_type) memcpy(p, linux_guid, 16); else p[0] = 0x28;
    p[16] = (uint8_t)(n + 1);
    put64(p + 32, first); put64(p + 40, last);
}
static void make_gpt(void) {
    clear_disk(); mbr_entry(0, 0xee, 1, 255);
    gpt_entry(0, true, 50, 100);
    gpt_entry(1, false, 150, 200);
    table_commit();
}
static void expect(enum kui_boot_volume_result expected) {
    struct kui_volume v = {123, 456, true};
    assert(kui_boot_volume_select(&media, &v) == expected);
    if(expected != KUI_BOOT_VOLUME_OK) assert(v.start == 0 && v.count == 0 && !v.partitioned);
}
static struct kui_boot_layout expect_scan(enum kui_boot_volume_result expected) {
    struct kui_boot_layout layout;
    memset(&layout, 0xa5, sizeof(layout));
    assert(kui_boot_volume_scan(&media, &layout) == expected);
    if(expected != KUI_BOOT_VOLUME_OK) {
        assert(layout.count == 0);
        for(unsigned i = 0; i < 2; ++i) {
            assert(layout.candidates[i].kind == KUI_BOOT_VOLUME_RAW);
            assert(layout.candidates[i].volume.start == 0);
            assert(layout.candidates[i].volume.count == 0);
            assert(!layout.candidates[i].volume.partitioned);
        }
    }
    return layout;
}
int main(void) {
    struct kui_volume v;
    struct kui_boot_layout layout;
    clear_disk();
    assert(kui_boot_volume_select(&media, &v) == KUI_BOOT_VOLUME_OK);
    assert(v.start == 0 && v.count == 256 && !v.partitioned && reads == 1);
    mbr_entry(0, 0, 0, 0);
    expect(KUI_BOOT_VOLUME_OK); /* harmless signature with empty partition table */
    mbr_entry(0, 0x83, 16, 240);
    assert(kui_boot_volume_select(&media, &v) == KUI_BOOT_VOLUME_OK);
    assert(v.start == 16 && v.count == 240 && v.partitioned);
    mbr_entry(0, 0x83, 16, 241); expect(KUI_BOOT_VOLUME_INVALID);
    mbr_entry(0, 0x83, UINT32_MAX, UINT32_MAX); expect(KUI_BOOT_VOLUME_INVALID);
    mbr_entry(0, 0x05, 16, 200); expect(KUI_BOOT_VOLUME_UNSUPPORTED);
    mbr_entry(0, 0x83, 16, 100); mbr_entry(1, 0x83, 150, 50);
    expect(KUI_BOOT_VOLUME_AMBIGUOUS);
    clear_disk(); fail_block = 0; expect(KUI_BOOT_VOLUME_IO);
    clear_disk(); device_blocks = UINT64_C(0x100000000);
    expect(KUI_BOOT_VOLUME_UNSUPPORTED); assert(reads == 0);

    clear_disk();
    layout = expect_scan(KUI_BOOT_VOLUME_OK);
    assert(layout.count == 1 && layout.candidates[0].kind == KUI_BOOT_VOLUME_RAW);
    assert(layout.candidates[0].volume.start == 0 && layout.candidates[0].volume.count == 256);
    /* FAT superfloppy code is not a partition table, even with nonzero bytes
     * in the area that an MBR would use for entries. */
    disk[0][510] = 0x55; disk[0][511] = 0xaa; disk[0][12] = 2;
    memcpy(disk[0] + 82, "FAT32   ", 8); memset(disk[0] + 446, 0xa5, 64);
    layout = expect_scan(KUI_BOOT_VOLUME_OK);
    assert(layout.count == 1 && layout.candidates[0].kind == KUI_BOOT_VOLUME_RAW);
    clear_disk(); mbr_entry(0, 0x0c, 16, 100); mbr_entry(1, 0x83, 150, 50);
    layout = expect_scan(KUI_BOOT_VOLUME_OK);
    assert(layout.count == 2 && layout.candidates[0].kind == KUI_BOOT_VOLUME_FAT_CANDIDATE);
    assert(layout.candidates[0].volume.start == 16 && layout.candidates[0].volume.count == 100);
    assert(layout.candidates[1].kind == KUI_BOOT_VOLUME_LINUX_CANDIDATE);
    assert(kui_boot_volume_select(&media, &v) == KUI_BOOT_VOLUME_OK && v.start == 150 && v.count == 50);
    mbr_entry(1, 0x83, 80, 50); expect_scan(KUI_BOOT_VOLUME_INVALID);
    mbr_entry(1, 0x07, 150, 50); expect_scan(KUI_BOOT_VOLUME_AMBIGUOUS);
    mbr_entry(1, 0xef, 150, 50); expect_scan(KUI_BOOT_VOLUME_UNSUPPORTED);
    clear_disk(); mbr_entry(0, 0x07, 16, 100);
    layout = expect_scan(KUI_BOOT_VOLUME_OK);
    assert(layout.count == 1 && layout.candidates[0].kind == KUI_BOOT_VOLUME_FAT_CANDIDATE);
    expect(KUI_BOOT_VOLUME_UNSUPPORTED); /* ext4 helper never chooses FAT */
    clear_disk(); mbr_entry(0, 0x83, 150, 50); mbr_entry(1, 0x0b, 16, 100);
    layout = expect_scan(KUI_BOOT_VOLUME_OK);
    assert(layout.candidates[0].kind == KUI_BOOT_VOLUME_LINUX_CANDIDATE);
    assert(layout.candidates[1].kind == KUI_BOOT_VOLUME_FAT_CANDIDATE);

    make_gpt();
    assert(kui_boot_volume_select(&media, &v) == KUI_BOOT_VOLUME_OK);
    assert(v.start == 50 && v.count == 51 && v.partitioned);
    disk[1][40] ^= 1; expect(KUI_BOOT_VOLUME_INVALID); /* primary header CRC */
    make_gpt(); disk[255][40] ^= 1; expect(KUI_BOOT_VOLUME_INVALID); /* backup header CRC */
    make_gpt(); disk[223][60] ^= 1; expect(KUI_BOOT_VOLUME_INVALID); /* arrays disagree */
    make_gpt(); disk[2][60] ^= 1; disk[223][60] ^= 1;
    expect(KUI_BOOT_VOLUME_INVALID); /* both arrays corrupt with same bytes */
    make_gpt(); gpt_entry(1, false, 80, 150); table_commit();
    expect(KUI_BOOT_VOLUME_INVALID); /* overlapping used entries */
    make_gpt(); gpt_entry(1, true, 150, 200); table_commit();
    expect(KUI_BOOT_VOLUME_AMBIGUOUS);
    make_gpt(); gpt_entry(0, true, 20, 60); table_commit();
    expect(KUI_BOOT_VOLUME_INVALID); /* entry overlaps GPT metadata */
    make_gpt(); memcpy(disk[2] + 128 + 16, disk[2] + 16, 16); table_commit();
    expect(KUI_BOOT_VOLUME_INVALID); /* duplicate partition GUID */
    make_gpt(); put64(disk[1] + 72, 50); header_crc(1);
    expect(KUI_BOOT_VOLUME_INVALID); /* table inside usable range */
    make_gpt(); put32(disk[1] + 80, 256); header_crc(1);
    expect(KUI_BOOT_VOLUME_UNSUPPORTED);
    make_gpt(); mbr_entry(1, 0x83, 50, 51); expect(KUI_BOOT_VOLUME_AMBIGUOUS);
    make_gpt(); fail_block = 255; expect(KUI_BOOT_VOLUME_IO);
    make_gpt(); fail_block = 223; expect(KUI_BOOT_VOLUME_IO);
    make_gpt(); memcpy(disk[2] + 128, esp_guid, 16); table_commit();
    layout = expect_scan(KUI_BOOT_VOLUME_OK);
    assert(layout.count == 2 && layout.candidates[0].kind == KUI_BOOT_VOLUME_LINUX_CANDIDATE);
    assert(layout.candidates[1].kind == KUI_BOOT_VOLUME_FAT_CANDIDATE);
    assert(layout.candidates[1].volume.start == 150 && layout.candidates[1].volume.count == 51);
    assert(kui_boot_volume_select(&media, &v) == KUI_BOOT_VOLUME_OK && v.start == 50);
    memcpy(disk[2] + 128, basic_guid, 16); table_commit();
    layout = expect_scan(KUI_BOOT_VOLUME_OK);
    assert(layout.count == 2 && layout.candidates[1].kind == KUI_BOOT_VOLUME_FAT_CANDIDATE);
    /* ESP plus Basic Data is ambiguous, regardless of partition names. */
    gpt_entry(2, false, 110, 130); memcpy(disk[2] + 256, esp_guid, 16);
    memcpy(disk[2] + 256 + 56, "K\0U\0I\0", 6); table_commit();
    expect_scan(KUI_BOOT_VOLUME_AMBIGUOUS);
    make_gpt(); memcpy(disk[2] + 128, esp_guid, 16); table_commit();
    disk[255][40] ^= 1; expect_scan(KUI_BOOT_VOLUME_INVALID);
    make_gpt(); fail_block = 223; expect_scan(KUI_BOOT_VOLUME_IO);
    puts("boot volume: typed raw/FAT/Linux discovery, split MBR/GPT, CRCs/bounds/ambiguity and read-only failures passed");
    return 0;
}
