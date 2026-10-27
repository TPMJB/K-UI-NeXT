/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_MAPLE_FENCE_H
#define KUI_ARMADA_CE_MAPLE_FENCE_H
#include <stdbool.h>
#include <stddef.h>
#include "kui/armada_ce_maple_fence_types.h"

typedef bool (*kui_armada_ce_maple_fence_read32)(void *,uint32_t,uint32_t *);
typedef bool (*kui_armada_ce_maple_fence_write32)(void *,uint32_t,uint32_t);
enum kui_armada_ce_maple_fence_result {
    KUI_ARMADA_CE_MAPLE_FENCE_OK,
    KUI_ARMADA_CE_MAPLE_FENCE_SKIPPED,
    KUI_ARMADA_CE_MAPLE_FENCE_ARGUMENT,
    KUI_ARMADA_CE_MAPLE_FENCE_ADMISSION,
    KUI_ARMADA_CE_MAPLE_FENCE_READ,
    KUI_ARMADA_CE_MAPLE_FENCE_WRITE,
    KUI_ARMADA_CE_MAPLE_FENCE_METADATA,
    KUI_ARMADA_CE_MAPLE_FENCE_CHANGED,
    KUI_ARMADA_CE_MAPLE_FENCE_TIMEOUT,
    KUI_ARMADA_CE_MAPLE_FENCE_STATE,
    KUI_ARMADA_CE_MAPLE_FENCE_RESTORE
};
/* Zero before use, private and single caller. pending includes paired PRE
 * skips/refusals. active means a disable was attempted and restoration has not
 * yet been confirmed, not necessarily that a valid held window was admitted.
 * Freeze may skip both phases only with pending==active==0. POST must always
 * run/retry while active, even if capture has otherwise frozen or refused.
 * PRE cannot grant permission to touch any guest/DMA buffer: this API gates
 * MDEN only, and the source caller must separately prove its MMIO mapping,
 * worker ownership and original completion's behavior across this window. */
struct kui_armada_ce_maple_fence_state {
    uint32_t pending,active,extension,source,saved_enable,trigger,previous_mode;
};
static inline uint32_t kui_armada_ce_maple_fence_inc(uint32_t n) {
    return n==UINT32_MAX?n:n+1u;
}
static inline bool kui_armada_ce_maple_fence_arguments(
    kui_armada_ce_maple_fence_read32 read32,kui_armada_ce_maple_fence_write32 write32,
    struct kui_armada_ce_maple_fence_state *state,struct kui_armada_ce_maple_fence_snapshot *out) {
    if(!read32 || !write32 || !state || !out) return false;
    uintptr_t a=(uintptr_t)(void *)state,b=(uintptr_t)(void *)out;
    return a<=b?b-a>=sizeof(*state):a-b>=sizeof(*out);
}
static inline void kui_armada_ce_maple_fence_note(
    struct kui_armada_ce_maple_fence_snapshot *out,uint32_t fault[5],unsigned why,
    uint32_t at,uint32_t actual,uint32_t expected,uint32_t detail) {
    out->status[4]|=UINT32_C(1)<<why;
    if(!fault[0]) {fault[0]=why;fault[1]=at;fault[2]=actual;fault[3]=expected;fault[4]=detail;}
}
static inline enum kui_armada_ce_maple_fence_result kui_armada_ce_maple_fence_finish(
    struct kui_armada_ce_maple_fence_state *state,struct kui_armada_ce_maple_fence_snapshot *out,
    const uint32_t fault[5],enum kui_armada_ce_maple_fence_result otherwise) {
    out->status[3]=state->active;
    if(!fault[0]) return otherwise;
    out->status[1]=kui_armada_ce_maple_fence_inc(out->status[1]);
    for(unsigned i=0;i<5u;i++) out->fault[i]=fault[i];
    return (enum kui_armada_ce_maple_fence_result)fault[0];
}
static inline bool kui_armada_ce_maple_fence_read(
    kui_armada_ce_maple_fence_read32 read32,void *context,
    struct kui_armada_ce_maple_fence_snapshot *out,uint32_t fault[5],
    uint32_t at,uint32_t *value,uint32_t detail) {
    if(read32(context,at,value)) return true;
    kui_armada_ce_maple_fence_note(out,fault,KUI_ARMADA_CE_MAPLE_FENCE_READ,at,0,0,detail);
    return false;
}
static inline void kui_armada_ce_maple_fence_expect(
    struct kui_armada_ce_maple_fence_snapshot *out,uint32_t fault[5],
    uint32_t at,uint32_t actual,uint32_t expected,unsigned why,uint32_t detail) {
    if(actual!=expected) kui_armada_ce_maple_fence_note(out,fault,why,at,actual,expected,detail);
}
/* Called after every attempted disable, including failed write callbacks.
 * Readback, rather than callback success, determines whether active can clear.
 * A failed restore readback takes priority over an earlier refusal. Enabling
 * can legitimately start a new transfer, so restored MDST is observational. */
