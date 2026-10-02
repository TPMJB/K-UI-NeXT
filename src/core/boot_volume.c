/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/boot_volume.h"
#include <string.h>

/* GPT field layout/CRC rules: UEFI 2.10, chapter 5, tables 5.5/5.6:
 * https://uefi.org/specs/UEFI/2.10/05_GUID_Partition_Table_Format.html
 * This bounded boot reader deliberately accepts only the common 128-byte
 * entry layout, validates both copies, and never repairs disk metadata. */
#define GPT_MAX_ENTRIES 128u
static const uint8_t linux_guid[16] = {
    0xaf,0x3d,0xc6,0x0f,0x83,0x84,0x72,0x47,
    0x8e,0x79,0x3d,0x69,0xd8,0x47,0x7d,0xe4
};
/* UEFI ESP and Microsoft Basic Data type GUIDs (on-disk mixed endian).
 * Basic Data can contain non-FAT filesystems; the caller must actually mount
 * and validate FAT before treating either type as a usable boot volume. */
static const uint8_t esp_guid[16] = {
    0x28,0x73,0x2a,0xc1,0x1f,0xf8,0xd2,0x11,
    0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b
};
static const uint8_t basic_guid[16] = {
    0xa2,0xa0,0xd0,0xeb,0xe5,0xb9,0x33,0x44,
    0x87,0xc0,0x68,0xb6,0xb7,0x26,0x99,0xc7
};
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t le64(const uint8_t *p) {
    return le32(p) | (uint64_t)le32(p + 4) << 32;
}
static bool zero(const uint8_t *p, size_t bytes) {
    for(size_t i = 0; i < bytes; ++i) if(p[i]) return false;
    return true;
}
static bool read_sector(const struct kui_media_ops *m, uint32_t block, uint8_t *data) {
    return m->read(m->ctx, block, 1, data) == 0;
}
struct header {
    uint32_t first, last, table, entries, crc;
    uint8_t guid[16];
};
static enum kui_boot_volume_result add_candidate(struct kui_boot_layout *layout,
        enum kui_boot_volume_kind kind, uint32_t first, uint32_t count) {
    for(unsigned i = 0; i < layout->count; ++i)
        if(layout->candidates[i].kind == kind) return KUI_BOOT_VOLUME_AMBIGUOUS;
    if(layout->count == 2) return KUI_BOOT_VOLUME_AMBIGUOUS;
    layout->candidates[layout->count++] = (struct kui_boot_candidate){kind,
        {first, count, kind != KUI_BOOT_VOLUME_RAW}};
    return KUI_BOOT_VOLUME_OK;
}
static enum kui_boot_volume_result header(const struct kui_media_ops *m,
        uint32_t total, bool backup, struct header *out) {
    uint8_t sector[512];
    uint32_t at = backup ? total - 1u : 1u;
    if(!read_sector(m, at, sector)) return KUI_BOOT_VOLUME_IO;
    if(memcmp(sector, "EFI PART", 8)) return KUI_BOOT_VOLUME_INVALID;
    if(le32(sector + 8) != 0x00010000u || le32(sector + 12) != 92u)
        return KUI_BOOT_VOLUME_UNSUPPORTED;
    uint32_t expected = le32(sector + 16);
    memset(sector + 16, 0, 4);
    if(kui_crc32(0, sector, 92) != expected || le32(sector + 20) ||
       !zero(sector + 92, sizeof(sector) - 92)) return KUI_BOOT_VOLUME_INVALID;
    if(le64(sector + 24) != at || le64(sector + 32) != (backup ? 1u : total - 1u))
        return KUI_BOOT_VOLUME_INVALID;
    uint64_t first = le64(sector + 40), last = le64(sector + 48);
    uint64_t table = le64(sector + 72);
    uint32_t entries = le32(sector + 80);
    if(!entries || entries > GPT_MAX_ENTRIES || le32(sector + 84) != 128u)
        return KUI_BOOT_VOLUME_UNSUPPORTED;
    uint32_t table_sectors = (entries + 3u) / 4u;
    /* The UEFI minimum metadata reservation is 16 KiB even with fewer
     * populated descriptors. Nothing usable may overlap either header/table. */
    if(first < 34u || first > last || last >= total - 33u ||
       table < 2u || table >= total || table_sectors > total - table ||
       zero(sector + 56, 16)) return KUI_BOOT_VOLUME_INVALID;
    if(backup ? (table <= last || table + table_sectors > at) :
                (table + table_sectors > first)) return KUI_BOOT_VOLUME_INVALID;
    *out = (struct header){(uint32_t)first, (uint32_t)last, (uint32_t)table,
                           entries, le32(sector + 88), {0}};
    memcpy(out->guid, sector + 56, 16);
    return KUI_BOOT_VOLUME_OK;
}
static enum kui_boot_volume_result gpt(const struct kui_media_ops *m,
                                      uint32_t total, struct kui_boot_layout *out) {
    if(total < 68u) return KUI_BOOT_VOLUME_INVALID;
    struct header primary, backup;
    enum kui_boot_volume_result result = header(m, total, false, &primary);
    if(result != KUI_BOOT_VOLUME_OK) return result;
    result = header(m, total, true, &backup);
    if(result != KUI_BOOT_VOLUME_OK) return result;
    if(primary.first != backup.first || primary.last != backup.last ||
       primary.entries != backup.entries || primary.crc != backup.crc ||
       memcmp(primary.guid, backup.guid, 16)) return KUI_BOOT_VOLUME_INVALID;
    struct range { uint32_t first, last; uint8_t guid[16]; } ranges[GPT_MAX_ENTRIES];
    unsigned used = 0;
    uint32_t crc = 0;
    uint8_t p[512], b[512];
    for(uint32_t n = 0; n < primary.entries; n += 4u) {
        if(!read_sector(m, primary.table + n / 4u, p) ||
           !read_sector(m, backup.table + n / 4u, b)) return KUI_BOOT_VOLUME_IO;
        unsigned count = primary.entries - n;
        if(count > 4u) count = 4u;
        /* Compare complete used arrays, not just matching CRC values. */
        if(memcmp(p, b, count * 128u)) return KUI_BOOT_VOLUME_INVALID;
        crc = kui_crc32(crc, p, count * 128u);
        for(unsigned j = 0; j < count; ++j) {
            const uint8_t *entry = p + j * 128u;
            if(zero(entry, 16)) continue;
            uint64_t first = le64(entry + 32), last = le64(entry + 40);
            if(first < primary.first || first > last || last > primary.last ||
               zero(entry + 16, 16)) return KUI_BOOT_VOLUME_INVALID;
            for(unsigned k = 0; k < used; ++k)
                if((first <= ranges[k].last && last >= ranges[k].first) ||
                   !memcmp(entry + 16, ranges[k].guid, 16)) return KUI_BOOT_VOLUME_INVALID;
            ranges[used].first = (uint32_t)first;
            ranges[used].last = (uint32_t)last;
            memcpy(ranges[used++].guid, entry + 16, 16);
            if(!memcmp(entry, linux_guid, 16))
                result = add_candidate(out, KUI_BOOT_VOLUME_LINUX_CANDIDATE,
                    (uint32_t)first, (uint32_t)(last - first + 1u));
            else if(!memcmp(entry, esp_guid, 16) || !memcmp(entry, basic_guid, 16))
                result = add_candidate(out, KUI_BOOT_VOLUME_FAT_CANDIDATE,
                    (uint32_t)first, (uint32_t)(last - first + 1u));
            if(result != KUI_BOOT_VOLUME_OK) return result;
        }
    }
    if(crc != primary.crc) return KUI_BOOT_VOLUME_INVALID;
    return out->count ? KUI_BOOT_VOLUME_OK : KUI_BOOT_VOLUME_UNSUPPORTED;
}

