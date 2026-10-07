/* SPDX-License-Identifier: GPL-3.0-only */
/* Words for the Wi-Fi board's states, security, bands and chips, kept apart
 * from the driver so the shell's renderer can use them alone. */
#include "kui/network_wifi.h"

const char *kui_wifi_state_text(uint8_t state) {
    static const char *const names[] = {"no network set up", "connecting", "joined; waiting for an address", "online",
                                        "wrong password", "network not found", "connection lost; retrying"};
    return state < sizeof(names) / sizeof(names[0]) ? names[state] : "unknown";
}
const char *kui_wifi_security_text(uint8_t security) {
    static const char *const names[] = {"Open", "WEP", "WPA", "WPA2", "WPA3", "Enterprise", "Other"};
    return security < sizeof(names) / sizeof(names[0]) ? names[security] : "Other";
}
const char *kui_wifi_band_text(uint8_t band_mode) {
    return band_mode == KWM_BAND_24 ? "2.4 GHz only" : band_mode == KWM_BAND_5 ? "5 GHz only" : "2.4 and 5 GHz";
}
const char *kui_wifi_chip_text(uint8_t chip) { return chip == 5 ? "ESP32-C5" : chip == 6 ? "ESP32-C6" : "ESP32"; }
