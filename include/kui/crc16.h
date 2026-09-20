/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CRC16_H
#define KUI_CRC16_H
#include <stddef.h>
#include <stdint.h>

/* CRC-16/CCITT: polynomial 0x1021, most significant bit first, no reflection, no final
 * XOR ("XMODEM" with start 0; check value 0x31C3 for "123456789"). It is the CRC
 * KallistiOS's SD driver computes over every 512-byte block it writes, and over every
 * block it reads while check_crc is on, with a bit-trick loop.
 *
 * The bench measured that loop's true CPU cost against these, so the choice of a faster
 * replacement rests on cycles per byte on the real machine rather than a guess. All four
 * compute exactly the same value; tests/test_crc16.c proves it against the reference.
 *   kui_crc16_ref     KOS's net_crc16ccitt, bit for bit (the reference)
 *   kui_crc16_table   one 256-entry table, one byte per step
 *   kui_crc16_slice2  two 256-entry tables, two bytes per step
 *   kui_crc16_nibble  one 16-entry table, two steps per byte (the smallest cache footprint)
 * `crc` is the running value (0 to start); calls chain: f(f(s,a),b) == f(s,a+b). */
uint16_t kui_crc16_ref(uint16_t crc, const void *data, size_t bytes);
uint16_t kui_crc16_table(uint16_t crc, const void *data, size_t bytes);
uint16_t kui_crc16_slice2(uint16_t crc, const void *data, size_t bytes);
uint16_t kui_crc16_nibble(uint16_t crc, const void *data, size_t bytes);
#endif
