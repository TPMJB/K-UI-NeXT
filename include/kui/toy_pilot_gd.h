/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_GD_H
#define KUI_TOY_PILOT_GD_H
#include "kui/retail_gd.h"
#include "kui/toy_pilot.h"

/* A completed/cancelled data command still owns its native handle until a
 * valid CHECK acknowledges it. Neither predicate observes audio state. */
static inline bool kui_toy_pilot_gd_data_owned(const struct kui_retail_gd *s) {
    return s->command==KUI_GD_PIOREAD || s->command==KUI_GD_DMAREAD;
}
static inline bool kui_toy_pilot_gd_data_pending(const struct kui_retail_gd *s) {
    return s->pending && kui_toy_pilot_gd_data_owned(s);
}

static inline bool kui_toy_pilot_gd_audio_pending(const struct kui_retail_gd *s) {
    return s->pending && (s->command-20u<=4u || s->command==KUI_GD_STOP);
}

/* First EXEC acknowledges mailbox acceptance, not hardware application.
 * The game's GD polling/interrupt callbacks cannot run the sound worker.
 * Waiting for applied_generation here can therefore prevent the game from
 * ever reaching that worker. CHECK retains its normal terminal-handle
 * ownership. Actual application remains independently visible in telemetry.
 * Call only for a pending audio command, with the GD serialization held. */
static inline void kui_toy_pilot_gd_acknowledge(struct kui_retail_gd *s,
    const struct kui_toy_pilot_snapshot *p) {
    uint32_t error=!p || p->fault?KUI_GD_ERROR_UNAVAILABLE:
        p->generation!=s->count?KUI_GD_ERROR_CANCELLED:0u;
    /* The ordinary scalar EXEC also owns INIT datatype and STOP drive state.
     * Audio IDs here cannot enter its image-read paths. */
    if(!error) (void)kui_retail_gd_dispatch(s,0,0,0,KUI_GD_EXEC);
    else { s->pending=0;s->error=error;s->status=KUI_GD_FAILED; }
    if(p && p->position_fad>=150u) s->position_lba=p->position_fad-150u;
}
#endif
