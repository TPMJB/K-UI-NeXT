/* SPDX-License-Identifier: MIT */
/* The SPI link to the Dreamcast: this board is the SPI device, in mode 3,
 * with one transfer armed at a time. READY flips each time one is armed
 * (see PROTOCOL.md). The bridge core does everything else. */
#include "board.h"
#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "firmware.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

#define LINK_HOST SPI2_HOST
/* The reset line: a short pulse resets the link, a long hold the board. */
#define RESET_LINK_US 5000
#define RESET_BOARD_US 2000000

static volatile uint32_t ready_level;
static struct link_stats stats;
/* Whether the task armed its first transfer, for link_start. */
enum { LINK_STARTING, LINK_RUNNING, LINK_FAILED };
static volatile int link_state = LINK_STARTING;
static void link_failed(const char *what, esp_err_t err) {
    ESP_LOGE(TAG, "SPI link: %s (%s)", what, esp_err_to_name(err));
    link_state = LINK_FAILED;
    vTaskDelete(NULL);
}

/* Runs in the SPI interrupt once the transfer's data is loaded. */
static void IRAM_ATTR on_armed(spi_slave_transaction_t *t) {
    (void)t;
    ready_level ^= 1u;
    gpio_set_level(PIN_READY, ready_level);
}

static void link_task(void *arg) {
    struct kwb *b = arg;
    const gpio_config_t ready = {.pin_bit_mask = 1ull << PIN_READY, .mode = GPIO_MODE_OUTPUT};
    const gpio_config_t reset = {.pin_bit_mask = 1ull << PIN_RESET, .mode = GPIO_MODE_INPUT,
                                 .pull_up_en = GPIO_PULLUP_ENABLE};
    esp_err_t err = gpio_config(&ready);
    if(err == ESP_OK) err = gpio_config(&reset);
    if(err != ESP_OK) {
        link_failed("pins", err);
        return;
    }
    gpio_set_level(PIN_READY, 0);
    const spi_bus_config_t bus = {.mosi_io_num = PIN_MOSI, .miso_io_num = PIN_MISO, .sclk_io_num = PIN_SCLK,
                                  .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = KWL_FRAME_MAX};
    const spi_slave_interface_config_t device = {.spics_io_num = PIN_CS, .queue_size = 1, .mode = 3,
                                                 .post_setup_cb = on_armed};
    err = spi_slave_initialize(LINK_HOST, &bus, &device, SPI_DMA_CH_AUTO);
    if(err != ESP_OK) {
        link_failed("SPI device", err);
        return;
    }
    /* Defined idle levels while nothing drives the lines, as on the bench
     * with only USB connected: deselected, clock high. */
    gpio_set_pull_mode(PIN_CS, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_SCLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(PIN_MOSI, GPIO_PULLUP_ONLY);
    uint8_t *tx = spi_bus_dma_memory_alloc(LINK_HOST, KWL_FRAME_MAX, 0);
    uint8_t *rx = spi_bus_dma_memory_alloc(LINK_HOST, KWL_FRAME_MAX, 0);
    if(!tx || !rx) {
        free(tx);
        free(rx);
        link_failed("no memory for its buffers", ESP_ERR_NO_MEM);
        return;
    }
    spi_slave_transaction_t transfer;
    memset(&transfer, 0, sizeof transfer);
    transfer.length = KWL_FRAME_MAX * 8u;
    transfer.tx_buffer = tx;
    transfer.rx_buffer = rx;
    size_t len = kwb_frame(b, tx);
    memset(tx + len, 0, KWL_FRAME_MAX - len);
    err = spi_slave_queue_trans(LINK_HOST, &transfer, portMAX_DELAY);
    if(err != ESP_OK) {
        link_failed("first transfer", err);
        return;
    }
    link_state = LINK_RUNNING;
    int64_t low_since = 0;
    bool link_reset = false;
    for(;;) {
        spi_slave_transaction_t *done = NULL;
        if(spi_slave_get_trans_result(LINK_HOST, &done, pdMS_TO_TICKS(2)) == ESP_OK) {
            ++stats.transfers;
            kwb_receive(b, rx, done->trans_len / 8u);
            if(b->link.live) stats.host_seen = true;
            len = kwb_frame(b, tx);
            memset(tx + len, 0, KWL_FRAME_MAX - len);
            ESP_ERROR_CHECK(spi_slave_queue_trans(LINK_HOST, &transfer, portMAX_DELAY));
            /* lwIP socket work can run while the next DMA frame is armed.
             * In particular, do not leave SPI unarmed during select/recv:
             * the six-wire host cannot see READY and has a fixed gap. */
            kwb_service(b);
        } else {
            kwb_service(b);
        }
        if(!gpio_get_level(PIN_RESET)) {
            int64_t now = esp_timer_get_time();
            if(!low_since) low_since = now;
            if(!link_reset && now - low_since >= RESET_LINK_US) {
                ESP_LOGI(TAG, "reset line: link reset");
                kwb_reset(b);
                ++stats.resets;
                link_reset = true;
            }
            if(now - low_since >= RESET_BOARD_US) esp_restart();
        } else {
            low_since = 0;
            link_reset = false;
        }
    }
}

bool link_start(struct kwb *bridge) {
    if(xTaskCreate(link_task, "kui-link", 8192, bridge, 10, NULL) != pdPASS) {
        ESP_LOGE(TAG, "SPI link: no memory for its task");
        return false;
    }
    /* Setting up the SPI device takes a few milliseconds. */
    for(unsigned i = 0; i < 100 && link_state == LINK_STARTING; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    return link_state == LINK_RUNNING;
}
void link_stats(struct link_stats *out) { *out = stats; }
