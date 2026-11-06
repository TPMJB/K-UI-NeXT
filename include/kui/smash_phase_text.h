/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_PHASE_TEXT_H
#define KUI_SMASH_PHASE_TEXT_H
#include "kui/smash_phase_trace.h"
#include "kui/vmu_fs_admission.h"
#define KUI_SMASH_PHASE_TEXT_BYTES 4096u
/* These counters describe this cold owner, not accumulated prior owners.
 * The receipt generation/sequence identifies the live collection handoff. */
struct kui_smash_phase_session_stats {
    uint32_t full_admissions,attaches,receipt_generation,receipt_sequence;
    uint32_t receipt_valid,receipt_blank,native_gin_route,boot2_calls;
    uint32_t witness_sectors,witness_runs;
    uint32_t allocation_epoch; /* phase12..13 actually used the epoch path */
    /* Actual SD callback time within phase12..13; no RTC/G2 sampling.
     * flags preserve CYCLES/NOMINAL_MS only while every observed command
     * has compatible PMCR0/FRQCR and the accumulated sum remains bounded.
     * Zero flags means unavailable, not zero duration. */
    uint64_t metadata_io_cycles;
    uint32_t metadata_io_flags,metadata_io_frqcr;
    uint32_t metadata_dma,metadata_fallback;
};
/* Pure rendering. Unavailable clocks are printed as NA, never as zero time.
 * This report ends at native worker installation. It does not pretend to
 * time subsequent cartridge reads, BUP jobs or the options menu. */
bool kui_smash_phase_text_format(char out[KUI_SMASH_PHASE_TEXT_BYTES],
    const struct kui_smash_phase_trace *,const char build[13],const char *selection,
    const struct kui_vmu_fs_stats *,const uint32_t io[6]);
bool kui_smash_phase_text_format_session(char out[KUI_SMASH_PHASE_TEXT_BYTES],
    const struct kui_smash_phase_trace *,const char build[13],const char *selection,
    const struct kui_vmu_fs_stats *,const uint32_t io[6],
    const struct kui_smash_phase_session_stats *optional_session);
#endif
