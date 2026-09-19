/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/options.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Defaults are chosen so a card with no bench.cfg still gives a useful sweep
 * in about two minutes: three SD chunk sizes, with and without preallocation,
 * 8 MiB each way, plus a fixed optical range at the start of the high-density
 * area (FAD 45150 on every GD-ROM, so results compare across discs). */
void kui_options_default(struct kui_options *out) {
    memset(out, 0, sizeof(*out));
    out->chunks[0] = 32; out->chunks[1] = 128; out->chunks[2] = 512;
    out->chunk_count = 3;
    out->sd_mib = 8;
    out->expand = true;
    out->hash_mib = 8;
    out->optical_fad = 45150;
    out->optical_sectors = 4096;
    out->yield_us = 2000;
    /* The transport pair KOS's own sd_init() hardcodes. Defaulting to it keeps
     * a card readable even if a previous run left an untested setting behind. */
    out->sd_if[0] = 0; out->sd_if_count = 1;
    out->sd_crc[0] = true; out->sd_crc_count = 1;
}

/* --- small helpers ------------------------------------------------------ */

static const char *skip_space(const char *p, const char *end) {
    while(p < end && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
    return p;
}
static const char *trim_end(const char *p, const char *end) {
    while(end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) --end;
    return end;
}
/* Whole token must be a decimal number inside [lo, hi]. */
static bool parse_unsigned(const char *p, const char *end, unsigned long lo,
                           unsigned long hi, unsigned long *out) {
    char tmp[16];
    size_t n = (size_t)(end - p);
    if(!n || n >= sizeof(tmp)) return false;
    memcpy(tmp, p, n); tmp[n] = 0;
    char *stop;
    unsigned long v = strtoul(tmp, &stop, 10);
    if(stop == tmp || *stop || v < lo || v > hi) return false;
    *out = v; return true;
}
static bool parse_bool(const char *p, const char *end, bool *out);

/* "scif" -> 0, "sci" -> 1. A word rather than a number so a report says which. */
static bool parse_interface(const char *p, const char *end, unsigned *out) {
    size_t n = (size_t)(end - p);
    if(n == 4 && !memcmp(p, "scif", 4)) { *out = 0; return true; }
    if(n == 3 && !memcmp(p, "sci", 3)) { *out = 1; return true; }
    return false;
}
/* Walks "a,b" and hands each item to one of the two parsers above. Duplicates
 * are rejected: sweeping the same setting twice is always a typo, and it would
 * put two identically-labelled lines in the report. */
static bool parse_sd_list(const char *p, const char *end, void *items, unsigned *count,
                          bool is_bool) {
    unsigned n = 0;
    for(;;) {
        const char *comma = memchr(p, ',', (size_t)(end - p));
        const char *item_end = comma ? comma : end;
        const char *s = skip_space(p, item_end), *e = trim_end(p, item_end);
        if(n == KUI_OPT_SD_MAX) return false;
        if(is_bool) {
            bool v;
            if(!parse_bool(s, e, &v)) return false;
            for(unsigned i = 0; i < n; ++i) if(((bool *)items)[i] == v) return false;
            ((bool *)items)[n++] = v;
        } else {
            unsigned v;
            if(!parse_interface(s, e, &v)) return false;
            for(unsigned i = 0; i < n; ++i) if(((unsigned *)items)[i] == v) return false;
            ((unsigned *)items)[n++] = v;
        }
        if(!comma) break;
        p = comma + 1;
    }
    *count = n; return true;
}
static bool parse_bool(const char *p, const char *end, bool *out) {
    size_t n = (size_t)(end - p);
    static const struct { const char *word; bool value; } words[] = {
        {"on", true}, {"1", true}, {"true", true}, {"yes", true},
        {"off", false}, {"0", false}, {"false", false}, {"no", false},
    };
    for(size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
        if(strlen(words[i].word) == n && !memcmp(p, words[i].word, n)) {
            *out = words[i].value; return true;
        }
    return false;
}
/* "32,128,512" -> chunks[], each in [1, KUI_OPT_CHUNK_MAX]. */
static bool parse_list(const char *p, const char *end, unsigned *list, unsigned *count) {
    unsigned n = 0;
    for(;;) {   /* one item per iteration; an empty item (",,", "32,") is an error */
        const char *comma = memchr(p, ',', (size_t)(end - p));
        const char *item_end = comma ? comma : end;
        unsigned long v;
        if(n == KUI_OPT_LIST_MAX) return false;
        if(!parse_unsigned(skip_space(p, item_end), trim_end(p, item_end), 1, KUI_OPT_CHUNK_MAX, &v))
            return false;
        list[n++] = (unsigned)v;
        if(!comma) break;
        p = comma + 1;
    }
    *count = n; return true;
}

/* --- parser --------------------------------------------------------------- */

static bool apply(struct kui_options *o, const char *key, size_t klen,
                  const char *v, const char *vend) {
    unsigned long n;
#define KEY(s) (klen == sizeof(s) - 1 && !memcmp(key, s, klen))
    if(KEY("chunks")) return parse_list(v, vend, o->chunks, &o->chunk_count);
    if(KEY("sd_mib")) { if(!parse_unsigned(v, vend, 1, 512, &n)) return false; o->sd_mib = (unsigned)n; return true; }
    if(KEY("hash_mib")) { if(!parse_unsigned(v, vend, 1, 256, &n)) return false; o->hash_mib = (unsigned)n; return true; }
    if(KEY("expand")) return parse_bool(v, vend, &o->expand);
    if(KEY("optical_fad")) { if(!parse_unsigned(v, vend, 150, 0xffffff, &n)) return false; o->optical_fad = (uint32_t)n; return true; }
    if(KEY("optical_sectors")) { if(!parse_unsigned(v, vend, 1, 65536, &n)) return false; o->optical_sectors = (unsigned)n; return true; }
    if(KEY("sd_if")) return parse_sd_list(v, vend, o->sd_if, &o->sd_if_count, false);
    if(KEY("sd_crc")) return parse_sd_list(v, vend, o->sd_crc, &o->sd_crc_count, true);
    if(KEY("yield_us")) { if(!parse_unsigned(v, vend, 100, 20000, &n)) return false; o->yield_us = (unsigned)n; return true; }
    if(KEY("note")) {
        size_t len = (size_t)(vend - v);
        if(len >= KUI_OPT_NOTE_MAX) len = KUI_OPT_NOTE_MAX - 1;
        memcpy(o->note, v, len); o->note[len] = 0; return true;
    }
#undef KEY
    return true; /* unknown key: caller logs it, value ignored */
}

bool kui_options_parse(struct kui_options *opt, const char *text, size_t size,
                       kui_log_fn log) {
    struct kui_options draft = *opt;   /* commit only if every line is valid */
    const char *p = text, *end = text + size;
    unsigned line_no = 0;
    while(p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        const char *line_end = nl ? nl : end;
        ++line_no;
        const char *s = skip_space(p, line_end), *e = trim_end(s, line_end);
        p = nl ? nl + 1 : end;
        if(s == e || *s == '#') continue;
        const char *eq = memchr(s, '=', (size_t)(e - s));
        if(!eq) { log("bench.cfg line %u: expected key=value", line_no); return false; }
        const char *key = s, *key_end = trim_end(s, eq);
        const char *val = skip_space(eq + 1, e);
        size_t klen = (size_t)(key_end - key);
        if(!klen) { log("bench.cfg line %u: empty key", line_no); return false; }
        static const char *known[] = {"chunks", "sd_mib", "hash_mib", "expand",
            "optical_fad", "optical_sectors", "yield_us", "sd_if", "sd_crc", "note"};
        bool is_known = false;
        for(size_t i = 0; i < sizeof(known) / sizeof(known[0]); ++i)
            if(strlen(known[i]) == klen && !memcmp(key, known[i], klen)) is_known = true;
        if(!is_known) { log("bench.cfg line %u: unknown key ignored", line_no); continue; }
        if(!apply(&draft, key, klen, val, e)) {
            log("bench.cfg line %u: bad value; file rejected, defaults kept", line_no);
            return false;
        }
    }
    *opt = draft;
    return true;
}

void kui_options_log(const struct kui_options *o, kui_log_fn log) {
    char list[KUI_OPT_LIST_MAX * 5], *w = list;
    for(unsigned i = 0; i < o->chunk_count; ++i)
        w += snprintf(w, (size_t)(list + sizeof(list) - w), "%s%u", i ? "," : "", o->chunks[i]);
    log("OPTIONS chunks=%s sd_mib=%u expand=%s hash_mib=%u",
        list, o->sd_mib, o->expand ? "on" : "off", o->hash_mib);
    log("OPTIONS optical_fad=%" PRIu32 " optical_sectors=%u yield_us=%u",
        o->optical_fad, o->optical_sectors, o->yield_us);
    char ifs[16] = "", crcs[16] = "";
    for(unsigned i = 0; i < o->sd_if_count; ++i)
        snprintf(ifs + strlen(ifs), sizeof(ifs) - strlen(ifs), "%s%s",
            i ? "," : "", o->sd_if[i] ? "sci" : "scif");
    for(unsigned i = 0; i < o->sd_crc_count; ++i)
        snprintf(crcs + strlen(crcs), sizeof(crcs) - strlen(crcs), "%s%s",
            i ? "," : "", o->sd_crc[i] ? "on" : "off");
    log("OPTIONS sd_if=%s sd_crc=%s", ifs, crcs);
    log("OPTIONS note=%s", o->note[0] ? o->note : "(none)");
}
