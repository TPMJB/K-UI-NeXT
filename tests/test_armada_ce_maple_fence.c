/* SPDX-License-Identifier: GPL-3.0-only */
/* Closed register fixture: the only mutable device word is MDEN. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kui/armada_ce_maple_fence.h"

static const uint32_t EXT=0x00030a60u,TX=0x0cfd3000u;
enum { STAR=0,TRIGGER=1,ENABLE=2,STATE=3 };
static const uint32_t address[4]={0xa05f6c04u,0xa05f6c10u,0xa05f6c14u,0xa05f6c18u};
struct event {uint32_t address,value;unsigned write,success;};
struct fixture {
    uint32_t reg[4];
    struct event event[256];
    unsigned calls,reads,writes,fail_at,fail_at2,disable_attempts,restore_attempts;
    unsigned busy_remaining,busy_forever,ignore_disable,ignore_enable,failed_write_effect;
} f;
static struct kui_armada_ce_maple_fence_state state;
static struct kui_armada_ce_maple_fence_snapshot out;
static unsigned checks;
#define CHECK(c) do {++checks;assert(c);} while(0)
static unsigned index_of(uint32_t at) {
    for(unsigned i=0;i<4u;i++) if(address[i]==at) return i;
    fprintf(stderr,"Unadmitted MMIO address %08x\n",at);assert(0);return 0;
}
static struct event *event(uint32_t at,unsigned write,uint32_t value) {
    CHECK(f.calls<256u);
    struct event *e=&f.event[f.calls++];e->address=at;e->write=write;e->value=value;
    e->success=f.calls!=f.fail_at && f.calls!=f.fail_at2;return e;
}
static bool read_reg(void *context,uint32_t at,uint32_t *value) {
    CHECK(context==&f && value!=NULL);unsigned i=index_of(at);++f.reads;
    uint32_t v=f.reg[i];
    if(i==STATE && !f.reg[ENABLE] && f.disable_attempts) {
        if(f.busy_forever) v=1u;
        else if(f.busy_remaining) {--f.busy_remaining;v=1u;}
    }
    struct event *e=event(at,0,v);
    if(!e->success) return false;
    *value=v;return true;
}
static bool write_reg(void *context,uint32_t at,uint32_t value) {
    CHECK(context==&f && at==address[ENABLE] && value<=1u);++f.writes;
    struct event *e=event(at,1,value);
    if(value) ++f.restore_attempts;else ++f.disable_attempts;
    if(e->success || f.failed_write_effect) {
        if((value && !f.ignore_enable) || (!value && !f.ignore_disable)) f.reg[ENABLE]=value;
    }
    return e->success!=0u;
}
static void reset(void) {
    memset(&f,0,sizeof(f));memset(&state,0,sizeof(state));memset(&out,0,sizeof(out));
    f.reg[STAR]=TX;f.reg[TRIGGER]=1u;f.reg[ENABLE]=1u;
}
static unsigned pre(uint32_t ext,uint32_t source,uint32_t mode,bool admitted) {
    return kui_armada_ce_maple_fence_pre(ext,source,mode,admitted,read_reg,write_reg,&f,&state,&out);
}
static unsigned post(uint32_t ext,uint32_t mode) {
    return kui_armada_ce_maple_fence_post(ext,mode,read_reg,write_reg,&f,&state,&out);
}
static void successful_pair(void) {
    for(unsigned mode=0;mode<2u;mode++) {
        reset();CHECK(pre(EXT,TX,mode,true)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
        CHECK(out.counts[0]==UINT32_C(0x31464c4d) && out.counts[1]==1u && !out.counts[2]);
        CHECK(state.pending==1u && state.active==1u && f.reg[ENABLE]==0u);
        CHECK(out.counts[3]==1u && !out.counts[4] && out.status[3]==1u);
        CHECK(f.disable_attempts==1u && !f.restore_attempts);
        CHECK(out.before[0]==1u && out.before[1]==1u && !out.before[2]);
        CHECK(out.before[3]==TX && out.before[4]==TX);
        CHECK(!out.held[0] && !out.held[1]);
        unsigned held_at=f.calls;
        /* The driver consumes its unchanged reply while the gate is held. */
        for(unsigned i=0;i<16u;i++) CHECK(f.reg[ENABLE]==0u && state.active==1u);
        CHECK(post(EXT,mode)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
        CHECK(!state.pending && !state.active && f.reg[ENABLE]==1u);
        CHECK(out.counts[1]==1u && out.counts[2]==1u && out.counts[3]==1u && out.counts[4]==1u);
        CHECK(!out.status[1] && !out.status[3] && !out.status[4]);
        CHECK(out.after[0]==1u && !out.after[1] && out.after[2]==mode && out.after[3]==mode && out.after[4]==EXT);
        CHECK(f.disable_attempts==1u && f.restore_attempts==1u && f.writes==2u);
        unsigned restores=0;
        for(unsigned i=0;i<f.calls;i++) if(f.event[i].write && f.event[i].value) {CHECK(i>=held_at);++restores;}
        CHECK(restores==1u);
    }
}
static void skips_and_admission(void) {
    reset();f.reg[STATE]=1u;
    CHECK(pre(EXT,TX,0,true)==KUI_ARMADA_CE_MAPLE_FENCE_SKIPPED);
    CHECK(!f.writes && out.status[0]==1u && !out.status[1] && !state.active);
    unsigned n=f.calls;CHECK(post(EXT,0)==KUI_ARMADA_CE_MAPLE_FENCE_SKIPPED && f.calls==n);
    CHECK(!state.pending && !state.active && !out.status[4]);
    reset();CHECK(pre(EXT,TX,0,false)==KUI_ARMADA_CE_MAPLE_FENCE_ADMISSION);
    CHECK(!f.calls && !f.writes && out.status[1]==1u);
    CHECK(post(EXT,0)==KUI_ARMADA_CE_MAPLE_FENCE_SKIPPED && !f.calls && !state.pending);
    const uint32_t value[4]={TX+4u,0u,0u,2u};
    for(unsigned i=0;i<4u;i++) {
        reset();f.reg[i]=value[i];
        CHECK(pre(EXT,TX,0,true)!=KUI_ARMADA_CE_MAPLE_FENCE_OK);
        CHECK(!f.writes && !state.active && out.status[1]==1u);
        CHECK(post(EXT,0)==KUI_ARMADA_CE_MAPLE_FENCE_SKIPPED);
    }
}
static void poll_bound_and_readback(void) {
    reset();f.busy_remaining=3u;
    CHECK(pre(EXT,TX,0,true)==KUI_ARMADA_CE_MAPLE_FENCE_OK && out.status[2]==4u);
    CHECK(post(EXT,0)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
    reset();f.busy_forever=1u;
    CHECK(pre(EXT,TX,0,true)==KUI_ARMADA_CE_MAPLE_FENCE_TIMEOUT);
    CHECK(out.status[2]==64u && out.status[1]==1u && !out.counts[3]);
    CHECK(f.disable_attempts==1u && f.restore_attempts==1u && f.reg[ENABLE]==1u && !state.active);
    CHECK(out.counts[4]==1u && f.calls<=80u);
    unsigned n=f.calls;CHECK(post(EXT,0)==KUI_ARMADA_CE_MAPLE_FENCE_SKIPPED && f.calls==n);
    reset();f.ignore_disable=1u;
    CHECK(pre(EXT,TX,0,true)!=KUI_ARMADA_CE_MAPLE_FENCE_OK);
    CHECK(f.disable_attempts==1u && f.restore_attempts==1u && f.reg[ENABLE]==1u && !state.active);
    reset();CHECK(pre(EXT,TX,0,true)==KUI_ARMADA_CE_MAPLE_FENCE_OK);f.ignore_enable=1u;
    CHECK(post(EXT,0)==KUI_ARMADA_CE_MAPLE_FENCE_RESTORE);
    CHECK(state.active==1u && out.status[3]==1u && f.reg[ENABLE]==0u && !out.counts[4]);
    f.ignore_enable=0u;CHECK(post(EXT,0)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
    CHECK(!state.active && !state.pending && f.reg[ENABLE]==1u && out.counts[4]==1u);
}
static void all_pre_callback_failures(void) {
    reset();CHECK(pre(EXT,TX,0,true)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
    unsigned bound=f.calls,write_at=0;
    for(unsigned i=0;i<bound;i++) if(f.event[i].write) {write_at=i+1u;break;}
    CHECK(write_at && bound<32u);
    for(unsigned effect=0;effect<2u;effect++) for(unsigned fail=1;fail<=bound;fail++) {
        reset();f.fail_at=fail;f.failed_write_effect=effect;
        CHECK(pre(EXT,TX,0,true)!=KUI_ARMADA_CE_MAPLE_FENCE_OK);
        CHECK(out.status[1]==1u && !out.counts[3] && f.reg[ENABLE]==1u && !state.active);
        if(fail<write_at) CHECK(!f.writes && !f.restore_attempts);
        else CHECK(f.disable_attempts==1u && f.restore_attempts==1u && out.counts[4]==1u);
        unsigned n=f.calls;f.fail_at=0;
        CHECK(post(EXT,0)==KUI_ARMADA_CE_MAPLE_FENCE_SKIPPED && f.calls==n && !state.pending);
    }
}
static void rollback_callback_failures(void) {
    reset();CHECK(pre(EXT,TX,0,true)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
    unsigned disable_at=0;
    for(unsigned i=0;i<f.calls;i++) if(f.event[i].write && !f.event[i].value) {disable_at=i+1u;break;}
    CHECK(disable_at!=0u);
    unsigned primary=disable_at+1u;
    reset();f.fail_at=primary;
    CHECK(pre(EXT,TX,0,true)!=KUI_ARMADA_CE_MAPLE_FENCE_OK);
    unsigned rollback_end=f.calls;CHECK(rollback_end>primary);
    /* The primary failure is the disabled-MDEN readback. A second failure at
     * every rollback callback must not suppress the final restore attempt. */
    for(unsigned effect=0;effect<2u;effect++) for(unsigned second=primary+1u;second<=rollback_end;second++) {
        reset();f.fail_at=primary;f.fail_at2=second;f.failed_write_effect=effect;
        CHECK(pre(EXT,TX,0,true)!=KUI_ARMADA_CE_MAPLE_FENCE_OK);
        CHECK(out.status[1]==1u && f.disable_attempts==1u && f.restore_attempts==1u);
        if(state.active) {
            CHECK(out.status[3]==1u && out.fault[0]==KUI_ARMADA_CE_MAPLE_FENCE_RESTORE);
            f.fail_at=f.fail_at2=0;unsigned result=post(EXT,0);(void)result;
            CHECK(!state.active && !state.pending && f.reg[ENABLE]==1u);
        } else {
            CHECK(f.reg[ENABLE]==1u && out.counts[4]==1u);
            f.fail_at=f.fail_at2=0;CHECK(post(EXT,0)==KUI_ARMADA_CE_MAPLE_FENCE_SKIPPED);
        }
    }
}
static void all_post_callback_failures(void) {
    reset();CHECK(pre(EXT,TX,0,true)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
    unsigned start=f.calls;CHECK(post(EXT,0)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
    unsigned bound=f.calls-start;CHECK(bound<32u);
    for(unsigned effect=0;effect<2u;effect++) for(unsigned fail=1;fail<=bound;fail++) {
        reset();CHECK(pre(EXT,TX,0,true)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
        f.failed_write_effect=effect;f.fail_at=f.calls+fail;
        CHECK(post(EXT,0)!=KUI_ARMADA_CE_MAPLE_FENCE_OK);
        CHECK(out.status[1]==1u && f.restore_attempts==1u);
        if(state.active) {
            f.fail_at=0;unsigned again=post(EXT,0);(void)again;
            CHECK(!state.active && !state.pending && f.reg[ENABLE]==1u);
        } else CHECK(f.reg[ENABLE]==1u && out.counts[4]==1u && !state.pending);
    }
}
static void post_mutations_restore(void) {
    for(unsigned i=0;i<4u;i++) {
        reset();CHECK(pre(EXT,TX,0,true)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
        f.reg[i]=i==STAR?TX+4u:i==TRIGGER?0u:i==ENABLE?1u:2u;
        CHECK(post(EXT,0)!=KUI_ARMADA_CE_MAPLE_FENCE_OK);
        CHECK(out.status[1]==1u && !state.active && !state.pending && f.reg[ENABLE]==1u && out.counts[4]==1u);
    }
    for(unsigned mismatch=0;mismatch<2u;mismatch++) {
        reset();CHECK(pre(EXT,TX,0,true)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
        CHECK(post(mismatch?EXT:EXT+4u,mismatch?1u:0u)!=KUI_ARMADA_CE_MAPLE_FENCE_OK);
        CHECK(!state.active && !state.pending && f.reg[ENABLE]==1u && out.status[1]==1u);
    }
    reset();CHECK(pre(EXT,TX,0,true)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
    f.reg[STAR]=TX+4u;f.reg[TRIGGER]=0u;f.reg[STATE]=2u;f.reg[ENABLE]=1u;
    CHECK(post(EXT+4u,1u)!=KUI_ARMADA_CE_MAPLE_FENCE_OK);
    CHECK(out.status[1]==1u && f.restore_attempts==1u && !state.active && f.reg[ENABLE]==1u);
}
static void nesting_orphan_and_history(void) {
    reset();CHECK(pre(EXT,TX,0,true)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
    struct kui_armada_ce_maple_fence_state held=state;unsigned n=f.calls;
    CHECK(pre(EXT+4u,TX+0x1000u,1u,true)==KUI_ARMADA_CE_MAPLE_FENCE_STATE);
    CHECK(!memcmp(&held,&state,sizeof(state)) && f.calls==n && f.reg[ENABLE]==0u);
    CHECK(out.counts[1]==2u && out.status[1]==1u);
    uint32_t fault[5];memcpy(fault,out.fault,sizeof(fault));uint32_t flags=out.status[4];
    CHECK(post(EXT,0)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
    CHECK(!memcmp(fault,out.fault,sizeof(fault)) && out.status[4]==flags);
    CHECK(pre(EXT,TX,0,true)==KUI_ARMADA_CE_MAPLE_FENCE_OK && post(EXT,0)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
    CHECK(!memcmp(fault,out.fault,sizeof(fault)) && out.status[1]==1u);
    reset();CHECK(post(EXT,0)==KUI_ARMADA_CE_MAPLE_FENCE_STATE);
    CHECK(!f.calls && !f.writes && out.status[1]==1u);
}
static void invalid_arguments(void) {
    reset();struct kui_armada_ce_maple_fence_snapshot before=out;
    struct kui_armada_ce_maple_fence_state saved=state;
    CHECK(kui_armada_ce_maple_fence_pre(EXT,TX,0,true,read_reg,write_reg,&f,NULL,&out)==KUI_ARMADA_CE_MAPLE_FENCE_ARGUMENT);
    CHECK(kui_armada_ce_maple_fence_pre(EXT,TX,0,true,NULL,write_reg,&f,&state,&out)==KUI_ARMADA_CE_MAPLE_FENCE_ARGUMENT);
    CHECK(kui_armada_ce_maple_fence_pre(EXT,TX,0,true,read_reg,NULL,&f,&state,&out)==KUI_ARMADA_CE_MAPLE_FENCE_ARGUMENT);
    CHECK(kui_armada_ce_maple_fence_post(EXT,0,read_reg,write_reg,&f,&state,NULL)==KUI_ARMADA_CE_MAPLE_FENCE_ARGUMENT);
    CHECK(!memcmp(&before,&out,sizeof(out)) && !memcmp(&saved,&state,sizeof(state)) && !f.calls);
    union {uint32_t alignment;unsigned char bytes[256];} overlap;
    memset(&overlap,0xa5,sizeof(overlap));unsigned char copy[256];memcpy(copy,overlap.bytes,sizeof(copy));
    struct kui_armada_ce_maple_fence_state *s=(void *)overlap.bytes;
    struct kui_armada_ce_maple_fence_snapshot *o=(void *)overlap.bytes;
    CHECK(kui_armada_ce_maple_fence_pre(EXT,TX,0,true,read_reg,write_reg,&f,s,o)==KUI_ARMADA_CE_MAPLE_FENCE_ARGUMENT);
    CHECK(kui_armada_ce_maple_fence_post(EXT,0,read_reg,write_reg,&f,s,o)==KUI_ARMADA_CE_MAPLE_FENCE_ARGUMENT);
    CHECK(!memcmp(copy,overlap.bytes,sizeof(copy)) && !f.calls);
    const uint32_t ext[]={0u,EXT+1u,UINT32_MAX};
    for(unsigned i=0;i<3u;i++) {reset();CHECK(pre(ext[i],TX,0,true)!=KUI_ARMADA_CE_MAPLE_FENCE_OK && !f.calls);}
    const uint32_t source[]={0u,0x0c000000u,TX+1u,0x0d000000u,UINT32_MAX};
    for(unsigned i=0;i<5u;i++) {reset();CHECK(pre(EXT,source[i],0,true)!=KUI_ARMADA_CE_MAPLE_FENCE_OK && !f.calls);}
    reset();CHECK(pre(EXT,TX,2u,true)!=KUI_ARMADA_CE_MAPLE_FENCE_OK && !f.calls);
}
static void saturation(void) {
    reset();out.counts[0]=UINT32_C(0x31464c4d);
    for(unsigned i=1;i<5u;i++) out.counts[i]=UINT32_MAX;
    out.status[0]=out.status[1]=UINT32_MAX;
    CHECK(pre(EXT,TX,0,true)==KUI_ARMADA_CE_MAPLE_FENCE_OK && post(EXT,0)==KUI_ARMADA_CE_MAPLE_FENCE_OK);
    for(unsigned i=1;i<5u;i++) CHECK(out.counts[i]==UINT32_MAX);
    CHECK(out.status[0]==UINT32_MAX && out.status[1]==UINT32_MAX);
    f.reg[STATE]=1u;CHECK(pre(EXT,TX,0,true)==KUI_ARMADA_CE_MAPLE_FENCE_SKIPPED);
    CHECK(out.status[0]==UINT32_MAX && out.counts[1]==UINT32_MAX);
    CHECK(post(EXT,0)==KUI_ARMADA_CE_MAPLE_FENCE_SKIPPED);
}
int main(void) {
    CHECK(sizeof(out)==120u);
    successful_pair();skips_and_admission();poll_bound_and_readback();all_pre_callback_failures();
    rollback_callback_failures();all_post_callback_failures();post_mutations_restore();nesting_orphan_and_history();invalid_arguments();saturation();
    printf("CE Maple fence: %u checks; bounded ordered MMIO, every callback failure, rollback/retry, pairing, mutations, immutable invalid arguments and saturation passed\n",checks);
    return 0;
}
