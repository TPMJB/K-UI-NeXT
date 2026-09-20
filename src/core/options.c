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
    /* Experiment keys: defaults reproduce the bench as it was before they existed. */
    /* While an operation runs the UI is capped at 2 redraws a second. Measured on the
     * console (docs/evidence/t1-ui-census-2026-09-19.json): the unthrottled loop took
     * 30-34% of the CPU, each redraw costs about 26 ms, so 2 Hz costs about 5% and
     * SD writes run 43-51% faster. "full" restores the unthrottled loop. Idle
     * screens are never capped. */
    out->ui_hz[0] = 2; out->ui_count = 1;
    out->sections = KUI_SEC_OPTICAL | KUI_SEC_HASH | KUI_SEC_SD;
    out->sweep_sectors = 2048;
    out->sweep_verify = true;
    out->sweep_mode_count = 1;   /* PIO only */
    out->sweep_spin_count = 1;   /* no competing thread */
    /* Capture engine: the engine as it has always been. */
    out->capture_hash_count = 1;      /* both */
    out->end_readback[0] = true; out->end_readback_count = 1;
    out->resume_check_count = 1;      /* full */
    out->sample_readback_count = 1;   /* 0: off */
    out->capture_sectors = 4096;
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
/* A list drawn from exactly two words ("both,crc32"): the first word is false, the
 * second true. Duplicates are rejected, as everywhere else. */
