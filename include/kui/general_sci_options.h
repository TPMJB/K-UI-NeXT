/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GENERAL_SCI_OPTIONS_H
#define KUI_GENERAL_SCI_OPTIONS_H

/* Optional DMA1 ownership repair for the native background SCI reader.
 * Capture the current channel at actual acquisition and defer repair during
 * cancellation. Stream layout, target, IRQ priority and receive-ahead policy
 * retain their existing values. */
#ifndef KUI_GENERAL_SCI_RESUME_FIX
#define KUI_GENERAL_SCI_RESUME_FIX 0
#endif
#if KUI_GENERAL_SCI_RESUME_FIX < 0 || KUI_GENERAL_SCI_RESUME_FIX > 1
#error General SCI resume fix must be 0 or 1
#endif
#if KUI_GENERAL_SCI_RESUME_FIX
#if !defined(KUI_RETAIL_ASYNC) || KUI_RETAIL_ASYNC != 1 || \
    !defined(KUI_RETAIL_GD_ASYNC) || KUI_RETAIL_GD_ASYNC != 1 || \
    !defined(KUI_RETAIL_TRANSPORT) || KUI_RETAIL_TRANSPORT != 1 || \
    !defined(KUI_SCI_SD_NO_BLOCK) || KUI_SCI_SD_NO_BLOCK != 1
#error General SCI resume fix requires the native background SCI reader
#endif
#if defined(KUI_RETAIL_CE) || KUI_TOY_PIPELINE_HIGH_STREAM || \
    KUI_TOY_PILOT_DATA_PIPELINE_PROFILE || KUI_TOY_PILOT_DATA_SCHEDULE_MODE || \
    KUI_TOY_PILOT_SHARED_SCI || KUI_TOY_PILOT_WORD_REVERSE_PROFILE
#error General SCI resume fix cannot alter CE or Toy readers
#endif
#endif

/* Separate opt-in for the CE background reader. The physical receiver and
 * DMA1 handoff use the same repair, but CE retains its own IRQ table, MMU,
 * CRC, token budget and stream-delivery policies. */
#ifndef KUI_CE_SCI_RESUME_FIX
#define KUI_CE_SCI_RESUME_FIX 0
#endif
#if KUI_CE_SCI_RESUME_FIX < 0 || KUI_CE_SCI_RESUME_FIX > 1
#error CE SCI resume fix must be 0 or 1
#endif
#if KUI_CE_SCI_RESUME_FIX
#if !defined(KUI_RETAIL_CE) || KUI_RETAIL_CE != 1 || \
    !defined(KUI_RETAIL_ASYNC) || KUI_RETAIL_ASYNC != 1 || \
    !defined(KUI_RETAIL_GD_ASYNC) || KUI_RETAIL_GD_ASYNC != 1 || \
    !defined(KUI_RETAIL_TRANSPORT) || KUI_RETAIL_TRANSPORT != 1 || \
    !defined(KUI_SCI_SD_NO_BLOCK) || KUI_SCI_SD_NO_BLOCK != 1
#error CE SCI resume fix requires the CE background SCI reader
#endif
#if KUI_GENERAL_SCI_RESUME_FIX || KUI_RETAIL_TOY_PILOT || \
    KUI_TOY_PIPELINE_HIGH_STREAM || KUI_TOY_PILOT_DATA_PIPELINE_PROFILE || \
    KUI_TOY_PILOT_DATA_SCHEDULE_MODE || KUI_TOY_PILOT_SHARED_SCI || \
    KUI_TOY_PILOT_WORD_REVERSE_PROFILE
#error CE SCI resume fix cannot alter native or Toy readers
#endif
#endif

#endif
