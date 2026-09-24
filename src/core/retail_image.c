/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_image.h"
#include <stdbool.h>
#include <string.h>

#define TRACK_BASE 320u
#define TRACK_BYTES 32u
#define EXTENT_BASE (TRACK_BASE + KUI_RETAIL_IMAGE_TRACKS * TRACK_BYTES)
#define USED_BYTES (EXTENT_BASE + KUI_RETAIL_IMAGE_EXTENTS * 12u)
_Static_assert(USED_BYTES <= KUI_RETAIL_IMAGE_WIRE_BYTES, "manifest wire capacity");
_Static_assert(sizeof(struct kui_retail_manifest) < 4096u, "bounded manifest");

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t get64(const uint8_t *p) {
    return (uint64_t)get32(p) | (uint64_t)get32(p + 4) << 32;
}
static void put32(uint8_t *p, uint32_t v) {
    for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (i * 8u));
}
static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32));
}
uint32_t kui_retail_crc32(uint32_t previous, const void *data, size_t bytes) {
    static const uint32_t table[16] = {
        0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu,
        0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
        0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
        0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu
    };
    uint32_t crc = previous ^ UINT32_MAX;
    const uint8_t *p = data;
    while(bytes--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ table[crc & 15u];
        crc = (crc >> 4) ^ table[crc & 15u];
    }
    return crc ^ UINT32_MAX;
}
static uint32_t wire_crc(const uint8_t *wire) {
    static const uint8_t zero[4];
    uint32_t crc = kui_retail_crc32(0, wire, 16);
    crc = kui_retail_crc32(crc, zero, 4);
    return kui_retail_crc32(crc, wire + 20,
                              KUI_RETAIL_IMAGE_WIRE_BYTES - 20u);
}
static bool zeroes(const uint8_t *p, size_t n) {
    while(n--) if(*p++) return false;
    return true;
}
/* Canonical text excludes uninitialised trailing bytes as well as controls. */
static bool text_valid(const char *p, size_t n, bool required) {
    if(required && !*p) return false;
    for(size_t i = 0; i < n; ++i) {
        if(!p[i]) return zeroes((const uint8_t *)p + i, n - i);
        if((unsigned char)p[i] < 32 || (unsigned char)p[i] > 126) return false;
    }
    return false;
}
static bool tracks_valid(const struct kui_retail_manifest *m) {
    if(!m || !m->track_count || m->track_count > KUI_RETAIL_IMAGE_TRACKS)
        return false;
    for(uint32_t i = 0; i < m->track_count; ++i) {
        const struct kui_retail_track *t = &m->tracks[i];
        if(t->number != i + 1 || t->start_lba >= t->end_lba ||
           t->end_lba > KUI_GAME_LBA_LIMIT ||
           (t->control != 0 && t->control != 4) ||
           (i && m->tracks[i - 1].end_lba > t->start_lba)) return false;
    }
    return true;
}
static enum kui_game_result range_check(const struct kui_retail_manifest *m,
    uint32_t lba, uint32_t count, enum kui_game_sector_format format) {
    if(!count || (format != KUI_GAME_SECTOR_RAW && format != KUI_GAME_SECTOR_MODE1))
        return KUI_GAME_INVALID;
    if(lba < m->tracks[0].start_lba || lba >= KUI_GAME_LBA_LIMIT ||
       count > KUI_GAME_LBA_LIMIT - lba ||
       lba + count > m->tracks[m->track_count - 1].end_lba) return KUI_GAME_RANGE;
    uint32_t cursor = lba, end = lba + count;
    for(uint32_t i = 0; i < m->track_count && cursor < end; ++i) {
        const struct kui_retail_track *t = &m->tracks[i];
        if(t->end_lba <= cursor) continue;
        if(t->start_lba > cursor) return KUI_GAME_GAP;
        if(format == KUI_GAME_SECTOR_MODE1 && t->control != 4) return KUI_GAME_AUDIO;
        cursor = t->end_lba < end ? t->end_lba : end;
    }
    return cursor == end ? KUI_GAME_OK : KUI_GAME_RANGE;
}
enum kui_game_result kui_retail_image_check(const struct kui_retail_manifest *m,
    uint32_t lba, uint32_t count, enum kui_game_sector_format format) {
    if(!tracks_valid(m)) return KUI_GAME_INVALID;
    if(count > KUI_RETAIL_IMAGE_MAX_SECTORS) return KUI_GAME_RANGE;
    return range_check(m, lba, count, format);
}
enum kui_game_result kui_retail_manifest_validate(const struct kui_retail_manifest *m) {
    if(!tracks_valid(m) || !m->card_sectors || m->card_sectors > UINT64_C(0x100000000) ||
       m->partition_start >= m->partition_end || m->partition_end > m->card_sectors ||
       !m->extent_count || m->extent_count > KUI_RETAIL_IMAGE_EXTENTS ||
       !text_valid(m->title, sizeof(m->title), true) ||
       !text_valid(m->product, sizeof(m->product), false) ||
       !text_valid(m->bootfile, sizeof(m->bootfile), true) ||
       !text_valid(m->region, sizeof(m->region), false)) return KUI_GAME_INVALID;
    uint32_t next_extent = 0;
    bool session_found = false;
    for(uint32_t i = 0; i < m->track_count; ++i) {
        const struct kui_retail_track *t = &m->tracks[i];
        if(t->control == 4 && t->start_lba == m->session_lba &&
           m->session_lba >= 45000) session_found = true;
        if(t->first_extent != next_extent || !t->extent_count ||
           t->extent_count > m->extent_count - next_extent) return KUI_GAME_RANGE;
        uint32_t bytes = (t->end_lba - t->start_lba) * KUI_GAME_RAW_BYTES;
        uint32_t blocks = (bytes + 511u) / 512u, next_block = 0;
        for(uint32_t j = 0; j < t->extent_count; ++j) {
            const struct kui_retail_extent *e = &m->extents[next_extent + j];
            if(e->file_block != next_block || !e->blocks ||
               e->blocks > blocks - next_block ||
               e->card_lba < m->partition_start ||
               (uint64_t)e->card_lba + e->blocks > m->partition_end)
                return KUI_GAME_RANGE;
            next_block += e->blocks;
        }
        if(next_block != blocks) return KUI_GAME_FILE_SIZE;
        next_extent += t->extent_count;
    }
    if(next_extent != m->extent_count || !session_found || !m->boot_bytes ||
       m->boot_bytes > KUI_RETAIL_IMAGE_BOOT_MAX || m->boot_lba < m->session_lba)
        return KUI_GAME_INVALID;
    enum kui_game_result r = range_check(m, m->boot_lba,
        (m->boot_bytes + 2047u) / 2048u, KUI_GAME_SECTOR_MODE1);
    if(r != KUI_GAME_OK) return r;
    /* One-time validation. Per-command reads use the immutable validated map. */
    for(uint32_t i = 0; i < m->extent_count; ++i) {
        const struct kui_retail_extent *e = &m->extents[i];
        for(uint32_t j = 0; j < i; ++j) {
            const struct kui_retail_extent *q = &m->extents[j];
            if((uint64_t)e->card_lba < (uint64_t)q->card_lba + q->blocks &&
               (uint64_t)q->card_lba < (uint64_t)e->card_lba + e->blocks)
                return KUI_GAME_OVERLAP;
        }
    }
    return KUI_GAME_OK;
}
enum kui_game_result kui_retail_manifest_encode(const struct kui_retail_manifest *m,
    uint8_t out[KUI_RETAIL_IMAGE_WIRE_BYTES]) {
    if(!out) return KUI_GAME_INVALID;
    enum kui_game_result r = kui_retail_manifest_validate(m);
    if(r != KUI_GAME_OK) return r;
    memset(out, 0, KUI_RETAIL_IMAGE_WIRE_BYTES);
    memcpy(out, "KUIRTI01", 8);
    put32(out + 8, KUI_RETAIL_IMAGE_VERSION);
    put32(out + 12, KUI_RETAIL_IMAGE_WIRE_BYTES);
    put32(out + 20, m->track_count); put32(out + 24, m->extent_count);
    put64(out + 32, m->card_sectors);
    put64(out + 40, m->partition_start); put64(out + 48, m->partition_end);
    put32(out + 56, m->session_lba); put32(out + 60, m->boot_lba);
    put32(out + 64, m->boot_bytes); put32(out + 68, m->gdi_crc32);
    memcpy(out + 72, m->title, 128); memcpy(out + 200, m->product, 16);
    memcpy(out + 216, m->bootfile, 24); memcpy(out + 240, m->region, 16);
    put32(out + 256, m->boot_crc32); put32(out + 260, m->ip_crc32);
    for(uint32_t i = 0; i < m->track_count; ++i) {
        uint8_t *p = out + TRACK_BASE + i * TRACK_BYTES;
        const struct kui_retail_track *t = &m->tracks[i];
        put32(p, t->number); put32(p + 4, t->start_lba); put32(p + 8, t->end_lba);
        put32(p + 12, t->control); put32(p + 16, t->first_extent);
        put32(p + 20, t->extent_count);
    }
    for(uint32_t i = 0; i < m->extent_count; ++i) {
        uint8_t *p = out + EXTENT_BASE + i * 12;
        put32(p, m->extents[i].file_block); put32(p + 4, m->extents[i].card_lba);
        put32(p + 8, m->extents[i].blocks);
    }
    put32(out + 16, wire_crc(out));
    return KUI_GAME_OK;
}
enum kui_game_result kui_retail_manifest_decode(
    const uint8_t wire[KUI_RETAIL_IMAGE_WIRE_BYTES], struct kui_retail_manifest *m) {
    if(!m) return KUI_GAME_INVALID;
    memset(m, 0, sizeof(*m));
    if(!wire || memcmp(wire, "KUIRTI01", 8) ||
       get32(wire + 8) != KUI_RETAIL_IMAGE_VERSION ||
       get32(wire + 12) != KUI_RETAIL_IMAGE_WIRE_BYTES ||
       get32(wire + 16) != wire_crc(wire) || !zeroes(wire + 28, 4) ||
       !zeroes(wire + 264, 56) ||
       !zeroes(wire + USED_BYTES, KUI_RETAIL_IMAGE_WIRE_BYTES - USED_BYTES))
        return KUI_GAME_INVALID;
    m->track_count = get32(wire + 20); m->extent_count = get32(wire + 24);
    if(m->track_count > KUI_RETAIL_IMAGE_TRACKS ||
       m->extent_count > KUI_RETAIL_IMAGE_EXTENTS) goto invalid;
    m->card_sectors = get64(wire + 32); m->partition_start = get64(wire + 40);
    m->partition_end = get64(wire + 48); m->session_lba = get32(wire + 56);
    m->boot_lba = get32(wire + 60); m->boot_bytes = get32(wire + 64);
    m->gdi_crc32 = get32(wire + 68);
    m->boot_crc32 = get32(wire + 256); m->ip_crc32 = get32(wire + 260);
    memcpy(m->title, wire + 72, 128); memcpy(m->product, wire + 200, 16);
    memcpy(m->bootfile, wire + 216, 24); memcpy(m->region, wire + 240, 16);
    for(uint32_t i = 0; i < KUI_RETAIL_IMAGE_TRACKS; ++i) {
        const uint8_t *p = wire + TRACK_BASE + i * TRACK_BYTES;
        if(i >= m->track_count) { if(!zeroes(p, TRACK_BYTES)) goto invalid; continue; }
        if(!zeroes(p + 24, 8)) goto invalid;
        struct kui_retail_track *t = &m->tracks[i];
        t->number = get32(p); t->start_lba = get32(p + 4); t->end_lba = get32(p + 8);
        t->control = get32(p + 12); t->first_extent = get32(p + 16);
        t->extent_count = get32(p + 20);
    }
    for(uint32_t i = 0; i < KUI_RETAIL_IMAGE_EXTENTS; ++i) {
        const uint8_t *p = wire + EXTENT_BASE + i * 12;
        if(i >= m->extent_count) { if(!zeroes(p, 12)) goto invalid; continue; }
        m->extents[i].file_block = get32(p); m->extents[i].card_lba = get32(p + 4);
        m->extents[i].blocks = get32(p + 8);
    }
    enum kui_game_result r = kui_retail_manifest_validate(m);
    if(r != KUI_GAME_OK) memset(m, 0, sizeof(*m));
    return r;
invalid:
    memset(m, 0, sizeof(*m));
    return KUI_GAME_INVALID;
}
enum kui_game_result kui_retail_image_init(struct kui_retail_image *image,
    const struct kui_retail_manifest *manifest, kui_retail_read_block read, void *context) {
    if(!image) return KUI_GAME_INVALID;
    memset(image, 0, sizeof(*image));
    if(!read) return KUI_GAME_INVALID;
    enum kui_game_result r = kui_retail_manifest_validate(manifest);
    if(r != KUI_GAME_OK) return r;
    image->manifest = manifest; image->read_block = read; image->context = context;
    return KUI_GAME_OK;
}
static enum kui_game_result file_read(struct kui_retail_image *image,
    const struct kui_retail_track *track, uint32_t offset, uint8_t *out, uint32_t bytes) {
    const struct kui_retail_manifest *m = image->manifest;
    uint32_t file_bytes = (track->end_lba - track->start_lba) * KUI_GAME_RAW_BYTES;
    if(offset > file_bytes || bytes > file_bytes - offset) return KUI_GAME_RANGE;
    while(bytes) {
        uint32_t file_block = offset / 512u, inside = offset % 512u;
        uint32_t take = 512u - inside;
        if(take > bytes) take = bytes;
        /* Binary search within this track's extents, ordered by file block. */
        uint32_t lo = track->first_extent, hi = lo + track->extent_count;
        while(lo + 1 < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            if(m->extents[mid].file_block <= file_block) lo = mid;
            else hi = mid;
        }
        const struct kui_retail_extent *e = &m->extents[lo];
        if(file_block < e->file_block || file_block - e->file_block >= e->blocks)
            return KUI_GAME_RANGE;
        uint32_t card_lba = e->card_lba + file_block - e->file_block;
        if(!image->cache_valid || image->cached_lba != card_lba) {
            image->cache_valid = 0;
            if(image->read_block(image->context, card_lba, image->block)) return KUI_GAME_IO;
            ++image->blocks_read; image->cached_lba = card_lba; image->cache_valid = 1;
        }
        memcpy(out, image->block + inside, take);
        out += take; bytes -= take; offset += take;
    }
    return KUI_GAME_OK;
}
enum kui_game_result kui_retail_image_read(struct kui_retail_image *image,
    uint32_t lba, uint32_t count, enum kui_game_sector_format format, void *out, size_t capacity) {
    if(!image || !image->manifest || !image->read_block || !out) return KUI_GAME_INVALID;
    enum kui_game_result r = kui_retail_image_check(image->manifest, lba, count, format);
    if(r != KUI_GAME_OK) return r;
    uint32_t sector_bytes = format == KUI_GAME_SECTOR_RAW ? KUI_GAME_RAW_BYTES : KUI_GAME_DATA_BYTES;
    if(capacity < (size_t)sector_bytes * count) return KUI_GAME_RANGE;
    uint8_t *destination = out;
    uint32_t track_index = 0;
    for(uint32_t i = 0; i < count; ++i) {
        uint32_t current = lba + i;
        while(image->manifest->tracks[track_index].end_lba <= current) ++track_index;
        const struct kui_retail_track *t = &image->manifest->tracks[track_index];
        uint32_t offset = (current - t->start_lba) * KUI_GAME_RAW_BYTES;
        if(format == KUI_GAME_SECTOR_MODE1) {
            uint8_t header[16];
            r = file_read(image, t, offset, header, sizeof(header));
            if(r != KUI_GAME_OK) return r;
            if(header[0] || header[11] || header[15] != 1) return KUI_GAME_MODE;
            for(unsigned j = 1; j < 11; ++j) if(header[j] != 255) return KUI_GAME_MODE;
            offset += sizeof(header);
        }
        r = file_read(image, t, offset, destination, sector_bytes);
        if(r != KUI_GAME_OK) return r;
        destination += sector_bytes;
    }
    return KUI_GAME_OK;
}
