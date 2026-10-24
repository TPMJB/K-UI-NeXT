/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_BACKGROUND_BUDGET_H
#define KUI_NATIVE_BACKGROUND_BUDGET_H
/* Candidate B: retain full foreground catch-up; shorten interrupt bursts. */
#define KUI_RETAIL_NATIVE_IRQ_BATCH_LIMIT 8u
#define KUI_RETAIL_NATIVE_PUMP_BATCH_LIMIT 2u
#define KUI_RETAIL_NATIVE_PUMP_COOKIE UINT32_C(0x4b504d50)
#endif
