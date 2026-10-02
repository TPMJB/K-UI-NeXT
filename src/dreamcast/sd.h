/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RUNTIME_SD_H
#define KUI_RUNTIME_SD_H
#include "kui/sci_async_probe.h"
/* Runtime Diagnostics only. The sole storage worker pauses all consumers and
 * owns the connected SCI card with all files closed. This wrapper unmounts
 * the volume before raw reads and leaves it unmounted on every exit. */
struct kui_sd_async_result {
    struct kui_sci_async_probe_result probe;
    bool baseline_verified, recovery_verified, recovery_reinitialized, restart_required;
    uint32_t baseline_crc32;
    char message[128];
};
void kui_sd_async_probe(struct kui_sd_async_result *out,
    bool (*cancelled)(void *),void *cancel_ctx);
#endif
