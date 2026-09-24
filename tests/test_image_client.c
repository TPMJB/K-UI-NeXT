/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/image_client.h"
#include "kui/gd_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* The client is linked unchanged against the real portable BIOS service.
 * Only address translation and the physical-storage boundary are modelled.
 * Deliberate service/transport faults test whether the client can actually
 * distinguish a working handoff from plausible-looking but wrong results. */
static struct kui_gd_service service;
static const struct kui_image_client_track tracks[] = {
    {1, 4, 0, 8}, {2, 0, 8, 12}, {3, 4, 45000, 45012}
};
/* Independently generated with Python zlib.crc32, not the client's CRC. */
static const struct kui_image_client_sample samples[] = {
    {0, 4, 2048, 0x56f2f4f4}, {7, 2, 2352, 0xf6a05d6e},
    {8, 4, 2352, 0x7ec76f75}, {45000, 4, 2048, 0x2e1b8f0d},
    {45011, 1, 2352, 0x66245fbc}
};
enum fault {
    NONE, CORRUPT_DATA, WRONG_STATUS, STUCK_PENDING, WRONG_SIZE,
    EXTRA_OUTPUT, SPURIOUS_POLL_READ, NO_PHYSICAL_IO, BAD_TOC, BAD_DRIVE,
    IO_ERROR, DATATYPE_FAIL, ABORT_FAIL
};
static enum fault injected;
static uint32_t blocks, reports, failed_reports, exec_calls, read_calls,
                pio_calls, dma_calls, vector_calls;
static const void *pointers[16];
static uint32_t pointer_count;

