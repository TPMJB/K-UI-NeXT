/* SPDX-License-Identifier: GPL-3.0-only */
/* Independently authored bounded BREAD adapter using public KOS Maple APIs. */
#include "kui/vmu_read_bounded.h"
#include "kui/core.h"
#include <kos/genwait.h>
#include <kos/irq.h>
#include <kos/thread.h>
#include <kos/cache.h>
#include <dc/memory.h>
#include <string.h>

static struct kui_vmu_read_bounded_diagnostic diagnostic;
const struct kui_vmu_read_bounded_diagnostic *kui_vmu_read_bounded_last(void) {
    return &diagnostic;
}
static int bounded_vmu_read_finish(int result) {
    diagnostic.result=result;return result;
}
static void bounded_vmu_read_frame_status(const maple_frame_t *frame) {
    diagnostic.frame_state=(uint32_t)frame->state;diagnostic.queued=(uint32_t)frame->queued;
}
static int bounded_vmu_read_lock(void *context) {
    ++diagnostic.lock_attempts;return maple_frame_trylock(context)==0;
}
static void bounded_vmu_read_reply(maple_state_t *state,maple_frame_t *frame) {
    (void)state;genwait_wake_all(frame);
}
static void bounded_vmu_read_abandoned(maple_state_t *state,maple_frame_t *frame) {
    (void)state;maple_frame_unlock(frame);
}
static uint32_t bounded_vmu_read_word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
int kui_vmu_read_bounded(maple_device_t *device,uint16_t block,uint8_t out[512]) {
    memset(&diagnostic,0,sizeof(diagnostic));
    diagnostic.block=block;diagnostic.stage=KUI_VMU_READ_ARGUMENT;
    diagnostic.response=-127;diagnostic.expected_function=MAPLE_FUNC_MEMCARD;
    diagnostic.expected_location=(uint32_t)block<<24;
    if(!device || !out || block>=256u || !device->valid ||
       !(device->info.functions&MAPLE_FUNC_MEMCARD) ||
       device->port<0 || device->port>=MAPLE_PORT_COUNT ||
       device->unit<=0 || device->unit>=MAPLE_UNIT_COUNT) return bounded_vmu_read_finish(MAPLE_EINVALID);
    maple_frame_t *frame=&device->frame;
    diagnostic.stage=KUI_VMU_READ_LOCK;
    /* The public SDK's synchronous BREAD waits for frame ownership. Preserve
     * that behavior with a deadline instead of treating an unrelated current
     * owner as a permanent source error. thd_poll retries the atomic lock and
     * yields to the scheduler; it never steals or resets another frame. */
    if(thd_poll(bounded_vmu_read_lock,frame,100u)==0) {
        bounded_vmu_read_frame_status(frame);return bounded_vmu_read_finish(MAPLE_ETIMEOUT);
    }
    diagnostic.stage=KUI_VMU_READ_FRAME;
    /* Public KOS frame locks acquire VACANT -> UNSENT before initialization. */
    if(frame->state!=MAPLE_FRAME_UNSENT) {
        bounded_vmu_read_frame_status(frame);return bounded_vmu_read_finish(MAPLE_EFAIL);
    }
    maple_frame_init(frame);
    /* KOS calloc initializes recv_buf_arr through cached P1, whereas frame
     * initialization and Maple DMA use its uncached P2 alias. Discard any
     * surviving dirty cached lines before the request or DMA can own these
     * bytes; a later writeback could otherwise overwrite a completed reply.
     * The SDK reserves an aligned 1024-byte receive area wholly inside its
     * padded array, so this cannot discard adjacent frame metadata. */
    uintptr_t cached_receive=((uintptr_t)frame->recv_buf&MEM_AREA_CACHE_MASK)|MEM_AREA_P1_BASE;
    dcache_inval_range(cached_receive,1024u);
    uint32_t location=(uint32_t)block<<24;
    frame->send_buf[0]=MAPLE_FUNC_MEMCARD;frame->send_buf[1]=location;
    frame->cmd=MAPLE_COMMAND_BREAD;frame->dst_port=device->port;frame->dst_unit=device->unit;
    frame->length=2;frame->callback=bounded_vmu_read_reply;
    diagnostic.stage=KUI_VMU_READ_QUEUE;
    if(maple_queue_frame(frame)<0) {
        bounded_vmu_read_frame_status(frame);frame->state=MAPLE_FRAME_VACANT;
        return bounded_vmu_read_finish(MAPLE_EFAIL);
    }
    diagnostic.stage=KUI_VMU_READ_WAIT;
    int waited=genwait_wait(frame,"bounded_vmu_read",100u);
    diagnostic.wait_result=waited;
    irq_mask_t saved_irq=irq_disable();
    bounded_vmu_read_frame_status(frame);
    if(frame->state!=MAPLE_FRAME_RESPONDED) {
        /* An in-flight DMA owns these buffers even after the waiter expires.
         * Keep that frame queued and locked; a later response releases it
         * through KOS's normal RESPONDED callback. A hung DMA stays locked.
         * The IRQ mask also prevents racing KOS's queue-removal precheck. */
        if(frame->state==MAPLE_FRAME_SENT) frame->callback=bounded_vmu_read_abandoned;
        else {
            if(frame->queued) (void)maple_queue_remove(frame);
            frame->state=MAPLE_FRAME_VACANT;
        }
        irq_restore(saved_irq);
        return bounded_vmu_read_finish(waited<0?MAPLE_ETIMEOUT:MAPLE_EFAIL);
    }
    irq_restore(saved_irq);
    int result=MAPLE_EFAIL;
    const maple_response_t *response=(const maple_response_t *)(const void *)frame->recv_buf;
    diagnostic.response=response->response;diagnostic.data_len=response->data_len;
    /* Check length BEFORE touching response.data, preventing short-response
     * underflow and overlong copies in the SDK's older block-read helper. */
    diagnostic.stage=KUI_VMU_READ_TYPE;
    if(response->response==MAPLE_RESPONSE_DATATRF) {
        diagnostic.stage=KUI_VMU_READ_LENGTH;
        if(response->data_len==130u) {
            diagnostic.actual_function=bounded_vmu_read_word(response->data);
            diagnostic.actual_location=bounded_vmu_read_word(response->data+4u);
            diagnostic.stage=KUI_VMU_READ_FUNCTION;
            if(diagnostic.actual_function==MAPLE_FUNC_MEMCARD) {
                diagnostic.stage=KUI_VMU_READ_LOCATION;
                if(diagnostic.actual_location==location) {
                    diagnostic.raw_crc_before=kui_crc32(0,response->data+8u,512u);
                    memcpy(out,response->data+8u,512u);
                    diagnostic.raw_crc_after=kui_crc32(0,response->data+8u,512u);
                    diagnostic.copy_crc=kui_crc32(0,out,512u);result=MAPLE_EOK;
                    diagnostic.stage=KUI_VMU_READ_COMPLETE;
                }
            }
        }
    }
    maple_frame_unlock(frame);return bounded_vmu_read_finish(result);
}
