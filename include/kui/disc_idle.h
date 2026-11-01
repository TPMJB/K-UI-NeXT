/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_IDLE_H
#define KUI_DISC_IDLE_H
#include <stdbool.h>
#include <stdint.h>

#define KUI_DISC_IDLE_GRACE_MS UINT64_C(45000)
struct kui_disc_idle {uint64_t deadline;bool armed;};
enum kui_disc_idle_result {KUI_DISC_IDLE_NONE,KUI_DISC_IDLE_STOP,KUI_DISC_IDLE_GRACE};
static inline void kui_disc_idle_cancel(struct kui_disc_idle *idle) {
    idle->deadline=0u;idle->armed=false;
}
/* Foreground action numbers are the worker's fixed dispatch ABI. Inspection
 * is part of a possible disc launch, not a completed capture session. */
static inline bool kui_disc_idle_cleanup(unsigned action) {
    return action==12u || action==1u || (action>=4u && action<=7u) || action==22u ||
        action==49u || action==81u || action==96u || action==97u || action==103u ||
        action==104u || action==105u || (action>=46u && action<=48u);
}
static inline bool kui_disc_idle_inspection(unsigned action) {
    return action==12u || action==103u || action==105u;
}
/* The sole I/O worker owns this state. A successful real read earns one finite
 * idle grace; successful VMU-Off review performs no optical I/O and preserves
 * an existing grace without inventing another drive operation. Errors and
 * cancellation retain immediate cleanup. CD audio retains its own stop path. */
static inline enum kui_disc_idle_result kui_disc_idle_complete(struct kui_disc_idle *idle,
    unsigned action,bool passed,bool read_disc,uint64_t now,bool audio_owned) {
    if(audio_owned) {kui_disc_idle_cancel(idle);return KUI_DISC_IDLE_NONE;}
    if(!kui_disc_idle_cleanup(action)) return KUI_DISC_IDLE_NONE;
    if(kui_disc_idle_inspection(action) && passed) {
        if(read_disc) {
            idle->deadline=now>UINT64_MAX-KUI_DISC_IDLE_GRACE_MS?UINT64_MAX:now+KUI_DISC_IDLE_GRACE_MS;
            idle->armed=true;
        }
        return KUI_DISC_IDLE_GRACE;
    }
    kui_disc_idle_cancel(idle);return KUI_DISC_IDLE_STOP;
}
/* Consume the deadline before issuing STOP, so even a refused stop cannot
 * become a polling command loop. Pending foreground work always has priority. */
static inline bool kui_disc_idle_take_stop(struct kui_disc_idle *idle,uint64_t now,
    bool worker_idle,bool audio_owned,bool reset_required,bool media_absent) {
    if(audio_owned || reset_required || media_absent) {kui_disc_idle_cancel(idle);return false;}
    if(!idle->armed || !worker_idle || now<idle->deadline) return false;
    kui_disc_idle_cancel(idle);return true;
}
#endif
