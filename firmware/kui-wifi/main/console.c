/* SPDX-License-Identifier: MIT */
/* The USB console: set up and check Wi-Fi from a computer's terminal, with
 * no Dreamcast attached. `help` lists the commands. */
#include "board.h"
#include "esp_console.h"
#include "esp_system.h"
#include "firmware.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static struct kwb *bridge;
static const char *const states[] = {"no network saved", "connecting", "joined, waiting for an address", "online",
                                     "could not join: wrong password?", "network not found",
                                     "connection lost, retrying"};
static const char *const bands[] = {"-", "2.4 GHz only", "5 GHz only", "2.4 and 5 GHz"};
static const char *const securities[] = {"open", "WEP", "WPA", "WPA2", "WPA3", "enterprise", "other"};

static const char *state_name(uint8_t state) { return state < 7 ? states[state] : "?"; }
static void address(const char *label, const uint8_t ip[4]) {
    printf("%-12s%u.%u.%u.%u\n", label, ip[0], ip[1], ip[2], ip[3]);
}
static int status_cmd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    struct kwb_wifi w;
    wifi_status(&w);
    char version[33];
    platform_version(version);
    printf("%-12s%s on %s\n", "Firmware", version, BOARD_NAME);
    printf("%-12s%s\n", "Wi-Fi", state_name(w.state));
    if(w.ssid[0]) printf("%-12s%s%s\n", "Network", w.ssid, w.saved ? " (saved)" : "");
    if(w.channel) printf("%-12s%u (%s GHz), signal %d dBm\n", "Channel", w.channel, w.band == 5 ? "5" : "2.4", w.rssi);
    if(w.state == KWM_WIFI_ONLINE) {
        address("Address", w.ip);
        address("Netmask", w.mask);
        address("Gateway", w.gateway);
        address("DNS", w.dns);
    } else if(w.reason) {
        printf("%-12s%u\n", "Last reason", w.reason);
    }
    printf("%-12s%s\n", "Bands", bands[w.band_mode < 4 ? w.band_mode : 0]);
#ifdef PIN_RF_ANTENNA
    printf("%-12s%s\n", "Antenna", wifi_external_antenna() ? "U.FL connector" : "built-in ceramic");
