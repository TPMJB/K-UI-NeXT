/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the independently scheduled receiver/cursor fixture. Its unused
 * legacy audio entry points deliberately abort if this DATA-only suite ever
 * reaches them; production mode 2 emits no such helpers. */
int kui_toy_pilot_sci_audio_acquire(void);
void kui_toy_pilot_sci_audio_release(void);
void kui_toy_pilot_sci_audio_cancel(void);
#define main shared_engine_fixture_main
#include "test_toy_pilot_sci.c"
#undef main

int kui_toy_pilot_sci_audio_acquire(void) {assert(!"startup DATA reached audio acquire");return -1;}
void kui_toy_pilot_sci_audio_release(void) {assert(!"startup DATA reached audio release");}
void kui_toy_pilot_sci_audio_cancel(void) {assert(!"startup DATA reached audio cancel");}

static void startup_quiescence_and_retirement(void) {
    setup(false,true);
    CHECK(kui_toy_pilot_sci_startup_active());
    uint32_t token=request(45000u,8u,OUT,false);
    visit(false);CHECK(wire.flight && hw.leased && hw.vbr!=GAME_VBR);
    wire.receiver_delay=100u;wire.ready_tick=hw.ticks+100u;
    advance_clock(50u);CHECK(wire.partial_bytes==64u && !hw.bytes);
    CHECK(kui_toy_pilot_sci_quiesce());
    CHECK(!wire.flight && !hw.leased && hw.vbr==GAME_VBR && hw.iprb==0x5a0fu);
    CHECK(service.pending && service.completed_bytes==0u && !hw.bytes);
    const struct kui_toy_pilot_sci_startup_stats *st=kui_toy_pilot_sci_startup_snapshot();
    CHECK(st->enabled && !st->retired && st->quiesces==1u && !st->quiesce_failed);
    CHECK(!gd(KUI_GD_ABORT,token,0u));
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED);
    /* Quiescing a refused mailbox must permit a later validated request. */
    token=request(45003u,2u,OUT2,false);visit(false);finish();
    compare(45003u,2u,OUT2,false);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED);
    CHECK(kui_toy_pilot_sci_quiesce());kui_toy_pilot_sci_retire();
    CHECK(!kui_toy_pilot_sci_startup_active() && !st->enabled && st->retired);
    unsigned fetches=wire.fetches,bytes=hw.bytes,calls=kui_toy_pilot_sci_snapshot()->calls;
    token=request(45006u,2u,OUT,false);
    for(unsigned i=0;i<10u;i++) {
        CHECK(kui_toy_pilot_sci_pump(&service)==KUI_TOY_SCI_OK);
        CHECK(kui_toy_pilot_sci_service(&service)==KUI_TOY_SCI_OK);
        CHECK(kui_toy_pilot_sci_irq()==1u);
    }
    CHECK(fetches==wire.fetches && bytes==hw.bytes && calls==kui_toy_pilot_sci_snapshot()->calls);
    CHECK(!wire.flight && !hw.leased && service.pending && !service.completed_bytes);
    CHECK(!gd(KUI_GD_RESET,0u,0u));
    CHECK(!kui_toy_pilot_sci_startup_active());cleanup();

    setup(false,false);(void)request(45000u,2u,OUT,false);visit(false);
    wire.stop_error=KUI_SCI_STREAM_RESET;
    CHECK(!kui_toy_pilot_sci_quiesce());
    st=kui_toy_pilot_sci_startup_snapshot();
    CHECK(st->enabled && !st->retired && st->quiesces==1u && st->quiesce_failed==1u);
    CHECK(!wire.flight && !hw.leased && hw.vbr==GAME_VBR && hw.iprb==0x5a0fu);
    wire.stop_error=KUI_SCI_STREAM_OK;cleanup();
}
static void startup_transfer_failures(void) {
    static const enum kui_sci_stream_result errors[]={
        KUI_SCI_STREAM_BUSY,KUI_SCI_STREAM_COMMAND,KUI_SCI_STREAM_TOKEN,KUI_SCI_STREAM_RESET
    };
    for(unsigned i=0;i<sizeof(errors)/sizeof(*errors);i++) {
        setup(false,false);uint32_t token=request(45000u,2u,OUT,false);
        wire.fetch_error=errors[i];
        for(unsigned n=0;n<20u && service.pending;n++) {arrive();visit(false);}
        CHECK(!service.pending && service.error && !hw.bytes && !wire.flight && !hw.leased);
        CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED);cleanup();
    }
    setup(false,false);uint32_t token=request(45000u,2u,OUT,false);
    visit(false);wire.take_error=KUI_SCI_STREAM_CRC;
    for(unsigned n=0;n<20u && service.pending;n++) {arrive();visit(false);}
    CHECK(!service.pending && service.error==KUI_GD_ERROR_IO && !hw.bytes && !hw.leased);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED);cleanup();
}
int main(void) {
    nonblocking_and_complete();token_search_yields();cancellation_and_epochs();
    destination_failure();startup_transfer_failures();foreground_progress_and_limits();
    foreground_quota_credits_background();startup_quiescence_and_retirement();
    printf("Startup DATA SCI engine: %u checks passed; private partial DMA fenced, ownership restored and retirement permanent\n",checks);
    return 0;
}
