/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_CE_GIN_H
#define KUI_SMASH_CE_GIN_H

/* Launch-local capability/continuation record in the already authenticated
 * cold capsule. On disk this record is blank. Only the original GIN handoff
 * may capture it, after checking the complete capsule's immutable CRC. */
#define KUI_SMASH_CE_GIN_MAGIC0 0x5349554b
#define KUI_SMASH_CE_GIN_MAGIC1 0x314e4947
#define KUI_SMASH_CE_GIN_OFFSET 0x200
#define KUI_SMASH_CE_GIN_BYTES 160
#define KUI_SMASH_CE_GIN_FRAME_P1 0x8c00f3ac
#define KUI_SMASH_CE_GIN_TARGET_P2 0xac010000
#define KUI_SMASH_CE_GIN_SAMPLE_P1 0x8cf90000
#define KUI_SMASH_CE_GIN_SAMPLE_BYTES 204576

#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct kui_smash_ce_gin_seed {
    uint32_t magic0,magic1,version,bytes;
    uint32_t profile,ccr;
    /* MACL,MACH,GBR,VBR,SR,PR,R14..R0: the actual active-bank values. */
    uint32_t frame[21];
    uint32_t crc32,reserved[12];
};
_Static_assert(sizeof(struct kui_smash_ce_gin_seed)==KUI_SMASH_CE_GIN_BYTES,
    "Smash CE GIN continuation ABI");

bool kui_smash_ce_gin_blank(const struct kui_smash_ce_gin_seed *);
bool kui_smash_ce_gin_capable(const void *,size_t,unsigned);
bool kui_smash_ce_gin_capture(struct kui_smash_ce_gin_seed *,
    const uint32_t [21],uint32_t,uint32_t);
bool kui_smash_ce_gin_valid(const struct kui_smash_ce_gin_seed *);

/* Call this function through P2 with SR still in the captured RB1 profile.
 * The caller restores the validated frame at FRAME_P1 first. This routine
 * publishes RAM, restores the original CCR and resumes original GIN's RTE
 * continuation. No FPU state or inactive register bank is changed. */
void kui_smash_ce_gin_resume(const uint32_t [21],uint32_t)
    __attribute__((noreturn));
#endif
#endif