#endif
    struct link_stats link;
    link_stats(&link);
    printf("%-12s%s, %lu transfers, %lu frames resent\n", "Dreamcast",
           bridge->link.live ? "connected" : link.host_seen ? "was connected" : "not connected",
           (unsigned long)link.transfers, (unsigned long)(bridge->link.stats.resent));
    uint64_t ms;
    if(wifi_time(&ms)) {
        time_t t = (time_t)(ms / 1000u);
        struct tm tm;
        gmtime_r(&t, &tm);
        printf("%-12s%04d-%02d-%02d %02d:%02d:%02d UTC\n", "Clock", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
               tm.tm_hour, tm.tm_min, tm.tm_sec);
    }
    printf("%-12s%lu bytes\n", "Free memory", (unsigned long)esp_get_free_heap_size());
    return 0;
}
static int scan_cmd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    if(!wifi_scan()) {
        printf("Could not start a scan; it may be busy joining. Try again in a moment.\n");
        return 1;
    }
    if(!wifi_scan_wait(15000)) {
        printf("The scan did not finish.\n");
        return 1;
    }
    static struct kwb_ap aps[KWB_SCAN_MAX];
    size_t n = wifi_scan_results(aps, KWB_SCAN_MAX);
    printf("%-32s  %-7s %4s %5s  %s\n", "Network", "Band", "Ch", "dBm", "Security");
    for(size_t i = 0; i < n; ++i)
        printf("%-32s  %-7s %4u %5d  %s\n", aps[i].ssid[0] ? aps[i].ssid : "(hidden)",
               aps[i].channel > 14 ? "5 GHz" : "2.4 GHz", aps[i].channel, aps[i].rssi,
               securities[aps[i].auth < 7 ? aps[i].auth : 6]);
    printf("%u networks.\n", (unsigned)n);
    return 0;
}
static int join_cmd(int argc, char **argv) {
    if(argc < 2 || argc > 3) {
        printf("Usage: join <network> [password]   (quote a name with spaces: join \"My network\" secret)\n");
        return 1;
    }
    uint8_t result = wifi_join(argv[1], argc == 3 ? argv[2] : "", true, KWM_BAND_KEEP);
    if(result) {
        puts(result == KWM_E_INVALID ? "The name must be 1-32 characters and the password 8-64 (or none)."
                                     : "Could not start joining.");
        return 1;
    }
    printf("Joining %s...\n", argv[1]);
    struct kwb_wifi w;
    for(unsigned waited = 0; waited < 30000; waited += 250) {
        vTaskDelay(pdMS_TO_TICKS(250));
        wifi_status(&w);
        if(w.state == KWM_WIFI_ONLINE || w.state == KWM_WIFI_BAD_PASSWORD || w.state == KWM_WIFI_NOT_FOUND) break;
    }
    printf("%s. The network is saved; the board rejoins it by itself.\n", state_name(w.state));
    if(w.state == KWM_WIFI_ONLINE) status_cmd(0, NULL);
    return w.state == KWM_WIFI_ONLINE ? 0 : 1;
}
static int forget_cmd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    wifi_leave(true);
    printf("Disconnected; no network saved.\n");
    return 0;
}
static int band_cmd(int argc, char **argv) {
    uint8_t band = 0;
    if(argc == 2 && (!strcmp(argv[1], "auto") || !strcmp(argv[1], "both"))) band = KWM_BAND_BOTH;
    else if(argc == 2 && (!strcmp(argv[1], "2.4") || !strcmp(argv[1], "2"))) band = KWM_BAND_24;
    else if(argc == 2 && !strcmp(argv[1], "5")) band = KWM_BAND_5;
    if(!band) {
        printf("Usage: band auto|2.4|5   (now: %s)\n", bands[wifi_band() < 4 ? wifi_band() : 0]);
        return 1;
    }
    if(!wifi_set_band(band)) {
        printf("Could not change bands: unsupported by this board or refused by the Wi-Fi driver.\n");
        return 1;
    }
    printf("Bands: %s (kept across restarts). The saved network will reconnect if one is configured.\n", bands[wifi_band()]);
    printf("A different network name needs join \"SSID\" \"password\".\n");
    return 0;
}
#ifdef PIN_RF_ANTENNA
static int antenna_cmd(int argc, char **argv) {
    if(argc != 2 || (strcmp(argv[1], "external") && strcmp(argv[1], "internal"))) {
        printf("Usage: antenna external|internal   (now: %s)\n", wifi_external_antenna() ? "external" : "internal");
        return 1;
    }
    wifi_set_antenna(!strcmp(argv[1], "external"));
    printf("Antenna: %s (kept across restarts).\n", argv[1]);
    return 0;
}
#endif
static int reboot_cmd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    printf("Restarting.\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    return 0;
}
static int version_cmd(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char version[33];
    platform_version(version);
    printf("K-UI Wi-Fi %s, link protocol %u, %s\n", version, KWL_PROTOCOL, BOARD_NAME);
    return 0;
}

static void add(const char *name, const char *help, const char *hint, esp_console_cmd_func_t func) {
    const esp_console_cmd_t cmd = {.command = name, .help = help, .hint = hint, .func = func};
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
void console_start(struct kwb *b) {
    bridge = b;
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    config.prompt = "kui-wifi>";
    config.task_stack_size = 8192;
    config.max_cmdline_length = 256;
    esp_console_dev_usb_serial_jtag_config_t usb = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usb, &config, &repl));
    ESP_ERROR_CHECK(esp_console_register_help_command());
    add("status", "Wi-Fi, addresses, the Dreamcast link and the clock", NULL, status_cmd);
    add("scan", "List the networks in range", NULL, scan_cmd);
    add("join", "Join a network and save it", "<network> [password]", join_cmd);
    add("forget", "Disconnect and forget the saved network", NULL, forget_cmd);
    add("band", "Use 2.4 GHz, 5 GHz or both", "auto|2.4|5", band_cmd);
#ifdef PIN_RF_ANTENNA
    add("antenna", "Use the U.FL antenna or the built-in one", "external|internal", antenna_cmd);
#endif
    add("version", "Firmware version", NULL, version_cmd);
    add("reboot", "Restart the board", NULL, reboot_cmd);
    char version[33];
    platform_version(version);
    printf("\nK-UI Wi-Fi %s on %s. Type 'help' for commands.\n", version, BOARD_NAME);
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
