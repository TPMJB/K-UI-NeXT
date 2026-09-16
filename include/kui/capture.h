/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CAPTURE_H
#define KUI_CAPTURE_H
#include "kui/hash.h"
#include "kui/probe.h"
#define KUI_CAPTURE_PROFILE "gdi-raw2352-typegap150-v1"
#define KUI_CAPTURE_CHUNK 32u
#define KUI_CAPTURE_RETRIES 10u
#define KUI_CHECKPOINT_SECTORS 4096u
#define KUI_CHECKPOINT_BYTES 4096u
struct kui_capture_track { uint32_t number, control, session, start, end, toc_end; };
struct kui_capture_plan { struct kui_capture_track tracks[99]; unsigned count; uint64_t bytes; };
bool kui_plan_tracks(const struct kui_toc sessions[2], struct kui_capture_plan *out);
enum kui_read_result { KUI_READ_OK, KUI_READ_RETRY, KUI_READ_FATAL };
enum kui_capture_mode { KUI_CAPTURE_NEW, KUI_CAPTURE_RESUME, KUI_CAPTURE_VERIFY };
enum kui_capture_result { KUI_CAPTURE_FAILED, KUI_CAPTURE_STOPPED, KUI_CAPTURE_COMPLETE };
enum kui_capture_phase { KUI_IDENTIFY, KUI_PREFIX_CHECK, KUI_CAPTURING, KUI_VERIFYING, KUI_FINISHED };
struct kui_capture_progress {
    enum kui_capture_phase phase;
    unsigned track, tracks;
    uint32_t fad, retries;
    uint64_t done, total, committed, elapsed_ms;
};
struct kui_capture_ops {
    void *ctx;
    enum kui_read_result (*read)(void *, uint32_t fad, unsigned sectors, uint8_t *out);
    bool (*cancelled)(void *);
    uint64_t (*now_ms)(void *);
    void (*progress)(void *, const struct kui_capture_progress *);
    kui_log_fn log;
    const char *build;
};
struct kui_checkpoint {
    uint64_t sequence;
    uint8_t identity[32];
    uint32_t count, retries;
    char build[13];
    struct { uint32_t sectors, crc32; uint8_t sha256[32]; } track[99];
};
void kui_checkpoint_encode(const struct kui_checkpoint *state, uint8_t record[KUI_CHECKPOINT_BYTES]);
bool kui_checkpoint_decode(const uint8_t record[KUI_CHECKPOINT_BYTES], const struct kui_capture_plan *plan,
    const uint8_t identity[32], struct kui_checkpoint *out);
/* Sole worker owns FatFs and optical I/O. Caller connects/disconnects media.
 * Mount/unmount are handled here; all output is in an exclusively created job.
 */
enum kui_capture_result kui_capture(const struct kui_capture_plan *plan,
    const struct kui_capture_ops *ops, enum kui_capture_mode mode);
#endif