static bool parse_two_words(const char *p, const char *end, const char *no, const char *yes,
                            bool *items, unsigned *count) {
    unsigned n = 0;
    for(;;) {
        const char *comma = memchr(p, ',', (size_t)(end - p));
        const char *item_end = comma ? comma : end;
        const char *s = skip_space(p, item_end), *e = trim_end(p, item_end);
        size_t len = (size_t)(e - s);
        bool v;
        if(n == KUI_OPT_SD_MAX) return false;
        if(len == strlen(no) && !memcmp(s, no, len)) v = false;
        else if(len == strlen(yes) && !memcmp(s, yes, len)) v = true;
        else return false;
        for(unsigned i = 0; i < n; ++i) if(items[i] == v) return false;
        items[n++] = v;
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

/* "a,b,c" of decimal numbers in [lo, hi], each a multiple of `mult`. An empty
 * item, a duplicate or too many items is an error: a repeated point would put
 * two identically-labelled lines in the report. */
static bool parse_ulist(const char *p, const char *end, unsigned long lo, unsigned long hi,
                        unsigned long mult, unsigned max, unsigned *out, unsigned *count) {
    unsigned n = 0;
    for(;;) {
        const char *comma = memchr(p, ',', (size_t)(end - p));
        const char *item_end = comma ? comma : end;
        unsigned long v;
        if(n == max) return false;
        if(!parse_unsigned(skip_space(p, item_end), trim_end(p, item_end), lo, hi, &v) || v % mult)
            return false;
        for(unsigned i = 0; i < n; ++i) if(out[i] == v) return false;
        out[n++] = (unsigned)v;
        if(!comma) break;
        p = comma + 1;
    }
    *count = n; return true;
}
/* ui_hz items: the word "full" (today's unthrottled loop) or 0..30. */
static bool parse_ui_list(const char *p, const char *end, unsigned *out, unsigned *count) {
    unsigned n = 0;
    for(;;) {
        const char *comma = memchr(p, ',', (size_t)(end - p));
        const char *item_end = comma ? comma : end;
        const char *s = skip_space(p, item_end), *e = trim_end(p, item_end);
        unsigned long v;
        if(n == KUI_OPT_UI_MAX) return false;
        if(e - s == 4 && !memcmp(s, "full", 4)) v = KUI_OPT_UI_FULL;
        else if(!parse_unsigned(s, e, 0, KUI_OPT_UI_HZ_MAX, &v)) return false;
        for(unsigned i = 0; i < n; ++i) if(out[i] == v) return false;
        out[n++] = (unsigned)v;
        if(!comma) break;
        p = comma + 1;
    }
    *count = n; return true;
}
static bool parse_sections(const char *p, const char *end, unsigned *mask) {
    static const struct { const char *word; unsigned bit; } secs[] = {
        {"optical", KUI_SEC_OPTICAL}, {"hash", KUI_SEC_HASH},
        {"sd", KUI_SEC_SD}, {"sweep", KUI_SEC_SWEEP}, {"capture", KUI_SEC_CAPTURE},
        {"pipeline", KUI_SEC_PIPELINE},
    };
    unsigned m = 0;
    for(;;) {
        const char *comma = memchr(p, ',', (size_t)(end - p));
        const char *item_end = comma ? comma : end;
        const char *s = skip_space(p, item_end), *e = trim_end(p, item_end);
        unsigned bit = 0;
        for(size_t i = 0; i < sizeof(secs) / sizeof(secs[0]); ++i)
            if(strlen(secs[i].word) == (size_t)(e - s) && !memcmp(s, secs[i].word, (size_t)(e - s)))
                bit = secs[i].bit;
        if(!bit || (m & bit)) return false;   /* unknown word or repeated section */
        m |= bit;
        if(!comma) break;
        p = comma + 1;
    }
    *mask = m; return true;
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
    if(KEY("ui_hz")) return parse_ui_list(v, vend, o->ui_hz, &o->ui_count);
    if(KEY("sections")) return parse_sections(v, vend, &o->sections);
    if(KEY("sweep_chunks")) return parse_ulist(v, vend, 1, KUI_OPT_SWEEP_CHUNK_MAX, 1,
        KUI_OPT_LIST_MAX, o->sweep_chunks, &o->sweep_chunk_count);
    if(KEY("sweep_fads")) return parse_ulist(v, vend, 150, 0xffffff, 1,
        KUI_OPT_SWEEP_LIST_MAX, o->sweep_fads, &o->sweep_fad_count);
    if(KEY("sweep_gap_us")) return parse_ulist(v, vend, 0, 100000, 1,
        KUI_OPT_SWEEP_LIST_MAX, o->sweep_gap_us, &o->sweep_gap_count);
    if(KEY("sweep_service_us")) return parse_ulist(v, vend, 0, 50000, 1,
        KUI_OPT_SWEEP_LIST_MAX, o->sweep_service_us, &o->sweep_service_count);
    if(KEY("sweep_sectors")) { if(!parse_unsigned(v, vend, 32, 65536, &n)) return false; o->sweep_sectors = (unsigned)n; return true; }
    if(KEY("sweep_verify")) return parse_bool(v, vend, &o->sweep_verify);
    if(KEY("sweep_mode")) return parse_two_words(v, vend, "pio", "dma", o->sweep_dma, &o->sweep_mode_count);
    if(KEY("sweep_spin")) return parse_sd_list(v, vend, o->sweep_spin, &o->sweep_spin_count, true);
    /* Whole 512-byte blocks, at least 4 KiB, at most the bench buffer. */
    if(KEY("sd_bytes")) return parse_ulist(v, vend, 4096, (unsigned long)KUI_OPT_CHUNK_MAX * KUI_RAW_BYTES,
        512, KUI_OPT_SDBYTES_MAX, o->sd_bytes, &o->sd_bytes_count);
    if(KEY("capture_hash")) return parse_two_words(v, vend, "both", "crc32", o->capture_crc_only, &o->capture_hash_count);
    if(KEY("end_readback")) return parse_sd_list(v, vend, o->end_readback, &o->end_readback_count, true);
    if(KEY("resume_check")) return parse_two_words(v, vend, "full", "size", o->resume_size, &o->resume_check_count);
    if(KEY("sample_readback")) return parse_ulist(v, vend, 0, 1024, 1, KUI_OPT_CAPTURE_MAX,
        o->sample_readback, &o->sample_readback_count);
    if(KEY("capture_sectors")) { if(!parse_unsigned(v, vend, 64, 262144, &n)) return false; o->capture_sectors = (unsigned)n; return true; }
    if(KEY("capture_fad")) { if(!parse_unsigned(v, vend, 0, 0xffffff, &n) || (n && n < 150)) return false; o->capture_fad = (unsigned)n; return true; }
    if(KEY("capture_type")) {
        size_t len = (size_t)(vend - v);
        if(len == 4 && !memcmp(v, "data", 4)) { o->capture_audio = false; return true; }
        if(len == 5 && !memcmp(v, "audio", 5)) { o->capture_audio = true; return true; }
        return false;
    }
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
    /* Editors put invisible bytes at the start of a text file. A UTF-8 byte-order mark is skipped (it
     * made a valid file fail at "line 1": it is neither blank nor '#', and has no '='). UTF-16 cannot
     * be read as text at all, so it is named instead of being reported as a syntax error. */
    const unsigned char *head = (const unsigned char *)text;
    if(size >= 3 && head[0] == 0xef && head[1] == 0xbb && head[2] == 0xbf) {
        text += 3; size -= 3;
        log("bench.cfg starts with a UTF-8 byte-order mark; ignored");
    } else if(size >= 2 && ((head[0] == 0xff && head[1] == 0xfe) || (head[0] == 0xfe && head[1] == 0xff))) {
        log("bench.cfg is UTF-16 (starts %02x %02x); save it as plain text (UTF-8 or ANSI)",
            (unsigned)head[0], (unsigned)head[1]);
        return false;
    } else if(memchr(text, 0, size < 16 ? size : 16)) {
        log("bench.cfg contains NUL bytes, so it is not plain text (UTF-16?); save it as UTF-8 or ANSI");
        return false;
    }
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
        if(!eq) {
            /* Say what the line starts with, as text and as bytes: an invisible character is then visible. */
            static const char hex[] = "0123456789abcdef";
            char bytes[3 * 6 + 1], shown[6 + 1];
            size_t n = 0, k = 0;
            for(const char *q = s; q < e && k < 6; ++q, ++k) {
                unsigned char c = (unsigned char)*q;
                bytes[n++] = hex[c >> 4]; bytes[n++] = hex[c & 15]; bytes[n++] = ' ';
                shown[k] = c >= 32 && c < 127 ? (char)c : '.';
            }
            bytes[n ? n - 1 : 0] = 0; shown[k] = 0;
            log("bench.cfg line %u: expected key=value (starts \"%s\" = %s)", line_no, shown, bytes);
            return false;
        }
        const char *key = s, *key_end = trim_end(s, eq);
        const char *val = skip_space(eq + 1, e);
        size_t klen = (size_t)(key_end - key);
        if(!klen) { log("bench.cfg line %u: empty key", line_no); return false; }
        static const char *known[] = {"chunks", "sd_mib", "hash_mib", "expand",
            "optical_fad", "optical_sectors", "yield_us", "sd_if", "sd_crc", "note",
            "ui_hz", "sections", "sweep_chunks", "sweep_fads", "sweep_gap_us",
            "sweep_service_us", "sweep_sectors", "sweep_verify", "sd_bytes",
            "sweep_mode", "sweep_spin",
            "capture_hash", "end_readback", "resume_check", "sample_readback",
            "capture_sectors", "capture_fad", "capture_type"};
        bool is_known = false;
        for(size_t i = 0; i < sizeof(known) / sizeof(known[0]); ++i)
            if(strlen(known[i]) == klen && !memcmp(key, known[i], klen)) is_known = true;
        if(!is_known) {
            char name[25];
            size_t shown = klen < sizeof(name) - 1 ? klen : sizeof(name) - 1;
            memcpy(name, key, shown); name[shown] = 0;
            log("bench.cfg line %u: unknown key '%s' ignored", line_no, name);
            continue;
        }
        if(!apply(&draft, key, klen, val, e)) {
            log("bench.cfg line %u: bad value; file rejected, defaults kept", line_no);
            return false;
        }
    }
    *opt = draft;
    return true;
}

/* Appends "word", comma-separated, never past cap. Written as a plain copy
 * rather than snprintf(buf + strlen(buf), ...) because the compiler cannot
 * prove that form leaves room for a %s and rejects it at -O2 -Werror. */
static void append_word(char *dst, size_t cap, const char *word, bool first) {
    size_t n = strlen(dst);
    if(!first && n + 1 < cap) dst[n++] = ',';
    for(size_t i = 0; word[i] && n + 1 < cap; ++i) dst[n++] = word[i];
    dst[n] = 0;
}

/* "a,b,c", or `none` for an empty list. Plain copies for the same reason as
 * append_word: the compiler cannot prove snprintf(buf + strlen(buf)) fits. */
static void list_string(char *dst, size_t cap, const unsigned *v, unsigned n, const char *none) {
    dst[0] = 0;
    if(!n) { append_word(dst, cap, none, true); return; }
    for(unsigned i = 0; i < n; ++i) {
        char num[16];
        snprintf(num, sizeof(num), "%u", v[i]);
        append_word(dst, cap, num, i == 0);
    }
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
        append_word(ifs, sizeof(ifs), o->sd_if[i] ? "sci" : "scif", i == 0);
    for(unsigned i = 0; i < o->sd_crc_count; ++i)
        append_word(crcs, sizeof(crcs), o->sd_crc[i] ? "on" : "off", i == 0);
    log("OPTIONS sd_if=%s sd_crc=%s", ifs, crcs);
    char uis[32] = "", secs[32] = "", num[16];
    for(unsigned i = 0; i < o->ui_count; ++i) {
        snprintf(num, sizeof(num), "%u", o->ui_hz[i]);
        append_word(uis, sizeof(uis), o->ui_hz[i] == KUI_OPT_UI_FULL ? "full" : num, i == 0);
    }
    static const struct { const char *word; unsigned bit; } names[] = {
        {"optical", KUI_SEC_OPTICAL}, {"hash", KUI_SEC_HASH}, {"sd", KUI_SEC_SD}, {"sweep", KUI_SEC_SWEEP},
        {"capture", KUI_SEC_CAPTURE}, {"pipeline", KUI_SEC_PIPELINE}};
    bool first = true;
    for(size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if(o->sections & names[i].bit) { append_word(secs, sizeof(secs), names[i].word, first); first = false; }
    log("OPTIONS ui_hz=%s sections=%s", uis, secs);
    char chunks[48], fads[48], gaps[48], svcs[48], bytes[64];
    list_string(chunks, sizeof(chunks), o->sweep_chunks, o->sweep_chunk_count, "(sweep off)");
    list_string(fads, sizeof(fads), o->sweep_fads, o->sweep_fad_count, "(optical_fad)");
    list_string(gaps, sizeof(gaps), o->sweep_gap_us, o->sweep_gap_count, "0");
    list_string(svcs, sizeof(svcs), o->sweep_service_us, o->sweep_service_count, "0");
    list_string(bytes, sizeof(bytes), o->sd_bytes, o->sd_bytes_count, "(none)");
    log("OPTIONS sweep_chunks=%s sweep_sectors=%u verify=%s", chunks, o->sweep_sectors,
        o->sweep_verify ? "on" : "off");
    log("OPTIONS sweep_fads=%s", fads);
    log("OPTIONS sweep_gap_us=%s sweep_service_us=%s", gaps, svcs);
    char modes[16] = "", spins[16] = "";
    for(unsigned i = 0; i < o->sweep_mode_count; ++i)
        append_word(modes, sizeof(modes), o->sweep_dma[i] ? "dma" : "pio", i == 0);
    for(unsigned i = 0; i < o->sweep_spin_count; ++i)
        append_word(spins, sizeof(spins), o->sweep_spin[i] ? "on" : "off", i == 0);
    log("OPTIONS sweep_mode=%s sweep_spin=%s", modes, spins);
    log("OPTIONS sd_bytes=%s", bytes);
    char hashes[24] = "", ends[16] = "", checks[24] = "", samples[32];
    for(unsigned i = 0; i < o->capture_hash_count; ++i)
        append_word(hashes, sizeof(hashes), o->capture_crc_only[i] ? "crc32" : "both", i == 0);
    for(unsigned i = 0; i < o->end_readback_count; ++i)
        append_word(ends, sizeof(ends), o->end_readback[i] ? "on" : "off", i == 0);
    for(unsigned i = 0; i < o->resume_check_count; ++i)
        append_word(checks, sizeof(checks), o->resume_size[i] ? "size" : "full", i == 0);
    list_string(samples, sizeof(samples), o->sample_readback, o->sample_readback_count, "0");
    log("OPTIONS capture_hash=%s end_readback=%s resume_check=%s", hashes, ends, checks);
    log("OPTIONS sample_readback=%s capture_sectors=%u capture_type=%s", samples, o->capture_sectors,
        o->capture_audio ? "audio" : "data");
    if(o->capture_fad) log("OPTIONS capture_fad=%u", o->capture_fad);
    else log("OPTIONS capture_fad=(optical_fad)");
    log("OPTIONS note=%s", o->note[0] ? o->note : "(none)");
}
