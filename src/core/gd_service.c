/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/gd_service.h"
#include <stddef.h>
#include <string.h>

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void write32(uint8_t *p, uint32_t n) {
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8);
    p[2] = (uint8_t)(n >> 16); p[3] = (uint8_t)(n >> 24);
}
static uint8_t *guest(struct kui_gd_service *s, uint32_t address,
                       uint32_t bytes, uint32_t alignment, int writing) {
    /* Accept only real main RAM's physical, cached P1 or uncached P2 aliases.
     * Masking arbitrary input would admit peripherals or wrapped pointers. */
    uint32_t area = address & 0xff000000u;
    if(area != 0x0c000000u && area != 0x8c000000u && area != 0xac000000u)
        return NULL;
    uint32_t p1 = (address & 0x00ffffffu) | 0x8c000000u;
    if(!bytes || (p1 & (alignment - 1)) || p1 < s->guest_begin ||
       p1 >= s->guest_end || bytes > s->guest_end - p1) return NULL;
    return s->ops.map(s->ops.context, p1, bytes, writing);
}
static void reset_state(struct kui_gd_service *s) {
    s->sector_part = 0x2000; s->track_type = 1024; s->sector_bytes = 2048;
    s->command = 0; s->pending = 0; s->status = KUI_GD_NOT_FOUND;
    s->completed_bytes = 0; s->error = KUI_GD_ERROR_NONE;
}
int kui_gd_service_init(struct kui_gd_service *s,
    const struct kui_gd_track *tracks, uint32_t count,
    const struct kui_gd_ops *ops, uint32_t guest_begin, uint32_t guest_end) {
    if(!s || !tracks || !ops || !ops->map || !ops->check || !ops->read ||
       !count || count > KUI_GD_TRACK_MAX || guest_begin < 0x8c010000u ||
       guest_end > 0x8d000000u || guest_begin >= guest_end ||
       (guest_begin & 3u) || (guest_end & 3u)) return -1;
    for(uint32_t i = 0; i < count; ++i) {
        const struct kui_gd_track *t = &tracks[i];
        if(t->number != i + 1 || (t->control != 0 && t->control != 4) ||
           t->start_lba >= t->end_lba || t->end_lba > 719850u ||
           (t->start_lba < 45000u && t->end_lba > 45000u) ||
           (i && tracks[i - 1].end_lba > t->start_lba)) return -1;
    }
    memset(s, 0, sizeof(*s));
    s->ops = *ops; s->track_count = count;
    memcpy(s->tracks, tracks, count * sizeof(*tracks));
    s->guest_begin = guest_begin; s->guest_end = guest_end;
    s->initialized = 1; reset_state(s);
    return 0;
}
static int area_bounds(const struct kui_gd_service *s, uint32_t area,
                       uint32_t *first, uint32_t *last) {
    if(area > 1) return -1;
    *first = *last = KUI_GD_TRACK_MAX;
    for(uint32_t i = 0; i < s->track_count; ++i) {
        if((s->tracks[i].start_lba >= 45000u) != (area != 0)) continue;
        if(*first == KUI_GD_TRACK_MAX) *first = i;
        *last = i;
    }
    return *first == KUI_GD_TRACK_MAX ? -1 : 0;
}
static int32_t request(struct kui_gd_service *s, uint32_t command,
                       uint32_t parameters) {
    if(s->pending) return 0;
    uint32_t lba = 0, count = 0, destination = 0, bytes = 0, area = 0;
    if(command == KUI_GD_PIOREAD || command == KUI_GD_DMAREAD) {
        const uint8_t *p = guest(s, parameters, 16, 4, 0);
        if(!p) return 0;
        uint32_t fad = read32(p);
        count = read32(p + 4); destination = read32(p + 8);
        if(fad < KUI_GD_FAD_OFFSET || fad >= 720000u ||
           !count || count > KUI_GD_MAX_READ_SECTORS || read32(p + 12)) return 0;
        lba = fad - KUI_GD_FAD_OFFSET; bytes = count * s->sector_bytes;
        if(count > 719850u - lba ||
           !guest(s, destination, bytes, command == KUI_GD_DMAREAD ? 32 : 2, 1) ||
           s->ops.check(s->ops.context, lba, count, s->sector_bytes)) return 0;
    } else if(command == KUI_GD_GETTOC2) {
        const uint8_t *p = guest(s, parameters, 8, 4, 0);
        if(!p) return 0;
        area = read32(p); destination = read32(p + 4); bytes = KUI_GD_TOC_BYTES;
        uint32_t first, last;
        if(area_bounds(s, area, &first, &last) ||
           !guest(s, destination, bytes, 4, 1)) return 0;
    } else if(command != KUI_GD_COMMAND_INIT && command != KUI_GD_NOP &&
              command != KUI_GD_STOP) {
        return 0;
    }
    s->token = s->token >= 0x7fffffffu ? 1 : s->token + 1;
    s->command = command; s->lba = lba; s->count = count;
    s->destination = destination; s->area = area; s->request_bytes = bytes;
    s->completed_bytes = 0; s->error = KUI_GD_ERROR_NONE;
    s->status = KUI_GD_PROCESSING; s->pending = 1;
    return (int32_t)s->token;
}
static void fill_toc(struct kui_gd_service *s, uint8_t *out) {
    uint32_t first, last;
    if(area_bounds(s, s->area, &first, &last)) return; /* submission checked */
    memset(out, 0xff, KUI_GD_TOC_BYTES);
    for(uint32_t i = first; i <= last; ++i) {
        const struct kui_gd_track *t = &s->tracks[i];
        write32(out + (t->number - 1) * 4,
            t->control << 28 | 1u << 24 | (t->start_lba + KUI_GD_FAD_OFFSET));
    }
    const struct kui_gd_track *f = &s->tracks[first], *l = &s->tracks[last];
    write32(out + 99 * 4, f->control << 28 | 1u << 24 | f->number << 16);
    write32(out + 100 * 4, l->control << 28 | 1u << 24 | l->number << 16);
    write32(out + 101 * 4, l->control << 28 | 1u << 24 |
        (l->end_lba + KUI_GD_FAD_OFFSET));
}
static int32_t execute(struct kui_gd_service *s) {
    if(!s->pending) return 0;
    s->executing = 1;
    uint8_t *out = NULL;
    if(s->request_bytes) {
        out = guest(s, s->destination, s->request_bytes,
            s->command == KUI_GD_DMAREAD ? 32 :
            s->command == KUI_GD_GETTOC2 ? 4 : 2, 1);
        if(!out) s->error = KUI_GD_ERROR_MEMORY;
    }
    if(!s->error) {
        if(s->command == KUI_GD_PIOREAD || s->command == KUI_GD_DMAREAD) {
            if(s->ops.read(s->ops.context, s->lba, s->count, s->sector_bytes, out))
                s->error = KUI_GD_ERROR_IO;
        } else if(s->command == KUI_GD_GETTOC2) fill_toc(s, out);
        else if(s->command == KUI_GD_COMMAND_INIT) {
            s->sector_part = 0x2000; s->track_type = 1024; s->sector_bytes = 2048;
        }
    }
    s->completed_bytes = s->error ? 0 : s->request_bytes;
    s->status = s->error ? KUI_GD_FAILED : KUI_GD_COMPLETED;
    s->pending = 0; s->executing = 0;
    return 0;
}
static int32_t check(struct kui_gd_service *s, uint32_t token, uint32_t address) {
    uint8_t *out = guest(s, address, 16, 4, 1);
    if(!out) return KUI_GD_FAILED;
    if(!token || token != s->token || !s->command) {
        memset(out, 0, 16); return KUI_GD_NOT_FOUND;
    }
    write32(out, s->error ? 1u : 0u);
    write32(out + 4, s->error); write32(out + 8, s->completed_bytes);
    /* Busy until execution; internal/no outstanding ATA after completion.
     * No synthetic DMA hardware IRQ is asserted by this service. */
    write32(out + 12, s->pending ? 4u : 0u);
    return s->status;
}
static int32_t datatype(struct kui_gd_service *s, uint32_t address) {
    if(s->pending) return -1;
    uint8_t *p = guest(s, address, 16, 4, 0);
    if(!p) return -1;
    uint32_t rw = read32(p), part = read32(p + 4), type = read32(p + 8),
             bytes = read32(p + 12);
    if(rw == 1) {
        p = guest(s, address, 16, 4, 1);
        if(!p) return -1;
        write32(p + 4, s->sector_part); write32(p + 8, s->track_type);
        write32(p + 12, s->sector_bytes); return 0;
    }
    if(rw != 0 || !((part == 0x2000 && type == 1024 && bytes == 2048) ||
                   (part == 0x1000 && type == 0 && bytes == 2352))) return -1;
    s->sector_part = part; s->track_type = type; s->sector_bytes = bytes;
    return 0;
}
int32_t kui_gd_service_dispatch(struct kui_gd_service *s, uint32_t r4,
    uint32_t r5, uint32_t r6, uint32_t r7) {
    if(!s || !s->initialized || r6 != 0) return -1;
    if(s->executing) return r7 == KUI_GD_REQUEST ? 0 : r7 == KUI_GD_CHECK ? 4 : -1;
    switch(r7) {
    case KUI_GD_REQUEST: return request(s, r4, r5);
    case KUI_GD_CHECK: return check(s, r4, r5);
    case KUI_GD_EXEC: return execute(s);
    case KUI_GD_INIT: case KUI_GD_RESET: reset_state(s); return 0;
    case KUI_GD_DRIVE: {
        uint8_t *out = guest(s, r4, 8, 4, 1);
        if(!out) return -1;
        write32(out, s->pending ? 0u : 1u); write32(out + 4, 0x80); return 0;
    }
    case KUI_GD_ABORT:
        if(!r4 || !s->pending || r4 != s->token) return -1;
        s->pending = 0; s->status = KUI_GD_FAILED;
        s->error = KUI_GD_ERROR_CANCELLED; s->completed_bytes = 0; return 0;
    case KUI_GD_DATATYPE: return datatype(s, r4);
    default: return -1;
    }
}
