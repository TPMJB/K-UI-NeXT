/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_LOADER_SCHEDULE_REPORT_H
#define KUI_TOY_LOADER_SCHEDULE_REPORT_H
#include "kui/retail_gd.h"
#define KUI_TOY_SCHEDULE_REPORT_WORDS 176u
#define KUI_TOY_SCHEDULE_RECORDS 8u
/* Chronology is diagnostic only. Records retain first-sector CRC32 once
 * bytes have been credited by the original or asynchronous DATA reader. */
void kui_toy_loader_schedule_report_begin(const struct kui_retail_gd *);
void kui_toy_loader_schedule_report_end(const struct kui_retail_gd *,uint32_t,
    uint32_t,int32_t);
void kui_toy_loader_schedule_report_freeze(void);
const uint32_t *kui_toy_loader_schedule_report_words(void);
#ifdef KUI_TOY_LOADER_SCHEDULE_REPORT_HOST_TEST
uint32_t kui_toy_loader_schedule_report_host_read(uint32_t,unsigned);
#endif
#endif
