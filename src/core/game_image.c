/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_image.h"
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

struct line { const unsigned char *at, *end; };

static void space(struct line *line) {
    while(line->at < line->end && (*line->at == ' ' || *line->at == '\t'))
        ++line->at;
}

static bool number(struct line *line, uint32_t *out) {
    space(line);
    if(line->at == line->end || *line->at < '0' || *line->at > '9')
        return false;
    uint32_t value = 0;
    do {
        unsigned digit = *line->at++ - '0';
        if(value > (UINT32_MAX - digit) / 10u) return false;
        value = value * 10u + digit;
    } while(line->at < line->end && *line->at >= '0' && *line->at <= '9');
    if(line->at < line->end && *line->at != ' ' && *line->at != '\t')
        return false;
    *out = value;
    return true;
}

static bool filename(struct line *line, char out[KUI_GAME_NAME_CAP]) {
    space(line);
    if(line->at == line->end) return false;
    bool quoted = *line->at == '"';
    if(quoted) ++line->at;
    size_t length = 0;
    while(line->at < line->end) {
        unsigned ch = *line->at;
        if((quoted && ch == '"') || (!quoted && (ch == ' ' || ch == '\t')))
            break;
        if(ch < 32 || ch > 126 || strchr("\\/:*?\"<>|", (int)ch) ||
           length + 1u >= KUI_GAME_NAME_CAP) return false;
        out[length++] = (char)ch;
        ++line->at;
    }
    if(quoted && (line->at == line->end || *line->at++ != '"')) return false;
    if(line->at < line->end && *line->at != ' ' && *line->at != '\t')
        return false;
    if(!length || out[0] == '.' || out[0] == ' ' ||
       out[length - 1u] == '.' || out[length - 1u] == ' ') return false;
    out[length] = 0;
    return true;
}

