/* SPDX-License-Identifier: MIT */
/* The bridge core's platform on the ESP32: Wi-Fi from wifi.c, name lookups
 * on their own task, the network clock, and firmware updates into the
 * other OTA slot, checked against the SHA-256 the Dreamcast sends. */
#include "board.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "firmware.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

static struct kwb *bridge;

static uint32_t now_ms(void *ctx) {
    (void)ctx;
    return (uint32_t)(esp_timer_get_time() / 1000);
}
void platform_version(char out[33]) {
    const esp_app_desc_t *app = esp_app_get_description();
    char elf[9];
    esp_app_get_elf_sha256(elf, sizeof elf);
    snprintf(out, 33, "%.20s-%s", app->version, elf);
}
static void info(void *ctx, struct kwb_info *out) {
    (void)ctx;
    esp_read_mac(out->mac, ESP_MAC_WIFI_STA);
    out->chip = BOARD_CHIP;
    platform_version(out->version);
}
static void status(void *ctx, struct kwb_wifi *out) {
    (void)ctx;
    wifi_status(out);
}
static bool scan(void *ctx) {
    (void)ctx;
    return wifi_scan();
}
static size_t scan_results(void *ctx, struct kwb_ap *out, size_t max) {
    (void)ctx;
    return wifi_scan_results(out, max);
}
static uint8_t join(void *ctx, const char *ssid, const char *password, bool save, uint8_t band) {
    (void)ctx;
    return wifi_join(ssid, password, save, band);
}
static void leave(void *ctx, bool forget) {
    (void)ctx;
    wifi_leave(forget);
}
static bool band(void *ctx, uint8_t band_mode) {
    (void)ctx;
    return wifi_set_band(band_mode);
}

/* One lookup at a time (the bridge core sees to that). */
static QueueHandle_t lookups;
static char lookup_name[KWM_NAME_MAX + 1];
static uint8_t lookup_ip[4];
static volatile bool lookup_ok;
static void lookup_task(void *arg) {
    (void)arg;
    for(;;) {
        uint8_t token;
        if(xQueueReceive(lookups, &token, portMAX_DELAY) != pdTRUE) continue;
        struct addrinfo hints, *found = NULL;
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        bool ok = getaddrinfo(lookup_name, NULL, &hints, &found) == 0 && found;
        if(ok) memcpy(lookup_ip, &((struct sockaddr_in *)found->ai_addr)->sin_addr.s_addr, 4);
        if(found) freeaddrinfo(found);
        lookup_ok = ok;
        kwb_notify(bridge, KWB_NOTE_DNS);
    }
}
static bool dns_start(void *ctx, const char *name) {
    (void)ctx;
    uint8_t token = 1;
    snprintf(lookup_name, sizeof lookup_name, "%s", name);
    return xQueueSend(lookups, &token, 0) == pdTRUE;
}
static bool dns_result(void *ctx, uint8_t ip[4]) {
    (void)ctx;
    memcpy(ip, lookup_ip, 4);
    return lookup_ok;
}
static bool time_now(void *ctx, uint64_t *unix_ms) {
    (void)ctx;
    return wifi_time(unix_ms);
}

/* Firmware updates. Status codes: 1 out of order or too big, 2 flash
 * error, 3 checksum mismatch, 4 not a valid image, 5 could not select it. */
static esp_ota_handle_t ota;
static const esp_partition_t *ota_slot;
static mbedtls_sha256_context ota_sha;
static uint8_t ota_expected[32];
static uint32_t ota_size, ota_written;
static bool ota_open;
static void ota_close(void) {
    if(!ota_open) return;
    esp_ota_abort(ota);
    mbedtls_sha256_free(&ota_sha);
    ota_open = false;
}
static uint8_t ota_begin(void *ctx, uint32_t size, const uint8_t sha256[32]) {
    (void)ctx;
    ota_close();
    ota_slot = esp_ota_get_next_update_partition(NULL);
    if(!ota_slot || !size || size > ota_slot->size) return 1;
    /* Erase as the data arrives, so no single step blocks the link long. */
    if(esp_ota_begin(ota_slot, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) return 2;
    mbedtls_sha256_init(&ota_sha);
    mbedtls_sha256_starts(&ota_sha, 0);
    memcpy(ota_expected, sha256, 32);
    ota_size = size;
    ota_written = 0;
    ota_open = true;
    return 0;
}
static uint8_t ota_write(void *ctx, uint32_t offset, const uint8_t *data, size_t len) {
    (void)ctx;
    if(!ota_open || offset != ota_written || len > ota_size - ota_written) return 1;
    if(esp_ota_write(ota, data, len) != ESP_OK) {
        ota_close();
        return 2;
    }
    mbedtls_sha256_update(&ota_sha, data, len);
    ota_written += (uint32_t)len;
    return 0;
}
static uint8_t ota_end(void *ctx) {
    (void)ctx;
    if(!ota_open || ota_written != ota_size) {
        ota_close();
        return 1;
    }
    uint8_t digest[32];
    mbedtls_sha256_finish(&ota_sha, digest);
    mbedtls_sha256_free(&ota_sha);
    ota_open = false;
    if(memcmp(digest, ota_expected, sizeof digest)) {
        esp_ota_abort(ota);
        return 3;
    }
    if(esp_ota_end(ota) != ESP_OK) return 4;
    return esp_ota_set_boot_partition(ota_slot) == ESP_OK ? 0 : 5;
}
static void reboot(void *ctx) {
    (void)ctx;
    esp_restart();
}

static const struct kwb_platform platform = {NULL, now_ms, info, status, scan, scan_results, join, leave, band,
                                             dns_start, dns_result, time_now, ota_begin, ota_write, ota_end, reboot};

const struct kwb_platform *platform_start(struct kwb *b) {
    bridge = b;
    lookups = xQueueCreate(1, 1);
    xTaskCreate(lookup_task, "kui-dns", 4096, NULL, 4, NULL);
    return &platform;
}
