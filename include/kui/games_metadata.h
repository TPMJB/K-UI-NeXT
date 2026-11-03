/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_METADATA_H
#define KUI_GAMES_METADATA_H
#include "kui/games_activity.h"
#define KUI_GAMES_METADATA_CAP 128u
#define KUI_GAMES_METADATA_WIRE_MAX (40u+KUI_GAMES_METADATA_CAP*(64u+3u*(KUI_GAMES_FILE_CAP-1u)+32u+128u+10u+6u+8u))
struct kui_games_library_edit {char alias[129],cover_path[KUI_GAMES_FILE_CAP];};
struct kui_games_metadata_entry {
    char path[KUI_GAMES_FILE_CAP],group[33],anchor[KUI_GAMES_FILE_CAP];
    struct kui_games_library_edit edit;
    char product[11],version[7],region[9];
    uint8_t identity[32];
    bool verified;
    unsigned disc,representation;
};
struct kui_games_metadata {unsigned count;struct kui_games_metadata_entry entries[KUI_GAMES_METADATA_CAP];};
bool kui_games_library_edit_valid(const struct kui_games_library_edit *);
const struct kui_games_metadata_entry *kui_games_metadata_find(const struct kui_games_metadata *,const char *path,const char *group);
bool kui_games_metadata_upsert(struct kui_games_metadata *,const struct kui_games_metadata_entry *);
bool kui_games_metadata_encode(void *,size_t,size_t *,const struct kui_games_metadata *,uint64_t sequence);
bool kui_games_metadata_decode(struct kui_games_metadata *,uint64_t *,const void *,size_t);
/* Already-mounted, storage-worker-only dual-slot persistence. */
bool kui_games_metadata_load(struct kui_games_metadata *,kui_log_fn);
bool kui_games_metadata_save(const struct kui_games_metadata *,kui_log_fn);
#endif
