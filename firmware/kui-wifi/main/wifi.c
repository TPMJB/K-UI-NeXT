/* SPDX-License-Identifier: MIT */
/* Wi-Fi for the K-UI firmware: rejoins the saved network at start, retries
 * with backoff, scans, and keeps the status the bridge reports. */
#include "board.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "firmware.h"
#include "wifi_band_control.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "nvs.h"
#include "soc/soc_caps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define SCAN_DONE BIT0
#define SETTINGS "kuiwifi"
#define RETRY_MAX_MS 30000u
/* Before this the clock has not been set from the network. */
#define CLOCK_SET_AFTER 1704067200

static struct kwb *bridge;
static esp_netif_t *netif;
static SemaphoreHandle_t lock;
/* USB commands, bridge commands and the retry timer can run on different
 * tasks. Keep disconnect/configure/connect sequences together. */
static SemaphoreHandle_t control_lock;
static EventGroupHandle_t events;
static esp_timer_handle_t retry;
static struct kwb_wifi status;
static wifi_ap_record_t *records;
static uint16_t record_count;
/* A network is set and we want to be on it. */
static volatile bool wanted;
static bool configuring;
static unsigned attempts, auth_failures;
static bool sntp_started;
static uint8_t band_mode = KWM_BAND_BOTH;
static bool antenna_external = true;

