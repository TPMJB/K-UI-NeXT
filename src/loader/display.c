/* SPDX-License-Identifier: GPL-3.0-only */
#include "display.h"
#include "kui/shell_font.h"
#include <stddef.h>
#include <stdint.h>

#ifndef KUI_BUILD_ID
#define KUI_BUILD_ID "local-unversioned"
#endif

/* KOS's arch_shutdown has set cable-appropriate 640x480 RGB565 at VRAM offset
 * zero. Reuse only the hardware scanout; the font and renderer live in our
 * resident image. No KOS video pointer or callback survives. */
#define WIDTH 640u
#define HEIGHT 480u
#define BACKGROUND 0x0864u
static uint16_t *const frame = (uint16_t *)(uintptr_t)0xa5000000u;
static unsigned row;

static void number(char out[11], uint32_t value) {
    char reverse[10];
    unsigned n = 0, i;
    do { reverse[n++] = (char)('0' + value % 10u); value /= 10u; } while(value);
    for(i = 0; i < n; i++) out[i] = reverse[n - i - 1];
    out[n] = 0;
}
static void line(const char *text, uint16_t color) {
    /* Keep the header and final status fixed. Shift only the result area if
     * the test suite grows beyond the available lines. */
    if(row > 398u) {
        for(unsigned y = 112; y < 398; y++)
            for(unsigned x = 16; x < 624; x++)
                frame[y * WIDTH + x] = frame[(y + 20) * WIDTH + x];
        row = 398;
    }
    for(unsigned y = row; y < row + 20; y++)
        for(unsigned x = 16; x < 624; x++) frame[y * WIDTH + x] = BACKGROUND;
    kui_shell_font_draw(frame, 20, (int)row, color, text, false);
    row += 20;
}
void kui_loader_display_init(void) {
    for(unsigned i = 0; i < WIDTH * HEIGHT; i++) frame[i] = BACKGROUND;
    kui_shell_font_draw(frame, 20, 16, 0x7fff, "K-UI: resident loader probe", true);
    kui_shell_font_draw(frame, 20, 47, 0xffff, "Build " KUI_BUILD_ID, false);
    kui_shell_font_draw(frame, 20, 70, 0xffff,
        "Own test program / direct SD reads / no KOS", false);
    row = 112;
}
void kui_loader_display_line(const char *text) { line(text, 0xffff); }
void kui_loader_display_number(const char *label, uint32_t value) {
    char result[96], digits[11]; unsigned n = 0;
    number(digits, value);
    while(*label && n < 79) result[n++] = *label++;
    for(unsigned i = 0; digits[i] && n < sizeof(result)-1; i++) result[n++] = digits[i];
    result[n] = 0; line(result, 0xffff);
}
void kui_loader_display_result(const char *label, uint32_t passed, uint32_t detail) {
    char result[96], digits[11];
    const char *prefix = passed ? "PASS  " : "FAIL  ";
    unsigned n = 0;
    while(*prefix) result[n++] = *prefix++;
    while(*label && n < 72) result[n++] = *label++;
    if(!passed) {
        number(digits, detail); result[n++] = ' '; result[n++] = '(';
        for(unsigned i = 0; digits[i] && n < sizeof(result)-2; i++) result[n++] = digits[i];
        result[n++] = ')';
    }
    result[n] = 0;
    line(result, passed ? 0x87f0 : 0xfba0);
}
void kui_loader_display_summary(uint32_t failures, uint32_t blocks) {
    kui_loader_display_number("Post-handoff SD blocks read: ", blocks);
    for(unsigned y = 425; y < HEIGHT; y++)
        for(unsigned x = 0; x < WIDTH; x++) frame[y * WIDTH + x] = BACKGROUND;
    kui_shell_font_draw(frame, 20, 425, failures ? 0xfba0 : 0x87f0,
        failures ? "PROBE FAILED - photograph this screen" : "PROBE PASSED - photograph this screen", false);
    kui_shell_font_draw(frame, 20, 449, 0xffff,
        "Power off/on to return. The SD card was read only.", false);
}
