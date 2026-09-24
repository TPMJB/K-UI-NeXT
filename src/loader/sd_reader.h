/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_LOADER_SD_READER_H
#define KUI_LOADER_SD_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Freestanding, synchronous, read-only transport. The native implementation
 * takes ownership of SCIF GPIO and TMU1 after the old kernel has shut down.
 * ticks() is a monotonically wrapping 32-bit counter at 12.5 MHz nominal.
 * Callbacks must be bounded and must not access the departed shell. */
struct kui_loader_sd_bus {
    void *ctx;
    void (*begin)(void *ctx);
    void (*end)(void *ctx);
    void (*select)(void *ctx, bool selected);
    uint8_t (*transfer)(void *ctx, uint8_t data, bool slow);
    uint32_t (*ticks)(void *ctx);
};

enum kui_loader_sd_result {
    KUI_LOADER_SD_OK = 0,
    KUI_LOADER_SD_ARGUMENT,
    KUI_LOADER_SD_NOT_READY,
    KUI_LOADER_SD_TIMEOUT,
    KUI_LOADER_SD_COMMAND,
    KUI_LOADER_SD_TOKEN,
    KUI_LOADER_SD_CRC,
    KUI_LOADER_SD_CAPACITY,
    KUI_LOADER_SD_RANGE,
    KUI_LOADER_SD_UNSUPPORTED
};

struct kui_loader_sd {
    struct kui_loader_sd_bus bus;
    uint64_t blocks;
    bool high_capacity;
    bool ready;
    bool slow;
    uint8_t last_command;
    uint8_t last_response;
};

#define KUI_LOADER_SD_MAX_READ_BLOCKS 128u

/* Native initialization is available with KUI_ON_CONSOLE; host callers inject
 * a bus to exercise the same SD protocol and validation without MMIO. */
enum kui_loader_sd_result kui_loader_sd_init(struct kui_loader_sd *card);
enum kui_loader_sd_result kui_loader_sd_init_bus(
    struct kui_loader_sd *card, const struct kui_loader_sd_bus *bus);
enum kui_loader_sd_result kui_loader_sd_read(
    struct kui_loader_sd *card, uint32_t lba, uint32_t count, void *out);
/* Separate CMD18 comparison path; the existing read API remains CMD17-only.
 * Each block is CRC checked. Every issued CMD18 is stopped with CMD12 before
 * deselection, including failed/uncertain responses. If stop/idle cannot be
 * confirmed, ready becomes false and initialization is required before reuse.
 * On error, output may contain completed blocks and the failing block; callers
 * must not treat any part of the requested read as successful. */
enum kui_loader_sd_result kui_loader_sd_read_multi(
    struct kui_loader_sd *card, uint32_t lba, uint32_t count, void *out);
void kui_loader_sd_shutdown(struct kui_loader_sd *card);
const char *kui_loader_sd_result_name(enum kui_loader_sd_result result);

#endif
