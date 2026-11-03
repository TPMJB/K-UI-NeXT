/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_GROUP_H
#define KUI_GAMES_GROUP_H
#include <stdbool.h>
#include <stdint.h>

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
/* Capture collision names identify a candidate family, never a disc number.
 * Remove a known image extension and one exact trailing ' (2)'..' (9999)'
 * suffix, then trim outer spaces. Unrecognized numeric suffixes stay intact.
 * The input must be one bounded FAT-safe name. Failure preserves base. */
bool kui_games_group_capture_base(const char *name,char base[KUI_GAMES_GROUP_NAME_CAP]);

struct kui_games_group_evidence {
    char title[129],product[11],version[7],region[9];
    unsigned disc,total;
};
/* Read a bounded IP.BIN sector without I/O. Only native GD-ROM media with
 * explicit n/m (1 <= n <= m, 2 <= m <= 9), printable nonempty fixed fields,
 * and an uppercase alphanumeric/hyphen product code provide evidence.
 * Preserve actual field case and qualifiers, trimming only outer spaces.
 * Failure preserves out. This is grouping evidence, not image verification. */
bool kui_games_group_evidence_read(const uint8_t ip[2048],struct kui_games_group_evidence *out);
/* Compare complete valid release fields and total count exactly. Disc number
 * is deliberately excluded; callers must reject duplicate disc numbers. */
bool kui_games_group_evidence_equal(const struct kui_games_group_evidence *a,
    const struct kui_games_group_evidence *b);
/* Stable non-security identity of an already parsed base, folding ASCII case.
 * A key match alone is insufficient: callers must also compare complete bases
 * and keep conflicting names separate. Outputs are unchanged on failure. */
bool kui_games_group_key(const char *base,char key[KUI_GAMES_GROUP_KEY_CAP]);
#endif
