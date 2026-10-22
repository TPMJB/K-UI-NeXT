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
bool kui_sector_edc_valid(const uint8_t raw[KUI_RAW_BYTES]);
#endif
