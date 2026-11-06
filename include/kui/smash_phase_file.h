/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_PHASE_FILE_H
#define KUI_SMASH_PHASE_FILE_H
#include "kui/smash_menu_capsule.h"
#include <stddef.h>
#include <stdint.h>

#define KUI_SMASH_PHASE_FILE_PATH "0:/KUI/apps/games/smash-native-phases.txt"
#define KUI_SMASH_PHASE_FILE_BYTES 4096u
#define KUI_SMASH_PHASE_FILE_PREFIX "KUI SMASH PHASE TRACE v1\nBUILD "
#define KUI_SMASH_PHASE_FILE_HEADER_BYTES (sizeof(KUI_SMASH_PHASE_FILE_PREFIX)-1u+13u)
#define KUI_SMASH_PHASE_FILE_READ_LIMIT 256u

struct kui_retail_storage;
enum kui_smash_phase_file_result {
    KUI_SMASH_PHASE_FILE_OK=0,
    KUI_SMASH_PHASE_FILE_UNAVAILABLE=-1,
    /* The caller must retain the normal storage refusal after this result.
     * A partially written report is harmless, but uncertain card state must
     * never be treated as a successful diagnostic and resumed silently. */
    KUI_SMASH_PHASE_FILE_CARD_FAULT=-2
};
struct kui_smash_phase_file_stats {
    uint32_t reads,writes,identities,detail;
};
/* Cold stage only, with IMASK15 and exclusive initialized SCI ownership.
 * The GUI has preallocated exactly4096 bytes and published the same header.
 * FatFs is a read-only pathname/geometry mapper: its disk_write always
 * refuses. The file is closed and unmounted before exactly eight mapped
 * data sectors are overwritten; no allocation, directory, FAT or bitmap is
 * written. Every block retains the normal protocol/CRC/status checks.
 *
 * The prepared VMU profile has already passed fresh ownership admission.
 * Its current geometry/card identity is rechecked here, and the report's
 * visited full clusters must not alias any GDI, VMU or supplied capsule
 * allocation. This API never trusts a persisted raw report map.
 *
 * Bounded seeks inspect at most eight report sectors/clusters. Do not replace
 * them with CREATE_LINKMAP: that upstream operation has no chain-cycle
 * bound. Missing/malformed/unavailable report is best effort. I/O uncertainty
 * returns CARD_FAULT and leaves the card poisoned. No worker RAM survives. */
enum kui_smash_phase_file_result kui_smash_phase_file_write(
    struct kui_retail_storage *,const struct kui_retail_manifest *,
    const struct kui_vmu_persist_profile *,
    const struct kui_smash_menu_capsule_runs *forbidden,size_t forbidden_count,
    const char build[13],const char text[KUI_SMASH_PHASE_FILE_BYTES],
    struct kui_smash_phase_file_stats *);
#endif
