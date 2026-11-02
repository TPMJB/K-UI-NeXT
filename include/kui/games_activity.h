/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_ACTIVITY_H
#define KUI_GAMES_ACTIVITY_H
#include "kui/games.h"
#define KUI_GAMES_ACTIVITY_CAP 128u
#define KUI_GAMES_ACTIVITY_WIRE_MAX (40u+KUI_GAMES_ACTIVITY_CAP*(12u+KUI_GAMES_FILE_CAP-1u+32u))
enum kui_games_filter {KUI_GAMES_FILTER_ALL,KUI_GAMES_FILTER_FAVORITES,KUI_GAMES_FILTER_RECENT,KUI_GAMES_FILTER_COUNT};
struct kui_games_activity_entry {
    char path[KUI_GAMES_FILE_CAP],group[33];
    uint64_t played;
    bool favorite;
};
struct kui_games_activity {
    uint64_t clock;
    unsigned count;
    struct kui_games_activity_entry entries[KUI_GAMES_ACTIVITY_CAP];
};
/* Ordinary images use their canonical card path. Disc sets use the catalogue
 * root plus its stable group key, so playing another disc keeps one row. */
bool kui_games_activity_key(struct kui_games_activity_entry *,const char *path,const char *group);
const struct kui_games_activity_entry *kui_games_activity_find(const struct kui_games_activity *,const char *path,const char *group);
bool kui_games_activity_favorite(struct kui_games_activity *,const char *path,const char *group,bool enabled);
bool kui_games_activity_played(struct kui_games_activity *,const char *path,const char *group);
bool kui_games_activity_encode(void *wire,size_t capacity,size_t *bytes,const struct kui_games_activity *,uint64_t sequence);
bool kui_games_activity_decode(struct kui_games_activity *,uint64_t *sequence,const void *wire,size_t bytes);
/* Caller owns an already mounted volume and excludes other writers. Missing
 * files mean empty history. Unreadable/conflicting records never get replaced.
 * Saves alternate slots and verify the complete synced record by reread. */
bool kui_games_activity_load(struct kui_games_activity *,kui_log_fn log);
bool kui_games_activity_save(const struct kui_games_activity *,kui_log_fn log);
#endif
