/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/splash.h"
#include <string.h>
#include "../../build/boot_splash_pixels.inc"

_Static_assert(sizeof(kui_boot_splash_pixels)==640u*480u*sizeof(uint16_t),
    "Boot artwork must be 640x480 RGB565");

void kui_boot_splash_draw(uint16_t *frame) {
    if(frame) memcpy(frame,kui_boot_splash_pixels,sizeof(kui_boot_splash_pixels));
}
void kui_boot_splash_draw_dimmed(uint16_t *frame) {
    if(!frame) return;
    /* Read the constant pixels in main RAM, not the uncached framebuffer.
     * This matches the original RGB565 dimming without a VRAM readback pass. */
    for(unsigned i=0;i<640u*480u;i++)
        frame[i]=(uint16_t)((kui_boot_splash_pixels[i]&0xe79cu)>>2);
}
