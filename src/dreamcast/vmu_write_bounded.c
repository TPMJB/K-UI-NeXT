/* SPDX-License-Identifier: GPL-3.0-only */
/* Quarantined prototype; deliberately absent from every production target. */
#if !defined(KUI_VMU_WRITE_BOUNDED_PROTOTYPE) || KUI_VMU_WRITE_BOUNDED_PROTOTYPE != 1
#error "Build this quarantined prototype only with its standalone test target"
#endif
#include "kui/vmu_write_bounded.h"
#include <dc/asic.h>
#include <dc/memory.h>
#include <kos/cache.h>
#include <kos/genwait.h>
#include <kos/irq.h>
#include <kos/thread.h>
#include <string.h>

static struct {
    volatile int active,tainted,quarantined,intercepted;
    int hooked;
    maple_device_t *device,*controller;
    maple_frame_t *frame,*guard;
    asic_evt_handler_entry_t previous;
    uint8_t payload[512];
} writer;

int kui_vmu_write_bounded_quarantined(void) {return writer.quarantined;}
int kui_vmu_write_bounded_tainted(void) {return writer.tainted;}

static void writer_dma(uint32_t code,void *context);
/* IRQs are masked by every caller. Preserve a handler installed by somebody
 * else if the caller violated the exclusive-handler-ownership contract. */
