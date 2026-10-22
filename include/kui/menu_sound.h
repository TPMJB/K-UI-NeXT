/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_MENU_SOUND_H
#define KUI_MENU_SOUND_H
#include <stdbool.h>
enum kui_menu_sound { KUI_MENU_SOUND_MOVE, KUI_MENU_SOUND_CONFIRM };
/* Call once from the I/O worker after startup audio has finished, before the
 * shell starts using effects. Calls after successful initialization are no-ops.
 * No filesystem access; owns one reserved AICA channel and 4,736 sample bytes. */
bool kui_menu_sound_init(void);
/* Configuration and play are UI-thread calls; serialize shutdown with them.
 * The caller must stop menu effects before global sound/BIOS shutdown. */
void kui_menu_sound_config(bool enabled,unsigned volume_percent);
void kui_menu_sound_play(enum kui_menu_sound effect);
/* Frees this module's resources only. Never shuts down music/global audio. */
void kui_menu_sound_shutdown(void);
#endif
