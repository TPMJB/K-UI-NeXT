/* SPDX-License-Identifier: GPL-3.0-only */
#define _DEFAULT_SOURCE
#include "wifi_model.h"
#include "bridge.h"
#include "kui/hash.h"
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
    uint8_t chip;
    char version[33], next_version[33];
    uint8_t *ota;
    struct wifi_model_ota_faults faults;
    struct wifi_model_ota_result ota_result;
    uint64_t boot_until;
    bool reset_pending, corrupted_frame;
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
    out->chip = m.chip;
    snprintf(out->version, sizeof(out->version), "%s", m.version);
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
    if(m.ota_result.open) ++m.ota_result.aborts;
    m.ota_result.open = false;
    free(m.ota);
    m.ota = NULL;
    if(!size || size > 0x1e0000u) return 1;
    ++m.ota_result.begins;
    if(m.faults.begin_status) return m.faults.begin_status;
    m.ota = malloc(size);
    if(!m.ota) return 2;
    m.ota_result.size = size;
    m.ota_result.written = 0;
    m.ota_result.committed = m.ota_result.checksum_ok = false;
    memcpy(m.ota_result.expected_sha, sha256, 32);
    m.ota_result.open = true;
    return 0;
}
static uint8_t pf_ota_write(void *ctx, uint32_t offset, const uint8_t *data, size_t len) {
    (void)ctx;
    ++m.ota_result.writes;
    if(!m.ota_result.open || offset != m.ota_result.written || len > m.ota_result.size - offset) return 1;
    if(m.faults.data_status) return m.faults.data_status;
    memcpy(m.ota + offset, data, len);
    if(m.faults.corrupt_image && !offset && len) m.ota[0] ^= 1;
    m.ota_result.written += (uint32_t)len;
    if(m.faults.reset_after && m.ota_result.written >= m.faults.reset_after) {
        m.reset_pending = true;
        m.faults.reset_after = 0;
    }
    return 0;
}
static uint8_t pf_ota_end(void *ctx) {
    (void)ctx;
    ++m.ota_result.ends;
    bool complete = m.ota_result.open && m.ota_result.written == m.ota_result.size;
    m.ota_result.open = false;
    if(!complete) return 1;
    struct kui_sha256 hash;
    kui_sha256_init(&hash);
    kui_sha256_update(&hash, m.ota, m.ota_result.size);
    kui_sha256_digest(&hash, m.ota_result.actual_sha);
    m.ota_result.checksum_ok = !memcmp(m.ota_result.actual_sha, m.ota_result.expected_sha, 32);
    if(!m.ota_result.checksum_ok) return 3;
    if(m.faults.end_status) return m.faults.end_status;
    /* The ESP validates the image; the model checks the descriptor needed
     * for the next HELLO. Structural validation is exercised on the host. */
    if(m.ota_result.size < 208u || m.ota[0] != 0xe9 || kwl_get32(m.ota + 32) != 0xabcd5432u) return 4;
    char version[33], hash_text[65];
    memcpy(version, m.ota + 48, 32);
    version[32] = 0;
    kui_hex(m.ota + 176, 32, hash_text);
    snprintf(m.next_version, sizeof(m.next_version), "%.20s-%.8s", version, hash_text);
    m.ota_result.committed = true;
    return 0;
}
static void pf_reboot(void *ctx) {
    (void)ctx;
    ++m.ota_result.reboots;
    if(m.ota_result.committed && !m.faults.rollback) {
        snprintf(m.version, sizeof(m.version), "%s", m.faults.wrong_version ? "unrelated-ffffffff" : m.next_version);
    }
    wifi_model_restart();
    m.boot_until = clock_ms() + m.faults.reboot_silent_ms;
    if(m.faults.never_returns) m.options.absent = true;
}
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
    /* Mutate a valid application reply before the real link computes CRC.
     * This tests host reply checks, independently of transport protection. */
    for(size_t at = 0; at + KWM_HEADER <= m.bridge.reply_len;) {
        uint8_t *head = m.bridge.reply + at;
        size_t len = kwl_get16(head + 2);
        if(at + KWM_HEADER + len > m.bridge.reply_len) break;
        if(head[0] == KWM_OTA_R && len >= 8 && head[4] == KWM_OTA_END_PHASE && m.faults.lose_end_reply) {
            /* Flash/selection succeeded, but its application answer never
             * leaves the device. Valid link frames continue to arrive. */
            size_t bytes = KWM_HEADER + len;
            memmove(head, head + bytes, m.bridge.reply_len - at - bytes);
            m.bridge.reply_len -= bytes;
            continue;
        }
        if(head[0] == KWM_OTA_R && len >= 8 && head[4] == KWM_OTA_DATA_PHASE) {
            if(m.faults.wrong_phase) head[4] = KWM_OTA_BEGIN_PHASE;
            if(m.faults.wrong_written) kwl_put32(head + 8, kwl_get32(head + 8) + 1u);
        }
        at += KWM_HEADER + len;
    }
    size_t n = kwb_frame(&m.bridge, m.armed);
    memset(m.armed + n, 0, KWL_FRAME_MAX - n);
}
static bool transfer(void *ctx, const uint8_t *out, uint8_t *in, size_t len, bool *ready) {
    (void)ctx;
    assert(m.open && len % 4u == 0 && len >= KWL_HEADER && len <= KWL_FRAME_MAX);
    service();
    ++wifi_model_transfers;
    if(m.options.absent || clock_ms() < m.boot_until || m.level / WIFI_MODEL_RATES != m.options.select) {
        /* Nobody listens on this chip select. */
        memset(in, 0xff, len);
        *ready = false;
        ++wifi_model_unanswered;
        return true;
    }
    *ready = !m.options.no_ready;
    memcpy(in, m.armed, len);
    memcpy(m.in, out, len);
    if(m.faults.corrupt_frame_once && m.ota_result.written && !m.corrupted_frame) {
        in[12] ^= 1;
        m.corrupted_frame = true;
    }
    if(m.level % WIFI_MODEL_RATES < m.options.bad_rates) {
        /* Too fast for these wires: a checksum bit flips each way. */
        in[12 + rnd() % 4u] ^= (uint8_t)(1u << (rnd() % 8u));
        m.in[12 + rnd() % 4u] ^= (uint8_t)(1u << (rnd() % 8u));
    }
    kwb_transfer(&m.bridge, m.in, len);
    if(m.reset_pending) {
        m.reset_pending = false;
        wifi_model_restart();
    }
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
    m.chip = 5;
    snprintf(m.version, sizeof(m.version), "model-1");
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
void wifi_model_stop(void) { kwb_release(&m.bridge); free(m.ota); m.ota = NULL; }
struct wifi_model_options *wifi_model_live(void) { return &m.options; }
bool wifi_model_listening(uint16_t port) {
    for(unsigned i = 0; i < KWM_SLOTS; ++i)
        if(m.bridge.listener[i].fd >= 0 && m.bridge.listener[i].port == port) return true;
    return false;
}
void wifi_model_restart(void) {
    m.ota_result.open = false;
    kwb_reset(&m.bridge);
    arm();
}
void wifi_model_ota_configure(const struct wifi_model_ota_faults *faults) {
    m.faults = faults ? *faults : (struct wifi_model_ota_faults){0};
}
const struct wifi_model_ota_result *wifi_model_ota_result(void) { return &m.ota_result; }
const uint8_t *wifi_model_ota_bytes(void) { return m.ota; }
void wifi_model_firmware(uint8_t chip, const char *version) {
    m.chip = chip;
    snprintf(m.version, sizeof(m.version), "%s", version);
}
void wifi_model_drop(void) {
    offline(KWM_WIFI_LOST);
    kwb_notify(&m.bridge, KWB_NOTE_WIFI);
}
void wifi_model_recover(const uint8_t ip[4]) {
    online(36, -50, ip);
    kwb_notify(&m.bridge, KWB_NOTE_WIFI);
}
