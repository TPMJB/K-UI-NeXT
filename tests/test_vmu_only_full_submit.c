/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual worker entry over the established persistent-card fixture. The
 * uniform Skies RX/timer lifecycle has its own two-selector regression. */
#define KUI_SPLIT_CONTROLLER_TEST_MAIN full_submit_original_split_suite
#include "test_vmu_native_split_controller.c"

_Static_assert(KUI_NATIVE_VMU_ONLY_FULL_SUBMIT==1,"Diagnostic full submit");
_Static_assert(KUI_NATIVE_VMU_ONLY==1,"Independent VMU service");
_Static_assert(WORKER_SPAN==UINT32_C(0x1c000),"Unchanged 112 KiB worker");
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES==WORKER_SPAN,"No reader/cache tail");

static void control_submit(void) {
    memset(&measured_work,0,sizeof(measured_work));
    if(setjmp(terminal)==0) kui_vmu_native_passive_split_submit(&frame);
    else {
        fprintf(stderr,"Full-submit control refused %u/%08x\n",terminal_error,state.runtime_result);
        CHECK(false);
    }
    CHECK(state.pending && state.submitted==state.completed+1u);
    CHECK(controller_pair.pending!=NATIVE_CONTROLLER_CAPTURED);
    /* The control executes retained full-entry ownership checks immediately.
     * The capture route would publish CAPTURED with all six counters zero. */
    CHECK(measured_work.heap && measured_work.hooks);
    CHECK(measured_work.runtime || controller_pair.pending==NATIVE_CONTROLLER_PREPARED);
    CHECK(live_plan.ready && !lease_calls && !sd.bytes && !sd.writes);
}

static void physical_batches_use_retained_full_entry(void) {
    for(unsigned mode=0u;mode<3u;mode++) {
        split_setup();
        for(unsigned round=0u;round<3u;round++) {
            unsigned conditions=mode==2u?1u:0u,allinfo=mode==1u?15u:0u;
            split_batch(conditions,allinfo,round&1u);
            uint32_t source=value_at(0xa05f6c04u);
            uint8_t tx[128];memcpy(tx,memory+source-0x0c000000u,sizeof(tx));
            control_submit();
            if(conditions && round) CHECK(controller_pair.pending==NATIVE_CONTROLLER_PREPARED && !overlay.pending);
            else CHECK(controller_pair.pending==NATIVE_CONTROLLER_IDLE && overlay.pending);
            /* Even the full virtual overlay retains original source bytes. */
            CHECK(!memcmp(tx,memory+source-0x0c000000u,sizeof(tx)));
            split_done(conditions,allinfo,0x01000000u);
            /* Use controller main-unit replies on B-D too. split_done's
             * generic nonzero device function is broader than the retained
             * full path's typed main-unit response policy. */
            for(unsigned port=1u;port<4u;port++) put(rx_at(port*6u)+4u,4u,0x01000000u);
            uint8_t before[4][1024];
            for(unsigned port=0u;port<4u;port++) memcpy(before[port],rx_bytes(port*6u),1024u);
            complete_return();before[0][2]|=2u;
            for(unsigned port=0u;port<4u;port++) CHECK(!memcmp(before[port],rx_bytes(port*6u),1024u));
            CHECK(controller_pair.pending==NATIVE_CONTROLLER_IDLE && !overlay.pending);
            CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes);
        }
    }
}

static void virtual_operations_keep_durable_path(void) {
    split_setup();
    begin_batch(1u,0u,0u,NULL);physical_complete();complete_return();
    CHECK(value_at(rx_at(2u))==UINT32_C(0x1c020005));
    uint8_t next[512];for(unsigned i=0u;i<512u;i++) next[i]=(uint8_t)(i^0x6du);
    stage_block(42u,next);transfer_batch(13u,42u,4u,NULL);read_compare(42u,next);
    CHECK(durable_card.commits==1u && durable_card.reads==1u && release_saw_unpublished);
    CHECK(config.lease.lease_bytes==UINT32_C(0x1c000));
}

int main(void) {
    physical_batches_use_retained_full_entry();virtual_operations_keep_durable_path();
    printf("Full-submit control: %u checks; wrapper enters retained ownership path before DMA, INFO/ALLINFO/controller routes and 112 KiB durable save/read unchanged\n",checks);
    return 0;
}
