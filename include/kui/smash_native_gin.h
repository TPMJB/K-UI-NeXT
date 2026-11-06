/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_NATIVE_GIN_H
#define KUI_SMASH_NATIVE_GIN_H

/* Launch-local native continuation, captured only after the original GIN
 * completed its original executable transfer. On-disk capsules stay blank. */
#define KUI_SMASH_NATIVE_GIN_MAGIC0 0x5349554b
#define KUI_SMASH_NATIVE_GIN_MAGIC1 0x3149474e
#define KUI_SMASH_NATIVE_GIN_OFFSET 0x200
#define KUI_SMASH_NATIVE_GIN_BYTES 160
#define KUI_SMASH_NATIVE_GIN_PROFILE 3
#define KUI_SMASH_NATIVE_GIN_FRAME_P1 0x8c00f3ac
#define KUI_SMASH_NATIVE_GIN_TARGET_P2 0xac010000
#define KUI_SMASH_NATIVE_GIN_SOURCE_P1 0x8ce00000
#define KUI_SMASH_NATIVE_GIN_CONTEXT_OFFSET 0x18ee60
#define KUI_SMASH_NATIVE_GIN_CONTEXT_BYTES 8192
#define KUI_SMASH_NATIVE_GIN_TRANSFER_P1 0x8cd00000
#define KUI_SMASH_NATIVE_GIN_TRANSFER_MAGIC0 0x5349554b
#define KUI_SMASH_NATIVE_GIN_TRANSFER_MAGIC1 0x3152544e
#define KUI_SMASH_NATIVE_GIN_TRANSFER_BYTES 8628

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct kui_smash_native_menu_seed;
struct kui_smash_native_gin_seed {
    uint32_t magic0,magic1,version,bytes;
    uint32_t profile,ccr;
    /* MACL,MACH,GBR,VBR,SR,PR,R14..R0: actual active-bank values. */
    uint32_t frame[21];
    uint32_t crc32,reserved[12];
};
_Static_assert(sizeof(struct kui_smash_native_gin_seed)==KUI_SMASH_NATIVE_GIN_BYTES,
    "Smash native GIN continuation ABI");
struct kui_smash_native_gin_transfer {
    uint32_t magic0,magic1,version,bytes;
    struct kui_smash_native_gin_seed gin;
    uint8_t context[KUI_SMASH_NATIVE_GIN_CONTEXT_BYTES];
    uint8_t ip[256];
    uint32_t crc32;
};
_Static_assert(sizeof(struct kui_smash_native_gin_transfer)==KUI_SMASH_NATIVE_GIN_TRANSFER_BYTES,
    "Smash native GIN transfer ABI");

bool kui_smash_native_gin_blank(const struct kui_smash_native_gin_seed *);
bool kui_smash_native_gin_capable(const void *,size_t);
bool kui_smash_native_gin_capture(struct kui_smash_native_gin_seed *,
    const uint32_t [21],uint32_t,uint32_t);
bool kui_smash_native_gin_valid(const struct kui_smash_native_gin_seed *);
/* All three byte pointers name complete 8192-byte GINS contexts, not MDE
 * image bases. source is the canonical original-file context; loaded is the
 * post-GIN MDE context; gin_context is the original high GIN header. */
bool kui_smash_native_gin_context_matches(const void *source,const void *loaded,
    const void *gin_context,const struct kui_smash_native_menu_seed *);
/* After matches() and the caller's complete image ownership proof, replace
 * only transfer words +0x10..0x1f and menu fields +0xc0..0x1ff with their
 * canonical source bytes. Other loaded bytes remain untouched. */
bool kui_smash_native_gin_normalize(const void *source,void *loaded);
/* Enter through P2 after restoring the captured frame at FRAME_P1. */
void kui_smash_native_gin_resume(const uint32_t [21],uint32_t)
    __attribute__((noreturn));
#endif
#endif
