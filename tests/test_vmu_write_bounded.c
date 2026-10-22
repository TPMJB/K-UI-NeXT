/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent public-API model of Maple DMA, including SDK AGAIN resubmits.
 * Each failure case forks before entering the permanently tainted writer. */
#define _POSIX_C_SOURCE 200809L
#include "kui/vmu_write_bounded.h"
#include <dc/asic.h>
#include <dc/memory.h>
#include <kos/cache.h>
#include <kos/genwait.h>
#include <kos/irq.h>
#include <kos/thread.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

enum fault_kind {
    NONE,RESPONSE,LENGTH,SOURCE,DESTINATION,CANCEL,BUSY_PARENT,BUSY_TARGET,
    TRANSIENT_PARENT,TRANSIENT_TARGET,QUEUE_FAIL,QUEUE_SENT_FAIL,SENT_TIMEOUT,
    UNSENT_TIMEOUT,UNSENT_IRQ,CHANGE_PARENT,CHANGE_TARGET,NO_HANDLER,
    STEAL_HANDLER,WAIT_RACE,INVALID,REENTRANT,OVERLAP,STALE_SELF,FINISH_QUEUED
};
static struct {enum fault_kind kind;unsigned phase,value,block;} test;
static maple_device_t target,parent,replacement;
static struct {
    _Alignas(32) union {uint32_t words[256];uint8_t bytes[1024];} shared;
    uint8_t expected[512],*caller_payload,*header_only;
    maple_frame_t other;
    uint8_t other_reply[4];
    unsigned parent_owned,target_owned,parent_attempts,target_attempts,parent_lookups,target_lookups;
    unsigned inits,invalidations,queues,waits,removes,wakes,unlocks;
    unsigned parent_unlocks,target_unlocks,polls,callbacks,other_completions;
    unsigned sdk_retries,sdk_calls,irq_depth,irq_disables,irq_restores,hook_sets;
    unsigned dirty,dirty_evictions,cancel_calls,caller_cancel_dead;
    unsigned active_phase,late,hook_stolen,reentered,finish_fault;
    int sdk_cookie,foreign_cookie;
    asic_evt_handler_entry_t handler,saved_wrapper;
} model;

