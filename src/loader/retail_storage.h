/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_STORAGE_H
#define KUI_RETAIL_STORAGE_H
#include "retail_sd.h"
#include "kui/storage.h"
#include "kui/ata.h"

struct kui_retail_storage {
    uint32_t transport;
    union { struct kui_loader_sd sd; struct kui_ata ata; } device;
    struct kui_loader_sd_stream stream;
};
/* Fixed-device handoff: no probing or fallback after a manifest is prepared.
 * Each image operation acquires, reads, stops, then releases on every path.
 * Adopt copies protocol state and rebinds callbacks to this linked image. */
enum kui_loader_sd_result kui_retail_storage_init(struct kui_retail_storage *, uint32_t transport);
enum kui_loader_sd_result kui_retail_storage_adopt(struct kui_retail_storage *, const struct kui_retail_storage *);
enum kui_loader_sd_result kui_retail_storage_acquire(struct kui_retail_storage *);
enum kui_loader_sd_result kui_retail_storage_read_run(struct kui_retail_storage *, uint32_t lba, uint32_t available, uint8_t out[512]);
enum kui_loader_sd_result kui_retail_storage_stop(struct kui_retail_storage *);
void kui_retail_storage_release(struct kui_retail_storage *);
uint64_t kui_retail_storage_blocks(const struct kui_retail_storage *);
const char *kui_retail_storage_name(uint32_t transport);
#endif
