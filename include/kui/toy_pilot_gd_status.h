/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_GD_STATUS_H
#define KUI_TOY_PILOT_GD_STATUS_H
#include "kui/toy_pilot.h"
#include "kui/retail_gd.h"

/* Scalar protocol values only; no sound, card or SDK access. A track remains
 * logically playing while its finite banks are being prepared or exchanged.
 * Reporting PAUSED during such a gap makes the title discard its music ID.
 * The caller owns the GD serialization and supplies the linked worker's RAM
 * snapshot. Return the GETSCD audio-status byte, and refresh drive/position.
 * Values follow the documented KallistiOS GD syscall contract: drive 1/2/3/9
 * and audio 0x11..0x15. This implementation is independently authored. */
static inline uint32_t kui_toy_pilot_gd_project(struct kui_retail_gd *s,
    const struct kui_toy_pilot_snapshot *p) {
    if(!p) return 0x15u;
    uint32_t command=p->command, state=p->state, audio=0x15u;
    uint32_t track=p->track, fad=p->position_fad;
    bool pending=p->generation!=p->applied_generation;
    if(p->fault || state==KUI_TOY_PILOT_FAULT) {
        s->drive_status=9u;audio=0x14u;
    } else if(command==KUI_TOY_PILOT_RESET) {
        s->drive_status=1u;return 0x15u;
    } else if(command==KUI_GD_STOP) {
        s->drive_status=2u;audio=0x13u;
    } else if(pending && command==KUI_RETAIL_GD_PLAY) {
        s->drive_status=3u;audio=0x11u;
        track=p->parameters[0];fad=0; /* A newly accepted play starts here. */
    } else if(pending && command==KUI_RETAIL_GD_PAUSE) {
        s->drive_status=1u;audio=0x12u;
    } else if(pending && command==KUI_RETAIL_GD_RELEASE) {
        s->drive_status=track?3u:1u;audio=track?0x11u:0x15u;
    } else if(state==KUI_TOY_PILOT_PREFILL || state==KUI_TOY_PILOT_START_WAIT ||
              state==KUI_TOY_PILOT_PLAYING) {
        s->drive_status=3u;audio=0x11u;
    } else if(state==KUI_TOY_PILOT_PAUSED) {
        s->drive_status=1u;audio=0x12u;
    } else if(state==KUI_TOY_PILOT_EOF) {
        s->drive_status=1u;audio=0x13u;
    } else if(state==KUI_TOY_PILOT_STOPPED) {
        /* A startup RELEASE without an audio source is an idle no-op.
         * Explicitly project it, rather than retaining a previous query's
         * PLAYING drive value after the worker completes the command. */
        s->drive_status=track?2u:1u;audio=track?0x13u:0x15u;
    } else if(state==KUI_TOY_PILOT_OFF && track) {
        s->drive_status=2u;audio=0x13u;
    }
    /* The immutable admitted map owns the range. Do not let a stale snapshot
     * move position into another track, or replace a data position before the
     * pilot has actually selected an audio track. EOF is the last audio FAD,
     * not the following track's first sector. */
    if(track && track<=s->track_count) {
        const struct kui_retail_track *t=&s->tracks[track-1u].track;
        if(!(kui_retail_track_control(t)&4u) && t->start_lba<t->end_lba) {
            uint32_t first=t->start_lba+150u,last=t->end_lba+149u;
            if(fad<first) fad=first;
            if(fad>last) fad=last;
            s->position_lba=fad-150u;
        }
    }
    return audio;
}
#endif
