/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SHELL_FONT_H
#define KUI_SHELL_FONT_H
#include <stdbool.h>
#include <stdint.h>

#define KUI_SHELL_FONT_SMALL_HEIGHT 18u
#define KUI_SHELL_FONT_LARGE_HEIGHT 24u
#define KUI_SHELL_FONT_LETTER_SPACING 1u
/* ASCII 32..126, proportional advances plus 1px between characters; no trailing
 * gap. Unsupported bytes use '?'. NULL/empty strings have width zero. Width
 * saturates at UINT_MAX. Glyphs fit inside their advance, with no kerning. */
unsigned kui_shell_font_width(const char *text,bool large);
/* Draw into a 640x480 RGB565 framebuffer. x/y are the line box's top-left;
 * clipping includes negative/extreme coordinates. No allocation, I/O or KOS
 * calls. Four-bit coverage blends with existing pixels; transparency skips
 * writes and full coverage writes the exact requested color. */
void kui_shell_font_draw(uint16_t *frame,int x,int y,uint16_t rgb565,
                         const char *text,bool large);
#endif
