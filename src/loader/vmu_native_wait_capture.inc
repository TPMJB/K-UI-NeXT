/* SPDX-License-Identifier: GPL-3.0-only */
/* Pure, bounded Evolution1 observation. Included after native_phase_record;
 * no saved pointer is followed and no observation authorizes game I/O. */
#define NATIVE_WAIT_CAPTURE_RECORDS 8u
#define NATIVE_WAIT_CAPTURE_ATTEMPTS 120u
enum native_wait_capture_flags {
    NATIVE_WAIT_UNWOUND=1u, NATIVE_WAIT_BANK_VALID=2u, NATIVE_WAIT_PC=4u,
    NATIVE_WAIT_OPERANDS_CHECKED=8u, NATIVE_WAIT_OPERANDS_MATCH=16u,
    NATIVE_WAIT_T_CHECKED=32u, NATIVE_WAIT_T_MATCH=64u
};
struct native_wait_record {
    uint32_t flags,frame,pc,pr,ssr,r0,r4,r12,r13,r14,isr_sr,vbr;
    uint32_t scan_begin,scan_end,tick_begin,tick_end,video_counter;
};
struct native_wait_capture {
    uint32_t attempts,valid,unwind_fail,read_fail,waits,operand_match,operand_bad;
    uint32_t bank_bad,t_checked,t_match,t_bad,first_wait,first_bad,ready;
    struct native_wait_record record[NATIVE_WAIT_CAPTURE_RECORDS];
    /* Owned by the terminal caller; the collector never interprets it. */
    uint32_t snapshot[32];
};
_Static_assert(sizeof(struct native_wait_record)==68u,"Wait record ABI");
_Static_assert(sizeof(struct native_wait_capture)<=884u,"Wait capture reuses phase union");
static bool native_phase_unwind(struct native_phase_record *,uint32_t,uint32_t,
    unsigned,native_phase_read,void *);
static bool native_phase_wait(const struct native_phase_record *);
static inline void native_wait_capture_reset(struct native_wait_capture *c) {
    memset(c,0,sizeof(*c));c->first_wait=c->first_bad=UINT32_MAX;
}
static inline bool native_wait_capture_ready(const struct native_wait_capture *c) {
    return c && c->ready!=0u;
}
static inline bool native_wait_capture_finish(struct native_wait_capture *c) {
    if(c->valid==NATIVE_WAIT_CAPTURE_RECORDS) c->ready=1u;
    else if(c->attempts==NATIVE_WAIT_CAPTURE_ATTEMPTS) c->ready=2u;
    return native_wait_capture_ready(c);
}
static inline bool native_wait_capture_add(struct native_wait_capture *c,uint32_t frame,
    uint32_t lease,unsigned lease_bytes,native_phase_read read,void *context) {
    if(!c || c->ready) return native_wait_capture_ready(c);
    ++c->attempts;
    if(!read) {++c->read_fail;return native_wait_capture_finish(c);}
    struct native_phase_record owner={.kind=KUI_VMU_NATIVE_PROFILE_COMPLETE};
    /* This exact walk checks its entire400-byte span and all four ordered
     * SDK return witnesses before exposing any additional saved operand. */
    if(!native_phase_unwind(&owner,frame,lease,lease_bytes,read,context)) {
        ++c->unwind_fail;return native_wait_capture_finish(c);
    }
    struct native_wait_record *r=&c->record[c->valid];memset(r,0,sizeof(*r));
    r->flags=NATIVE_WAIT_UNWOUND;r->frame=frame;
    r->pc=owner.pc;r->pr=owner.pr;r->ssr=owner.sr;
    /* S=F+172 is the fully-pushed original common frame/callback SP.
     * Both exact common handlers have this same228-byte saved layout.
     * S+228 is not an interrupted-SP claim for the shared-stack handler. */
    static const unsigned offsets[]={308u,312u,328u,360u,364u,368u,380u};
    uint32_t *const values[]={&r->isr_sr,&r->r0,&r->r4,&r->r12,&r->r13,&r->r14,&r->vbr};
    bool got=read(context,UINT32_C(0xa05f810c),4u,&r->scan_begin);
    got=read(context,UINT32_C(0xffd8000c),4u,&r->tick_begin) && got;
    for(unsigned i=0u;i<7u;i++)
        got=read(context,frame+offsets[i],4u,values[i]) && got;
    got=read(context,UINT32_C(0x8c358d18),4u,&r->video_counter) && got;
    got=read(context,UINT32_C(0xffd8000c),4u,&r->tick_end) && got;
    got=read(context,UINT32_C(0xa05f810c),4u,&r->scan_end) && got;
    if(!got) {memset(r,0,sizeof(*r));++c->read_fail;return native_wait_capture_finish(c);}
    bool bank=(r->ssr&UINT32_C(0x60000000))==UINT32_C(0x60000000) &&
        (r->isr_sr&UINT32_C(0x60000000))==UINT32_C(0x60000000);
    if(bank) r->flags|=NATIVE_WAIT_BANK_VALID;else ++c->bank_bad;
    bool wait=native_phase_wait(&owner),bad=!bank;
    if(wait) {
        r->flags|=NATIVE_WAIT_PC;++c->waits;
        if(c->first_wait==UINT32_MAX) c->first_wait=c->valid;
        if(bank) {
            r->flags|=NATIVE_WAIT_OPERANDS_CHECKED;
            if(r->r12==UINT32_C(0x8c1f1f36) && r->r13==UINT32_C(0x3ff) &&
               r->r14==UINT32_C(0x10c) && r->r4==UINT32_C(0x10c)) {
                r->flags|=NATIVE_WAIT_OPERANDS_MATCH;++c->operand_match;
            } else {++c->operand_bad;bad=true;}
        }
        /* Only at BF has the preceding TST executed. In the getter, at
         * JSR, and before TST, saved T has no such comparison meaning. */
        if(bank && r->pc==UINT32_C(0x8c1f362e)) {
            r->flags|=NATIVE_WAIT_T_CHECKED;++c->t_checked;
            if((r->ssr&1u)==((r->r0&r->r13)==0u)) {
                r->flags|=NATIVE_WAIT_T_MATCH;++c->t_match;
            } else {++c->t_bad;bad=true;}
        }
    }
    if(wait && bad && c->first_bad==UINT32_MAX) c->first_bad=c->valid;
    ++c->valid;return native_wait_capture_finish(c);
}
