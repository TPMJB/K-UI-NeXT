/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DRIVE_SPEED_PROBE_H
#define KUI_DRIVE_SPEED_PROBE_H
#include "kui/capture.h"

#define KUI_SPEED_PROBE_PROFILES 9u
#define KUI_SPEED_PROBE_REFERENCES 3u
#define KUI_SPEED_PROBE_WINDOW_SECTORS 64u
#define KUI_SPEED_PROBE_BUDGET_MS 300000u

struct kui_drive_speed_probe_status {
    char message[128];
    unsigned selection;
    uint32_t current_fad;
    unsigned references,done,total;
    bool complete,failed,stopped,budget_expired;
    bool tried[KUI_SPEED_PROBE_PROFILES],verified[KUI_SPEED_PROBE_PROFILES];
    unsigned matched[KUI_SPEED_PROBE_PROFILES],attempted[KUI_SPEED_PROBE_PROFILES];
    uint64_t read_ms[KUI_SPEED_PROBE_PROFILES];
};

/* Selection zero uses the entry speed with one firmware retry. Selections
 * 1..8 request protocol speed codes 0..7, also with one retry. select returns
 * 1 only after complete mode verification, 0 only after a rejected mode was
 * safely rolled back, and -1 when no further optical work is safe.
 *
 * Caller owns the optical session and always attempts its bounded original
 * mode restoration afterward. This probe never mounts or writes storage,
 * changes saved jobs, calls recover/INIT, or accepts a changed reference.
 * All callbacks use ops->ctx. The five-minute soft budget includes mode
 * selection/reference reads and completes the active bounded command first.
 * A matching read is not proof of a physical speed or optical-cache change.
 * Returns true only after all nine selections have completed or been safely
 * rejected. Rejected speed codes are normal diagnostic outcomes. */
bool kui_drive_speed_probe_run(const struct kui_capture_plan *plan,
    const struct kui_capture_ops *ops,int (*select)(void *,unsigned),
    void (*progress)(void *,const struct kui_drive_speed_probe_status *),
    struct kui_drive_speed_probe_status *out);
#endif
