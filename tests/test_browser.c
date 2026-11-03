/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/browser_service.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    uint8_t state;
    const char *input;
    size_t input_size, input_at, visible, room, output_size;
    unsigned listens, closes, disconnects, reads, opens, file_closes, dirs, dir_closes;
    unsigned dir_at, entries;
    uint64_t file_size, file_at;
    bool closed, fail_read, stuck_send, pending_ack, fail_close, bad_net;
    char output[200000];
} model;
static struct kui_browser_status status;
static uint64_t tick;
static void reset(void) {
    memset(&model, 0, sizeof(model));
    memset(&status, 0, sizeof(status));
    model.room = 19;
    model.file_size = 60013;
    model.entries = 14;
    tick = 1;
}
static bool listen_socket(void *ctx, unsigned s, uint16_t port) {
    assert(ctx == &model && s == 3 && port == 8080);
    ++model.listens;
    model.state = KUI_NET_LISTEN;
    return true;
}
static bool close_socket(void *ctx, unsigned s) {
    assert(ctx == &model && s == 3);
    ++model.closes;
    if(model.fail_close) return false;
    model.state = KUI_NET_CLOSED;
    return true;
}
static bool disconnect_socket(void *ctx, unsigned s) {
    assert(ctx == &model && s == 3);
    ++model.disconnects;
    model.state = KUI_NET_CLOSED;
    model.closed = true;
    return true;
}
static bool socket_state(void *ctx, unsigned s, uint8_t *state) {
    assert(ctx == &model && s == 3);
    *state = model.state;
    return !model.bad_net;
}
static bool received(void *ctx, unsigned s, size_t *bytes) {
    assert(ctx == &model && s == 3);
    *bytes = model.visible - model.input_at;
    return true;
}
static bool receive(void *ctx, unsigned s, void *data, size_t bytes) {
    assert(ctx == &model && s == 3 && bytes <= model.visible - model.input_at);
    memcpy(data, model.input + model.input_at, bytes);
    model.input_at += bytes;
    return true;
}
static bool room(void *ctx, unsigned s, size_t *bytes) {
    assert(ctx == &model && s == 3);
    *bytes = model.stuck_send ? 0 : model.room;
    return true;
}
static bool send_data(void *ctx, unsigned s, const void *data, size_t bytes) {
    assert(ctx == &model && s == 3 && bytes <= model.room);
    assert(model.output_size + bytes < sizeof(model.output));
    memcpy(model.output + model.output_size, data, bytes);
    model.output_size += bytes;
    model.output[model.output_size] = 0;
    return true;
}
static bool sent(void *ctx, unsigned s, bool *all) {
    assert(ctx == &model && s == 3);
    *all = !model.pending_ack;
    return true;
}
static const struct kui_net_sockets net = {
    .ctx = &model, .count = 8, .listen = listen_socket, .close = close_socket,
    .disconnect = disconnect_socket, .state = socket_state, .received = received,
    .receive = receive, .room = room, .send = send_data, .sent = sent
};
FRESULT f_stat(const TCHAR *path, FILINFO *info) {
    memset(info, 0, sizeof(*info));
    if(strstr(path, "missing")) return FR_NO_FILE;
    if(strstr(path, "hidden")) info->fattrib = AM_HID;
    if(!strrchr(path, '.') || strstr(path, "/support") == path + strlen(path) - 8)
        info->fattrib |= AM_DIR;
    info->fsize = (FSIZE_t)model.file_size;
    return FR_OK;
}
FRESULT f_open(FIL *file, const TCHAR *path, BYTE mode) {
    assert(mode == FA_READ && !strncmp(path, "0:/KUI/", 7));
    ++model.opens;
    model.file_at = 0;
    file->obj.objsize = (FSIZE_t)model.file_size;
    return FR_OK;
}
FRESULT f_read(FIL *file, void *data, UINT bytes, UINT *got) {
    (void)file;
    ++model.reads;
    if(model.fail_read) { *got = 0; return FR_DISK_ERR; }
    assert(bytes <= 4096 && bytes <= model.file_size - model.file_at);
    for(UINT i = 0; i < bytes; ++i) ((uint8_t *)data)[i] = (uint8_t)((model.file_at + i) * 37u);
    model.file_at += bytes;
    *got = bytes;
    return FR_OK;
}
FRESULT f_close(FIL *file) { (void)file; ++model.file_closes; return FR_OK; }
FRESULT f_opendir(DIR *dir, const TCHAR *path) {
    (void)dir;
    assert(!strncmp(path, "0:/KUI/", 7));
    ++model.dirs;
    model.dir_at = 0;
    return FR_OK;
}
FRESULT f_readdir(DIR *dir, FILINFO *info) {
    (void)dir;
    memset(info, 0, sizeof(*info));
    if(model.dir_at >= model.entries) return FR_OK;
    if(model.dir_at == 0) snprintf(info->fname, sizeof(info->fname), "Fish & Chips.bmp");
    else snprintf(info->fname, sizeof(info->fname), "KUI-%06u.bmp", model.dir_at);
    info->fsize = 321u + model.dir_at;
    ++model.dir_at;
    return FR_OK;
}
FRESULT f_closedir(DIR *dir) { (void)dir; ++model.dir_closes; return FR_OK; }