uint8_t maple_addr(int port,int unit) {
    assert(port>=0 && port<4 && unit>=0 && unit<6);
    if(test.kind==FINISH_QUEUED && model.queues==5u &&
       target.frame.state==MAPLE_FRAME_RESPONDED && !target.frame.queued && !model.finish_fault) {
        /* Inject an owned-state anomaly after the final reply completed,
         * immediately before successful parsing would reach cleanup. */
        target.frame.queued=1;model.finish_fault=1;
    }
    return (uint8_t)((port<<6)|(unit?1u<<(unit-1):0x20u));
}
maple_device_t *maple_enum_dev(int port,int unit) {
    if(port!=target.port) return NULL;
    if(unit==0) {
        ++model.parent_lookups;
        if(test.kind==CHANGE_PARENT && model.parent_lookups>1u && model.queues>=test.phase) return &replacement;
        return &parent;
    }
    if(unit==target.unit) {
        ++model.target_lookups;
        if(test.kind==CHANGE_TARGET && model.target_lookups>1u && model.queues>=test.phase) return &replacement;
        return &target;
    }
    return NULL;
}
int maple_frame_trylock(maple_frame_t *frame) {
    unsigned *attempts=frame==&parent.frame?&model.parent_attempts:&model.target_attempts;
    unsigned *owned=frame==&parent.frame?&model.parent_owned:&model.target_owned;
    assert(frame==&parent.frame || frame==&target.frame);++*attempts;
    assert(!*owned);
    int permanent=(frame==&parent.frame && test.kind==BUSY_PARENT) ||
        (frame==&target.frame && test.kind==BUSY_TARGET);
    int transient=(frame==&parent.frame && test.kind==TRANSIENT_PARENT) ||
        (frame==&target.frame && test.kind==TRANSIENT_TARGET);
    if(permanent || (transient && *attempts<=test.value)) {frame->state=MAPLE_FRAME_SENT;return -1;}
    assert(frame->state==MAPLE_FRAME_VACANT);
    frame->state=MAPLE_FRAME_UNSENT;*owned=1;return 0;
}
int thd_poll(thd_cb_t callback,void *context,unsigned long timeout) {
    assert(callback && context && timeout==100u);++model.polls;
    for(unsigned attempt=0;attempt<8u;attempt++) {
        int result=callback(context);if(result) return result;
        if(!model.parent_owned && test.kind!=BUSY_PARENT) parent.frame.state=MAPLE_FRAME_VACANT;
        if(!model.target_owned && test.kind!=BUSY_TARGET) target.frame.state=MAPLE_FRAME_VACANT;
    }
    return 0;
}
void maple_frame_init(maple_frame_t *frame) {
    assert(frame==&target.frame && model.parent_owned && model.target_owned);
    assert(frame->state==MAPLE_FRAME_UNSENT && !frame->queued);
    assert(model.inits==model.queues);++model.inits;
    free(model.header_only);model.header_only=NULL;
    memset(model.shared.bytes,0,sizeof(model.shared.bytes));model.dirty=1;
    frame->send_buf=model.shared.words;frame->recv_buf=model.shared.bytes;frame->callback=NULL;
}
void dcache_inval_range(uintptr_t start,size_t count) {
    assert(model.parent_owned && model.target_owned && target.frame.state==MAPLE_FRAME_UNSENT && !target.frame.queued);
    assert(start==(((uintptr_t)target.frame.recv_buf&MEM_AREA_CACHE_MASK)|MEM_AREA_P1_BASE));
    assert(!(start&31u) && count==1024u && model.inits==model.invalidations+1u);
    for(unsigned i=0;i<136u;i++) assert(model.shared.bytes[i]==0);
    model.dirty=0;++model.invalidations;
}
static void foreign_handler(uint32_t code,void *context) {
    assert(code==ASIC_EVT_MAPLE_DMA && context==&model.foreign_cookie);
}
static void sdk_handler(uint32_t code,void *context) {
    assert(code==ASIC_EVT_MAPLE_DMA && context==&model.sdk_cookie);++model.sdk_calls;
    if(target.frame.state==MAPLE_FRAME_SENT && target.frame.queued) {
        /* This models the SDK behavior the prototype must intercept first. */
        if((int8_t)target.frame.recv_buf[0]==MAPLE_RESPONSE_AGAIN) {
            ++model.sdk_retries;target.frame.state=MAPLE_FRAME_UNSENT;
        } else {
            target.frame.queued=0;target.frame.state=MAPLE_FRAME_RESPONDED;
            assert(target.frame.callback);++model.callbacks;
            target.frame.callback(NULL,&target.frame);
        }
    }
    if(model.other.state==MAPLE_FRAME_SENT && model.other.queued) {
        model.other.queued=0;model.other.state=MAPLE_FRAME_RESPONDED;++model.other_completions;
    }
}
asic_evt_handler_entry_t asic_evt_set_handler(uint16_t code,asic_evt_handler handler,void *context) {
    assert(code==ASIC_EVT_MAPLE_DMA && model.irq_depth);
    asic_evt_handler_entry_t old=model.handler;
    if(handler && handler!=sdk_handler && handler!=foreign_handler && old.hdl==sdk_handler) {
        model.saved_wrapper.hdl=handler;model.saved_wrapper.data=context;
    }
    model.handler.hdl=handler;model.handler.data=context;++model.hook_sets;return old;
}
irq_mask_t irq_disable(void) {
    irq_mask_t prior=model.irq_depth;++model.irq_depth;++model.irq_disables;return prior;
}
void irq_restore(irq_mask_t prior) {
    assert(model.irq_depth==prior+1u);model.irq_depth=prior;++model.irq_restores;
}
int maple_queue_frame(maple_frame_t *frame) {
    unsigned phase=model.queues;
    assert(frame==&target.frame && model.parent_owned && model.target_owned && model.irq_depth);
    assert(phase<5u && frame->state==MAPLE_FRAME_UNSENT && !frame->queued);
    assert(frame->cmd==(phase<4u?MAPLE_COMMAND_BWRITE:MAPLE_COMMAND_BSYNC));
    assert(frame->length==(phase<4u?34:2) && frame->dst_port==target.port && frame->dst_unit==target.unit);
    assert(frame->send_buf[0]==MAPLE_FUNC_MEMCARD);
    assert(frame->send_buf[1]==((test.block<<24)|(phase<<8)));
    assert(model.inits==phase+1u && model.invalidations==phase+1u && !model.dirty);
    if(phase<4u) assert(!memcmp(frame->send_buf+2,model.expected+phase*128u,128u));
    assert(frame->callback && model.handler.hdl && model.handler.hdl!=sdk_handler);
    model.active_phase=phase;++model.queues;
    if(!phase) memset(model.caller_payload,0xe3,512u);
    if(test.kind==REENTRANT && !model.reentered) {
        model.reentered=1;struct kui_vmu_write_diagnostic nested;
        assert(kui_vmu_write_bounded(&target,3u,model.expected,NULL,NULL,&nested)==KUI_VMU_WRITE_BUSY);
        assert(!nested.submitted_mask);
    }
    if(test.kind==QUEUE_FAIL && phase==test.phase) return -1;
    frame->queued=1;frame->state=MAPLE_FRAME_SENT;
    if(test.kind==QUEUE_SENT_FAIL && phase==test.phase) return -1;
    return 0;
}
int maple_queue_remove(maple_frame_t *frame) {
    assert(frame==&target.frame && frame->queued && model.irq_depth);
    frame->queued=0;++model.removes;return 0;
}
void maple_frame_unlock(maple_frame_t *frame) {
    assert(!frame->queued && frame->state==MAPLE_FRAME_RESPONDED);
    if(frame==&target.frame) {assert(model.target_owned);model.target_owned=0;++model.target_unlocks;}
    else {assert(frame==&parent.frame && model.parent_owned);model.parent_owned=0;++model.parent_unlocks;}
    frame->state=MAPLE_FRAME_VACANT;++model.unlocks;
}
static void fire_dma(void) {
    assert(model.handler.hdl);
    irq_mask_t prior=irq_disable();
    model.other.state=MAPLE_FRAME_SENT;model.other.queued=1;
    asic_evt_handler_entry_t entry=model.handler;entry.hdl(ASIC_EVT_MAPLE_DMA,entry.data);
    irq_restore(prior);
}
static void reply(unsigned phase,int late) {
    uint8_t header[4]={MAPLE_RESPONSE_OK,(uint8_t)(target.port<<6),maple_addr(target.port,target.unit),0};
    if(phase==test.phase) {
        if(test.kind==RESPONSE) header[0]=(uint8_t)test.value;
        if(test.kind==LENGTH) header[3]=(uint8_t)test.value;
        if(test.kind==SOURCE) header[2]=(uint8_t)test.value;
        if(test.kind==DESTINATION) header[1]=(uint8_t)test.value;
    }
    if(late) header[0]=(uint8_t)MAPLE_RESPONSE_AGAIN;
    memset(model.shared.bytes,0xd7,sizeof(model.shared.bytes));memcpy(model.shared.bytes,header,4u);
    if(model.dirty) {++model.dirty_evictions;memset(model.shared.bytes,0,32u);}
    /* Software parsing gets only the four-byte header: ASan rejects any
     * access to response.data, even for a malicious nonzero length. */
    free(model.header_only);model.header_only=malloc(4u);assert(model.header_only);
    memcpy(model.header_only,model.shared.bytes,4u);target.frame.recv_buf=model.header_only;
}
int genwait_wait(void *object,const char *label,unsigned int timeout) {
    assert(object==&target.frame && label && timeout==100u && !model.irq_depth);
    assert(target.frame.queued && target.frame.state==MAPLE_FRAME_SENT);++model.waits;
    unsigned phase=model.active_phase;
    if(phase==test.phase && test.kind==SENT_TIMEOUT) return -1;
    reply(phase,0);
    if(phase==test.phase && (test.kind==UNSENT_TIMEOUT || test.kind==UNSENT_IRQ)) {
        target.frame.state=MAPLE_FRAME_UNSENT;
        if(test.kind==UNSENT_IRQ) {
            target.frame.recv_buf[0]=(uint8_t)MAPLE_RESPONSE_AGAIN;fire_dma();
        }
        return -1;
    }
    fire_dma();
    if(test.kind==STEAL_HANDLER && phase==test.phase) {
        irq_mask_t prior=irq_disable();
        (void)asic_evt_set_handler(ASIC_EVT_MAPLE_DMA,foreign_handler,&model.foreign_cookie);
        irq_restore(prior);model.hook_stolen=1;
    }
    return test.kind==WAIT_RACE?-1:0;
}
void genwait_wake_all(const void *object) {assert(object==&target.frame && !model.caller_cancel_dead);++model.wakes;}
static int cancel(void *context) {
    assert(context==&model.cancel_calls && !model.caller_cancel_dead);++model.cancel_calls;
    return test.kind==CANCEL && model.queues==test.phase;
}
static void setup(void) {
    memset(&model,0,sizeof(model));memset(&target,0,sizeof(target));memset(&parent,0,sizeof(parent));
    target.valid=parent.valid=1;target.port=parent.port=2;target.unit=1;parent.unit=0;
    target.info.functions=MAPLE_FUNC_MEMCARD;parent.info.functions=MAPLE_FUNC_CONTROLLER;
    replacement=target;
    for(unsigned i=0;i<512u;i++) model.expected[i]=(uint8_t)(i*31u+test.block*7u+19u);
    model.handler.hdl=sdk_handler;model.handler.data=&model.sdk_cookie;
    if(test.kind==NO_HANDLER) {model.handler.hdl=NULL;model.handler.data=NULL;}
}
static void assert_refused(unsigned queues) {
    uint8_t payload[512];memcpy(payload,model.expected,sizeof(payload));
    struct kui_vmu_write_diagnostic d;
    enum fault_kind saved=test.kind;test.kind=NONE;
    assert(kui_vmu_write_bounded(&target,4u,payload,cancel,&model.cancel_calls,&d)==KUI_VMU_WRITE_REFUSED);
    assert(d.stage==KUI_VMU_WRITE_TAINTED && d.tainted && !d.submitted_mask && model.queues==queues);
    test.kind=saved;
}
static void one_case(void) {
    setup();uint8_t payload[512];memcpy(payload,model.expected,sizeof(payload));model.caller_payload=payload;
    if(test.kind==OVERLAP) {
        union {max_align_t alignment;uint8_t bytes[1024];} overlapping;
        uint8_t before[sizeof(overlapping)];memset(&overlapping,0x5c,sizeof(overlapping));memcpy(before,&overlapping,sizeof(before));
        unsigned output_at=test.value==0u?256u:test.value==1u?252u:764u;
        struct kui_vmu_write_diagnostic *output=(void *)(overlapping.bytes+output_at);
        assert(kui_vmu_write_bounded(&target,42u,overlapping.bytes+256u,cancel,&model.cancel_calls,output)==KUI_VMU_WRITE_INVALID);
        assert(!memcmp(&overlapping,before,sizeof(before)) && !model.queues && !model.parent_attempts && !model.target_attempts);
        return;
    }
    maple_device_t *input=&target;const uint8_t *bytes=payload;unsigned block=test.block;
    struct kui_vmu_write_diagnostic d;
    if(test.kind==INVALID) {
        if(test.value==0u) input=NULL;
        if(test.value==1u) bytes=NULL;
        if(test.value==2u) block=256u;
        if(test.value==3u) target.valid=0;
        if(test.value==4u) target.info.functions=0;
        if(test.value==5u) target.port=-1;
        if(test.value==6u) target.port=MAPLE_PORT_COUNT;
        if(test.value==7u) target.unit=0;
        if(test.value==8u) target.unit=-1;
        if(test.value==9u) target.unit=MAPLE_UNIT_COUNT;
        if(test.value==10u) parent.valid=0;
    }
    struct kui_vmu_write_diagnostic *output=test.kind==INVALID && test.value==11u?NULL:&d;
    int result=kui_vmu_write_bounded(input,(uint16_t)block,bytes,cancel,&model.cancel_calls,output);
    if(!output) {assert(result==KUI_VMU_WRITE_INVALID && !model.queues && !model.parent_attempts && !model.target_attempts);return;}
    unsigned queues=model.queues;
    assert(d.result==result && !model.irq_depth && model.irq_disables==model.irq_restores);
    assert(!model.sdk_retries && !model.callbacks && !model.dirty_evictions);
    int success=test.kind==NONE || test.kind==TRANSIENT_PARENT || test.kind==TRANSIENT_TARGET ||
        test.kind==WAIT_RACE || test.kind==REENTRANT || test.kind==STALE_SELF ||
        (test.kind==RESPONSE && test.value==MAPLE_RESPONSE_OK) ||
        (test.kind==LENGTH && !test.value) ||
        (test.kind==SOURCE && test.value==maple_addr(target.port,target.unit)) ||
        (test.kind==DESTINATION && test.value==(unsigned)(target.port<<6));
    if(success) {
        assert(result==KUI_VMU_WRITE_OK && d.stage==KUI_VMU_WRITE_COMPLETE && d.outcome==KUI_VMU_WRITE_ACKNOWLEDGED);
        assert(d.submitted_mask==31u && d.completed_mask==31u && d.acknowledged_mask==31u);
        assert(model.queues==5u && model.waits==5u && model.inits==5u && model.invalidations==5u && model.removes==5u);
        assert(model.other_completions==5u && model.sdk_calls==5u && model.wakes==5u);
        assert(model.parent_unlocks==1u && model.target_unlocks==1u && !model.parent_owned && !model.target_owned);
        assert(!d.tainted && !d.quarantined && !kui_vmu_write_bounded_tainted() && !kui_vmu_write_bounded_quarantined());
    } else {
        assert(result!=KUI_VMU_WRITE_OK && d.outcome!=KUI_VMU_WRITE_ACKNOWLEDGED);
        if(test.kind==INVALID || test.kind==BUSY_PARENT || test.kind==BUSY_TARGET || test.kind==NO_HANDLER) assert(!model.queues);
        else if(test.kind==CANCEL || test.kind==CHANGE_PARENT || test.kind==CHANGE_TARGET) assert(model.queues==test.phase);
        else assert(model.queues==test.phase+1u);
        if(test.kind==CANCEL) assert(result==KUI_VMU_WRITE_CANCELLED && d.stage==KUI_VMU_WRITE_CANCEL);
        if(test.kind==RESPONSE) assert(d.stage==KUI_VMU_WRITE_RESPONSE && d.response==(int8_t)test.value);
        if(test.kind==LENGTH) assert(d.stage==KUI_VMU_WRITE_LENGTH && d.data_len==test.value);
        if(test.kind==SOURCE || test.kind==DESTINATION) assert(d.stage==KUI_VMU_WRITE_HEADER);
        /* Derive the required safety state from the injected boundary, never
         * from the implementation's reported taint or submission masks. An
         * erroneous clean/unlocked return after a sent error must fail here. */
        unsigned prior=(1u<<test.phase)-1u,through=(1u<<(test.phase+1u))-1u;
        unsigned submitted=0,completed=0,acknowledged=0;
        int must_taint=0,must_quarantine=0;
        switch(test.kind) {
            case RESPONSE:case LENGTH:case SOURCE:case DESTINATION:
                submitted=completed=through;acknowledged=prior;must_taint=1;break;
            case SENT_TIMEOUT:case UNSENT_TIMEOUT:case UNSENT_IRQ:
                submitted=through;completed=acknowledged=prior;must_taint=1;
                must_quarantine=test.kind==SENT_TIMEOUT;break;
            case CANCEL:case QUEUE_FAIL:
                submitted=completed=acknowledged=prior;must_taint=test.phase!=0u;break;
            case QUEUE_SENT_FAIL:
                /* A broken enqueue can leave DMA owning the frame despite
                 * returning failure before the helper records submission. */
                submitted=completed=acknowledged=prior;must_taint=must_quarantine=1;break;
            case CHANGE_PARENT:case CHANGE_TARGET:
                /* This model changes identity at the post-reply check: the
                 * current response completed but its ACK is not trusted. */
                submitted=completed=prior;
                acknowledged=test.phase?(1u<<(test.phase-1u))-1u:0u;
                must_taint=test.phase!=0u;break;
            case STEAL_HANDLER:case FINISH_QUEUED:
                submitted=completed=acknowledged=through;must_taint=1;break;
            case INVALID:case BUSY_PARENT:case BUSY_TARGET:case NO_HANDLER:break;
            default:assert(!"unclassified writer failure expectation");
        }
        assert(d.submitted_mask==submitted && d.completed_mask==completed && d.acknowledged_mask==acknowledged);
        assert(d.tainted==(unsigned)must_taint && kui_vmu_write_bounded_tainted()==must_taint);
        assert(d.quarantined==(unsigned)must_quarantine && kui_vmu_write_bounded_quarantined()==must_quarantine);
        if(must_taint) {
            assert(model.parent_owned && model.target_owned && !model.unlocks);
            assert(d.outcome==KUI_VMU_WRITE_UNCERTAIN);
            unsigned cancels=model.cancel_calls;
            assert_refused(queues);assert(model.cancel_calls==cancels);
        } else {
            assert(d.outcome==KUI_VMU_WRITE_NOT_SENT && !submitted && !completed && !acknowledged);
            assert(!model.parent_owned && !model.target_owned);
        }
        if(test.kind==FINISH_QUEUED) {
            assert(model.finish_fault && d.stage==KUI_VMU_WRITE_FRAME && result==KUI_VMU_WRITE_FAILED);
            assert(model.queues==5u && model.removes==6u && !target.frame.queued);
        }
        if(test.kind==SENT_TIMEOUT || test.kind==QUEUE_SENT_FAIL) {
            assert(result==(test.kind==SENT_TIMEOUT?KUI_VMU_WRITE_TIMEOUT:KUI_VMU_WRITE_FAILED));
            assert(d.quarantined && kui_vmu_write_bounded_quarantined());
            assert(target.frame.state==MAPLE_FRAME_SENT && target.frame.queued);
            struct kui_vmu_write_diagnostic saved=d;unsigned wakes=model.wakes;
            model.caller_cancel_dead=1;memset(payload,0xac,sizeof(payload));reply(test.phase,1);fire_dma();
            assert(!memcmp(&d,&saved,sizeof(d)) && model.wakes==wakes && !model.sdk_retries);
            assert(!kui_vmu_write_bounded_quarantined() && kui_vmu_write_bounded_tainted());
            assert(model.parent_owned && model.target_owned && !model.unlocks);
            assert(target.frame.state==MAPLE_FRAME_RESPONDED && !target.frame.queued);
            model.caller_cancel_dead=0;assert_refused(queues);
        } else assert(!kui_vmu_write_bounded_quarantined());
    }
    if(test.kind==STALE_SELF) {
        assert(model.saved_wrapper.hdl && model.saved_wrapper.hdl!=sdk_handler);
        model.handler=model.saved_wrapper;
        unsigned before=model.queues,sdk_before=model.sdk_calls;
        assert(kui_vmu_write_bounded(&target,42u,payload,cancel,&model.cancel_calls,&d)==KUI_VMU_WRITE_FAILED);
        assert(d.stage==KUI_VMU_WRITE_HOOK && d.tainted && !d.submitted_mask && model.queues==before);
        assert(model.parent_owned && model.target_owned && model.handler.hdl==model.saved_wrapper.hdl);
        fire_dma();assert(model.sdk_calls==sdk_before+1u && !model.sdk_retries);
        assert_refused(before);free(model.header_only);return;
    }
    if(model.hook_stolen) assert(model.handler.hdl==foreign_handler && model.handler.data==&model.foreign_cookie);
    else if(test.kind==NO_HANDLER) assert(!model.handler.hdl && !model.handler.data);
    else assert(model.handler.hdl==sdk_handler && model.handler.data==&model.sdk_cookie);
    free(model.header_only);model.header_only=NULL;
}
static unsigned cases;
static void run(enum fault_kind kind,unsigned phase,unsigned value,unsigned block) {
    test.kind=kind;test.phase=phase;test.value=value;test.block=block;
    pid_t child=fork();assert(child>=0);
    if(!child) {one_case();_exit(0);}
    int status;assert(waitpid(child,&status,0)==child);
    if(!WIFEXITED(status) || WEXITSTATUS(status)) {
        fprintf(stderr,"writer case failed: kind=%u phase=%u value=%u block=%u status=%d\n",kind,phase,value,block,status);
        exit(1);
    }
    ++cases;
}
int main(void) {
    for(unsigned block=0;block<256u;block++) run(NONE,0,0,block);
    puts("PASS all256blocks: five exact commands, shared buffers, cache alias, original payload ownership");fflush(stdout);
    for(unsigned phase=0;phase<5u;phase++) for(unsigned value=0;value<256u;value++) {
        run(RESPONSE,phase,value,255u);run(LENGTH,phase,value,255u);
        run(SOURCE,phase,value,255u);run(DESTINATION,phase,value,255u);
    }
    puts("PASS all response codes, lengths and both address bytes at everyphase");fflush(stdout);
    for(unsigned phase=0;phase<5u;phase++) {
        run(CANCEL,phase,0,42u);run(QUEUE_FAIL,phase,0,42u);run(QUEUE_SENT_FAIL,phase,0,42u);
        run(SENT_TIMEOUT,phase,0,42u);run(UNSENT_TIMEOUT,phase,0,42u);run(UNSENT_IRQ,phase,0,42u);
        run(CHANGE_PARENT,phase,0,42u);run(CHANGE_TARGET,phase,0,42u);
        run(STEAL_HANDLER,phase,0,42u);
    }
    run(BUSY_PARENT,0,0,42u);run(BUSY_TARGET,0,0,42u);run(NO_HANDLER,0,0,42u);
    for(unsigned busy=1;busy<=6u;busy++) {run(TRANSIENT_PARENT,0,busy,42u);run(TRANSIENT_TARGET,0,busy,42u);}
    run(WAIT_RACE,0,0,42u);run(REENTRANT,0,0,42u);
    for(unsigned invalid=0;invalid<12u;invalid++) run(INVALID,0,invalid,42u);
    for(unsigned overlap=0;overlap<3u;overlap++) run(OVERLAP,0,overlap,42u);
    run(STALE_SELF,0,0,42u);
    run(FINISH_QUEUED,4u,0,42u);
    printf("PASS bounded writer prototype: %u isolated cases; no automatic retries or extra commands\n",cases);return 0;
}
