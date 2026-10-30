/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_LIBRARY_H
#define KUI_GAMES_LIBRARY_H
#include "kui/games.h"

/* Storage-worker APIs. A checked, sorted catalogue survives reboot; opening
 * a valid catalogue never walks game folders or stats their images. group is
 * empty for the ordinary list, or the stable key of a row's disc chooser.
 * Returned paths always remain real card paths. Artwork loads separately. */
bool kui_games_library_rows(const char *root,const char *group,unsigned offset,
    unsigned view,struct kui_games_page *out,kui_log_fn log,kui_cancel_fn cancel);
bool kui_games_library_find(const char *root,const char *group,const char *name,
    unsigned *offset,kui_log_fn log,kui_cancel_fn cancel);
/* Scans build a complete replacement from fresh directory/title discovery.
 * Rebuild additionally permits repair of a damaged catalogue-generation
 * ledger after removing only generated index files. Stop before commit or a
 * failed replacement preserves the previous complete RAM catalogue. Only
 * KUI/games-library is written (the existing view helper may save its view). */
bool kui_games_library_scan(const char *root,bool rebuild,kui_log_fn log,kui_cancel_fn cancel);
/* Drop RAM only (storage switch, launch, memory reclamation). */
void kui_games_library_clear(void);
/* RAM-only readiness check, including after a stopped replacement scan. */
bool kui_games_library_ready(const char *root);
/* Advance the checked generation before K-UI changes game files. All indexed
 * roots are invalidated, conservatively, regardless of root. A false result
 * means the caller must not assume a next-boot index has been invalidated. */
bool kui_games_library_invalidate(const char *root,kui_log_fn log);

#endif
