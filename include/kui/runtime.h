/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RUNTIME_H
#define KUI_RUNTIME_H
#include "kui/probe.h"

#define KUI_RUNTIME_HEADER_BYTES 64u
#define KUI_RUNTIME_VERSION 1u
#define KUI_RUNTIME_ADDRESS 0x8c010000u
#define KUI_RUNTIME_MAX_BYTES (4u * 1024u * 1024u)
#define KUI_RUNTIME_MAX_MEMORY (8u * 1024u * 1024u)
#define KUI_RUNTIME_PATH "0:/KUI/runtime.kui"

struct kui_runtime_info {
    uint32_t payload_bytes, memory_bytes, crc32;
    char build[13];
};
enum kui_runtime_result {
    KUI_RUNTIME_OK, KUI_RUNTIME_HEADER, KUI_RUNTIME_VERSION_ERROR,
    KUI_RUNTIME_ADDRESS_ERROR, KUI_RUNTIME_SIZE, KUI_RUNTIME_CHECKSUM,
    KUI_RUNTIME_IO, KUI_RUNTIME_CANCELLED, KUI_RUNTIME_MEMORY
};
struct kui_runtime_image { void *data; struct kui_runtime_info info; };

/* Versioned, little-endian raw SH-4 image. CRC checks detect corruption;
 * they do not authenticate executable code. Use trusted project packages. */
enum kui_runtime_result kui_runtime_header(const uint8_t header[64],
    uint64_t file_bytes, struct kui_runtime_info *out);
const char *kui_runtime_result_name(enum kui_runtime_result result);

/* Caller mounts the volume and later unmounts/disconnects it. Never executes.
 * Success owns a malloc allocation in out; every failure leaves out empty. */
enum kui_runtime_result kui_runtime_read(const char *path,
    struct kui_runtime_image *out, kui_log_fn log, kui_cancel_fn cancelled);
void kui_runtime_free(struct kui_runtime_image *image);
#endif
