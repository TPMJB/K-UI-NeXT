/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_LAUNCH_HISTORY_H
#define KUI_VMU_LAUNCH_HISTORY_H
#include "kui/vmu_history.h"
/* Sole storage worker only. Request immediately before one foreground prepare.
 * The mounted prepare consumes it before any launch/card allocation. End on
 * every prepare outcome and again before returning ownership to the frontend.
 * No view, proof or request may survive that job or a game handoff. */
void kui_vmu_launch_history_request(bool recover);
void kui_vmu_launch_history_begin_mounted(kui_log_fn,kui_cancel_fn);
void kui_vmu_launch_history_end(void);
bool kui_vmu_launch_history_performed(void);
/* Internal optional session hook: one freshly recovered exact-card checkpoint,
 * still under the same mount/device/write epoch, may supply the before-launch
 * checkpoint. Fresh identity and ordinary receipt publication remain required.
 * Zero means use the unchanged complete checkpoint path. */
int kui_vmu_launch_history_reuse(const struct kui_vmu_game_selection *,
    const struct kui_vmu_history_platform *,struct kui_vmu_history_view *,kui_cancel_fn);
int kui_vmu_history_resume_mounted(struct kui_vmu_history_view *,kui_log_fn,kui_cancel_fn);
#endif
