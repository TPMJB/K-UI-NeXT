/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SALVAGE_DISPLAY_H
#define KUI_SALVAGE_DISPLAY_H
#include "kui/apps.h"
#include "kui/salvage.h"

/* Presentation only. Per-item attempts and the retained last problem come
 * from the worker; the cumulative read-call total is never a sector budget. */
void kui_salvage_display_status(struct kui_app_status *out,
    const struct kui_salvage_status *status);
#endif
