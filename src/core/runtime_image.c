/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/runtime.h"
#include <string.h>

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

enum kui_runtime_result kui_runtime_header(const uint8_t header[64],
    uint64_t file_bytes, struct kui_runtime_info *out) {
    memset(out, 0, sizeof(*out));
    if(memcmp(header, "KUIRUN1\0", 8) || le32(header + 12) != 64 ||
       le32(header + 36) || le32(header + 56)) return KUI_RUNTIME_HEADER;
    if(kui_crc32(0, header, 60) != le32(header + 60)) return KUI_RUNTIME_CHECKSUM;
    if(le32(header + 8) != KUI_RUNTIME_VERSION) return KUI_RUNTIME_VERSION_ERROR;
    if(le32(header + 20) != KUI_RUNTIME_ADDRESS ||
       le32(header + 24) != KUI_RUNTIME_ADDRESS) return KUI_RUNTIME_ADDRESS_ERROR;
    uint32_t bytes = le32(header + 16), memory = le32(header + 28);
    if(bytes < 4 || bytes > KUI_RUNTIME_MAX_BYTES || bytes % 4 ||
       memory < bytes || memory > KUI_RUNTIME_MAX_MEMORY || memory % 4 ||
       file_bytes != (uint64_t)64 + bytes) return KUI_RUNTIME_SIZE;
    for(unsigned i = 40; i < 52; ++i)
        if(!((header[i] >= '0' && header[i] <= '9') ||
             (header[i] >= 'a' && header[i] <= 'f'))) return KUI_RUNTIME_HEADER;
    for(unsigned i = 52; i < 56; ++i)
        if(header[i]) return KUI_RUNTIME_HEADER;
    out->payload_bytes = bytes;
    out->memory_bytes = memory;
    out->crc32 = le32(header + 32);
    memcpy(out->build, header + 40, 12);
    return KUI_RUNTIME_OK;
}

const char *kui_runtime_result_name(enum kui_runtime_result result) {
    switch(result) {
        case KUI_RUNTIME_OK: return "OK";
        case KUI_RUNTIME_HEADER: return "invalid runtime header";
        case KUI_RUNTIME_VERSION_ERROR: return "unsupported runtime format version";
        case KUI_RUNTIME_ADDRESS_ERROR: return "unsupported load/entry address";
        case KUI_RUNTIME_SIZE: return "invalid, truncated or oversized runtime";
        case KUI_RUNTIME_CHECKSUM: return "runtime checksum mismatch";
        case KUI_RUNTIME_IO: return "runtime file I/O failed or file missing";
        case KUI_RUNTIME_CANCELLED: return "runtime loading cancelled";
        case KUI_RUNTIME_MEMORY: return "insufficient safe staging memory";
    }
    return "unknown runtime error";
}
