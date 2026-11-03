/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_HISTORY_H
#define KUI_VMU_HISTORY_H
#include "kui/vmu_history_record.h"
#include "kui/vmu_game_info.h"

#define KUI_VMU_HISTORY_ROOT "0:/KUI/vmu/history"
#define KUI_VMU_HISTORY_EXPORT_ROOT "0:/KUI/exports/saves"
enum kui_vmu_history_action {
    KUI_VMU_HISTORY_LIST=0,KUI_VMU_HISTORY_CHECKPOINT=1,
    KUI_VMU_HISTORY_RESTORE=2,KUI_VMU_HISTORY_EXPORT=3
};
enum kui_vmu_history_result {
    KUI_VMU_HISTORY_OK=0,KUI_VMU_HISTORY_UNCHANGED=1,
    KUI_VMU_HISTORY_ARGUMENT=-1,KUI_VMU_HISTORY_IO=-2,
    KUI_VMU_HISTORY_INVALID=-3,KUI_VMU_HISTORY_CANCELLED=-4,
    KUI_VMU_HISTORY_STALE=-5,KUI_VMU_HISTORY_FULL=-6
};
struct kui_vmu_history_entry {
    uint64_t sequence,timestamp_seconds;
    uint32_t reason,source_pair;
    uint8_t sha256[32];
    char label[64];
};
struct kui_vmu_history_request {
    enum kui_vmu_history_action action;
    struct kui_vmu_game_selection card;
    /* LIST/CHECKPOINT use zero. RESTORE pins one listed record. EXPORT
     * uses zero for the recovered current card, or an exact listed record. */
    uint64_t sequence;
    uint8_t sha256[32];
};
struct kui_vmu_history_view {
    bool known,changed,restored,exported;
    uint32_t count,damaged;
    struct kui_vmu_game_selection card;
    struct kui_vmu_history_entry entries[KUI_VMU_HISTORY_KEEP];
    char notice[128],export_path[192];
};
/* Pure UI handoff: copy the exact sequence/hash instead of retaining a row
 * number. Restore confirmation belongs to the shell before dispatch. */
bool kui_vmu_history_choose(const struct kui_vmu_history_view *,unsigned,
    enum kui_vmu_history_action,struct kui_vmu_history_request *);
/* Mounted FatFs and exclusive storage-worker ownership. storage is freshly
 * checked before publication and after work; salt must change for imports.
 * No physical VMU access, resident operation, active image/journal overwrite
 * or implicit repair is permitted. */
struct kui_vmu_history_platform {
    void *context;
    bool (*storage)(void *,struct kui_vmu_game_candidates_storage *);
    void (*salt)(void *,uint8_t [16]);
};
int kui_vmu_history_apply(const struct kui_vmu_history_request *,
    enum kui_vmu_history_reason,uint64_t local_seconds,
    const struct kui_vmu_history_platform *,struct kui_vmu_history_view *,
    kui_log_fn,kui_cancel_fn);
/* Shell worker entry: opens and releases Games storage. */
void kui_vmu_history_run(const struct kui_vmu_history_request *,
    struct kui_vmu_history_view *,kui_log_fn,kui_cancel_fn);
/* Safe launch/return boundaries only, while the launcher owns its mounted
 * volume. A failed checkpoint must be shown; callers decide launch policy.
 * Pin helper captures the current physical SCI identity and partition. */
bool kui_vmu_history_card_pin_mounted(const char *product,unsigned pair,
    const uint8_t identity[16],struct kui_vmu_game_selection *);
int kui_vmu_history_checkpoint_mounted(const struct kui_vmu_game_selection *,
    enum kui_vmu_history_reason,struct kui_vmu_history_view *,kui_log_fn,kui_cancel_fn);
/* Checkpoint the actual resolved card, then durably publish its two-generation
 * storage/identity-pinned session receipt before loader handoff. Any previous
 * receipt is recovered first. Failure refuses a history-enabled launch. */
int kui_vmu_history_session_begin(const struct kui_vmu_game_selection *,uint64_t local_seconds,
    const struct kui_vmu_history_platform *,struct kui_vmu_history_view *,kui_log_fn,kui_cancel_fn);
/* A pending receipt means a previous launch may have run. It does not prove
 * a clean return or a complete game save. Recovered journal data is admitted,
 * checkpointed/deduplicated, then the receipt is cleared by a new generation.
 * Corrupt/foreign/stale receipts remain intact and refuse automatic recovery. */
int kui_vmu_history_session_resume(uint64_t local_seconds,const struct kui_vmu_history_platform *,
    struct kui_vmu_history_view *,kui_log_fn,kui_cancel_fn);
int kui_vmu_history_launch_mounted(const struct kui_vmu_game_selection *,
    struct kui_vmu_history_view *,kui_log_fn,kui_cancel_fn);
/* Read-only full managed-data allocation admission before a missing game card
 * is created. Caller owns mounted storage exclusively; no card is selected. */
int kui_vmu_history_allocation_mounted(kui_cancel_fn);
void kui_vmu_history_resume_run(struct kui_vmu_history_view *,kui_log_fn,kui_cancel_fn);
#endif
