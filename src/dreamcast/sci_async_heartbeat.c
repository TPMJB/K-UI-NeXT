/* SPDX-License-Identifier: GPL-3.0-only
 * Observe the pinned KOS TMU0 dispatcher without replacing scheduler behavior.
 * KOS fcfa7d869471591ca1c777543261a7bfea7cb726: kernel/arch/dreamcast/kernel/
 * timer.c:tp_handler and irq.c:irq_handle_exception; kernel/thread/thread.c:
 * thd_timer_hnd changes the exception-return context and rearms its own timer.
 * Forward the original arguments after observation; do nothing after it.
 */
#include "sci_async_heartbeat.h"
#include <stddef.h>
#include <string.h>

#ifdef KUI_SCI_ASYNC_HEARTBEAT_TEST
#include "../../tests/sci_async_heartbeat_test_support.h"
#else
#include <kos/irq.h>
#include <kos/timer.h>
#endif

struct heartbeat {
    irq_cb_t previous;
    bool (*dma_active)(void *);
    void *context;
    bool occupied, poisoned;
    volatile bool observing;
    volatile uint32_t total_ticks, dma_ticks;
    volatile uint64_t last_tick_us, max_gap_us;
    struct kui_sci_async_heartbeat_result result;
};
static struct heartbeat state;

static bool same_handler(irq_cb_t a, irq_cb_t b) {
    return a.hdl==b.hdl && a.data==b.data;
}

static void timer_irq(irq_t source, irq_context_t *context, void *data) {
    struct heartbeat *h=data;
    if(h->observing) {
        uint64_t now=timer_us_gettime64();
        if(h->total_ticks) {
            uint64_t gap=now-h->last_tick_us;
            if(gap>h->max_gap_us) h->max_gap_us=gap;
        }
        h->last_tick_us=now;
        ++h->total_ticks;
        if(h->dma_active(h->context)) ++h->dma_ticks;
    }
    h->previous.hdl(source,context,h->previous.data);
}

bool kui_sci_async_heartbeat_start(bool (*dma_active)(void *), void *context) {
    irq_mask_t mask=irq_disable();
    if(state.occupied || state.poisoned) {
        irq_restore(mask);
        return false;
    }
    memset(&state,0,sizeof(state));
    state.result.restored=true; /* No hook acquired yet. */
    if(!dma_active) {
        irq_restore(mask);
        return false;
    }
    irq_cb_t previous=irq_get_handler(EXC_TMU0_TUNI0);
    if(!previous.hdl || previous.hdl==timer_irq) {
        irq_restore(mask);
        return false;
    }
    state.previous=previous;
    state.dma_active=dma_active;
    state.context=context;
    state.occupied=true;
    state.observing=true;
    state.result.restored=false;
    (void)irq_set_handler(EXC_TMU0_TUNI0,timer_irq,&state);
    irq_cb_t installed=irq_get_handler(EXC_TMU0_TUNI0);
    state.result.installed=same_handler(installed,(irq_cb_t){timer_irq,&state});
    if(!state.result.installed) {
        state.observing=false;
        /* A failed registration may still have escaped through another
         * chained owner. Keep its forwarding state alive, without callbacks. */
        state.result.ownership_lost=!same_handler(installed,previous);
        state.result.restored=same_handler(installed,previous);
        state.poisoned=!state.result.restored;
        state.occupied=false;
    }
    irq_restore(mask);
    return state.result.installed;
}

static void snapshot(struct kui_sci_async_heartbeat_result *out) {
    if(!out) return;
    *out=state.result;
    out->total_ticks=state.total_ticks;
    out->dma_ticks=state.dma_ticks;
    out->max_gap_us=state.max_gap_us;
}

void kui_sci_async_heartbeat_end(struct kui_sci_async_heartbeat_result *out) {
    irq_mask_t mask=irq_disable();
    state.observing=false;
    if(state.occupied) {
        irq_cb_t current=irq_get_handler(EXC_TMU0_TUNI0);
        if(same_handler(current,(irq_cb_t){timer_irq,&state})) {
            (void)irq_set_handler(EXC_TMU0_TUNI0,state.previous.hdl,state.previous.data);
            current=irq_get_handler(EXC_TMU0_TUNI0);
            state.result.restored=same_handler(current,state.previous);
        } else {
            state.result.ownership_lost=true;
        }
        if(!state.result.restored) state.poisoned=true;
        state.occupied=false;
    }
    snapshot(out);
    irq_restore(mask);
}

#ifdef KUI_SCI_ASYNC_HEARTBEAT_TEST
/* Tests create independent interrupt-controller lifetimes. */
void kui_sci_async_heartbeat_test_reset(void) { memset(&state,0,sizeof(state)); }
#endif
