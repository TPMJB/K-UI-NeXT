/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_G1_MODE_PROBE_H
#define KUI_G1_MODE_PROBE_H
#include <stdbool.h>
#include <stdint.h>

#define KUI_G1_MODE_BYTES 10u
#define KUI_G1_MODE_CHOICES 8u

enum kui_g1_probe_reg {
    KUI_G1_ALTSTATUS, KUI_G1_STATUS, KUI_G1_ERROR, KUI_G1_REASON,
    KUI_G1_BYTECOUNT_LO, KUI_G1_BYTECOUNT_HI, KUI_G1_DEVICE,
    KUI_G1_FEATURES, KUI_G1_COMMAND
};
enum kui_g1_probe_result {
    KUI_G1_PROBE_OK, KUI_G1_PROBE_INVALID, KUI_G1_PROBE_BUSY,
    KUI_G1_PROBE_OWNERSHIP, KUI_G1_PROBE_TIMEOUT, KUI_G1_PROBE_PHASE,
    KUI_G1_PROBE_LENGTH, KUI_G1_PROBE_DEVICE_ERROR, KUI_G1_PROBE_MISMATCH
};
struct kui_g1_mode_probe_status {
    char message[128];
    unsigned current, done, total, commands;
    bool complete, failed, stopped, snapshot_valid, restored, restart_required;
    /* Native adapter retains its masked GD IRQ and G1 semaphore on an
     * uncertain command. They are not released before a console restart. */
    bool lease_retained;
    bool tried[KUI_G1_MODE_CHOICES], verified[KUI_G1_MODE_CHOICES];
    bool observed_valid[KUI_G1_MODE_CHOICES];
    uint8_t original[KUI_G1_MODE_BYTES];
    uint8_t requested[KUI_G1_MODE_CHOICES][KUI_G1_MODE_BYTES];
    uint8_t observed[KUI_G1_MODE_CHOICES][KUI_G1_MODE_BYTES];
    uint16_t difference[KUI_G1_MODE_CHOICES];
    enum kui_g1_probe_result attempt[KUI_G1_MODE_CHOICES], result;
    enum kui_g1_probe_result set_result[KUI_G1_MODE_CHOICES], query_result[KUI_G1_MODE_CHOICES];
    uint8_t set_status[KUI_G1_MODE_CHOICES], set_reason[KUI_G1_MODE_CHOICES], set_error[KUI_G1_MODE_CHOICES];
    uint8_t status, reason, error;
    uint64_t elapsed_us;
};
struct kui_g1_mode_probe_bus {
    void *ctx;
    uint8_t (*read8)(void *, enum kui_g1_probe_reg);
    void (*write8)(void *, enum kui_g1_probe_reg, uint8_t);
    uint16_t (*read16)(void *);
    void (*write16)(void *, uint16_t);
    bool (*safe)(void *);
    uint64_t (*now_us)(void *);
    void (*pause)(void *);
    bool (*cancelled)(void *);
    void (*log)(const char *, ...);
};

/* Caller owns an idle, master-selected G1 bus and prevents BIOS/G1 activity.
 * Only SPI REQ_MODE/SET_MODE are sent. Each SET is followed by query, then
 * complete original-mode restore/query, preserving all ten raw mode bytes.
 * There are no sector reads, DMA, storage operations, reset, abort or IRQ
 * register accesses in this portable engine. Waits have deadlines AND poll
 * limits. Stop is honored between commands, after mandatory safe cleanup.
 *
 * A timeout, wrong phase or extra/short data request leaves ownership
 * uncertain; no further command is then issued, even for restoration.
 * restored means either original bytes were fully restored/queried, or no
 * SET was issued and transport ownership is still certain. It never measures
 * physical spindle speed. Rejected, safely restored speed codes are normal
 * diagnostic outcomes. */
bool kui_g1_mode_probe_run(const struct kui_g1_mode_probe_bus *,
    void (*progress)(void *, const struct kui_g1_mode_probe_status *),
    void *progress_ctx, struct kui_g1_mode_probe_status *);
const char *kui_g1_probe_result_name(enum kui_g1_probe_result);

/* Dreamcast only. Caller must first obtain kui_disc_g1_probe_begin(), and
 * always finish with kui_disc_g1_probe_end(out.restored &&
 * !out.restart_required). The wrapper additionally claims KOS's G1 semaphore
 * and masks only the GD-command interrupt. It refuses unknown existing GD
 * handlers and pending transfers. An uncertain raw command retains the G1
 * lease until restart; the caller must block ordinary BIOS/G1 use. */
bool kui_g1_mode_probe_native_run(
    void (*progress)(void *, const struct kui_g1_mode_probe_status *),
    void *progress_ctx, struct kui_g1_mode_probe_status *);
#endif
