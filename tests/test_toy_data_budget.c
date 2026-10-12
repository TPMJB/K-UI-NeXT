/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual DATA engine and cursor, with an independently timed card fixture.
 * No legacy audio entry is callable by the observed startup profiles. */
int kui_toy_pilot_sci_audio_acquire(void);
void kui_toy_pilot_sci_audio_release(void);
void kui_toy_pilot_sci_audio_cancel(void);
#define main original_shared_engine_fixture_main
#define kui_sci_stream_poll original_budget_fixture_poll
#include "test_toy_pilot_sci.c"
#undef kui_sci_stream_poll
#undef main
static bool budget_revoke_credit;
enum kui_sci_stream_result kui_sci_stream_poll(bool interrupt) {
    if(budget_revoke_credit) {
        budget_revoke_credit=false;service.completed_bytes=0u;++service.token;
    }
    return original_budget_fixture_poll(interrupt);
}

int kui_toy_pilot_sci_audio_acquire(void) {assert(!"budget reached audio acquire");return -1;}
void kui_toy_pilot_sci_audio_release(void) {assert(!"budget reached audio release");}
void kui_toy_pilot_sci_audio_cancel(void) {assert(!"budget reached audio cancel");}

#if !KUI_TOY_PILOT_DATA_BUDGET_PROFILE
#error This fixture requires an observed budget profile
#endif
static const struct kui_toy_pilot_sci_budget_stats *budget(void) {
    return kui_toy_pilot_sci_budget_snapshot();
}
static uint32_t exits(const struct kui_toy_pilot_sci_budget_stats *b) {
    return b->block_exits+b->time_exits+b->token_exits+b->step_exits+
        b->finished_or_inactive_exits;
}
static int measured_service(void) {
    struct kui_toy_pilot_sci_budget_stats prior=*budget();
    uint32_t bytes=service.completed_bytes,blocks=wire.stats.blocks;
    int result=kui_toy_pilot_sci_service(&service);
    const struct kui_toy_pilot_sci_budget_stats *b=budget();
    CHECK(b->service_calls==prior.service_calls+1u);
    CHECK(exits(b)==exits(&prior)+1u);
    CHECK(wire.stats.blocks-blocks<=KUI_TOY_SCI_SERVICE_BLOCKS);
    CHECK(b->service_verified_blocks-prior.service_verified_blocks==wire.stats.blocks-blocks);
    CHECK(b->credited_bytes-prior.credited_bytes==service.completed_bytes-bytes);
    CHECK(b->zero_credit_calls-prior.zero_credit_calls==(service.completed_bytes==bytes));
    CHECK(!wire.waits);
    return result;
}
static void descriptor_and_no_work(void) {
    setup(true,false);
    const struct kui_toy_pilot_sci_budget_stats *b=budget();
    CHECK(b->profile==KUI_TOY_PILOT_DATA_BUDGET_PROFILE &&
        b->block_limit==KUI_TOY_SCI_SERVICE_BLOCKS && b->tick_limit==KUI_TOY_SCI_SERVICE_TICKS);
    CHECK(!b->service_calls && !exits(b) && !b->credited_bytes);
    CHECK(measured_service()==KUI_TOY_SCI_OK);
    CHECK(b->finished_or_inactive_exits==1u && b->zero_credit_calls==1u);
    CHECK(!wire.opens && !wire.fetches && !hw.bytes);cleanup();
}
static void block_limit_and_credit(void) {
    for(unsigned cooked=0u;cooked<2u;++cooked) {
        setup(cooked!=0u,true);uint32_t token=request(45000u,16u,OUT,false);
        wire.receiver_delay=1u;visit(false);arrive();
        CHECK(measured_service()==KUI_TOY_SCI_PENDING);
        const struct kui_toy_pilot_sci_budget_stats *b=budget();
        CHECK(b->block_exits==1u && !b->time_exits && !b->token_exits);
        CHECK(b->service_verified_blocks==KUI_TOY_SCI_SERVICE_BLOCKS);
        uint32_t credit=cooked?KUI_TOY_SCI_SERVICE_BLOCKS/4u*2048u:
            (KUI_TOY_SCI_SERVICE_BLOCKS==4u?0u:4096u);
        CHECK(service.completed_bytes==credit && b->credited_bytes==credit);
        CHECK(b->zero_credit_calls==!credit);
        unsigned iterations=1u;
        while(service.pending && iterations<100u) {arrive();(void)measured_service();++iterations;}
        CHECK(!service.pending && service.status==KUI_GD_COMPLETED && !service.error);
        compare(45000u,16u,OUT,false);
        CHECK(b->credited_bytes==16u*2048u);
        CHECK(b->service_verified_blocks==kui_toy_pilot_sci_snapshot()->call_blocks);
        CHECK(b->finished_or_inactive_exits==1u);
        CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED);cleanup();
    }
}
static void time_and_step_limits(void) {
    setup(true,false);(void)request(45000u,8u,OUT,false);visit(false);
    hw.tick_step=10u;uint32_t start=hw.ticks;unsigned polls=wire.polls;
    CHECK(measured_service()==KUI_TOY_SCI_PENDING);
    CHECK(budget()->time_exits==1u && !budget()->block_exits && !budget()->step_exits);
    CHECK(hw.ticks-start>=KUI_TOY_SCI_SERVICE_TICKS && hw.ticks-start<=KUI_TOY_SCI_SERVICE_TICKS+20u);
    CHECK(wire.polls-polls<=KUI_TOY_SCI_SERVICE_TICKS/10u && !hw.bytes);cleanup();

    setup(true,false);(void)request(45000u,8u,OUT,false);visit(false);
    hw.tick_step=0u;polls=wire.polls;
    CHECK(measured_service()==KUI_TOY_SCI_PENDING);
    CHECK(wire.polls-polls==1024u && budget()->step_exits==1u && !budget()->time_exits);
    CHECK(!hw.bytes && service.pending);cleanup();

    setup(true,false);(void)request(45000u,8u,OUT,false);visit(false);
    hw.ticks=UINT32_MAX-100u;hw.tick_step=10u;
    CHECK(measured_service()==KUI_TOY_SCI_PENDING);
    CHECK(budget()->time_exits==1u && !budget()->step_exits && !hw.bytes);cleanup();
}
static void token_completion_and_failure(void) {
    setup(true,false);(void)request(45000u,3u,OUT,false);wire.token_pending=true;
    unsigned fetches=wire.fetches;
    CHECK(measured_service()==KUI_TOY_SCI_PENDING);
    CHECK(budget()->token_exits==1u && !budget()->time_exits && !budget()->block_exits);
    CHECK(wire.fetches==fetches+1u && wire.token_bytes==256u && !hw.bytes);
    CHECK(measured_service()==KUI_TOY_SCI_PENDING && wire.token_bytes==512u);
    CHECK(budget()->token_exits==2u);cleanup();

    setup(true,false);uint32_t token=request(45000u,1u,OUT,false);
    wire.receiver_delay=1u;visit(false);arrive();
    CHECK(measured_service()==KUI_TOY_SCI_OK);
    CHECK(!budget()->block_exits && budget()->finished_or_inactive_exits==1u);
    CHECK(budget()->credited_bytes==2048u && !budget()->zero_credit_calls);
    compare(45000u,1u,OUT,false);CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED);cleanup();

    setup(true,false);token=request(45000u,1u,OUT,false);visit(false);
    wire.poll_error=KUI_SCI_STREAM_BUSY;
    CHECK(measured_service()==KUI_TOY_SCI_FAULT);
    CHECK(budget()->finished_or_inactive_exits==1u && !budget()->credited_bytes);
    CHECK(!service.pending && service.error && !wire.flight && !hw.leased);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED);cleanup();
}
static void irq_credit_and_retirement(void) {
    setup(true,false);uint32_t token=request(45000u,16u,OUT,false);visit(false);
    arrive();visit(true);CHECK(kui_toy_pilot_sci_snapshot()->irq_blocks==1u);
    wire.receiver_delay=1u;arrive();CHECK(measured_service()==KUI_TOY_SCI_PENDING);
    CHECK(budget()->block_exits==1u && budget()->service_verified_blocks==KUI_TOY_SCI_SERVICE_BLOCKS-1u);
    CHECK(kui_toy_pilot_sci_snapshot()->irq_blocks==1u);cleanup();
    CHECK(gd(KUI_GD_ABORT,token,0u)==0);(void)gd(KUI_GD_CHECK,token,STATUS);
    CHECK(kui_toy_pilot_sci_quiesce());kui_toy_pilot_sci_retire();
    struct kui_toy_pilot_sci_budget_stats frozen=*budget();
    token=request(45000u,2u,OUT,false);
    CHECK(kui_toy_pilot_sci_service(&service)==KUI_TOY_SCI_OK);
    CHECK(kui_toy_pilot_sci_pump(&service)==KUI_TOY_SCI_OK && kui_toy_pilot_sci_irq()==1u);
    CHECK(!memcmp(&frozen,budget(),sizeof(frozen)) && !service.completed_bytes && !hw.leased);
    CHECK(gd(KUI_GD_ABORT,token,0u)==0);cleanup();
}
static unsigned independent_work_model(void) {
    setup(true,false);uint32_t token=request(45000u,8u,OUT,false);
    wire.receiver_delay=1u;wire.poll_tick_cost=260u;visit(false);arrive();
    unsigned calls=0u;
    while(service.pending && calls<100u) {
        advance_clock(100u);(void)measured_service();++calls;
        int32_t state=gd(KUI_GD_CHECK,token,STATUS);
        CHECK(state==(service.pending?KUI_GD_PROCESSING:KUI_GD_COMPLETED));
    }
    CHECK(!service.pending && !service.error && calls<100u && !kui_toy_pilot_sci_snapshot()->irq_blocks);
    compare(45000u,8u,OUT,false);
    if(KUI_TOY_PILOT_DATA_BUDGET_PROFILE==2) CHECK(budget()->time_exits && !budget()->block_exits);
    else CHECK(budget()->block_exits && !budget()->time_exits);
    CHECK(budget()->credited_bytes==8u*2048u);cleanup();return calls;
}
static void revoked_credit_is_not_unsigned_work(void) {
    setup(true,false);(void)request(45000u,8u,OUT,false);
    wire.receiver_delay=1u;visit(false);arrive();
    CHECK(measured_service()==KUI_TOY_SCI_PENDING && service.completed_bytes);
    uint32_t credited=budget()->credited_bytes,zero=budget()->zero_credit_calls;
    unsigned bytes=hw.bytes;budget_revoke_credit=true;arrive();
    CHECK(kui_toy_pilot_sci_service(&service)==KUI_TOY_SCI_FAULT);
    CHECK(!service.completed_bytes && hw.bytes==bytes && !budget_revoke_credit);
    CHECK(budget()->credited_bytes==credited && budget()->zero_credit_calls==zero+1u);
    CHECK(!wire.flight && !hw.leased);cleanup();
}
static void counter_saturation(void) {
    setup(true,false);
    /* Fault-inject telemetry only through its actual writable backing
     * object. Normal calls expose a const snapshot and never mutate it. */
    struct kui_toy_pilot_sci_budget_stats *b=(struct kui_toy_pilot_sci_budget_stats *)budget();
    b->service_calls=b->finished_or_inactive_exits=b->zero_credit_calls=UINT32_MAX;
    CHECK(kui_toy_pilot_sci_service(&service)==KUI_TOY_SCI_OK);
    CHECK(b->service_calls==UINT32_MAX && b->finished_or_inactive_exits==UINT32_MAX && b->zero_credit_calls==UINT32_MAX);
    cleanup();setup(true,false);(void)request(45000u,8u,OUT,false);
    wire.receiver_delay=1u;visit(false);arrive();b=(struct kui_toy_pilot_sci_budget_stats *)budget();
    b->block_exits=UINT32_MAX;b->service_verified_blocks=UINT32_MAX-1u;b->credited_bytes=UINT32_MAX-10u;
    CHECK(kui_toy_pilot_sci_service(&service)==KUI_TOY_SCI_PENDING);
    CHECK(b->block_exits==UINT32_MAX && b->service_verified_blocks==UINT32_MAX && b->credited_bytes==UINT32_MAX);
    cleanup();
}
int main(void) {
    descriptor_and_no_work();block_limit_and_credit();time_and_step_limits();
    token_completion_and_failure();irq_credit_and_retirement();
    unsigned calls=independent_work_model();revoked_credit_is_not_unsigned_work();counter_saturation();
    printf("DATA budget profile %u: %u checks passed; blocks=%u ticks=%u; independent-work calls=%u\n",
        KUI_TOY_PILOT_DATA_BUDGET_PROFILE,checks,KUI_TOY_SCI_SERVICE_BLOCKS,KUI_TOY_SCI_SERVICE_TICKS,calls);
    return 0;
}
