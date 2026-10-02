/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RUNTIME_SD_H
#define KUI_RUNTIME_SD_H
#include "kui/sci_async_probe.h"
#include "../loader/sd_reader.h"
enum kui_sd_async_recovery_phase {
    KUI_SD_ASYNC_RECOVERY_NONE, KUI_SD_ASYNC_RECOVERY_ACQUIRE,
    KUI_SD_ASYNC_RECOVERY_INITIALIZE, KUI_SD_ASYNC_RECOVERY_READ
};
/* Runtime Diagnostics only. The sole storage worker pauses all consumers and
 * owns the connected SCI card with all files closed. This wrapper unmounts
 * the volume before raw reads and leaves it unmounted on every exit. */
struct kui_sd_async_result {
    struct kui_sci_async_probe_result probe;
    bool baseline_verified, recovery_verified, recovery_reinitialized, restart_required;
    uint32_t baseline_crc32;
    enum kui_sd_async_recovery_phase recovery_phase;
    enum kui_loader_sd_result recovery_result;
    uint8_t recovery_command, recovery_response;
    bool recovery_command_valid, recovery_bus_healthy, recovery_data_match;
    char message[128];
};
const char *kui_sd_async_recovery_name(enum kui_sd_async_recovery_phase phase);
void kui_sd_async_probe(struct kui_sd_async_result *out,
    bool (*cancelled)(void *),void *cancel_ctx);
#endif
