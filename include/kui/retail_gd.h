/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_GD_H
#define KUI_RETAIL_GD_H

#include "kui/gd_service.h"
#include "kui/retail_image.h"

/* This opt-in service is independent of the accepted own-client probe. The
 * native entry must serialize dispatch BEFORE switching to its private stack,
 * preserve the caller's interrupt state, and make P1/P2 data coherent. */
#define KUI_RETAIL_GD_STEP_SECTORS 2u
#define KUI_RETAIL_GD_STEP_MAX 8u
#define KUI_RETAIL_GD_CHECK_SECTORS 8u
enum kui_retail_map_access {
    /* ops.map writing=0/1 retains read/write access. Validation only checks
     * bounds and ownership; its non-null result must never be dereferenced. */
    KUI_RETAIL_MAP_VALIDATE = 2
};
enum kui_retail_gd_command {
    KUI_RETAIL_GD_GETTOC = 18, KUI_RETAIL_GD_PLAY = 20, KUI_RETAIL_GD_PLAY2 = 21,
    KUI_RETAIL_GD_PAUSE = 22, KUI_RETAIL_GD_RELEASE = 23, KUI_RETAIL_GD_SEEK = 27,
    KUI_RETAIL_GD_REQ_MODE = 30, KUI_RETAIL_GD_SET_MODE = 31,
    KUI_RETAIL_GD_GETSCD = 34,
    KUI_RETAIL_GD_REQ_STAT = 36, KUI_RETAIL_GD_GET_VERS = 40
};

#ifdef KUI_RETAIL_CE
/* Windows CE's DMA stream read (DMAREAD_STREAM_EX), params {FAD, sectors,
 * flag}: CHECK reports STREAMING while bytes remain, and the driver moves
 * them with DMA_TRANSFER {destination, bytes}, one physically contiguous
 * piece of its buffer at a time (adjacent pages merge, so any size), then
 * DMA_CHECK {bytes left}. Each driver call moves 4 KiB of a transfer and
 * raises the interrupts that keep CE's driver calling: G1 DMA end, and the
 * drive's while bytes remain and at completion (its SYSINTR 21 and 20). */
/* Ordinary reads (PIOREAD, DMAREAD) in this build: CHECK's word 3 is 1
 * while one is pending and each step raises the drive's interrupt, so CE's
 * driver continues on that interrupt instead of sleeping a 25 ms tick. */
#define KUI_RETAIL_GD_DMAREAD_STREAM 38u
/* Its PIO twin (PIOREAD_STREAM_EX), same params: the driver registers a
 * callback (PIO_CALLBACK), then moves pieces of at most what PIO_CHECK
 * offers (4 KiB) with PIO_TRANSFER {destination, bytes} into its own
 * (virtual) buffer, and its callback, made when a transfer ends, moves the
 * next. Transfers complete at once; the callback is then due and the drive's
 * interrupt raised so that CE's driver calls EXEC, after which the adapter
 * makes the callback (callback_due, pio_callback, pio_argument). */
#define KUI_RETAIL_GD_PIOREAD_STREAM 39u
#define KUI_RETAIL_GD_STREAMING 3
enum kui_retail_gd_interrupt {
    KUI_RETAIL_GD_IRQ_DMA_END = 1, KUI_RETAIL_GD_IRQ_DRIVE = 2
};
#endif
struct kui_retail_gd_diagnostics {
    uint32_t calls, requests, exec_calls, read_steps, sectors_read, rejected;
    uint32_t last_function, last_command, last_lba, last_count, last_destination;
    uint32_t last_error;
    int32_t last_result;
};
struct kui_retail_gd {
    struct kui_gd_ops ops;
    const union kui_retail_slot *tracks; /* A launch map's track slots. */
    uint32_t track_count, guest_begin, guest_end;
    uint32_t sector_part, track_type, sector_bytes;
    uint32_t token, command, lba, count, destination, area, request_bytes;
    uint32_t completed_bytes, error, pending, executing, initialized;
    uint32_t position_lba, drive_status, mode[4], outputs[4];
    uint32_t disc_type; /* Persistent BIOS disc kind: GD0x80 or CD-ROM0x10. */
    int32_t status;
    /* Sectors one EXEC may read: adapter pacing policy, not drive state.
     * Init sets STEP_SECTORS; values outside 1..STEP_MAX use that default. */
    uint32_t step;
#ifdef KUI_RETAIL_CE
    /* Copies bytes [skip, skip + bytes) of the user data of the sectors
     * from lba on (sector_bytes each); zero only when all were produced.
     * The adapter sets it after init; streams are refused without it. */
    int (*read_part)(void *, uint32_t lba, uint32_t sector_bytes,
                     uint32_t skip, uint32_t bytes, void *output);
    uint32_t interrupts; /* KUI_RETAIL_GD_IRQ_* raised; the adapter clears. */
    uint32_t xfer_destination, xfer_left; /* The stream's current DMA transfer. */
    uint32_t pio_callback, pio_argument, callback_due; /* A PIO stream's callback. */
#endif
    struct kui_retail_gd_diagnostics diag;
};

