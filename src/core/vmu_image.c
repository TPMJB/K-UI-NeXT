/* SPDX-License-Identifier: GPL-3.0-only */
/* Independently authored image preparation and admission. On-disk structures
 * follow pinned KOS declarations and published VMU storage documentation. */
#include "kui/vmu_image.h"
#include <string.h>

enum { ROOT = 255, FAT = 254, DIR_FIRST = 253, DIR_LAST = 241,
       FREE = 0xfffc, END = 0xfffa };
static const uint8_t default_time[] = {0x20,0x00,0x01,0x01,0,0,0,5};

static unsigned le16(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static void put16(uint8_t *p, unsigned value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static int all_zero(const uint8_t *p, size_t bytes) {
    for(size_t i = 0; i < bytes; ++i)
        if(p[i]) return 0;
    return 1;
}

static int unbcd(uint8_t b) {
    if((b & 15u) > 9u || (b >> 4) > 9u) return -1;
    return (b >> 4) * 10 + (b & 15);
}

static int valid_time(const uint8_t *p) {
    static const unsigned days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int cent = unbcd(p[0]), year = unbcd(p[1]), month = unbcd(p[2]);
    int day = unbcd(p[3]), hour = unbcd(p[4]), minute = unbcd(p[5]);
    int second = unbcd(p[6]);
    if(cent < 0 || year < 0 || month < 1 || month > 12 || day < 1 ||
       hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
       second < 0 || second > 59 || p[7] > 6)
        return 0;
    unsigned full_year = (unsigned)(cent * 100 + year);
    unsigned limit = days[month - 1];
    if(month == 2 && !(full_year % 4) &&
       ((full_year % 100) || !(full_year % 400)))
        ++limit;
    return (unsigned)day <= limit;
}

static void make_block(unsigned block, const uint8_t stamp[8], uint8_t *out) {
    memset(out, 0, 512);
    if(block == FAT) {
        for(unsigned i = 0; i < 256; ++i) {
            unsigned next = i < DIR_LAST ? FREE :
                            i > DIR_LAST && i <= DIR_FIRST ? i - 1 : END;
            put16(out + i * 2, next);
        }
    }
    else if(block == ROOT) {
        memset(out, 0x55, 16);
        memcpy(out + 0x30, stamp, 8);
        put16(out + 0x40, 255);
        put16(out + 0x44, ROOT);
        put16(out + 0x46, FAT);
        put16(out + 0x48, 1);
        put16(out + 0x4a, DIR_FIRST);
        put16(out + 0x4c, 13);
        put16(out + 0x50, 200);
        put16(out + 0x52, 31);
    }
}

int kui_vmu_image_blank_block(uint16_t block, uint8_t out[512]) {
    if(block >= KUI_VMU_IMAGE_BLOCKS || !out)
        return KUI_VMU_IMAGE_ARGUMENT;
    make_block(block, default_time, out);
    return KUI_VMU_IMAGE_OK;
}

int kui_vmu_image_format_blank(const struct kui_vmu_image_io *io,
    size_t image_bytes, const uint8_t timestamp[8],
    struct kui_vmu_image_workspace *work) {
    uint8_t stamp[8];
    if(!io || !io->read_block || !io->write_block || !io->sync || !work ||
       image_bytes != KUI_VMU_IMAGE_BYTES)
        return KUI_VMU_IMAGE_ARGUMENT;
    memcpy(stamp, timestamp ? timestamp : default_time, 8);
    if(!valid_time(stamp)) return KUI_VMU_IMAGE_ARGUMENT;
    /* No mutation until every sector of the exact-length new file is blank. */
    for(unsigned block = 0; block < 256; ++block) {
        if(io->read_block(io->context, (uint16_t)block, work->block))
            return KUI_VMU_IMAGE_IO;
        if(!all_zero(work->block, 512)) return KUI_VMU_IMAGE_NOT_BLANK;
    }
    /* The valid root is installed only after all other contents are durable.
     * Until then, a partial newly created image remains unformatted. */
    for(unsigned block = 0; block < ROOT; ++block) {
        make_block(block, stamp, work->block);
        if(io->write_block(io->context, (uint16_t)block, work->block))
            return KUI_VMU_IMAGE_IO;
    }
    if(io->sync(io->context)) return KUI_VMU_IMAGE_IO;
    make_block(ROOT, stamp, work->block);
    if(io->write_block(io->context, ROOT, work->block) || io->sync(io->context))
        return KUI_VMU_IMAGE_IO;
    for(unsigned block = 0; block < 256; ++block) {
        if(io->read_block(io->context, (uint16_t)block, work->block))
            return KUI_VMU_IMAGE_IO;
        make_block(block, stamp, work->fat);
        if(memcmp(work->block, work->fat, 512)) return KUI_VMU_IMAGE_VERIFY;
    }
    return KUI_VMU_IMAGE_OK;
}

static int root_geometry(const uint8_t *root) {
    for(unsigned i = 0; i < 16; ++i)
        if(root[i] != 0x55) return 0;
    /* KOS vmu_root_t leaves 0x40..0x45 and 0x52 onward opaque. They are
     * preserved during import, rather than compared with formatter defaults.
     * Only documented fields used to describe this layout constrain admission. */
    return root[16] <= 1 &&
        le16(root + 0x46) == FAT && le16(root + 0x48) == 1 &&
        le16(root + 0x4a) == DIR_FIRST && le16(root + 0x4c) == 13 &&
        le16(root + 0x4e) <= 123 && le16(root + 0x50) == 200;
}

static int name_key(const uint8_t *name, uint8_t *key) {
    unsigned bytes = 12;
    while(bytes && (!name[bytes - 1] || name[bytes - 1] == ' ')) --bytes;
    if(!bytes) return 0;
    for(unsigned i = 0; i < bytes; ++i)
        if(name[i] < 32 || name[i] == 127) return 0;
    memset(key, 0, 12);
    memcpy(key, name, bytes);
    return 1;
}

int kui_vmu_image_validate(const struct kui_vmu_image_io *io,
    size_t image_bytes, struct kui_vmu_image_workspace *work,
    struct kui_vmu_image_summary *summary) {
    struct kui_vmu_image_summary result = {0,0,0,0};
    if(summary) memset(summary, 0, sizeof(*summary));
    if(!io || !io->read_block || !work || !summary ||
       image_bytes != KUI_VMU_IMAGE_BYTES)
        return KUI_VMU_IMAGE_ARGUMENT;
    /* This also detects a truncated/partly unreadable file when its nominal
     * containing-file length is correct. It does not inspect opaque save data. */
    for(unsigned block = 0; block < 256; ++block)
        if(io->read_block(io->context, (uint16_t)block, work->block))
            return KUI_VMU_IMAGE_IO;
    if(!root_geometry(work->block)) return KUI_VMU_IMAGE_GEOMETRY;
    if(io->read_block(io->context, FAT, work->fat)) return KUI_VMU_IMAGE_IO;
    for(unsigned block = 200; block < 256; ++block) {
        unsigned expected = block < DIR_LAST ? FREE :
            block > DIR_LAST && block <= DIR_FIRST ? block - 1 : END;
        if(le16(work->fat + block * 2) != expected)
            return KUI_VMU_IMAGE_FILESYSTEM;
    }
    for(unsigned block = 0; block < 200; ++block) {
        unsigned next = le16(work->fat + block * 2);
        if(next == FREE) ++result.free_blocks;
        else if(next != END && next >= 200) return KUI_VMU_IMAGE_FILESYSTEM;
    }
    memset(work->used, 0, sizeof(work->used));
    for(unsigned directory = DIR_FIRST; directory >= DIR_LAST; --directory) {
        if(io->read_block(io->context, (uint16_t)directory, work->block))
            return KUI_VMU_IMAGE_IO;
        for(unsigned slot = 0; slot < 16; ++slot) {
            const uint8_t *entry = work->block + slot * 32;
            if(!all_zero(entry + 28, 4)) return KUI_VMU_IMAGE_FILESYSTEM;
            if(!entry[0]) continue;
            unsigned blocks = le16(entry + 24), at = le16(entry + 2);
            /* entry[1], the copy-protect byte, is opaque: games write
             * values besides 0x00 and 0xff (Crazy Taxi 2 writes 0x10). */
            if((entry[0] != 0x33 && entry[0] != 0xcc) ||
               !blocks || blocks > 200 ||
               le16(entry + 26) >= blocks ||
               !name_key(entry + 4, work->names[result.files]))
                return KUI_VMU_IMAGE_FILESYSTEM;
            for(unsigned i = 0; i < result.files; ++i)
                if(!memcmp(work->names[i], work->names[result.files], 12))
                    return KUI_VMU_IMAGE_FILESYSTEM;
            if(entry[0] == 0xcc) {
                if(result.game_files || at) return KUI_VMU_IMAGE_FILESYSTEM;
                ++result.game_files;
            }
            for(unsigned n = 0; n < blocks; ++n) {
                if(at >= 200 || work->used[at] ||
                   (entry[0] == 0xcc && at != n))
                    return KUI_VMU_IMAGE_FILESYSTEM;
                work->used[at] = 1;
                ++result.used_blocks;
                unsigned next = le16(work->fat + at * 2);
                if(n + 1 == blocks) {
                    if(next != END) return KUI_VMU_IMAGE_FILESYSTEM;
                }
                else at = next;
            }
            ++result.files;
        }
    }
    for(unsigned block = 0; block < 200; ++block)
        if(!work->used[block] && le16(work->fat + block * 2) != FREE)
            return KUI_VMU_IMAGE_FILESYSTEM;
    if(result.used_blocks + result.free_blocks != 200)
        return KUI_VMU_IMAGE_FILESYSTEM;
    *summary = result;
    return KUI_VMU_IMAGE_OK;
}
