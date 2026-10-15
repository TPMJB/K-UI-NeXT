/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_DISPLAY_H
#define KUI_RETAIL_DISPLAY_H
#include <stdint.h>
struct retail_display_state { uint32_t regs[14]; };
void retail_display_capture(struct retail_display_state *);
void retail_display_restore(const struct retail_display_state *);
void retail_display_line(const char *);
void retail_display_hex(const char *, uint32_t);
void retail_display_values(const char *legend, const uint32_t *values, unsigned count);
void retail_display_progress(uint32_t, uint32_t);
#ifdef KUI_RETAIL_CE
void retail_display_status(const char *legend, const uint32_t *values, unsigned count);
#endif
void retail_display_pause(uint32_t frames);
#endif
