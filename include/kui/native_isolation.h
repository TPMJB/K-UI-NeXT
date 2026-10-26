/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_ISOLATION_H
#define KUI_NATIVE_ISOLATION_H

/* Explicit diagnostic build variants. Never infer these from a game name,
 * saved launch option, runtime mode or profile contents. */
#ifndef KUI_NATIVE_READER_ONLY_ISOLATION
#define KUI_NATIVE_READER_ONLY_ISOLATION 0
#endif
#ifndef KUI_NATIVE_STANDARD_VMU_ISOLATION
#define KUI_NATIVE_STANDARD_VMU_ISOLATION 0
#endif
#ifndef KUI_NATIVE_RA_TICK_OFF_ISOLATION
#define KUI_NATIVE_RA_TICK_OFF_ISOLATION 0
#endif
#ifndef KUI_NATIVE_E1_WAIT_CAPTURE
#define KUI_NATIVE_E1_WAIT_CAPTURE 0
#endif
#ifndef KUI_NATIVE_MAPLE_BYPASS_ISOLATION
#define KUI_NATIVE_MAPLE_BYPASS_ISOLATION 0
#endif
#if (KUI_NATIVE_READER_ONLY_ISOLATION != 0 && KUI_NATIVE_READER_ONLY_ISOLATION != 1) || \
    (KUI_NATIVE_STANDARD_VMU_ISOLATION != 0 && KUI_NATIVE_STANDARD_VMU_ISOLATION != 1) || \
    (KUI_NATIVE_RA_TICK_OFF_ISOLATION != 0 && KUI_NATIVE_RA_TICK_OFF_ISOLATION != 1) || \
    (KUI_NATIVE_E1_WAIT_CAPTURE != 0 && KUI_NATIVE_E1_WAIT_CAPTURE != 1) || \
    (KUI_NATIVE_MAPLE_BYPASS_ISOLATION != 0 && KUI_NATIVE_MAPLE_BYPASS_ISOLATION != 1)
#error "Native isolation selectors must be exactly zero or one"
#endif
#if KUI_NATIVE_READER_ONLY_ISOLATION && KUI_NATIVE_STANDARD_VMU_ISOLATION
#error "Select one native isolation variant"
#endif
#if KUI_NATIVE_RA_TICK_OFF_ISOLATION && \
    (!KUI_NATIVE_READER_ONLY_ISOLATION || !KUI_NATIVE_DUAL_READERS)
#error "TICK-off isolation requires the reader-only dual variant"
#endif
#if KUI_NATIVE_E1_WAIT_CAPTURE && \
    (!KUI_NATIVE_RA_TICK_OFF_ISOLATION || !KUI_NATIVE_READER_ONLY_ISOLATION || !KUI_NATIVE_DUAL_READERS)
#error "Evolution wait capture requires the reader-only TICK-off dual variant"
#endif
#if KUI_NATIVE_MAPLE_BYPASS_ISOLATION && \
    (!KUI_NATIVE_RA_TICK_OFF_ISOLATION || !KUI_NATIVE_READER_ONLY_ISOLATION || \
     !KUI_NATIVE_DUAL_READERS || KUI_NATIVE_E1_WAIT_CAPTURE)
#error "Maple bypass requires reader-only TICK-off isolation without wait capture"
#endif
#if KUI_NATIVE_READER_ONLY_ISOLATION && !KUI_NATIVE_DUAL_READERS
#error "Reader-only isolation requires the current dual-reader build"
#endif
#if KUI_NATIVE_STANDARD_VMU_ISOLATION && \
    (KUI_NATIVE_DUAL_READERS || KUI_NATIVE_READAHEAD || KUI_VMU_NATIVE_BACKGROUND)
#error "Standard VMU isolation cannot enable a background reader"
#endif

/* Match the current dual-reader reservation and placement in the Standard
 * control without enabling a background engine or its reader bindings. */
#define KUI_NATIVE_CURRENT_BOTTOM_HEAP \
    (KUI_NATIVE_DUAL_READERS || KUI_NATIVE_STANDARD_VMU_ISOLATION)

#endif