static unsigned lower(unsigned ch) {
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

static bool same_name(const char *a, const char *b) {
    while(*a && *b) {
        if(lower((unsigned char)*a++) != lower((unsigned char)*b++))
            return false;
    }
    return *a == *b;
}

/* Reject the complete descriptor before asking the backend about any file. */
static enum kui_game_result parse(const void *gdi, size_t size,
                                   struct kui_game_image *image) {
    const unsigned char *at = gdi, *end = at + size;
    if(size >= 3u && at[0] == 0xefu && at[1] == 0xbbu && at[2] == 0xbfu) at += 3u;
    unsigned row = 0;
    while(at < end) {
        const unsigned char *next = at;
        while(next < end && *next != '\n') ++next;
        struct line line = {at, next};
        if(line.end > line.at && line.end[-1] == '\r') --line.end;
        if(!row) {
            uint32_t count;
            if(!number(&line, &count) || !count || count > KUI_GAME_TRACK_MAX)
                return KUI_GAME_SYNTAX;
            image->count = count;
        } else {
            if(row > image->count) return KUI_GAME_SYNTAX;
            uint32_t index, lba, control, bytes, offset;
            struct kui_game_image_track *track = &image->tracks[row - 1u];
            if(!number(&line, &index) || index != row || !number(&line, &lba) ||
               !number(&line, &control) || !number(&line, &bytes) ||
               !filename(&line, track->name) || !number(&line, &offset))
                return KUI_GAME_SYNTAX;
            if((control != 0 && control != 4) ||
               (bytes != KUI_GAME_RAW_BYTES &&
                (bytes != KUI_GAME_DATA_BYTES || control != 4)))
                return KUI_GAME_UNSUPPORTED;
            if(lba >= KUI_GAME_LBA_LIMIT) return KUI_GAME_RANGE;
            if(row > 1u && lba <= image->tracks[row - 2u].start_lba)
                return KUI_GAME_OVERLAP;
            for(unsigned i = 0; i + 1u < row; ++i)
                if(same_name(image->tracks[i].name, track->name) &&
                   offset <= image->tracks[i].file_offset)
                    return KUI_GAME_OVERLAP;
            track->number = index;
            track->start_lba = lba;
            track->control = control;
            track->sector_bytes = bytes;
            track->file_offset = offset;
            track->sector_mode = control ? 1u : 0u;
            track->data_offset = control && bytes == KUI_GAME_RAW_BYTES ? 16u : 0u;
        }
        space(&line);
        if(line.at != line.end) return KUI_GAME_SYNTAX;
        ++row;
        at = next == end ? end : next + 1;
    }
    return row == image->count + 1u ? KUI_GAME_OK : KUI_GAME_SYNTAX;
}

enum kui_game_result kui_game_image_open(const void *gdi, size_t size,
    const struct kui_game_file_ops *files, struct kui_game_image *out) {
    if(!gdi || !size || size > KUI_GAME_GDI_LIMIT || !files || !files->stat ||
       !files->read || !out) return KUI_GAME_INVALID;
    struct kui_game_image image = {0};
    enum kui_game_result result = parse(gdi, size, &image);
    if(result != KUI_GAME_OK) return result;
    image.files = *files;
    for(unsigned i = 0; i < image.count; ++i) {
        struct kui_game_image_track *track = &image.tracks[i];
        uint64_t bytes = 0;
        result = files->stat(files->ctx, track->name, &bytes);
        if(result != KUI_GAME_OK) return result;
        if(!bytes || track->file_offset >= bytes) return KUI_GAME_FILE_SIZE;
        uint64_t available = bytes - track->file_offset;
        for(unsigned j = i + 1u; j < image.count; ++j) {
            if(same_name(track->name, image.tracks[j].name)) {
                if(image.tracks[j].file_offset > bytes) return KUI_GAME_FILE_SIZE;
                available = image.tracks[j].file_offset - track->file_offset;
                break;
            }
        }
        if(!available || available % track->sector_bytes) return KUI_GAME_FILE_SIZE;
        uint64_t sectors = available / track->sector_bytes;
        if(sectors > KUI_GAME_LBA_LIMIT - track->start_lba)
            return KUI_GAME_RANGE;
        track->file_bytes = bytes;
        track->end_lba = track->start_lba + (uint32_t)sectors;
        if(i + 1u < image.count && track->end_lba > image.tracks[i + 1u].start_lba)
            return KUI_GAME_OVERLAP;
    }
    image.format = KUI_GAME_IMAGE_GDI;
    for(unsigned i = 0; i < image.count; ++i)
        if(image.tracks[i].control == 4 &&
           (image.tracks[i].start_lba >= 45000u || !image.data_lba)) {
            image.data_lba = image.tracks[i].start_lba;
            if(image.data_lba >= 45000u) break;
        }
    *out = image;
    return KUI_GAME_OK;
}

/* Public structs make metadata inspection cheap; guard structural invariants
 * here too, so a damaged/uninitialized descriptor cannot cause out-of-bounds
 * accesses in this module. Callers must retain an unchanged opened image. */
static bool valid_image(const struct kui_game_image *image) {
    if(!image || !image->count || image->count > KUI_GAME_TRACK_MAX ||
       !image->files.stat || !image->files.read) return false;
    for(unsigned i = 0; i < image->count; ++i) {
        const struct kui_game_image_track *track = &image->tracks[i];
        if(track->number != i + 1u || track->start_lba >= track->end_lba ||
           track->end_lba > KUI_GAME_LBA_LIMIT ||
           (track->control != 0 && track->control != 4) ||
           ((track->control == 0 &&
             (track->sector_mode != 0 || track->data_offset != 0 ||
              (track->sector_bytes != 2352u && track->sector_bytes != 2448u))) ||
            (track->control == 4 &&
             ((track->sector_mode != 1 && track->sector_mode != 2) ||
              (track->sector_bytes == 2048u ? track->data_offset != 0 :
               track->sector_bytes == 2336u ?
                 (track->sector_mode != 2 || track->data_offset != 8u) :
               (track->sector_bytes != 2352u && track->sector_bytes != 2448u) ||
                 track->data_offset != (track->sector_mode == 1 ? 16u : 24u))))) ||
           track->file_offset > track->file_bytes ||
           (uint64_t)(track->end_lba - track->start_lba) * track->sector_bytes >
                 track->file_bytes - track->file_offset ||
           !track->name[0] || !memchr(track->name, 0, sizeof(track->name)) ||
           (i && image->tracks[i - 1u].end_lba > track->start_lba))
            return false;
    }
    return true;
}

enum kui_game_result kui_game_image_check(const struct kui_game_image *image,
    uint32_t lba, uint32_t count, enum kui_game_sector_format format) {
    if(!valid_image(image) || !count ||
       (format != KUI_GAME_SECTOR_RAW && format != KUI_GAME_SECTOR_MODE1))
        return KUI_GAME_INVALID;
    uint64_t end = (uint64_t)lba + count;
    if(lba < image->tracks[0].start_lba ||
       end > image->tracks[image->count - 1u].end_lba)
        return KUI_GAME_RANGE;
    uint32_t cursor = lba;
    for(unsigned i = 0; i < image->count && cursor < end; ++i) {
        const struct kui_game_image_track *track = &image->tracks[i];
        if(track->end_lba <= cursor) continue;
        if(track->start_lba > cursor) return KUI_GAME_GAP;
        if(format == KUI_GAME_SECTOR_MODE1 && track->control != 4)
            return KUI_GAME_AUDIO;
        if(format == KUI_GAME_SECTOR_RAW && track->sector_bytes != KUI_GAME_RAW_BYTES &&
           track->sector_bytes != KUI_GAME_SUBCHANNEL_BYTES)
            return KUI_GAME_UNSUPPORTED;
        cursor = track->end_lba < end ? track->end_lba : (uint32_t)end;
    }
    return cursor == end ? KUI_GAME_OK : KUI_GAME_RANGE;
}

static bool sector_data(const unsigned char *raw,
                        const struct kui_game_image_track *track) {
    if(track->sector_bytes == 2048u) return true;
    if(track->sector_bytes != 2336u) {
        if(raw[0] || raw[11] || raw[15] != track->sector_mode) return false;
        for(unsigned i = 1; i < 11; ++i) if(raw[i] != 255) return false;
    }
    if(track->sector_mode == 2) {
        unsigned sub = track->sector_bytes == 2336u ? 0u : 16u;
        /* Duplicated XA subheaders and Form 1; never truncate Form 2 data. */
        if(memcmp(raw + sub, raw + sub + 4u, 4u) || (raw[sub + 2u] & 0x20u))
            return false;
    }
    return true;
}

enum kui_game_result kui_game_image_read(const struct kui_game_image *image,
    uint32_t lba, uint32_t count, enum kui_game_sector_format format,
    void *out, size_t size) {
    enum kui_game_result result = kui_game_image_check(image, lba, count, format);
    if(result != KUI_GAME_OK) return result;
    unsigned bytes = format == KUI_GAME_SECTOR_RAW ? KUI_GAME_RAW_BYTES : KUI_GAME_DATA_BYTES;
    uint64_t needed = (uint64_t)count * bytes;
    if(!out || needed > size) return KUI_GAME_INVALID;
    unsigned char *destination = out;
    uint32_t cursor = lba, remaining = count;
    unsigned char raw[KUI_GAME_SUBCHANNEL_BYTES];
    for(unsigned i = 0; i < image->count && remaining; ++i) {
        const struct kui_game_image_track *track = &image->tracks[i];
        if(track->end_lba <= cursor) continue;
        uint32_t take = track->end_lba - cursor;
        if(take > remaining) take = remaining;
        uint64_t offset = track->file_offset +
            (uint64_t)(cursor - track->start_lba) * track->sector_bytes;
        if((format == KUI_GAME_SECTOR_RAW && track->sector_bytes == KUI_GAME_RAW_BYTES) ||
           track->sector_bytes == KUI_GAME_DATA_BYTES) {
            size_t length = (size_t)take * bytes;
            result = image->files.read(image->files.ctx, track->name, offset,
                                       destination, length);
            if(result != KUI_GAME_OK) return result;
            destination += length;
        } else {
            for(uint32_t sector = 0; sector < take; ++sector) {
                result = image->files.read(image->files.ctx, track->name, offset,
                                           raw, track->sector_bytes);
                if(result != KUI_GAME_OK) return result;
                if(format == KUI_GAME_SECTOR_MODE1 && !sector_data(raw, track))
                    return KUI_GAME_MODE;
                memcpy(destination, raw + (format == KUI_GAME_SECTOR_RAW ? 0u : track->data_offset), bytes);
                destination += bytes;
                offset += track->sector_bytes;
            }
        }
        remaining -= take;
        cursor += take;
    }
    return KUI_GAME_OK;
}

/* The following parsers normalize containers into the same bounded track map.
 * Format references (no implementation imported): DiscJuggler fields and
 * version-relative footer offsets in DC-SWAT/DreamShell, modules/isofs/cdi.c,
 * include/isofs/cdi.h at 4a2b898cbc244b2fb9bd1698b45e5325056232fb;
 * CDI 2448-byte size code and pregap FAD normalization cross-checked against
 * flyinghead/flycast core/deps/chdpsr/cdipsr.cpp and core/imgread/cdi.cpp;
 * CUE INDEX,
 * PREGAP and POSTGAP handling in MAME src/lib/util/cdrom.cpp; single-session
 * ISO base detection in DreamShell modules/isofs/fs_iso9660.c. */
static bool safe_name(const char *name) {
    if(!name) return false;
    size_t n = 0;
    while(n < KUI_GAME_NAME_CAP && name[n]) ++n;
    if(!n || n == KUI_GAME_NAME_CAP || name[0] == '.' || name[0] == ' ' ||
       name[n - 1u] == '.' || name[n - 1u] == ' ') return false;
    for(size_t i = 0; i < n; ++i) {
        unsigned ch = (unsigned char)name[i];
        if(ch < 32 || ch > 126 || strchr("\\/:*?\"<>|", (int)ch)) return false;
    }
    return true;
}
static bool extension(const char *name, const char *suffix) {
    const char *dot = strrchr(name, '.');
    return dot && same_name(dot, suffix);
}
bool kui_game_image_name_supported(const char *name) {
    return safe_name(name) && (extension(name, ".gdi") || extension(name, ".cue") ||
        extension(name, ".iso") || extension(name, ".bin") ||
        extension(name, ".img") || extension(name, ".cdi"));
}
const char *kui_game_image_format_name(enum kui_game_image_format format) {
    switch(format) {
    case KUI_GAME_IMAGE_GDI: return "GDI";
    case KUI_GAME_IMAGE_ISO: return "ISO";
    case KUI_GAME_IMAGE_CUE: return "BIN/CUE";
    case KUI_GAME_IMAGE_CDI: return "CDI";
    case KUI_GAME_IMAGE_RAW: return "Raw BIN/IMG";
    default: return "Unknown";
    }
}
static uint16_t little16(const unsigned char *p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)p[1] << 8);
}
static uint32_t little32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void track_mode(struct kui_game_image_track *track, unsigned mode,
                       unsigned stride) {
    track->control = mode ? 4u : 0u;
    track->sector_mode = mode;
    track->sector_bytes = stride;
    track->data_offset = !mode || stride == 2048u ? 0u : stride == 2336u ? 8u :
                         mode == 1u ? 16u : 24u;
}
static enum kui_game_result mapped(const struct kui_game_image *image) {
    if(!image->count) return KUI_GAME_SYNTAX;
    for(unsigned i = 0; i < image->count; ++i) {
        const struct kui_game_image_track *track = &image->tracks[i];
        if(track->start_lba >= track->end_lba || track->end_lba > KUI_GAME_LBA_LIMIT)
            return KUI_GAME_RANGE;
        if(track->file_offset > track->file_bytes ||
           (uint64_t)(track->end_lba - track->start_lba) * track->sector_bytes >
               track->file_bytes - track->file_offset) return KUI_GAME_FILE_SIZE;
        if(i && track->start_lba < image->tracks[i - 1u].end_lba)
            return KUI_GAME_OVERLAP;
        uint64_t end = track->file_offset +
            (uint64_t)(track->end_lba - track->start_lba) * track->sector_bytes;
        for(unsigned j = 0; j < i; ++j) {
            const struct kui_game_image_track *other = &image->tracks[j];
            if(same_name(track->name, other->name)) {
                uint64_t other_end = other->file_offset +
                    (uint64_t)(other->end_lba - other->start_lba) * other->sector_bytes;
                if(track->file_offset < other_end && other->file_offset < end)
                    return KUI_GAME_OVERLAP;
            }
        }
    }
    return valid_image(image) ? KUI_GAME_OK : KUI_GAME_UNSUPPORTED;
}
struct cue_position {
    unsigned file, session, density;
    uint32_t index0, index1, pregap, postgap, last_index, leadin, leadout;
    bool have0, have1, have_pre, have_post, have_leadin, have_leadout;
};
static bool token(struct line *line, char *out, size_t cap) {
    space(line);
    size_t n = 0;
    while(line->at < line->end && *line->at != ' ' && *line->at != '\t') {
        unsigned ch = *line->at++;
        if(ch < 33u || ch > 126u || n + 1u >= cap) return false;
        out[n++] = (char)ch;
    }
    out[n] = 0;
    return n != 0;
}
static bool frames(struct line *line, uint32_t *out) {
    char time[24];
    if(!token(line, time, sizeof(time))) return false;
    const char *p = time;
    uint32_t component[3] = {0};
    for(unsigned i = 0; i < 3; ++i) {
        unsigned digits = 0;
        while(*p >= '0' && *p <= '9') {
            if(component[i] > 9999u) return false;
            component[i] = component[i] * 10u + (unsigned)(*p++ - '0');
            ++digits;
        }
        if(!digits || (i != 2 && *p++ != ':')) return false;
    }
    if(*p || component[1] >= 60u || component[2] >= 75u) return false;
    uint64_t value = (uint64_t)component[0] * 4500u + component[1] * 75u + component[2];
    if(value >= KUI_GAME_LBA_LIMIT) return false;
    *out = (uint32_t)value;
    return true;
}
static enum kui_game_result cue_parse(const unsigned char *data, size_t size,
                                      struct kui_game_image *image) {
    struct cue_position positions[KUI_GAME_TRACK_MAX] = {{0}};
    char current_file[KUI_GAME_NAME_CAP] = {0};
    unsigned group = 0, current_session = 1u, density = 0u;
    uint32_t pending_leadin = 0;
    bool have_pending_leadin = false, scramble_marker = false;
    bool single_density = false, high_density = false;
    struct kui_game_image_track *track = NULL;
    struct cue_position *position = NULL;
    const unsigned char *at = data, *end = data + size;
    if(size >= 3u && at[0] == 0xefu && at[1] == 0xbbu && at[2] == 0xbfu) at += 3u;
    while(at < end) {
        const unsigned char *next = at;
        while(next < end && *next != '\n') ++next;
        struct line line = {at, next};
        if(line.end > at && line.end[-1] == '\r') --line.end;
        char command[24];
        space(&line);
        if(line.at != line.end) {
            if(!token(&line, command, sizeof(command))) return KUI_GAME_SYNTAX;
            if(same_name(command, "FILE")) {
                char type[24];
                if(!filename(&line, current_file) || !token(&line, type, sizeof(type)))
                    return KUI_GAME_SYNTAX;
                if(!same_name(type, "BINARY")) return KUI_GAME_UNSUPPORTED;
                if(group && (!track || !position->have1)) return KUI_GAME_SYNTAX;
                ++group;
                track = NULL;
                position = NULL;
            } else if(same_name(command, "TRACK")) {
                uint32_t n;
                char type[24];
                if(!group || !number(&line, &n) || n != image->count + 1u ||
                   n > KUI_GAME_TRACK_MAX || !token(&line, type, sizeof(type)) ||
                   (position && !position->have1)) return KUI_GAME_SYNTAX;
                track = &image->tracks[image->count];
                position = &positions[image->count++];
                position->file = group;
                position->session = current_session;
                position->density = density;
                position->leadin = pending_leadin;
                position->have_leadin = have_pending_leadin;
                pending_leadin = 0;
                have_pending_leadin = false;
                track->number = n;
                memcpy(track->name, current_file, strlen(current_file) + 1u);
                if(same_name(type, "AUDIO")) track_mode(track, 0, 2352);
                else if(same_name(type, "CDG")) track_mode(track, 0, 2448);
                else if(same_name(type, "MODE1/2048")) track_mode(track, 1, 2048);
                else if(same_name(type, "MODE1/2352")) track_mode(track, 1, 2352);
                else if(same_name(type, "MODE2/2048")) track_mode(track, 2, 2048);
                else if(same_name(type, "MODE2/2336")) track_mode(track, 2, 2336);
                else if(same_name(type, "MODE2/2352")) track_mode(track, 2, 2352);
                else if(same_name(type, "MODE2/2448")) track_mode(track, 2, 2448);
                else if(same_name(type, "MODE1/2448")) track_mode(track, 1, 2448);
                else return KUI_GAME_UNSUPPORTED;
            } else if(same_name(command, "INDEX")) {
                uint32_t n, frame;
                if(!position || !number(&line, &n) || n > 99u || !frames(&line, &frame))
                    return KUI_GAME_SYNTAX;
                if(n == 0) {
                    if(position->have0 || position->have1) return KUI_GAME_SYNTAX;
                    position->index0 = frame;
                    position->have0 = true;
                } else if(n == 1) {
                    if(position->have1 || (position->have0 && frame < position->index0))
                        return KUI_GAME_SYNTAX;
                    position->index1 = position->last_index = frame;
                    position->have1 = true;
                } else {
                    if(!position->have1 || frame < position->last_index) return KUI_GAME_SYNTAX;
                    position->last_index = frame;
                }
            } else if(same_name(command, "PREGAP") || same_name(command, "POSTGAP")) {
                uint32_t frame;
                if(!position || !frames(&line, &frame)) return KUI_GAME_SYNTAX;
                if(same_name(command, "PREGAP")) {
                    if(position->have_pre) return KUI_GAME_SYNTAX;
                    position->have_pre = true;
                    position->pregap = frame;
                } else {
                    if(position->have_post) return KUI_GAME_SYNTAX;
                    position->have_post = true;
                    position->postgap = frame;
                }
            } else if(same_name(command, "REM")) {
                char kind[24] = {0};
                space(&line);
                const unsigned char *comment = line.at;
                if(line.at != line.end && !token(&line, kind, sizeof(kind))) {
                    /* Unknown REM text can be long or UTF-8; only known
                     * geometry directives need a bounded ASCII keyword. */
                    kind[0] = 0;
                    line.at = comment;
                }
                if(same_name(kind, "SESSION")) {
                    uint32_t session;
                    if(!number(&line, &session) || !session || session > 2u ||
                       session < current_session) return KUI_GAME_UNSUPPORTED;
                    current_session = session;
                } else if(same_name(kind, "LEAD-IN") || same_name(kind, "PREGAP")) {
                    uint32_t frame;
                    if(!frames(&line, &frame)) return KUI_GAME_SYNTAX;
                    if(frame > KUI_GAME_LBA_LIMIT - pending_leadin) return KUI_GAME_RANGE;
                    pending_leadin += frame;
                    have_pending_leadin = true;
                } else if(same_name(kind, "LEAD-OUT")) {
                    uint32_t frame;
                    if(!position || position->have_leadout || !frames(&line, &frame))
                        return KUI_GAME_SYNTAX;
                    position->leadout = frame;
                    position->have_leadout = true;
                } else if(same_name(kind, "SINGLE-DENSITY") || same_name(kind, "HIGH-DENSITY")) {
                    char area[16];
                    if(!token(&line, area, sizeof(area)) || !same_name(area, "AREA"))
                        return KUI_GAME_SYNTAX;
                    unsigned next_density = same_name(kind, "HIGH-DENSITY") ? 2u : 1u;
                    if(density == 2u && next_density == 1u) return KUI_GAME_UNSUPPORTED;
                    density = next_density;
                    if(density == 1u) single_density = true;
                    else high_density = true;
                } else if(same_name(kind, "KUI")) {
                    char setting[24];
                    uint32_t value;
                    if(!token(&line, setting, sizeof(setting)) ||
                       !same_name(setting, "SCRAMBLED") || !number(&line, &value) ||
                       value > 1u || scramble_marker) return KUI_GAME_SYNTAX;
                    image->scrambled = value != 0;
                    scramble_marker = true;
                } else {
                    for(const unsigned char *p = line.at; p < line.end; ++p)
                        if(*p < 32u && *p != '\t') return KUI_GAME_SYNTAX;
                    line.at = line.end;
                }
            } else if(same_name(command, "TITLE") ||
                      same_name(command, "PERFORMER") || same_name(command, "SONGWRITER") ||
                      same_name(command, "CATALOG") || same_name(command, "ISRC") ||
                      same_name(command, "FLAGS") || same_name(command, "CDTEXTFILE")) {
                /* Metadata has no effect on the sector layout. Still reject NUL/control
                 * bytes so malformed binary input cannot masquerade as a cue. */
                for(const unsigned char *p = line.at; p < line.end; ++p)
                    if(*p < 32u && *p != '\t') return KUI_GAME_SYNTAX;
                line.at = line.end;
            } else return KUI_GAME_UNSUPPORTED;
            space(&line);
            if(line.at != line.end) return KUI_GAME_SYNTAX;
        }
        at = next == end ? end : next + 1u;
    }
    if(!image->count || !position || !position->have1 || !track) return KUI_GAME_SYNTAX;
    uint64_t logical = 0;
    unsigned first = 0;
    while(first < image->count) {
        unsigned last = first + 1u;
        while(last < image->count && positions[last].file == positions[first].file) ++last;
        uint64_t bytes = 0;
        enum kui_game_result result = image->files.stat(image->files.ctx,
            image->tracks[first].name, &bytes);
        if(result != KUI_GAME_OK) return result;
        if(!bytes) return KUI_GAME_FILE_SIZE;
        for(unsigned j = 0; j < first; ++j)
            if(same_name(image->tracks[first].name, image->tracks[j].name))
                return KUI_GAME_OVERLAP; /* Reusing a FILE stanza aliases the same bytes. */
        uint64_t byte_cursor = 0;
        uint32_t file_frame = 0;
        for(unsigned i = first; i < last; ++i) {
            struct kui_game_image_track *t = &image->tracks[i];
            struct cue_position *pos = &positions[i];
            uint32_t begin = pos->have0 ? pos->index0 : pos->index1;
            if(begin < file_frame || pos->index1 < begin) return KUI_GAME_OVERLAP;
            uint64_t skipped = (uint64_t)(pos->index1 - file_frame) * t->sector_bytes;
            if(skipped > bytes - byte_cursor) return KUI_GAME_FILE_SIZE;
            if(i && pos->session > positions[i - 1u].session &&
               pos->file != positions[i - 1u].file)
                logical += pos->have_leadin ? pos->leadin : 4500u;
            else if(pos->have_leadin)
                return KUI_GAME_UNSUPPORTED; /* A lead-in only belongs to a new session. */
            logical += pos->index1 - file_frame;
            logical += pos->pregap;
            if(pos->density == 2u && (!i || positions[i - 1u].density != 2u))
                logical = 45000u;
            byte_cursor += skipped;
            t->file_offset = byte_cursor;
            t->file_bytes = bytes;
            uint64_t sectors;
            if(i + 1u < last) {
                struct cue_position *after = &positions[i + 1u];
                uint32_t next_begin = after->have0 ? after->index0 : after->index1;
                if(after->session > pos->session && pos->have_leadout) {
                    /* DiscImageCreator .img omits the intersession padding
                     * represented by INDEX timestamps. Do not apply the BIN
                     * byte clock to that distinct shared-file convention. */
                    if(extension(t->name, ".img")) return KUI_GAME_UNSUPPORTED;
                    if(pos->leadout > next_begin || pos->leadout <= pos->index1)
                        return KUI_GAME_RANGE;
                    next_begin = pos->leadout;
                } else if(pos->have_leadout) return KUI_GAME_UNSUPPORTED;
                if(next_begin <= pos->index1) return KUI_GAME_OVERLAP;
                sectors = next_begin - pos->index1;
            } else {
                if((bytes - byte_cursor) % t->sector_bytes) return KUI_GAME_FILE_SIZE;
                sectors = (bytes - byte_cursor) / t->sector_bytes;
            }
            if(!sectors || logical >= KUI_GAME_LBA_LIMIT ||
               sectors > KUI_GAME_LBA_LIMIT - logical) return KUI_GAME_RANGE;
            if(pos->last_index >= (uint64_t)pos->index1 + sectors) return KUI_GAME_RANGE;
            if(sectors * t->sector_bytes > bytes - byte_cursor) return KUI_GAME_FILE_SIZE;
            t->start_lba = (uint32_t)logical;
            t->end_lba = (uint32_t)(logical + sectors);
            byte_cursor += sectors * t->sector_bytes;
            file_frame = pos->index1 + (uint32_t)sectors;
            logical += sectors + pos->postgap;
            if(i + 1u < image->count && positions[i + 1u].session > pos->session &&
               positions[i + 1u].file != pos->file)
                logical += pos->have_leadout ? pos->leadout :
                           pos->session == 1u ? 6750u : 2250u;
            else if(i + 1u == image->count && pos->have_leadout)
                return KUI_GAME_UNSUPPORTED;
        }
        first = last;
    }
    if(have_pending_leadin) return KUI_GAME_SYNTAX;
    image->format = KUI_GAME_IMAGE_CUE;
    image->cd_image = !(single_density && high_density);
    bool chosen = false;
    unsigned data_session = 0;
    for(unsigned i = 0; i < image->count; ++i) {
        if(image->tracks[i].control == 4 && (!chosen ||
           positions[i].session > data_session ||
           (image->data_lba < 45000u && image->tracks[i].start_lba >= 45000u))) {
            image->data_lba = image->tracks[i].start_lba;
            data_session = positions[i].session;
            chosen = true;
        }
    }
    return mapped(image);
}

