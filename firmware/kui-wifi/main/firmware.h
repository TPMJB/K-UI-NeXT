/* SPDX-License-Identifier: MIT */
/* The ESP-IDF side of the K-UI Wi-Fi firmware: Wi-Fi, the SPI link, the
 * USB console and the bridge core's platform. */
#ifndef KUI_WIFI_FIRMWARE_H
#define KUI_WIFI_FIRMWARE_H
#include "bridge.h"
#include <stdbool.h>
#include <stdint.h>

#define TAG "kui-wifi"

/* Wi-Fi (wifi.c). Safe from any task. */
void wifi_start(struct kwb *bridge);
void wifi_status(struct kwb_wifi *out);
bool wifi_scan(void);
size_t wifi_scan_results(struct kwb_ap *out, size_t max);
/* Waits for the scan started by wifi_scan; false on timeout. */
bool wifi_scan_wait(uint32_t ms);
uint8_t wifi_join(const char *ssid, const char *password, bool save, uint8_t band);
void wifi_leave(bool forget);
/* KWM_BAND_24, KWM_BAND_5 or KWM_BAND_BOTH; kept across restarts. */
bool wifi_set_band(uint8_t band);
uint8_t wifi_band(void);
/* XIAO ESP32-C6 only: the U.FL antenna (true) or the ceramic one. */
bool wifi_set_antenna(bool external);
bool wifi_external_antenna(void);
bool wifi_time(uint64_t *unix_ms);

/* The platform the bridge core runs on (esp_platform.c). */
const struct kwb_platform *platform_start(struct kwb *bridge);
void platform_version(char out[33]);

/* The SPI link to the Dreamcast (link_spi.c). */
struct link_stats {
    uint32_t transfers, resets;
    bool host_seen;
};
/* True once the link has armed its first transfer. */
bool link_start(struct kwb *bridge);
void link_stats(struct link_stats *out);

/* The USB console (console.c). */
void console_start(struct kwb *bridge);
#endif
