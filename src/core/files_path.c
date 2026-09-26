/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/files.h"
#include "kui/games_retail.h"
#include "kui/runtime.h"
#include <stdio.h>
#include <string.h>

static unsigned char lower(unsigned char c) { return c >= 'A' && c <= 'Z' ? (unsigned char)(c + 32) : c; }
static size_t bounded(const char *text, size_t limit) {
    size_t n = 0;
    if(text) while(n < limit && text[n]) ++n;
    return n;
}
static bool same_letters(const char *a, const char *b, size_t n) {
    for(size_t i = 0; i < n; ++i) if(lower((unsigned char)a[i]) != lower((unsigned char)b[i])) return false;
    return true;
}
static bool ends_with(const char *name, const char *suffix) {
    size_t n = strlen(name), m = strlen(suffix);
    return n > m && same_letters(name + n - m, suffix, m);
}

bool kui_files_path_valid(const char *path) {
    size_t length = bounded(path, KUI_FILES_PATH_CAP);
    if(!path || !length || length == KUI_FILES_PATH_CAP || path[0] != '/') return false;
    if(length == 1) return true;
    for(const char *at = path + 1;;) {
        const char *slash = strchr(at, '/');
        size_t n = slash ? (size_t)(slash - at) : strlen(at);
        if(!n || n >= KUI_FILES_NAME_CAP) return false;
        char name[KUI_FILES_NAME_CAP];
        memcpy(name, at, n); name[n] = 0;
        if(!kui_destination_name_valid(name)) return false;
        if(!slash) return true;
        at = slash + 1;
    }
}
bool kui_files_join(char out[KUI_FILES_PATH_CAP], const char *folder, const char *name) {
    if(!out) return false;
    out[0] = 0;
    if(!kui_files_path_valid(folder) || !kui_destination_name_valid(name)) return false;
    char joined[KUI_FILES_PATH_CAP];
    int n = snprintf(joined, sizeof(joined), "%s%s%s", folder, folder[1] ? "/" : "", name);
    if(n <= 0 || n >= (int)sizeof(joined)) return false;
    memcpy(out, joined, (size_t)n + 1u);
    return true;
}
bool kui_files_parent(char out[KUI_FILES_PATH_CAP], const char *path) {
    if(!out) return false;
    out[0] = 0;
    if(!kui_files_path_valid(path)) return false;
    const char *slash = strrchr(path, '/');
    size_t n = slash == path ? 1u : (size_t)(slash - path);
    memcpy(out, path, n); out[n] = 0;
    return true;
}
const char *kui_files_leaf(const char *path) {
    const char *slash = path ? strrchr(path, '/') : NULL;
    return slash ? slash + 1 : path ? path : "";
}
bool kui_files_within(const char *path, const char *folder) {
    if(!kui_files_path_valid(path) || !kui_files_path_valid(folder)) return false;
    if(!folder[1]) return true;
    size_t n = strlen(folder);
    return strlen(path) >= n && same_letters(path, folder, n) && (!path[n] || path[n] == '/');
}
bool kui_files_protected(const char *path) {
    /* Kept in step with the loaders' own paths, less the drive prefix. */
    static const char *const needed[] = {KUI_RUNTIME_PATH + 2, KUI_GAMES_RETAIL_PACKAGE + 2};
    for(size_t i = 0; i < sizeof(needed) / sizeof(needed[0]); ++i)
        if(kui_files_within(needed[i], path)) return true;
    return false;
}
bool kui_files_numbered(char out[KUI_FILES_NAME_CAP], const char *name, bool directory, unsigned number) {
    if(!out) return false;
    out[0] = 0;
    if(!kui_destination_name_valid(name) || number < 2) return false;
    /* A leading dot names the file rather than starting an extension. */
    const char *dot = directory ? NULL : strrchr(name, '.');
    if(dot == name) dot = NULL;
    size_t stem = dot ? (size_t)(dot - name) : strlen(name);
    char result[KUI_FILES_NAME_CAP + 16];
    int n = snprintf(result, sizeof(result), "%.*s (%u)%s", (int)stem, name, number, dot ? dot : "");
    if(n <= 0 || n >= (int)KUI_FILES_NAME_CAP || !kui_destination_name_valid(result)) return false;
    memcpy(out, result, (size_t)n + 1u);
    return true;
}
bool kui_files_part_name(const char *name) {
    return name && ends_with(name, KUI_FILES_PART_SUFFIX);
}
enum kui_files_kind kui_files_kind(const char *name, bool directory) {
    if(directory) return KUI_FILES_KIND_FOLDER;
    if(!name) return KUI_FILES_KIND_OTHER;
    if(ends_with(name, ".gdi")) return KUI_FILES_KIND_GDI;
    if(ends_with(name, ".wav") || ends_with(name, ".ogg")) return KUI_FILES_KIND_AUDIO;
    if(ends_with(name, ".png") || ends_with(name, ".jpg") || ends_with(name, ".jpeg") ||
       ends_with(name, ".pvr")) return KUI_FILES_KIND_PICTURE;
    return KUI_FILES_KIND_OTHER;
}
int kui_files_compare(bool a_directory, const char *a, bool b_directory, const char *b) {
    if(a_directory != b_directory) return a_directory ? -1 : 1;
    for(size_t i = 0;; ++i) {
        unsigned char x = lower((unsigned char)a[i]), y = lower((unsigned char)b[i]);
        if(x != y) return x < y ? -1 : 1;
        if(!x) break;
    }
    int exact = strcmp(a, b);
    return exact < 0 ? -1 : exact > 0;
}
void kui_files_size_text(char out[16], uint64_t bytes) {
    static const char *const units[] = {"B", "KB", "MB", "GB", "TB"};
    if(bytes < 1024u) { snprintf(out, 16, "%u B", (unsigned)bytes); return; }
    unsigned unit = 0;
    uint64_t scale = 1;
    while(unit < 4 && bytes / scale >= 1024u) { scale *= 1024u; ++unit; }
    /* Tenths, rounded to nearest; one decimal below 100. */
    uint64_t tenths = bytes / scale * 10u + ((bytes % scale) * 10u + scale / 2u) / scale;
    if(tenths < 1000u) snprintf(out, 16, "%u.%u %s", (unsigned)(tenths / 10u), (unsigned)(tenths % 10u), units[unit]);
    else snprintf(out, 16, "%u %s", (unsigned)((tenths + 5u) / 10u), units[unit]);
}
void kui_files_date_text(char out[20], uint16_t date, uint16_t time) {
    unsigned year = 1980u + (date >> 9), month = (date >> 5) & 15u, day = date & 31u;
    unsigned hour = time >> 11, minute = (time >> 5) & 63u;
    if(!date || month < 1 || month > 12 || day < 1 || hour > 23 || minute > 59) {
        snprintf(out, 20, "No date");
        return;
    }
    snprintf(out, 20, "%04u-%02u-%02u %02u:%02u", year, month, day, hour, minute);
}
