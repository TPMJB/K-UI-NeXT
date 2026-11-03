/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_SCAN_LOG_H
#define KUI_GAMES_SCAN_LOG_H
#include "kui/apps.h"

#define KUI_GAMES_SCAN_LOG_TEXT_MAX 1408u
#define KUI_GAMES_SCAN_LOG_WIRE_MAX 1536u

/* Mounted worker-only APIs. No storage session or file handle survives a
 * call. load returns the caller's plain text body and an extra terminating
 * NUL; cap must fit both. Missing slots return empty text successfully.
 * Invalid/conflicting slots without a valid survivor and filesystem errors
 * fail without changing out or *bytes. */
bool kui_games_scan_log_load(char *out,size_t cap,size_t *bytes,kui_log_fn log);
/* Alternate fixed scan-a.txt/scan-b.txt slots under KUI/games-library.
 * text need not have a terminating NUL; controls other than tab/CR/LF and
 * embedded NUL are refused. Successful writes are synced, closed and fully
 * reread before return. A pending record is not eligible for recovery.
 * The prior complete slot is retained when publication fails; a failure
 * during the final commit may also leave a complete newer record. If both
 * readable slots are invalid, a fresh record may replace one invalid slot.
 * Read errors and conflicting equal sequences always refuse publication. */
bool kui_games_scan_log_write(const char *text,size_t bytes,kui_log_fn log);
/* Read-only startup display. Missing/empty records stay silent; bounded
 * chunks preserve the complete text without placing it on the stack. */
void kui_games_scan_log_show(kui_log_fn log);
#endif
