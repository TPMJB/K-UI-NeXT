/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_LOADER_TRACE_REPORT_H
#define KUI_TOY_LOADER_TRACE_REPORT_H
#include <stdint.h>
struct retail_display_state;
struct kui_retail_gd_diagnostics;
/* Terminal only: snapshot before RESET/display reuse; no gameplay logging. */
void kui_toy_loader_trace_report_capture(const struct retail_display_state *,
    const uint32_t *,const struct kui_retail_gd_diagnostics *);
const uint32_t *kui_toy_loader_trace_report_page(unsigned trace,unsigned page);
void kui_toy_loader_trace_terminal(const struct retail_display_state *,
    const uint32_t *,const struct kui_retail_gd_diagnostics *,uint32_t);
#endif
