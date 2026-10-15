/* SPDX-License-Identifier: MIT */
/* Seeed XIAO pins for the SCI connector (docs/sci-connector.md). The same
 * XIAO pins on both boards; the GPIO numbers behind them differ. Strapping
 * pins are avoided: the console's pull-ups must not change how it starts. */
#ifndef KUI_WIFI_BOARD_H
#define KUI_WIFI_BOARD_H
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32C5
#define BOARD_CHIP 5
#define BOARD_NAME "XIAO ESP32-C5"
#define PIN_SCLK 8   /* D8 */
#define PIN_MISO 9   /* D9 */
#define PIN_MOSI 10  /* D10 */
#define PIN_CS 1     /* D0 */
#define PIN_READY 0  /* D1 */
#define PIN_RESET 23 /* D4 */
#elif CONFIG_IDF_TARGET_ESP32C6
#define BOARD_CHIP 6
#define BOARD_NAME "XIAO ESP32-C6"
#define PIN_SCLK 19  /* D8 */
#define PIN_MISO 20  /* D9 */
#define PIN_MOSI 18  /* D10 */
#define PIN_CS 0     /* D0 */
#define PIN_READY 1  /* D1 */
#define PIN_RESET 22 /* D4 */
/* The C6 board switches between its own ceramic antenna and the U.FL
 * connector: GPIO3 low powers the switch, GPIO14 high picks U.FL. */
#define PIN_RF_SWITCH_POWER 3
#define PIN_RF_ANTENNA 14
#else
#error "K-UI Wi-Fi firmware supports the XIAO ESP32-C5 and ESP32-C6"
#endif
#endif
