/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/runtime.h"
#include <inttypes.h>
#include <stdlib.h>

void kui_runtime_free(struct kui_runtime_image *image) {
    free(image->data);
    *image = (struct kui_runtime_image){0};
}

enum kui_runtime_result kui_runtime_read(const char *path,
    struct kui_runtime_image *out, kui_log_fn log, kui_cancel_fn cancelled) {
    FIL file;
    uint8_t header[KUI_RUNTIME_HEADER_BYTES];
    UINT done;
    enum kui_runtime_result result = KUI_RUNTIME_IO;
    *out = (struct kui_runtime_image){0};
    if(cancelled()) return KUI_RUNTIME_CANCELLED;
    FRESULT r = f_open(&file, path, FA_READ);
    if(r != FR_OK) {
        log("Cannot open %s: FatFs=%u", path + 2, (unsigned)r);
        return result;
    }
    if(f_size(&file) < sizeof(header)) { result = KUI_RUNTIME_SIZE; goto close; }
    if(f_read(&file, header, sizeof(header), &done) != FR_OK || done != sizeof(header))
        goto close;
    result = kui_runtime_header(header, f_size(&file), &out->info);
    if(result != KUI_RUNTIME_OK) goto close;
    if(cancelled()) { result = KUI_RUNTIME_CANCELLED; goto close; }
    out->data = malloc(out->info.payload_bytes);
    if(!out->data) { result = KUI_RUNTIME_MEMORY; goto close; }
    log("Loading SD runtime %s (%" PRIu32 " bytes)", out->info.build, out->info.payload_bytes);
    uint32_t offset = 0, crc = 0;
    while(offset < out->info.payload_bytes) {
        if(cancelled()) { result = KUI_RUNTIME_CANCELLED; goto close; }
        UINT n = out->info.payload_bytes - offset;
        if(n > 32768) n = 32768;
        uint8_t *data = (uint8_t *)out->data + offset;
        if(f_read(&file, data, n, &done) != FR_OK || done != n) {
            result = KUI_RUNTIME_IO; goto close;
        }
        crc = kui_crc32(crc, data, n);
        offset += n;
    }
    if(crc != out->info.crc32) result = KUI_RUNTIME_CHECKSUM;
    if(cancelled()) result = KUI_RUNTIME_CANCELLED;
close:
    if(f_close(&file) != FR_OK && result == KUI_RUNTIME_OK) result = KUI_RUNTIME_IO;
    if(result != KUI_RUNTIME_OK) kui_runtime_free(out);
    return result;
}
