/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_image.h"
#include <stdbool.h>
#include <string.h>

#define RECORD_BASE 320u
#define RECORD_BYTES 12u
_Static_assert(RECORD_BASE + KUI_RETAIL_IMAGE_SLOTS * RECORD_BYTES <= KUI_RETAIL_IMAGE_WIRE_BYTES,
               "manifest wire capacity");
_Static_assert(sizeof(struct kui_retail_manifest) < 4096u, "bounded manifest");

#if KUI_RETAIL_MANIFEST_SLOTS == KUI_RETAIL_IMAGE_SLOTS
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
#endif
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
#if KUI_RETAIL_MANIFEST_SLOTS == KUI_RETAIL_IMAGE_SLOTS
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
#endif
static bool tracks_valid(const struct kui_retail_manifest *m) {
    if(!m || !m->track_count || m->track_count > KUI_RETAIL_IMAGE_TRACKS ||
       m->track_count > KUI_RETAIL_MANIFEST_SLOTS) return false;
    for(uint32_t i = 0; i < m->track_count; ++i) {
        const struct kui_retail_track *t = &m->slots[i].track;
        if(t->start_lba >= t->end_lba || t->end_lba > KUI_GAME_LBA_LIMIT ||
           (!(m->flags & KUI_RETAIL_IMAGE_CD) && t->start_lba < 45000u && t->end_lba > 45000u) ||
           (t->control & 3u) ||
           (!(t->control & 4u) && (t->control & (KUI_RETAIL_TRACK_COOKED | KUI_RETAIL_TRACK_MODE2 | KUI_RETAIL_TRACK_2336))) ||
           ((t->control & KUI_RETAIL_TRACK_COOKED) && (t->control & (KUI_RETAIL_TRACK_2336 | KUI_RETAIL_TRACK_2448))) ||
           ((t->control & KUI_RETAIL_TRACK_2336) && (!(t->control & KUI_RETAIL_TRACK_MODE2) || (t->control & KUI_RETAIL_TRACK_2448))) ||
           (i && m->slots[i - 1].track.end_lba > t->start_lba)) return false;
    }
    return true;
}
static enum kui_game_result range_check(const struct kui_retail_manifest *m,
    uint32_t lba, uint32_t count, enum kui_game_sector_format format) {
    if(!count || (format != KUI_GAME_SECTOR_RAW && format != KUI_GAME_SECTOR_MODE1))
        return KUI_GAME_INVALID;
    if(lba < m->slots[0].track.start_lba || lba >= KUI_GAME_LBA_LIMIT ||
       count > KUI_GAME_LBA_LIMIT - lba ||
       lba + count > m->slots[m->track_count - 1].track.end_lba) return KUI_GAME_RANGE;
    uint32_t cursor = lba, end = lba + count;
    for(uint32_t i = 0; i < m->track_count && cursor < end; ++i) {
        const struct kui_retail_track *t = &m->slots[i].track;
        if(t->end_lba <= cursor) continue;
        if(t->start_lba > cursor) return KUI_GAME_GAP;
        /* Data tracks always have extents; an unmapped audio track has none. */
        /* Full map validation admits only audio 0, raw data 4 or cooked
         * data 0x84. The data bit therefore identifies Mode 1 eligibility. */
        if(!t->extent_count || (format == KUI_GAME_SECTOR_MODE1 && !(t->control & 4u)))
            return KUI_GAME_AUDIO;
        if(format == KUI_GAME_SECTOR_RAW && kui_retail_track_sector_bytes(t) != 2352u &&
           kui_retail_track_sector_bytes(t) != 2448u)
            return KUI_GAME_UNSUPPORTED;
        cursor = t->end_lba < end ? t->end_lba : end;
    }
    return cursor == end ? KUI_GAME_OK : KUI_GAME_RANGE;
}
enum kui_game_result kui_retail_image_check_validated(const struct kui_retail_manifest *m,
    uint32_t lba, uint32_t count, enum kui_game_sector_format format) {
    if(count > KUI_RETAIL_IMAGE_MAX_SECTORS) return KUI_GAME_RANGE;
    return range_check(m, lba, count, format);
}
enum kui_game_result kui_retail_image_check(const struct kui_retail_manifest *m,
    uint32_t lba, uint32_t count, enum kui_game_sector_format format) {
    if(!tracks_valid(m)) return KUI_GAME_INVALID;
    return kui_retail_image_check_validated(m, lba, count, format);
}
#if KUI_RETAIL_MANIFEST_SLOTS == KUI_RETAIL_IMAGE_SLOTS
static const struct kui_retail_track *extent_owner(const struct kui_retail_manifest *m,uint32_t index,uint32_t *owner) {
    for(uint32_t i=0;i<m->track_count;i++) {
        const struct kui_retail_track *t=&m->slots[i].track;
        uint32_t first=kui_retail_track_first_extent(t);
        if(index>=first && index-first<t->extent_count) {*owner=i;return t;}
    }
    return NULL;
}
/* A shared container can end one track and begin the next inside the same
 * allocated block. Only that boundary block may overlap, and its used byte
 * slices must be disjoint; padding never grants permission for data aliases. */
