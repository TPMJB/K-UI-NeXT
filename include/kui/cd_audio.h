/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CD_AUDIO_H
#define KUI_CD_AUDIO_H
#include "kui/probe.h"
enum kui_cd_audio_action {KUI_CD_AUDIO_LIST,KUI_CD_AUDIO_PLAY,KUI_CD_AUDIO_PAUSE,
    KUI_CD_AUDIO_RESUME,KUI_CD_AUDIO_STOP};
struct kui_cd_audio_track {unsigned number,seconds;};
struct kui_cd_audio_status {
    bool loaded,playing,paused,poisoned;
    unsigned count,first,last,current;
    struct kui_cd_audio_track tracks[99];
    char message[128];
};
/* All calls belong to the single optical/I/O worker. PLAY takes the actual CD
 * track number, not a zero-based menu index. UI copies the published snapshot.
 * Audio entries on ordinary/enhanced CDs only; data tracks and GD-ROMs are
 * deliberately refused. The listed track numbers may contain gaps. */
void kui_cd_audio_run(enum kui_cd_audio_action action,unsigned track,
    struct kui_cd_audio_status *out,kui_log_fn log,kui_cancel_fn cancel);
/* Stop before any foreground optical operation, including GD Play. A failed
 * stop refuses handoff. Failed command abort permanently poisons this adapter. */
bool kui_cd_audio_stop_for_io(kui_log_fn log);
bool kui_cd_audio_owns_drive(void);
void kui_cd_audio_set_volume(unsigned percent);
/* Idle worker only: synchronous drive-status observation, no command or read. */
void kui_cd_audio_poll(struct kui_cd_audio_status *out,kui_log_fn log);
void kui_cd_audio_status_copy(struct kui_cd_audio_status *out);
#endif
