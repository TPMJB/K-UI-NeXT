/* SPDX-License-Identifier: GPL-3.0-only */
#define KUI_FMV_TEST 1
#define KUI_TOY_PILOT_WORKER_TEST 1
#include "../src/loader/toy_pilot_worker.c"
#include <assert.h>
#include <stdio.h>

static void ready(void) {
    memset(&owner,0,sizeof(owner));
    kui_toy_pilot_model_init(&owner.model);
    owner.configured=owner.sdk_ready=owner.stats.driver_verified=1u;
    owner.stats.state=KUI_TOY_PILOT_STOPPED;
    owner.transaction_bank=NONE;
    owner.mailbox.generation=owner.handled_generation=owner.model.generation;
    owner.stats.applied_generation=owner.model.generation;
    assert(kui_toy_pilot_fmv_data_idle());
}
#define BLOCK(change) do {ready();change;assert(!kui_toy_pilot_fmv_data_idle());} while(0)
int main(void) {
    ready();
    BLOCK(owner.configured=0u);
    BLOCK(owner.sdk_ready=0u);
    BLOCK(owner.stats.driver_verified=0u);
    BLOCK(owner.disabled=1u);
    BLOCK(++owner.mailbox.generation);
    BLOCK(++owner.handled_generation);
    BLOCK(++owner.stats.applied_generation);
    BLOCK(++owner.model.generation);
    BLOCK(owner.stop_wait=1u);
    BLOCK(owner.queue_wait=1u);
    BLOCK(owner.transaction_bank=0u);
    BLOCK(owner.ring_queued=1u);
    BLOCK(owner.model.active_bank=0u);
    BLOCK(owner.model.pending_bank=0u);
    for(unsigned bank=0;bank<KUI_TOY_PILOT_BLOCKS;++bank) {
        BLOCK(owner.model.banks[bank].state=KUI_TOY_PILOT_BANK_FILLING);
        BLOCK(owner.model.banks[bank].state=KUI_TOY_PILOT_BANK_READY);
    }
    for(unsigned state=KUI_TOY_PILOT_OFF;state<=KUI_TOY_PILOT_FAULT;++state) {
        ready();owner.stats.state=state;
        assert(kui_toy_pilot_fmv_data_idle()==(state==KUI_TOY_PILOT_STOPPED ||
            state==KUI_TOY_PILOT_PAUSED || state==KUI_TOY_PILOT_EOF));
    }
    puts("PASS: actual Toy audio owner refuses lending while a command, queue, bank or stop is live");
}
