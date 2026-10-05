/* SPDX-License-Identifier: GPL-3.0-only */
#define _DEFAULT_SOURCE
#include "wifi_model.h"
#include "bridge.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* As the firmware (main/main.c): each open slot's buffers. */
#define SLOT_BUFFER 8192u
/* Joining takes this long in the model. */
#define JOIN_MS 40u

static struct {
    struct wifi_model_options options;
    struct kwb bridge;
    bool open;
    unsigned level;
    uint8_t armed[KWL_FRAME_MAX], in[KWL_FRAME_MAX];
    struct kwb_wifi wifi;
    bool dns_found, dns_done;
    uint32_t dns_ticket;
    /* A join under way: the state it ends in, and when. */
    bool joining;
    uint8_t outcome, outcome_channel;
    int8_t outcome_rssi;
    uint64_t outcome_at;
    uint32_t rng;
} m;
struct wifi_model_join wifi_model_joined;
unsigned wifi_model_transfers, wifi_model_unanswered;
static const uint8_t localhost[4] = {127, 0, 0, 1};

static uint64_t clock_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}
static uint32_t rnd(void) {
    m.rng ^= m.rng << 13;
    m.rng ^= m.rng >> 17;
    m.rng ^= m.rng << 5;
    return m.rng;
}

/* ---- The board's Wi-Fi, faked ---- */
static const struct kwb_ap networks[] = {
    {"Home 5G", {2, 0, 0, 0, 0, 1}, 36, 3, -48},
    {"Home", {2, 0, 0, 0, 0, 2}, 6, 3, -61},
    {"Home", {2, 0, 0, 0, 0, 3}, 11, 3, -55},
    {"Cafe", {2, 0, 0, 0, 0, 4}, 1, 0, -80},
    {"", {2, 0, 0, 0, 0, 5}, 44, 3, -70},
};
static void online(uint8_t channel, int8_t rssi, const uint8_t ip[4]) {
    struct kwb_wifi *w = &m.wifi;
    w->state = KWM_WIFI_ONLINE;
    w->channel = channel;
    w->band = channel > 14 ? 5 : 2;
    w->rssi = rssi;
    memcpy(w->ip, ip, 4);
    static const uint8_t mask[4] = {255, 0, 0, 0};
    memcpy(w->mask, mask, 4);
    memcpy(w->gateway, localhost, 4);
    memcpy(w->dns, localhost, 4);
}
static void offline(uint8_t state) {
    m.wifi.state = state;
    m.wifi.channel = m.wifi.band = 0;
    memset(m.wifi.ip, 0, 4);
}
static uint32_t pf_now(void *ctx) { (void)ctx; return (uint32_t)clock_ms(); }
static void pf_info(void *ctx, struct kwb_info *out) {
    (void)ctx;
    static const uint8_t mac[6] = {0x02, 0x4b, 0x55, 0x49, 0x00, 0x05};
    memcpy(out->mac, mac, 6);
    out->chip = 5;
    snprintf(out->version, sizeof(out->version), "model-1");
}
static void pf_status(void *ctx, struct kwb_wifi *out) { (void)ctx; *out = m.wifi; }
static bool pf_scan(void *ctx) {
    (void)ctx;
    kwb_notify(&m.bridge, KWB_NOTE_SCAN);
    return true;
}
static size_t pf_scan_results(void *ctx, struct kwb_ap *out, size_t max) {
    (void)ctx;
    size_t n = sizeof(networks) / sizeof(networks[0]);
    if(n > max) n = max;
    memcpy(out, networks, n * sizeof(*out));
    return n;
}
static uint8_t pf_join(void *ctx, const char *ssid, const char *password, bool save, uint8_t band) {
    (void)ctx;
    size_t pass_len = strlen(password);
    ++wifi_model_joined.count;
    snprintf(wifi_model_joined.ssid, sizeof(wifi_model_joined.ssid), "%s", ssid);
    snprintf(wifi_model_joined.password, sizeof(wifi_model_joined.password), "%s", password);
    wifi_model_joined.band = band;
    wifi_model_joined.save = save;
    /* As the firmware: WPA passphrases are 8 to 63 characters, or 64 hex digits. */
    if(!ssid[0] || pass_len > KWM_PASSWORD_MAX || (pass_len && pass_len < 8)) return KWM_E_INVALID;
    if(band != KWM_BAND_KEEP) m.wifi.band_mode = band;
    offline(KWM_WIFI_CONNECTING);
    snprintf(m.wifi.ssid, sizeof(m.wifi.ssid), "%s", ssid);
    if(save) m.wifi.saved = 1;
    m.outcome = KWM_WIFI_NOT_FOUND;
    for(size_t i = 0; i < sizeof(networks) / sizeof(networks[0]); ++i) {
        const struct kwb_ap *ap = &networks[i];
        if(strcmp(ap->ssid, ssid)) continue;
        bool right = ap->auth ? !strcmp(password, WIFI_MODEL_PASSWORD) : !pass_len;
        m.outcome = right ? KWM_WIFI_ONLINE : KWM_WIFI_BAD_PASSWORD;
        m.outcome_channel = ap->channel;
        m.outcome_rssi = ap->rssi;
    }
    m.joining = true;
    m.outcome_at = clock_ms() + JOIN_MS;
    kwb_notify(&m.bridge, KWB_NOTE_WIFI);
    return 0;
}
static void pf_leave(void *ctx, bool forget) {
    (void)ctx;
    m.joining = false;
    offline(KWM_WIFI_IDLE);
    if(forget) {
        m.wifi.saved = 0;
        m.wifi.ssid[0] = 0;
    }
    kwb_notify(&m.bridge, KWB_NOTE_WIFI);
}
static bool pf_band(void *ctx, uint8_t band) {
    (void)ctx;
    if(band != KWM_BAND_24 && band != KWM_BAND_5 && band != KWM_BAND_BOTH) return false;
    m.wifi.band_mode = band;
    kwb_notify(&m.bridge, KWB_NOTE_WIFI);
    return true;
}
static bool pf_dns_start(void *ctx, uint32_t ticket, const char *name) {
    (void)ctx;
    m.dns_ticket = ticket;
    m.dns_done = true;
    m.dns_found = !strcmp(name, "localhost") || !strcmp(name, "pool.ntp.org");
    kwb_notify(&m.bridge, KWB_NOTE_DNS);
    return true;
}
static bool pf_dns_result(void *ctx, uint32_t *ticket, bool *found, uint8_t ip[4]) {
    (void)ctx;
    if(!m.dns_done) return false;
    m.dns_done = false;
    *ticket = m.dns_ticket;
    *found = m.dns_found;
    memcpy(ip, localhost, 4);
    return true;
}
static bool pf_time(void *ctx, uint64_t *ms) {
    (void)ctx;
    *ms = 1790000000123ull;
    return m.wifi.state == KWM_WIFI_ONLINE;
}
static uint8_t pf_ota_begin(void *ctx, uint32_t size, const uint8_t sha256[32]) {
    (void)ctx;
    (void)size;
    (void)sha256;
    return 2;
}
static uint8_t pf_ota_write(void *ctx, uint32_t offset, const uint8_t *data, size_t len) {
    (void)ctx;
    (void)offset;
    (void)data;
    (void)len;
    return 1;
}
static uint8_t pf_ota_end(void *ctx) { (void)ctx; return 1; }
static void pf_reboot(void *ctx) { (void)ctx; wifi_model_restart(); }
static const struct kwb_platform platform = {NULL, pf_now, pf_info, pf_status, pf_scan, pf_scan_results, pf_join,
    pf_leave, pf_band, pf_dns_start, pf_dns_result, pf_time, pf_ota_begin, pf_ota_write, pf_ota_end, pf_reboot};

