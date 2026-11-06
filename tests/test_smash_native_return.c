/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual worker, ownership witnesses, overlay and durable protocol. The host
 * fixture supplies RAM/MMIO only; the retirement C body is production code. */
#define KUI_SPLIT_CONTROLLER_TEST_MAIN unused_split_regression
#include "test_vmu_native_split_controller.c"
#include "kui/smash_native_return.h"

static void return_setup(void) {split_setup();live_sr=UINT32_C(0x700000f0);}
static void unchanged_io(unsigned bytes,unsigned writes,unsigned leases) {
    CHECK(sd.bytes==bytes && sd.writes==writes && lease_calls==leases);
    CHECK(!sd.owned && !sd.locked && !durable_card.store.io.context);
}
static void busy_refusals(void) {
    for(unsigned which=0;which<8u;which++) {
        return_setup();
        if(which==0u)state.pending=1u;
        else if(which==1u)++state.submitted;
        else if(which==2u)overlay.pending=1u;
        else if(which==3u)durable_card.handling=1u;
        else if(which==4u)durable_card.inflight=1u;
        else if(which==5u)durable_card.protocol.active=1u;
        else if(which==6u)controller_pair.pending=1u;
        else put(UINT32_C(0xa05f6c18),4u,1u);
        unsigned bytes=sd.bytes,writes=sd.writes,leases=lease_calls;
        struct kui_vmu_durable saved=durable_card;
        CHECK(kui_smash_native_return_retire()==0);
        CHECK(state.armed && overlay.admitted && !memcmp(&saved,&durable_card,sizeof(saved)));
        unchanged_io(bytes,writes,leases);
    }
}
static void invalid_refusals(void) {
    for(unsigned which=0;which<13u;which++) {
        return_setup();
        if(which==0u)live_sr&=~UINT32_C(0x10);
        else if(which==1u)live_sr&=~UINT32_C(0x10000000);
        else if(which==2u)durable_card.ready=0u;
        else if(which==3u)durable_card.self=NULL;
        else if(which==4u)durable_card.faulted=1u;
        else if(which==5u)state.error=1u;
        else if(which==6u)overlay.admitted=0u;
        else if(which==7u)++kui_vmu_native_passive_consumer;
        else if(which==8u)put(input.lease.header+4u,4u,input.lease.units+1u);
        else if(which==9u)put(input.driver.submit,4u,0u);
        else if(which==10u)bad_guard=1u;
        else if(which==11u)live_sp=UINT32_C(0x8c00b000);
        else fail_read=bus_reads+1u;
        unsigned bytes=sd.bytes,writes=sd.writes,leases=lease_calls;
        bool advertised=overlay.admitted;
        CHECK(kui_smash_native_return_retire()==-1);
        CHECK(state.armed && overlay.admitted==advertised);
        unchanged_io(bytes,writes,leases);
    }
}
static unsigned dma_samples;
static bool dma_starts_during_proof(uint32_t address,unsigned width,uint32_t *value) {
    if(address!=UINT32_C(0xa05f6c18))return false;
    CHECK(width==4u);*value=++dma_samples>1u?1u:0u;return true;
}
static void once_only_and_racing_dma(void) {
    return_setup();unsigned bytes=sd.bytes,writes=sd.writes,leases=lease_calls;
    struct kui_vmu_durable saved=durable_card;
    CHECK(kui_smash_native_return_retire()==1);
    CHECK(!state.armed && !overlay.admitted && !memcmp(&saved,&durable_card,sizeof(saved)));
    CHECK(kui_smash_native_return_retire()==-1);unchanged_io(bytes,writes,leases);
    return_setup();dma_samples=0u;read_override=dma_starts_during_proof;
    CHECK(kui_smash_native_return_retire()==0 && dma_samples==2u);
    CHECK(state.armed && overlay.admitted);unchanged_io(bytes,writes,leases);
}
static void preflight_does_not_borrow_a_busy_private_stack(void) {
    for(unsigned busy=0;busy<2u;busy++) {
        return_setup();live_sp=UINT32_C(0x8c00b000); /* The low caller's stack. */
        if(busy)durable_card.inflight=1u;else durable_card.handling=1u;
        uint8_t before[4096];uint8_t *private_stack=memory+physical(input.lease.payload)-UINT32_C(0x0c000000)+WORKER_SPAN-4096u;
        memset(private_stack,0xacu,sizeof(before));memcpy(before,private_stack,sizeof(before));
        unsigned reads=bus_reads;
        CHECK(kui_smash_native_return_preflight()==0 && kui_smash_native_return_retire()==0);
        CHECK(bus_reads==reads && !memcmp(before,private_stack,sizeof(before)));
        CHECK(state.armed && overlay.admitted && !sd.bytes && !sd.writes && !lease_calls);
    }
    return_setup();live_sp=UINT32_C(0x8c00b000);unsigned reads=bus_reads;
    CHECK(kui_smash_native_return_preflight()==1 && bus_reads==reads && state.armed && overlay.admitted);
    CHECK(kui_smash_native_return_retire()==-1 && state.armed && overlay.admitted);
}
static void actual_staged_write_then_durable_retirement(void) {
    uint8_t next[512];memset(next,0x4du,sizeof(next));
    return_setup();live_sr=0xf0u;
    stage_block(42u,next);
    CHECK(durable_card.protocol.active && durable_card.protocol.phases==15u && !durable_card.commits);
    live_sr=UINT32_C(0x700000f0);
    CHECK(kui_smash_native_return_retire()==0 && state.armed && overlay.admitted);
    CHECK(durable_card.protocol.active && !durable_card.commits && !sd.writes);
    live_sr=0xf0u;transfer_batch(13u,42u,4u,NULL);
    CHECK(durable_card.commits==1u && !durable_card.protocol.active && release_saw_unpublished);
    CHECK(value_at(rx_at(VIRTUAL))==((VIRTUAL<<16)|7u));
    CHECK(!memcmp(sd.media[image_lba(42u)],next,sizeof(next)));
    unsigned bytes=sd.bytes,writes=sd.writes,leases=lease_calls;
    live_sr=UINT32_C(0x700000f0);
    CHECK(kui_smash_native_return_retire()==1 && !state.armed && !overlay.admitted);
    CHECK(durable_card.ready && durable_card.self==&durable_card && durable_card.commits==1u);
    unchanged_io(bytes,writes,leases);
    /* The old worker grants no submission/publication after retirement.
     * A real caller must cold-restart immediately; this is a negative test. */
    unsigned advertisements=overlay.stats.advertisements;
    if(setjmp(terminal)==0) {kui_vmu_native_passive_submit(&frame);CHECK(false);}
    CHECK(!state.armed && !overlay.admitted && overlay.stats.advertisements==advertisements);
    unchanged_io(bytes,writes,leases);protect_media();
}
int main(void) {
    CHECK(VIRTUAL==1u && WORKER_SPAN==0x12000u);
    busy_refusals();invalid_refusals();once_only_and_racing_dma();
    preflight_does_not_borrow_a_busy_private_stack();
    actual_staged_write_then_durable_retirement();
    printf("Smash worker retirement: %u checks, busy/save/ownership guards, one-shot authority removal, no I/O/reset, durable ACK retained\n",checks);
    return 0;
}
