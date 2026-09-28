/* SPDX-License-Identifier: MIT */
/* K-UI Wi-Fi firmware for the Seeed XIAO ESP32-C5 (and C6): the Dreamcast's
 * network connection over its SCI port. See ../README.md. */
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "firmware.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

/* Receive and transmit buffer per open socket slot. lwIP buffers more. */
#define SLOT_BUFFER 4096u

/* A firmware update from the Dreamcast starts on trial (ESP-IDF's app
 * rollback): it is kept only once everything has started and the Dreamcast
 * has reached it over the link, the way the next update would come. A
 * restart before then goes back to the firmware before it. */
static bool on_trial(void) {
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

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
    bool trial = on_trial();
    static struct kwb bridge;
    const struct kwb_platform *platform = platform_start(&bridge);
    bool started = false;
    if(platform) {
        kwb_init(&bridge, platform, SLOT_BUFFER);
        wifi_start(&bridge);
        started = link_start(&bridge);
    }
    if(!started && trial) {
        ESP_LOGE(TAG, "this firmware did not start; going back to the one before it");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    /* Without the platform nothing else can run; without the link, the
     * console still shows what does. */
    if(!platform) return;
    if(!started) ESP_LOGE(TAG, "the Dreamcast link is not running");
    console_start(&bridge);
    if(!started || !trial) return;
    ESP_LOGI(TAG, "updated firmware: kept once the Dreamcast connects");
    struct link_stats link;
    for(link_stats(&link); !link.host_seen; link_stats(&link)) vTaskDelay(pdMS_TO_TICKS(100));
    err = esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "the Dreamcast connected; keeping this firmware (%s)", esp_err_to_name(err));
}
