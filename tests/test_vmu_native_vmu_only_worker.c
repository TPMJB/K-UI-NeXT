/* SPDX-License-Identifier: GPL-3.0-only */
/* Run the production worker, Maple split path and durable SD model with the
 * exact VMU-only allocation. The separate heap test executes all reservation
 * transitions at the same size; this fixture checks its runtime boundaries. */
#define KUI_SPLIT_CONTROLLER_TEST_MAIN vmu_only_split_regression
#include "test_vmu_native_split_controller.c"

_Static_assert(KUI_NATIVE_VMU_ONLY==1,"Run the independent VMU worker");
#if KUI_NATIVE_READAHEAD || KUI_NATIVE_DUAL_READERS || KUI_VMU_NATIVE_BACKGROUND
#error "No background reader machinery"
#endif
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES==UINT32_C(0x1c000),
    "Measured VMU-only reservation");
_Static_assert(WORKER_SPAN==KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES,
    "No unused combined-reader tail");

static void exact_lease_arm_refusals(void) {
    for(unsigned mutation=0u;mutation<8u;mutation++) {
        split_prepare();
        CHECK(input.lease.lease_bytes==WORKER_SPAN &&
            input.runtime.lease_bytes==WORKER_SPAN &&
            input.runtime.required_lease_bytes==WORKER_SPAN);
        switch(mutation) {
        case 0u:input.lease.lease_bytes-=32u;break;
        case 1u:input.lease.lease_bytes+=32u;break;
        case 2u:input.runtime.lease_bytes-=32u;break;
        case 3u:input.runtime.lease_bytes+=32u;break;
        case 4u:input.runtime.required_lease_bytes-=32u;break;
        case 5u:input.runtime.required_lease_bytes+=32u;break;
        case 6u:live_sp=STACK_HIGH;break;
        default:bad_guard=1u;break;
        }
        CHECK(kui_vmu_native_passive_arm(&input,NULL)<0 && !state.armed);
        CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes);
    }
}

static void retained_lease_cannot_be_freed(void) {
    const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    const uint32_t offsets[]={0u,32u,UINT32_C(0x1bfe0)};
    for(volatile unsigned a=0u;a<3u;a++) for(volatile unsigned i=0u;i<3u;i++) {
        split_setup();
        frame.r4=(physical(config.lease.payload)+offsets[i])|aliases[a];
        if(setjmp(terminal)==0) {
            kui_vmu_native_readonly_release_check(&frame);CHECK(false);
        }
        CHECK(terminal_error==0x30u && !lease_calls && !sd.bytes && !sd.writes);
    }
}

static void saves_check_ownership_at_both_boundaries(void) {
    uint8_t data[512];for(unsigned n=0u;n<sizeof(data);n++) data[n]=(uint8_t)(n^0x63u);
    for(unsigned phase=0u;phase<2u;phase++) for(unsigned witness=1u;witness<=4u;witness++) {
        split_setup();stage_block(42u,data);begin_batch(13u,42u,4u,NULL);physical_complete();
        uint8_t before[1024];memcpy(before,rx_bytes(2),sizeof(before));
        if(!phase) corrupt_before_loan=witness;else corrupt_after_loan=witness;
        reject_complete();
        CHECK(!memcmp(before,rx_bytes(2),sizeof(before)) && !durable_card.commits);
        if(!phase) CHECK(!lease_calls && !sd.bytes && !sd.writes);
        else CHECK(lease_calls==1u && release_saw_unpublished);
    }
}

static void bottom_reservation_runs_the_durable_path(void) {
    uint8_t data[512];for(unsigned n=0u;n<sizeof(data);n++) data[n]=(uint8_t)(n^0xb5u);
    split_prepare();
    /* Match the production bottom placement after its original allocator
     * calls: the worker header begins the arena and one upper free block
     * starts immediately after the exact worker span. */
    input.lease.begin=input.lease.header;
    uint32_t upper=input.lease.payload+input.lease.lease_bytes;
    const struct kui_vmu_native_heap *h=&input.lease.owner;
    put(h->begin_cell,4u,input.lease.begin);put(h->begin_copy_cell,4u,input.lease.begin);
    put(h->sentinel,4u,upper);put(h->cursor_cell,4u,h->sentinel);
    put(upper,4u,h->sentinel);put(upper+4u,4u,input.lease.remaining);
    CHECK(kui_vmu_native_heap_bottom_ready(&input.lease,read_heap,NULL,1u));
    arm();install_hooks();
    CHECK(config.lease.header==config.lease.begin &&
        config.lease.payload==config.lease.begin+32u &&
        config.lease.lease_bytes==WORKER_SPAN);
    stage_block(42u,data);transfer_batch(13u,42u,4u,NULL);read_compare(42u,data);
    CHECK(durable_card.commits==1u && durable_card.reads==1u && release_saw_unpublished);
    CHECK(kui_vmu_native_heap_bottom_ready(&config.lease,read_heap,NULL,1u));
    protect_media();model_reopen();
    struct kui_vmu_backing backing={0};struct kui_vmu_store reopened={0};
    CHECK(!kui_vmu_backing_init(&backing,&model_bus,&backing_config));
    struct kui_vmu_store_io io=kui_vmu_backing_io(&backing);
    CHECK(!kui_vmu_store_open(&reopened,&io,&store_config));
    CHECK(!kui_vmu_store_checkpoint(&reopened));
    CHECK(!memcmp(sd.media[image_lba(42u)],data,sizeof(data)));protect_media();
}

int main(void) {
    int result=vmu_only_split_regression();CHECK(!result);
    exact_lease_arm_refusals();retained_lease_cannot_be_freed();
    saves_check_ownership_at_both_boundaries();bottom_reservation_runs_the_durable_path();
    printf("Independent VMU worker: %u checks; exact %u-byte lease, physical polling, durable save/reopen, no false ACK\n",
        checks,(unsigned)WORKER_SPAN);
    return 0;
}
