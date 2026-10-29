/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DRIVE_CONTROLS_H
#define KUI_DRIVE_CONTROLS_H
#include <stdbool.h>
#include <stdint.h>

/* Session-only selection indices. Zero leaves the firmware setting unchanged.
 * Speed: unchanged, maximum, standard, 2x, 4x, 6x, 8x, 10x, 12x.
 * Retries: unchanged, 0, 1, 2, 4, 8. These are firmware decoding retries per
 * command, independent of application attempts, recovery passes and deadlines. */
#define KUI_DRIVE_SPEED_CHOICES 9u
#define KUI_DRIVE_RETRY_CHOICES 6u
struct kui_drive_options { unsigned speed, retries; };
struct kui_drive_mode { uint32_t speed, standby, read_flags, read_retry; };
struct kui_drive_control_ops {
    void *ctx;
    /* Query/set must use bounded commands and retain all firmware-visible
     * storage after failed abort. safe() is false while ownership is uncertain,
     * after media change, or after a fatal adapter error. Cleanup ignores user
     * cancellation but never ignores safe(). */
    bool (*safe)(void *);
    bool (*query)(void *,struct kui_drive_mode *);
    bool (*set)(void *,const struct kui_drive_mode *);
};
enum kui_drive_control_result {
    KUI_DRIVE_CONTROL_OK, KUI_DRIVE_CONTROL_INVALID, KUI_DRIVE_CONTROL_UNSAFE,
    KUI_DRIVE_CONTROL_QUERY_FAILED, KUI_DRIVE_CONTROL_SET_FAILED,
    KUI_DRIVE_CONTROL_MISMATCH
};
struct kui_drive_controls {
    struct kui_drive_mode original, requested, observed;
    bool active, modified, snapshot_valid, verified, trial;
};
const char *kui_drive_speed_name(unsigned selection);
const char *kui_drive_retry_name(unsigned selection);
const char *kui_drive_control_result_name(enum kui_drive_control_result result);
bool kui_drive_options_valid(const struct kui_drive_options *options);
enum kui_drive_control_result kui_drive_controls_begin(struct kui_drive_controls *state,
    const struct kui_drive_control_ops *ops,const struct kui_drive_options *options);
/* Explicit diagnostic session: snapshot even when starting with the original
 * mode. Every later selection derives from that same restoration snapshot. */
enum kui_drive_control_result kui_drive_controls_trial_begin(struct kui_drive_controls *state,
    const struct kui_drive_control_ops *ops);
/* Trial-only selection. A rejected setting is rolled back to the previous
 * verified tuple. A non-OK result with verified=false must end the trial;
 * cleanup still attempts original-mode restoration when ownership permits. */
enum kui_drive_control_result kui_drive_controls_select(struct kui_drive_controls *state,
    const struct kui_drive_control_ops *ops,const struct kui_drive_options *options);
enum kui_drive_control_result kui_drive_controls_reapply(struct kui_drive_controls *state,
    const struct kui_drive_control_ops *ops);
/* Always clears the session. An unsafe result means no restore command was
 * issued; the caller must prevent ordinary drive use until a restart. */
enum kui_drive_control_result kui_drive_controls_end(struct kui_drive_controls *state,
    const struct kui_drive_control_ops *ops);
#endif