/* ---- The bus ---- */
static void service(void) {
    if(m.joining && clock_ms() >= m.outcome_at) {
        m.joining = false;
        if(m.outcome == KWM_WIFI_ONLINE) online(m.outcome_channel, m.outcome_rssi, localhost);
        else offline(m.outcome);
        kwb_notify(&m.bridge, KWB_NOTE_WIFI);
    }
    kwb_service(&m.bridge);
}
/* As the firmware: the next transfer is armed as soon as one ends. */
static void arm(void) {
    size_t n = kwb_frame(&m.bridge, m.armed);
    memset(m.armed + n, 0, KWL_FRAME_MAX - n);
}
static bool transfer(void *ctx, const uint8_t *out, uint8_t *in, size_t len, bool *ready) {
    (void)ctx;
    assert(m.open && len % 4u == 0 && len >= KWL_HEADER && len <= KWL_FRAME_MAX);
    service();
    ++wifi_model_transfers;
    if(m.options.absent || m.level / WIFI_MODEL_RATES != m.options.select) {
        /* Nobody listens on this chip select. */
        memset(in, 0xff, len);
        *ready = false;
        ++wifi_model_unanswered;
        return true;
    }
    *ready = !m.options.no_ready;
    memcpy(in, m.armed, len);
    memcpy(m.in, out, len);
    if(m.level % WIFI_MODEL_RATES < m.options.bad_rates) {
        /* Too fast for these wires: a checksum bit flips each way. */
        in[12 + rnd() % 4u] ^= (uint8_t)(1u << (rnd() % 8u));
        m.in[12 + rnd() % 4u] ^= (uint8_t)(1u << (rnd() % 8u));
    }
    kwb_transfer(&m.bridge, m.in, len);
    arm();
    return true;
}
static uint64_t now_ms(void *ctx) { (void)ctx; return clock_ms(); }
static void pause_ms(void *ctx, unsigned ms) {
    (void)ctx;
    service();
    if(ms) usleep(ms * 1000u);
}
static const struct kui_wifi_bus bus = {.transfer = transfer, .now_ms = now_ms, .pause = pause_ms};
static bool open_level(unsigned level) {
    if(level >= WIFI_MODEL_SELECTS * WIFI_MODEL_RATES) return false;
    m.open = true;
    m.level = level;
    return true;
}
static void close_port(void) { m.open = false; }
static const char *speed(unsigned level) {
    static const char *const names[] = {"12.5 MHz, select GPIO6", "6.25 MHz, select GPIO6", "12.5 MHz, select GPIO7",
                                        "6.25 MHz, select GPIO7"};
    return level < sizeof(names) / sizeof(names[0]) ? names[level] : "?";
}
const struct kui_wifi_port wifi_model_port = {&bus, WIFI_MODEL_SELECTS * WIFI_MODEL_RATES, WIFI_MODEL_RATES, open_level,
    close_port, speed};

