/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SPLASH_H
#define KUI_SPLASH_H
#include <stdint.h>
/* Copy the original 640x480 RGB565 startup art to an off-screen framebuffer. */
void kui_splash_draw(uint16_t *frame);
#endif
