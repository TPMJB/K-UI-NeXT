/* SPDX-License-Identifier: GPL-3.0-only */
#include "../src/dreamcast/sci_async_heartbeat.h"
#include "sci_async_heartbeat_test_support.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static irq_cb_t handler;
static unsigned masked, set_calls, sampler_calls, forwarded;
static unsigned set_failure, foreign_on_set;
static uint64_t now;
static bool dma;
static unsigned sampler_context, old_context;
static irq_context_t *expected_context;

irq_mask_t irq_disable(void) { unsigned old=masked; masked=1; return old; }
void irq_restore(irq_mask_t mask) { assert(masked); masked=mask; }
irq_cb_t irq_get_handler(irq_t source) {
    assert(source==EXC_TMU0_TUNI0 && masked); return handler;
}
static void foreign_irq(irq_t source,irq_context_t *context,void *data) {
    (void)source; (void)context; (void)data;
}
int irq_set_handler(irq_t source,irq_hdl_t callback,void *data) {
    assert(source==EXC_TMU0_TUNI0 && masked); ++set_calls;
    if(set_failure) return -1;
    if(foreign_on_set) handler=(irq_cb_t){foreign_irq,NULL};
    else handler=(irq_cb_t){callback,data};
    return 0;
}
uint64_t timer_us_gettime64(void) { assert(masked); return now; }
static bool dma_active(void *context) {
    assert(masked && context==&sampler_context); ++sampler_calls; return dma;
}
static void old_irq(irq_t source,irq_context_t *context,void *data) {
    assert(masked && source==EXC_TMU0_TUNI0);
    assert(context==expected_context && data==&old_context);
    ++forwarded;
    /* A scheduler handler may select the exception-return context. The
     * observer must neither replace nor undo the original handler's choice. */
    context->selected_context=forwarded;
}
static void reset(void) {
    kui_sci_async_heartbeat_test_reset();
    handler=(irq_cb_t){old_irq,&old_context};
    masked=set_calls=sampler_calls=forwarded=set_failure=foreign_on_set=0;
    now=0; dma=false; expected_context=NULL;
}
static void deliver(irq_cb_t callback,uint64_t at,bool active) {
    irq_context_t context={0};
    expected_context=&context; now=at; dma=active;
    irq_mask_t mask=irq_disable();
    callback.hdl(EXC_TMU0_TUNI0,&context,callback.data);
    irq_restore(mask);
    assert(context.selected_context==forwarded && !masked);
}
static void counts_and_exact_restore(void) {
    reset();
    assert(kui_sci_async_heartbeat_start(dma_active,&sampler_context));
    assert(!masked && set_calls==1);
    irq_cb_t wrapper=handler;
    assert(!kui_sci_async_heartbeat_start(dma_active,&sampler_context));
    assert(set_calls==1 && handler.hdl==wrapper.hdl && handler.data==wrapper.data);
    deliver(wrapper,100,false);
    deliver(wrapper,1100,true);
    deliver(wrapper,3600,true);
    deliver(wrapper,4600,false);
    struct kui_sci_async_heartbeat_result result;
    kui_sci_async_heartbeat_end(&result);
    assert(!masked && result.installed && result.restored && !result.ownership_lost);
    assert(result.total_ticks==4 && result.dma_ticks==2 && result.max_gap_us==2500);
    assert(forwarded==4 && sampler_calls==4 && set_calls==2);
    assert(handler.hdl==old_irq && handler.data==&old_context);
    /* end is idempotent and copies a stable snapshot. */
    kui_sci_async_heartbeat_end(&result);
    assert(set_calls==2 && result.total_ticks==4);
    assert(kui_sci_async_heartbeat_start(dma_active,&sampler_context));
    kui_sci_async_heartbeat_end(&result);
    assert(result.total_ticks==0 && result.dma_ticks==0 && result.max_gap_us==0);
}
static void missing_handler_or_sampler(void) {
    reset();
    struct kui_sci_async_heartbeat_result result;
    assert(!kui_sci_async_heartbeat_start(NULL,&sampler_context));
    kui_sci_async_heartbeat_end(&result);
    assert(!result.installed && result.restored && set_calls==0);
    handler=(irq_cb_t){NULL,NULL};
    assert(!kui_sci_async_heartbeat_start(dma_active,&sampler_context));
    kui_sci_async_heartbeat_end(&result);
    assert(!result.installed && result.restored && set_calls==0);
}
static void ownership_change_is_not_overwritten(void) {
    reset();
    assert(kui_sci_async_heartbeat_start(dma_active,&sampler_context));
    irq_cb_t wrapper=handler;
    deliver(wrapper,100,true);
    handler=(irq_cb_t){foreign_irq,&old_context};
    struct kui_sci_async_heartbeat_result result;
    kui_sci_async_heartbeat_end(&result);
    assert(result.installed && !result.restored && result.ownership_lost);
    assert(handler.hdl==foreign_irq && handler.data==&old_context && set_calls==1);
    /* A new owner might retain our callback in its chain. It forwards without
     * accessing the now-expired sampler/context or counting extra ticks. */
    deliver(wrapper,1000,true);
    assert(sampler_calls==1 && forwarded==2);
    assert(!kui_sci_async_heartbeat_start(dma_active,&sampler_context));
    kui_sci_async_heartbeat_end(&result);
    assert(result.total_ticks==1 && result.dma_ticks==1 && set_calls==1);
}
static void failed_restore_retains_safe_forwarding(void) {
    reset();
    assert(kui_sci_async_heartbeat_start(dma_active,&sampler_context));
    irq_cb_t wrapper=handler;
    set_failure=1;
    struct kui_sci_async_heartbeat_result result;
    kui_sci_async_heartbeat_end(&result);
    assert(result.installed && !result.restored && !result.ownership_lost);
    assert(handler.hdl==wrapper.hdl && handler.data==wrapper.data);
    deliver(wrapper,100,true);
    assert(sampler_calls==0 && forwarded==1);
    assert(!kui_sci_async_heartbeat_start(dma_active,&sampler_context));
}
static void failed_install_verified_without_overwrite(void) {
    reset(); set_failure=1;
    assert(!kui_sci_async_heartbeat_start(dma_active,&sampler_context));
    struct kui_sci_async_heartbeat_result result;
    kui_sci_async_heartbeat_end(&result);
    assert(!result.installed && result.restored && !result.ownership_lost);
    assert(set_calls==1 && handler.hdl==old_irq && handler.data==&old_context);
    set_failure=0;
    assert(kui_sci_async_heartbeat_start(dma_active,&sampler_context));
    kui_sci_async_heartbeat_end(NULL);
    reset(); foreign_on_set=1;
    assert(!kui_sci_async_heartbeat_start(dma_active,&sampler_context));
    kui_sci_async_heartbeat_end(&result);
    assert(!result.installed && !result.restored && result.ownership_lost);
    assert(set_calls==1 && handler.hdl==foreign_irq);
    assert(!kui_sci_async_heartbeat_start(dma_active,&sampler_context));
}
int main(void) {
    counts_and_exact_restore();
    missing_handler_or_sampler();
    ownership_change_is_not_overwritten();
    failed_restore_retains_safe_forwarding();
    failed_install_verified_without_overwrite();
    puts("SCI async heartbeat tests passed");
    return 0;
}
