/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TARGET_H
#define KUI_TARGET_H

/* Shared by the KOS application and freestanding loaders. GCC's SH-4 mode
 * macros differ; __sh__ also identifies the SH cross compiler. Keep raw
 * target detection here so every mode selects the same console code. */
#if !defined(KUI_ON_CONSOLE) && (defined(_arch_dreamcast) || defined(__DREAMCAST__) || \
    defined(__sh__) || defined(__SH4__) || defined(__SH4_SINGLE__) || \
    defined(__SH4_SINGLE_ONLY__) || defined(__SH4_NOFPU__))
#define KUI_ON_CONSOLE 1
#endif

#endif
