/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_GAME_INFO_H
#define KUI_VMU_GAME_INFO_H
#include "kui/probe.h"
#include "kui/vmu_game_candidates.h"
#include <stdbool.h>
#include <string.h>

#define KUI_VMU_GAME_INFO_CARDS_MAX 32u
struct kui_vmu_game_info_card {
    uint32_t pair;
    uint8_t identity[16];
    char label[64];
};
/* One launch/default request, never a row number or a borrowed UI pointer.
 * The launcher rechecks this identity, product membership and storage pin. */
struct kui_vmu_game_selection {
    char product[12];
    uint32_t pair;
    uint8_t identity[16];
    struct kui_vmu_game_candidates_storage storage;
    uint64_t candidates_sequence;
    bool selected;
};

/* Shell-only metadata summary. known never means hardware compatibility:
 * native module, allocation and lifetime admission still run at launch. */
struct kui_vmu_game_info {
    char product[12],label[64],notice[128];
    bool enabled,known;
    struct kui_vmu_game_candidates_storage storage;
    uint64_t candidates_sequence;
    uint32_t count,default_pair;
    bool choice_required;
    struct kui_vmu_game_info_card cards[KUI_VMU_GAME_INFO_CARDS_MAX];
};
/* Pure snapshot copy for the shell. No setting or card file is changed. */
static inline bool kui_vmu_game_info_choose(const struct kui_vmu_game_info *info,unsigned index,
    struct kui_vmu_game_selection *out) {
    if(!out) return false;
    struct kui_vmu_game_selection value={0};
    if(!info || !info->known || !info->enabled || !info->product[0] ||
       !memchr(info->product,0,sizeof(info->product)) || info->count>KUI_VMU_GAME_INFO_CARDS_MAX ||
       index>=info->count || !info->storage.card_sectors ||
       info->storage.partition_first>=info->storage.partition_end ||
       info->cards[index].pair<1u || info->cards[index].pair>9999u) {*out=value;return false;}
    bool identity=false;for(unsigned i=0;i<16u;i++) identity|=info->cards[index].identity[i]!=0;
    if(!identity) {*out=value;return false;}
    memcpy(value.product,info->product,sizeof(value.product));
    value.pair=info->cards[index].pair;memcpy(value.identity,info->cards[index].identity,16u);
    value.storage=info->storage;value.candidates_sequence=info->candidates_sequence;value.selected=true;
    *out=value;return true;
}
/* Worker owns storage. Read-only: no images, journals, assignments, profiles
 * or preferences are changed, and no card is created or activated. */
void kui_vmu_game_info_run(const char *product,bool native_route,
    struct kui_vmu_game_info *out,kui_log_fn log,kui_cancel_fn cancel);
/* Storage-worker publication. clear means Always ask; otherwise the exact
 * selected target becomes the default. An intervening catalogue/storage
 * change refuses the request. Successful output is a refreshed snapshot. */
bool kui_vmu_game_info_default(const struct kui_vmu_game_selection *selection,bool clear,
    struct kui_vmu_game_info *out,kui_log_fn log,kui_cancel_fn cancel);
#endif