static bool shared_boundary(const struct kui_retail_manifest *m,uint32_t ei,uint32_t qi) {
    const struct kui_retail_extent *e=&m->slots[ei].extent,*q=&m->slots[qi].extent;
    uint64_t first=e->card_lba>q->card_lba?e->card_lba:q->card_lba;
    uint64_t last=(uint64_t)e->card_lba+e->blocks;
    if(last>(uint64_t)q->card_lba+q->blocks) last=(uint64_t)q->card_lba+q->blocks;
    if(last-first!=1u) return false;
    uint32_t a,b;
    const struct kui_retail_track *ta=extent_owner(m,ei,&a),*tb=extent_owner(m,qi,&b);
    if(!ta || !tb || (a+1u!=b && b+1u!=a)) return false;
    if(a>b) {const struct kui_retail_track *tt=ta;ta=tb;tb=tt;
        const struct kui_retail_extent *te=e;e=q;q=te;}
    uint32_t end=kui_retail_track_file_offset(ta)+(ta->end_lba-ta->start_lba)*kui_retail_track_sector_bytes(ta);
    uint32_t begin=kui_retail_track_file_offset(tb);
    return (end & 511u) && begin && (end & 511u)<=begin &&
        e->file_block+(uint32_t)(first-e->card_lba)==(end-1u)/512u &&
        q->file_block+(uint32_t)(first-q->card_lba)==0;
}
enum kui_game_result kui_retail_manifest_validate(const struct kui_retail_manifest *m) {
    if(!tracks_valid(m) || m->storage_transport > KUI_STORAGE_IDE || !m->card_sectors || m->card_sectors > UINT64_C(0x100000000) ||
       (m->flags & ~(KUI_RETAIL_IMAGE_CD | KUI_RETAIL_IMAGE_SCRAMBLED | KUI_RETAIL_IMAGE_BOOT_CRC)) ||
       ((m->flags & KUI_RETAIL_IMAGE_SCRAMBLED) && !(m->flags & KUI_RETAIL_IMAGE_CD)) ||
       m->reader > KUI_RETAIL_READER_ASYNC_EAGER || (m->reader != KUI_RETAIL_READER_STANDARD &&
           (m->storage_transport != KUI_STORAGE_SCI || m->track_count > KUI_RETAIL_ASYNC_SLOTS ||
            m->extent_count > KUI_RETAIL_ASYNC_SLOTS - m->track_count)) ||
       m->partition_start >= m->partition_end || m->partition_end > m->card_sectors ||
       !m->extent_count || m->extent_count > KUI_RETAIL_IMAGE_SLOTS - m->track_count ||
       !text_valid(m->title, sizeof(m->title), true) ||
       !text_valid(m->product, sizeof(m->product), false) ||
       !text_valid(m->bootfile, sizeof(m->bootfile), true) ||
       !text_valid(m->region, sizeof(m->region), false)) return KUI_GAME_INVALID;
    const uint32_t used = m->track_count + m->extent_count;
    uint32_t next_extent = m->track_count;
    bool session_found = false;
    for(uint32_t i = 0; i < m->track_count; ++i) {
        const struct kui_retail_track *t = &m->slots[i].track;
        if(kui_retail_track_control(t) == 4 && t->start_lba == m->session_lba &&
           ((m->flags & KUI_RETAIL_IMAGE_CD) || m->session_lba >= 45000)) session_found = true;
        if(kui_retail_track_first_extent(t) != next_extent || t->extent_count > used - next_extent ||
           (!t->extent_count && kui_retail_track_control(t) == 4)) return KUI_GAME_RANGE;
        if(!t->extent_count) continue; /* Audio listed without its file mapped. */
        uint32_t bytes = kui_retail_track_file_offset(t)+(t->end_lba - t->start_lba) * kui_retail_track_sector_bytes(t);
        uint32_t blocks = (bytes + 511u) / 512u, next_block = 0;
        for(uint32_t j = 0; j < t->extent_count; ++j) {
            const struct kui_retail_extent *e = &m->slots[next_extent + j].extent;
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
    if(next_extent != used || !session_found || !m->boot_bytes ||
       m->boot_bytes > KUI_RETAIL_IMAGE_BOOT_MAX || m->boot_lba < m->session_lba)
        return KUI_GAME_INVALID;
    enum kui_game_result r = range_check(m, m->boot_lba,
        (m->boot_bytes + 2047u) / 2048u, KUI_GAME_SECTOR_MODE1);
    if(r != KUI_GAME_OK) return r;
    /* One-time validation. Per-command reads use the immutable validated map. */
    for(uint32_t i = m->track_count; i < used; ++i) {
        const struct kui_retail_extent *e = &m->slots[i].extent;
        for(uint32_t j = m->track_count; j < i; ++j) {
            const struct kui_retail_extent *q = &m->slots[j].extent;
            if((uint64_t)e->card_lba < (uint64_t)q->card_lba + q->blocks &&
               (uint64_t)q->card_lba < (uint64_t)e->card_lba + e->blocks && !shared_boundary(m,i,j))
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
    memcpy(out, "KUIRTI03", 8);
    put32(out + 8, KUI_RETAIL_IMAGE_VERSION);
    put32(out + 12, KUI_RETAIL_IMAGE_WIRE_BYTES);
    put32(out + 20, m->track_count); put32(out + 24, m->extent_count);
    put32(out + 28, m->storage_transport);
    put64(out + 32, m->card_sectors);
    put64(out + 40, m->partition_start); put64(out + 48, m->partition_end);
    put32(out + 56, m->session_lba); put32(out + 60, m->boot_lba);
    put32(out + 64, m->boot_bytes); put32(out + 68, m->gdi_crc32);
    memcpy(out + 72, m->title, 128); memcpy(out + 200, m->product, 16);
    memcpy(out + 216, m->bootfile, 24); memcpy(out + 240, m->region, 16);
    put32(out + 256, m->boot_crc32); put32(out + 260, m->ip_crc32);
    put32(out + 264, m->reader);
    put32(out + 268, m->flags);
    for(uint32_t i = 0; i < m->track_count + m->extent_count; ++i) {
        uint8_t *p = out + RECORD_BASE + i * RECORD_BYTES;
        if(i < m->track_count) {
            const struct kui_retail_track *t = &m->slots[i].track;
            put32(p, t->start_lba); put32(p + 4, t->end_lba);
            p[8] = t->control; p[9] = t->extent_count;
            p[10] = (uint8_t)kui_retail_track_file_offset(t);
        } else {
            const struct kui_retail_extent *e = &m->slots[i].extent;
            put32(p, e->file_block); put32(p + 4, e->card_lba); put32(p + 8, e->blocks);
        }
    }
    put32(out + 16, wire_crc(out));
    return KUI_GAME_OK;
}
enum kui_game_result kui_retail_manifest_decode(
    const uint8_t wire[KUI_RETAIL_IMAGE_WIRE_BYTES], struct kui_retail_manifest *m) {
    if(!m) return KUI_GAME_INVALID;
    memset(m, 0, sizeof(*m));
    if(!wire || memcmp(wire, "KUIRTI03", 8) ||
       get32(wire + 8) != KUI_RETAIL_IMAGE_VERSION ||
       get32(wire + 12) != KUI_RETAIL_IMAGE_WIRE_BYTES ||
       get32(wire + 16) != wire_crc(wire) ||
       !zeroes(wire + 272, 48))
        return KUI_GAME_INVALID;
    m->track_count = get32(wire + 20); m->extent_count = get32(wire + 24);
    m->storage_transport = get32(wire + 28);
    if(m->track_count > KUI_RETAIL_IMAGE_TRACKS ||
       m->extent_count > KUI_RETAIL_IMAGE_SLOTS - m->track_count) goto invalid;
    const uint32_t used = m->track_count + m->extent_count;
    const uint8_t *unused = wire + RECORD_BASE + used * RECORD_BYTES;
    if(!zeroes(unused, (size_t)(wire + KUI_RETAIL_IMAGE_WIRE_BYTES - unused))) goto invalid;
    m->card_sectors = get64(wire + 32); m->partition_start = get64(wire + 40);
    m->partition_end = get64(wire + 48); m->session_lba = get32(wire + 56);
    m->boot_lba = get32(wire + 60); m->boot_bytes = get32(wire + 64);
    m->gdi_crc32 = get32(wire + 68);
    m->boot_crc32 = get32(wire + 256); m->ip_crc32 = get32(wire + 260);
    m->reader = get32(wire + 264);
    m->flags = get32(wire + 268);
    memcpy(m->title, wire + 72, 128); memcpy(m->product, wire + 200, 16);
    memcpy(m->bootfile, wire + 216, 24); memcpy(m->region, wire + 240, 16);
    uint32_t next_extent = m->track_count;
    for(uint32_t i = 0; i < used; ++i) {
        const uint8_t *p = wire + RECORD_BASE + i * RECORD_BYTES;
        if(i < m->track_count) {
            if(p[11]) goto invalid;
            struct kui_retail_track *t = &m->slots[i].track;
            t->start_lba = get32(p); t->end_lba = get32(p + 4);
            t->control = p[8];
            t->extent_count = p[9];
            t->first_extent = (uint16_t)(next_extent | (uint32_t)p[10]<<8); next_extent += p[9];
        } else {
            struct kui_retail_extent *e = &m->slots[i].extent;
            e->file_block = get32(p); e->card_lba = get32(p + 4); e->blocks = get32(p + 8);
        }
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
#endif
static enum kui_game_result file_read(struct kui_retail_image *image,
    const struct kui_retail_track *track, uint32_t offset, uint8_t *out,
    uint32_t bytes, uint32_t limit) {
    const struct kui_retail_manifest *m = image->manifest;
    offset += kui_retail_track_file_offset(track);
    /* Private calls follow the full immutable-map request preflight. Their
     * offsets identify a checked sector in this track, and their copy ends
     * within its stored stride (including bounded read_part slices). No file
     * padding is exposed. Keep physical extent and stream-limit checks here. */
    while(bytes) {
        uint32_t file_block = offset / 512u, inside = offset % 512u;
        uint32_t take = 512u - inside;
        if(take > bytes) take = bytes;
        /* Binary search within this track's extents, ordered by file block. */
        uint32_t lo = kui_retail_track_first_extent(track), hi = lo + track->extent_count;
        while(lo + 1 < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            if(m->slots[mid].extent.file_block <= file_block) lo = mid;
            else hi = mid;
        }
        const struct kui_retail_extent *e = &m->slots[lo].extent;
        if(file_block < e->file_block || file_block - e->file_block >= e->blocks)
            return KUI_GAME_RANGE;
        uint32_t card_lba = e->card_lba + file_block - e->file_block;
        if(!image->cache_valid || image->cached_lba != card_lba) {
            image->cache_valid = 0;
            int failed;
            if(image->read_run) {
                if(file_block >= limit) return KUI_GAME_RANGE;
                uint32_t available = limit - file_block;
                uint32_t extent_left = e->blocks - (file_block - e->file_block);
                if(available > extent_left) available = extent_left;
                failed = image->read_run(image->context, card_lba, available, image->block);
            } else {
                failed = image->read_block(image->context, card_lba, image->block);
            }
            if(failed) return KUI_GAME_IO;
            ++image->blocks_read; image->cached_lba = card_lba; image->cache_valid = 1;
        }
        memcpy(out, image->block + inside, take);
        out += take; bytes -= take; offset += take;
    }
    return KUI_GAME_OK;
}
static enum kui_game_result data_header(struct kui_retail_image *image,
    const struct kui_retail_track *t,uint32_t offset,uint32_t limit) {
    uint32_t bytes=kui_retail_track_header_bytes(t);
    if(!bytes) return KUI_GAME_OK;
    uint8_t header[24];
    enum kui_game_result r=file_read(image,t,offset,header,bytes,limit);
    if(r!=KUI_GAME_OK) return r;
    uint32_t sub=0;
    if(!(t->control & KUI_RETAIL_TRACK_2336)) {
        if(header[0] || header[11] || header[15]!=((t->control & KUI_RETAIL_TRACK_MODE2)?2:1))
            return KUI_GAME_MODE;
        for(unsigned i=1;i<11;i++) if(header[i]!=255) return KUI_GAME_MODE;
        if(!(t->control & KUI_RETAIL_TRACK_MODE2)) return KUI_GAME_OK;
        sub=16;
    }
    if(memcmp(header+sub,header+sub+4,4) || (header[sub+2]&0x20u)) return KUI_GAME_MODE;
    return KUI_GAME_OK;
}
enum kui_game_result kui_retail_image_read(struct kui_retail_image *image,
    uint32_t lba, uint32_t count, enum kui_game_sector_format format, void *out, size_t capacity) {
    if(!image || !image->manifest || !image->read_block || !out) return KUI_GAME_INVALID;
    enum kui_game_result r = kui_retail_image_check_validated(image->manifest, lba, count, format);
    if(r != KUI_GAME_OK) return r;
    uint32_t sector_bytes = format == KUI_GAME_SECTOR_RAW ? KUI_GAME_RAW_BYTES : KUI_GAME_DATA_BYTES;
    if(capacity < (size_t)sector_bytes * count) return KUI_GAME_RANGE;
    uint8_t *destination = out;
    uint32_t track_index = 0;
    for(uint32_t i = 0; i < count; ++i) {
        uint32_t current = lba + i;
        while(image->manifest->slots[track_index].track.end_lba <= current) ++track_index;
        const struct kui_retail_track *t = &image->manifest->slots[track_index].track;
        uint32_t stride = kui_retail_track_sector_bytes(t);
        uint32_t offset = (current - t->start_lba) * stride;
        uint32_t end = lba + count;
        if(end > t->end_lba) end = t->end_lba;
        uint32_t header_bytes=format==KUI_GAME_SECTOR_MODE1?kui_retail_track_header_bytes(t):0;
        uint32_t last_byte=kui_retail_track_file_offset(t)+(end-1u-t->start_lba)*stride+header_bytes+sector_bytes;
        uint32_t limit = (last_byte + 511u) / 512u;
        if(header_bytes) {
            r = data_header(image,t,offset,limit);
            if(r != KUI_GAME_OK) return r;
            offset += header_bytes;
        }
        r = file_read(image, t, offset, destination, sector_bytes, limit);
        if(r != KUI_GAME_OK) return r;
        destination += sector_bytes;
    }
    return KUI_GAME_OK;
}
enum kui_game_result kui_retail_image_read_part(struct kui_retail_image *image,
    uint32_t lba, uint32_t skip, uint32_t bytes, enum kui_game_sector_format format, void *out) {
    if(!image || !image->manifest || !image->read_block || !out || !bytes)
        return KUI_GAME_INVALID;
    uint32_t sector_bytes = format == KUI_GAME_SECTOR_RAW ? KUI_GAME_RAW_BYTES : KUI_GAME_DATA_BYTES;
    uint32_t first = skip / sector_bytes;
    skip %= sector_bytes;
    if(bytes > KUI_RETAIL_IMAGE_MAX_SECTORS * sector_bytes - skip || first > UINT32_MAX - lba)
        return KUI_GAME_RANGE;
    lba += first;
    uint32_t count = (skip + bytes + sector_bytes - 1u) / sector_bytes;
    enum kui_game_result r = kui_retail_image_check_validated(image->manifest, lba, count, format);
    if(r != KUI_GAME_OK) return r;
    uint8_t *destination = out;
    uint32_t track_index = 0;
    for(uint32_t i = 0; i < count; ++i) {
        uint32_t current = lba + i;
        while(image->manifest->slots[track_index].track.end_lba <= current) ++track_index;
        const struct kui_retail_track *t = &image->manifest->slots[track_index].track;
        uint32_t stride = kui_retail_track_sector_bytes(t);
        uint32_t offset = (current - t->start_lba) * stride;
        uint32_t end = lba + count;
        if(end > t->end_lba) end = t->end_lba;
        uint32_t header_bytes=format==KUI_GAME_SECTOR_MODE1?kui_retail_track_header_bytes(t):0;
        uint32_t last_byte=kui_retail_track_file_offset(t)+(end-1u-t->start_lba)*stride+header_bytes+sector_bytes;
        uint32_t limit = (last_byte + 511u) / 512u;
        if(header_bytes) {
            r = data_header(image,t,offset,limit);
            if(r != KUI_GAME_OK) return r;
            offset += header_bytes;
        }
        uint32_t take = sector_bytes - skip;
        if(take > bytes) take = bytes;
        r = file_read(image, t, offset + skip, destination, take, limit);
        if(r != KUI_GAME_OK) return r;
        destination += take; bytes -= take; skip = 0;
    }
    return KUI_GAME_OK;
}
static uint8_t bcd(uint32_t value) { return (uint8_t)((value / 10u) << 4 | value % 10u); }
enum kui_retail_header kui_retail_sector_header(const uint8_t raw[KUI_GAME_RAW_BYTES],
    uint32_t lba) {
    if(raw[0] || raw[11]) return KUI_RETAIL_HEADER_SYNC;
    for(unsigned i = 1; i < 11; ++i) if(raw[i] != 255) return KUI_RETAIL_HEADER_SYNC;
    if(raw[15] != 1) return KUI_RETAIL_HEADER_MODE;
    uint32_t fad = lba + 150u;
    if(lba >= KUI_GAME_LBA_LIMIT || raw[12] != bcd(fad / 4500u) ||
       raw[13] != bcd(fad / 75u % 60u) || raw[14] != bcd(fad % 75u))
        return KUI_RETAIL_HEADER_ADDRESS;
    return KUI_RETAIL_HEADER_OK;
}
