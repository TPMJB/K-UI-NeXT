/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_SELECTION_H
#define KUI_GAMES_SELECTION_H
#include "kui/destination.h"
#include "kui/probe.h"

struct kui_games_selection {
    char root[KUI_DEST_ROOT_CAP],name[KUI_DEST_NAME_CAP];
};
#define KUI_GAMES_SELECTION_BYTES 512u
#define KUI_GAMES_SELECTION_PATH_A "0:/KUI/apps/games/selection-a.bin"
#define KUI_GAMES_SELECTION_PATH_B "0:/KUI/apps/games/selection-b.bin"
void kui_games_selection_default(struct kui_games_selection *);
bool kui_games_selection_valid(const struct kui_games_selection *);
bool kui_games_selection_equal(const struct kui_games_selection *,const struct kui_games_selection *);
/* Fixed records with CRC/sequence. Invalid input preserves outputs. */
bool kui_games_selection_encode(uint8_t out[KUI_GAMES_SELECTION_BYTES],const struct kui_games_selection *,uint64_t sequence);
bool kui_games_selection_decode(struct kui_games_selection *,uint64_t *sequence,const uint8_t record[KUI_GAMES_SELECTION_BYTES]);
/* Mounted-volume helpers; caller owns filesystem/storage worker. Missing or
 * corrupt records use /Games with no selected row. I/O/conflicts return false.
 * Saves alternate slots and verify every byte; unchanged selections do not write. */
bool kui_games_selection_load(struct kui_games_selection *,kui_log_fn);
bool kui_games_selection_save(const struct kui_games_selection *,kui_log_fn);
#endif
