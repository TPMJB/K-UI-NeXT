/* SPDX-License-Identifier: GPL-3.0-only */
/* The FTP server's protocol pieces: commands, paths, addresses and
 * listing lines. No sockets and no card access. */
#include "kui/ftp.h"
#include <stdio.h>
#include <string.h>

bool kui_ftp_parse(const char *line, char verb[8], const char **argument) {
    if(!verb || !argument) return false;
    verb[0] = 0;
    *argument = "";
    if(!line) return false;
    size_t n = 0;
    while(line[n] && line[n] != ' ') {
        char c = line[n];
        if(n >= 7 || !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) { verb[0] = 0; return false; }
        verb[n++] = (char)(c >= 'a' ? c - 32 : c);
    }
    verb[n] = 0;
    if(!n) return false;
    if(line[n] == ' ') *argument = line + n + 1;
    return true;
}
bool kui_ftp_resolve(char out[KUI_FILES_PATH_CAP], const char *folder, const char *argument) {
    if(!out) return false;
    out[0] = 0;
    if(!folder || !argument || !kui_files_path_valid(folder)) return false;
    char result[KUI_FILES_PATH_CAP];
    size_t used;
    if(argument[0] == '/') { result[0] = '/'; used = 1; }
    else { used = strlen(folder); memcpy(result, folder, used + 1u); }
    result[used] = 0;
    for(const char *at = argument;;) {
        while(*at == '/') ++at;
        if(!*at) break;
        const char *end = strchr(at, '/');
        size_t n = end ? (size_t)(end - at) : strlen(at);
        if(n == 1 && at[0] == '.') { /* stays */ }
        else if(n == 2 && at[0] == '.' && at[1] == '.') {
            char *slash = strrchr(result, '/');
            used = slash == result ? 1u : (size_t)(slash - result);
            result[used] = 0;
        } else {
            char name[KUI_FILES_NAME_CAP];
            if(n >= sizeof(name)) return false;
            memcpy(name, at, n);
            name[n] = 0;
            if(!kui_destination_name_valid(name)) return false;
            size_t need = used + (used > 1u ? 1u : 0u) + n;
            if(need >= sizeof(result)) return false;
            if(used > 1u) result[used++] = '/';
            memcpy(result + used, name, n + 1u);
            used += n;
        }
        at += n;
    }
    if(!kui_files_path_valid(result)) return false;
    memcpy(out, result, used + 1u);
    return true;
}
const char *kui_ftp_list_path(const char *argument) {
    if(!argument) return "";
    /* Options first, each word starting with '-'; the rest is the path. */
    while(argument[0] == '-') {
        const char *space = strchr(argument, ' ');
        if(!space) return "";
        argument = space + 1;
        while(*argument == ' ') ++argument;
    }
    return argument;
}
bool kui_ftp_split_pattern(const char *path, char folder[KUI_FTP_LINE_CAP], char pattern[KUI_FILES_NAME_CAP]) {
    if(!path || !folder || !pattern) return false;
    folder[0] = pattern[0] = 0;
    const char *slash = strrchr(path, '/'), *leaf = slash ? slash + 1 : path;
    size_t n = strlen(leaf), before = (size_t)(leaf - path);
    if(!strpbrk(leaf, "*?") || n >= KUI_FILES_NAME_CAP || before >= KUI_FTP_LINE_CAP) return false;
    memcpy(pattern, leaf, n + 1u);
    memcpy(folder, path, before);
    folder[before] = 0;
    return true;
}
static unsigned char fold(unsigned char c) { return c >= 'A' && c <= 'Z' ? (unsigned char)(c + 32) : c; }
bool kui_ftp_glob(const char *pattern, const char *name) {
    if(!pattern || !name) return false;
    const char *star = NULL, *resume = NULL;
    while(*name) {
        if(*pattern == '*') { star = pattern++; resume = name; }
        else if(*pattern == '?' || (*pattern && fold((unsigned char)*pattern) == fold((unsigned char)*name))) {
            ++pattern;
            ++name;
        } else if(star) { pattern = star + 1; name = ++resume; }
        else return false;
    }
    while(*pattern == '*') ++pattern;
    return !*pattern;
}
static bool number(const char **text, unsigned limit, unsigned *value) {
    unsigned n = 0, digits = 0;
    while(**text >= '0' && **text <= '9') {
        n = n * 10u + (unsigned)(**text - '0');
        if(++digits > 5 || n > limit) return false;
        ++*text;
    }
    *value = n;
    return digits > 0;
}
bool kui_ftp_parse_port(const char *argument, uint8_t ip[4], uint16_t *port) {
    if(!argument || !ip || !port) return false;
    unsigned v[6];
    const char *at = argument;
    for(unsigned i = 0; i < 6; ++i) {
        if(!number(&at, 255, &v[i])) return false;
        if(i < 5 && *at++ != ',') return false;
    }
    if(*at) return false;
    for(unsigned i = 0; i < 4; ++i) ip[i] = (uint8_t)v[i];
    *port = (uint16_t)(v[4] << 8 | v[5]);
    return true;
}
bool kui_ftp_parse_eprt(const char *argument, uint8_t ip[4], uint16_t *port, bool *ipv6) {
    if(!argument || !ip || !port || !ipv6) return false;
    *ipv6 = false;
    char d = argument[0];
    /* The delimiter is any printable character other than a digit. */
    if(d < 33 || d > 126 || (d >= '0' && d <= '9')) return false;
    const char *at = argument + 1;
    unsigned family, value;
    if(!number(&at, 2, &family) || *at++ != d || !family) return false;
    if(family == 2) { *ipv6 = true; return false; }
    for(unsigned i = 0; i < 4; ++i) {
        if(!number(&at, 255, &value)) return false;
        ip[i] = (uint8_t)value;
        if(*at++ != (i < 3 ? '.' : d)) return false;
    }
    if(!number(&at, 65535, &value) || *at++ != d || *at || !value) return false;
    *port = (uint16_t)value;
    return true;
}
static bool date_parts(uint16_t date, uint16_t time, unsigned part[6]) {
    part[0] = 1980u + (date >> 9); part[1] = (date >> 5) & 15u; part[2] = date & 31u;
    part[3] = time >> 11; part[4] = (time >> 5) & 63u; part[5] = (time & 31u) * 2u;
    return date && part[1] >= 1 && part[1] <= 12 && part[2] >= 1 && part[3] < 24 && part[4] < 60 && part[5] < 60;
}
bool kui_ftp_timestamp(char out[15], uint16_t date, uint16_t time) {
    unsigned p[6];
    bool valid = date_parts(date, time, p);
    if(!valid) { p[0] = 1980; p[1] = p[2] = 1; p[3] = p[4] = p[5] = 0; }
    snprintf(out, 15, "%04u%02u%02u%02u%02u%02u", p[0] % 10000u, p[1] % 100u, p[2] % 100u, p[3] % 100u, p[4] % 100u,
        p[5] % 100u);
    return valid;
}
size_t kui_ftp_list_line(char *out, size_t cap, const char *name, uint64_t bytes, uint16_t date, uint16_t time,
                         bool directory, bool read_only, uint16_t today) {
    static const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if(!out || !name || !name[0]) return 0;
    unsigned p[6];
    if(!date_parts(date, time, p)) { p[0] = 1980; p[1] = p[2] = 1; p[3] = p[4] = 0; }
    char when[16];
    if(p[0] == 1980u + (today >> 9)) snprintf(when, sizeof(when), "%02u:%02u", p[3] % 100u, p[4] % 100u);
    else snprintf(when, sizeof(when), " %04u", p[0] % 10000u);
    const char *mode = directory ? (read_only ? "dr-xr-xr-x" : "drwxr-xr-x") : (read_only ? "-r--r--r--" : "-rw-r--r--");
    int n = snprintf(out, cap, "%s 1 kui kui %13llu %s %2u %5s %s\r\n", mode, directory ? 0ull : (unsigned long long)bytes,
        months[p[1] - 1u], p[2], when, name);
    return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}
size_t kui_ftp_mlsd_line(char *out, size_t cap, const char *name, uint64_t bytes, uint16_t date, uint16_t time,
                         bool directory) {
    if(!out || !name || !name[0]) return 0;
    char modify[15];
    kui_ftp_timestamp(modify, date, time);
    int n = directory ? snprintf(out, cap, "type=dir;modify=%s; %s\r\n", modify, name) :
        snprintf(out, cap, "type=file;size=%llu;modify=%s; %s\r\n", (unsigned long long)bytes, modify, name);
    return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}
