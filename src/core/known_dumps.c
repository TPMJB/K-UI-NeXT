/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/known_dumps.h"
#include <stdlib.h>
#include <string.h>

#define DB_HEADER "DREAMSHELL_REDUMP_CRC_V1"
#define LINE_BYTES 512u
#define NAME_BYTES 192u
#define MAX_TRACKS 99u

/* One catalogue entry being read: how many tracks it declares, how many it
 * listed, and how many of those the capture matches exactly. */
struct candidate {
    uint32_t declared, seen, exact, data_represented, data_exact;
    bool listed[MAX_TRACKS + 1];
    char name[NAME_BYTES];
};
struct best {
    uint32_t rank;
    enum kui_known_result result;
    char name[NAME_BYTES];
};

static void copy_text(char *dst, size_t capacity, const char *src) {
    size_t n = strlen(src);
    if(!capacity) return;
    if(n >= capacity) n = capacity - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}
static void trim_line(char *line) {
    size_t n = strlen(line);
    while(n && (line[n - 1] == '\r' || line[n - 1] == '\n')) line[--n] = 0;
}
static const struct kui_known_track *find_track(const struct kui_known_track *tracks,
                                                unsigned count, uint32_t number) {
    for(unsigned i = 0; i < count; ++i) if(tracks[i].number == number) return &tracks[i];
    return NULL;
}

/* How well one catalogue entry matches, as a grade plus a rank for choosing
 * between entries of the same grade (more exact tracks wins). Rank 0 means no
 * useful match. The ladder is the one the earlier GD Ripper used, unchanged. */
static enum kui_known_result grade(const struct candidate *c, unsigned local_tracks,
                                   unsigned local_data, uint32_t *rank) {
    bool full = c->declared == local_tracks && c->seen == c->declared && c->exact == local_tracks;
    bool all_data = local_data > 0 && c->data_represented == local_data && c->data_exact == local_data;
    if(full) { *rank = 400000 + c->exact; return KUI_KNOWN_FULL_MATCH; }
    if(all_data) {
        if(c->declared >= local_tracks) { *rank = 300000 + c->exact; return KUI_KNOWN_DATA_MATCH; }
        *rank = 200000 + c->data_exact * 100 + c->exact;
        return KUI_KNOWN_IDENTIFIED;
    }
    if(c->data_exact && c->data_represented < local_data) {
        *rank = 200000 + c->data_exact * 100 + c->exact;
        return KUI_KNOWN_IDENTIFIED;
    }
    if(c->data_exact) { *rank = 100000 + c->data_exact * 100 + c->exact; return KUI_KNOWN_PARTIAL; }
    *rank = 0;
    return KUI_KNOWN_NO_MATCH;
}
static void finish(const struct candidate *c, unsigned local_tracks, unsigned local_data,
                   struct best *best) {
    uint32_t rank;
    enum kui_known_result result = grade(c, local_tracks, local_data, &rank);
    if(rank > best->rank) {
        best->rank = rank;
        best->result = result;
        copy_text(best->name, sizeof(best->name), c->name);
    }
}

/* "T<tab>number<tab>size<tab>crc32". Strict: three fields, hex of 1-8 digits,
 * nothing after it. A line that does not parse is skipped, never guessed at. */
static bool parse_track(const char *line, unsigned long *number, unsigned long long *size,
                        uint32_t *crc) {
    char *end;
    const char *p = line + 2;
    *number = strtoul(p, &end, 10);
    if(end == p || *end != '\t') return false;
    p = end + 1;
    *size = strtoull(p, &end, 10);
    if(end == p || *end != '\t') return false;
    p = end + 1;
    unsigned long value = 0;
    size_t digits = 0;
    for(; p[digits]; ++digits) {
        char c = p[digits];
        unsigned d = c >= '0' && c <= '9' ? (unsigned)(c - '0') :
                     c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10) :
                     c >= 'A' && c <= 'F' ? (unsigned)(c - 'A' + 10) : 16u;
        if(d > 15 || digits >= 8) return false;
        value = value << 4 | d;
    }
    if(!digits) return false;
    *crc = (uint32_t)value;
    return true;
}

