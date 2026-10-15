/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_WIFI_UPDATE_H
#define KUI_WIFI_UPDATE_H
#include "kui/network_wifi.h"
#define KUI_WIFI_FIRMWARE_MAX 0x1e0000u
#define KUI_WIFI_FIRMWARE_HEADER 288u
/* Validate an ESP app header/description for this adapter, excluding merged
 * USB images and other applications. Sets chip/version, leaving other fields. */
bool kui_wifi_firmware_header(const uint8_t *data, size_t bytes, uint8_t chip,
                              struct kui_wifi_firmware *out);
/* UPDATE_CHECK reads and hashes the card image + .sha256 sidecar; UPDATE
 * rechecks that exact preview, writes the inactive slot through SCI, commits,
 * reboots the adapter and checks its new HELLO. No radio connection needed. */
void kui_wifi_update_run(const struct kui_wifi_port *port, const struct kui_wifi_request *request,
    struct kui_wifi_view *out, kui_log_fn log, kui_cancel_fn cancel, kui_wifi_publish_fn publish);
#endif
