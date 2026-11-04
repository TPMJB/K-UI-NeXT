/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_REPORT_SD_STORAGE_H
#define KUI_REPORT_SD_STORAGE_H
#include "sd_reader.h"
/* Private stopped-report byte-PIO protocol. Exactly one CMD24 data sector;
 * no block DMA, CMD25, initialization or address-mode change is admitted. */
enum kui_loader_sd_result kui_report_sd_write(struct kui_loader_sd *,uint32_t,
    const uint8_t in[512]);
enum kui_loader_sd_result kui_report_sd_sync(struct kui_loader_sd *);
#endif
