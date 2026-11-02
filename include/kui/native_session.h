/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_SESSION_H
#define KUI_NATIVE_SESSION_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifndef KUI_NATIVE_SESSION_CONTROLS
#define KUI_NATIVE_SESSION_CONTROLS 0
#endif
#if KUI_NATIVE_SESSION_CONTROLS != 0 && KUI_NATIVE_SESSION_CONTROLS != 1
#error "Native session controls must be exactly zero or one"
#endif
#if KUI_NATIVE_SESSION_CONTROLS && (defined(KUI_RETAIL_CE) || KUI_RETAIL_ZSO)
#error "Native session controls exclude Windows CE and compressed readers"
#endif
/* Fixed LE stage payload, containing no launcher pointers. CRC32 at 16 covers
 * all 18432 bytes with its own four bytes treated as zero. Reserved bytes
 * 32..127 and 1152..2047, disabled return data and unused map records are zero. */
#define KUI_NATIVE_SESSION_MAGIC "KUISES01"
#define KUI_NATIVE_SESSION_VERSION 1u
#define KUI_NATIVE_SESSION_WIRE_BYTES 18432u
#define KUI_NATIVE_SESSION_CRC_OFFSET 16u
#define KUI_NATIVE_SESSION_FLAGS_OFFSET 20u
#define KUI_NATIVE_SESSION_COUNT_OFFSET 24u
#define KUI_NATIVE_SESSION_SELECTED_OFFSET 28u
#define KUI_NATIVE_SESSION_RETURN_OFFSET 128u
#define KUI_NATIVE_SESSION_RETURN_BYTES 1024u
#define KUI_NATIVE_SESSION_MAP_OFFSET 2048u
#define KUI_NATIVE_SESSION_MAP_BYTES 4096u
#define KUI_NATIVE_SESSION_DISCS_MAX 4u
#define KUI_NATIVE_SESSION_FLAG_DISCS 1u
#define KUI_NATIVE_SESSION_FLAG_RETURN 2u
#define KUI_NATIVE_SESSION_FLAGS_ALL 3u
/* Blank templates have flags/count/selected/CRC zero and empty data. */
int kui_native_session_descriptor_find(const void *module,size_t bytes,size_t *offset);
bool kui_native_session_wire_valid(const void *wire,size_t bytes,bool configured);
uint32_t kui_native_session_crc(const void *wire);
#endif
