/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_RETURN_FAULT_DISPLAY_H
#define KUI_SMASH_RETURN_FAULT_DISPLAY_H
#include <stdint.h>
/* Bounded return failure display. Draw RET + eight hex digits, then eight
 * detail hex digits, in the current scanout without trusting runtime data.
 * Unsupported/unsafe scanout geometry gets a red border only. */
void kui_smash_return_fault_display(uint32_t reason,uint32_t detail);
/* Successful return checkpoints stay in RAM and never touch scanout. */
extern volatile uint32_t kui_smash_return_progress_state[2];
void kui_smash_return_progress(uint32_t phase,uint32_t detail);
#endif