enum kui_known_result kui_known_search(kui_known_line_fn next, void *ctx,
    const struct kui_known_track *tracks, unsigned count, char *name, size_t name_capacity,
    bool (*cancelled)(void *), void *cancel_ctx) {
    char line[LINE_BYTES];
    struct candidate cand;
    struct best best;
    unsigned local_data = 0, lines = 0;
    bool in_game = false;
    if(!next || !tracks || !count || count > MAX_TRACKS || !name || !name_capacity)
        return KUI_KNOWN_ERROR;
    name[0] = 0;
    if(!next(ctx, line, sizeof(line))) return KUI_KNOWN_ERROR;
    trim_line(line);
    if(strcmp(line, DB_HEADER)) return KUI_KNOWN_ERROR;   /* not a catalogue we know how to read */
    for(unsigned i = 0; i < count; ++i) if(tracks[i].data) ++local_data;
    memset(&cand, 0, sizeof(cand));
    memset(&best, 0, sizeof(best));
    best.result = KUI_KNOWN_NO_MATCH;
    while(next(ctx, line, sizeof(line))) {
        if(cancelled && !(++lines & 255u) && cancelled(cancel_ctx)) return KUI_KNOWN_CANCELLED;
        trim_line(line);
        if(!line[0] || line[0] == '#') continue;
        if(line[0] == 'G' && line[1] == '\t') {
            char *end;
            unsigned long declared;
            if(in_game) finish(&cand, count, local_data, &best);
            memset(&cand, 0, sizeof(cand));
            declared = strtoul(line + 2, &end, 10);
            if(end == line + 2 || !declared || declared > MAX_TRACKS || *end != '\t') {
                in_game = false;   /* malformed entry: ignore it whole */
                continue;
            }
            cand.declared = (uint32_t)declared;
            copy_text(cand.name, sizeof(cand.name), end + 1);
            in_game = true;
        } else if(line[0] == 'T' && line[1] == '\t' && in_game) {
            unsigned long number;
            unsigned long long size;
            uint32_t crc;
            const struct kui_known_track *track;
            if(!parse_track(line, &number, &size, &crc)) continue;
            if(!number || number > MAX_TRACKS || cand.listed[number]) {
                in_game = false;   /* a repeated or impossible track number poisons the entry */
                continue;
            }
            cand.listed[number] = true;
            ++cand.seen;
            track = find_track(tracks, count, (uint32_t)number);
            if(!track) continue;
            if(track->data) ++cand.data_represented;
            if(track->bytes == size && track->crc32 == crc) {
                ++cand.exact;
                if(track->data) ++cand.data_exact;
            }
        } else if(!strcmp(line, "E") && in_game) {
            finish(&cand, count, local_data, &best);
            in_game = false;
        }
    }
    if(in_game) finish(&cand, count, local_data, &best);   /* a final entry with no closing E */
    if(best.rank) copy_text(name, name_capacity, best.name);
    return best.result;
}

/* --- the card ------------------------------------------------------------- */

/* FF_USE_STRFUNC is off, so f_gets is not available: assemble lines from
 * f_read ourselves. A line longer than the buffer is truncated and its
 * remainder discarded, so it can never be mistaken for a following line. */
struct file_lines {
    FIL file;
    uint8_t buffer[512];
    UINT have, pos;
    bool error;
};
static bool file_line(void *ctx, char *line, size_t capacity) {
    struct file_lines *f = ctx;
    size_t n = 0;
    bool any = false;
    for(;;) {
        if(f->pos == f->have) {
            UINT got = 0;
            if(f->error) break;
            if(f_read(&f->file, f->buffer, sizeof(f->buffer), &got) != FR_OK) { f->error = true; break; }
            if(!got) break;
            f->have = got; f->pos = 0;
        }
        uint8_t c = f->buffer[f->pos++];
        any = true;
        if(c == '\n') { line[n] = 0; return true; }
        if(c != '\r' && n + 1 < capacity) line[n++] = (char)c;
    }
    line[n] = 0;
    return any;   /* a final line with no newline still counts */
}

