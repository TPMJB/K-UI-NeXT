/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_STORAGE_H
#define KUI_GAMES_STORAGE_H
#include "kui/probe.h"
#include <stdbool.h>

/* Games browsing jobs (listing, find, inspect, box art and the remembered
 * selection) share one card connection and one mounted filesystem while the
 * storage worker holds the session. Every other job still connects and
 * mounts for itself, so the worker releases the session before any of them.
 * Without a hold, each browsing job connects and releases exactly as before.
 * Storage worker only. */
enum kui_games_storage_result {
    KUI_GAMES_STORAGE_OK, KUI_GAMES_STORAGE_NO_CARD, KUI_GAMES_STORAGE_NO_MOUNT
};
/* Connect and mount "0:", or reuse the session a held earlier job kept. A
 * session another mount or unmount invalidated is replaced by a new one. A
 * mount failure disconnects again; nothing is left open on failure. */
enum kui_games_storage_result kui_games_storage_open(kui_log_fn log);
/* End one job. The session stays open when held and `keep` (the job met no
 * storage error; a stop is not an error); otherwise unmount and disconnect.
 * False only when an unmount this call performed failed. */
bool kui_games_storage_done(bool keep);
void kui_games_storage_hold(bool hold);
/* Unmount and disconnect now if a session is open. False if unmount failed. */
bool kui_games_storage_release(void);
bool kui_games_storage_active(void);
/* Jobs finished on an open session, so the worker can time an idle one out. */
unsigned kui_games_storage_jobs(void);
#endif
