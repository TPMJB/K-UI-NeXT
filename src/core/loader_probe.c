/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/loader_probe.h"

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t n) {
    for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (8u * i));
}
static void copy(uint8_t *out, const uint8_t *in, uint32_t bytes) {
    for(uint32_t i = 0; i < bytes; ++i) out[i] = in[i];
}
static uint32_t checksum(const uint8_t *p) {
    uint32_t c = UINT32_MAX;
    for(uint32_t i = 0; i < KUI_LOADER_PROBE_MANIFEST_BYTES; ++i) {
        c ^= (i >= 16 && i < 20) ? 0 : p[i];
        for(unsigned bit = 0; bit < 8; ++bit)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return c ^ UINT32_MAX;
}
static enum kui_loader_probe_result manifest_valid(
    const struct kui_loader_probe_manifest *m) {
    if(!m || !m->card_sectors || !m->extent_count ||
       m->extent_count > KUI_LOADER_PROBE_EXTENTS) return KUI_LP_INVALID;
    uint32_t next = 0;
    for(uint32_t i = 0; i < m->extent_count; ++i) {
        const struct kui_loader_probe_extent *e = &m->extents[i];
        if(e->file_block != next || !e->blocks ||
           e->blocks > KUI_LOADER_PROBE_BLOCKS - next ||
           e->card_lba >= m->card_sectors ||
           e->blocks > m->card_sectors - e->card_lba) return KUI_LP_RANGE;
        for(uint32_t j = 0; j < i; ++j) {
            const struct kui_loader_probe_extent *q = &m->extents[j];
            if(e->card_lba < q->card_lba + q->blocks &&
               q->card_lba < e->card_lba + e->blocks) return KUI_LP_RANGE;
        }
        next += e->blocks;
    }
    return next == KUI_LOADER_PROBE_BLOCKS ? KUI_LP_OK : KUI_LP_RANGE;
}
enum kui_loader_probe_result kui_loader_probe_manifest_encode(
    const struct kui_loader_probe_manifest *m, uint8_t out[1600]) {
    enum kui_loader_probe_result r = manifest_valid(m);
    if(r != KUI_LP_OK) return r;
    if(!out) return KUI_LP_INVALID;
    for(uint32_t i = 0; i < 1600; ++i) out[i] = 0;
    copy(out, (const uint8_t *)"KUIG3P01", 8);
    put32(out + 8, KUI_LOADER_PROBE_VERSION); put32(out + 12, 1600);
    put32(out + 24, m->card_sectors);
    put32(out + 28, KUI_LOADER_PROBE_FILE_BYTES);
    put32(out + 32, m->extent_count);
    for(uint32_t i = 0; i < m->extent_count; ++i) {
        put32(out + 64 + i * 12, m->extents[i].file_block);
        put32(out + 68 + i * 12, m->extents[i].card_lba);
        put32(out + 72 + i * 12, m->extents[i].blocks);
    }
    put32(out + 16, checksum(out));
    return KUI_LP_OK;
}
enum kui_loader_probe_result kui_loader_probe_manifest_decode(
    const uint8_t wire[1600], struct kui_loader_probe_manifest *out) {
    if(!wire || !out) return KUI_LP_INVALID;
    for(uint32_t i = 0; i < 8; ++i)
        if(wire[i] != (uint8_t)"KUIG3P01"[i]) return KUI_LP_INVALID;
    if(get32(wire + 8) != KUI_LOADER_PROBE_VERSION ||
       get32(wire + 12) != 1600 ||
       get32(wire + 28) != KUI_LOADER_PROBE_FILE_BYTES)
        return KUI_LP_INVALID;
    if(get32(wire + 16) != checksum(wire)) return KUI_LP_CHECKSUM;
    if(get32(wire + 20)) return KUI_LP_INVALID;
    for(uint32_t i = 36; i < 64; ++i)
        if(wire[i]) return KUI_LP_INVALID;
    struct kui_loader_probe_manifest m = {0};
    m.card_sectors = get32(wire + 24); m.extent_count = get32(wire + 32);
    if(m.extent_count > KUI_LOADER_PROBE_EXTENTS) return KUI_LP_INVALID;
    for(uint32_t i = 0; i < KUI_LOADER_PROBE_EXTENTS; ++i) {
        m.extents[i].file_block = get32(wire + 64 + i * 12);
        m.extents[i].card_lba = get32(wire + 68 + i * 12);
        m.extents[i].blocks = get32(wire + 72 + i * 12);
        if(i >= m.extent_count && (m.extents[i].file_block ||
            m.extents[i].card_lba || m.extents[i].blocks)) return KUI_LP_INVALID;
    }
    enum kui_loader_probe_result r = manifest_valid(&m);
    if(r == KUI_LP_OK) *out = m;
    return r;
}
static int sector(uint32_t lba) {
    if(lba < 12) return (int)lba;
    if(lba >= 45000 && lba < 45012) return (int)(lba - 45000 + 12);
    return -1;
}
static uint8_t bcd(uint32_t n) { return (uint8_t)((n / 10u) * 16u + n % 10u); }
uint8_t kui_loader_probe_fixture_byte(uint32_t file_byte) {
    if(file_byte >= 24u * 2352u) return 0;
    uint32_t s = file_byte / 2352u, p = file_byte % 2352u;
    if(s < 8 || s >= 12) {
        if(p == 0 || p == 11) return 0;
        if(p < 11) return 0xff;
        uint32_t fad = (s < 12 ? s : s - 12 + 45000) + 150;
        if(p == 12) return bcd(fad / 4500u);
        if(p == 13) return bcd(fad / 75u % 60u);
        if(p == 14) return bcd(fad % 75u);
        if(p == 15) return 1;
    }
    return (uint8_t)((s * 37u) ^ (p * 13u) ^ (p >> 8) ^ 0xa5u);
}
enum kui_loader_probe_result kui_loader_probe_init(
    struct kui_loader_probe_service *s, const struct kui_loader_probe_manifest *m,
    kui_loader_probe_read_block read, void *context) {
    if(!s || !read) return KUI_LP_INVALID;
    enum kui_loader_probe_result r = manifest_valid(m);
    if(r != KUI_LP_OK) return r;
    s->manifest = *m; s->read = read; s->read_context = context;
    s->token = 0; s->pending = 0; s->cache_valid = 0; s->blocks_read = 0;
    s->completed_bytes = 0; s->completion = KUI_LP_INVALID;
    return KUI_LP_OK;
}
static enum kui_loader_probe_result preflight(
    const struct kui_loader_probe_request *r, uint32_t capacity) {
    if(r->opcode == KUI_LP_STATUS || r->opcode == KUI_LP_TOC) {
        if(r->lba || r->count || r->format) return KUI_LP_INVALID;
        return capacity >= (r->opcode == KUI_LP_STATUS ? 16u : 48u) ?
            KUI_LP_OK : KUI_LP_RANGE;
    }
    if(r->opcode != KUI_LP_READ) return KUI_LP_UNSUPPORTED;
    if(r->format != KUI_LP_RAW && r->format != KUI_LP_MODE1)
        return KUI_LP_UNSUPPORTED;
    if(!r->count || r->count > KUI_LOADER_PROBE_MAX_SECTORS ||
       r->lba >= 45012 || r->count > 45012 - r->lba) return KUI_LP_RANGE;
    uint32_t bytes = r->format == KUI_LP_RAW ? 2352u : 2048u;
    if(capacity < r->count * bytes) return KUI_LP_RANGE;
    for(uint32_t i = 0; i < r->count; ++i) {
        int index = sector(r->lba + i);
        if(index < 0) return KUI_LP_GAP;
        if(r->format == KUI_LP_MODE1 && index >= 8 && index < 12)
            return KUI_LP_AUDIO;
    }
    return KUI_LP_OK;
}
enum kui_loader_probe_result kui_loader_probe_submit(
    struct kui_loader_probe_service *s, const struct kui_loader_probe_request *r,
    void *out, uint32_t capacity, uint32_t *token) {
    if(!s || !r || !out || !token || !s->read) return KUI_LP_INVALID;
    if(s->pending) return KUI_LP_BUSY;
    enum kui_loader_probe_result result = preflight(r, capacity);
    if(result != KUI_LP_OK) return result;
    s->request = *r; s->output = out; s->capacity = capacity;
    ++s->token; if(!s->token) ++s->token;
    *token = s->token; s->pending = 1; s->completion = KUI_LP_PENDING;
    s->completed_bytes = 0;
    /* Do not let an earlier command's cache stand in for a fresh SD read. */
    s->cache_valid = 0;
    return KUI_LP_OK;
}
static enum kui_loader_probe_result read_file(
    struct kui_loader_probe_service *s, uint32_t offset, uint8_t *out,
    uint32_t bytes) {
    while(bytes) {
        uint32_t block = offset / 512u, inside = offset % 512u;
        uint32_t count = 512u - inside;
        if(count > bytes) count = bytes;
        if(!s->cache_valid || s->cached_block != block) {
            uint32_t lba = 0, found = 0;
            for(uint32_t i = 0; i < s->manifest.extent_count; ++i) {
                const struct kui_loader_probe_extent *e = &s->manifest.extents[i];
                if(block >= e->file_block && block - e->file_block < e->blocks) {
                    lba = e->card_lba + block - e->file_block; found = 1; break;
                }
            }
            if(!found) return KUI_LP_RANGE;
            s->cache_valid = 0;
            if(s->read(s->read_context, lba, s->block)) return KUI_LP_IO;
            ++s->blocks_read; s->cached_block = block; s->cache_valid = 1;
        }
        copy(out, s->block + inside, count);
        out += count; offset += count; bytes -= count;
    }
    return KUI_LP_OK;
}
static enum kui_loader_probe_result execute(struct kui_loader_probe_service *s) {
    uint8_t *out = s->output;
    if(s->request.opcode == KUI_LP_STATUS) {
        put32(out, 1); put32(out + 4, 1); put32(out + 8, 3);
        put32(out + 12, KUI_LOADER_PROBE_MAX_SECTORS);
        s->completed_bytes = 16; return KUI_LP_OK;
    }
    if(s->request.opcode == KUI_LP_TOC) {
        static const uint32_t toc[12] = {1, 4, 0, 8, 2, 0, 8, 12,
                                       3, 4, 45000, 45012};
        for(uint32_t i = 0; i < 12; ++i) put32(out + i * 4, toc[i]);
        s->completed_bytes = 48; return KUI_LP_OK;
    }
    uint32_t bytes = s->request.format == KUI_LP_RAW ? 2352u : 2048u;
    for(uint32_t i = 0; i < s->request.count; ++i) {
        int index = sector(s->request.lba + i);
        if(index < 0) return KUI_LP_RANGE;
        enum kui_loader_probe_result r = read_file(s, (uint32_t)index * 2352u,
                                                   s->raw, 2352);
        if(r != KUI_LP_OK) return r;
        uint32_t offset = 0;
        if(s->request.format == KUI_LP_MODE1) {
            if(s->raw[0] || s->raw[11] || s->raw[15] != 1) return KUI_LP_FORMAT;
            for(unsigned j = 1; j < 11; ++j)
                if(s->raw[j] != 0xff) return KUI_LP_FORMAT;
            offset = 16;
        }
        copy(out + i * bytes, s->raw + offset, bytes);
    }
    s->completed_bytes = s->request.count * bytes;
    return KUI_LP_OK;
}
enum kui_loader_probe_result kui_loader_probe_poll(
    struct kui_loader_probe_service *s, uint32_t token, uint32_t *bytes) {
    if(!s || !bytes || !token || token != s->token) return KUI_LP_INVALID;
    if(s->pending) {
        s->completion = execute(s); s->pending = 0;
    }
    *bytes = s->completed_bytes;
    return s->completion;
}
enum kui_loader_probe_result kui_loader_probe_cancel(
    struct kui_loader_probe_service *s, uint32_t token) {
    if(!s || !token || token != s->token) return KUI_LP_INVALID;
    if(!s->pending) return s->completion;
    s->pending = 0; s->completed_bytes = 0; s->completion = KUI_LP_CANCELLED;
    return KUI_LP_CANCELLED;
}
const char *kui_loader_probe_result_name(enum kui_loader_probe_result r) {
    static const char *const names[] = {"OK", "pending", "busy", "invalid",
        "checksum", "range", "gap", "audio", "unsupported", "I/O",
        "cancelled", "sector format"};
    return (unsigned)r < sizeof(names) / sizeof(names[0]) ? names[r] : "unknown";
}