static inline void kui_armada_ce_maple_fence_restore(
    kui_armada_ce_maple_fence_read32 read32,kui_armada_ce_maple_fence_write32 write32,void *context,
    struct kui_armada_ce_maple_fence_state *state,struct kui_armada_ce_maple_fence_snapshot *out,
    uint32_t fault[5]) {
    uint32_t enabled=UINT32_MAX,started=UINT32_MAX;
    bool wrote=write32(context,KUI_ARMADA_CE_MAPLE_FENCE_MDEN,1u);
    if(!wrote) kui_armada_ce_maple_fence_note(out,fault,KUI_ARMADA_CE_MAPLE_FENCE_WRITE,
        KUI_ARMADA_CE_MAPLE_FENCE_MDEN,0,1u,3u);
    bool readback=read32(context,KUI_ARMADA_CE_MAPLE_FENCE_MDEN,&enabled);
    out->after[0]=enabled;
    if(!readback || enabled!=1u) {
        fault[0]=0;
        kui_armada_ce_maple_fence_note(out,fault,KUI_ARMADA_CE_MAPLE_FENCE_RESTORE,
            KUI_ARMADA_CE_MAPLE_FENCE_MDEN,enabled,1u,readback?2u:1u);
    } else {
        state->active=0;
        out->counts[4]=kui_armada_ce_maple_fence_inc(out->counts[4]);
    }
    if(kui_armada_ce_maple_fence_read(read32,context,out,fault,
       KUI_ARMADA_CE_MAPLE_FENCE_MDST,&started,4u) && started>1u)
        kui_armada_ce_maple_fence_note(out,fault,KUI_ARMADA_CE_MAPLE_FENCE_METADATA,
            KUI_ARMADA_CE_MAPLE_FENCE_MDST,started,1u,4u);
    out->after[1]=started;
}

/* PRE has no MMIO write before admission and all four initial registers pass.
 * An initially busy transfer is skipped; the caller still pairs POST. A valid
 * held window returns OK with active1. Interrupt masking and source-specific
 * normal-mode execution of the original response walker are caller duties. */