static int writer_hook_owned(void) {
    asic_evt_handler_entry_t old=asic_evt_set_handler(ASIC_EVT_MAPLE_DMA,writer_dma,&writer);
    if(old.hdl==writer_dma && old.data==&writer) return 1;
    (void)asic_evt_set_handler(ASIC_EVT_MAPLE_DMA,old.hdl,old.data);
    writer.hooked=0;return 0;
}
static int writer_hook_restore(void) {
    if(!writer.hooked || !writer_hook_owned()) return 0;
    (void)asic_evt_set_handler(ASIC_EVT_MAPLE_DMA,writer.previous.hdl,writer.previous.data);
    writer.hooked=0;return 1;
}
static void writer_dma(uint32_t code,void *context) {
    (void)context;
    asic_evt_handler_entry_t previous=writer.previous;
    maple_frame_t *frame=writer.frame;
    if(code==ASIC_EVT_MAPLE_DMA && context==&writer && writer.active && frame &&
       frame->state==MAPLE_FRAME_SENT && frame->queued) {
        /* This completion covers the active DMA batch. Remove only our frame
         * before the SDK sees its response: AGAIN must never become a retry. */
        if(maple_queue_remove(frame)==0) {
            frame->state=MAPLE_FRAME_RESPONDED;writer.intercepted=1;
            if(writer.quarantined) {
                /* All referenced state is static or caller-held device state.
                 * A late response cannot mutate the returned diagnostic. Keep
                 * both frame locks held: a partial write session is tainted. */
                writer.quarantined=0;writer.active=0;
                (void)writer_hook_restore();
            } else genwait_wake_all(frame);
        }
    }
    if(previous.hdl) previous.hdl(code,previous.data);
}
static void writer_unexpected_callback(maple_state_t *state,maple_frame_t *frame) {
    (void)state;
    /* This fallback never claims interception. If another owner stole the DMA
     * hook, a normal SDK callback is insufficient to certify no hidden retry. */
    genwait_wake_all(frame);
}
struct writer_lock_context {
    maple_frame_t *frame;
    struct kui_vmu_write_diagnostic *diagnostic;
};
static int writer_lock(void *context) {
    struct writer_lock_context *lock=context;
    ++lock->diagnostic->lock_attempts;
    return maple_frame_trylock(lock->frame)==0;
}
static int writer_same_device(int port,int unit) {
    return maple_enum_dev(port,unit)==writer.device &&
        maple_enum_dev(port,0)==writer.controller && writer.device->valid &&
        writer.controller->valid && writer.device->port==port && writer.device->unit==unit &&
        writer.controller->port==port && writer.controller->unit==0 &&
        (writer.device->info.functions&MAPLE_FUNC_MEMCARD);
}
static void writer_release_frame(maple_frame_t *frame) {
    /* Only a frame we exclusively own, with no queued/in-flight operation,
     * reaches this path. KOS unlock requires RESPONDED, including lock-only
     * guards and failures before submission. */
    frame->state=MAPLE_FRAME_RESPONDED;maple_frame_unlock(frame);
}
int kui_vmu_write_bounded(maple_device_t *device,uint16_t block,
    const uint8_t payload[512],kui_vmu_write_cancel_fn cancel,void *context,
    struct kui_vmu_write_diagnostic *output) {
    struct kui_vmu_write_diagnostic d={0};
    d.block=block;d.stage=KUI_VMU_WRITE_ARGUMENT;d.response=-127;
    d.expected_function=MAPLE_FUNC_MEMCARD;d.result=KUI_VMU_WRITE_INVALID;
    if(!output) return d.result;
    if(payload) {
        uintptr_t data_at=(uintptr_t)payload,output_at=(uintptr_t)output;
        if((data_at>=output_at && data_at-output_at<sizeof(*output)) ||
           (output_at>data_at && output_at-data_at<512u)) return d.result;
    }
    *output=d;
    if(!device || !payload || block>=256u || !device->valid ||
       !(device->info.functions&MAPLE_FUNC_MEMCARD) || device->port<0 ||
       device->port>=MAPLE_PORT_COUNT || device->unit<=0 || device->unit>=MAPLE_UNIT_COUNT)
        return d.result;
    int port=device->port,unit=device->unit;
    maple_device_t *controller=maple_enum_dev(port,0);
    if(!controller || controller==device || !controller->valid ||
       maple_enum_dev(port,unit)!=device) {d.stage=KUI_VMU_WRITE_DEVICE;*output=d;return d.result;}
    irq_mask_t saved=irq_disable();
    if(writer.active || writer.tainted) {
        d.stage=writer.tainted?KUI_VMU_WRITE_TAINTED:KUI_VMU_WRITE_LOCK;
        d.tainted=(uint32_t)writer.tainted;d.quarantined=(uint32_t)writer.quarantined;
        d.result=writer.tainted?KUI_VMU_WRITE_REFUSED:KUI_VMU_WRITE_BUSY;
        irq_restore(saved);*output=d;return d.result;
    }
    writer.active=1;writer.intercepted=0;writer.device=device;writer.controller=controller;
    writer.frame=&device->frame;writer.guard=&controller->frame;
    irq_restore(saved);
    /* No request ever retains a pointer into the caller's payload. */
    memcpy(writer.payload,payload,sizeof(writer.payload));
    int guard_locked=0,frame_locked=0;
    d.stage=KUI_VMU_WRITE_LOCK;d.result=KUI_VMU_WRITE_BUSY;
    struct writer_lock_context lock={writer.guard,&d};
    if(!thd_poll(writer_lock,&lock,100u)) goto finished;
    guard_locked=1;
    if(writer.guard->state!=MAPLE_FRAME_UNSENT || writer.guard->queued) {
        d.stage=KUI_VMU_WRITE_FRAME;d.frame_state=(uint32_t)writer.guard->state;
        d.queued=(uint32_t)writer.guard->queued;guard_locked=0;goto finished;
    }
    lock.frame=writer.frame;
    if(!thd_poll(writer_lock,&lock,100u)) goto finished;
    frame_locked=1;
    if(writer.frame->state!=MAPLE_FRAME_UNSENT || writer.frame->queued) {
        d.stage=KUI_VMU_WRITE_FRAME;d.frame_state=(uint32_t)writer.frame->state;
        d.queued=(uint32_t)writer.frame->queued;frame_locked=0;goto finished;
    }
    d.stage=KUI_VMU_WRITE_HOOK;d.result=KUI_VMU_WRITE_FAILED;
    saved=irq_disable();
    asic_evt_handler_entry_t previous=asic_evt_set_handler(ASIC_EVT_MAPLE_DMA,writer_dma,&writer);
    if(previous.hdl==writer_dma) {
        /* A stale self hook must not overwrite its separately retained chain
         * with itself, which would recurse on the next unrelated DMA. */
        (void)asic_evt_set_handler(ASIC_EVT_MAPLE_DMA,previous.hdl,previous.data);
        writer.tainted=1;irq_restore(saved);goto finished;
    }
    writer.previous=previous;
    writer.hooked=1;
    if(!writer.previous.hdl) {
        (void)writer_hook_restore();irq_restore(saved);goto finished;
    }
    irq_restore(saved);
    for(unsigned phase=0;phase<5u;phase++) {
        d.phase=phase;d.response=-127;d.data_len=0;
        d.response_destination=0;d.response_source=0;
        d.expected_location=((uint32_t)block<<24)|(phase<<8);
        d.stage=KUI_VMU_WRITE_CANCEL;
        if(cancel && cancel(context)) {d.result=KUI_VMU_WRITE_CANCELLED;goto finished;}
        d.stage=KUI_VMU_WRITE_DEVICE;
        if(!writer_same_device(port,unit)) {d.result=KUI_VMU_WRITE_REFUSED;goto finished;}
        d.stage=KUI_VMU_WRITE_HOOK;
        saved=irq_disable();int owned=writer_hook_owned();irq_restore(saved);
        if(!owned) {d.result=KUI_VMU_WRITE_FAILED;goto finished;}
        d.stage=KUI_VMU_WRITE_FRAME;
        maple_frame_t *frame=writer.frame;
        saved=irq_disable();
        if(frame->queued || (frame->state!=MAPLE_FRAME_UNSENT && frame->state!=MAPLE_FRAME_RESPONDED)) {
            irq_restore(saved);d.result=KUI_VMU_WRITE_FAILED;goto finished;
        }
        frame->state=MAPLE_FRAME_UNSENT;writer.intercepted=0;
        maple_frame_init(frame);
        uintptr_t cached=((uintptr_t)frame->recv_buf&MEM_AREA_CACHE_MASK)|MEM_AREA_P1_BASE;
        dcache_inval_range(cached,1024u);
        frame->send_buf[0]=MAPLE_FUNC_MEMCARD;frame->send_buf[1]=d.expected_location;
        if(phase<4u) memcpy(frame->send_buf+2,writer.payload+phase*128u,128u);
        frame->cmd=phase<4u?MAPLE_COMMAND_BWRITE:MAPLE_COMMAND_BSYNC;
        frame->dst_port=port;frame->dst_unit=unit;frame->length=phase<4u?34:2;
        frame->callback=writer_unexpected_callback;
        d.stage=KUI_VMU_WRITE_QUEUE;
        int queued=maple_queue_frame(frame);
        if(queued<0) {irq_restore(saved);d.result=KUI_VMU_WRITE_FAILED;goto finished;}
        d.submitted_mask|=1u<<phase;d.outcome=KUI_VMU_WRITE_UNCERTAIN;
        irq_restore(saved);
        d.stage=KUI_VMU_WRITE_WAIT;
        d.wait_result=genwait_wait(frame,"bounded_vmu_write",100u);
        saved=irq_disable();d.frame_state=(uint32_t)frame->state;d.queued=(uint32_t)frame->queued;
        if(frame->state!=MAPLE_FRAME_RESPONDED || frame->queued || !writer.intercepted) {
            if(frame->state==MAPLE_FRAME_SENT) {
                writer.quarantined=1;d.quarantined=1;
            } else if(frame->queued) (void)maple_queue_remove(frame);
            irq_restore(saved);d.result=d.wait_result<0?KUI_VMU_WRITE_TIMEOUT:KUI_VMU_WRITE_FAILED;
            goto finished;
        }
        d.completed_mask|=1u<<phase;irq_restore(saved);
        /* Successful replies have no payload. Never inspect response.data,
         * including on every error code or malformed nonzero data length. */
        const maple_response_t *response=(const maple_response_t *)(const void *)frame->recv_buf;
        d.response=response->response;d.data_len=response->data_len;
        d.response_destination=response->dst_addr;d.response_source=response->src_addr;
        d.stage=KUI_VMU_WRITE_DEVICE;
        if(!writer_same_device(port,unit)) {d.result=KUI_VMU_WRITE_REFUSED;goto finished;}
        d.stage=KUI_VMU_WRITE_HEADER;
        if(response->dst_addr!=(uint8_t)(port<<6) || response->src_addr!=maple_addr(port,unit)) {
            d.result=KUI_VMU_WRITE_FAILED;goto finished;
        }
        d.stage=KUI_VMU_WRITE_RESPONSE;
        if(response->response!=MAPLE_RESPONSE_OK) {d.result=KUI_VMU_WRITE_FAILED;goto finished;}
        d.stage=KUI_VMU_WRITE_LENGTH;
        if(response->data_len) {d.result=KUI_VMU_WRITE_FAILED;goto finished;}
        d.acknowledged_mask|=1u<<phase;
    }
    d.stage=KUI_VMU_WRITE_COMPLETE;d.result=KUI_VMU_WRITE_OK;d.outcome=KUI_VMU_WRITE_ACKNOWLEDGED;
finished:
    saved=irq_disable();
    if(frame_locked && writer.frame->state==MAPLE_FRAME_SENT) {
        /* Even an unexpected queue/API failure must not release storage that
         * DMA may still own. No subsequent call can reclaim this session. */
        writer.quarantined=1;writer.tainted=1;d.outcome=KUI_VMU_WRITE_UNCERTAIN;
        if(d.result==KUI_VMU_WRITE_OK) {d.stage=KUI_VMU_WRITE_FRAME;d.result=KUI_VMU_WRITE_FAILED;}
    } else if(frame_locked && writer.frame->queued) {
        (void)maple_queue_remove(writer.frame);
        if(d.result==KUI_VMU_WRITE_OK) {d.stage=KUI_VMU_WRITE_FRAME;d.result=KUI_VMU_WRITE_FAILED;}
    }
    if(d.result!=KUI_VMU_WRITE_OK && d.submitted_mask) {
        writer.tainted=1;d.outcome=KUI_VMU_WRITE_UNCERTAIN;
    }
    if(writer.frame && frame_locked) {d.frame_state=(uint32_t)writer.frame->state;d.queued=(uint32_t)writer.frame->queued;}
    if(!writer.quarantined && writer.hooked && !writer_hook_restore()) {
        d.stage=KUI_VMU_WRITE_HOOK;d.result=KUI_VMU_WRITE_FAILED;
        if(d.submitted_mask) {writer.tainted=1;d.outcome=KUI_VMU_WRITE_UNCERTAIN;}
    }
    d.tainted=(uint32_t)writer.tainted;d.quarantined=(uint32_t)writer.quarantined;
    if(!writer.tainted) {
        if(frame_locked) writer_release_frame(writer.frame);
        if(guard_locked) writer_release_frame(writer.guard);
    }
    if(!writer.quarantined) writer.active=0;
    irq_restore(saved);*output=d;return d.result;
}
