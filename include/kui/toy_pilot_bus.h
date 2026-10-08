/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_BUS_H
#define KUI_TOY_PILOT_BUS_H
#include <stdint.h>

enum kui_toy_pilot_bus_result {
    KUI_TOY_PILOT_BUS_OK,
    KUI_TOY_PILOT_BUS_BUSY,
    KUI_TOY_PILOT_BUS_TIMEOUT,
    KUI_TOY_PILOT_BUS_ARGUMENT,
    KUI_TOY_PILOT_BUS_STATE,
    /* The command header was published. It must not be retried or rolled back. */
    KUI_TOY_PILOT_BUS_PUBLISHED_STALLED
};

enum kui_toy_pilot_bus_result kui_toy_pilot_bus_read(
    uint32_t address, uint32_t *value);
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_write(
    uint32_t address, uint32_t value);
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_copy(
    uint32_t destination, const void *source, uint32_t bytes);
/* On OK or PUBLISHED_STALLED, slot identifies the published command slot. */
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_publish(
    const uint32_t packet[4], uint32_t *slot);

#endif
