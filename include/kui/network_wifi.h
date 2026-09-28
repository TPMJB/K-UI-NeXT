/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NETWORK_WIFI_H
#define KUI_NETWORK_WIFI_H
#include "kui/apps.h"
#include "kui/net.h"
#include "kwhost.h"

/* K-UI's Wi-Fi board on the SCI port: a Seeed XIAO ESP32-C5 (or C6)
 * running firmware/kui-wifi, found only when asked for. The board runs
 * Wi-Fi, DHCP and TCP/IP itself; K-UI opens socket slots on it through the
 * link described in firmware/kui-wifi/PROTOCOL.md, using the portable host
 * side in firmware/kui-wifi/components/kwlink.
 *
 * The console's link to the board: one transfer at a time, paced by the
 * board's READY line. */
struct kui_wifi_bus {
    void *ctx;
    /* Waits up to KUI_WIFI_READY_MS for READY to change, then clocks `bytes`
     * out of `out` while filling `in`, with the board's chip select held.
     * *ready: READY changed (false: the wait ran out and the transfer went
     * anyway, as the protocol allows). False when the SCI port failed. */
    bool (*transfer)(void *ctx, const uint8_t *out, uint8_t *in, size_t bytes, bool *ready);
    uint64_t (*now_ms)(void *ctx);
    void (*pause)(void *ctx, unsigned ms);
};
/* levels: every chip select the board may be on, each at `speeds` clock
 * rates from the fastest down (level = select * speeds + rate). */
struct kui_wifi_port {
    const struct kui_wifi_bus *bus;
    unsigned levels, speeds;
    bool (*open)(unsigned level);
    void (*close)(void);
    const char *(*speed)(unsigned level); /* "12.5 MHz, select GPIO6" */
};
/* Provided by the platform: src/dreamcast/wifi_sci.c on the console. */
const struct kui_wifi_port *kui_wifi_console_port(void);

#define KUI_WIFI_READY_MS 20u
/* No valid frame from the board for this long: it has stopped answering. */
#define KUI_WIFI_SILENT_MS 3000u
/* Poll transfers that check a clock rate once the board has answered. */
#define KUI_WIFI_CHECK_POLLS 24u
/* Joining: the board's own retries decide sooner in most cases. */
#define KUI_WIFI_JOIN_MS 30000u
#define KUI_WIFI_SCAN_MS 15000u

/* A session is large (the link keeps 16 KB per socket slot): allocate it. */
struct kui_wifi_session {
    struct kwh host;
    struct kwh_bus link;
    const struct kui_wifi_port *port;
    unsigned level;
    /* answered: something on the port behaved like the board, even if it
     * never became usable (then `problem` says why). */
    bool open, found, answered;
    /* READY changes seen and waits that ran out, since the port opened. */
    uint32_t ready_changes, ready_timeouts;
    /* The last valid frame from the board: host.heard's count, and when. */
    uint32_t heard;
    uint64_t heard_ms;
    uint32_t bad; /* frames with a bad checksum at the chosen rate */
    char problem[96];
};
/* Opens the port at each chip select and rate until the board answers the
 * link's HELLO with clean frames (it tries slower rates on errors), and
 * leaves the port open at the best one. False with `problem` set when
 * none works (the port is closed again). */
bool kui_wifi_session_find(struct kui_wifi_session *s, const struct kui_wifi_port *port, kui_log_fn log);
/* One transfer. False with `problem` set once the board has not answered
 * for KUI_WIFI_SILENT_MS. */
bool kui_wifi_session_step(struct kui_wifi_session *s);
/* Work waits on the link: something to send or acknowledge, or the board
 * has something for us. */
bool kui_wifi_session_busy(const struct kui_wifi_session *s);
/* Asks for the Wi-Fi status and waits for it (host.wifi). */
bool kui_wifi_session_status(struct kui_wifi_session *s, kui_cancel_fn cancel);
/* Waits up to limit_ms for the board to be online with an address,
 * calling stage() with a line to show whenever the state changes. False
 * with `problem` set: stopped, no network set up, wrong password, not
 * found in time, or the board stopped answering. */
