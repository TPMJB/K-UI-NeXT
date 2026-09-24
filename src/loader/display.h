/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_LOADER_DISPLAY_H
#define KUI_LOADER_DISPLAY_H
#include <stdint.h>
void kui_loader_display_init(void);
void kui_loader_display_line(const char *);
void kui_loader_display_result(const char *, uint32_t passed, uint32_t detail);
void kui_loader_display_number(const char *, uint32_t);
void kui_loader_display_summary(uint32_t failures, uint32_t blocks);
#endif
