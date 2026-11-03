/* SPDX-License-Identifier: GPL-3.0-only */
/* Native read-only HTTP service, written for K-UI. The adapter gateway in
 * ftp_server.c owns the SCI bus, filesystem and event loop. */
#include "kui/browser_service.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_ROWS 6u
#define SCAN_LIMIT 1024u
#define BODY_CAP 16384u
#define FILE_BUFFER 4096u
#define REQUEST_MS 15000u
#define STALL_MS 60000u
#define DRAIN_MS 10000u
enum browser_phase { B_LISTEN, B_REQUEST, B_LISTING, B_HEADERS, B_BODY, B_FILE, B_DRAIN, B_FIN, B_CLOSE };
struct kui_browser_service {
    struct kui_net_sockets net;
    unsigned socket;
    uint16_t port;
    struct kui_browser_status *status;
    enum browser_phase phase;
    struct kui_browser_request request;
    char incoming[KUI_BROWSER_REQUEST_CAP];
    size_t received;
    char headers[512], body[BODY_CAP];
    size_t header_bytes, header_sent, body_bytes, body_sent;
    uint8_t buffer[FILE_BUFFER];
    size_t buffered, sent;
    uint64_t remaining, progress_ms, opened_ms;
    FIL file;
    DIR directory;
    bool file_open, dir_open, download, initialized;
    unsigned scanned, eligible, rows;
    const char *error;
};
static const char page_start[] =
    "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>K-UI SD card</title><style>"
    "body{font:17px system-ui,sans-serif;background:#111821;color:#eaf1f7;max-width:820px;margin:36px auto;padding:0 22px}"
    "a{color:#77d8ee}header{color:#ffad52;font-weight:750;letter-spacing:.12em}"
    "li{padding:12px 0;border-bottom:1px solid #33404c}small,p{color:#b6c4d0}"
    "nav{margin:24px 0}ul{padding-left:20px}</style><header>K-UI</header>";

