/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_HASH_H
#define KUI_HASH_H
#include "kui/core.h"
struct kui_sha256 { uint32_t h[8]; uint64_t bytes; uint8_t block[64]; };
void kui_sha256_init(struct kui_sha256 *s);
void kui_sha256_update(struct kui_sha256 *s, const void *data, size_t size);
/* Finishes a copy; the original remains available for the next checkpoint. */
void kui_sha256_digest(const struct kui_sha256 *s, uint8_t digest[32]);
void kui_hex(const uint8_t *bytes, size_t size, char *out);
uint32_t kui_cd_edc(const uint8_t *data, size_t size);
/* IEEE CRC32 with exactly the same finalized/incremental convention as
 * kui_crc32(), using four byte tables for capture's pre/post-write integrity
 * passes. Lazy initialization is owned by the sole storage worker. */
uint32_t kui_crc32_capture(uint32_t previous,const void *data,size_t size);
bool kui_sector_edc_valid(const uint8_t raw[KUI_RAW_BYTES]);
/* Ordinary acquisition validity: retain Mode 2 Form 1 framing/EDC support,
 * and for Mode 1 also require the requested absolute sector address. This
 * does not compute P/Q parity; full parity remains an opt-in strict check. */
bool kui_sector_edc_valid_at(const uint8_t raw[KUI_RAW_BYTES],uint32_t fad);
#endif