static void locked(void) { xSemaphoreTake(lock, portMAX_DELAY); }
static void unlocked(void) { xSemaphoreGive(lock); }
static void changed(void) {
    if(bridge) kwb_notify(bridge, KWB_NOTE_WIFI);
}
static uint8_t setting(const char *key, uint8_t fallback) {
    nvs_handle_t h;
    uint8_t value = fallback;
    if(nvs_open(SETTINGS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, key, &value);
        nvs_close(h);
    }
    return value;
}
static void save_setting(const char *key, uint8_t value) {
    nvs_handle_t h;
    if(nvs_open(SETTINGS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, key, value);
    nvs_commit(h);
    nvs_close(h);
}

static bool apply_band(uint8_t band) {
#if SOC_WIFI_SUPPORT_5G
    wifi_band_mode_t mode = band == KWM_BAND_24 ? WIFI_BAND_MODE_2G_ONLY
                            : band == KWM_BAND_5 ? WIFI_BAND_MODE_5G_ONLY
                                                       : WIFI_BAND_MODE_AUTO;
    esp_err_t err = esp_wifi_set_band_mode(mode);
    if(err != ESP_OK) ESP_LOGW(TAG, "band mode: %s", esp_err_to_name(err));
    return err == ESP_OK;
#else
    return band != KWM_BAND_5;
#endif
}
static uint8_t actual_band(void) {
#if SOC_WIFI_SUPPORT_5G
    wifi_band_mode_t mode;
    ESP_ERROR_CHECK(esp_wifi_get_band_mode(&mode));
    return mode == WIFI_BAND_MODE_2G_ONLY ? KWM_BAND_24 : mode == WIFI_BAND_MODE_5G_ONLY ? KWM_BAND_5 : KWM_BAND_BOTH;
#else
    return KWM_BAND_24;
#endif
}
static void apply_antenna(void) {
#ifdef PIN_RF_SWITCH_POWER
    gpio_reset_pin(PIN_RF_SWITCH_POWER);
    gpio_set_direction(PIN_RF_SWITCH_POWER, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_RF_SWITCH_POWER, 0);
    gpio_reset_pin(PIN_RF_ANTENNA);
    gpio_set_direction(PIN_RF_ANTENNA, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_RF_ANTENNA, antenna_external ? 1 : 0);
#endif
}

static void connect_now(void) {
    esp_err_t err = esp_wifi_connect();
    if(err != ESP_OK && err != ESP_ERR_WIFI_CONN) ESP_LOGW(TAG, "connect: %s", esp_err_to_name(err));
}
static void retry_later(void) {
    xSemaphoreTake(control_lock, portMAX_DELAY);
    if(!wanted) {
        xSemaphoreGive(control_lock);
        return;
    }
    uint64_t ms = 1000ull << (attempts < 5 ? attempts : 5);
    if(ms > RETRY_MAX_MS) ms = RETRY_MAX_MS;
    ++attempts;
    esp_timer_stop(retry);
    esp_timer_start_once(retry, ms * 1000u);
    xSemaphoreGive(control_lock);
}
static void on_retry(void *arg) {
    (void)arg;
    xSemaphoreTake(control_lock, portMAX_DELAY);
    if(wanted) connect_now();
    xSemaphoreGive(control_lock);
}
static void clear_connection(void) {
    memset(status.ip, 0, sizeof status.ip);
    memset(status.mask, 0, sizeof status.mask);
    memset(status.gateway, 0, sizeof status.gateway);
    memset(status.dns, 0, sizeof status.dns);
    memset(status.bssid, 0, sizeof status.bssid);
    status.channel = status.band = 0;
    status.rssi = 0;
    status.reason = 0;
}
static void pause_wifi(void) {
    locked();
    configuring = true;
    bool was_wanted = wanted;
    wanted = false;
    clear_connection();
    status.state = was_wanted ? KWM_WIFI_CONNECTING : KWM_WIFI_IDLE;
    unlocked();
    esp_timer_stop(retry);
    esp_wifi_scan_stop();
    changed();
}
static bool disconnect_wifi(void) {
    esp_err_t err = esp_wifi_disconnect();
    if(err != ESP_OK && err != ESP_ERR_WIFI_NOT_CONNECT) ESP_LOGW(TAG, "disconnect: %s", esp_err_to_name(err));
    return err == ESP_OK || err == ESP_ERR_WIFI_NOT_CONNECT;
}
static void resume_wifi(bool reconnect) {
    locked();
    wanted = reconnect;
    configuring = false;
    status.state = reconnect ? KWM_WIFI_CONNECTING : KWM_WIFI_IDLE;
    unlocked();
    if(reconnect) {
        attempts = auth_failures = 0;
        connect_now();
    }
    changed();
}
struct band_change { bool was_wanted, reconnect; };
static void band_pause(void *ctx) { (void)ctx; pause_wifi(); }
static bool band_disconnect(void *ctx) { (void)ctx; return disconnect_wifi(); }
static bool band_apply(void *ctx, uint8_t band) { (void)ctx; return apply_band(band); }
static void band_commit(void *ctx, uint8_t band) {
    (void)ctx;
    locked();
    band_mode = status.band_mode = band;
    unlocked();
    save_setting("band", band);
}
static void band_resume(void *ctx, bool changed_band) {
    const struct band_change *change = ctx;
    resume_wifi(change->was_wanted && (change->reconnect || !changed_band));
}
static bool set_band_locked(uint8_t band, bool reconnect) {
    struct band_change change = {wanted, reconnect};
    const struct kwifi_band_ops ops = {&change, band_pause, band_disconnect, band_apply, band_commit, band_resume};
#if SOC_WIFI_SUPPORT_5G
    return kwifi_band_change(band_mode, band, true, &ops);
#else
    return kwifi_band_change(band_mode, band, false, &ops);
#endif
}
static bool auth_reason(uint8_t reason) {
    return reason == WIFI_REASON_AUTH_EXPIRE || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
           reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_HANDSHAKE_TIMEOUT;
}
static bool missing_reason(uint8_t reason) {
    return reason == WIFI_REASON_NO_AP_FOUND || reason == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY ||
           reason == WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD || reason == WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD;
}
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;
    if(base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        const wifi_event_sta_connected_t *e = data;
        locked();
        if(configuring || !wanted) { unlocked(); return; }
        size_t len = e->ssid_len < KWM_SSID_MAX ? e->ssid_len : KWM_SSID_MAX;
        memcpy(status.ssid, e->ssid, len);
        status.ssid[len] = 0;
        memcpy(status.bssid, e->bssid, 6);
        status.channel = e->channel;
        status.band = e->channel > 14 ? 5 : 2;
        status.reason = 0;
        status.state = KWM_WIFI_ASSOCIATED;
        unlocked();
        auth_failures = 0;
        ESP_LOGI(TAG, "joined %.32s on channel %u", status.ssid, e->channel);
        changed();
    } else if(base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *e = data;
        locked();
        /* A queued intentional leave can arrive after the new join has
         * started. Its status was already cleared before disconnecting. */
        if(configuring || !wanted || e->reason == WIFI_REASON_ASSOC_LEAVE) { unlocked(); return; }
        status.reason = e->reason;
        memset(status.ip, 0, sizeof status.ip);
        status.channel = status.band = 0;
        uint8_t state = KWM_WIFI_LOST;
        if(auth_reason(e->reason)) state = ++auth_failures >= 3 ? KWM_WIFI_BAD_PASSWORD : KWM_WIFI_CONNECTING;
        else if(missing_reason(e->reason)) state = KWM_WIFI_NOT_FOUND;
        status.state = state;
        unlocked();
        ESP_LOGI(TAG, "disconnected (reason %u)", e->reason);
        changed();
        retry_later();
    } else if(base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        uint16_t n = KWB_SCAN_MAX;
        locked();
        if(esp_wifi_scan_get_ap_records(&n, records) != ESP_OK) n = 0;
        record_count = n;
        unlocked();
        xEventGroupSetBits(events, SCAN_DONE);
        if(bridge) kwb_notify(bridge, KWB_NOTE_SCAN);
    } else if(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = data;
        esp_netif_dns_info_t dns;
        memset(&dns, 0, sizeof dns);
        esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
        locked();
        /* Reject an old lease notification while a band change/new join
         * has not yet associated with its access point. */
        if(configuring || !wanted || !status.channel) { unlocked(); return; }
        memcpy(status.ip, &e->ip_info.ip.addr, 4);
        memcpy(status.mask, &e->ip_info.netmask.addr, 4);
        memcpy(status.gateway, &e->ip_info.gw.addr, 4);
        memcpy(status.dns, &dns.ip.u_addr.ip4.addr, 4);
        status.state = KWM_WIFI_ONLINE;
        unlocked();
        attempts = 0;
        ESP_LOGI(TAG, "online as " IPSTR, IP2STR(&e->ip_info.ip));
        changed();
        if(!sntp_started) {
            esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
            sntp_started = esp_netif_sntp_init(&config) == ESP_OK;
        }
    } else if(base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        locked();
        if(configuring) { unlocked(); return; }
        memset(status.ip, 0, sizeof status.ip);
        if(status.state == KWM_WIFI_ONLINE) status.state = KWM_WIFI_ASSOCIATED;
        unlocked();
        changed();
    }
}