bool kui_wifi_session_online(struct kui_wifi_session *s, unsigned limit_ms, kui_cancel_fn cancel,
                             void (*stage)(void *ctx, const char *text), void *ctx);
/* A scan of the networks in range (host.scan, host.scan_count). */
bool kui_wifi_session_scan(struct kui_wifi_session *s, kui_cancel_fn cancel);
/* Joins a network and keeps it (the board rejoins it by itself), then
 * waits up to KUI_WIFI_JOIN_MS until it is online, refused or not found.
 * band: KWM_BAND_* (KWM_BAND_KEEP leaves the setting). True only online. */
bool kui_wifi_session_join(struct kui_wifi_session *s, const char *ssid, const char *password, uint8_t band,
                           kui_cancel_fn cancel, void (*stage)(void *ctx, const char *text), void *ctx);
/* Disconnects; forget: also drops the saved network. */
bool kui_wifi_session_leave(struct kui_wifi_session *s, bool forget, kui_cancel_fn cancel);
bool kui_wifi_session_band(struct kui_wifi_session *s, uint8_t band, kui_cancel_fn cancel);
/* The session's socket slots for the FTP server. */
void kui_wifi_session_sockets(struct kui_wifi_session *s, struct kui_net_sockets *out);
/* Closes every slot on the board, waits briefly for the board to take the
 * closes, and closes the port. */
void kui_wifi_session_end(struct kui_wifi_session *s);

/* Text for the board's Wi-Fi state, security codes and band modes
 * (src/core/wifi_text.c). */
const char *kui_wifi_state_text(uint8_t state);
const char *kui_wifi_security_text(uint8_t security);
const char *kui_wifi_band_text(uint8_t band_mode);
/* "ESP32-C5", "ESP32-C6" or "ESP32". */
const char *kui_wifi_chip_text(uint8_t chip);

/* The Network app: inspection (A) and the connection test (X) when no
 * BBA, LAN adapter or W5500 is found. False when the Wi-Fi board does not
 * answer (out is then untouched). */
bool kui_wifi_network_inspect(struct kui_app_status *out, kui_log_fn log, kui_cancel_fn cancel);
bool kui_wifi_network_test(struct kui_app_status *out, kui_log_fn log, kui_cancel_fn cancel,
                           kui_app_progress_fn progress);

/* The Wi-Fi page: the board, its status, and networks in range. */
#define KUI_WIFI_NETWORKS 24u
struct kui_wifi_network {
    char ssid[KWM_SSID_MAX + 1];
    uint8_t channel, security;
    int8_t rssi;
    bool five; /* 5 GHz */
};
enum kui_wifi_action { KUI_WIFI_REFRESH, KUI_WIFI_JOIN, KUI_WIFI_FORGET, KUI_WIFI_BAND };
struct kui_wifi_request {
    enum kui_wifi_action action;
    char ssid[KWM_SSID_MAX + 1], password[KWM_PASSWORD_MAX + 1];
    uint8_t band;
};
struct kui_wifi_view {
    bool found, working, failed;
    char message[128];
    char board[KUI_APP_LINE_CAP]; /* "XIAO ESP32-C5, firmware 0.1.0 (12.5 MHz)" */
    struct kwh_wifi wifi;
    bool scanned;
    struct kui_wifi_network networks[KUI_WIFI_NETWORKS];
    unsigned count;
};
typedef void (*kui_wifi_publish_fn)(const struct kui_wifi_view *view);
/* Finds the board, does what was asked (REFRESH: status and a scan), and
 * publishes the view as it goes. The session, which held the password on
 * its way out, is wiped afterwards; the caller wipes its request. Scans
 * list each network name once, strongest first. */
void kui_wifi_run(const struct kui_wifi_port *port, const struct kui_wifi_request *request, struct kui_wifi_view *out,
                  kui_log_fn log, kui_cancel_fn cancel, kui_wifi_publish_fn publish);
#endif
