/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CORE_H
#define KUI_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KUI_RAW_BYTES 2352u
#define KUI_DATA_BYTES 2048u

/* Firmware status values, passed through by the platform adapter. */
enum kui_command_result {
    KUI_CMD_OK, KUI_CMD_FAILED, KUI_CMD_TIMEOUT, KUI_CMD_CANCELLED,
    KUI_CMD_RECOVERY_FAILED, KUI_CMD_INVALID
};
struct kui_command_ops {
    void *ctx;
    uint64_t (*now_ms)(void *);
    void (*yield)(void *);
    bool (*cancelled)(void *);
    int (*submit)(void *, int command, void *params);
    int (*poll)(void *, int handle);
    void (*abort)(void *, int handle);
};
/* No DMA/streaming. Callbacks must return; a firmware hang is not preemptible.
 * RECOVERY_FAILED means params/buffers must remain alive and untouched and
 * further commands must be refused until a console reset. */
enum kui_command_result kui_command(const struct kui_command_ops *ops,
    int command, void *params, uint32_t timeout_ms, uint32_t abort_ms);

/* The same command, split so the CPU can do something else while the drive works. Only worth
 * it for a GD-ROM DMA read, where the firmware moves the bytes and the CPU is free (measured:
 * 0.4-0.9% of one core, docs/evidence/t6a-dma-probe-2026-09-20.json); a PIO read needs the CPU
 * to move every byte, so splitting it would gain nothing.
 *
 * kui_command_begin submits and returns at once. kui_command_ready polls without blocking.
 * kui_command_end waits for whatever is left and always finishes the command: it aborts and
 * recovers exactly as kui_command does, so a caller that has begun MUST end (a begun command
 * may still own its params and buffer). The deadline covers begin to end, not each call. */
struct kui_command_async { int handle; uint64_t start; uint32_t timeout_ms, abort_ms; bool live; };
enum kui_command_result kui_command_begin(const struct kui_command_ops *ops,
    int command, void *params, uint32_t timeout_ms, uint32_t abort_ms,
    struct kui_command_async *out);
/* true when end will not have to wait. Never blocks; a failure is reported by end. */
bool kui_command_ready(const struct kui_command_ops *ops, struct kui_command_async *async);
enum kui_command_result kui_command_end(const struct kui_command_ops *ops,
    struct kui_command_async *async);
const char *kui_command_name(enum kui_command_result result);

struct kui_volume { uint32_t start, count; bool partitioned; };
/* Select a FAT32/exFAT superfloppy or exactly one supported MBR partition.
 * A partition adapter exposes this range as sector zero to FatFs. */
bool kui_select_volume(const uint8_t mbr[512], uint64_t blocks,
                       struct kui_volume *out);
bool kui_block_range(uint64_t start, uint64_t count, uint64_t total);
bool kui_fad_to_lba(uint32_t fad, uint32_t *lba);

struct kui_track { unsigned number, control; uint32_t start, end; };
struct kui_toc { struct kui_track tracks[99]; unsigned count; };
/* end is the next TOC start/leadout, NOT an agreed rip/pregap boundary. */
bool kui_parse_toc(const uint32_t entries[99], uint32_t first, uint32_t last,
                   uint32_t leadout, struct kui_toc *out);
unsigned kui_sample_points(const struct kui_track *track, uint32_t points[3]);
int kui_data_offset(const uint8_t raw[KUI_RAW_BYTES]);
bool kui_guard_is(const uint8_t *data, size_t size, uint8_t value);

/* Position-dependent fixture, shared by console and independent PC checker. */
void kui_pattern(uint8_t *out, uint64_t offset, size_t size);
uint32_t kui_crc32(uint32_t previous, const void *data, size_t size);

#endif