struct footer_reader {
    const struct kui_game_file_ops *files;
    const char *name;
    uint64_t cursor, end;
};
static enum kui_game_result footer_read(struct footer_reader *r, void *out, size_t size) {
    if(r->cursor > r->end || size > r->end - r->cursor) return KUI_GAME_SYNTAX;
    enum kui_game_result result = r->files->read(r->files->ctx, r->name, r->cursor, out, size);
    if(result == KUI_GAME_OK) r->cursor += size;
    return result;
}
static enum kui_game_result footer_skip(struct footer_reader *r, unsigned size) {
    if(r->cursor > r->end || size > r->end - r->cursor) return KUI_GAME_SYNTAX;
    r->cursor += size;
    return KUI_GAME_OK;
}
static enum kui_game_result cdi_parse(const char *name, uint64_t bytes,
                                      struct kui_game_image *image) {
    static const unsigned char marker[20] = {
        0,0,1,0,0,0,255,255,255,255,0,0,1,0,0,0,255,255,255,255
    };
    if(bytes < 10u) return KUI_GAME_FILE_SIZE;
    unsigned char tail[8];
    enum kui_game_result result = image->files.read(image->files.ctx, name,
                                                   bytes - 8u, tail, sizeof(tail));
    if(result != KUI_GAME_OK) return result;
    uint32_t version = little32(tail), value = little32(tail + 4u);
    if(version != 0x80000004u && version != 0x80000005u && version != 0x80000006u)
        return KUI_GAME_UNSUPPORTED;
    if(!value || value >= bytes) return KUI_GAME_SYNTAX;
    uint64_t header = version == 0x80000006u ? bytes - value : value;
    if(header >= bytes - 8u || bytes - 8u - header > 65536u) return KUI_GAME_SYNTAX;
    struct footer_reader reader = {&image->files, name, header, bytes - 8u};
    unsigned char fields[87];
#define CDI_READ(n) do { result = footer_read(&reader, fields, (n)); if(result != KUI_GAME_OK) return result; } while(0)
#define CDI_SKIP(n) do { result = footer_skip(&reader, (n)); if(result != KUI_GAME_OK) return result; } while(0)
    CDI_READ(2u);
    unsigned sessions = little16(fields);
    if(!sessions || sessions > 2u) return KUI_GAME_UNSUPPORTED;
    uint64_t payload = 0;
    for(unsigned session = 0; session < sessions; ++session) {
        CDI_READ(2u);
        unsigned tracks = little16(fields);
        if(tracks > KUI_GAME_TRACK_MAX - image->count || (!tracks && session + 1u != sessions))
            return KUI_GAME_SYNTAX;
        bool session_data = false;
        for(unsigned i = 0; i < tracks; ++i) {
            CDI_READ(4u);
            if(little32(fields)) CDI_SKIP(8u);
            CDI_READ(20u);
            if(memcmp(fields, marker, sizeof(marker))) return KUI_GAME_SYNTAX;
            CDI_SKIP(4u);
            CDI_READ(1u);
            unsigned filename_bytes = fields[0];
            CDI_SKIP(filename_bytes);
            CDI_SKIP(19u);
            CDI_READ(4u);
            if(little32(fields) == 0x80000000u) CDI_SKIP(10u);
            else CDI_SKIP(2u);
            CDI_READ(87u);
            uint32_t pregap = little32(fields), length = little32(fields + 4u);
            unsigned mode = little32(fields + 14u);
            uint32_t lba = little32(fields + 30u), total = little32(fields + 34u);
            unsigned size_code = little32(fields + 54u), stride;
            if(mode > 2u || (size_code > 2u && size_code != 4u) ||
               (!mode && size_code < 2u))
                return KUI_GAME_UNSUPPORTED;
            stride = size_code == 0u ? 2048u : size_code == 1u ? 2336u :
                     size_code == 2u ? 2352u : 2448u;
            if(mode == 1u && stride == 2336u) return KUI_GAME_UNSUPPORTED;
            if(!length || pregap > total || length > total - pregap)
                return KUI_GAME_FILE_SIZE;
            uint64_t track_bytes = (uint64_t)total * stride;
            if(payload > header || track_bytes > header - payload) return KUI_GAME_FILE_SIZE;
            /* CDI records the pregap start in the disc's FAD coordinate
             * system. INDEX 01 follows the stored pregap; convert to LBA. */
            uint64_t index_fad = (uint64_t)lba + pregap;
            if(index_fad < 150u || index_fad - 150u >= KUI_GAME_LBA_LIMIT ||
               length > KUI_GAME_LBA_LIMIT - (index_fad - 150u))
                return KUI_GAME_RANGE;
            lba = (uint32_t)(index_fad - 150u);
            struct kui_game_image_track *track = &image->tracks[image->count];
            track->number = ++image->count;
            track->start_lba = lba;
            track->end_lba = lba + length;
            track->file_bytes = bytes;
            track->file_offset = payload + (uint64_t)pregap * stride;
            memcpy(track->name, name, strlen(name) + 1u);
            track_mode(track, mode, stride);
            payload += track_bytes;
            if(mode && !session_data) {
                image->data_lba = lba;
                session_data = true;
            }
            if(version != 0x80000004u) {
                CDI_SKIP(5u);
                CDI_READ(4u);
                if(little32(fields) == UINT32_MAX) CDI_SKIP(78u);
            }
        }
        CDI_SKIP(version == 0x80000004u ? 12u : 13u);
    }
#undef CDI_READ
#undef CDI_SKIP
    image->format = KUI_GAME_IMAGE_CDI;
    image->cd_image = true;
    return mapped(image);
}

