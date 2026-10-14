/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_STREAM_MODEL_H
#define KUI_ARMADA_CE_STREAM_MODEL_H
#include <stdbool.h>
#include <stdint.h>

#define KUI_ARMADA_CE_STREAM_RING_BYTES 352768u
#define KUI_ARMADA_CE_STREAM_SECTOR_BYTES 2352u
#define KUI_ARMADA_CE_STREAM_STAGE_BYTES (4u*2352u+64u)
#define KUI_ARMADA_CE_STREAM_GUARD_BYTES 4096u
#define KUI_ARMADA_CE_STREAM_MAX_OBSERVE_MS 999u
enum kui_armada_ce_stream_state {
    KUI_ARMADA_CE_STREAM_STOPPED, KUI_ARMADA_CE_STREAM_PREFILL,
    KUI_ARMADA_CE_STREAM_PLAYING, KUI_ARMADA_CE_STREAM_PAUSED,
    KUI_ARMADA_CE_STREAM_QUIESCING, KUI_ARMADA_CE_STREAM_ENDED,
    KUI_ARMADA_CE_STREAM_FAULT
};
enum kui_armada_ce_stream_result {
    KUI_ARMADA_CE_STREAM_OK, KUI_ARMADA_CE_STREAM_ARGUMENT,
    KUI_ARMADA_CE_STREAM_STATE, KUI_ARMADA_CE_STREAM_BUSY,
    KUI_ARMADA_CE_STREAM_NOTHING, KUI_ARMADA_CE_STREAM_STALE,
    KUI_ARMADA_CE_STREAM_OVERFLOW, KUI_ARMADA_CE_STREAM_IO,
    KUI_ARMADA_CE_STREAM_CURSOR, KUI_ARMADA_CE_STREAM_AGE,
    KUI_ARMADA_CE_STREAM_UNDERRUN, KUI_ARMADA_CE_STREAM_EOF
};
/* Immutable validated intervals; ends are exclusive LBAs. Gaps and data
 * tracks cannot silently become audio. One-based sequential track numbers. */
struct kui_armada_ce_stream_track { uint32_t number,control,first_lba,end_lba; };
struct kui_armada_ce_stream_read {
    uint32_t epoch,selection,id,track,lba,sectors,skip,bytes;
};
struct kui_armada_ce_stream_refill {
    uint32_t epoch,selection,id,ring_offset,bytes,audio_bytes,pad_bytes;
};
struct kui_armada_ce_stream_model {
    const struct kui_armada_ce_stream_track *tracks;
    uint32_t track_count,epoch,selection,sequence,ring_epoch;
    uint32_t first_lba,end_lba,repeats,staged;
    uint32_t next_first_lba,next_end_lba,next_repeats,next_selection;
    uint32_t last_play,write_cursor,last_ms,recoveries;
    uint64_t origin,audible,fetch,produced,consumed,finite_end;
    enum kui_armada_ce_stream_state state,after_stop;
    bool infinite,native_running,stop_needed,initialized,pending_play;
    struct kui_armada_ce_stream_read read;
    struct kui_armada_ce_stream_refill refill;
};
enum kui_armada_ce_stream_result kui_armada_ce_stream_init(
    struct kui_armada_ce_stream_model *,const struct kui_armada_ce_stream_track *,uint32_t);
/* PLAY20 track endpoints are inclusive; PLAY21 FAD endpoints are half-open.
 * Repeats0 plays once,1..14 add passes,15 repeats indefinitely. PAUSE22 and
 * RELEASE23 retain selection; RELEASE resumes. STOP33 clears selection.
 * A new epoch revokes every old read/refill; selection changes only on PLAY.
 * Caller must observe the native cursor before PAUSE when practical. */
enum kui_armada_ce_stream_result kui_armada_ce_stream_request(
    struct kui_armada_ce_stream_model *,uint32_t command,uint32_t first,uint32_t end,uint32_t repeat);
/* Source work uses complete raw sectors with a leading skip. Before commit,
 * append exactly ticket.bytes from that raw span to caller-owned staging.
 * Validate BEFORE any copy. Drain committed refill.audio_bytes from staging
 * only AFTER successful native Unlock, retaining its <64-byte carry. */
enum kui_armada_ce_stream_result kui_armada_ce_stream_read_plan(
    struct kui_armada_ce_stream_model *,uint32_t max_sectors,struct kui_armada_ce_stream_read *);
enum kui_armada_ce_stream_result kui_armada_ce_stream_read_validate(
    const struct kui_armada_ce_stream_model *,const struct kui_armada_ce_stream_read *);
enum kui_armada_ce_stream_result kui_armada_ce_stream_read_complete(
    struct kui_armada_ce_stream_model *,const struct kui_armada_ce_stream_read *,bool,uint32_t bytes);
/* A refill never wraps the native ring and is always64-byte aligned. Its
 * audio prefix is followed by zero padding only after finite source EOF. */
enum kui_armada_ce_stream_result kui_armada_ce_stream_refill_plan(
    struct kui_armada_ce_stream_model *,uint32_t max_bytes,struct kui_armada_ce_stream_refill *);
enum kui_armada_ce_stream_result kui_armada_ce_stream_refill_validate(
    const struct kui_armada_ce_stream_model *,const struct kui_armada_ce_stream_refill *);
enum kui_armada_ce_stream_result kui_armada_ce_stream_refill_complete(
    struct kui_armada_ce_stream_model *,const struct kui_armada_ce_stream_refill *,bool);
bool kui_armada_ce_stream_can_start(const struct kui_armada_ce_stream_model *);
enum kui_armada_ce_stream_result kui_armada_ce_stream_start_complete(
    struct kui_armada_ce_stream_model *,uint32_t epoch,uint32_t now_ms,bool);
/* Cursor values are native BYTE positions, four-byte stereo-frame aligned.
 * now_ms must be a certified monotonic elapsed clock; a stale observation
 * never authorizes a refill. ring_epoch identifies the active native lease
 * even when a newer control epoch has asked that lease to stop. */
enum kui_armada_ce_stream_result kui_armada_ce_stream_observe(
    struct kui_armada_ce_stream_model *,uint32_t ring_epoch,uint32_t play,uint32_t write,uint32_t now_ms);
enum kui_armada_ce_stream_result kui_armada_ce_stream_stop_complete(
    struct kui_armada_ce_stream_model *,uint32_t epoch,bool);
#endif
