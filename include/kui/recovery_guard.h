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
 * Failed removal/sync blocks subsequent check/validate calls for this boot,
 * even if the directory entry has disappeared. Marker restoration is best
 * effort; physical failure atomicity is not promised by FatFs/card firmware.
 */
bool kui_recovery_guard_check(const char *directory,kui_log_fn log);
bool kui_recovery_guard_mark(const char *directory,kui_log_fn log);
/* Read-only preflight for clear. This does not establish read consistency. */
bool kui_recovery_guard_validate(const char *directory,kui_log_fn log);
bool kui_recovery_guard_clear(const char *directory,kui_log_fn log);
/* Block recovery if marker publication or post-clear cleanup is uncertain. Caller
 * owns the volume; restoration is best effort and the boot-local block is
 * unconditional, including when the volume can no longer be accessed. */
void kui_recovery_guard_reblock(const char *directory,kui_log_fn log);
#endif
