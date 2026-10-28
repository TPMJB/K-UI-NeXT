/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RECOVERY_GUARD_H
#define KUI_RECOVERY_GUARD_H
#include "kui/probe.h"

/* Caller owns the mounted volume. A persistent, job-local quarantine records
 * disagreement between SAVED-file reads. Optical disagreement is not storage
 * instability. An uncommitted capture tail needs no quarantine: Resume must
 * discard it, including Quick Resume.
 *
 * check returns false for a quarantine, malformed marker or storage failure.
 * mark publishes a CRC-checked, synced/read-back marker bound to the canonical
 * folder and immutable filesystem geometry/serial. An interrupted publication
 * also blocks check. No original track or capture metadata is changed.
 *
 * clear may ONLY be called after a complete, consistent repeat-read of every
 * saved source byte against the same validated layout. Cancellation, partial
 * scans, hashing alone, or agreement of a few suspect sectors cannot clear it.
 * A valid marker belonging to another volume/folder is never cleared.
 */
bool kui_recovery_guard_check(const char *directory,kui_log_fn log);
bool kui_recovery_guard_mark(const char *directory,kui_log_fn log);
/* Read-only preflight for clear. This does not establish read consistency. */
bool kui_recovery_guard_validate(const char *directory,kui_log_fn log);
bool kui_recovery_guard_clear(const char *directory,kui_log_fn log);
#endif
