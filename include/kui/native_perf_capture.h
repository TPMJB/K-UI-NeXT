/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_PERF_CAPTURE_H
#define KUI_NATIVE_PERF_CAPTURE_H
#include "kui/native_isolation.h"
#ifndef KUI_NATIVE_PERF_CAPTURE
#define KUI_NATIVE_PERF_CAPTURE 0
#endif
#ifndef KUI_NATIVE_PERF_FULL_SUBMIT
#define KUI_NATIVE_PERF_FULL_SUBMIT 0
#endif
#if (KUI_NATIVE_PERF_CAPTURE != 0 && KUI_NATIVE_PERF_CAPTURE != 1) || \
    (KUI_NATIVE_PERF_FULL_SUBMIT != 0 && KUI_NATIVE_PERF_FULL_SUBMIT != 1)
#error "Native performance selectors must be exactly zero or one"
#endif
#if KUI_NATIVE_PERF_CAPTURE && (!KUI_NATIVE_SPLIT_CONTROLLER_GENERAL || \
    !KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION || !KUI_VMU_NATIVE_PERSISTENT_PROBE || \
    KUI_NATIVE_SPLIT_CONTROLLER_PREFETCH || KUI_NATIVE_SESSION_CONTROLS)
#error "Native performance capture requires the general persistent split worker without prefetch or session controls"
#endif
#if KUI_NATIVE_PERF_FULL_SUBMIT && !KUI_NATIVE_PERF_CAPTURE
#error "Native full-submit comparison requires performance capture"
#endif
/* Sampling never configures the owner's TMU0. The clock must pass the existing
 * 781250 Hz profile at both endpoints. Collection is terminal and opt-in. */
#define KUI_NATIVE_PERF_CAPTURE_POLLS 1200u
#define KUI_NATIVE_PERF_CAPTURE_SECONDS 20u
#define KUI_NATIVE_PERF_CAPTURE_HOLD 30u
#endif
