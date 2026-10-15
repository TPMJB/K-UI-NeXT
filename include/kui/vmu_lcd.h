/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_LCD_H
#define KUI_VMU_LCD_H
#include <stdint.h>
#define KUI_VMU_LCD_WIDTH 48u
#define KUI_VMU_LCD_HEIGHT 32u
#define KUI_VMU_LCD_BYTES 192u
/* Word-backed storage meets vmu_draw_lcd_rotated's alignment and word-read
 * contract. Pixels are top-left row-major, MSB first within each byte. */
struct kui_vmu_lcd_bitmap {uint32_t words[KUI_VMU_LCD_BYTES / 4u];};
/* Centered K-UI plus version: 5x7 up to eight characters, 3x5 up to twelve
 * per line thereafter. Two lines retain up to 24 characters; longer strings
 * end with '+'. Missing/unsupported characters use '?'. No allocation. */
void kui_vmu_lcd_render(struct kui_vmu_lcd_bitmap *out,const char *version);
/* Native startup only: one nonblocking LCD submission per attached device.
 * No retry, wait, hotplug polling or VMU memory-card access. */
void kui_vmu_lcd_startup(void);
#endif
