/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_control.h"

enum kui_cdda_control_result kui_cdda_control_init(struct kui_cdda_control *c, uint32_t frames) {
    if(!c || !frames) return KUI_CDDA_CONTROL_INVALID;
    *c=(struct kui_cdda_control){.current={.state=KUI_CDDA_CONTROL_STOPPED,.end=frames},
        .source_frames=frames,.initialized=true};
    return KUI_CDDA_CONTROL_OK;
}
enum kui_cdda_control_result kui_cdda_control_request(struct kui_cdda_control *c,
    const struct kui_cdda_control_request *r, struct kui_cdda_control_action *out) {
    if(!c || !c->initialized || !r || !out || r->command>KUI_CDDA_CONTROL_STATUS ||
       r->command<KUI_CDDA_CONTROL_PLAY) return KUI_CDDA_CONTROL_INVALID;
    const struct kui_cdda_control_status *s=&c->current;
    struct kui_cdda_control_action action={.command=r->command,.target_state=s->state,
        .epoch=s->epoch,.first=s->first,.end=s->end,.frame=s->frame,.loops=s->loops,.repeat=s->repeat};
    if(r->command==KUI_CDDA_CONTROL_STATUS) {*out=action;return KUI_CDDA_CONTROL_OK;}
    if(s->pending && r->command!=KUI_CDDA_CONTROL_STOP && r->command!=KUI_CDDA_CONTROL_PLAY)
        return KUI_CDDA_CONTROL_BUSY;
    bool needed=true;
    switch(r->command) {
    case KUI_CDDA_CONTROL_PLAY:
        if(r->first>=r->end || r->end>c->source_frames) return KUI_CDDA_CONTROL_INVALID;
        action.first=action.frame=r->first;action.end=r->end;action.repeat=r->repeat;
        action.loops=0;action.target_state=KUI_CDDA_CONTROL_PLAYING;break;
    case KUI_CDDA_CONTROL_STOP:
        needed=s->state!=KUI_CDDA_CONTROL_STOPPED || s->pending;
        action.target_state=KUI_CDDA_CONTROL_STOPPED;break;
    case KUI_CDDA_CONTROL_PAUSE:
        if(s->state==KUI_CDDA_CONTROL_PAUSED) needed=false;
        else if(s->state!=KUI_CDDA_CONTROL_PLAYING) return KUI_CDDA_CONTROL_STATE;
        action.target_state=KUI_CDDA_CONTROL_PAUSED;break;
    case KUI_CDDA_CONTROL_RESUME:
        if(s->state==KUI_CDDA_CONTROL_PLAYING) needed=false;
        else if(s->state!=KUI_CDDA_CONTROL_PAUSED || s->frame>=s->end) return KUI_CDDA_CONTROL_STATE;
        action.target_state=KUI_CDDA_CONTROL_PLAYING;break;
    case KUI_CDDA_CONTROL_SEEK:
        if(s->state==KUI_CDDA_CONTROL_FAULT) return KUI_CDDA_CONTROL_STATE;
        if(r->frame<s->first || r->frame>s->end) return KUI_CDDA_CONTROL_INVALID;
        action.frame=r->frame;action.loops=0;
        if(r->frame==s->end) action.target_state=KUI_CDDA_CONTROL_EOF;
        else if(s->state==KUI_CDDA_CONTROL_EOF) action.target_state=KUI_CDDA_CONTROL_STOPPED;
        break;
    case KUI_CDDA_CONTROL_LOOP:
        if(s->state==KUI_CDDA_CONTROL_EOF || s->state==KUI_CDDA_CONTROL_FAULT)
            return KUI_CDDA_CONTROL_STATE;
        needed=s->repeat!=r->repeat;action.repeat=r->repeat;break;
    case KUI_CDDA_CONTROL_STATUS: break;
    }
    if(!needed) {*out=action;return KUI_CDDA_CONTROL_OK;}
    if(c->generation==UINT32_MAX) return KUI_CDDA_CONTROL_OVERFLOW;
    action.epoch=c->generation+1u;action.pending=true;
    c->generation=action.epoch;c->action=action;c->current.pending=true;
    c->current.pending_epoch=action.epoch;*out=action;
    return KUI_CDDA_CONTROL_OK;
}
enum kui_cdda_control_result kui_cdda_control_complete(struct kui_cdda_control *c,
    uint32_t epoch, bool success) {
    if(!c || !c->initialized) return KUI_CDDA_CONTROL_INVALID;
    if(!c->current.pending || epoch!=c->action.epoch) return KUI_CDDA_CONTROL_STALE;
    c->current.pending=false;c->current.pending_epoch=0;
    c->action.pending=false;c->current.epoch=epoch;
    if(!success) {c->current.state=KUI_CDDA_CONTROL_FAULT;return KUI_CDDA_CONTROL_IO;}
    const struct kui_cdda_control_action *a=&c->action;
    c->current.state=a->target_state;c->current.first=a->first;c->current.end=a->end;
    c->current.frame=a->frame;c->current.loops=a->loops;c->current.repeat=a->repeat;
    c->origin_frame=a->frame;c->origin_loops=a->loops;c->last_played=0;
    return KUI_CDDA_CONTROL_OK;
}
enum kui_cdda_control_result kui_cdda_control_observe(struct kui_cdda_control *c,
    uint32_t epoch, uint32_t played) {
    if(!c || !c->initialized) return KUI_CDDA_CONTROL_INVALID;
    if(c->current.pending) return KUI_CDDA_CONTROL_BUSY;
    if(epoch!=c->current.epoch) return KUI_CDDA_CONTROL_STALE;
    if(c->current.state!=KUI_CDDA_CONTROL_PLAYING) return KUI_CDDA_CONTROL_STATE;
    if(played<c->last_played) return KUI_CDDA_CONTROL_INVALID;
    uint32_t frame,loops=c->origin_loops;
    enum kui_cdda_control_state state=KUI_CDDA_CONTROL_PLAYING;
    if(!c->current.repeat) {
        uint32_t available=c->current.end-c->origin_frame;
        if(played>=available) {frame=c->current.end;state=KUI_CDDA_CONTROL_EOF;}
        else frame=c->origin_frame+played;
    } else {
        uint32_t span=c->current.end-c->current.first;
        uint32_t relative=c->origin_frame-c->current.first;
        uint32_t turns=played/span,remainder=played%span;
        if(remainder>=span-relative) {
            if(turns==UINT32_MAX) return KUI_CDDA_CONTROL_OVERFLOW;
            turns++;relative=remainder-(span-relative);
        } else relative+=remainder;
        if(turns>UINT32_MAX-loops) return KUI_CDDA_CONTROL_OVERFLOW;
        loops+=turns;frame=c->current.first+relative;
    }
    c->current.frame=frame;c->current.loops=loops;c->current.state=state;c->last_played=played;
    return state==KUI_CDDA_CONTROL_EOF?KUI_CDDA_CONTROL_ENDED:KUI_CDDA_CONTROL_OK;
}
enum kui_cdda_control_result kui_cdda_control_fail(struct kui_cdda_control *c, uint32_t epoch) {
    if(!c || !c->initialized) return KUI_CDDA_CONTROL_INVALID;
    if(c->current.pending) return kui_cdda_control_complete(c,epoch,false);
    if(epoch!=c->current.epoch) return KUI_CDDA_CONTROL_STALE;
    c->current.state=KUI_CDDA_CONTROL_FAULT;
    return KUI_CDDA_CONTROL_IO;
}
enum kui_cdda_control_result kui_cdda_control_status(const struct kui_cdda_control *c,
    struct kui_cdda_control_status *out) {
    if(!c || !c->initialized || !out) return KUI_CDDA_CONTROL_INVALID;
    *out=c->current;return KUI_CDDA_CONTROL_OK;
}