void wifi_start(struct kwb *b) {
    bridge = b;
    lock = xSemaphoreCreateMutex();
    control_lock = xSemaphoreCreateMutex();
    events = xEventGroupCreate();
    records = calloc(KWB_SCAN_MAX, sizeof *records);
    uint8_t saved_band = setting("band", KWM_BAND_BOTH);
    if(saved_band != KWM_BAND_24 && saved_band != KWM_BAND_5 && saved_band != KWM_BAND_BOTH) saved_band = KWM_BAND_BOTH;
#if !SOC_WIFI_SUPPORT_5G
    saved_band = KWM_BAND_24;
#endif
    antenna_external = setting("antenna", 1) != 0;
    apply_antenna();
    netif = esp_netif_create_default_wifi_sta();
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char name[20];
    snprintf(name, sizeof name, "kui-wifi-%02x%02x", mac[4], mac[5]);
    esp_netif_set_hostname(netif, name);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    /* The Dreamcast supplies continuous power: keep the radio awake so
     * incoming data does not wait for the router's DTIM wake interval. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_LOST_IP, on_event, NULL, NULL));
    const esp_timer_create_args_t timer = {.callback = on_retry, .name = "wifi-retry"};
    ESP_ERROR_CHECK(esp_timer_create(&timer, &retry));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t saved;
    memset(&saved, 0, sizeof saved);
    esp_wifi_get_config(WIFI_IF_STA, &saved);
    locked();
    memcpy(status.ssid, saved.sta.ssid, KWM_SSID_MAX);
    status.ssid[KWM_SSID_MAX] = 0;
    status.saved = saved.sta.ssid[0] != 0;
    status.state = status.saved ? KWM_WIFI_CONNECTING : KWM_WIFI_IDLE;
    unlocked();
    wanted = status.saved;
    ESP_ERROR_CHECK(esp_wifi_start());
    /* The SDK requires a started driver. Report its actual mode if the
     * saved preference could not be applied; never claim a failed change. */
    band_mode = actual_band();
    if(apply_band(saved_band)) band_mode = saved_band;
    locked();
    status.band_mode = band_mode;
    unlocked();
    if(wanted) connect_now();
}