/* Tracks (a launch map's first count slots, track i numbered i + 1; their
 * extent fields are not used here) are referenced, not copied, and must
 * stay resident and immutable.
 * ops.map receives checked P1 addresses. Full read destinations are mapped
 * with KUI_RETAIL_MAP_VALIDATE during REQUEST, without cache maintenance or
 * memory access; EXEC maps each output chunk for writing before use.
 * ops.check is called in <=8-sector chunks with no I/O; ops.read only from
 * EXEC, in chunks of at most step sectors (two unless the adapter paces it).
 * All destination bytes must fit [guest_begin,guest_end). */
int kui_retail_gd_init(struct kui_retail_gd *, const union kui_retail_slot *tracks,
    uint32_t count, const struct kui_gd_ops *, uint32_t guest_begin,
    uint32_t guest_end);

/* Resident-only initialization after the high stage has validated the entire
 * immutable manifest (including GD session boundaries). Pointers/callbacks and
 * guest bounds must be valid. This keeps duplicate input validation out of the
 * protected low-memory reader; untrusted callers use kui_retail_gd_init. */
void kui_retail_gd_init_validated(struct kui_retail_gd *, const union kui_retail_slot *tracks,
    uint32_t count, const struct kui_gd_ops *, uint32_t guest_begin,
    uint32_t guest_end);
/* Low resident only: _start has zeroed the entire service and the high stage
 * has validated this immutable manifest. Initializes the same state as the
 * validated initializer followed by its disc/session setter, without clearing
 * BSS or constructing an intermediate GD default. */
void kui_retail_gd_init_prepared(struct kui_retail_gd *, const struct kui_retail_manifest *,
    const struct kui_gd_ops *, uint32_t guest_begin, uint32_t guest_end);
/* Configure a validated manifest's disc/session before requests. Protocol
 * INIT/RESET retains this kind; pending commands refuse a reconfiguration. */
void kui_retail_gd_set_disc_type(struct kui_retail_gd *, uint32_t disc_type,
    uint32_t session_lba);

/* Virtual PIOREAD/DMAREAD complete by polling and CPU copy into guest memory;
 * no virtual GD DMA interrupt/callback is generated. The physical sector
 * backend may use SCI DMA independently. CHECK never reads storage.
 * Completion/failure is acknowledged once by CHECK;
 * a subsequent CHECK returns NOT_FOUND. ABORT retains completed chunk bytes.
 * MISC and stream functions are not handled here, except Windows CE's DMA
 * stream read in the KUI_RETAIL_CE build (see KUI_RETAIL_GD_DMAREAD_STREAM). Callback-clear (r4=0) is a
 * supported no-op; nonzero callback installation is explicitly unsupported.
 * The mode command's four words are virtual drive metadata, not physical SD
 * settings. DATATYPE accepts 2048-byte user data (type0 automatic,1024 Mode1
 * or2048 Mode2 Form1) and complete2352 sectors. No audio playback or CDDA emulation:
 * PLAY/PLAY2 ({start, end, repeat}), PAUSE and RELEASE are accepted and
 * complete at once, silently, so games that play disc audio run without it.
 * GET_VERS writes the 28-byte driver compatibility response at params[0],
 * with a trailing state byte (not a C-string terminator). It performs no I/O
 * and reports zero disc-transfer bytes, following the BIOS command contract.
 * GETSCD params are {format, capacity, destination}. Formats 0/1 synthesize
 * standard index-1 Q position from the GDI track map and last completed read;
 * format 2 reports an unavailable catalog. No captured subchannels or audio
 * playback are implied. Unsupported formats and zero capacities are rejected.
 */
int32_t kui_retail_gd_dispatch(struct kui_retail_gd *, uint32_t r4,
    uint32_t r5, uint32_t r6, uint32_t r7);
#ifdef KUI_RETAIL_GD_ASYNC
/* Background readers (built with KUI_RETAIL_GD_ASYNC): EXEC leaves a pending
 * PIOREAD/DMAREAD to the adapter, which writes the destination itself and
 * reports here: sectors is the request's total of complete sectors so far,
 * error a nonzero KUI_GD_ERROR_* that ends it. The request completes when
 * every sector is delivered; CHECK then reports it as usual. Calls for
 * anything but a pending read are ignored. ops.read is never called. */
void kui_retail_gd_progress(struct kui_retail_gd *, uint32_t sectors, uint32_t error);
#ifdef KUI_RETAIL_CE
/* Windows CE's background reader also fills a DMA stream's transfers: total
 * is the request's output bytes written so far (in this transfer and the
 * earlier ones), error a nonzero KUI_GD_ERROR_* that ends the request. A
 * transfer whose bytes are all written raises the DMA end interrupt; the
 * request's last bytes, or an error, complete it with the drive's. In this
 * build DMA_TRANSFER and DMA_CHECK move nothing themselves, and a completed
 * or failed PIOREAD/DMAREAD (kui_retail_gd_progress) raises the drive's
 * interrupt too. */
void kui_retail_gd_stream_progress(struct kui_retail_gd *, uint32_t total, uint32_t error);
#endif
#endif

#endif
