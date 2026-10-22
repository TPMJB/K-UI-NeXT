/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GD_PLAY_H
#define KUI_GD_PLAY_H

/* Confirmed, main-thread-only exit to the console's normal boot sequence.
 * The caller must first stop the I/O worker, finish/unmount storage work and
 * shut down menu/CD audio. No app operation may run concurrently with this.
 * KOS performs its normal shutdown before rebooting the console BIOS.
 * This is not an independent game loader or a region/autostart bypass. */
void kui_gd_play_boot(void) __attribute__((noreturn));

#endif
