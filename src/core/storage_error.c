/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/storage_error.h"

const char *kui_storage_error_operation_name(enum kui_storage_error_operation operation) {
    switch(operation) {
        case KUI_STORAGE_ERROR_READ:return "read";
        case KUI_STORAGE_ERROR_WRITE:return "write";
        case KUI_STORAGE_ERROR_SYNC:return "sync";
        case KUI_STORAGE_ERROR_INIT:return "init";
        default:return "none";
    }
}
const char *kui_storage_error_result_name(enum kui_storage_error_result result) {
    switch(result) {
        case KUI_STORAGE_ERROR_IO:return "I/O failure";
        case KUI_STORAGE_ERROR_TIMEOUT:return "timeout";
        case KUI_STORAGE_ERROR_CRC:return "CRC mismatch";
        case KUI_STORAGE_ERROR_REJECTED:return "card rejected request";
        default:return "none";
    }
}
