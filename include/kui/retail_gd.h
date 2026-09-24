/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_GD_H
#define KUI_RETAIL_GD_H

#include "kui/gd_service.h"

/* This opt-in service is independent of the accepted own-client probe. The
 * native entry must serialize dispatch BEFORE switching to its private stack,
 * preserve the caller's interrupt state, and make P1/P2 data coherent. */
#define KUI_RETAIL_GD_STEP_SECTORS 2u
#define KUI_RETAIL_GD_CHECK_SECTORS 8u
enum kui_retail_map_access {
    /* ops.map writing=0/1 retains read/write access. Validation only checks
     * bounds and ownership; its non-null result must never be dereferenced. */
    KUI_RETAIL_MAP_VALIDATE = 2
};
enum kui_retail_gd_command {
    KUI_RETAIL_GD_GETTOC = 18, KUI_RETAIL_GD_SEEK = 27,
    KUI_RETAIL_GD_REQ_MODE = 30, KUI_RETAIL_GD_SET_MODE = 31,
    KUI_RETAIL_GD_REQ_STAT = 36, KUI_RETAIL_GD_GET_VERS = 40
};

struct kui_retail_gd_diagnostics {
    uint32_t calls, requests, exec_calls, read_steps, sectors_read, rejected;
    uint32_t last_function, last_command, last_lba, last_count, last_destination;
    uint32_t last_error;
    int32_t last_result;
};
struct kui_retail_gd {
    struct kui_gd_ops ops;
    const struct kui_gd_track *tracks;
    uint32_t track_count, guest_begin, guest_end;
    uint32_t sector_part, track_type, sector_bytes;
    uint32_t token, command, lba, count, destination, area, request_bytes;
    uint32_t completed_bytes, error, pending, executing, initialized;
    uint32_t position_lba, drive_status, mode[4], outputs[4];
    int32_t status;
    struct kui_retail_gd_diagnostics diag;
};

/* Tracks are referenced, not copied, and must stay resident and immutable.
 * ops.map receives checked P1 addresses. Full read destinations are mapped
 * with KUI_RETAIL_MAP_VALIDATE during REQUEST, without cache maintenance or
 * memory access; EXEC maps each output chunk for writing before use.
 * ops.check is called in <=8-sector chunks with no I/O; ops.read in <=2-sector
 * chunks only from EXEC.
 * All destination bytes must fit [guest_begin,guest_end). */
int kui_retail_gd_init(struct kui_retail_gd *, const struct kui_gd_track *,
    uint32_t count, const struct kui_gd_ops *, uint32_t guest_begin,
    uint32_t guest_end);

/* PIOREAD/DMAREAD are polled CPU copies, without DMA hardware, IRQ or callback.
 * CHECK never reads storage. Completion/failure is acknowledged once by CHECK;
 * a subsequent CHECK returns NOT_FOUND. ABORT retains completed chunk bytes.
 * MISC and stream functions are not handled here. Callback-clear (r4=0) is a
 * supported no-op; nonzero callback installation is explicitly unsupported.
 * The mode command's four words are virtual drive metadata, not physical SD
 * settings. DATATYPE accepts Mode1/2048 (type0 automatic or1024 explicit) and
 * complete2352 sectors. No audio playback, Mode2 conversion or CDDA emulation.
 * GET_VERS writes the 28-byte driver compatibility response at params[0],
 * with a trailing state byte (not a C-string terminator). It performs no I/O
 * and reports zero disc-transfer bytes, following the BIOS command contract.
 */
int32_t kui_retail_gd_dispatch(struct kui_retail_gd *, uint32_t r4,
    uint32_t r5, uint32_t r6, uint32_t r7);

#endif
