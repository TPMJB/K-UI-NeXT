/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/shell_font.h"
#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 640
#define HEIGHT 480
#define PIXELS ((size_t)WIDTH * HEIGHT)
#define GUARDS 64u

struct canvas {
    uint16_t before[GUARDS];
    uint16_t pixels[PIXELS];
    uint16_t after[GUARDS];
};
static struct canvas actual, reference;

static void clear(struct canvas *canvas, uint16_t background) {
    for(unsigned i = 0; i < GUARDS; ++i) {
        canvas->before[i] = (uint16_t)(0xa500u + i);
        canvas->after[i] = (uint16_t)(0x5a00u + i);
    }
    for(size_t i = 0; i < PIXELS; ++i) canvas->pixels[i] = background;
}
static void check_guards(const struct canvas *canvas) {
    for(unsigned i = 0; i < GUARDS; ++i) {
        assert(canvas->before[i] == (uint16_t)(0xa500u + i));
        assert(canvas->after[i] == (uint16_t)(0x5a00u + i));
    }
}
static size_t changed(const struct canvas *canvas, uint16_t background) {
    size_t count = 0;
    for(size_t i = 0; i < PIXELS; ++i) count += canvas->pixels[i] != background;
    return count;
}
static void check_noop(bool large) {
    const uint16_t background = 0x35ad;
    clear(&actual, background);
    assert(kui_shell_font_width(NULL, large) == 0);
    assert(kui_shell_font_width("", large) == 0);
    assert(kui_shell_font_width(" ", large) > 0);
    kui_shell_font_draw(actual.pixels, 10, 20, 0xffff, NULL, large);
    kui_shell_font_draw(actual.pixels, 10, 20, 0xffff, "", large);
    kui_shell_font_draw(actual.pixels, 10, 20, 0xffff, "   ", large);
    check_guards(&actual);
    assert(changed(&actual, background) == 0);
}
static void check_ascii(bool large) {
    char all[96]; unsigned sum = 0;
    for(unsigned ch = 32; ch <= 126; ++ch) {
        char glyph[] = {(char)ch, '\0'};
        unsigned width = kui_shell_font_width(glyph, large);
        assert(width > 0 && width < 64);
        sum += width; all[ch - 32] = (char)ch;
        clear(&actual, 0);
        kui_shell_font_draw(actual.pixels, 80, 90, 0xffff, glyph, large);
        check_guards(&actual);
        if(ch == ' ') assert(changed(&actual, 0) == 0);
        else assert(changed(&actual, 0) > 0);
        unsigned height = large ? KUI_SHELL_FONT_LARGE_HEIGHT : KUI_SHELL_FONT_SMALL_HEIGHT;
        for(size_t i = 0; i < PIXELS; ++i) {
            if(actual.pixels[i] == 0) continue;
            unsigned x = (unsigned)(i % WIDTH), y = (unsigned)(i / WIDTH);
            assert(x >= 80 && x < 80 + width && y >= 90 && y < 90 + height);
        }
    }
    all[95] = '\0';
    assert(kui_shell_font_width(all, large) == sum + 94 * KUI_SHELL_FONT_LETTER_SPACING);
    assert(kui_shell_font_width("AV", large) ==
           kui_shell_font_width("A", large) + kui_shell_font_width("V", large) + KUI_SHELL_FONT_LETTER_SPACING);
    assert(kui_shell_font_width("A V", large) ==
           kui_shell_font_width("AV", large) + kui_shell_font_width(" ", large) + KUI_SHELL_FONT_LETTER_SPACING);

    /* The advance accumulator must not silently wrap at 8 or 16 bits. */
    static char long_text[131073];
    memset(long_text, 'W', sizeof(long_text) - 1); long_text[sizeof(long_text) - 1] = '\0';
    unsigned w = kui_shell_font_width("W", large);
    assert(kui_shell_font_width(long_text, large) ==
           (sizeof(long_text) - 1) * w + (sizeof(long_text) - 2) * KUI_SHELL_FONT_LETTER_SPACING);
}
static void check_advances_match_draw(bool large) {
    const char *text = "AgjW ?";
    clear(&reference, 0x39c7); clear(&actual, 0x39c7);
    kui_shell_font_draw(reference.pixels, 20, 30, 0xffff, text, large);
    int x = 20;
    for(size_t i = 0; text[i]; ++i) {
        char glyph[] = {text[i], '\0'};
        kui_shell_font_draw(actual.pixels, x, 30, 0xffff, glyph, large);
        x += (int)kui_shell_font_width(glyph, large) + KUI_SHELL_FONT_LETTER_SPACING;
    }
    assert(x - 20 - (int)KUI_SHELL_FONT_LETTER_SPACING == (int)kui_shell_font_width(text, large));
    check_guards(&reference); check_guards(&actual);
    assert(!memcmp(actual.pixels, reference.pixels, sizeof(actual.pixels)));
}
static void check_fallback(bool large) {
    const unsigned char unsupported[] = {1, 9, 10, 13, 31, 127, 128, 193, 255};
    clear(&reference, 0x0410);
    kui_shell_font_draw(reference.pixels, 20, 30, 0xffff, "?", large);
    for(size_t i = 0; i < sizeof(unsupported); ++i) {
        char text[] = {(char)unsupported[i], '\0'};
        assert(kui_shell_font_width(text, large) == kui_shell_font_width("?", large));
        clear(&actual, 0x0410);
        kui_shell_font_draw(actual.pixels, 20, 30, 0xffff, text, large);
        check_guards(&actual);
        assert(!memcmp(actual.pixels, reference.pixels, sizeof(actual.pixels)));
    }
    const char bytes[] = {'A', (char)0xc3, (char)0xa9, '\n', 'B', '\0'};
    assert(kui_shell_font_width(bytes, large) == kui_shell_font_width("A???B", large));
    clear(&reference, 0x0410); clear(&actual, 0x0410);
    kui_shell_font_draw(reference.pixels, 20, 30, 0xffff, "A???B", large);
    kui_shell_font_draw(actual.pixels, 20, 30, 0xffff, bytes, large);
    check_guards(&actual); check_guards(&reference);
    assert(!memcmp(actual.pixels, reference.pixels, sizeof(actual.pixels)));
}
static void check_clipping(bool large) {
    const char *text = "AgjW_?";
    const uint16_t background = 0x021f, color = 0xfbe1;
    const int reference_x = 128, reference_y = 128;
    int width = (int)kui_shell_font_width(text, large);
    int height = large ? KUI_SHELL_FONT_LARGE_HEIGHT : KUI_SHELL_FONT_SMALL_HEIGHT;
    const int positions[][2] = {
        {0, 0}, {-1, 0}, {-width / 2, 8}, {-width + 1, 8},
        {WIDTH - 1, 8}, {WIDTH - width / 2, 8},
        {8, -1}, {8, -height / 2}, {8, -height + 1},
        {8, HEIGHT - 1}, {8, HEIGHT - height / 2},
        {-width / 2, -height / 2}, {WIDTH - width / 2, HEIGHT - height / 2},
        {-width - 10, -height - 10}, {WIDTH, HEIGHT},
        {INT_MIN, 0}, {INT_MAX, 0}, {0, INT_MIN}, {0, INT_MAX},
        {INT_MIN, INT_MAX}, {INT_MAX, INT_MIN}
    };
    clear(&reference, background);
    kui_shell_font_draw(reference.pixels, reference_x, reference_y, color, text, large);
    check_guards(&reference);
    assert(changed(&reference, background) > 0);
    for(size_t pos = 0; pos < sizeof(positions) / sizeof(positions[0]); ++pos) {
        int x = positions[pos][0], y = positions[pos][1];
        clear(&actual, background);
        kui_shell_font_draw(actual.pixels, x, y, color, text, large);
        check_guards(&actual);
        for(int py = 0; py < HEIGHT; ++py) {
            for(int px = 0; px < WIDTH; ++px) {
                /* Translate using a wider integer so the test itself handles
                 * INT_MIN/MAX without signed overflow. */
                int64_t rx = (int64_t)px - x + reference_x;
                int64_t ry = (int64_t)py - y + reference_y;
                uint16_t expected = background;
                if(rx >= 0 && rx < WIDTH && ry >= 0 && ry < HEIGHT)
                    expected = reference.pixels[(size_t)ry * WIDTH + (size_t)rx];
                assert(actual.pixels[(size_t)py * WIDTH + (size_t)px] == expected);
            }
        }
    }
}
static void check_blending(bool large) {
    clear(&reference, 0); clear(&actual, 0x001f);
    kui_shell_font_draw(reference.pixels, 40, 50, 0xffff, "Ag@", large);
    kui_shell_font_draw(actual.pixels, 40, 50, 0xf800, "Ag@", large);
    check_guards(&reference); check_guards(&actual);
    bool saw_mixed = false, saw_solid = false, levels[32] = {false};
    for(size_t i = 0; i < PIXELS; ++i) {
        unsigned white_r = reference.pixels[i] >> 11;
        unsigned white_g = (reference.pixels[i] >> 5) & 63;
        unsigned white_b = reference.pixels[i] & 31;
        unsigned red = actual.pixels[i] >> 11;
        unsigned green = (actual.pixels[i] >> 5) & 63;
        unsigned blue = actual.pixels[i] & 31;
        assert(white_r == white_b);
        assert(white_g >= white_r * 2 && white_g <= white_r * 2 + 2);
        assert(red == white_r && green == 0);
        /* Both five-bit channels contribute to a red/blue blend; rounding
         * may lose or gain one unit but cannot leak into the green channel. */
        assert(red + blue >= 30 && red + blue <= 32);
        if(reference.pixels[i] == 0) assert(actual.pixels[i] == 0x001f);
        if(red > 0 && red < 31 && blue > 0) saw_mixed = true;
        if(red == 31 && blue == 0) saw_solid = true;
        levels[white_r] = true;
    }
    unsigned count = 0;
    for(unsigned i = 0; i < 32; ++i) count += levels[i];
    assert(saw_mixed && saw_solid && count > 2 && count <= 16);
}
int main(void) {
    assert(kui_shell_font_width("Dreamcast", true) > kui_shell_font_width("Dreamcast", false));
    for(unsigned large = 0; large < 2; ++large) {
        check_noop(large != 0);
        check_ascii(large != 0);
        check_advances_match_draw(large != 0);
        check_fallback(large != 0);
        check_clipping(large != 0);
        check_blending(large != 0);
    }
    puts("PASS shell font: advances, all ASCII, fallback, clipped rendering, extreme coordinates, RGB565 antialiasing");
    return 0;
}
