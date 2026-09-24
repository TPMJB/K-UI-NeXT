/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_image.h"
#include <stdbool.h>
#include <string.h>

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
            if((control != 0 && control != 4) || bytes != KUI_GAME_RAW_BYTES || offset)
                return KUI_GAME_UNSUPPORTED;
            if(lba >= KUI_GAME_LBA_LIMIT) return KUI_GAME_RANGE;
            if(row > 1u && lba <= image->tracks[row - 2u].start_lba)
                return KUI_GAME_OVERLAP;
            for(unsigned i = 0; i + 1u < row; ++i)
                if(same_name(image->tracks[i].name, track->name))
                    return KUI_GAME_OVERLAP;
            track->number = index;
            track->start_lba = lba;
            track->control = control;
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
        if(!bytes || bytes % KUI_GAME_RAW_BYTES) return KUI_GAME_FILE_SIZE;
        uint64_t sectors = bytes / KUI_GAME_RAW_BYTES;
        if(sectors > KUI_GAME_LBA_LIMIT - track->start_lba)
            return KUI_GAME_RANGE;
        track->file_bytes = bytes;
        track->end_lba = track->start_lba + (uint32_t)sectors;
        if(i + 1u < image.count && track->end_lba > image.tracks[i + 1u].start_lba)
            return KUI_GAME_OVERLAP;
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
           track->file_bytes != (uint64_t)(track->end_lba - track->start_lba) *
                                KUI_GAME_RAW_BYTES ||
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
        cursor = track->end_lba < end ? track->end_lba : (uint32_t)end;
    }
    return cursor == end ? KUI_GAME_OK : KUI_GAME_RANGE;
}

static bool mode1(const unsigned char raw[KUI_GAME_RAW_BYTES]) {
    if(raw[0] || raw[11] || raw[15] != 1) return false;
    for(unsigned i = 1; i < 11; ++i) if(raw[i] != 255) return false;
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
    unsigned char raw[KUI_GAME_RAW_BYTES];
    for(unsigned i = 0; i < image->count && remaining; ++i) {
        const struct kui_game_image_track *track = &image->tracks[i];
        if(track->end_lba <= cursor) continue;
        uint32_t take = track->end_lba - cursor;
        if(take > remaining) take = remaining;
        uint64_t offset = (uint64_t)(cursor - track->start_lba) * KUI_GAME_RAW_BYTES;
        if(format == KUI_GAME_SECTOR_RAW) {
            size_t length = (size_t)take * KUI_GAME_RAW_BYTES;
            result = image->files.read(image->files.ctx, track->name, offset,
                                       destination, length);
            if(result != KUI_GAME_OK) return result;
            destination += length;
        } else {
            for(uint32_t sector = 0; sector < take; ++sector) {
                result = image->files.read(image->files.ctx, track->name, offset,
                                           raw, sizeof(raw));
                if(result != KUI_GAME_OK) return result;
                if(!mode1(raw)) return KUI_GAME_MODE;
                memcpy(destination, raw + 16, KUI_GAME_DATA_BYTES);
                destination += KUI_GAME_DATA_BYTES;
                offset += KUI_GAME_RAW_BYTES;
            }
        }
        remaining -= take;
        cursor += take;
    }
    return KUI_GAME_OK;
}

const char *kui_game_result_name(enum kui_game_result result) {
    switch(result) {
    case KUI_GAME_OK: return "OK";
    case KUI_GAME_INVALID: return "Invalid image request";
    case KUI_GAME_SYNTAX: return "Malformed GDI";
    case KUI_GAME_UNSUPPORTED: return "Unsupported GDI layout";
    case KUI_GAME_NOT_FOUND: return "Track file missing";
    case KUI_GAME_IO: return "Image read failed";
    case KUI_GAME_CANCELLED: return "Cancelled";
    case KUI_GAME_FILE_SIZE: return "Invalid raw track length";
    case KUI_GAME_OVERLAP: return "Overlapping or aliased tracks";
    case KUI_GAME_RANGE: return "Outside image bounds";
    case KUI_GAME_GAP: return "Unmapped disc gap";
    case KUI_GAME_AUDIO: return "Audio is not Mode 1 data";
    case KUI_GAME_MODE: return "Invalid or unsupported data sector";
    default: return "Unknown image error";
    }
}
