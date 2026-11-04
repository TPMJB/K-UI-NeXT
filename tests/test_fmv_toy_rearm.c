/* SPDX-License-Identifier: GPL-3.0-only */
#include "fmv_toy_profile.h"
#define KUI_TOY_PILOT_SCI_TEST 1
#include "../src/loader/toy_pilot_sci.c"
#include <assert.h>
#include <stdio.h>
static bool busy;
bool kui_sci_stream_busy(void) {return busy;}
static void ready(void) {
    memset(&e,0,sizeof(e));
    memset(&kui_toy_pilot_sci_release,0,sizeof(kui_toy_pilot_sci_release));
    kui_toy_pilot_sci_irq_fault=0u;busy=false;
    e.configured=e.startup.retired=1u;
    fmv_rearms=fmv_rearm_refusals=0u;
}
#define BLOCK(change) do {ready();change;assert(!kui_toy_pilot_sci_fmv_rearm()); \
    assert(!e.startup.enabled && !fmv_rearms && fmv_rearm_refusals==1u);} while(0)
int main(void) {
    ready();assert(kui_toy_pilot_sci_fmv_rearm());
    assert(e.startup.enabled && e.startup.retired && fmv_rearms==1u);
    assert(kui_toy_pilot_sci_fmv_rearm() && fmv_rearms==1u);
    kui_toy_pilot_sci_retire();
    assert(!e.startup.enabled);
    assert(kui_toy_pilot_sci_fmv_rearm() && fmv_rearms==2u);
    BLOCK(e.configured=0u);BLOCK(e.startup.retired=0u);
    BLOCK(e.transport_fault=1u);BLOCK(kui_toy_pilot_sci_irq_fault=1u);
    BLOCK(e.active=1u);BLOCK(e.opened=1u);BLOCK(e.in_irq=1u);
    BLOCK(e.hooked=1u);BLOCK(e.audio_held=1u);BLOCK(e.audio_waiting=1u);
    BLOCK(kui_toy_pilot_sci_release.depth=1u);BLOCK(busy=true);
    puts("PASS: actual Toy receiver rearm refuses outstanding DMA, IRQs, card ownership and quarantined faults");
}
