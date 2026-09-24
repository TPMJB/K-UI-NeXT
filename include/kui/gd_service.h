/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GD_SERVICE_H
#define KUI_GD_SERVICE_H

#include <stdint.h>

#define KUI_GD_VECTOR_ADDRESS 0x8c0000bcu
#define KUI_GD_TRACK_MAX 99u
#define KUI_GD_TOC_BYTES 408u
#define KUI_GD_MAX_READ_SECTORS 64u
#define KUI_GD_FAD_OFFSET 150u

enum kui_gd_function {
    KUI_GD_REQUEST = 0, KUI_GD_CHECK = 1, KUI_GD_EXEC = 2,
    KUI_GD_INIT = 3, KUI_GD_DRIVE = 4, KUI_GD_DMA_CALLBACK = 5,
    KUI_GD_DMA_TRANSFER = 6, KUI_GD_DMA_CHECK = 7, KUI_GD_ABORT = 8,
    KUI_GD_RESET = 9, KUI_GD_DATATYPE = 10, KUI_GD_PIO_CALLBACK = 11,
    KUI_GD_PIO_TRANSFER = 12, KUI_GD_PIO_CHECK = 13
};
enum kui_gd_command {
    KUI_GD_PIOREAD = 16, KUI_GD_DMAREAD = 17, KUI_GD_GETTOC2 = 19,
    KUI_GD_COMMAND_INIT = 24, KUI_GD_NOP = 29, KUI_GD_STOP = 33
};
enum kui_gd_status {
    KUI_GD_FAILED = -1, KUI_GD_NOT_FOUND = 0,
    KUI_GD_PROCESSING = 1, KUI_GD_COMPLETED = 2
};
/* Failure detail is K-UI's diagnostic field, not a claim about firmware
 * error/sense fidelity. Unsupported commands are rejected at submission. */
enum kui_gd_error {
    KUI_GD_ERROR_NONE, KUI_GD_ERROR_IO, KUI_GD_ERROR_CANCELLED,
    KUI_GD_ERROR_MEMORY
};
struct kui_gd_track {
    uint32_t number, control, start_lba, end_lba; /* exclusive */
};
struct kui_gd_ops {
    void *context;
    /* Addresses here are normalized to cached P1. The core checks the whole
     * range lies in [guest_begin,guest_end); the adapter may restrict further.
     * Host tests map guest numbers to real host pointers without truncation. */
    uint8_t *(*map)(void *, uint32_t address, uint32_t bytes, int writing);
    /* check performs no I/O. Return zero only for a fully supported range.
     * read returns zero only after producing every requested byte. */
    int (*check)(void *, uint32_t lba, uint32_t count, uint32_t sector_bytes);
    int (*read)(void *, uint32_t lba, uint32_t count, uint32_t sector_bytes,
                void *output);
};
struct kui_gd_service {
    struct kui_gd_ops ops;
    struct kui_gd_track tracks[KUI_GD_TRACK_MAX];
    uint32_t track_count, guest_begin, guest_end;
    uint32_t sector_part, track_type, sector_bytes;
    uint32_t token, command, lba, count, destination, area, request_bytes;
    uint32_t completed_bytes, error, pending, executing, initialized;
    int32_t status;
};

/* Tracks are copied after validation. guest bounds must be a P1 main-RAM
 * interval excluding all resident code, data and stacks; firmware's low
 * 64 KiB is never an allowed guest output. Neither init nor check reads SD. */
int kui_gd_service_init(struct kui_gd_service *, const struct kui_gd_track *,
    uint32_t count, const struct kui_gd_ops *, uint32_t guest_begin,
    uint32_t guest_end);

/* Native hook passes SH-4 r4,r5,r6,r7 unchanged. GD uses r6=0 and r7=function;
 * r0 is the signed return. The shared-vector MISC superfunction is rejected
 * here; preserving/forwarding it belongs to the native hook.
 *
 * REQUEST returns a positive token or 0. params: READ={FAD,count,dst,test=0},
 * GETTOC2={area(0/1),dst}. CHECK returns status and writes {err1,err2,bytes,ata}.
 * DRIVE writes {paused=1,GD-ROM=0x80}, or busy=0 while pending. DATATYPE words
 * {rw,part,type,size}: accepts {0,0x2000,1024,2048} or {0,0x1000,0,2352};
 * rw=1 reads the current mode. EXEC performs queued I/O; CHECK never does.
 * ABORT succeeds only on a queued command and produces FAILED/CANCELLED.
 * INIT/RESET invalidate outstanding handles and restore Mode 1/2048.
 *
 * DMAREAD currently supplies bytes through the same CPU-copy backend as PIO.
 * It does not generate a hardware interrupt. Streaming/callback functions
 * 5..7 and 11..15 return -1. This is a bounded BIOS-ABI proof, not a complete
 * retail-compatible implementation. */
int32_t kui_gd_service_dispatch(struct kui_gd_service *, uint32_t r4,
    uint32_t r5, uint32_t r6, uint32_t r7);

#endif