void wifi_model_start(const struct wifi_model_options *options) {
    memset(&m, 0, sizeof(m));
    memset(&wifi_model_joined, 0, sizeof(wifi_model_joined));
    wifi_model_transfers = wifi_model_unanswered = 0;
    m.options = *options;
    m.rng = 0x4b554957u;
    m.wifi.band_mode = KWM_BAND_BOTH;
    if(options->state == KWM_WIFI_ONLINE) {
        online(36, -48, localhost);
        snprintf(m.wifi.ssid, sizeof(m.wifi.ssid), "Home 5G");
        m.wifi.saved = 1;
    } else {
        offline(options->state);
        if(options->state != KWM_WIFI_IDLE) {
            snprintf(m.wifi.ssid, sizeof(m.wifi.ssid), "Home 5G");
            m.wifi.saved = 1;
        }
    }
    kwb_init(&m.bridge, &platform, SLOT_BUFFER);
    arm();
}
void wifi_model_stop(void) { kwb_release(&m.bridge); }
struct wifi_model_options *wifi_model_live(void) { return &m.options; }
bool wifi_model_listening(uint16_t port) {
    for(unsigned i = 0; i < KWM_SLOTS; ++i)
        if(m.bridge.listener[i].fd >= 0 && m.bridge.listener[i].port == port) return true;
    return false;
}
void wifi_model_restart(void) {
    kwb_reset(&m.bridge);
    arm();
}
void wifi_model_drop(void) {
    offline(KWM_WIFI_LOST);
    kwb_notify(&m.bridge, KWB_NOTE_WIFI);
}
void wifi_model_recover(const uint8_t ip[4]) {
    online(36, -50, ip);
    kwb_notify(&m.bridge, KWB_NOTE_WIFI);
}
