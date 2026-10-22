/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/core.h"
#include <string.h>

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool kui_block_range(uint64_t start, uint64_t count, uint64_t total) {
    return count && start < total && count <= total - start;
}

bool kui_select_volume(const uint8_t mbr[512], uint64_t blocks,
                       struct kui_volume *out) {
    if(!mbr || !out || !blocks || blocks > UINT32_MAX ||
       mbr[510] != 0x55 || mbr[511] != 0xaa) return false;
    if((!memcmp(mbr + 82, "FAT32   ", 8) && mbr[11] == 0 && mbr[12] == 2) ||
       (!memcmp(mbr + 3, "EXFAT   ", 8) && mbr[108] == 9)) {
        *out = (struct kui_volume){0, (uint32_t)blocks, false};
        return true;
    }
    unsigned found = 0;
    struct kui_volume candidate = {0};
    for(unsigned i = 0; i < 4; ++i) {
        const uint8_t *p = mbr + 446 + i * 16;
        if(p[4] == 0) {
            if(le32(p + 8) || le32(p + 12)) return false;
            continue;
        }
        /* Includes protective GPT, extended and unsupported partitions. */
        if(++found > 1 || (p[0] != 0 && p[0] != 0x80) ||
           (p[4] != 0x0b && p[4] != 0x0c && p[4] != 0x07)) return false;
        candidate = (struct kui_volume){le32(p + 8), le32(p + 12), true};
        if(!candidate.start ||
           !kui_block_range(candidate.start, candidate.count, blocks)) return false;
    }
    if(found != 1) return false;
    *out = candidate;
    return true;
}

bool kui_fad_to_lba(uint32_t fad, uint32_t *lba) {
    if(!lba || fad < 150 || fad > 0xffffff) return false;
    *lba = fad - 150;
    return true;
}

bool kui_parse_toc(const uint32_t entries[99], uint32_t first, uint32_t last,
                   uint32_t leadout, struct kui_toc *out) {
    if(!entries || !out) return false;
    unsigned lo = (first >> 16) & 255, hi = (last >> 16) & 255;
    uint32_t end = leadout & 0xffffff;
    if(lo < 1 || hi > 99 || lo > hi || leadout == UINT32_MAX) return false;
    struct kui_toc parsed = {0};
    for(unsigned n = lo; n <= hi; ++n) {
        uint32_t word = entries[n - 1];
        uint32_t start = word & 0xffffff;
        uint32_t next = n == hi ? end : entries[n] & 0xffffff;
        if(word == UINT32_MAX || ((word >> 24) & 15) != 1 ||
           start < 150 || next <= start || next > end) return false;
        parsed.tracks[parsed.count++] = (struct kui_track){
            n, (word >> 28) & 15, start, next};
    }
    *out = parsed;
    return true;
}

unsigned kui_sample_points(const struct kui_track *t, uint32_t points[3]) {
    if(!t || !points || t->end <= t->start) return 0;
    uint32_t length = t->end - t->start;
    /* Keep the last sample clear of a possible 150-sector pregap. */
    uint32_t candidates[3] = {t->start, t->start + (length - 1) / 2,
        length > 300 ? t->end - 151 : t->start};
    unsigned count = 0;
    for(unsigned i = 0; i < 3; ++i) {
        bool duplicate = false;
        for(unsigned j = 0; j < count; ++j)
            if(points[j] == candidates[i]) duplicate = true;
        if(!duplicate) points[count++] = candidates[i];
    }
    return count;
}

int kui_data_offset(const uint8_t raw[KUI_RAW_BYTES]) {
    if(raw[0] || raw[11]) return -1;
    for(unsigned i = 1; i < 11; ++i) if(raw[i] != 255) return -1;
    if(raw[15] == 1) return 16;
    /* Mode 2 Form 1: duplicated XA subheader and no Form 2 flag. */
    if(raw[15] == 2 && !(raw[18] & 0x20) && !memcmp(raw + 16, raw + 20, 4))
        return 24;
    return -1;
}

bool kui_guard_is(const uint8_t *data, size_t size, uint8_t value) {
    for(size_t i = 0; i < size; ++i) if(data[i] != value) return false;
    return true;
}

void kui_pattern(uint8_t *out, uint64_t offset, size_t size) {
    for(size_t i = 0; i < size; ++i) {
        uint64_t pos = offset + i;
        uint32_t x = (uint32_t)(pos / 4) ^ UINT32_C(0x4b554931);
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        out[i] = (uint8_t)(x >> ((pos % 4) * 8));
    }
}

uint32_t kui_crc32(uint32_t previous, const void *data, size_t size) {
    static const uint32_t nibble[16]={0,0x1db71064,0x3b6e20c8,0x26d930ac,
        0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,0xedb88320,0xf00f9344,
        0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
    const uint8_t *p = data;
    uint32_t crc = ~previous;
    for(size_t i = 0; i < size; ++i) {
        crc ^= p[i];
        crc=(crc>>4)^nibble[crc&15];
        crc=(crc>>4)^nibble[crc&15];
    }
    return ~crc;
}
