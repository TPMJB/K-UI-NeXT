/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_WIFI_PACE_H
#define KUI_WIFI_PACE_H
#include <stdbool.h>
#include <stdint.h>

/* Feedback is taken after kwl_receive has checked the frame, rather than
 * checksumming it a second time in the platform driver. TRAIN is sent only
 * after this session's HELLO and four intact large echo round trips. */
#define KUI_WIFI_PACE_VALID 0x01u
#define KUI_WIFI_PACE_PROGRESS 0x02u
#define KUI_WIFI_PACE_ACTIVE 0x04u
#define KUI_WIFI_PACE_DUPLICATE 0x08u
#define KUI_WIFI_PACE_RESET 0x10u
#define KUI_WIFI_PACE_TRAIN 0x20u
#define KUI_WIFI_PACE_RECOVER 32u
#define KUI_WIFI_PACE_STALL 3u
#define KUI_WIFI_PACE_LEVELS 6u

struct kui_wifi_pace {
    bool trained, wired;
    unsigned level, stalled, stable;
};
static inline void kui_wifi_pace_reset(struct kui_wifi_pace *p) {
    *p = (struct kui_wifi_pace){0};
}
static inline unsigned kui_wifi_pace_gap_us(const struct kui_wifi_pace *p) {
    static const unsigned gaps[KUI_WIFI_PACE_LEVELS] = {500u, 1000u, 2000u, 4000u, 8000u, 20000u};
    return p->trained && !p->wired ? gaps[p->level] : 20000u;
}
static inline void kui_wifi_pace_ready(struct kui_wifi_pace *p) {
    /* Once the physical handshake works, never replace its timeout with a
     * guessed delay. Opening the port again starts a fresh determination. */
    p->wired = true;
}
static inline void kui_wifi_pace_backoff(struct kui_wifi_pace *p) {
    if(p->level + 1u < KUI_WIFI_PACE_LEVELS) ++p->level;
    p->stalled = p->stable = 0;
}
static inline void kui_wifi_pace_feedback(struct kui_wifi_pace *p, unsigned flags) {
    if(flags & KUI_WIFI_PACE_RESET) {
        bool wired = p->wired;
        kui_wifi_pace_reset(p);
        p->wired = wired;
        return;
    }
    if(flags & KUI_WIFI_PACE_TRAIN) {
        if(!p->wired) {
            p->trained = true;
            p->level = p->stalled = p->stable = 0;
        }
        return;
    }
    if(!p->trained || p->wired) return;
    if(!(flags & KUI_WIFI_PACE_VALID) ||
       ((flags & KUI_WIFI_PACE_ACTIVE) && (flags & KUI_WIFI_PACE_DUPLICATE))) {
        kui_wifi_pace_backoff(p);
        return;
    }
    if(flags & KUI_WIFI_PACE_PROGRESS) {
        p->stalled = 0;
        if(p->level && ++p->stable >= KUI_WIFI_PACE_RECOVER) {
            --p->level;
            p->stable = 0;
        }
    } else if(flags & KUI_WIFI_PACE_ACTIVE) {
        /* A normal pipelined answer needs a transfer to come back. Keep
         * earned progress across that latency, but reset it on a stall. */
        if(++p->stalled >= KUI_WIFI_PACE_STALL) kui_wifi_pace_backoff(p);
    } else {
        /* Repeated idle headers are normal, but are not proof that the
         * slave can sustain a faster stream. They cannot speed us up. */
        p->stalled = 0;
    }
}
#endif
