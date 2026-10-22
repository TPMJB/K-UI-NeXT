/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_STORAGE_ERROR_H
#define KUI_STORAGE_ERROR_H

#include <stdbool.h>
#include <stdint.h>

enum kui_storage_error_operation {
    KUI_STORAGE_ERROR_NONE = 0,
    KUI_STORAGE_ERROR_READ,
    KUI_STORAGE_ERROR_WRITE,
    KUI_STORAGE_ERROR_SYNC,
    KUI_STORAGE_ERROR_INIT
};

enum kui_storage_error_result {
    KUI_STORAGE_ERROR_OK = 0,
    KUI_STORAGE_ERROR_IO,
    KUI_STORAGE_ERROR_TIMEOUT,
    KUI_STORAGE_ERROR_CRC,
    KUI_STORAGE_ERROR_REJECTED
};

/* A snapshot of failed hardware operations since reset. Validation failures
 * (bad pointers/ranges, disconnected devices) are not hardware errors. Reads
 * larger than the protocol limit identify the failing chunk, not the whole
 * filesystem request; no claim is made about the exact failed sector within
 * that chunk. SYNC/INIT have zero LBA/count. SD detail is available for SCI;
 * response is the rejected command, data-token, or CMD13 status response.
 * This API is owned by the filesystem worker: reset/get between operations,
 * and copy the snapshot into shared UI state under its usual lock. */
struct kui_storage_errors {
    uint32_t total;
    uint32_t read_errors, write_errors, sync_errors, init_errors;
    uint32_t timeout_errors, crc_errors, rejected_errors, io_errors;
    enum kui_storage_error_operation last_operation;
    enum kui_storage_error_result last_result;
    unsigned last_transport;
    uint32_t last_lba, last_count;
    uint8_t sd_command, sd_response;
    bool sd_detail_valid;
};

void kui_storage_errors_reset(void);
void kui_storage_errors_get(struct kui_storage_errors *out);
const char *kui_storage_error_operation_name(enum kui_storage_error_operation operation);
const char *kui_storage_error_result_name(enum kui_storage_error_result result);

#endif
