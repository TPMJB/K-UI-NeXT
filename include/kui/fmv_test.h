/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_FMV_TEST_H
#define KUI_FMV_TEST_H
#ifndef KUI_FMV_TEST
#define KUI_FMV_TEST 0
#endif
#ifndef KUI_TOY_FMV_IDLE_REARM
#define KUI_TOY_FMV_IDLE_REARM 0
#endif
#ifndef KUI_ARMADA_FMV_MUTE
#define KUI_ARMADA_FMV_MUTE 0
#endif
#if (KUI_FMV_TEST != 0 && KUI_FMV_TEST != 1) || \
    (KUI_TOY_FMV_IDLE_REARM != 0 && KUI_TOY_FMV_IDLE_REARM != 1) || \
    (KUI_ARMADA_FMV_MUTE != 0 && KUI_ARMADA_FMV_MUTE != 1)
#error FMV test selectors must be zero or one
#endif
#if (KUI_TOY_FMV_IDLE_REARM || KUI_ARMADA_FMV_MUTE) && !KUI_FMV_TEST
#error FMV alternatives require the isolated FMV test build
#endif
#endif
