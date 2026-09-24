/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_RETAIL_H
#define KUI_GAMES_RETAIL_H
#include "kui/runtime.h"
#define KUI_GAMES_RETAIL_PACKAGE "0:/KUI/apps/games/retail-boot.kui"
/* Single storage worker only. Prepare one native GD-ROM launch;
 * complete IP/boot CRCs and bounded physical extent map are read-only. Success
 * owns a patched stage package with all files closed and SD disconnected.
 * Cancellation/failure releases the package and leaves image empty. */
bool kui_games_retail_prepare(const char *path,struct kui_runtime_image *image,
    kui_log_fn log,kui_cancel_fn cancelled);
#endif