enum kui_known_result kui_known_search_file(const char *path,
    const struct kui_known_track *tracks, unsigned count, char *name, size_t name_capacity,
    bool (*cancelled)(void *), void *cancel_ctx) {
    struct file_lines f;
    enum kui_known_result result;
    memset(&f, 0, sizeof(f));
    FRESULT r = f_open(&f.file, path, FA_READ);
    if(r == FR_NO_FILE || r == FR_NO_PATH) { if(name && name_capacity) name[0] = 0; return KUI_KNOWN_NO_DATABASE; }
    if(r != FR_OK) { if(name && name_capacity) name[0] = 0; return KUI_KNOWN_ERROR; }
    result = kui_known_search(file_line, &f, tracks, count, name, name_capacity, cancelled, cancel_ctx);
    /* A read error part-way means the search saw only some of the catalogue.
     * A match found before it is still a real match; "nothing matched" is not. */
    if(f.error && result == KUI_KNOWN_NO_MATCH) result = KUI_KNOWN_ERROR;
    f_close(&f.file);
    return result;
}

void kui_known_check(const char *redump_path, const char *tosec_path,
    const struct kui_known_track *tracks, unsigned count, struct kui_known_summary *out,
    bool (*cancelled)(void *), void *cancel_ctx) {
    char tosec_name[sizeof(out->name)];
    enum kui_known_result redump, tosec;
    memset(out, 0, sizeof(*out));
    tosec_name[0] = 0;
    copy_text(out->catalog, sizeof(out->catalog), "Redump");
    redump = kui_known_search_file(redump_path, tracks, count, out->name, sizeof(out->name),
                                   cancelled, cancel_ctx);
    out->result = redump;
    if(redump == KUI_KNOWN_CANCELLED) return;
    tosec = kui_known_search_file(tosec_path, tracks, count, tosec_name, sizeof(tosec_name),
                                  cancelled, cancel_ctx);
    if(tosec == KUI_KNOWN_CANCELLED) { out->result = KUI_KNOWN_CANCELLED; return; }
    if(tosec >= KUI_KNOWN_PARTIAL && redump < tosec) {          /* TOSEC matched better */
        out->result = tosec;
        copy_text(out->name, sizeof(out->name), tosec_name);
        copy_text(out->catalog, sizeof(out->catalog), "TOSEC");
    } else if(redump == KUI_KNOWN_NO_MATCH && tosec == KUI_KNOWN_NO_MATCH) {
        copy_text(out->catalog, sizeof(out->catalog), "Redump+TOSEC");
    } else if((redump == KUI_KNOWN_NO_DATABASE || redump == KUI_KNOWN_ERROR) &&
              tosec == KUI_KNOWN_NO_MATCH) {
        out->result = tosec;
        copy_text(out->catalog, sizeof(out->catalog), "TOSEC");
    } else if(redump == KUI_KNOWN_NO_DATABASE && tosec == KUI_KNOWN_ERROR) {
        out->result = tosec;                                    /* a catalogue exists but is unreadable */
        copy_text(out->catalog, sizeof(out->catalog), "TOSEC");
    }
}

const char *kui_known_text(enum kui_known_result result) {
    switch(result) {
        case KUI_KNOWN_CANCELLED: return "CANCELLED";
        case KUI_KNOWN_NO_DATABASE: return "NOT COMPARED - NO DATABASE ON CARD";
        case KUI_KNOWN_NO_MATCH: return "NO CATALOGUE MATCH (INCONCLUSIVE)";
        case KUI_KNOWN_PARTIAL: return "PARTIAL MATCH ONLY";
        case KUI_KNOWN_IDENTIFIED: return "IDENTIFIED BY DATA TRACK";
        case KUI_KNOWN_DATA_MATCH: return "DATA TRACKS MATCH";
        case KUI_KNOWN_FULL_MATCH: return "FULL TRACK MATCH";
        default: return "CATALOGUE READ ERROR";
    }
}
