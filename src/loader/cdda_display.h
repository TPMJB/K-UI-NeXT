/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_DISPLAY_H
#define KUI_CDDA_DISPLAY_H
#include <stdint.h>
void cdda_display_init(void);
void cdda_display_line(const char *text);
void cdda_display_number(const char *label,uint32_t value);
void cdda_display_finish(unsigned failures);
#endif
