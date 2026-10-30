/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_GROUP_H
#define KUI_GAMES_GROUP_H
#include <stdbool.h>

#define KUI_GAMES_GROUP_NAME_CAP 128u
#define KUI_GAMES_GROUP_KEY_CAP 33u

/* Parse an explicit Disc/Disk/CD number or a separate D1-style token from a
 * single FAT-safe image/folder name. Known image extensions and an otherwise
 * empty marker wrapper are removed. Spaces/underscores are normalized; all
 * other title/region/revision text is preserved. Bare sequel numbers and a
 * leading D2 game title are not disc markers. Multiple markers, zero/oversized
 * numbers and empty titles are refused. Outputs are unchanged on failure.
 * This identifies candidates only:
 * callers must retain ambiguous/duplicate discs and check matching bases. */
bool kui_games_group_parse(const char *name,
    char base[KUI_GAMES_GROUP_NAME_CAP],unsigned *disc);
/* Stable non-security identity of an already parsed base, folding ASCII case.
 * A key match alone is insufficient: callers must also compare complete bases
 * and keep conflicting names separate. Outputs are unchanged on failure. */
bool kui_games_group_key(const char *base,char key[KUI_GAMES_GROUP_KEY_CAP]);
#endif
