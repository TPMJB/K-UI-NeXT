/* SPDX-License-Identifier: GPL-3.0-only */
/* Production DATA engine/cursor; the existing card model advances from an
 * independent clock. Literal expectations below come from the test matrix,
 * not the production allowance macros. No audio transport is callable. */
int kui_toy_pilot_sci_audio_acquire(void);
void kui_toy_pilot_sci_audio_release(void);
void kui_toy_pilot_sci_audio_cancel(void);
#define main original_followup_engine_fixture_main
#include "test_toy_pilot_sci.c"
#undef main

int kui_toy_pilot_sci_audio_acquire(void) {assert(!"follow-up DATA reached audio acquire");return -1;}
void kui_toy_pilot_sci_audio_release(void) {assert(!"follow-up DATA reached audio release");}
void kui_toy_pilot_sci_audio_cancel(void) {assert(!"follow-up DATA reached audio cancellation");}

#if !KUI_TOY_PILOT_DATA_BUDGET_PROFILE
#error Follow-up fixture requires the production AD budget path
#endif
#if KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE == 1
enum { EXPECT_BLOCKS=24u, EXPECT_TICKS=6000u, EXPECT_REPORT=5u };
#elif KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE == 2
enum { EXPECT_BLOCKS=24u, EXPECT_TICKS=12000u, EXPECT_REPORT=6u };
#elif KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE == 3
enum { EXPECT_BLOCKS=12u, EXPECT_TICKS=6000u, EXPECT_REPORT=7u };
#elif KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE == 4
enum { EXPECT_BLOCKS=12u, EXPECT_TICKS=6000u, EXPECT_REPORT=8u };
#elif KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE == 0
enum { EXPECT_BLOCKS=12u, EXPECT_TICKS=6000u, EXPECT_REPORT=4u };
#else
#error Unknown follow-up fixture profile
#endif

