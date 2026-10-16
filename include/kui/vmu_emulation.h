/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_EMULATION_H
#define KUI_VMU_EMULATION_H

#include <stddef.h>
#include <stdint.h>

#define KUI_VMU_EMU_BLOCK_BYTES 512u
#define KUI_VMU_EMU_BLOCK_COUNT 256u
#define KUI_VMU_EMU_PHASE_BYTES 128u
#define KUI_VMU_EMU_MAX_RESPONSE_BYTES 524u
/* CPU/DMA representation, as in pinned KOS dc/maple.h. */
#define KUI_VMU_EMU_FUNC_MEMCARD UINT32_C(0x02000000)

enum kui_vmu_emulation_io_result {
    KUI_VMU_EMU_IO_OK = 0,
    KUI_VMU_EMU_IO_BUSY = 1,
    KUI_VMU_EMU_IO_ERROR = -1
};

struct kui_vmu_emulation_io {
    void *context;
    int (*read_block)(void *context, uint16_t block, uint8_t data[512]);
    /* OK means all 512 bytes are durably committed and readable, including
     * after power loss/reopen. BUSY means retry this SAME immutable block.
     * Implementations must be idempotent: an error may follow a completed
     * device write whose acknowledgement was lost. No deferred RAM ACKs.
     * Neither callback may retain its data pointer after returning. */
    int (*commit_block)(void *context, uint16_t block, const uint8_t data[512]);
};

/* Caller-owned, allocation-free, single caller only. Initialize before use.
 * One incomplete 512-byte transaction is staged; a read sees committed data.
 * No entire card, physical controller state, LCD or clock is emulated. */
typedef struct kui_vmu_emulation {
    struct kui_vmu_emulation_io io;
    uint8_t staged[512];
    uint16_t staged_block;
    uint16_t committed_block;
    uint16_t read_block;
    uint8_t address;
    uint8_t phases;
    uint8_t active;
    uint8_t committed_valid;
    uint8_t read_valid;
    uint8_t commit_attempted;
    uint8_t killed;
    uint8_t initialized;
} kui_vmu_emulation_t;

/* address is a single Maple subunit, e.g. 0x01=A1 or 0x42=B2. */
int kui_vmu_emulation_init(kui_vmu_emulation_t *state, uint8_t address,
                           const struct kui_vmu_emulation_io *io);
/* Hardware-reset equivalent: discards only the unfinished transaction,
 * reactivates a killed device, and never writes storage. */
void kui_vmu_emulation_reset(kui_vmu_emulation_t *state);

/* Request/response are CPU Maple DMA buffers, NOT serialized wire words:
 * bytes {command, destination, source, payload_words}, followed by payload.
 * MEMCARD function bytes = 00 00 00 02; location bytes = partition, phase,
 * block_hi, block_lo. Save bytes are copied unchanged. No transport XOR CRC
 * byte appears in DMA buffers; hardware/transport must check it separately.
 *
 * Returns 1 with a complete response (or four FF bytes, the Maple hardware
 * timeout marker, after device KILL), 0 for another target/non-host sender,
 * -1 for invalid API arguments, -2 for insufficient output capacity. Set
 * *response_bytes to 0 on all nonhandled paths. For a malformed addressed
 * request with a complete header, responds FILEERR/invalid-length. A truncated
 * header returns -1. Output capacity is checked before state or I/O changes.
 * Buffers must not overlap each other or state; single-thread externally.
 */
int kui_vmu_emulation_handle(kui_vmu_emulation_t *state,
                            const uint8_t *request, size_t request_bytes,
                            uint8_t *response, size_t response_capacity,
                            size_t *response_bytes);

#endif