void wifi_status(struct kwb_wifi *out) {
    locked();
    *out = status;
    unlocked();
    wifi_ap_record_t ap;
    if((out->state == KWM_WIFI_ONLINE || out->state == KWM_WIFI_ASSOCIATED) && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        out->rssi = ap.rssi;
        out->channel = ap.primary;
        out->band = ap.primary > 14 ? 5 : 2;
    }
}
bool wifi_scan(void) {
    xEventGroupClearBits(events, SCAN_DONE);
    esp_err_t err = esp_wifi_scan_start(NULL, false);
    if(err != ESP_OK) ESP_LOGW(TAG, "scan: %s", esp_err_to_name(err));
    return err == ESP_OK;
}
bool wifi_scan_wait(uint32_t ms) {
    return (xEventGroupWaitBits(events, SCAN_DONE, pdFALSE, pdTRUE, pdMS_TO_TICKS(ms)) & SCAN_DONE) != 0;
}
/* Security in the protocol's terms (PROTOCOL.md). */
static uint8_t security(wifi_auth_mode_t mode) {
    switch(mode) {
    case WIFI_AUTH_OPEN: case WIFI_AUTH_OWE: return 0;
    case WIFI_AUTH_WEP: return 1;
    case WIFI_AUTH_WPA_PSK: return 2;
    case WIFI_AUTH_WPA2_PSK: case WIFI_AUTH_WPA_WPA2_PSK: return 3;
    case WIFI_AUTH_WPA3_PSK: case WIFI_AUTH_WPA2_WPA3_PSK: return 4;
    case WIFI_AUTH_WPA2_ENTERPRISE: case WIFI_AUTH_WPA3_ENTERPRISE: case WIFI_AUTH_WPA2_WPA3_ENTERPRISE: return 5;
    default: return 6;
    }
}
size_t wifi_scan_results(struct kwb_ap *out, size_t max) {
    locked();
    size_t n = record_count < max ? record_count : max;
    for(size_t i = 0; i < n; ++i) {
        memcpy(out[i].ssid, records[i].ssid, KWM_SSID_MAX);
        out[i].ssid[KWM_SSID_MAX] = 0;
        memcpy(out[i].bssid, records[i].bssid, 6);
        out[i].channel = records[i].primary;
        out[i].rssi = records[i].rssi;
        out[i].auth = security(records[i].authmode);
    }
    unlocked();
    return n;
}

uint8_t wifi_join(const char *ssid, const char *password, bool save, uint8_t band) {
    size_t ssid_len = strlen(ssid), pass_len = strlen(password);
    /* WPA passphrases are 8 to 63 characters, or 64 hex digits. */
    if(!ssid_len || ssid_len > KWM_SSID_MAX || pass_len > KWM_PASSWORD_MAX || (pass_len && pass_len < 8))
        return KWM_E_INVALID;
    wifi_config_t config;
    memset(&config, 0, sizeof config);
    memcpy(config.sta.ssid, ssid, ssid_len);
    memcpy(config.sta.password, password, pass_len);
    config.sta.threshold.authmode = pass_len ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    xSemaphoreTake(control_lock, portMAX_DELAY);
    bool was_wanted = wanted;
    if(band != KWM_BAND_KEEP && !set_band_locked(band, false)) {
        memset(&config, 0, sizeof config);
        xSemaphoreGive(control_lock);
        return KWM_E_OTHER;
    }
    pause_wifi();
    if(!disconnect_wifi()) {
        memset(&config, 0, sizeof config);
        resume_wifi(was_wanted);
        xSemaphoreGive(control_lock);
        return KWM_E_OTHER;
    }
    esp_err_t err = esp_wifi_set_storage(save ? WIFI_STORAGE_FLASH : WIFI_STORAGE_RAM);
    if(err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &config);
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    memset(&config, 0, sizeof config);
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "join: %s", esp_err_to_name(err));
        resume_wifi(was_wanted);
        xSemaphoreGive(control_lock);
        return KWM_E_OTHER;
    }
    locked();
    memcpy(status.ssid, ssid, ssid_len);
    status.ssid[ssid_len] = 0;
    if(save) status.saved = 1;
    status.reason = 0;
    unlocked();
    resume_wifi(true);
    xSemaphoreGive(control_lock);
    return 0;
}
void wifi_leave(bool forget) {
    xSemaphoreTake(control_lock, portMAX_DELAY);
    pause_wifi();
    disconnect_wifi();
    if(forget) {
        wifi_config_t empty;
        memset(&empty, 0, sizeof empty);
        esp_wifi_set_storage(WIFI_STORAGE_FLASH);
        esp_wifi_set_config(WIFI_IF_STA, &empty);
        locked();
        status.saved = 0;
        status.ssid[0] = 0;
        unlocked();
    }
    resume_wifi(false);
    xSemaphoreGive(control_lock);
}
bool wifi_set_band(uint8_t band) {
    xSemaphoreTake(control_lock, portMAX_DELAY);
    bool ok = set_band_locked(band, true);
    xSemaphoreGive(control_lock);
    return ok;
}
uint8_t wifi_band(void) {
    locked();
    uint8_t band = band_mode;
    unlocked();
    return band;
}
bool wifi_set_antenna(bool external) {
#ifdef PIN_RF_SWITCH_POWER
    antenna_external = external;
    save_setting("antenna", external);
    apply_antenna();
    return true;
#else
    (void)external;
    return false;
#endif
}
bool wifi_external_antenna(void) { return antenna_external; }
bool wifi_time(uint64_t *unix_ms) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if(tv.tv_sec < CLOCK_SET_AFTER) return false;
    *unix_ms = (uint64_t)tv.tv_sec * 1000u + (uint64_t)tv.tv_usec / 1000u;
    return true;
}