static enum kui_boot_volume_result scan(const struct kui_media_ops *m,
                                        struct kui_boot_layout *out) {
    if(!m || !m->blocks || !m->read) return KUI_BOOT_VOLUME_INVALID;
    uint64_t blocks = m->blocks(m->ctx);
    if(!blocks || blocks > UINT32_MAX) return KUI_BOOT_VOLUME_UNSUPPORTED;
    uint8_t mbr[512];
    if(!read_sector(m, 0, mbr)) return KUI_BOOT_VOLUME_IO;
    /* FAT superfloppy boot code may occupy the MBR partition-table bytes.
     * Preserve the existing explicit FAT32/exFAT whole-volume recognition. */
    struct kui_volume whole;
    if(mbr[510] != 0x55 || mbr[511] != 0xaa ||
       (kui_select_volume(mbr, blocks, &whole) && !whole.partitioned))
        return add_candidate(out, KUI_BOOT_VOLUME_RAW, 0, (uint32_t)blocks);
    unsigned found = 0;
    const uint8_t *partitions[4];
    for(unsigned i = 0; i < 4; ++i) {
        const uint8_t *entry = mbr + 446 + 16u * i;
        if(!entry[4]) {
            if(!zero(entry, 16)) return KUI_BOOT_VOLUME_INVALID;
            continue;
        }
        if((entry[0] != 0 && entry[0] != 0x80) || !le32(entry + 8) ||
           !kui_block_range(le32(entry + 8), le32(entry + 12), blocks))
            return KUI_BOOT_VOLUME_INVALID;
        partitions[found++] = entry;
    }
    if(!found) {
        /* A raw filesystem may have an otherwise empty boot-sector signature.
         * Its superblock remains authoritative at the next layer. */
        return add_candidate(out, KUI_BOOT_VOLUME_RAW, 0, (uint32_t)blocks);
    }
    for(unsigned i = 0; i < found; ++i) {
        const uint8_t *p = partitions[i];
        if(p[4] != 0xee) continue;
        if(found != 1) return KUI_BOOT_VOLUME_AMBIGUOUS; /* hybrid protective MBR */
        if(p[0] || le32(p + 8) != 1u || le32(p + 12) != blocks - 1u)
            return KUI_BOOT_VOLUME_INVALID;
        return gpt(m, (uint32_t)blocks, out);
    }
    for(unsigned i = 0; i < found; ++i) {
        const uint8_t *p = partitions[i];
        uint32_t first = le32(p + 8), count = le32(p + 12);
        for(unsigned j = 0; j < i; ++j) {
            uint32_t other = le32(partitions[j] + 8), size = le32(partitions[j] + 12);
            if((uint64_t)first < (uint64_t)other + size &&
               (uint64_t)other < (uint64_t)first + count) return KUI_BOOT_VOLUME_INVALID;
        }
        enum kui_boot_volume_kind kind;
        if(p[4] == 0x83) kind = KUI_BOOT_VOLUME_LINUX_CANDIDATE;
        else if(p[4] == 0x0b || p[4] == 0x0c || p[4] == 0x07)
            kind = KUI_BOOT_VOLUME_FAT_CANDIDATE;
        else return KUI_BOOT_VOLUME_UNSUPPORTED;
        enum kui_boot_volume_result result = add_candidate(out, kind, first, count);
        if(result != KUI_BOOT_VOLUME_OK) return result;
    }
    return KUI_BOOT_VOLUME_OK;
}

enum kui_boot_volume_result kui_boot_volume_scan(const struct kui_media_ops *m,
                                                 struct kui_boot_layout *out) {
    if(!out) return KUI_BOOT_VOLUME_INVALID;
    *out = (struct kui_boot_layout){0};
    struct kui_boot_layout candidate = {0};
    enum kui_boot_volume_result result = scan(m, &candidate);
    if(result == KUI_BOOT_VOLUME_OK) *out = candidate;
    return result;
}

enum kui_boot_volume_result kui_boot_volume_select(const struct kui_media_ops *m,
                                                   struct kui_volume *out) {
    if(!out) return KUI_BOOT_VOLUME_INVALID;
    *out = (struct kui_volume){0};
    struct kui_boot_layout layout;
    enum kui_boot_volume_result result = kui_boot_volume_scan(m, &layout);
    if(result != KUI_BOOT_VOLUME_OK) return result;
    for(unsigned i = 0; i < layout.count; ++i) {
        if(layout.candidates[i].kind == KUI_BOOT_VOLUME_FAT_CANDIDATE) continue;
        *out = layout.candidates[i].volume;
        return KUI_BOOT_VOLUME_OK;
    }
    return KUI_BOOT_VOLUME_UNSUPPORTED;
}
