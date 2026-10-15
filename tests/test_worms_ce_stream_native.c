/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual native lease logic, with authored CE boundaries only. */
#define KUI_WINCE_CDDA_WORMS 1
#define main worms_unused_native_fixture_main
#include "test_armada_ce_stream_native.c"
#undef main
static bool worms_read(void *context,uint32_t address,uint32_t *value) {
    if(address==api.owner_process+8u) {*value=model.owner_handle;return true;}
    if(address==api.owner_process+12u) {*value=model.owner_slot;return true;}
    return read32(context,address,value);
}
int main(void) {
    reset();api.owner_process=KUI_ARMADA_CE_LIVE_PROCESS_BASE+5u*KUI_ARMADA_CE_LIVE_PROCESS_STRIDE;
    model.process=api.owner_process;state.api=api;ops.read32=worms_read;
    CHECK(kui_armada_ce_stream_native_write_begin(&state,&ops,DEVICE));
    CHECK(state.consumer_cs==UINT32_MAX && !model.calls && !model.depth);
    CHECK(!kui_armada_ce_stream_native_write_begin(&state,&ops,DEVICE));
    uint32_t ticks;
    CHECK(!kui_armada_ce_stream_native_ticks(&state,&ops,&ticks));
    CHECK(!kui_armada_ce_stream_native_sleep(&state,&ops,1u));
    CHECK(!kui_armada_ce_stream_native_reap(&state,&ops));
    CHECK(!model.calls && state.consumer_cs==UINT32_MAX);
    CHECK(kui_armada_ce_stream_native_write_end(&state,&ops));
    CHECK(!state.consumer_cs && !model.calls && !model.depth);
    CHECK(kui_armada_ce_stream_native_write_begin(&state,&ops,DEVICE));
    model.owner_handle^=4u;CHECK(!kui_armada_ce_stream_native_write_end(&state,&ops));
    CHECK(state.consumer_cs==UINT32_MAX && !model.calls);
    model.owner_handle^=4u;CHECK(kui_armada_ce_stream_native_write_end(&state,&ops));
    puts("Worms native logical lease: no CS calls, nested lease/waits rejected, revocation retains lease until valid release");
    return 0;
}
