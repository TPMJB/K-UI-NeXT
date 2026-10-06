/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_WIFI_MODEL_H
#define KUI_TEST_WIFI_MODEL_H
#include "kui/network_wifi.h"

/* K-UI's Wi-Fi board for host tests: the firmware's own bridge core
 * (firmware/kui-wifi/main/bridge.c) behind a simulated SPI bus with its
 * READY line, and a stand-in for the board's Wi-Fi. Its sockets are real
 * sockets on this machine and its address is 127.0.0.1, so a real client
 * can connect to what K-UI opens on it.
 *
 * The port has two chip selects (GPIO6, GPIO7), each at two rates. */
#define WIFI_MODEL_SELECTS 2u
#define WIFI_MODEL_RATES 2u
struct wifi_model_options {
    bool absent;       /* nothing on the port: READY never changes, MISO reads 0xff */
    unsigned select;   /* the chip select the board is on */
    unsigned bad_rates; /* the fastest rates at which every transfer is damaged */
    bool no_ready;     /* the READY wire is missing */
    uint8_t state;     /* the board's Wi-Fi at start (KWM_WIFI_*); online means on "Home 5G" */
};
extern const struct kui_wifi_port wifi_model_port;
void wifi_model_start(const struct wifi_model_options *options);
void wifi_model_stop(void);
/* The board restarts: it forgets the session and closes everything. */
void wifi_model_restart(void);
/* The board's Wi-Fi drops (lost, retrying) or comes back on another address. */
void wifi_model_drop(void);
void wifi_model_recover(const uint8_t ip[4]);
/* What the board was last asked to join. */
struct wifi_model_join {
    unsigned count;
    char ssid[KWM_SSID_MAX + 1], password[KWM_PASSWORD_MAX + 1];
    uint8_t band;
    bool save;
};
extern struct wifi_model_join wifi_model_joined;
/* The options in force, to change while running (unplugging it, say). */
struct wifi_model_options *wifi_model_live(void);
/* A real TCP listener has been bound, rather than merely queued on SPI. */
bool wifi_model_listening(uint16_t port);
/* Firmware update faults, applied behind the real message/link bridge. */
struct wifi_model_ota_faults {
    uint8_t begin_status, data_status, end_status;
    bool wrong_phase, wrong_written, corrupt_image, corrupt_frame_once, rollback, wrong_version;
    bool lose_end_reply, never_returns;
    uint32_t reset_after;
    unsigned reboot_silent_ms;
};
struct wifi_model_ota_result {
    unsigned begins, writes, ends, reboots, aborts;
    uint32_t size, written;
    bool open, committed, checksum_ok;
    uint8_t expected_sha[32], actual_sha[32];
};
void wifi_model_ota_configure(const struct wifi_model_ota_faults *faults);
const struct wifi_model_ota_result *wifi_model_ota_result(void);
const uint8_t *wifi_model_ota_bytes(void);
void wifi_model_firmware(uint8_t chip, const char *version);
/* Transfers with the board, and transfers nobody answered. */
extern unsigned wifi_model_transfers, wifi_model_unanswered;
/* The networks the model's scan finds: "Home 5G" (5 GHz, WPA2, password
 * "correct horse"), "Home" (2.4 GHz, WPA2, same password, heard twice),
 * "Cafe" (open) and a hidden one. */
#define WIFI_MODEL_PASSWORD "correct horse"
#endif
