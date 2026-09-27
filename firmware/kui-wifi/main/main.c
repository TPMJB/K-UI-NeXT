/* SPDX-License-Identifier: MIT */
/* K-UI Wi-Fi firmware for the Seeed XIAO ESP32-C5 (and C6): the Dreamcast's
 * network connection over its SCI port. See ../README.md. */
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "firmware.h"
#include "nvs_flash.h"

/* Receive and transmit buffer per open socket slot. lwIP buffers more. */
#define SLOT_BUFFER 4096u

void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    static struct kwb bridge;
    const struct kwb_platform *platform = platform_start(&bridge);
    kwb_init(&bridge, platform, SLOT_BUFFER);
    wifi_start(&bridge);
    link_start(&bridge);
    /* This firmware started: after an update, keep it rather than rolling back. */
    esp_ota_mark_app_valid_cancel_rollback();
    console_start(&bridge);
}
