/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_GAME_INFO_H
#define KUI_VMU_GAME_INFO_H
#include "kui/probe.h"
#include <stdbool.h>

/* Shell-only metadata summary. known never means hardware compatibility:
 * native module, allocation and lifetime admission still run at launch. */
struct kui_vmu_game_info {
    char product[12],label[64],notice[128];
    bool enabled,known;
};
/* Worker owns storage. Read-only: no images, journals, assignments, profiles
 * or preferences are changed, and no card is created or activated. */
void kui_vmu_game_info_run(const char *product,bool native_route,
    struct kui_vmu_game_info *out,kui_log_fn log,kui_cancel_fn cancel);
#endif
