/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the production high audio worker entrypoints while SCI belongs
 * to VMU storage, including calls which bypass the resident GD entry. */
#define main baseline_transport_main
#include "test_armada_ce_stream_transport.c"
#undef main

static void test_no_audio_bus_callback_inside_loan(void) {
    setup();
    CHECK(!kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_STORAGE_RELEASE,0u));
    CHECK(!kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_STORAGE_CLAIM,1u));
    CHECK(kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_STORAGE_CLAIM,0u));
    unsigned char before[sizeof(wire)];memcpy(before,&wire,sizeof(wire));
    for(unsigned n=0;n<8u;++n) {
        CHECK(kui_armada_ce_stream_transport_submit(1u,45002u,1u,(uintptr_t)output.bytes,
            sizeof(output.bytes))==KUI_ARMADA_CE_STREAM_TRANSPORT_BUSY);
        CHECK(kui_armada_ce_stream_transport_pump()==KUI_ARMADA_CE_STREAM_TRANSPORT_BUSY);
        CHECK(kui_armada_ce_stream_transport_pump_worker()==KUI_ARMADA_CE_STREAM_TRANSPORT_BUSY);
        CHECK(!kui_armada_ce_stream_transport_worker_can_pump());
        CHECK(!kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_DATA_ENTER,0u));
        CHECK(!kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_DATA_PREFETCH,0u));
        CHECK(kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_IRQ,0u));
        CHECK(kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_IDLE,0u));
        CHECK(!kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_ATTACH,(uintptr_t)&bus));
        CHECK(!kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_STORAGE_CLAIM,0u));
        CHECK(!kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_STORAGE_RELEASE,1u));
        CHECK(!memcmp(before,&wire,sizeof(wire)));
    }
    CHECK(kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_STORAGE_RELEASE,0u));
    CHECK(!memcmp(before,&wire,sizeof(wire)));
    reference_read(45002u,1u);submit(1u,45002u,1u);finish_audio(1u);
    CHECK(snap(1u).status==KUI_ARMADA_CE_STREAM_TRANSPORT_DONE);
    CHECK(!memcmp(output.bytes,expected,2352u));intact();
}
static void test_pending_audio_and_data_exclude_new_loan(void) {
    setup();submit(1u,45002u,1u);
    CHECK(!kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_STORAGE_CLAIM,0u));
    pump_one(1u,false);
    CHECK(!kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_STORAGE_CLAIM,0u));
    finish_audio(1u);
    CHECK(kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_STORAGE_CLAIM,0u));
    CHECK(kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_STORAGE_RELEASE,0u));
    setup();data_active(10u);
    CHECK(!kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_STORAGE_CLAIM,0u));
    region.engine.active=0u;region.shared.service.pending=0u;
    wire.busy=wire.inflight_dma=1u;
    CHECK(!kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_STORAGE_CLAIM,0u));
    wire.busy=wire.inflight_dma=0u;
    CHECK(kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_STORAGE_CLAIM,0u));
    CHECK(kui_armada_ce_stream_transport_event(KUI_ARMADA_CE_STREAM_BUS_STORAGE_RELEASE,0u));
}
int main(void) {
    baseline_transport_main();
    test_no_audio_bus_callback_inside_loan();
    test_pending_audio_and_data_exclude_new_loan();
    printf("CE VMU audio storage exclusion: %u checks; worker/IRQ/DATA pumps and submissions cannot touch SCI inside a loan\n",checks);
    return 0;
}