static inline enum kui_armada_ce_maple_fence_result kui_armada_ce_maple_fence_pre(
    uint32_t extension,uint32_t expected_tx,uint32_t previous_mode,bool admitted,
    kui_armada_ce_maple_fence_read32 read32,kui_armada_ce_maple_fence_write32 write32,void *context,
    struct kui_armada_ce_maple_fence_state *state,struct kui_armada_ce_maple_fence_snapshot *out) {
    if(!kui_armada_ce_maple_fence_arguments(read32,write32,state,out)) return KUI_ARMADA_CE_MAPLE_FENCE_ARGUMENT;
    uint32_t fault[5];
    for(unsigned i=0;i<5u;i++) ((volatile uint32_t *)(void *)fault)[i]=0;
    if(!out->counts[0]) out->counts[0]=KUI_ARMADA_CE_MAPLE_FENCE_MAGIC;
    out->counts[1]=kui_armada_ce_maple_fence_inc(out->counts[1]);
    if(state->pending || state->active) {
        kui_armada_ce_maple_fence_note(out,fault,KUI_ARMADA_CE_MAPLE_FENCE_STATE,
            extension,state->pending,state->active,0);
        return kui_armada_ce_maple_fence_finish(state,out,fault,KUI_ARMADA_CE_MAPLE_FENCE_OK);
    }
    state->pending=1u;state->extension=extension;state->source=expected_tx;
    state->saved_enable=1u;state->trigger=1u;state->previous_mode=previous_mode;
    for(unsigned i=0;i<5u;i++) {
        ((volatile uint32_t *)(void *)out->before)[i]=0;
        ((volatile uint32_t *)(void *)out->held)[i]=0;
    }
    out->before[4]=expected_tx;out->after[0]=out->after[1]=0;
    out->after[2]=previous_mode;out->after[3]=UINT32_MAX;out->after[4]=extension;
    if(out->counts[0]!=KUI_ARMADA_CE_MAPLE_FENCE_MAGIC || !extension ||
       (extension&3u) || extension>=0x80000000u || previous_mode>1u ||
       expected_tx<0x0c010000u || expected_tx>0x0cfff000u || (expected_tx&0xfffu)) {
        kui_armada_ce_maple_fence_note(out,fault,KUI_ARMADA_CE_MAPLE_FENCE_ARGUMENT,
            extension,expected_tx,previous_mode,0);
        return kui_armada_ce_maple_fence_finish(state,out,fault,KUI_ARMADA_CE_MAPLE_FENCE_OK);
    }
    if(!admitted) {
        kui_armada_ce_maple_fence_note(out,fault,KUI_ARMADA_CE_MAPLE_FENCE_ADMISSION,
            extension,0,1u,0);
        return kui_armada_ce_maple_fence_finish(state,out,fault,KUI_ARMADA_CE_MAPLE_FENCE_OK);
    }
    static const uint32_t at[4]={KUI_ARMADA_CE_MAPLE_FENCE_MDEN,KUI_ARMADA_CE_MAPLE_FENCE_MDTSEL,
        KUI_ARMADA_CE_MAPLE_FENCE_MDST,KUI_ARMADA_CE_MAPLE_FENCE_MDSTAR};
    for(unsigned i=0;i<4u;i++) if(!kui_armada_ce_maple_fence_read(read32,context,out,fault,at[i],&out->before[i],i))
        return kui_armada_ce_maple_fence_finish(state,out,fault,KUI_ARMADA_CE_MAPLE_FENCE_OK);
    kui_armada_ce_maple_fence_expect(out,fault,at[0],out->before[0],1u,KUI_ARMADA_CE_MAPLE_FENCE_METADATA,0);
    kui_armada_ce_maple_fence_expect(out,fault,at[1],out->before[1],1u,KUI_ARMADA_CE_MAPLE_FENCE_METADATA,1u);
    kui_armada_ce_maple_fence_expect(out,fault,at[3],out->before[3],expected_tx,KUI_ARMADA_CE_MAPLE_FENCE_METADATA,3u);
    if(out->before[2]>1u) kui_armada_ce_maple_fence_note(out,fault,KUI_ARMADA_CE_MAPLE_FENCE_METADATA,
        at[2],out->before[2],1u,2u);
    if(fault[0]) return kui_armada_ce_maple_fence_finish(state,out,fault,KUI_ARMADA_CE_MAPLE_FENCE_OK);
    if(out->before[2]) {
        out->status[0]=kui_armada_ce_maple_fence_inc(out->status[0]);
        return kui_armada_ce_maple_fence_finish(state,out,fault,KUI_ARMADA_CE_MAPLE_FENCE_SKIPPED);
    }
    /* Set active before the callback: failure may still have performed the
     * store. Every path from this point either holds or attempts restoration. */
    state->active=1u;
    if(!write32(context,KUI_ARMADA_CE_MAPLE_FENCE_MDEN,0))
        kui_armada_ce_maple_fence_note(out,fault,KUI_ARMADA_CE_MAPLE_FENCE_WRITE,
            KUI_ARMADA_CE_MAPLE_FENCE_MDEN,1u,0,0);
    else if(kui_armada_ce_maple_fence_read(read32,context,out,fault,
            KUI_ARMADA_CE_MAPLE_FENCE_MDEN,&out->held[0],5u))
        kui_armada_ce_maple_fence_expect(out,fault,KUI_ARMADA_CE_MAPLE_FENCE_MDEN,
            out->held[0],0,KUI_ARMADA_CE_MAPLE_FENCE_CHANGED,5u);
    unsigned polls=0;
    if(!fault[0]) {
        while(polls<KUI_ARMADA_CE_MAPLE_FENCE_POLL_LIMIT) {
            ++polls;
            if(!kui_armada_ce_maple_fence_read(read32,context,out,fault,
               KUI_ARMADA_CE_MAPLE_FENCE_MDST,&out->held[1],6u)) break;
            if(!out->held[1]) break;
            if(out->held[1]!=1u) {
                kui_armada_ce_maple_fence_note(out,fault,KUI_ARMADA_CE_MAPLE_FENCE_METADATA,
                    KUI_ARMADA_CE_MAPLE_FENCE_MDST,out->held[1],1u,6u);break;
            }
        }
        if(!fault[0] && out->held[1]) kui_armada_ce_maple_fence_note(out,fault,
            KUI_ARMADA_CE_MAPLE_FENCE_TIMEOUT,KUI_ARMADA_CE_MAPLE_FENCE_MDST,
            out->held[1],0,polls);
    }
    if(polls>out->status[2]) out->status[2]=polls;
    if(fault[0]) kui_armada_ce_maple_fence_restore(read32,write32,context,state,out,fault);
    else out->counts[3]=kui_armada_ce_maple_fence_inc(out->counts[3]);
    return kui_armada_ce_maple_fence_finish(state,out,fault,KUI_ARMADA_CE_MAPLE_FENCE_OK);
}

