/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SALVAGE_SESSION_POLICY_H
#define KUI_SALVAGE_SESSION_POLICY_H
#include <stdbool.h>
#include <stdint.h>

/* Worker-owned invalidation policy only. This is not a validated receipt and
 * never grants repair authority; the salvage core independently admits it. */
struct kui_salvage_session_policy {
    uint64_t epoch;
    uint32_t errors;
    bool observed_errors, parked;
};
static inline void kui_salvage_session_policy_forget(struct kui_salvage_session_policy *s) {
    if(!++s->epoch)++s->epoch;
    s->parked=false;
}
static inline bool kui_salvage_session_policy_action(unsigned action,bool ordinary_continue) {
    /* Explicit report writes and ordinary insertion identification cannot
     * mutate the selected salvage sources. Every other foreground action
     * revokes the receipt, including read-only diagnostics and other apps. */
    return !action || action==3u || action==12u || (action==47u && ordinary_continue);
}
static inline bool kui_salvage_session_policy_errors(struct kui_salvage_session_policy *s,
        uint32_t total) {
    /* A reset is a change too. A saturated counter cannot prove that no new
     * failure happened, so it can never support a retained receipt. */
    bool changed=(s->observed_errors && total!=s->errors) || total==UINT32_MAX;
    s->observed_errors=true;s->errors=total;return changed;
}
#endif