static uint32_t transcript=UINT32_C(2166136261);
static unsigned bound_blocks,bound_time_blocks,timed_calls;
static const struct kui_toy_pilot_sci_budget_stats *budget(void) {
    return kui_toy_pilot_sci_budget_snapshot();
}
static void digest(uint32_t value) {
    for(unsigned i=0;i<4u;++i) {
        transcript=(transcript^(value&255u))*UINT32_C(16777619);value>>=8;
    }
}
static void observe(void) {
    const struct kui_toy_pilot_sci_stats *s=kui_toy_pilot_sci_snapshot();
    const struct kui_toy_pilot_sci_budget_stats *b=budget();
    digest(service.token);digest(service.pending);digest(service.error);
    digest(service.completed_bytes);digest(hw.bytes);digest(hw.callbacks);
    digest(hw.ticks);digest(wire.polls);digest(wire.fetches);
    digest(s->call_blocks);digest(s->irq_blocks);digest(s->retries);
    digest(b->service_calls);digest(b->block_exits);digest(b->time_exits);
    digest(b->token_exits);digest(b->step_exits);digest(b->finished_or_inactive_exits);
    digest(b->service_verified_blocks);digest(b->credited_bytes);digest(b->zero_credit_calls);
}
static uint32_t exit_total(const struct kui_toy_pilot_sci_budget_stats *b) {
    return b->block_exits+b->time_exits+b->token_exits+b->step_exits+
        b->finished_or_inactive_exits;
}
static int measured_service(void) {
    struct kui_toy_pilot_sci_budget_stats prior=*budget();
    uint32_t bytes=service.completed_bytes,blocks=wire.stats.blocks;
    int result=kui_toy_pilot_sci_service(&service);
    const struct kui_toy_pilot_sci_budget_stats *b=budget();
    CHECK(b->service_calls==prior.service_calls+1u);
    CHECK(exit_total(b)==exit_total(&prior)+1u);
    CHECK(wire.stats.blocks-blocks<=EXPECT_BLOCKS);
    CHECK(b->service_verified_blocks-prior.service_verified_blocks==wire.stats.blocks-blocks);
    CHECK(b->credited_bytes-prior.credited_bytes==service.completed_bytes-bytes);
    CHECK(b->zero_credit_calls-prior.zero_credit_calls==(service.completed_bytes==bytes));
    CHECK(!wire.waits);observe();return result;
}
static void descriptor_and_no_work(void) {
    setup(true,false);const struct kui_toy_pilot_sci_budget_stats *b=budget();
    CHECK(b->profile==EXPECT_REPORT);
    CHECK(b->block_limit==EXPECT_BLOCKS && b->tick_limit==EXPECT_TICKS);
    CHECK(!b->service_calls && !exit_total(b) && !b->credited_bytes);
    CHECK(measured_service()==KUI_TOY_SCI_OK);
    CHECK(b->finished_or_inactive_exits==1u && b->zero_credit_calls==1u);
    CHECK(!wire.opens && !wire.fetches && !hw.bytes);cleanup();
}
static void finish_measured(uint32_t token,uint32_t sectors) {
    for(unsigned n=0;n<200u && service.pending;++n) {arrive();(void)measured_service();}
    CHECK(!service.pending && !service.error && service.completed_bytes==sectors*2048u);
    compare(45000u,sectors,OUT,false);
    for(unsigned n=0;n<sectors*2048u;++n)digest(ram[OUT-BEGIN+n]);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_NOT_FOUND);
}
static void block_cap_with_fast_receiver(void) {
    setup(true,true);uint32_t token=request(45000u,32u,OUT,false);
    wire.receiver_delay=1u;visit(false);arrive();
    uint32_t start=hw.ticks;
    CHECK(measured_service()==KUI_TOY_SCI_PENDING);
    CHECK(budget()->block_exits==1u && !budget()->time_exits && !budget()->step_exits);
    CHECK(budget()->service_verified_blocks==EXPECT_BLOCKS);
    CHECK(service.completed_bytes==(EXPECT_BLOCKS/4u)*2048u);
    CHECK(hw.ticks-start<EXPECT_TICKS);bound_blocks=wire.stats.blocks;
    finish_measured(token,32u);cleanup();
}
static void clock_cap_with_progress(void) {
    setup(true,true);uint32_t token=request(45000u,32u,OUT,false);
    wire.receiver_delay=1u;wire.poll_tick_cost=600u;visit(false);arrive();
    uint32_t start=hw.ticks;
    CHECK(measured_service()==KUI_TOY_SCI_PENDING);
    CHECK(budget()->time_exits==1u && !budget()->block_exits && !budget()->step_exits);
    CHECK(wire.stats.blocks>0u && wire.stats.blocks<EXPECT_BLOCKS);
    CHECK(service.completed_bytes>=2048u && service.completed_bytes<EXPECT_BLOCKS/4u*2048u);
    CHECK(hw.ticks-start>=EXPECT_TICKS && hw.ticks-start<=EXPECT_TICKS+616u);
    bound_time_blocks=wire.stats.blocks;
    finish_measured(token,32u);cleanup();
}
static void stalled_timer_and_token_admission(void) {
    setup(true,false);(void)request(45000u,8u,OUT,false);visit(false);
    hw.tick_step=20u;unsigned polls=wire.polls;uint32_t start=hw.ticks;
    CHECK(measured_service()==KUI_TOY_SCI_PENDING);
    CHECK(budget()->time_exits==1u && !budget()->block_exits && !budget()->step_exits);
    CHECK(hw.ticks-start>=EXPECT_TICKS && hw.ticks-start<=EXPECT_TICKS+40u);
    CHECK(wire.polls-polls<=EXPECT_TICKS/20u && !hw.bytes);cleanup();

    setup(true,false);(void)request(45000u,8u,OUT,false);visit(false);
    hw.tick_step=0u;polls=wire.polls;
    CHECK(measured_service()==KUI_TOY_SCI_PENDING);
    CHECK(wire.polls-polls==1024u && budget()->step_exits==1u && !budget()->time_exits);
    CHECK(!hw.bytes && service.pending);cleanup();

    setup(true,false);(void)request(45000u,8u,OUT,false);visit(false);
    hw.ticks=UINT32_MAX-100u;hw.tick_step=20u;
    CHECK(measured_service()==KUI_TOY_SCI_PENDING && budget()->time_exits==1u);
    CHECK(!budget()->step_exits && !hw.bytes);cleanup();

    setup(true,false);(void)request(45000u,3u,OUT,false);wire.token_pending=true;
    unsigned fetches=wire.fetches;
    CHECK(measured_service()==KUI_TOY_SCI_PENDING && budget()->token_exits==1u);
    CHECK(wire.fetches==fetches+1u && wire.token_bytes==256u && !hw.bytes);
    CHECK(measured_service()==KUI_TOY_SCI_PENDING && wire.token_bytes==512u);
    CHECK(budget()->token_exits==2u && !budget()->time_exits && !budget()->block_exits);cleanup();
}
static void clock_model_for_profile_pair(void) {
    setup(true,false);uint32_t token=request(45000u,32u,OUT,false);
    wire.receiver_delay=1u;wire.poll_tick_cost=260u;visit(false);arrive();
    timed_calls=0u;
    while(service.pending && timed_calls<100u) {
        advance_clock(100u);(void)measured_service();++timed_calls;
        CHECK(gd(KUI_GD_CHECK,token,STATUS)==(service.pending?KUI_GD_PROCESSING:KUI_GD_COMPLETED));
    }
    CHECK(!service.pending && !service.error && timed_calls<100u);
    compare(45000u,32u,OUT,false);
#if KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE == 1
    CHECK(budget()->time_exits && !budget()->block_exits);
#else
    CHECK(budget()->block_exits && !budget()->time_exits);
#endif
    CHECK(budget()->credited_bytes==32u*2048u);cleanup();
}
static void intercall_irq_credit_and_retirement(void) {
    setup(true,false);uint32_t token=request(45000u,32u,OUT,false);visit(false);
    arrive();visit(true);CHECK(kui_toy_pilot_sci_snapshot()->irq_blocks==1u);
    wire.receiver_delay=1u;arrive();CHECK(measured_service()==KUI_TOY_SCI_PENDING);
    CHECK(budget()->block_exits==1u && budget()->service_verified_blocks==EXPECT_BLOCKS-1u);
    CHECK(service.completed_bytes==EXPECT_BLOCKS/4u*2048u);
    CHECK(kui_toy_pilot_sci_snapshot()->irq_blocks==1u);
    arrive();CHECK(measured_service()==KUI_TOY_SCI_PENDING);
    CHECK(budget()->service_verified_blocks==2u*EXPECT_BLOCKS-1u);
    CHECK(service.completed_bytes==2u*EXPECT_BLOCKS/4u*2048u);
    cleanup();CHECK(!gd(KUI_GD_ABORT,token,0u));(void)gd(KUI_GD_CHECK,token,STATUS);
    CHECK(kui_toy_pilot_sci_quiesce());kui_toy_pilot_sci_retire();
    struct kui_toy_pilot_sci_budget_stats frozen=*budget();
    token=request(45000u,2u,OUT,false);
    CHECK(kui_toy_pilot_sci_service(&service)==KUI_TOY_SCI_OK);
    CHECK(kui_toy_pilot_sci_pump(&service)==KUI_TOY_SCI_OK && kui_toy_pilot_sci_irq()==1u);
    CHECK(!memcmp(&frozen,budget(),sizeof(frozen)) && !service.completed_bytes && !hw.leased);
    CHECK(!gd(KUI_GD_RESET,0u,0u) && !kui_toy_pilot_sci_startup_active());cleanup();
}
static void reused_generation_cannot_publish_old_cursor(void) {
    setup(true,true);uint32_t old=request(45000u,8u,OUT,false);visit(false);
    CHECK(wire.flight && !hw.bytes);CHECK(!gd(KUI_GD_RESET,0u,0u));
    /* RESET preserves the allocation counter. Seed its legitimate wrap
     * boundary to exercise reuse without issuing two billion requests. */
    service.token=UINT32_C(0x7fffffff);
    uint32_t fresh=request(45003u,4u,OUT2,false);CHECK(fresh==old);
    hw.frozen=true;arrive();visit(true);hw.frozen=false;
    CHECK(!hw.bytes && ram[OUT-BEGIN]==0xa5u && ram[OUT2-BEGIN]==0xa5u);
    finish();compare(45003u,4u,OUT2,false);
    for(unsigned n=0;n<8u*2048u;++n)CHECK(ram[OUT-BEGIN+n]==0xa5u);
    CHECK(gd(KUI_GD_CHECK,fresh,STATUS)==KUI_GD_COMPLETED);observe();cleanup();
}
static void cooked_DATA_source_layouts(void) {
    /* Startup admission serves 2048-byte DATA. Raw 2352-byte guest reads
     * route through the original high adapter, so do not fake their entry
     * into this engine. DATA extraction from raw image tracks is supported. */
    for(unsigned cooked=0u;cooked<2u;++cooked) for(unsigned scattered=0u;scattered<2u;++scattered) {
        setup(cooked!=0u,scattered!=0u);
        uint32_t token=request(45001u,MAX_SECTORS-1u,OUT,false);
        finish();compare(45001u,MAX_SECTORS-1u,OUT,false);
        CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED);observe();cleanup();
    }
}
int main(void) {
    descriptor_and_no_work();block_cap_with_fast_receiver();clock_cap_with_progress();
    stalled_timer_and_token_admission();clock_model_for_profile_pair();
    nonblocking_and_complete();cooked_DATA_source_layouts();cancellation_and_epochs();destination_failure();
    reused_generation_cannot_publish_old_cursor();intercall_irq_credit_and_retirement();
    printf("AD follow-up DATA profile %u: %u checks passed; report=%u blocks=%u ticks=%u; fast-cap=%u time-cap=%u timed-calls=%u; DATA_TRANSCRIPT=%08x\n",
        KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE,checks,EXPECT_REPORT,EXPECT_BLOCKS,EXPECT_TICKS,
        bound_blocks,bound_time_blocks,timed_calls,transcript);
    return 0;
}