uint32_t kui_image_client_host_address(const void *pointer) {
    assert(pointer);
    for(uint32_t i = 0; i < pointer_count; ++i)
        if(pointers[i] == pointer) return 0x8c010000u + i * 0x10000u;
    assert(pointer_count < 16);
    pointers[pointer_count++] = pointer;
    return 0x8c010000u + (pointer_count - 1) * 0x10000u;
}
static uint8_t *map(void *context, uint32_t address, uint32_t bytes, int writing) {
    assert(context == &service); (void)writing;
    if(address < 0x8c010000u || address >= 0x8c110000u) return NULL;
    uint32_t index = (address - 0x8c010000u) / 0x10000u;
    uint32_t offset = address & 0xffffu;
    if(index >= pointer_count || bytes > 0x4000u || offset > 0x4000u - bytes)
        return NULL;
    return (uint8_t *)pointers[index] + offset;
}
static int check(void *context, uint32_t lba, uint32_t count, uint32_t size) {
    assert(context == &service);
    for(uint32_t n = 0; n < count; ++n) {
        int found = 0;
        for(uint32_t t = 0; t < 3; ++t) {
            if(lba + n < tracks[t].start_lba || lba + n >= tracks[t].end_lba)
                continue;
            if(size == 2048 && tracks[t].control == 0) return -1;
            found = 1; break;
        }
        if(!found) return -1;
    }
    return 0;
}
static int read_data(void *context, uint32_t lba, uint32_t count,
                      uint32_t size, void *destination) {
    assert(context == &service);
    ++read_calls;
    if(injected != NO_PHYSICAL_IO) blocks += (count * size + 511u) / 512u;
    if(injected == IO_ERROR) return -1;
    uint8_t *out = destination;
    for(uint32_t n = 0; n < count; ++n) {
        for(uint32_t byte = 0; byte < size; ++byte) {
            uint32_t p = byte + (size == 2048 ? 16u : 0u);
            out[n * size + byte] = (uint8_t)(((lba + n) * 37u) ^
                                           (p * 13u) ^ (p >> 8) ^ 0xa5u);
        }
    }
    if(injected == CORRUPT_DATA) out[77] ^= 1;
    if(injected == EXTRA_OUTPUT && count * size < 4u * 2352u)
        out[count * size] = 0x13;
    return 0;
}
int32_t kui_image_client_gd_call(uint32_t function, uintptr_t arg0,
                                uintptr_t arg1) {
    ++vector_calls;
    uint32_t a = (uint32_t)arg0, b = (uint32_t)arg1;
    if(function == KUI_GD_REQUEST || function == KUI_GD_CHECK)
        b = kui_image_client_host_address((const void *)arg1);
    else if(function == KUI_GD_DRIVE || function == KUI_GD_DATATYPE)
        a = kui_image_client_host_address((const void *)arg0);
    if(function == KUI_GD_REQUEST && arg0 == KUI_GD_PIOREAD) ++pio_calls;
    if(function == KUI_GD_REQUEST && arg0 == KUI_GD_DMAREAD) ++dma_calls;
    if(function == KUI_GD_EXEC) {
        ++exec_calls;
        if(injected == STUCK_PENDING) return 0;
    }
    if(function == KUI_GD_ABORT && injected == ABORT_FAIL) return -1;
    if(function == KUI_GD_DATATYPE && injected == DATATYPE_FAIL) return -1;
    int32_t result = kui_gd_service_dispatch(&service, a, b, 0, function);
    if(function == KUI_GD_CHECK) {
        if(injected == SPURIOUS_POLL_READ) ++blocks;
        if(injected == WRONG_STATUS && result == KUI_GD_COMPLETED) result = 3;
        if(injected == WRONG_SIZE && result == KUI_GD_COMPLETED)
            ((uint32_t *)arg1)[2] -= 1u;
    }
    if(function == KUI_GD_DRIVE && injected == BAD_DRIVE)
        ((uint32_t *)arg0)[1] = 0;
    if(function == KUI_GD_EXEC && service.command == KUI_GD_GETTOC2 &&
       injected == BAD_TOC) {
        uint8_t *out = map(&service, service.destination, 408, 1);
        assert(out); out[1] ^= 1;
    }
    return result;
}
static uint32_t read_count(void) { return blocks; }
static void report(const char *label, uint32_t pass, uint32_t detail) {
    assert(label && *label); (void)detail;
    ++reports; failed_reports += !pass;
}
static const struct kui_image_client_api api = {
    KUI_IMAGE_CLIENT_VERSION, sizeof(struct kui_image_client_api),
    3, tracks, 5, samples, report, read_count
};
static void reset(enum fault fault) {
    struct kui_gd_track service_tracks[3];
    for(uint32_t i = 0; i < 3; ++i)
        service_tracks[i] = (struct kui_gd_track) {
            tracks[i].number, tracks[i].control,
            tracks[i].start_lba, tracks[i].end_lba
        };
    const struct kui_gd_ops ops = {&service, map, check, read_data};
    assert(kui_gd_service_init(&service, service_tracks, 3, &ops,
                               0x8c010000u, 0x8c110000u) == 0);
    injected = fault;
    blocks = reports = failed_reports = exec_calls = read_calls = 0;
    pio_calls = dma_calls = vector_calls = pointer_count = 0;
    memset(pointers, 0, sizeof(pointers));
}
static void valid_cases(void) {
    reset(NONE);
    assert(kui_image_client_main(&api) == 0);
    assert(reports == 10 && failed_reports == 0 && blocks > 0);
    assert(read_calls == 14 && dma_calls >= 6 && pio_calls >= 8);
    /* A repeated run must not inherit a failed/complete request or CRC state. */
    assert(kui_image_client_main(&api) == 0 && reports == 20 && read_calls == 28);
}
static void fault_cases(void) {
    for(enum fault f = CORRUPT_DATA; f <= ABORT_FAIL; ++f) {
        reset(f);
        assert(kui_image_client_main(&api) != 0);
        assert(reports == 10 && failed_reports != 0);
        /* Includes the deliberately never-completing server. */
        assert(exec_calls < 4096 && vector_calls < 20000);
    }
    /* Successful transport is insufficient when launcher reference CRC differs. */
    struct kui_image_client_sample changed[5];
    memcpy(changed, samples, sizeof(changed)); changed[3].crc32 ^= 1;
    struct kui_image_client_api wrong = api; wrong.samples = changed;
    reset(NONE);
    assert(kui_image_client_main(&wrong) != 0 && failed_reports != 0);
}
static void invalid_cases(void) {
    reset(NONE);
    assert(kui_image_client_main(NULL) != 0);
    struct kui_image_client_api wrong = api;
    wrong.version = 99; assert(kui_image_client_main(&wrong) != 0);
    wrong = api; wrong.bytes--; assert(kui_image_client_main(&wrong) != 0);
    wrong = api; wrong.sample_count = 17; assert(kui_image_client_main(&wrong) != 0);
    wrong = api; wrong.track_count = 100; assert(kui_image_client_main(&wrong) != 0);
    wrong = api; wrong.track_count = 0; assert(kui_image_client_main(&wrong) != 0);
    wrong = api; wrong.samples = NULL; assert(kui_image_client_main(&wrong) != 0);
    wrong = api; wrong.report = NULL; assert(kui_image_client_main(&wrong) != 0);
    struct kui_image_client_sample changed[5];
    memcpy(changed, samples, sizeof(changed)); changed[0].count = 5;
    wrong = api; wrong.samples = changed;
    assert(kui_image_client_main(&wrong) != 0);
    changed[0] = samples[0]; changed[0].sector_bytes = 512;
    assert(kui_image_client_main(&wrong) != 0);
    changed[0] = samples[0]; changed[0].lba = UINT32_MAX;
    assert(kui_image_client_main(&wrong) != 0);
    assert(vector_calls == 0 && reports == 0 && read_calls == 0);
}
int main(void) {
    valid_cases(); fault_cases(); invalid_cases();
    puts("Image BIOS client: CRC, lifecycle, boundaries and 12 injected faults passed");
    return 0;
}