static bool bcd(unsigned value, unsigned *out) {
    if((value & 15u) > 9u || (value >> 4) > 9u) return false;
    *out = (value >> 4) * 10u + (value & 15u);
    return true;
}
static enum kui_game_result single_parse(const char *name, uint64_t bytes,
    bool raw, struct kui_game_image *image) {
    unsigned stride = raw ? 2352u : 2048u;
    if(!bytes || bytes % stride) return KUI_GAME_FILE_SIZE;
    uint64_t sectors = bytes / stride;
    if(sectors > KUI_GAME_LBA_LIMIT) return KUI_GAME_RANGE;
    struct kui_game_image_track *track = &image->tracks[0];
    track->number = image->count = 1u;
    track->file_bytes = bytes;
    memcpy(track->name, name, strlen(name) + 1u);
    track_mode(track, 1u, stride);
    enum kui_game_result result;
    if(raw) {
        unsigned char sector[2352];
        result = image->files.read(image->files.ctx, name, 0, sector, sizeof(sector));
        if(result != KUI_GAME_OK) return result;
        if(sector[15] != 1u && sector[15] != 2u) return KUI_GAME_MODE;
        track_mode(track, sector[15], stride);
        if(!sector_data(sector, track)) return KUI_GAME_MODE;
        unsigned m, s, f;
        if(!bcd(sector[12], &m) || !bcd(sector[13], &s) || !bcd(sector[14], &f) ||
           s >= 60u || f >= 75u) return KUI_GAME_MODE;
        uint32_t fad = m * 4500u + s * 75u + f;
        if(fad < 150u) return KUI_GAME_RANGE;
        track->start_lba = fad - 150u;
    } else {
        /* A standalone ISO omits its session LBA. Recover it from the PVD
         * root extent and the matching physical '.' directory record, rather
         * than guessing 0 or 45000 and launching at a wrong address. */
        if(sectors < 18u) return KUI_GAME_FILE_SIZE;
        unsigned char root[34], record[34], head[7];
        bool primary = false, found = false;
        uint32_t root_lba = 0, pvd = 0;
        unsigned horizon = sectors < 64u ? (unsigned)sectors : 64u;
        for(unsigned i = 16u; i < 32u && i < horizon; ++i) {
            result = image->files.read(image->files.ctx, name, (uint64_t)i * 2048u,
                                       head, sizeof(head));
            if(result != KUI_GAME_OK) return result;
            if(memcmp(head + 1u, "CD001", 5u) || head[6] != 1u) return KUI_GAME_SYNTAX;
            if(head[0] == 255u) break;
            if(head[0] == 1u) {
                result = image->files.read(image->files.ctx, name,
                    (uint64_t)i * 2048u + 156u, root, sizeof(root));
                if(result != KUI_GAME_OK) return result;
                uint32_t be = (uint32_t)root[6] << 24 | (uint32_t)root[7] << 16 |
                              (uint32_t)root[8] << 8 | root[9];
                root_lba = little32(root + 2u);
                if(root[0] != 34u || root[32] != 1u || root[33] ||
                   !(root[25] & 2u) || root_lba != be) return KUI_GAME_SYNTAX;
                primary = true;
                pvd = i;
                break;
            }
        }
        if(!primary) return KUI_GAME_SYNTAX;
        for(unsigned i = pvd + 1u; i < horizon; ++i) {
            result = image->files.read(image->files.ctx, name, (uint64_t)i * 2048u,
                                       record, sizeof(record));
            if(result != KUI_GAME_OK) return result;
            if(record[0] == 34u && !memcmp(root + 2u, record + 2u, 32u)) {
                if(root_lba < i) return KUI_GAME_RANGE;
                uint32_t base = root_lba - i;
                if(found && base != track->start_lba) return KUI_GAME_OVERLAP;
                found = true;
                track->start_lba = base;
                /* One verified root match is sufficient; following directory
                 * sectors can contain ordinary '.' records from subfolders. */
                break;
            }
        }
        if(!found) return KUI_GAME_UNSUPPORTED;
    }
    if(track->start_lba >= KUI_GAME_LBA_LIMIT ||
       sectors > KUI_GAME_LBA_LIMIT - track->start_lba) return KUI_GAME_RANGE;
    track->end_lba = track->start_lba + (uint32_t)sectors;
    image->format = raw ? KUI_GAME_IMAGE_RAW : KUI_GAME_IMAGE_ISO;
    image->cd_image = track->start_lba < 45000u;
    image->data_lba = track->start_lba;
    return mapped(image);
}

