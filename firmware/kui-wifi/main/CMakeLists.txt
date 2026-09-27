# SPDX-License-Identifier: MIT
idf_component_register(SRCS "main.c" "bridge.c" "link_spi.c" "wifi.c" "esp_platform.c" "console.c"
                       INCLUDE_DIRS "."
                       REQUIRES kwlink esp_wifi esp_netif esp_event nvs_flash console esp_driver_spi
                                esp_driver_gpio app_update esp_app_format mbedtls esp_timer lwip)
