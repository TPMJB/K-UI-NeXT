/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_pilot.h"
#define NO_BANK UINT32_MAX

void kui_toy_pilot_model_init(struct kui_toy_pilot_model *m) {
    if(!m) return;
    *m=(struct kui_toy_pilot_model){.generation=1,.active_bank=NO_BANK,.pending_bank=NO_BANK};
}
bool kui_toy_pilot_model_reset(struct kui_toy_pilot_model *m) {
    if(!m || !m->generation || m->generation==UINT32_MAX) return false;
    ++m->generation;
    m->stopped=1;
    /* An old active/pending bank remains owned until stop/end evidence.
     * Superseding a command cannot make its sound memory writable. */
    for(uint32_t i=0;i<2u;i++)
        if(i!=m->active_bank && i!=m->pending_bank)
            m->banks[i]=(struct kui_toy_pilot_bank){0};
    return true;
}
bool kui_toy_pilot_model_fill_begin(struct kui_toy_pilot_model *m,uint32_t b,
    uint32_t generation,uint32_t first,uint32_t frames) {
    if(!m || b>=2u || m->stopped || generation!=m->generation || !frames ||
       frames>KUI_TOY_PILOT_BANK_FRAMES || first>UINT32_MAX-frames ||
       b==m->active_bank || b==m->pending_bank || m->banks[b].state!=KUI_TOY_PILOT_BANK_EMPTY)
        return false;
    m->banks[b]=(struct kui_toy_pilot_bank){.state=KUI_TOY_PILOT_BANK_FILLING,
        .generation=generation,.first_frame=first,.frames=frames};
    return true;
}
bool kui_toy_pilot_model_fill_commit(struct kui_toy_pilot_model *m,uint32_t b,
    uint32_t generation,uint32_t frames) {
    if(!m || b>=2u || m->stopped || generation!=m->generation ||
       generation!=m->banks[b].generation || b==m->active_bank || b==m->pending_bank ||
       m->banks[b].state!=KUI_TOY_PILOT_BANK_FILLING || !frames ||
       frames>m->banks[b].frames-m->banks[b].filled) return false;
    m->banks[b].filled+=frames;
    if(m->banks[b].filled==m->banks[b].frames) m->banks[b].state=KUI_TOY_PILOT_BANK_READY;
    return true;
}
bool kui_toy_pilot_model_start_queued(struct kui_toy_pilot_model *m,uint32_t b,uint32_t generation) {
    if(!m || b>=2u || m->stopped || generation!=m->generation ||
       m->active_bank!=NO_BANK || m->pending_bank!=NO_BANK ||
       m->banks[b].generation!=generation || m->banks[b].state!=KUI_TOY_PILOT_BANK_READY)
        return false;
    m->banks[b].state=KUI_TOY_PILOT_BANK_START_WAIT;m->pending_bank=b;
    return true;
}
bool kui_toy_pilot_model_start_applied(struct kui_toy_pilot_model *m,uint32_t b,
    uint32_t generation,bool left,bool right) {
    if(!m || b>=2u || m->stopped || !left || !right || m->pending_bank!=b ||
       m->active_bank!=NO_BANK || m->banks[b].generation!=generation ||
       generation!=m->generation || m->banks[b].state!=KUI_TOY_PILOT_BANK_START_WAIT)
        return false;
    m->banks[b].state=KUI_TOY_PILOT_BANK_PLAYING;
    m->pending_bank=NO_BANK;m->active_bank=b;
    return true;
}
bool kui_toy_pilot_model_finite_end(struct kui_toy_pilot_model *m,uint32_t b,
    uint32_t generation,bool left,bool right,bool applied_end) {
    if(!m || b>=2u || !left || !right || !applied_end ||
       m->banks[b].generation!=generation ||
       !((m->active_bank==b && m->banks[b].state==KUI_TOY_PILOT_BANK_PLAYING) ||
         (m->pending_bank==b && m->banks[b].state==KUI_TOY_PILOT_BANK_START_WAIT))) return false;
    /* A long service gap may miss the active interval entirely. A verified
     * consumed start + finite elapsed interval still permits SAFE reuse; it
     * does not prove that all source samples were actually audible. */
    if(m->active_bank==b) m->active_bank=NO_BANK;
    if(m->pending_bank==b) m->pending_bank=NO_BANK;
    m->banks[b]=(struct kui_toy_pilot_bank){0};
    return true;
}
bool kui_toy_pilot_model_stop_applied(struct kui_toy_pilot_model *m,
    uint32_t generation,bool consumed,bool left,bool right) {
    if(!m || generation!=m->generation || !consumed || !left || !right) return false;
    m->active_bank=m->pending_bank=NO_BANK;
    m->banks[0]=(struct kui_toy_pilot_bank){0};m->banks[1]=(struct kui_toy_pilot_bank){0};
    m->stopped=0;
    return true;
}
