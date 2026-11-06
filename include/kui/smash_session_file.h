/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_SESSION_FILE_H
#define KUI_SMASH_SESSION_FILE_H
#include "kui/smash_card_session.h"
#include "kui/smash_phase_file.h"
struct kui_retail_storage;
enum kui_smash_session_file_result {
    KUI_SMASH_SESSION_FILE_OK=0,
    KUI_SMASH_SESSION_FILE_UNAVAILABLE=-1,
    KUI_SMASH_SESSION_FILE_CARD_FAULT=-2
};
/* capacity is a number of records, never bytes. Every operation freshly
 * resolves the fixed file and all six protected capsules with read-only
 * FatFs before unmounting. Only the meaningful header/records sectors are
 * written, with zero padding after the final record. The unused preallocated
 * tail is neither interpreted nor rewritten. This transports bytes, not
 * ownership authority: the witness engine validates binding/digest separately.
 * The caller has also matched config to the supplied profile/binding. Its
 * config shape/CRC is checked again here; its maps grant no raw-write shortcut.
 *
 * borrowed_lease=true requires this same initialized SCI device to be
 * exclusively acquired; it remains acquired on every return. Otherwise one
 * acquire/release pair is owned here. No reinitialization or metadata write.
 * CARD_FAULT poisons card.ready and must preserve ordinary launch refusal.
 * Safe missing/malformed/moved files return UNAVAILABLE without retention. */
enum kui_smash_session_file_result kui_smash_session_file_store(
    struct kui_retail_storage *,const struct kui_retail_manifest *,
    const struct kui_vmu_persist_profile *,const struct kui_smash_card_config *,
    bool borrowed_lease,const uint8_t header[512],
    const struct kui_vmu_fs_witness_record *,uint32_t count,
    struct kui_smash_phase_file_stats *);
enum kui_smash_session_file_result kui_smash_session_file_load(
    struct kui_retail_storage *,const struct kui_retail_manifest *,
    const struct kui_vmu_persist_profile *,const struct kui_smash_card_config *,
    bool borrowed_lease,uint8_t header[512],
    struct kui_vmu_fs_witness_record *,size_t capacity,uint32_t *count,
    struct kui_smash_phase_file_stats *);
#endif
