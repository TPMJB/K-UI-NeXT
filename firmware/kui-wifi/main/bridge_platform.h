/* SPDX-License-Identifier: MIT */
/* What the bridge core needs from the board: Wi-Fi, name lookup, the clock,
 * firmware updates. esp_platform.c provides it on the ESP32; the host tests
 * provide a stand-in. Sockets are plain BSD sockets on both. */
#ifndef KUI_WIFI_BRIDGE_PLATFORM_H
#define KUI_WIFI_BRIDGE_PLATFORM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct kwb_info {
    uint8_t mac[6];
    uint8_t chip; /* 5 or 6 */
    char version[33];
};
struct kwb_wifi {
    uint8_t state, band, channel;
    int8_t rssi;
    uint8_t ip[4], mask[4], gateway[4], dns[4], bssid[6];
    uint8_t band_mode, reason, saved;
    char ssid[33];
};
struct kwb_ap {
    char ssid[33];
    uint8_t bssid[6], channel, auth;
    int8_t rssi;
};
struct kwb_platform {
    void *ctx;
    uint32_t (*now_ms)(void *ctx);
    void (*info)(void *ctx, struct kwb_info *out);
    void (*wifi_status)(void *ctx, struct kwb_wifi *out);
    /* Starts a scan; the board calls kwb_notify(KWB_NOTE_SCAN) when done. */
    bool (*wifi_scan)(void *ctx);
    size_t (*wifi_scan_results)(void *ctx, struct kwb_ap *out, size_t max);
    /* 0 when accepted; progress arrives as KWB_NOTE_WIFI. */
    uint8_t (*wifi_join)(void *ctx, const char *ssid, const char *password, bool save, uint8_t band_mode);
    void (*wifi_leave)(void *ctx, bool forget);
    /* Starts a lookup; KWB_NOTE_DNS when done, then dns_result (false: none). */
    bool (*dns_start)(void *ctx, const char *name);
    bool (*dns_result)(void *ctx, uint8_t ip[4]);
    /* False until the clock has been set from the network. */
    bool (*time_now)(void *ctx, uint64_t *unix_ms);
    /* Firmware update: 0 for success, otherwise an error code. */
    uint8_t (*ota_begin)(void *ctx, uint32_t size, const uint8_t sha256[32]);
    uint8_t (*ota_write)(void *ctx, uint32_t offset, const uint8_t *data, size_t len);
    uint8_t (*ota_end)(void *ctx);
    void (*reboot)(void *ctx);
};
#endif
