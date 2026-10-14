/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_STREAM_INITIAL_WORD_H
#define KUI_ARMADA_CE_STREAM_INITIAL_WORD_H
#include "kui/armada_ce_live_sound.h"

struct kui_armada_ce_stream_initial_owner {
    uint32_t process,slot,handle,index;
};
/* The restored caller already has its native process/access-key context.
 * Capture only the exact kernel process-table record, with IRQs masked.
 * Eight bounded metadata reads precede installation of any native reader;
 * the caller also repeats actual TTB/CCR and privileged SR around this call. */
static inline bool kui_armada_ce_stream_initial_owner(
    uint32_t ttb,uint32_t ccr,uint32_t sr,kui_armada_ce_ram_read32 read_table,
    void *context,struct kui_armada_ce_stream_initial_owner *out) {
    uint32_t current,index,slot,handle,again,p1;
    if(!out) return false;
    memset(out,0,sizeof(*out));
    if(!read_table || !kui_armada_ce_ram_table_p1(ttb,&p1) ||
       (ccr&0xa1u)!=1u || (sr&0x700000f0u)!=0x400000f0u ||
       !read_table(context,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,&current) ||
       current<KUI_ARMADA_CE_LIVE_PROCESS_BASE ||
       current>=KUI_ARMADA_CE_LIVE_PROCESS_BASE+32u*KUI_ARMADA_CE_LIVE_PROCESS_STRIDE ||
       (current-KUI_ARMADA_CE_LIVE_PROCESS_BASE)%KUI_ARMADA_CE_LIVE_PROCESS_STRIDE)
        return false;
    if(!read_table(context,current,&index) || !read_table(context,current+12u,&slot) ||
       !read_table(context,current+8u,&handle)) return false;
    index&=255u;
    if(index!=(current-KUI_ARMADA_CE_LIVE_PROCESS_BASE)/KUI_ARMADA_CE_LIVE_PROCESS_STRIDE ||
       slot!=((index+1u)<<25) || !handle || handle==UINT32_MAX) return false;
    if(!read_table(context,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,&again) || again!=current ||
       !read_table(context,current,&again) || (again&255u)!=index ||
       !read_table(context,current+12u,&again) || again!=slot ||
       !read_table(context,current+8u,&again) || again!=handle) return false;
    *out=(struct kui_armada_ce_stream_initial_owner){current,slot,handle,index};
    return true;
}
/* A module record reaches this policy only after the full binder admits its
 * heap node, exact profile fields and section-table location. This permits
 * reading the bounded candidate to check its layout/CRC; it grants no API
 * target, COM reference or successful module identity. */
static inline bool kui_armada_ce_stream_initial_module(
    const struct kui_armada_ce_live_module *m,unsigned profile,uint32_t index,uint32_t va) {
    static const uint32_t span[3]={0xb000u,0x9000u,0x34000u};
    static const uint32_t table[3]={0x846cu,0x6198u,0x2e170u};
    if(!m || profile>2u || index>=32u || !m->node || !(m->attach&(UINT32_C(1)<<index)) ||
       (m->kind!=3u && (m->kind!=2u || profile==2u)) ||
       m->count!=(profile==2u || m->kind==2u?4u:3u) || m->span!=span[profile] ||
       (m->base&0xfe00ffffu)!=0x02000000u || m->sections!=m->base+table[profile] ||
       (m->base&0x01ffffffu)>0x02000000u-m->span || va<m->base ||
       va-m->base>=m->span || m->span-(va-m->base)<4u) return false;
    return true;
}
static inline bool kui_armada_ce_stream_initial_address(
    const struct kui_armada_ce_stream_initial_owner *owner,
    const struct kui_armada_ce_live_workspace *w,uint32_t va) {
    if(!owner || !w || (va&3u) || va>=0x80000000u ||
       (va&0x01ffffffu)<0x10000u) return false;
    const uint32_t slot=va&0xfe000000u;
    if(slot==owner->slot) return true;
    if(slot!=0x02000000u) return false;
    if(kui_armada_ce_stream_initial_module(&w->dsound,0u,owner->index,va) ||
       kui_armada_ce_stream_initial_module(&w->sndcore,1u,owner->index,va) ||
       kui_armada_ce_stream_initial_module(&w->coredll,2u,owner->index,va)) return true;
    const struct kui_armada_ce_live_module *candidate=w->native_candidate;
    return candidate && candidate->kind==2u &&
        (kui_armada_ce_stream_initial_module(candidate,0u,owner->index,va) ||
         kui_armada_ce_stream_initial_module(candidate,1u,owner->index,va));
}
#endif
