/* SPDX-License-Identifier: GPL-3.0-only */
/* Real owned adapter, public KOS frame API replaced at the device boundary. */
#include "kui/vmu_read_bounded.h"
#include "kui/core.h"
#include <kos/irq.h>
#include <kos/thread.h>
#include <kos/cache.h>
#include <dc/memory.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static maple_device_t device;
static struct {
    uint8_t reply[524],expected[512];
    uint8_t *header_only;
    _Alignas(32) union {uint32_t words[256];uint8_t bytes[1024];} shared;
    unsigned locks,inits,queues,waits,unlocks,removes,wakes,polls;
    unsigned busy_attempts;
    unsigned invalidations,dirty_evictions;
    int cached_alias_dirty,allow_dirty_queue;
    unsigned irq_depth,irq_disables,irq_restores;
    unsigned block;
    int lock_failure,lock_bad_state,queue_failure,wait_result,next_state;
    int short_storage;
} model;
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(value>>(i*8u));
}
static void setup(unsigned block) {
    memset(&device,0,sizeof(device));memset(&model,0,sizeof(model));
    device.valid=1u;device.port=2;device.unit=1;device.info.functions=MAPLE_FUNC_MEMCARD;
    model.cached_alias_dirty=1;
    model.block=block;model.next_state=MAPLE_FRAME_RESPONDED;
    model.reply[0]=MAPLE_RESPONSE_DATATRF;model.reply[3]=130u;
    put32(model.reply+4u,MAPLE_FUNC_MEMCARD);put32(model.reply+8u,block<<24);
    for(unsigned i=0;i<512u;i++) model.reply[12u+i]=model.expected[i]=(uint8_t)(i*17u+block+31u);
}
int maple_frame_trylock(maple_frame_t *frame) {
    assert(frame==&device.frame);++model.locks;
    if(model.lock_failure || model.busy_attempts) {
        if(model.busy_attempts) --model.busy_attempts;
        frame->state=MAPLE_FRAME_SENT;return -1;
    }
    assert(frame->state==MAPLE_FRAME_VACANT);
    frame->state=model.lock_bad_state?MAPLE_FRAME_SENT:MAPLE_FRAME_UNSENT;return 0;
}
int thd_poll(thd_cb_t callback,void *context,unsigned long timeout) {
    assert(context==&device.frame && callback && timeout==100u);++model.polls;
    for(unsigned attempt=0;attempt<8u;attempt++) {
        int result=callback(context);if(result) return result;
        /* Another public driver owns the frame until it completes. */
        if(!model.lock_failure) device.frame.state=MAPLE_FRAME_VACANT;
    }
    return 0;
}
void maple_frame_init(maple_frame_t *frame) {
    assert(frame==&device.frame && frame->state==MAPLE_FRAME_UNSENT && !frame->queued);
    ++model.inits;memset(model.shared.bytes,0,sizeof(model.shared.bytes));
    /* Actual KOS aliases the request and response buffers. A response must
     * replace the request before the completion callback wakes the caller. */
    frame->send_buf=model.shared.words;frame->recv_buf=model.shared.bytes;frame->callback=NULL;
}
void dcache_inval_range(uintptr_t start,size_t count) {
    assert(device.frame.state==MAPLE_FRAME_UNSENT && !device.frame.queued);
    assert(model.inits==1u && !model.queues && !model.waits);
    assert(start==(((uintptr_t)device.frame.recv_buf&MEM_AREA_CACHE_MASK)|MEM_AREA_P1_BASE));
    assert(!(start&31u) && count==1024u);
    for(unsigned i=0;i<8u;i++) assert(device.frame.recv_buf[i]==0u);
    /* Invalidating an alias discards its dirty cache lines; it must not write
     * those stale bytes back into the separate uncached physical memory. */
    model.cached_alias_dirty=0;++model.invalidations;
}
int maple_queue_frame(maple_frame_t *frame) {
    assert(frame==&device.frame && frame->state==MAPLE_FRAME_UNSENT && !frame->queued);
    assert(frame->cmd==MAPLE_COMMAND_BREAD && frame->length==2);
    assert(frame->dst_port==device.port && frame->dst_unit==device.unit);
    assert(frame->send_buf[0]==MAPLE_FUNC_MEMCARD && frame->send_buf[1]==model.block<<24);
    assert(frame->callback);++model.queues;
    assert(model.allow_dirty_queue || (model.invalidations==1u && !model.cached_alias_dirty));
    if(model.queue_failure) return -1;
    frame->queued=1;frame->state=MAPLE_FRAME_SENT;return 0;
}
int maple_queue_remove(maple_frame_t *frame) {
    assert(frame==&device.frame && frame->queued && model.irq_depth);
    ++model.removes;frame->queued=0;return 0;
}
irq_mask_t irq_disable(void) {
    irq_mask_t prior=model.irq_depth;++model.irq_depth;++model.irq_disables;return prior;
}
void irq_restore(irq_mask_t prior) {
    assert(model.irq_depth==prior+1u);model.irq_depth=prior;++model.irq_restores;
}
int genwait_wait(void *object,const char *label,unsigned int timeout) {
    maple_frame_t *frame=object;
    assert(frame==&device.frame && frame->queued && frame->state==MAPLE_FRAME_SENT);
    assert(label && timeout==100u);++model.waits;
    memcpy(frame->recv_buf,model.reply,sizeof(model.reply));
    if(model.cached_alias_dirty) {
        /* A dirty cached line left by calloc evicts after uncached DMA. The
         * response header remains valid while one payload line becomes zero. */
        memset(frame->recv_buf+128u,0,32u);++model.dirty_evictions;
        model.cached_alias_dirty=0;
    }
    if(model.short_storage) {
        model.header_only=malloc(4u);assert(model.header_only);
        memcpy(model.header_only,model.reply,4u);frame->recv_buf=model.header_only;
    }
    frame->state=model.next_state;
    if(frame->state==MAPLE_FRAME_RESPONDED) {
        frame->queued=0;frame->callback(NULL,frame);
    }
    return model.wait_result;
}
void genwait_wake_all(const void *object) {assert(object==&device.frame);++model.wakes;}
void maple_frame_unlock(maple_frame_t *frame) {
    assert(frame==&device.frame && frame->state==MAPLE_FRAME_RESPONDED && !frame->queued);
    ++model.unlocks;frame->state=MAPLE_FRAME_VACANT;
}
static void check(int expected) {
    uint8_t out[514],before[sizeof(out)];memset(out,0x7bu,sizeof(out));memcpy(before,out,sizeof(out));
    int result=kui_vmu_read_bounded(&device,(uint16_t)model.block,out+1u);
    assert(result==expected && out[0]==0x7bu && out[513]==0x7bu);
    if(expected==MAPLE_EOK) assert(!memcmp(out+1u,model.expected,512u));
    else assert(!memcmp(out,before,sizeof(out)));
    const struct kui_vmu_read_bounded_diagnostic *d=kui_vmu_read_bounded_last();
    assert(d && d->block==model.block && d->result==expected && d->lock_attempts==model.locks);
    assert(d->expected_function==MAPLE_FUNC_MEMCARD && d->expected_location==model.block<<24);
    assert(model.polls==1u && model.locks>=1u && !model.irq_depth && model.irq_disables==model.irq_restores);
    if(expected==MAPLE_EOK) {
        assert(d->stage==KUI_VMU_READ_COMPLETE && d->response==MAPLE_RESPONSE_DATATRF && d->data_len==130u);
        assert(d->actual_function==MAPLE_FUNC_MEMCARD && d->actual_location==model.block<<24);
        assert(d->frame_state==MAPLE_FRAME_RESPONDED && !d->queued);
        uint32_t crc=kui_crc32(0,model.expected,512u);
        assert(d->raw_crc_before==crc && d->raw_crc_after==crc && d->copy_crc==crc);
    }
    else assert(!d->raw_crc_before && !d->raw_crc_after && !d->copy_crc);
    if(model.lock_failure) {
        assert(!model.inits && !model.invalidations && !model.queues && !model.waits && !model.unlocks);
        assert(d->stage==KUI_VMU_READ_LOCK && d->response==-127 && d->frame_state==MAPLE_FRAME_SENT);
    }
    else {
        assert(model.inits==1u && model.invalidations==1u && model.queues==1u && !model.dirty_evictions);
        if(model.queue_failure) assert(!model.waits && !model.unlocks && !model.removes);
        else {
            assert(model.waits==1u);
            if(model.next_state==MAPLE_FRAME_RESPONDED)
                assert(model.unlocks==1u && !model.removes && model.wakes==1u);
            else if(model.next_state==MAPLE_FRAME_SENT) {
                assert(!model.unlocks && !model.removes && !model.wakes);
                assert(device.frame.state==MAPLE_FRAME_SENT && device.frame.queued);
                /* DMA may finish only after the helper has returned. It must
                 * still own the frame, and its late callback must unlock
                 * without waking or touching the expired caller's output. */
                struct kui_vmu_read_bounded_diagnostic saved=*d;
                device.frame.state=MAPLE_FRAME_RESPONDED;device.frame.queued=0;
                device.frame.callback(NULL,&device.frame);
                assert(!memcmp(d,&saved,sizeof(saved)));
                assert(model.unlocks==1u && !model.wakes);
                assert(!memcmp(out,before,sizeof(out)));
            } else assert(!model.unlocks && model.removes==1u && !model.wakes);
        }
    }
    if(model.lock_failure) assert(device.frame.state==MAPLE_FRAME_SENT && !device.frame.queued);
    else assert(device.frame.state==MAPLE_FRAME_VACANT && !device.frame.queued);
    free(model.header_only);model.header_only=NULL;
}
static void exact_lengths_and_locations(void) {
    for(unsigned length=0;length<256u;length++) {
        setup(0u);model.reply[3]=(uint8_t)length;
        /* For every malformed length, an allocation contains only the four
         * response-header bytes. ASan catches any software payload access. */
        model.short_storage=length!=130u;
        check(length==130u?MAPLE_EOK:MAPLE_EFAIL);
        if(length!=130u) {
            const struct kui_vmu_read_bounded_diagnostic *d=kui_vmu_read_bounded_last();
            assert(d->stage==KUI_VMU_READ_LENGTH && d->data_len==length);
            assert(!d->actual_function && !d->actual_location);
        }
    }
    for(unsigned block=0;block<256u;block++) {setup(block);check(MAPLE_EOK);}
}
static void malformed_responses(void) {
    for(unsigned type=0;type<256u;type++) {
        setup(255u);model.reply[0]=(uint8_t)type;model.short_storage=type!=MAPLE_RESPONSE_DATATRF;
        check(type==MAPLE_RESPONSE_DATATRF?MAPLE_EOK:MAPLE_EFAIL);
        if(type!=MAPLE_RESPONSE_DATATRF) {
            const struct kui_vmu_read_bounded_diagnostic *d=kui_vmu_read_bounded_last();
            assert(d->stage==KUI_VMU_READ_TYPE && d->response==(int8_t)type);
            assert(!d->actual_function && !d->actual_location);
        }
    }
    const uint32_t bad_functions[]={0u,1u,0x04000000u,0x02000001u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(bad_functions)/sizeof(bad_functions[0]);i++) {
        setup(4u);put32(model.reply+4u,bad_functions[i]);check(MAPLE_EFAIL);
        const struct kui_vmu_read_bounded_diagnostic *d=kui_vmu_read_bounded_last();
        assert(d->stage==KUI_VMU_READ_FUNCTION && d->actual_function==bad_functions[i]);
        assert(d->actual_location==4u<<24);
    }
    const uint32_t bad_locations[]={0u,1u,0x01000100u,0x01010000u,0x02000000u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(bad_locations)/sizeof(bad_locations[0]);i++) {
        setup(1u);put32(model.reply+8u,bad_locations[i]);check(MAPLE_EFAIL);
        const struct kui_vmu_read_bounded_diagnostic *d=kui_vmu_read_bounded_last();
        assert(d->stage==KUI_VMU_READ_LOCATION && d->actual_location==bad_locations[i]);
        assert(d->actual_function==MAPLE_FUNC_MEMCARD);
    }
}
static void frame_lifetimes(void) {
    setup(42u);model.lock_failure=1;check(MAPLE_ETIMEOUT);
    assert(model.locks==8u);
    /* A transient owner refused by the old single trylock is now allowed to
     * finish. No BREAD is queued until our atomic lock actually succeeds. */
    for(unsigned busy=1u;busy<=6u;busy++) {
        setup(42u);model.busy_attempts=busy;check(MAPLE_EOK);
        assert(model.locks==busy+1u && model.queues==1u);
    }
    setup(42u);model.queue_failure=1;check(MAPLE_EFAIL);
    for(unsigned state=0;state<6u;state++) if(state!=MAPLE_FRAME_RESPONDED) {
        setup(42u);model.next_state=(int)state;check(MAPLE_EFAIL);
        setup(42u);model.next_state=(int)state;model.wait_result=-1;check(MAPLE_ETIMEOUT);
    }
    /* A completed frame is authoritative even if wake-up raced the waiter. */
    setup(42u);model.wait_result=-1;check(MAPLE_EOK);
    setup(42u);model.lock_bad_state=1;uint8_t out[512];memset(out,0x6b,sizeof(out));
    assert(kui_vmu_read_bounded(&device,42u,out)==MAPLE_EFAIL);
    assert(device.frame.state==MAPLE_FRAME_SENT && !model.inits && !model.queues && !model.unlocks);
    for(unsigned i=0;i<512u;i++) assert(out[i]==0x6bu);
}
static void invalid_arguments(void) {
    for(unsigned fault=0;fault<10u;fault++) {
        setup(42u);uint8_t out[512];memset(out,0x6b,sizeof(out));
        maple_device_t *input=&device;uint8_t *output=out;uint16_t block=42u;
        if(fault==0u) input=NULL;
        if(fault==1u) output=NULL;
        if(fault==2u) block=256u;
        if(fault==3u) device.valid=0u;
        if(fault==4u) device.info.functions=0u;
        if(fault==5u) device.port=-1;
        if(fault==6u) device.port=MAPLE_PORT_COUNT;
        if(fault==7u) device.unit=0;
        if(fault==8u) device.unit=-1;
        if(fault==9u) device.unit=MAPLE_UNIT_COUNT;
        assert(kui_vmu_read_bounded(input,block,output)==MAPLE_EINVALID);
        assert(!model.locks && !model.inits && !model.invalidations && !model.queues && !model.waits);
        const struct kui_vmu_read_bounded_diagnostic *d=kui_vmu_read_bounded_last();
        assert(d->stage==KUI_VMU_READ_ARGUMENT && d->response==-127 && d->result==MAPLE_EINVALID);
        for(unsigned i=0;i<512u;i++) assert(out[i]==0x6bu);
    }
}
static void model_reply_wake(maple_state_t *state,maple_frame_t *frame) {
    (void)state;genwait_wake_all(frame);
}
static void dirty_cached_receive_alias(void) {
    /* Characterize the old coherent-host blind spot with separate logical
     * cached and uncached views. Skipping cache ownership transfer corrupts a
     * payload while leaving type, length, function and block checks intact. */
    setup(0u);model.allow_dirty_queue=1;
    maple_frame_t *f=&device.frame;
    assert(maple_frame_trylock(f)==0);maple_frame_init(f);
    f->send_buf[0]=MAPLE_FUNC_MEMCARD;f->send_buf[1]=0u;
    f->cmd=MAPLE_COMMAND_BREAD;f->length=2;f->dst_port=device.port;f->dst_unit=device.unit;
    f->callback=model_reply_wake;
    assert(maple_queue_frame(f)==0 && genwait_wait(f,"dirty_alias_model",100u)==0);
    assert(model.dirty_evictions==1u && !model.invalidations);
    assert(f->recv_buf[0]==MAPLE_RESPONSE_DATATRF && f->recv_buf[3]==130u);
    assert(!memcmp(f->recv_buf+4u,model.reply+4u,8u));
    assert(memcmp(f->recv_buf+12u,model.expected,512u));
    maple_frame_unlock(f);
    /* The actual adapter must preserve the complete payload under the same
     * dirty-alias starting condition. Existing assertions enforce ownership,
     * exact alias/range and ordering before queue submission. */
    setup(0u);check(MAPLE_EOK);
    assert(model.invalidations==1u && !model.dirty_evictions);
}
int main(void) {
    dirty_cached_receive_alias();exact_lengths_and_locations();malformed_responses();frame_lifetimes();invalid_arguments();
    puts("Bounded VMU BREAD: dirty cached receive aliases, shared request/reply buffer, bounded busy acquisition, every response length/type, exact block/function, diagnostics, unchanged failure output and public frame cleanup passed");
    return 0;
}
