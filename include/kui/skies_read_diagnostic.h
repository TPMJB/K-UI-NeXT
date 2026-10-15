/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SKIES_READ_DIAGNOSTIC_H
#define KUI_SKIES_READ_DIAGNOSTIC_H

/* Diagnostic RAM diversion only, enabled separately for the native high
 * stage paired with its standard SCI TRACE3 resident. No stored game-file changes. */
#ifndef KUI_RETAIL_SKIES_READ_DIAGNOSTIC
#define KUI_RETAIL_SKIES_READ_DIAGNOSTIC 0
#endif
#if KUI_RETAIL_SKIES_READ_DIAGNOSTIC != 0 && KUI_RETAIL_SKIES_READ_DIAGNOSTIC != 1
#error "KUI_RETAIL_SKIES_READ_DIAGNOSTIC must be 0 or 1"
#endif
#if KUI_RETAIL_SKIES_READ_DIAGNOSTIC && (defined(KUI_RETAIL_CE) || !KUI_RETAIL_LOW_RESIDENT)
#error "Skies read diagnostic is restricted to the native low-resident stage"
#endif
#define KUI_SKIES_READ_ENTRY_SLOT 36u

#endif
