/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_READ_BOUNDED_H
#define KUI_VMU_READ_BOUNDED_H
#include <stdint.h>
#include <dc/maple.h>

enum kui_vmu_read_bounded_stage {
    KUI_VMU_READ_ARGUMENT=1,KUI_VMU_READ_LOCK=2,KUI_VMU_READ_FRAME=3,
    KUI_VMU_READ_QUEUE=4,KUI_VMU_READ_WAIT=5,KUI_VMU_READ_TYPE=6,
    KUI_VMU_READ_LENGTH=7,KUI_VMU_READ_FUNCTION=8,KUI_VMU_READ_LOCATION=9,
    KUI_VMU_READ_COMPLETE=10
};
struct kui_vmu_read_bounded_diagnostic {
    uint32_t block,stage,frame_state,queued,lock_attempts;
    int result,wait_result,response;
    uint32_t data_len,expected_function,actual_function,expected_location,actual_location;
    uint32_t raw_crc_before,raw_crc_after,copy_crc;
};
/* The synchronous caller may inspect this after a failure. It is reset for
 * every read, contains no save bytes, and is never written by a late callback.
 * response == -127 means that no response header was observed. Payload words
 * are inspected only for a DATATRF with the complete 130-word payload.
 * CRC32 observations are made only after all headers match: raw payload before
 * and after copying, then the output before frame release. They do not alter
 * the helper's admission result. The importer independently checks both reads. */
const struct kui_vmu_read_bounded_diagnostic *kui_vmu_read_bounded_last(void);

/* Synchronous read only, outside IRQ context. The caller keeps the physical
 * device alive and serializes VMU filesystem access. The output is a separate
 * caller-owned 512-byte buffer. The helper submits one MEMCARD BREAD request;
 * KOS may requeue AGAIN responses internally. No block write, reset or
 * filesystem mutation is requested.
 * With the frame exclusively owned and initialized, the cached alias of its
 * aligned receive area is invalidated before request construction, preventing
 * stale calloc cache lines from later writing over uncached DMA replies.
 * Response type, complete 130-word payload, function and block location must
 * all match before any output store. Every refusal preserves all 512 bytes.
 * Frame acquisition waits at most 100ms for an existing owner; an ordinary
 * transient busy frame is not a read error. Returns MAPLE_EOK, MAPLE_EINVALID,
 * MAPLE_ETIMEOUT or MAPLE_EFAIL. A timeout
 * ends this import. An in-flight frame remains locked until KOS's late
 * completion callback releases it, so its DMA buffers cannot be reused. */
int kui_vmu_read_bounded(maple_device_t *,uint16_t block,uint8_t out[512]);
#endif
