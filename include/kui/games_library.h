/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_LIBRARY_H
#define KUI_GAMES_LIBRARY_H
#include "kui/games.h"
#include "kui/games_activity.h"
#include "kui/native_session_prepare.h"

/* Storage-worker APIs. A checked, sorted catalogue survives reboot; opening
 * a valid catalogue never walks game folders or stats their images. group is
 * empty for the ordinary list, or the stable key of a row's disc chooser.
 * Returned paths always remain real card paths. Artwork loads separately. */
bool kui_games_library_rows(const char *root,const char *group,unsigned offset,
    unsigned view,struct kui_games_page *out,kui_log_fn log,kui_cancel_fn cancel);
bool kui_games_library_find(const char *root,const char *group,const char *name,
    unsigned *offset,kui_log_fn log,kui_cancel_fn cancel);
bool kui_games_library_rows_filter(const char *root,const char *group,unsigned offset,
    unsigned view,unsigned filter,struct kui_games_page *out,kui_log_fn log,kui_cancel_fn cancel);
bool kui_games_library_find_filter(const char *root,const char *group,const char *name,
    unsigned filter,unsigned *offset,kui_log_fn log,kui_cancel_fn cancel);
/* A group favorite uses root+group; an ordinary image uses its exact path.
 * Call from the storage worker. Failures preserve the in-memory selection. */
bool kui_games_library_favorite_toggle(const char *root,const char *group,const char *path,kui_log_fn log);
/* Resolve before launch preparation releases catalogue memory, then record
 * only after preparation succeeds. A recent entry means a prepared launch,
 * never an inspection, read test or proof of gameplay success. */
bool kui_games_library_activity_key(const char *root,const char *path,
    struct kui_games_activity_entry *out,kui_log_fn log);
bool kui_games_library_record_played(const struct kui_games_activity_entry *,kui_log_fn log);
/* Snapshot up to four real members of the selected disc set before clearing
 * catalogue memory for a launch. An ordinary image becomes a one-disc set.
 * The selected Original/2048 dialect is used for other members when present. */
bool kui_games_library_session_request(const char *root,const char *path,
    struct kui_native_session_request *out,kui_log_fn log,kui_cancel_fn cancel);
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
