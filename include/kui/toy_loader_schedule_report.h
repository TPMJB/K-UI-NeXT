/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_LOADER_SCHEDULE_REPORT_H
#define KUI_TOY_LOADER_SCHEDULE_REPORT_H
#include "kui/retail_gd.h"
#ifndef KUI_TOY_PILOT_DATA_BUDGET_PROFILE
#define KUI_TOY_PILOT_DATA_BUDGET_PROFILE 0
#endif
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE < 0 || KUI_TOY_PILOT_DATA_BUDGET_PROFILE > 4
#error Toy loader DATA budget profile must be 0, 1, 2, 3 or 4
#endif
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE && KUI_TOY_PILOT_DATA_SCHEDULE_MODE != 2
#error Toy loader DATA budget profiles require startup asynchronous DATA scheduling
#endif
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE
#define KUI_TOY_SCHEDULE_REPORT_VERSION 2u
#define KUI_TOY_SCHEDULE_REPORT_WORDS 192u
#define KUI_TOY_SCHEDULE_RECORD_WORDS 18u
#else
#define KUI_TOY_SCHEDULE_REPORT_VERSION 1u
#define KUI_TOY_SCHEDULE_REPORT_WORDS 176u
#define KUI_TOY_SCHEDULE_RECORD_WORDS 16u
#endif
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
