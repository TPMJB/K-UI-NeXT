/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SCI_SD_STORAGE_H
#define KUI_SCI_SD_STORAGE_H
#include "../loader/sd_reader.h"

/* Runtime-only writes; the resident game reader remains read-only. Each
 * operation completes the card's busy phase before reporting success.
 * On a data/status rejection, last_response retains that response byte.
 * A timeout leaves 0xff unless an earlier rejection already explains it. */
enum kui_loader_sd_result kui_sci_sd_write(struct kui_loader_sd *card,
    uint32_t lba, uint32_t count, const uint8_t *data);
enum kui_loader_sd_result kui_sci_sd_sync(struct kui_loader_sd *card);
#endif
