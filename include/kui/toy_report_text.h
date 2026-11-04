/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_REPORT_TEXT_H
#define KUI_TOY_REPORT_TEXT_H
#include <stddef.h>
#include <stdint.h>
#include "kui/toy_decision_observer.h"
struct kui_toy_report_text {
    const struct kui_toy_decision_report *report;
    const uint32_t *stopped_words; /* Exactly sixteen pre-cleanup words. */
    const char *build; /* Twelve lower-case hexadecimal characters plus NUL. */
};
/* Frozen snapshot only. No allocation, hardware access or global mutation.
 * The sink may request the same bounded slice repeatedly for verification. */
size_t kui_toy_report_text_length(const struct kui_toy_report_text *);
size_t kui_toy_report_text_read(void *,size_t,uint8_t *,size_t);
#endif
