/* SPDX-License-Identifier: GPL-3.0-only
 * Original K-UI implementation. Interface references (not loader code):
 * KallistiOS fcfa7d869471591ca1c777543261a7bfea7cb726,
 * kernel/arch/dreamcast/{include/dc/syscalls.h,hardware/syscalls.c}.
 * REQ_MODE/SET_MODE/REQ_STAT word layouts and consumed CHECK result are also
 * independently documented by inolen/redream ffb7302245ff40515cb9f0f0b0e233a4b39342d3,
 * src/guest/bios/syscalls.c (GPL-3.0; interface reference only, no emulator
 * implementation copied). That reference also documents type0 automatic
 * sector selection. GET_VERS's 28-byte response and trailing state byte are
 * cross-checked against that reference and DreamShell ISO Loader syscalls.c
 * get_ver_str (GPL-3.0, Copyright 2009-2023 SWAT). This is a virtual driver
 * compatibility response, not a version query to the physical optical drive.
 * Our backend deliberately supports only Mode1 user data.
 */
#include "kui/retail_gd.h"
#include <stddef.h>
#include <string.h>

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t n) {
    for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(n >> (i * 8));
}
static uint8_t *guest(struct kui_retail_gd *s, uint32_t address,
                      uint32_t bytes, uint32_t align, int writing) {
    uint32_t area = address & 0xff000000u;
    if(area != 0x0c000000u && area != 0x8c000000u && area != 0xac000000u)
        return NULL;
    uint32_t p1 = (address & 0x00ffffffu) | 0x8c000000u;
    if(!bytes || (p1 & (align - 1u)) || p1 < s->guest_begin ||
       p1 >= s->guest_end || bytes > s->guest_end - p1) return NULL;
    return s->ops.map(s->ops.context, p1, bytes, writing);
}
static void reset(struct kui_retail_gd *s) {
    s->sector_part = 0x2000; s->track_type = 0; s->sector_bytes = 2048;
    s->pending = 0; s->command = 0; s->status = KUI_GD_NOT_FOUND;
    s->completed_bytes = 0; s->error = 0; s->drive_status = 1;
}
int kui_retail_gd_init(struct kui_retail_gd *s,
    const struct kui_gd_track *tracks, uint32_t count,
    const struct kui_gd_ops *ops, uint32_t begin, uint32_t end) {
    if(!s || !tracks || !ops || !ops->map || !ops->read || !ops->check ||
       !count || count > KUI_GD_TRACK_MAX || begin < 0x8c008000u ||
       end > 0x8d000000u || begin >= end || ((begin | end) & 3u)) return -1;
    for(uint32_t i = 0; i < count; ++i) {
        const struct kui_gd_track *t = tracks + i;
        if(t->number != i + 1 || (t->control != 0 && t->control != 4) ||
           t->start_lba >= t->end_lba || t->end_lba > 719850u ||
           (t->start_lba < 45000u && t->end_lba > 45000u) ||
           (i && tracks[i - 1].end_lba > t->start_lba)) return -1;
    }
    memset(s, 0, sizeof(*s));
    s->ops = *ops; s->tracks = tracks; s->track_count = count;
    s->guest_begin = begin; s->guest_end = end; s->initialized = 1;
    s->position_lba = tracks[count > 2 ? 2 : 0].start_lba;
    s->step = KUI_RETAIL_GD_STEP_SECTORS;
    reset(s);
    return 0;
}
static int area_bounds(const struct kui_retail_gd *s, uint32_t area,
                       uint32_t *first, uint32_t *last) {
    *first = *last = s->track_count;
    if(area > 1) return -1;
    for(uint32_t i = 0; i < s->track_count; ++i) {
        if((s->tracks[i].start_lba >= 45000u) != (area != 0)) continue;
        if(*first == s->track_count) *first = i;
        *last = i;
    }
    return *first == s->track_count ? -1 : 0;
}
static uint32_t step_count(const struct kui_retail_gd *s, uint32_t remaining) {
    uint32_t step = s->step - 1u < KUI_RETAIL_GD_STEP_MAX ?
        s->step : KUI_RETAIL_GD_STEP_SECTORS;
    return remaining > step ? step : remaining;
}
static int32_t request(struct kui_retail_gd *s, uint32_t cmd, uint32_t address) {
    if(s->pending) return 0;
    uint32_t nparams = 0, p[4] = {0}, bytes = 0, destination = 0, lba = 0;
    switch(cmd) {
    case KUI_GD_PIOREAD: case KUI_GD_DMAREAD:
    case KUI_RETAIL_GD_SET_MODE: case KUI_RETAIL_GD_REQ_STAT: nparams = 4; break;
    case KUI_RETAIL_GD_GETTOC: case KUI_GD_GETTOC2: nparams = 2; break;
    case KUI_RETAIL_GD_REQ_MODE: case KUI_RETAIL_GD_SEEK: nparams = 1; break;
    case KUI_RETAIL_GD_GET_VERS: nparams = 1; break;
    case KUI_RETAIL_GD_GETSCD: nparams = 3; break;
    /* CD audio: accepted and completed without sound (no CDDA emulation). */
    case KUI_RETAIL_GD_PLAY: case KUI_RETAIL_GD_PLAY2: nparams = 3; break;
    case KUI_GD_COMMAND_INIT: case KUI_GD_NOP: case KUI_GD_STOP:
    case KUI_RETAIL_GD_PAUSE: case KUI_RETAIL_GD_RELEASE: break;
    default: return 0;
    }
    if(nparams) {
        const uint8_t *params = guest(s, address, nparams * 4u, 4, 0);
        if(!params) return 0;
        for(uint32_t i = 0; i < nparams; ++i) p[i] = get32(params + i * 4u);
    }
    if(cmd == KUI_GD_PIOREAD || cmd == KUI_GD_DMAREAD) {
        s->diag.last_lba = p[0] >= 150 ? p[0] - 150 : UINT32_MAX;
        s->diag.last_count = p[1]; s->diag.last_destination = p[2];
        if(p[0] < 150 || p[0] >= 720000u || !p[1] || p[3]) return 0;
        lba = p[0] - 150;
        if(p[1] > 719850u - lba || p[1] > UINT32_MAX / s->sector_bytes) return 0;
        bytes = p[1] * s->sector_bytes; destination = p[2];
        if(!guest(s, destination, bytes, cmd == KUI_GD_DMAREAD ? 32 : 2,
                  KUI_RETAIL_MAP_VALIDATE)) return 0;
        for(uint32_t done = 0; done < p[1];) {
            uint32_t n = p[1] - done;
            if(n > KUI_RETAIL_GD_CHECK_SECTORS) n = KUI_RETAIL_GD_CHECK_SECTORS;
            if(s->ops.check(s->ops.context, lba + done, n, s->sector_bytes)) return 0;
            done += n;
        }
    } else if(cmd == KUI_RETAIL_GD_GETSCD) {
        s->diag.last_lba = p[0]; /* Format, not a read LBA. */
        s->diag.last_count = p[1]; s->diag.last_destination = p[2];
        if(p[0] > 2 || !p[1]) return 0;
        bytes = p[0] == 0 ? 100 : p[0] == 1 ? 14 : 24;
        if(bytes > p[1]) bytes = p[1];
        destination = p[2];
    } else if(cmd == KUI_RETAIL_GD_GETTOC || cmd == KUI_GD_GETTOC2) {
        uint32_t first, last;
        if(area_bounds(s, p[0], &first, &last)) return 0;
        bytes = KUI_GD_TOC_BYTES; destination = p[1];
    } else if(cmd == KUI_RETAIL_GD_REQ_MODE || cmd == KUI_RETAIL_GD_GET_VERS) {
        bytes = cmd == KUI_RETAIL_GD_GET_VERS ? 28 : 16; destination = p[0];
    } else if(cmd == KUI_RETAIL_GD_REQ_STAT) {
        for(unsigned i = 0; i < 4; ++i)
            if(!guest(s, p[i], 4, 4, 1)) return 0;
    } else if(cmd == KUI_RETAIL_GD_SEEK) {
        if(p[0] < 150 || p[0] >= 720000u) return 0;
        lba = p[0] - 150;
        uint32_t i;
        for(i = 0; i < s->track_count; ++i)
            if(lba >= s->tracks[i].start_lba && lba < s->tracks[i].end_lba) break;
        if(i == s->track_count) return 0;
    }
    if(bytes && cmd != KUI_GD_PIOREAD && cmd != KUI_GD_DMAREAD &&
       !guest(s, destination, bytes,
              cmd == KUI_RETAIL_GD_GET_VERS || cmd == KUI_RETAIL_GD_GETSCD ? 1 : 4,
              cmd == KUI_RETAIL_GD_GETSCD ? KUI_RETAIL_MAP_VALIDATE : 1)) return 0;
    s->token = s->token >= 0x7fffffffu ? 1 : s->token + 1;
    s->command = cmd; s->lba = lba; s->count = p[1];
    s->destination = destination; s->area = p[0]; s->request_bytes = bytes;
    memcpy(s->outputs, p, sizeof(p));
    s->completed_bytes = 0; s->error = 0;
    s->status = KUI_GD_PROCESSING; s->pending = 1;
    ++s->diag.requests;
    return (int32_t)s->token;
}
static void toc(struct kui_retail_gd *s, uint8_t *out) {
    uint32_t first, last;
    if(area_bounds(s, s->area, &first, &last)) return;
    memset(out, 0xff, KUI_GD_TOC_BYTES);
    for(uint32_t i = first; i <= last; ++i) {
        const struct kui_gd_track *t = s->tracks + i;
        put32(out + (t->number - 1u) * 4u,
              t->control << 28 | 0x01000000u | (t->start_lba + 150u));
    }
    const struct kui_gd_track *f = s->tracks + first, *l = s->tracks + last;
    put32(out + 396, f->control << 28 | 0x01000000u | f->number << 16);
    put32(out + 400, l->control << 28 | 0x01000000u | l->number << 16);
    put32(out + 404, l->control << 28 | 0x01000000u | (l->end_lba + 150u));
}
static uint8_t bcd(uint32_t n) { return (uint8_t)((n / 10u) * 16u + n % 10u); }
static void msf(uint8_t *out, uint32_t frames) {
    out[2] = bcd(frames % 75u); frames /= 75u;
    out[1] = bcd(frames % 60u); out[0] = bcd(frames / 60u);
}
static void subcode(const struct kui_retail_gd *s, uint8_t *out) {
    /* GETSCD wire layout cross-checked against KOS syscalls.h and Flycast
     * gd_get_subcode/GDCC_HLE_GETSCD; independent implementation. GDI stores
     * no subchannels: synthesize only ordinary index-1 Q and unavailable MCN.
     * Raw Q uses BCD MSF + complemented CCITT CRC; formatted Q uses binary
     * track/index and 24-bit sector counts. Never read SD or claim CDDA play. */
    uint8_t data[100] = {0};
    data[1] = 0x15; /* Audio status unavailable. */
    data[3] = s->area == 0 ? 100 : s->area == 1 ? 14 : 24;
    if(s->area == 2) {
        data[4] = 2;
        memset(data + 9, '0', 13); /* Catalog validity flag remains clear. */
    } else {
        uint32_t i = 0;
        while(i + 1 < s->track_count && s->tracks[i + 1].start_lba <= s->position_lba) ++i;
        const struct kui_gd_track *t = s->tracks + i;
        uint32_t elapsed = s->position_lba - t->start_lba;
        uint32_t fad = s->position_lba + 150u;
        data[4] = (uint8_t)(t->control << 4 | 1u);
        data[5] = (uint8_t)t->number; data[6] = 1;
        if(s->area == 1) {
            for(unsigned n = 0; n < 3; ++n) {
                data[9 - n] = (uint8_t)(elapsed >> (n * 8));
                data[13 - n] = (uint8_t)(fad >> (n * 8));
            }
        } else {
            data[5] = bcd(t->number);
            msf(data + 7, elapsed); msf(data + 11, fad);
            uint16_t crc = 0;
            for(unsigned n = 4; n < 14; ++n) {
                crc ^= (uint16_t)data[n] << 8;
                for(unsigned bit = 0; bit < 8; ++bit)
                    crc = (uint16_t)((crc << 1) ^ (crc & 0x8000u ? 0x1021u : 0));
            }
            crc = (uint16_t)~crc;
            data[14] = (uint8_t)(crc >> 8); data[15] = (uint8_t)crc;
            /* Expand backwards so the packed Q bytes can share this buffer. */
            for(unsigned bit = 96; bit-- > 0;)
                data[4 + bit] = (uint8_t)(((data[4 + bit / 8] >> (7 - bit % 8)) & 1u) << 6);
        }
    }
    memcpy(out, data, s->request_bytes);
}
static int32_t execute(struct kui_retail_gd *s) {
    ++s->diag.exec_calls;
    if(!s->pending) return 0;
    s->executing = 1;
    if(s->command == KUI_GD_PIOREAD || s->command == KUI_GD_DMAREAD) {
        uint32_t done = s->completed_bytes / s->sector_bytes;
        uint32_t n = step_count(s, s->count - done), bytes = n * s->sector_bytes;
        uint8_t *out = guest(s, s->destination + s->completed_bytes, bytes, 2, 1);
        if(!out) s->error = KUI_GD_ERROR_MEMORY;
        else {
            ++s->diag.read_steps;
            if(s->ops.read(s->ops.context, s->lba + done, n, s->sector_bytes, out))
                s->error = KUI_GD_ERROR_IO;
            else {
                s->completed_bytes += bytes; s->diag.sectors_read += n;
                s->position_lba = s->lba + done + n - 1u; s->drive_status = 1;
            }
        }
        if(!s->error && s->completed_bytes < s->request_bytes) {
            s->executing = 0; return 0;
        }
    } else if(s->command == KUI_RETAIL_GD_REQ_STAT) {
        uint8_t *out[4];
        for(unsigned i = 0; i < 4; ++i) {
            out[i] = guest(s, s->outputs[i], 4, 4, 1);
            if(!out[i]) s->error = KUI_GD_ERROR_MEMORY;
        }
        if(!s->error) {
            uint32_t i = 0;
            while(i + 1 < s->track_count && s->tracks[i + 1].start_lba <= s->position_lba) ++i;
            put32(out[0], s->drive_status); put32(out[1], s->tracks[i].number);
            put32(out[2], 0x10000000u | s->tracks[i].control << 24 |
                  (s->position_lba + 150u));
            put32(out[3], 1); s->completed_bytes = 16;
        }
    } else if(s->request_bytes) {
        uint8_t *out = guest(s, s->destination, s->request_bytes,
                            s->command == KUI_RETAIL_GD_GET_VERS ||
                            s->command == KUI_RETAIL_GD_GETSCD ? 1 : 4, 1);
        if(!out) s->error = KUI_GD_ERROR_MEMORY;
        else {
            if(s->command == KUI_RETAIL_GD_GETSCD) subcode(s, out);
            else if(s->command == KUI_RETAIL_GD_REQ_MODE)
                for(unsigned i = 0; i < 4; ++i) put32(out + i * 4u, s->mode[i]);
            else if(s->command == KUI_RETAIL_GD_GET_VERS)
                memcpy(out, "GDC Version 1.10 1999-03-31\002", 28);
            else toc(s, out);
            s->completed_bytes = s->command == KUI_RETAIL_GD_GET_VERS ? 0 : s->request_bytes;
        }
    } else if(s->command == KUI_RETAIL_GD_SET_MODE) memcpy(s->mode, s->outputs, sizeof(s->mode));
    else if(s->command == KUI_RETAIL_GD_SEEK) { s->position_lba = s->lba; s->drive_status = 1; }
    else if(s->command == KUI_GD_STOP) s->drive_status = 2;
    else if(s->command == KUI_GD_COMMAND_INIT) {
        s->sector_part = 0x2000; s->track_type = 0; s->sector_bytes = 2048;
        s->drive_status = 1;
    }
    s->status = s->error ? KUI_GD_FAILED : KUI_GD_COMPLETED;
    s->pending = 0; s->executing = 0;
    s->diag.last_error = s->error;
    return 0;
}
static int32_t check(struct kui_retail_gd *s, uint32_t token, uint32_t address) {
    uint8_t *out = guest(s, address, 16, 4, 1);
    if(!out) return KUI_GD_FAILED;
    if(!token || token != s->token || !s->command) {
        memset(out, 0, 16); return KUI_GD_NOT_FOUND;
    }
    put32(out, s->error ? 1u : 0u); put32(out + 4, s->error);
    put32(out + 8, s->completed_bytes); put32(out + 12, s->pending ? 4u : 0u);
    int32_t result = s->status;
    if(!s->pending) s->command = 0;
    return result;
}
static int32_t datatype(struct kui_retail_gd *s, uint32_t address) {
    if(s->pending) return -1;
    uint8_t *p = guest(s, address, 16, 4, 0);
    if(!p) return -1;
    uint32_t rw = get32(p), part = get32(p + 4), type = get32(p + 8), bytes = get32(p + 12);
    if(rw == 1) {
        p = guest(s, address, 16, 4, 1);
        if(!p) return -1;
        put32(p + 4, s->sector_part); put32(p + 8, s->track_type);
        put32(p + 12, s->sector_bytes); return 0;
    }
    if(rw || !((part == 0x2000 && (type == 0 || type == 1024) && bytes == 2048) ||
               (part == 0x1000 && type == 0 && bytes == 2352))) return -1;
    s->sector_part = part; s->track_type = type; s->sector_bytes = bytes;
    return 0;
}
int32_t kui_retail_gd_dispatch(struct kui_retail_gd *s, uint32_t r4,
                              uint32_t r5, uint32_t r6, uint32_t r7) {
    if(!s || !s->initialized) return -1;
    ++s->diag.calls; s->diag.last_function = r7;
    if(r7 == KUI_GD_REQUEST) s->diag.last_command = r4;
    int32_t result = -1;
    if(r6) goto done;
    if(s->executing) {
        result = r7 == KUI_GD_REQUEST ? 0 : r7 == KUI_GD_CHECK ? 4 : -1;
        goto done;
    }
    switch(r7) {
    case KUI_GD_REQUEST: result = request(s, r4, r5); break;
    case KUI_GD_EXEC: result = execute(s); break;
    case KUI_GD_CHECK: result = check(s, r4, r5); break;
    case KUI_GD_INIT: case KUI_GD_RESET: reset(s); result = 0; break;
    case KUI_GD_DATATYPE: result = datatype(s, r4); break;
    case KUI_GD_DRIVE: {
        uint8_t *out = guest(s, r4, 8, 4, 1);
        if(out) { put32(out, s->pending ? 0 : s->drive_status); put32(out + 4, 0x80); result = 0; }
        break;
    }
    case KUI_GD_ABORT:
        if(r4 && s->pending && r4 == s->token) {
            s->pending = 0; s->error = KUI_GD_ERROR_CANCELLED;
            s->diag.last_error = s->error; s->status = KUI_GD_FAILED; result = 0;
        }
        break;
    case KUI_GD_DMA_CALLBACK: case KUI_GD_PIO_CALLBACK:
        if(!r4) result = 0;
        break;
    default: break;
    }
done:
    s->diag.last_result = result;
    if(result < 0 || (r7 == KUI_GD_REQUEST && result == 0)) ++s->diag.rejected;
    return result;
}
