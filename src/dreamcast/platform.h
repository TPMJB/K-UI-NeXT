/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_PLATFORM_H
#define KUI_PLATFORM_H
#include "kui/probe.h"
void kui_log(const char *format, ...);
bool kui_cancelled(void);
bool kui_sd_connect(void);
void kui_sd_disconnect(void);
void kui_disc_probe(void);
void kui_drive_init_bus(void);
#endif
