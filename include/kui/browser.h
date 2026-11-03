/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_BROWSER_H
#define KUI_BROWSER_H
#include "kui/apps.h"
#include "kui/net.h"
#include "kui/files.h"
#include <stddef.h>
#include <stdint.h>

#define KUI_BROWSER_PORT 8080u
#define KUI_BROWSER_REQUEST_CAP 2048u
#define KUI_BROWSER_URL_CAP 64u
#define KUI_BROWSER_SCREENSHOTS "0:/KUI/screenshots"
#define KUI_BROWSER_REPORTS "0:/KUI/reports/support"
#define KUI_BROWSER_SAVES "0:/KUI/exports/saves"
enum kui_browser_state { KUI_BROWSER_STARTING, KUI_BROWSER_READY,
    KUI_BROWSER_STOPPED, KUI_BROWSER_FAILED };
struct kui_browser_options { uint16_t port; /* zero: 8080 */ };
struct kui_browser_status {
    enum kui_browser_state state;
    char message[128], adapter[KUI_APP_LINE_CAP], url[KUI_BROWSER_URL_CAP];
    uint8_t ip[4];
    uint16_t port;
    bool wifi, link;
    unsigned requests, downloads, failures;
    uint64_t bytes_out;
};
typedef void (*kui_browser_publish_fn)(const struct kui_browser_status *status);
/* Explicitly started on the sole storage worker, until cancel() says stop.
 * Reuses FTP's adapter/lease gateway, with no FTP listeners or password file.
 * FTP and this service are exclusive; both refuse a nested gateway owner.
 * Read-only exports only; HTTP is off until this function is called. */
void kui_browser_run(const struct kui_net_ports *ports,
    const struct kui_browser_options *options, struct kui_browser_status *out,
    kui_log_fn log, kui_cancel_fn cancel, kui_browser_publish_fn publish);

/* Pure bounded HTTP parser/path policy, also used by the real service.
 * parse: 0 incomplete; 200 complete; otherwise the HTTP error status.
 * Only GET/HEAD, HTTP/1.0/1.1, no bodies or transfer encoding. */
struct kui_browser_request {
    bool head;
    unsigned page;
    char path[KUI_FILES_PATH_CAP]; /* decoded, absolute URL path */
};
int kui_browser_parse(const char *data, size_t bytes,
    struct kui_browser_request *request);
/* Only /screenshots, /reports and /saves and safe descendants. A NULL/empty
 * suffix is not an export. Directories may be traversed within those roots;
 * only explicitly supported export suffixes may be downloaded. */
bool kui_browser_path(const char *url_path, char card_path[KUI_FILES_PATH_CAP + 3u]);
bool kui_browser_export_name(const char *url_path);
size_t kui_browser_html(char *out, size_t cap, const char *text);
size_t kui_browser_url(char *out, size_t cap, const char *text);
#endif
