/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_PROBE_H
#define KUI_GAMES_PROBE_H
#include "kui/runtime.h"

#define KUI_GAMES_PROBE_PACKAGE "0:/KUI/apps/games/probe.kui"
/* Worker-owned, read-only preparation. Validates the executable envelope and
 * maps only the fixed original fixture. On success caller owns image->data;
 * no filesystem, open file or SD connection survives this call. */
bool kui_games_probe_prepare(struct kui_runtime_image *image,
    kui_log_fn log, kui_cancel_fn cancelled);
#endif
