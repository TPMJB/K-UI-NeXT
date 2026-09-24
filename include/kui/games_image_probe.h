/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_IMAGE_PROBE_H
#define KUI_GAMES_IMAGE_PROBE_H
#include "kui/runtime.h"
#define KUI_GAMES_IMAGE_PROBE_PACKAGE "0:/KUI/apps/games/image-probe.kui"
/* Single storage worker only. Read-only preparation of a selected GDI;
 * success transfers the patched executable allocation to the caller, with
 * all files closed and SD disconnected. Failure leaves image empty. */
bool kui_games_image_probe_prepare(const char *path,struct kui_runtime_image *image,
    kui_log_fn log,kui_cancel_fn cancelled);
#endif