/* POST performs restoration even if the extension/mode or held hardware
 * witnesses changed. It never writes MDST, trigger, source, RX data or IRQ
 * state. A paired PRE without an active window requires no hardware access. */
static inline enum kui_armada_ce_maple_fence_result kui_armada_ce_maple_fence_post(
    uint32_t extension,uint32_t previous_mode,
    kui_armada_ce_maple_fence_read32 read32,kui_armada_ce_maple_fence_write32 write32,void *context,
    struct kui_armada_ce_maple_fence_state *state,struct kui_armada_ce_maple_fence_snapshot *out) {
    if(!kui_armada_ce_maple_fence_arguments(read32,write32,state,out)) return KUI_ARMADA_CE_MAPLE_FENCE_ARGUMENT;
    uint32_t fault[5],trigger=UINT32_MAX;
    for(unsigned i=0;i<5u;i++) ((volatile uint32_t *)(void *)fault)[i]=0;
    if(!out->counts[0]) out->counts[0]=KUI_ARMADA_CE_MAPLE_FENCE_MAGIC;
    out->counts[2]=kui_armada_ce_maple_fence_inc(out->counts[2]);
    out->after[3]=previous_mode;out->after[4]=extension;
    if(out->counts[0]!=KUI_ARMADA_CE_MAPLE_FENCE_MAGIC || state->pending!=1u || state->active>1u)
        kui_armada_ce_maple_fence_note(out,fault,KUI_ARMADA_CE_MAPLE_FENCE_STATE,
            extension,state->pending,state->active,1u);
    kui_armada_ce_maple_fence_expect(out,fault,0,extension,state->extension,KUI_ARMADA_CE_MAPLE_FENCE_CHANGED,0);
    kui_armada_ce_maple_fence_expect(out,fault,0,previous_mode,state->previous_mode,KUI_ARMADA_CE_MAPLE_FENCE_CHANGED,1u);
    if(!state->active) {
        state->pending=0;
        return kui_armada_ce_maple_fence_finish(state,out,fault,KUI_ARMADA_CE_MAPLE_FENCE_SKIPPED);
    }
    if(state->saved_enable!=1u || state->trigger!=1u)
        kui_armada_ce_maple_fence_note(out,fault,KUI_ARMADA_CE_MAPLE_FENCE_STATE,
            extension,state->saved_enable,state->trigger,2u);
    static const uint32_t at[4]={KUI_ARMADA_CE_MAPLE_FENCE_MDEN,KUI_ARMADA_CE_MAPLE_FENCE_MDST,
        KUI_ARMADA_CE_MAPLE_FENCE_MDSTAR,KUI_ARMADA_CE_MAPLE_FENCE_MDTSEL};
    uint32_t *values[4]={&out->held[2],&out->held[3],&out->held[4],&trigger};
    const uint32_t wanted[4]={0,0,state->source,1u};
    for(unsigned i=0;i<4u;i++) if(kui_armada_ce_maple_fence_read(read32,context,out,fault,at[i],values[i],7u+i))
        kui_armada_ce_maple_fence_expect(out,fault,at[i],*values[i],wanted[i],KUI_ARMADA_CE_MAPLE_FENCE_CHANGED,7u+i);
    kui_armada_ce_maple_fence_restore(read32,write32,context,state,out,fault);
    if(!state->active) state->pending=0;
    return kui_armada_ce_maple_fence_finish(state,out,fault,KUI_ARMADA_CE_MAPLE_FENCE_OK);
}
#endif
