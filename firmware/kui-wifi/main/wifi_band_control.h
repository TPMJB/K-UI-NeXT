/* SPDX-License-Identifier: MIT */
#ifndef KUI_WIFI_BAND_CONTROL_H
#define KUI_WIFI_BAND_CONTROL_H
#include "kwmsg.h"
#include <stdbool.h>
#include <stdint.h>

/* A serialized band change: pause retries, leave the old association,
 * apply the driver mode, then publish/save only a successful change.
 * resume runs on both success and failure so a failed request can rejoin
 * the previous network. No credentials are changed by these operations. */
struct kwifi_band_ops {
    void *ctx;
    void (*pause)(void *ctx);
    bool (*disconnect)(void *ctx);
    bool (*apply)(void *ctx, uint8_t band);
    void (*commit)(void *ctx, uint8_t band);
    void (*resume)(void *ctx, bool changed);
};
bool kwifi_band_change(uint8_t current, uint8_t requested, bool supports_5g, const struct kwifi_band_ops *ops);
#endif
