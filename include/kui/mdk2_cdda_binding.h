/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_MDK2_CDDA_BINDING_H
#define KUI_MDK2_CDDA_BINDING_H
#include <stdint.h>
/* Numeric interface bindings for the exact owner-verified MDK2 Audio64
 * driver. These globals describe the game's actual installed SDK state;
 * its custom SFX picker is separately restricted to ports 0..60. */
#define KUI_CDDA_HOST_INSTALLED UINT32_C(0x8c0a4074)
#define KUI_CDDA_HOST_QUEUE UINT32_C(0x8c4ac08c)
#define KUI_CDDA_HOST_PRODUCER UINT32_C(0x8c4ac090) /* 16-bit SDK cursor */
#define KUI_CDDA_HOST_NOTIFY UINT32_C(0x8c4ac094)
#define KUI_CDDA_HOST_COMMANDS UINT32_C(0x8c0a4040)
#define KUI_CDDA_HOST_AM_READY UINT32_C(0x8c4ac1f8) /* sound heap live */
#define KUI_CDDA_HOST_AC_READY UINT32_C(0x8c0a406c)
#define KUI_CDDA_HOST_DRIVER_READY UINT32_C(0x8c0a4074)
#define KUI_CDDA_HEAP_MANAGER UINT32_C(0x8c4ac1f8)
#define KUI_CDDA_FIRST_PORT 61u
#define KUI_CDDA_PORT_MASK UINT32_C(0x60000000)
#define KUI_CDDA_DRIVER_CURRENT_OFFSET UINT32_C(0x1530)
#define KUI_CDDA_DRIVER_PLAYING_WORD_OFFSET UINT32_C(0x14a4)
#define KUI_CDDA_DRIVER_PLAYING_SHIFT 8u
#define KUI_CDDA_DRIVER_TEMPLATE_OFFSET UINT32_C(0x2ca0) /* port 61 */
#define KUI_CDDA_DRIVER_TEMPLATE_BASE UINT32_C(0x1b78)
#define KUI_CDDA_DRIVER_CONSUMER_OFFSET UINT32_C(0x39dc)
#define KUI_CDDA_DRIVER_BYTES 20824u
#define KUI_CDDA_SOUND_FIRST UINT32_C(0xa0830040)
#define KUI_CDDA_SOUND_END UINT32_C(0xa09f4000)
/* Do not confuse the mutable event producer at +28 with first free memory
 * at +8. The latter is published by ARM header EC, value 0x30040. */
#define KUI_CDDA_HOST_FIRST_FREE UINT32_C(0x8c4ac088)
#define KUI_CDDA_GAME_VOICES UINT32_C(0x8c45a378)
#define KUI_CDDA_GAME_VOICE_BYTES 16u
#define KUI_CDDA_GAME_STREAM_PORT 63u
#endif
