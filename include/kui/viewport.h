/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VIEWPORT_H
#define KUI_VIEWPORT_H
#include <stdint.h>
/* Horizontal TV safe area without changing the accepted video timing.
 * In-place RGB565 canvas, nearest-pixel sampling; zero inset is a no-op. */
void kui_viewport_inset(uint16_t *pixels,unsigned width,unsigned height,unsigned inset,uint16_t border);
#endif
