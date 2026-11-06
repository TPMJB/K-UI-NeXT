/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_PHASE_VIDEO_H
#define KUI_SMASH_PHASE_VIDEO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define KUI_SMASH_PHASE_VIDEO_MAGIC0 UINT32_C(0x5349554b)
#define KUI_SMASH_PHASE_VIDEO_MAGIC1 UINT32_C(0x31444956)
#define KUI_SMASH_PHASE_VIDEO_BYTES 80u
#define KUI_SMASH_PHASE_OUTER_MAGIC1 UINT32_C(0x3154504f)
#define KUI_SMASH_PHASE_OUTER_WORDS KUI_SMASH_PHASE_VIDEO_MAGIC0,KUI_SMASH_PHASE_OUTER_MAGIC1,1u,16u
/* This diagnostic-only record follows TIM1, leaving both working seeds and
 * TIM1 unchanged. Register order is retail_display_state, captured by the
 * outer stage after KOS shutdown and before the original menu runs. */
struct kui_smash_phase_video_seed {
    uint32_t magic0,magic1,version,bytes;
    uint32_t regs[14];
    uint32_t crc32,reserved;
};
_Static_assert(sizeof(struct kui_smash_phase_video_seed)==80u,"Phase video seed ABI");
_Static_assert(offsetof(struct kui_smash_phase_video_seed,regs)==16u,"Phase video register ABI");
_Static_assert(offsetof(struct kui_smash_phase_video_seed,crc32)==72u,"Phase video CRC ABI");
bool kui_smash_phase_video_blank(const struct kui_smash_phase_video_seed *);
bool kui_smash_phase_video_valid(const struct kui_smash_phase_video_seed *);
bool kui_smash_phase_video_capture(struct kui_smash_phase_video_seed *,const uint32_t regs[14]);
/* Missing=0, one exact descriptor=1, malformed/ambiguous=-1. */
int kui_smash_phase_outer_profile_find(const void *,size_t,size_t *);
/* Accept an ordinary pair(flags0) or a matching diagnostic pair(flags7).
 * Diagnostic TIM1/VID1 seeds must be unique, blank, and at their fixed
 * offsets after the native menu seed. A same-build mixed pair refuses. */
bool kui_smash_phase_pair_profile(const void *outer,size_t outer_bytes,
    const void *child,size_t child_bytes,size_t native_at,uint32_t *flags);
#endif
