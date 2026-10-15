/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_MDK2_CDDA_LAYOUT_H
#define KUI_MDK2_CDDA_LAYOUT_H
/* The full MDK2 map moves to the actual first game-heap lease. Only its
 * guarded pointer and reader state remain below IP.BIN; the ordinary and
 * Toy layouts continue to use their original bounds. */
#define KUI_MDK2_CDDA_MANIFEST_SLOTS 256u
#define KUI_MDK2_CDDA_LOW_LIMIT 0x8c007800
#define KUI_MDK2_CDDA_HOOK_STACK 0x8c007d00
#define KUI_MDK2_CDDA_HOOK_STACK_BYTES 0x500
#define KUI_MDK2_CDDA_MAP_ADDRESS 0x8cfd8000
#define KUI_MDK2_CDDA_MAP_ALIAS 0xacfd8000
#define KUI_MDK2_CDDA_MAP_LIMIT 0x8cfd9000
#define KUI_MDK2_CDDA_MAP_BYTES 0x1000
#endif
