/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_HISTORY_RECORD_H
#define KUI_VMU_HISTORY_RECORD_H
#include "kui/vmu_game_card.h"
#define KUI_VMU_HISTORY_RECORD_BYTES 512u
#define KUI_VMU_HISTORY_KEEP 8u
enum kui_vmu_history_reason {
    KUI_VMU_HISTORY_MANUAL=1,KUI_VMU_HISTORY_BEFORE_LAUNCH=2,KUI_VMU_HISTORY_AFTER_RETURN=3
};
struct kui_vmu_history_record {
    uint64_t sequence,timestamp_seconds;
    uint32_t source_pair,reason,image_crc32;
    uint8_t source_identity[16],sha256[32];
    char product[KUI_VMU_GAME_CARD_KEY_BYTES];
};
/* Fixed canonical wire: KUIVMUH1, version1, bytes512, sequence16,
 * local RTC seconds24 (zero unknown), pair32, reason36, identity40,
 * image SHA25656, CRC3288, product92..103; reserved104..507 zero,
 * record CRC32 at508. The record is published after its complete image.
 * It describes a recovered card boundary, never a whole-game-save commit. */
bool kui_vmu_history_record_encode(const struct kui_vmu_history_record *,uint8_t [512]);
bool kui_vmu_history_record_decode(const void *,size_t,struct kui_vmu_history_record *);
#endif