enum kui_game_result kui_game_image_open_named(const char *name,
    const struct kui_game_file_ops *files, struct kui_game_image *out) {
    if(!safe_name(name) || !files || !files->stat || !files->read || !out)
        return KUI_GAME_INVALID;
    if(!kui_game_image_name_supported(name)) return KUI_GAME_UNSUPPORTED;
    uint64_t bytes = 0;
    enum kui_game_result result = files->stat(files->ctx, name, &bytes);
    if(result != KUI_GAME_OK) return result;
    if(extension(name, ".gdi") || extension(name, ".cue")) {
        if(!bytes || bytes > KUI_GAME_GDI_LIMIT) return KUI_GAME_FILE_SIZE;
        unsigned char *descriptor = malloc((size_t)bytes);
        if(!descriptor) return KUI_GAME_IO;
        result = files->read(files->ctx, name, 0, descriptor, (size_t)bytes);
        if(result == KUI_GAME_OK && extension(name, ".gdi"))
            result = kui_game_image_open(descriptor, (size_t)bytes, files, out);
        else if(result == KUI_GAME_OK) {
            struct kui_game_image image = {0};
            image.files = *files;
            result = cue_parse(descriptor, (size_t)bytes, &image);
            if(result == KUI_GAME_OK) *out = image;
        }
        free(descriptor);
        return result;
    }
    struct kui_game_image image = {0};
    image.files = *files;
    result = extension(name, ".cdi") ? cdi_parse(name, bytes, &image) :
        single_parse(name, bytes, !extension(name, ".iso"), &image);
    if(result == KUI_GAME_OK) *out = image;
    return result;
}

const char *kui_game_result_name(enum kui_game_result result) {
    switch(result) {
    case KUI_GAME_OK: return "OK";
    case KUI_GAME_INVALID: return "Invalid image request";
    case KUI_GAME_SYNTAX: return "Malformed image descriptor";
    case KUI_GAME_UNSUPPORTED: return "Unsupported image layout";
    case KUI_GAME_NOT_FOUND: return "Track file missing";
    case KUI_GAME_IO: return "Image read failed";
    case KUI_GAME_CANCELLED: return "Cancelled";
    case KUI_GAME_FILE_SIZE: return "Invalid track length";
    case KUI_GAME_OVERLAP: return "Overlapping or aliased tracks";
    case KUI_GAME_RANGE: return "Outside image bounds";
    case KUI_GAME_GAP: return "Unmapped disc gap";
    case KUI_GAME_AUDIO: return "Audio is not sector data";
    case KUI_GAME_MODE: return "Invalid or unsupported data sector";
    default: return "Unknown image error";
    }
}
