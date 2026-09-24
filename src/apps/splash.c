/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/splash.h"
#include <string.h>
#include "../../build/splash_pixels.inc"
void kui_splash_draw(uint16_t *frame) {
    if(frame) memcpy(frame,kui_splash_pixels,sizeof(kui_splash_pixels));
}
