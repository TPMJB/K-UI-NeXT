/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the actual bootstrap envelope parser and handoff patcher against
 * a delivered package. No console execution and no writes to the package. */
#include "kui/runtime.h"
#include "kui/storage_policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if(argc != 2) {
        fprintf(stderr, "usage: %s tools.kui\n", argv[0]);
        return 2;
    }
    FILE *file = fopen(argv[1], "rb");
    if(!file) { perror(argv[1]); return 1; }
    if(fseek(file, 0, SEEK_END)) { fclose(file); return 1; }
    long bytes = ftell(file);
    if(bytes < (long)KUI_RUNTIME_HEADER_BYTES ||
       bytes > (long)(KUI_RUNTIME_HEADER_BYTES + KUI_RUNTIME_MAX_BYTES) ||
       fseek(file, 0, SEEK_SET)) {
        fprintf(stderr, "FAIL: invalid package length\n");
        fclose(file);
        return 1;
    }
    uint8_t *package = malloc((size_t)bytes);
    if(!package) { fclose(file); return 1; }
    bool read_ok = fread(package, 1, (size_t)bytes, file) == (size_t)bytes;
    if(fclose(file)) read_ok = false;
    if(!read_ok) { free(package); return 1; }
    struct kui_runtime_info info;
    enum kui_runtime_result result = kui_runtime_header(package, (uint64_t)bytes, &info);
    if(result != KUI_RUNTIME_OK) {
        fprintf(stderr, "FAIL: %s\n", kui_runtime_result_name(result));
        free(package);
        return 1;
    }
    const uint8_t *payload = package + KUI_RUNTIME_HEADER_BYTES;
    if(kui_crc32(0, payload, info.payload_bytes) != info.crc32) {
        fprintf(stderr, "FAIL: runtime checksum mismatch\n");
        free(package);
        return 1;
    }
    uint8_t *staged = malloc(info.payload_bytes);
    if(!staged) { free(package); return 1; }
    bool accepted = true;
    for(unsigned transport = KUI_STORAGE_SCIF; transport < KUI_STORAGE_AUTO; ++transport) {
        memcpy(staged, payload, info.payload_bytes);
        if(!kui_storage_patch_boot(staged, info.payload_bytes, transport)) {
            fprintf(stderr, "FAIL: missing, malformed or duplicate storage handoff (transport %u)\n",
                transport);
            accepted = false;
            break;
        }
    }
    free(staged);
    free(package);
    if(!accepted) return 1;
    printf("PASS: package %s envelope/CRC and SCIF/SCI/IDE storage handoff\n", info.build);
    return 0;
}