static bool add(struct kui_browser_service *s, const char *format, ...) {
    if(s->body_bytes >= sizeof(s->body)) return false;
    va_list args;
    va_start(args, format);
    int n = vsnprintf(s->body + s->body_bytes, sizeof(s->body) - s->body_bytes, format, args);
    va_end(args);
    if(n < 0 || (size_t)n >= sizeof(s->body) - s->body_bytes) return false;
    s->body_bytes += (size_t)n;
    return true;
}
static void close_storage(struct kui_browser_service *s) {
    if(s->file_open) { (void)f_close(&s->file); s->file_open = false; }
    if(s->dir_open) { (void)f_closedir(&s->directory); s->dir_open = false; }
}
static void abort_connection(struct kui_browser_service *s, uint64_t now) {
    close_storage(s);
    s->phase = B_CLOSE;
    s->progress_ms = now;
}
static const char *reason(unsigned code) {
    switch(code) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 431: return "Request Header Fields Too Large";
    default: return "Internal Server Error";
    }
}
static void response(struct kui_browser_service *s, unsigned code, uint64_t bytes, const char *type) {
    int n = snprintf(s->headers, sizeof(s->headers),
        "HTTP/1.1 %u %s\r\nContent-Length: %llu\r\nContent-Type: %s\r\n"
        "Connection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
        "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; base-uri 'none'; frame-ancestors 'none'\r\n"
        "%s%s\r\n", code, reason(code), (unsigned long long)bytes, type,
        s->download ? "Content-Disposition: attachment\r\n" : "",
        code == 405 ? "Allow: GET, HEAD\r\n" : "");
    if(n < 0 || (size_t)n >= sizeof(s->headers)) { s->error = "Cannot prepare browser response"; return; }
    s->header_bytes = (size_t)n;
    s->header_sent = 0;
    s->phase = B_HEADERS;
}
static void error_response(struct kui_browser_service *s, unsigned code, const char *text) {
    close_storage(s);
    s->download = false;
    s->body_bytes = s->body_sent = 0;
    (void)add(s, "%s<h1>%s</h1><p>%s</p><nav><a href=\"/\">Home</a></nav></html>",
        page_start, reason(code), text);
    ++s->status->failures;
    response(s, code, s->body_bytes, "text/html; charset=utf-8");
}
static const char *category(const char *path) {
    if(!strncmp(path, "/screenshots", 12)) return "Screenshots";
    if(!strncmp(path, "/reports", 8)) return "Problem reports";
    return "Save exports";
}
static bool begin_listing(struct kui_browser_service *s, const char *path) {
    FRESULT result = f_opendir(&s->directory, path);
    if(result != FR_OK) {
        error_response(s, result == FR_NO_FILE || result == FR_NO_PATH ? 404 : 500,
            result == FR_NO_FILE || result == FR_NO_PATH ? "No exports in this folder yet." : "The folder could not be read.");
        return true;
    }
    s->dir_open = true;
    s->scanned = s->eligible = s->rows = 0;
    s->body_bytes = 0;
    if(!add(s, "%s<h1>%s</h1><nav><a href=\"/\">Home</a></nav><ul>", page_start, category(s->request.path))) return false;
    s->phase = B_LISTING;
    return true;
}
static void route(struct kui_browser_service *s) {
    if(!strcmp(s->request.path, "/")) {
        s->body_bytes = 0;
        (void)add(s, "%s<h1>Your SD card exports</h1><p>Download a screenshot, problem report or exported save.</p>"
            "<ul><li><a href=\"/screenshots/\">Screenshots</a></li>"
            "<li><a href=\"/reports/\">Problem reports</a></li>"
            "<li><a href=\"/saves/\">Save exports</a></li></ul>"
            "<p>Leave this screen open on your Dreamcast. Stop sharing with B.</p></html>", page_start);
        response(s, 200, s->body_bytes, "text/html; charset=utf-8");
        return;
    }
    char path[KUI_FILES_PATH_CAP + 3u];
    if(!kui_browser_path(s->request.path, path)) { error_response(s, 403, "This address is outside the shared exports."); return; }
    FILINFO info;
    FRESULT result = f_stat(path, &info);
    if(result != FR_OK) {
        error_response(s, result == FR_NO_FILE || result == FR_NO_PATH ? 404 : 500,
            result == FR_NO_FILE || result == FR_NO_PATH ? "This export does not exist yet." : "This export could not be read.");
        return;
    }
    if(info.fattrib & (AM_HID | AM_SYS)) { error_response(s, 403, "This export is not shared."); return; }
    if(info.fattrib & AM_DIR) {
        if(!begin_listing(s, path)) error_response(s, 500, "This folder page is too large.");
        return;
    }
    size_t n = strlen(s->request.path);
    if(s->request.page || s->request.path[n-1] == '/' || !kui_browser_export_name(s->request.path)) {
        error_response(s, 403, "This file is not a supported export."); return;
    }
    result = f_open(&s->file, path, FA_READ);
    if(result != FR_OK) { error_response(s, 500, "This export could not be opened."); return; }
    s->file_open = true;
    s->download = true;
    s->remaining = f_size(&s->file);
    response(s, 200, s->remaining, "application/octet-stream");
}
static void finish_listing(struct kui_browser_service *s, bool more) {
    if(s->dir_open) { (void)f_closedir(&s->directory); s->dir_open = false; }
    char url[KUI_FILES_PATH_CAP * 3u];
    if(!kui_browser_url(url, sizeof(url), s->request.path)) { error_response(s, 500, "This folder path is too long."); return; }
    bool ok = add(s, "</ul>%s<nav>", s->rows ? "" : "<p>No exports on this page.</p>");
    if(s->request.page) ok = ok && add(s, "<a href=\"%s?page=%u\">Previous</a> ", url, s->request.page - 1);
    if(more && s->request.page < 127) ok = ok && add(s, "<a href=\"%s?page=%u\">Next</a>", url, s->request.page + 1);
    ok = ok && add(s, "</nav></html>");
    if(!ok) { error_response(s, 500, "This folder page is too large."); return; }
    response(s, 200, s->body_bytes, "text/html; charset=utf-8");
}
/* At most eight directory reads on one gateway pass; a large export folder
 * cannot monopolize the worker or defer user cancellation indefinitely. */
