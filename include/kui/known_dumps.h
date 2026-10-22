/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_KNOWN_DUMPS_H
#define KUI_KNOWN_DUMPS_H
#include "kui/probe.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Compare a finished capture with an INDEPENDENT reference: the per-track size
 * and CRC32 of known-good dumps from the Redump and TOSEC catalogues, held on
 * the card as two compact line files (data/known-dumps/, copied to KUI/).
 *
 * This is what makes end-of-capture verification take milliseconds instead of a
 * full re-read of the card, and it proves more: a re-read only shows that the
 * card holds what the drive returned, while a catalogue match shows the bytes
 * equal the canonical dump. The lookup is a single streaming pass over a file
 * of a few hundred KiB, so it needs no memory for the catalogue.
 *
 * Grades, best first:
 *   FULL_MATCH   every track of the capture matches one catalogue entry exactly
 *   DATA_MATCH   every data track matches (the entry may list more tracks)
 *   IDENTIFIED   the data tracks the entry lists match, but it lists fewer than
 *                the capture has (the bundled Redump catalogue lists one)
 *   PARTIAL      some data tracks match
 *   NO_MATCH     nothing matched; INCONCLUSIVE, not a failure (see the note in
 *                kui_known_text): another revision, a track boundary convention,
 *                a disc not in the catalogue, or a read error all look the same */
enum kui_known_result {
    KUI_KNOWN_ERROR = -1,
    KUI_KNOWN_CANCELLED = 0,
    KUI_KNOWN_NO_DATABASE,
    KUI_KNOWN_NO_MATCH,
    KUI_KNOWN_PARTIAL,
    KUI_KNOWN_IDENTIFIED,
    KUI_KNOWN_DATA_MATCH,
    KUI_KNOWN_FULL_MATCH
};
struct kui_known_track { uint32_t number; bool data; uint64_t bytes; uint32_t crc32; };
struct kui_known_summary {
    enum kui_known_result result;
    char catalog[16];   /* "Redump", "TOSEC", or "Redump+TOSEC" when neither matched */
    char name[192];     /* the catalogue's name for the best match */
};

/* Supplies one line at a time, without its newline; false at end of input. */
typedef bool (*kui_known_line_fn)(void *ctx, char *line, size_t capacity);
/* Search one catalogue given as a line source. The first line must be the
 * catalogue header. Fills name with the best match's name when there is one. */
enum kui_known_result kui_known_search(kui_known_line_fn next, void *ctx,
    const struct kui_known_track *tracks, unsigned count, char *name, size_t name_capacity,
    bool (*cancelled)(void *), void *cancel_ctx);
/* Search a catalogue file on the mounted card. A missing file is NO_DATABASE. */
enum kui_known_result kui_known_search_file(const char *path,
    const struct kui_known_track *tracks, unsigned count, char *name, size_t name_capacity,
    bool (*cancelled)(void *), void *cancel_ctx);
/* Redump first, then TOSEC, keeping the better grade. */
void kui_known_check(const char *redump_path, const char *tosec_path,
    const struct kui_known_track *tracks, unsigned count, struct kui_known_summary *out,
    bool (*cancelled)(void *), void *cancel_ctx);
const char *kui_known_text(enum kui_known_result result);
#endif
