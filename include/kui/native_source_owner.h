/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_SOURCE_OWNER_H
#define KUI_NATIVE_SOURCE_OWNER_H
#include <stddef.h>
#include <stdint.h>
#define KUI_NATIVE_SOURCE_OWNER_BYTES 64u
/* Immutable stage capability, not an executable proof. A matching stage
 * freshly checks this complete source owner before installing native hooks. */
struct kui_native_source_owner {uint32_t image_bytes,load;uint8_t sha256[32];};
/* 0 absent, 1 unique canonical record, -1 malformed/ambiguous. Compiler magic
 * literals without the complete typed16-byte prefix are not records. */
int kui_native_source_owner_find(const void *,size_t,struct kui_native_source_owner *);
#endif