static struct kui_browser_service *begin(const char *request) {
    struct kui_browser_service *s = kui_browser_service_create(&net, 3, 8080, &status);
    assert(s && model.state == KUI_NET_LISTEN);
    model.input = request;
    model.input_size = strlen(request);
    model.state = KUI_NET_ESTABLISHED;
    return s;
}
static void step(struct kui_browser_service *s, size_t reveal) {
    if(model.visible < model.input_size) {
        model.visible += reveal;
        if(model.visible > model.input_size) model.visible = model.input_size;
    }
    (void)kui_browser_service_step(s, tick++);
    assert(!kui_browser_service_error(s));
}
static void complete(struct kui_browser_service *s) {
    for(unsigned i = 0; i < 15000 && !model.closed; ++i) step(s, 3);
    assert(model.closed && model.disconnects == 1);
}
static size_t body_offset(void) {
    const char *p = strstr(model.output, "\r\n\r\n");
    assert(p);
    return (size_t)(p + 4 - model.output);
}
static uint64_t content_length(void) {
    const char *p = strstr(model.output, "Content-Length: ");
    assert(p);
    return strtoull(p + 16, NULL, 10);
}
static void assert_response(unsigned code, bool head) {
    char prefix[32]; snprintf(prefix, sizeof(prefix), "HTTP/1.1 %u ", code);
    assert(!strncmp(model.output, prefix, strlen(prefix)));
    size_t offset = body_offset();
    assert(model.output_size == offset + (head ? 0u : content_length()));
    assert(strstr(model.output, "Connection: close\r\n"));
    assert(strstr(model.output, "X-Content-Type-Options: nosniff\r\n"));
}
static void protocol(void) {
    struct kui_browser_request req;
    const char *good = "GET /screenshots/Fish%20%26%20Chips.bmp HTTP/1.1\r\nHost: dreamcast\r\n\r\n";
    for(size_t i = 0; i < strlen(good); ++i) assert(kui_browser_parse(good, i, &req) == 0);
    assert(kui_browser_parse(good, strlen(good), &req) == 200);
    assert(!strcmp(req.path, "/screenshots/Fish & Chips.bmp"));
    const char *bad[] = {
        "POST / HTTP/1.1\r\nHost:x\r\n\r\n", "GET / HTTP/1.1\r\n\r\n",
        "GET / HTTP/1.1\r\nHost:x\r\nHost:y\r\n\r\n", "GET / HTTP/1.1\r\nHost:x\r\nContent-Length:1\r\n\r\nx",
        "GET / HTTP/1.1\r\nHost:x\r\nTransfer-Encoding:chunked\r\n\r\n", "GET / HTTP/1.1\nHost:x\n\n",
        "GET / HTTP/1.1\r\nHost:x\r\n folded:bad\r\n\r\n", "GET /?page=128 HTTP/1.0\r\n\r\n",
        "GET /?page=-1 HTTP/1.0\r\n\r\n", "GET /?q=1 HTTP/1.0\r\n\r\n",
        "GET /screenshots/%2Ffoo HTTP/1.0\r\n\r\n", "GET /screenshots/%5cfoo HTTP/1.0\r\n\r\n",
        "GET /screenshots/%252e%252e/foo HTTP/1.0\r\n\r\n", "GET /screenshots/a%00.bmp HTTP/1.0\r\n\r\n",
        "GET / HTTP/1.0\r\n\r\nGET / HTTP/1.0\r\n\r\n"
    };
    for(size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) assert(kui_browser_parse(bad[i], strlen(bad[i]), &req) >= 400);
    char overflow[KUI_BROWSER_REQUEST_CAP]; memset(overflow, 'a', sizeof(overflow));
    assert(kui_browser_parse(overflow, sizeof(overflow), &req) == 431);
    char path[KUI_FILES_PATH_CAP+3u];
    assert(kui_browser_path("/reports/session/report.txt", path));
    assert(!strcmp(path, KUI_BROWSER_REPORTS "/session/report.txt"));
    assert(kui_browser_path("/saves/Export.vmd", path));
    const char *deny[] = {"/", "/KUI/runtime.kui", "/screenshots-old/a.bmp", "/reports/../ftp-password.txt",
        "/saves/./card.vmd", "/saves//card.vmd", "/saves/.hidden.vmd", "/saves/a:b.vmd", "/saves/a\\b.vmd",
        "/reports/REPORT-000001.part/log.txt", "/reports/REPORT-000001.PART/log.txt", "/screenshots/a.part"};
    for(size_t i=0; i<sizeof(deny)/sizeof(deny[0]); ++i) assert(!kui_browser_path(deny[i], path));
    assert(kui_browser_export_name("/screenshots/A.BMP"));
    assert(kui_browser_export_name("/reports/session/problem.json"));
    assert(kui_browser_export_name("/reports/REPORT-000001/screenshot.bmp"));
    assert(kui_browser_export_name("/saves/Game.VMD"));
    assert(!kui_browser_export_name("/saves/card.journal"));
    assert(!kui_browser_export_name("/screenshots/KUI-000001.part"));
    char escaped[100];
    assert(kui_browser_html(escaped, sizeof(escaped), "<&\"'>") && !strcmp(escaped, "&lt;&amp;&quot;&#39;&gt;"));
    assert(!kui_browser_html(escaped, 3, "&") && !escaped[0]);
}
static void html_and_head(void) {
    reset();
    struct kui_browser_service *s = begin("GET / HTTP/1.1\r\nHost:dc\r\n\r\n");
    complete(s); assert_response(200, false);
    uint64_t length = content_length();
    assert(strstr(model.output, "/screenshots/") && strstr(model.output, "/reports/") && strstr(model.output, "/saves/"));
    assert(!model.opens && !model.dirs);
    kui_browser_service_destroy(s);
    reset(); s = begin("HEAD / HTTP/1.0\r\n\r\n");
    complete(s); assert_response(200, true); assert(content_length() == length);
    kui_browser_service_destroy(s);
}
static void files(void) {
    reset(); model.room = 97;
    struct kui_browser_service *s = begin("GET /screenshots/KUI-000001.bmp HTTP/1.1\r\nHost:dc\r\n\r\n");
    complete(s); assert_response(200, false);
    assert(content_length() == model.file_size && model.opens == 1 && model.file_closes == 1);
    size_t offset = body_offset();
    for(size_t i = 0; i < model.file_size; ++i) assert((uint8_t)model.output[offset+i] == (uint8_t)(i * 37u));
    assert(status.downloads == 1 && status.requests == 1 && status.bytes_out == model.output_size);
    kui_browser_service_destroy(s);
    reset(); s = begin("HEAD /saves/Card.vmd HTTP/1.0\r\n\r\n");
    complete(s); assert_response(200, true); assert(!model.reads && model.file_closes == 1 && !status.downloads);
    kui_browser_service_destroy(s);
    reset(); model.file_size = 0; s = begin("GET /reports/empty.txt HTTP/1.0\r\n\r\n");
    complete(s); assert_response(200, false); assert(content_length() == 0);
    kui_browser_service_destroy(s);
}
static void listings_and_errors(void) {
    reset(); model.room = 1000;
    struct kui_browser_service *s = begin("GET /screenshots/ HTTP/1.0\r\n\r\n");
    complete(s); assert_response(200, false);
    assert(strstr(model.output, "Fish &amp; Chips.bmp") && strstr(model.output, "Fish%20%26%20Chips.bmp"));
    assert(strstr(model.output, "?page=1") && !strstr(model.output, "KUI-000006.bmp"));
    assert(model.dirs == 1 && model.dir_closes == 1);
    kui_browser_service_destroy(s);
    reset(); s = begin("GET /screenshots/?page=1 HTTP/1.0\r\n\r\n");
    complete(s); assert_response(200, false);
    assert(strstr(model.output, "KUI-000006.bmp") && !strstr(model.output, "Fish &amp;"));
    kui_browser_service_destroy(s);
    const char *requests[] = {
        "GET /screenshots/../ftp-password.txt HTTP/1.0\r\n\r\n",
        "GET /saves/card.journal HTTP/1.0\r\n\r\n",
        "GET /KUI/runtime.kui HTTP/1.0\r\n\r\n",
        "GET /screenshots/missing.bmp HTTP/1.0\r\n\r\n",
        "DELETE /saves/Card.vmd HTTP/1.0\r\n\r\n"
    };
    const unsigned codes[] = {403,403,403,404,405};
    for(unsigned i=0;i<5;++i) {
        reset(); s=begin(requests[i]); complete(s); assert_response(codes[i], false);
        assert(!model.opens && !model.dirs && status.failures == 1);
        kui_browser_service_destroy(s);
    }
}
static void interrupted(void) {
    reset(); model.entries=1024;
    struct kui_browser_service *s=begin("GET /screenshots/?page=127 HTTP/1.0\r\n\r\n");
    step(s,512); assert(model.dirs==1 && !model.dir_closes);
    step(s,0); assert(model.dir_at<=8); /* one bounded directory pass */
    kui_browser_service_destroy(s);
    assert(model.dir_closes==1);
    reset(); model.room=1000;
    s=begin("GET /screenshots/a.bmp HTTP/1.0\r\n\r\n");
    while(!model.reads) step(s,512);
    assert(model.opens == 1 && !model.file_closes);
    /* Cancel while downloading: destroy owns and closes the export. */
    kui_browser_service_destroy(s);
    assert(model.file_closes == 1 && model.closes == 1 && !status.downloads);
    reset(); s=begin("GET /screenshots/a.bmp HTTP/1.0\r\n\r\n");
    while(!model.reads) step(s,512);
    model.state=KUI_NET_CLOSED; step(s,0);
    assert(model.file_closes==1 && !status.downloads);
    kui_browser_service_destroy(s);
    reset(); s=begin("GET / HTTP/1.0\r\n\r\n");
    model.visible=4; step(s,0); tick+=15000; step(s,0); step(s,0);
    assert(model.closes && !model.output_size);
    kui_browser_service_destroy(s);
    reset(); model.pending_ack=true; model.room=1024;
    s=begin("GET /saves/Card.vmd HTTP/1.0\r\n\r\n");
    for(unsigned i=0;i<1000;++i) step(s,512);
    assert(!model.disconnects && !status.downloads);
    model.pending_ack=false; complete(s); assert(status.downloads==1);
    kui_browser_service_destroy(s);
    reset(); model.fail_read=true; s=begin("GET /saves/Card.vmd HTTP/1.0\r\n\r\n");
    for(unsigned i=0;i<500 && !model.file_closes;++i) step(s,512);
    assert(model.file_closes==1 && status.failures==1 && !status.downloads);
    kui_browser_service_destroy(s);
}
int main(void) {
    protocol(); html_and_head(); files(); listings_and_errors(); interrupted();
    puts("browser: partial HTTP, export allowlist, escaping, content lengths, pagination, cancellation and disconnects passed");
    return 0;
}
