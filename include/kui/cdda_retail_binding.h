/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_RETAIL_BINDING_H
#define KUI_CDDA_RETAIL_BINDING_H

/* The playback engine is shared; host addresses and port ownership belong
 * to an exact executable adapter. The default retains the admitted Toy
 * contract. Defining the MDK2 adapter does not admit a game or a driver. */
#ifndef KUI_RETAIL_MDK2_CDDA
#define KUI_RETAIL_MDK2_CDDA 0
#endif
#if KUI_RETAIL_MDK2_CDDA != 0 && KUI_RETAIL_MDK2_CDDA != 1
#error MDK2 CDDA adapter must be 0 or 1
#endif
#if KUI_RETAIL_MDK2_CDDA
#include "kui/mdk2_cdda_binding.h"
#include "kui/cdda_audio64_profile.h"
#define KUI_CDDA_DRIVER_PROFILE kui_cdda_audio64_mdk2_190b1
#else
#define KUI_CDDA_HOST_INSTALLED UINT32_C(0x8c0af74c)
#define KUI_CDDA_HOST_QUEUE UINT32_C(0x8c112b08)
#define KUI_CDDA_HOST_PRODUCER UINT32_C(0x8c112b0c)
#define KUI_CDDA_HOST_NOTIFY UINT32_C(0x8c112b10)
#define KUI_CDDA_HOST_COMMANDS UINT32_C(0x8c0a8924)
#define KUI_CDDA_HOST_AM_READY UINT32_C(0x8c0a7318)
#define KUI_CDDA_HOST_AC_READY UINT32_C(0x8c0a8940)
#define KUI_CDDA_HOST_DRIVER_READY UINT32_C(0x8c0af74c)
#define KUI_CDDA_HEAP_MANAGER UINT32_C(0x8c10e55c)
#define KUI_CDDA_FIRST_PORT 62u
#define KUI_CDDA_PORT_MASK UINT32_C(0xc0000000)
#define KUI_CDDA_DRIVER_CURRENT_OFFSET UINT32_C(0x14f0)
#define KUI_CDDA_DRIVER_PLAYING_WORD_OFFSET UINT32_C(0x14a4)
#define KUI_CDDA_DRIVER_PLAYING_SHIFT 16u
#define KUI_CDDA_DRIVER_TEMPLATE_OFFSET UINT32_C(0x2ca8)
#define KUI_CDDA_DRIVER_CONSUMER_OFFSET UINT32_C(0x399c)
#define KUI_CDDA_DRIVER_BYTES 20740u
#endif
#endif
