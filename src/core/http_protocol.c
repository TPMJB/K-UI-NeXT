/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/browser.h"
#include <stdio.h>
#include <string.h>

static int hex(unsigned char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool equal(const char *a, size_t n, const char *b) {
    if(strlen(b) != n) return false;
    for(size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)a[i];
        if(c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        if(c != (unsigned char)b[i]) return false;
    }
    return true;
}
static bool decode(char *out, size_t cap, const char *p, size_t n) {
    size_t used = 0;
    for(size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)p[i];
        if(c == '%') {
            if(i + 2 >= n || hex((unsigned char)p[i+1]) < 0 || hex((unsigned char)p[i+2]) < 0) return false;
            c = (unsigned char)(hex((unsigned char)p[i+1]) * 16 + hex((unsigned char)p[i+2]));
            i += 2;
            /* No encoded separators or second decoding pass. */
            if(c == '/' || c == '\\' || c == '%' || c == '?' || c == '#') return false;
        }
        if(c < 32 || c == 127 || c == '\\' || c == '#' || c == ':' || used + 1 >= cap) return false;
        out[used++] = (char)c;
    }
    out[used] = 0;
    return used && out[0] == '/';
}
int kui_browser_parse(const char *data, size_t bytes, struct kui_browser_request *req) {
    if(!data || !req || bytes > KUI_BROWSER_REQUEST_CAP) return 431;
    size_t end = 0;
    for(size_t i = 0; i < bytes; ++i) {
        unsigned char c = (unsigned char)data[i];
        if(!c || c == 127 || (c < 32 && c != '\r' && c != '\n' && c != '\t')) return 400;
        if(c == '\r' && i + 1 < bytes && data[i+1] != '\n') return 400;
        if(c == '\n' && (!i || data[i-1] != '\r')) return 400;
        if(i >= 3 && !memcmp(data + i - 3, "\r\n\r\n", 4)) { end = i + 1; break; }
    }
    if(!end) return bytes == KUI_BROWSER_REQUEST_CAP ? 431 : 0;
    if(end != bytes) return 400; /* no request body or pipelining */
    memset(req, 0, sizeof(*req));
    size_t line = 0;
    while(line + 1 < end && !(data[line] == '\r' && data[line+1] == '\n')) ++line;
    const char *space = memchr(data, ' ', line);
    if(!space) return 400;
    size_t method = (size_t)(space - data);
    if(method == 4 && !memcmp(data, "HEAD", 4)) req->head = true;
    else if(method != 3 || memcmp(data, "GET", 3)) return 405;
    const char *target = space + 1;
    size_t left = line - (size_t)(target - data);
    const char *version = memchr(target, ' ', left);
    if(!version || version == target) return 400;
    size_t url_n = (size_t)(version - target);
    ++version;
    if((size_t)(data + line - version) != 8 ||
       (memcmp(version, "HTTP/1.0", 8) && memcmp(version, "HTTP/1.1", 8))) return 400;
    const char *query = memchr(target, '?', url_n);
    if(query) {
        size_t n = url_n - (size_t)(query + 1 - target);
        const char *value = query + 1;
        if(n < 6 || memcmp(value, "page=", 5)) return 400;
        value += 5; n -= 5;
        if(n > 3) return 400;
        for(size_t i = 0; i < n; ++i) {
            if(value[i] < '0' || value[i] > '9') return 400;
            req->page = req->page * 10u + (unsigned)(value[i] - '0');
        }
        if(req->page > 127) return 400;
        url_n = (size_t)(query - target);
    }
    if(!decode(req->path, sizeof(req->path), target, url_n)) return 400;
    bool host = false, length = false;
    for(size_t at = line + 2; at + 2 < end;) {
        size_t stop = at;
        while(stop + 1 < end && !(data[stop] == '\r' && data[stop+1] == '\n')) ++stop;
        if(stop == at) break;
        if(data[at] == ' ' || data[at] == '\t') return 400;
        const char *colon = memchr(data + at, ':', stop - at);
        if(!colon || colon == data + at) return 400;
        for(const char *p = data + at; p < colon; ++p)
            if(!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                 (*p >= '0' && *p <= '9') || *p == '-')) return 400;
        const char *value = colon + 1, *tail = data + stop;
        while(value < tail && (*value == ' ' || *value == '\t')) ++value;
        while(tail > value && (tail[-1] == ' ' || tail[-1] == '\t')) --tail;
        size_t name_n = (size_t)(colon - (data + at));
        if(equal(data + at, name_n, "host")) {
            if(host || value == tail) return 400;
            host = true;
        } else if(equal(data + at, name_n, "content-length")) {
            if(length || tail - value != 1 || *value != '0') return 400;
            length = true;
        } else if(equal(data + at, name_n, "transfer-encoding") || equal(data + at, name_n, "expect")) return 400;
        at = stop + 2;
    }
    if(version[7] == '1' && !host) return 400;
    return 200;
}
bool kui_browser_path(const char *path, char out[KUI_FILES_PATH_CAP + 3u]) {
    static const struct { const char *url, *card; } roots[] = {
        {"/screenshots", KUI_BROWSER_SCREENSHOTS}, {"/reports", KUI_BROWSER_REPORTS},
        {"/saves", KUI_BROWSER_SAVES}
    };
    if(!path || !out) return false;
    size_t n = strlen(path);
    if(!n || n >= KUI_FILES_PATH_CAP || path[0] != '/') return false;
    char normal[KUI_FILES_PATH_CAP];
    memcpy(normal, path, n + 1);
    if(n > 1 && normal[n-1] == '/') normal[--n] = 0;
    if(!kui_files_path_valid(normal)) return false;
    unsigned depth = 0;
    for(const char *p = normal + 1; *p;) {
        const char *end = strchr(p, '/');
        size_t component = end ? (size_t)(end - p) : strlen(p);
        if(p[0] == '.' || (component >= 5 && equal(p + component - 5, 5, ".part")) ||
           (component >= 4 && equal(p + component - 4, 4, ".tmp")) ||
           (component >= 9 && equal(p + component - 9, 9, ".kui-part"))) return false;
        if(!end) break;
        if(++depth > 8) return false;
        p = end + 1;
    }
    for(size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
        size_t prefix = strlen(roots[i].url);
        if(strncmp(normal, roots[i].url, prefix) || (normal[prefix] && normal[prefix] != '/')) continue;
        int wrote = snprintf(out, KUI_FILES_PATH_CAP + 3u, "%s%s", roots[i].card, normal + prefix);
        return wrote > 0 && wrote < (int)(KUI_FILES_PATH_CAP + 3u);
    }
    return false;
}
bool kui_browser_export_name(const char *path) {
    const char *dot = strrchr(path, '.');
    if(!dot || strchr(dot, '/')) return false;
    const char *types;
    if(!strncmp(path, "/screenshots/", 13)) types = ".bmp .png .ppm";
    else if(!strncmp(path, "/reports/", 9)) types = ".txt .json .zip .log .md .bmp";
    else if(!strncmp(path, "/saves/", 7)) types = ".vms .vmi .dci .vmd .zip";
    else return false;
    size_t n = strlen(dot);
    for(const char *p = types; *p;) {
        const char *end = strchr(p, ' ');
        size_t k = end ? (size_t)(end - p) : strlen(p);
        if(k == n) {
            bool same = true;
            for(size_t i = 0; i < n; ++i) {
                unsigned char c = (unsigned char)dot[i];
                if(c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
                if(c != (unsigned char)p[i]) same = false;
            }
            if(same) return true;
        }
        if(!end) break;
        p = end + 1;
    }
    return false;
}
size_t kui_browser_html(char *out, size_t cap, const char *text) {
    size_t used = 0;
    if(!out || !cap || !text) return 0;
    for(const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        const char *escape = *p == '&' ? "&amp;" : *p == '<' ? "&lt;" : *p == '>' ? "&gt;" :
            *p == '"' ? "&quot;" : *p == '\'' ? "&#39;" : NULL;
        size_t n = escape ? strlen(escape) : 1;
        if(used + n >= cap) { out[0] = 0; return 0; }
        if(escape) memcpy(out + used, escape, n); else out[used] = (char)*p;
        used += n;
    }
    out[used] = 0;
    return used;
}
size_t kui_browser_url(char *out, size_t cap, const char *text) {
    static const char digits[] = "0123456789ABCDEF";
    size_t used = 0;
    if(!out || !cap || !text) return 0;
    for(const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        bool plain = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_' || *p == '.' || *p == '~' || *p == '/';
        size_t n = plain ? 1 : 3;
        if(used + n >= cap) { out[0] = 0; return 0; }
        if(plain) out[used++] = (char)*p;
        else { out[used++] = '%'; out[used++] = digits[*p >> 4]; out[used++] = digits[*p & 15]; }
    }
    out[used] = 0;
    return used;
}
