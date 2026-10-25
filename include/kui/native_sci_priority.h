/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_SCI_PRIORITY_H
#define KUI_NATIVE_SCI_PRIORITY_H

/* Shared by C ownership checks and the assembly return trampoline. The
 * experimental level must not silently fall back to one on an IRQ return.
 * No suffix: this constant is also an SH immediate operand. */
#ifndef KUI_NATIVE_SCI_PRIORITY7
#define KUI_NATIVE_SCI_PRIORITY7 0
#endif
#if KUI_NATIVE_SCI_PRIORITY7
#if !KUI_NATIVE_LOADER_TIMELINE || !KUI_NATIVE_IRQ_REARM || \
    !KUI_RETAIL_NATIVE_HYBRID || !KUI_VMU_NATIVE_BACKGROUND || \
    !KUI_RETAIL_ASYNC || defined(KUI_RETAIL_CE)
#error "SCI priority7 is restricted to the separate native timeline IRQ-rearm experiment"
#endif
#define KUI_RETAIL_SCI_PRIORITY_FIELD 0x70
#else
#define KUI_RETAIL_SCI_PRIORITY_FIELD 0x10
#endif

#endif
