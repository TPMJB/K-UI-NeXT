/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_CONTROL_H
#define KUI_CDDA_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

/* Independent controlled-runtime semantics, not a BIOS command-number ABI. */
enum kui_cdda_control_state {
    KUI_CDDA_CONTROL_STOPPED, KUI_CDDA_CONTROL_PLAYING,
    KUI_CDDA_CONTROL_PAUSED, KUI_CDDA_CONTROL_EOF, KUI_CDDA_CONTROL_FAULT
};
enum kui_cdda_control_result {
    KUI_CDDA_CONTROL_OK, KUI_CDDA_CONTROL_INVALID, KUI_CDDA_CONTROL_STATE,
    KUI_CDDA_CONTROL_BUSY, KUI_CDDA_CONTROL_STALE, KUI_CDDA_CONTROL_IO,
    KUI_CDDA_CONTROL_ENDED, KUI_CDDA_CONTROL_OVERFLOW
};
enum kui_cdda_control_command {
    KUI_CDDA_CONTROL_PLAY, KUI_CDDA_CONTROL_STOP, KUI_CDDA_CONTROL_PAUSE,
    KUI_CDDA_CONTROL_RESUME, KUI_CDDA_CONTROL_SEEK, KUI_CDDA_CONTROL_LOOP,
    KUI_CDDA_CONTROL_STATUS
};
struct kui_cdda_control_request {
    enum kui_cdda_control_command command;
    uint32_t first, end; /* PLAY range, exclusive end, track-relative frames. */
    uint32_t frame;      /* SEEK target; exact end means EOF. */
    bool repeat;        /* PLAY or LOOP. */
};
struct kui_cdda_control_action {
    enum kui_cdda_control_command command;
    enum kui_cdda_control_state target_state;
    uint32_t epoch, first, end, frame, loops;
    bool repeat, pending;
};
struct kui_cdda_control_status {
    enum kui_cdda_control_state state;
    uint32_t first, end, frame, loops, epoch, pending_epoch;
    bool repeat, pending;
};
struct kui_cdda_control {
    struct kui_cdda_control_status current;
    struct kui_cdda_control_action action;
    uint32_t source_frames, generation, origin_frame, origin_loops, last_played;
    bool initialized;
};

/* Nonzero source length. Invalid calls leave all supplied state unchanged. */
enum kui_cdda_control_result kui_cdda_control_init(struct kui_cdda_control *, uint32_t source_frames);
/* Request validates and stages an action; committed cursor/state remain valid
 * until matching complete(). STOP/PLAY may replace a pending action, canceling
 * its completion token. Other commands are BUSY during pending work. STATUS is
 * pure; repeated STOP/PAUSE/RESUME/LOOP can return pending=false without work.
 * A generation is never reused: exhaustion refuses further actions. */
enum kui_cdda_control_result kui_cdda_control_request(struct kui_cdda_control *,
    const struct kui_cdda_control_request *, struct kui_cdda_control_action *);
/* Only the currently staged epoch can complete, exactly once. Stale callbacks
 * cannot commit a canceled command. Hardware/I/O failure commits FAULT and
 * returns IO; the caller must stop its owned audio resources. */
enum kui_cdda_control_result kui_cdda_control_complete(struct kui_cdda_control *, uint32_t epoch, bool success);
/* A fatal active playback/I/O error also commits FAULT. The matching pending
 * epoch, if any, is completed as a failure; a stale epoch cannot fault a newer
 * session. Caller still performs the bounded owned-resource stop. */
enum kui_cdda_control_result kui_cdda_control_fail(struct kui_cdda_control *, uint32_t epoch);
/* Cumulative frames ACTUALLY PLAYED since the last completed action. Never pass
 * a read/prefetch cursor. Values cannot move backwards or wrap. EOF clamps at
 * exclusive end and returns ENDED; caller must stop audio at that point. */
enum kui_cdda_control_result kui_cdda_control_observe(struct kui_cdda_control *, uint32_t epoch, uint32_t played);
/* Pure snapshot, including pending action epoch; no hardware phase queries. */
enum kui_cdda_control_result kui_cdda_control_status(const struct kui_cdda_control *, struct kui_cdda_control_status *);

#endif
