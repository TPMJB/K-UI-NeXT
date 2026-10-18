/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_CHD_H
#define KUI_GAMES_CHD_H
#include "kui/games.h"
/* Sole storage worker, already mounted. Read-only: validate the source profile
 * and a published cache, if present. path remains the user's CHD identity. */
bool kui_games_chd_inspect_mounted(const char *path, struct kui_games_detail *out,
    kui_log_fn log, kui_cancel_fn cancel);
/* Explicit user action. Creates a new verified cache without changing the
 * CHD, existing caches, game settings, or detached readers. Owns the mount. */
bool kui_games_chd_prepare(const char *path, struct kui_games_detail *out,
    kui_log_fn log, kui_cancel_fn cancel, kui_app_progress_fn progress);
#endif
