/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_REPORT_TEST_H
#define KUI_TOY_REPORT_TEST_H
/* Private selected-bank launcher. Ordinary release behavior is unchanged. */
#ifndef KUI_TOY_REPORT_TEST
#define KUI_TOY_REPORT_TEST 0
#endif
#if KUI_TOY_REPORT_TEST != 0 && KUI_TOY_REPORT_TEST != 1
#error "KUI_TOY_REPORT_TEST must be exactly zero or one"
#endif
#define KUI_TOY_REPORT_TEST_LABEL "TOY TXT TEST"
#endif
