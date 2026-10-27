/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_ASYNC_LOAN_H
#define KUI_VMU_NATIVE_ASYNC_LOAN_H
#include <stdbool.h>
#include <stdint.h>

#ifndef KUI_VMU_NATIVE_BACKGROUND
#define KUI_VMU_NATIVE_BACKGROUND 0
#endif
#if KUI_VMU_NATIVE_BACKGROUND != 0 && KUI_VMU_NATIVE_BACKGROUND != 1
#error "Native VMU background support must be explicitly disabled or enabled"
#endif
#if KUI_VMU_NATIVE_BACKGROUND
#if defined(KUI_RETAIL_CE) || !defined(KUI_RETAIL_ASYNC) || KUI_RETAIL_ASYNC != 1 || \
    !defined(KUI_RETAIL_GD_ASYNC) || KUI_RETAIL_GD_ASYNC != 1 || \
    !defined(KUI_RETAIL_TRANSPORT) || KUI_RETAIL_TRANSPORT != 1 || \
    !defined(KUI_SCI_SD_NO_BLOCK) || KUI_SCI_SD_NO_BLOCK != 1 || \
    !KUI_GENERAL_SCI_RESUME_FIX
#error "Native VMU background support requires the repaired native SCI background reader"
#endif
#endif

enum kui_vmu_native_async_loan_result {
    KUI_VMU_NATIVE_ASYNC_LOAN_OK,
    KUI_VMU_NATIVE_ASYNC_LOAN_BUSY,
    KUI_VMU_NATIVE_ASYNC_LOAN_FAULT,
    KUI_VMU_NATIVE_ASYNC_LOAN_CONTEXT
};
enum kui_vmu_native_async_loan_state {
    KUI_VMU_NATIVE_ASYNC_LOAN_IDLE,
    KUI_VMU_NATIVE_ASYNC_LOAN_STOPPING,
    KUI_VMU_NATIVE_ASYNC_LOAN_WRITER,
    KUI_VMU_NATIVE_ASYNC_LOAN_QUARANTINED
};
struct kui_loader_sd;

/* The admitted native worker calls both entries with actual IMASK15 and
 * keeps it through its bounded transaction. No SR write or interrupt wake
 * occurs here. Begin holds the GD entry lock until end; BUSY has no stop,
 * acquisition or GD-state side effect. Terminal command/token/completion
 * metadata survives a loan. A successful end leaves the background reader
 * closed so the next read acquires SCI through its ordinary start path.
 * Any uncertain stop/restoration quarantines the reader until restart. */
enum kui_vmu_native_async_loan_result kui_retail_native_async_loan_begin(
    struct kui_loader_sd *copied_card);
enum kui_vmu_native_async_loan_result kui_retail_native_async_loan_end(bool healthy);

#endif