static void listing_step(struct kui_browser_service *s) {
    for(unsigned pass = 0; pass < 8; ++pass) {
        FILINFO info;
        FRESULT result = f_readdir(&s->directory, &info);
        if(result != FR_OK) { error_response(s, 500, "This folder could not be read."); return; }
        if(!info.fname[0]) { finish_listing(s, false); return; }
        if(++s->scanned > SCAN_LIMIT) { finish_listing(s, false); return; }
        if(info.fname[0] == '.' || (info.fattrib & (AM_HID | AM_SYS))) continue;
        char child[KUI_FILES_PATH_CAP], card[KUI_FILES_PATH_CAP + 3u];
        size_t n = strlen(s->request.path);
        int wrote = snprintf(child, sizeof(child), "%s%s%s", s->request.path,
            n && s->request.path[n-1] == '/' ? "" : "/", info.fname);
        if(wrote < 0 || (size_t)wrote >= sizeof(child) || !kui_browser_path(child, card) ||
           (!(info.fattrib & AM_DIR) && !kui_browser_export_name(child))) continue;
        if(s->eligible++ < s->request.page * PAGE_ROWS) continue;
        if(s->rows == PAGE_ROWS) { finish_listing(s, true); return; }
        char name[KUI_FILES_NAME_CAP * 6u], url[KUI_FILES_PATH_CAP * 3u];
        if(!kui_browser_html(name, sizeof(name), info.fname) || !kui_browser_url(url, sizeof(url), child)) continue;
        bool ok;
        if(info.fattrib & AM_DIR) ok = add(s, "<li><a href=\"%s/\">%s/</a></li>", url, name);
        else ok = add(s, "<li><a href=\"%s\" download>%s</a> <small>%llu bytes</small></li>",
            url, name, (unsigned long long)info.fsize);
        if(!ok) { error_response(s, 500, "This folder page is too large."); return; }
        ++s->rows;
    }
}
struct kui_browser_service *kui_browser_service_create(const struct kui_net_sockets *net,
    unsigned socket, uint16_t port, struct kui_browser_status *status) {
    if(!net || !status || socket >= net->count || !net->listen || !net->close ||
       !net->disconnect || !net->state || !net->received || !net->receive ||
       !net->room || !net->send || !net->sent || !port) return NULL;
    struct kui_browser_service *s = calloc(1, sizeof(*s));
    if(!s) return NULL;
    s->net = *net;
    s->socket = socket;
    s->port = port;
    s->status = status;
    if(!net->listen(net->ctx, socket, port)) { free(s); return NULL; }
    return s;
}
bool kui_browser_service_step(struct kui_browser_service *s, uint64_t now) {
    if(!s || s->error) return false;
    if(!s->initialized) { s->progress_ms = now; s->initialized = true; }
    uint8_t state;
    if(!s->net.state(s->net.ctx, s->socket, &state)) { s->error = "Browser socket stopped answering"; return false; }
    if(s->phase == B_CLOSE) {
        if(s->net.close(s->net.ctx, s->socket)) {
            s->phase = B_LISTEN;
            s->progress_ms = now;
        } else if(now - s->progress_ms > DRAIN_MS) s->error = "Cannot close browser connection";
        return true;
    }
    if(s->phase == B_FIN) {
        if(state == KUI_NET_CLOSED) { s->phase = B_LISTEN; return true; }
        if(now - s->progress_ms >= DRAIN_MS) abort_connection(s, now);
        return false;
    }
    if(s->phase == B_LISTEN) {
        if(state == KUI_NET_CLOSED) {
            if(!s->net.listen(s->net.ctx, s->socket, s->port)) s->error = "Cannot listen for a browser";
            return true;
        }
        if(state != KUI_NET_ESTABLISHED && state != KUI_NET_PEER_CLOSED) return false;
        s->received = s->header_bytes = s->header_sent = s->body_bytes = s->body_sent = 0;
        s->remaining = s->buffered = s->sent = 0;
        s->download = false;
        memset(&s->request, 0, sizeof(s->request));
        s->phase = B_REQUEST;
        s->opened_ms = s->progress_ms = now;
    }
    if(state != KUI_NET_ESTABLISHED && state != KUI_NET_PEER_CLOSED) {
        abort_connection(s, now); return true;
    }
    if(s->phase == B_REQUEST) {
        size_t waiting = 0;
        if(!s->net.received(s->net.ctx, s->socket, &waiting)) { abort_connection(s, now); return true; }
        if(waiting) {
            size_t n = sizeof(s->incoming) - s->received;
            if(n > waiting) n = waiting;
            if(n > 512u) n = 512u;
            if(!s->net.receive(s->net.ctx, s->socket, s->incoming + s->received, n)) { abort_connection(s, now); return true; }
            s->received += n;
            s->progress_ms = now;
            int code = kui_browser_parse(s->incoming, s->received, &s->request);
            if(code) {
                ++s->status->requests;
                if(code == 200) route(s);
                else error_response(s, (unsigned)code, "Use a normal GET or HEAD request without a request body.");
            }
            return true;
        }
        if(state == KUI_NET_PEER_CLOSED || now - s->opened_ms >= REQUEST_MS) abort_connection(s, now);
        return false;
    }
    if(now - s->progress_ms >= (s->phase == B_DRAIN ? DRAIN_MS : STALL_MS)) { abort_connection(s, now); return true; }
    if(s->phase == B_LISTING) { listing_step(s); return true; }
    if(s->phase == B_DRAIN) {
        bool all = false;
        if(!s->net.sent(s->net.ctx, s->socket, &all)) { abort_connection(s, now); return true; }
        if(all && s->net.disconnect(s->net.ctx, s->socket)) {
            if(s->download && !s->request.head) ++s->status->downloads;
            close_storage(s);
            s->phase = B_FIN;
            s->progress_ms = now;
        }
        return all;
    }
    size_t room = 0;
    if(!s->net.room(s->net.ctx, s->socket, &room)) { abort_connection(s, now); return true; }
    if(!room) return false;
    const void *data = NULL;
    size_t n = 0;
    if(s->phase == B_HEADERS) {
        data = s->headers + s->header_sent;
        n = s->header_bytes - s->header_sent;
    } else if(s->phase == B_BODY) {
        data = s->body + s->body_sent;
        n = s->body_bytes - s->body_sent;
    } else if(s->phase == B_FILE) {
        if(s->sent == s->buffered && s->remaining) {
            UINT got = 0;
            size_t want = s->remaining < sizeof(s->buffer) ? (size_t)s->remaining : sizeof(s->buffer);
            if(f_read(&s->file, s->buffer, (UINT)want, &got) != FR_OK || got != want) {
                ++s->status->failures; abort_connection(s, now); return true;
            }
            s->buffered = got;
            s->sent = 0;
        }
        data = s->buffer + s->sent;
        n = s->buffered - s->sent;
    }
    if(n > room) n = room;
    if(n > 1024u) n = 1024u;
    if(n && !s->net.send(s->net.ctx, s->socket, data, n)) { abort_connection(s, now); return true; }
    if(n) { s->progress_ms = now; s->status->bytes_out += n; }
    if(s->phase == B_HEADERS) {
        s->header_sent += n;
        if(s->header_sent == s->header_bytes) {
            s->phase = s->request.head ? B_DRAIN : s->download ? B_FILE : B_BODY;
            if(s->request.head) close_storage(s);
        }
    } else if(s->phase == B_BODY) {
        s->body_sent += n;
        if(s->body_sent == s->body_bytes) s->phase = B_DRAIN;
    } else if(s->phase == B_FILE) {
        s->sent += n;
        s->remaining -= n;
        if(!s->remaining) { close_storage(s); s->phase = B_DRAIN; }
    }
    return n != 0;
}
const char *kui_browser_service_error(const struct kui_browser_service *s) {
    return s ? s->error : "Cannot allocate browser service";
}
void kui_browser_service_destroy(struct kui_browser_service *s) {
    if(!s) return;
    close_storage(s);
    (void)s->net.close(s->net.ctx, s->socket);
    free(s);
}
