/* SPDX-License-Identifier: GPL-3.0-only */
/* Production startup DATA engine and cursor. DMA deadlines advance separately
 * from polls; simulated guest SR eligibility gates the actual IRQ handler. */
int kui_toy_pilot_sci_audio_acquire(void);
void kui_toy_pilot_sci_audio_release(void);
void kui_toy_pilot_sci_audio_cancel(void);
#define main original_irq_engine_fixture_main
#include "test_toy_pilot_sci.c"
#undef main
int kui_toy_pilot_sci_audio_acquire(void) {assert(!"IRQ fixture reached audio acquire");return -1;}
void kui_toy_pilot_sci_audio_release(void) {assert(!"IRQ fixture reached audio release");}
void kui_toy_pilot_sci_audio_cancel(void) {assert(!"IRQ fixture reached audio cancellation");}

#if KUI_TOY_PILOT_STARTUP_IRQ_PROFILE == 0
enum { EXPECT_PRIORITY=1u, EXPECT_STEPS=2048u, EXPECT_REPORT=9u };
#elif KUI_TOY_PILOT_STARTUP_IRQ_PROFILE == 1
enum { EXPECT_PRIORITY=7u, EXPECT_STEPS=2048u, EXPECT_REPORT=13u };
#elif KUI_TOY_PILOT_STARTUP_IRQ_PROFILE == 2
enum { EXPECT_PRIORITY=7u, EXPECT_STEPS=1u, EXPECT_REPORT=14u };
#else
#error Unknown IRQ progress fixture profile
#endif
enum { EXPECT_BLOCKS=24u, EXPECT_TICKS=12000u };
static bool guest_bl;
static unsigned guest_imask,eligible_events,blocked_events;
static unsigned between_gd_irq_blocks,fast_foreground_blocks,stalled_polls;
static unsigned foreground_calls;
static unsigned priority(void) {return (hw.iprb>>4)&15u;}
static unsigned deliver_guest_event(void) {
    /* Receiver completion is independent of eligibility. An asserted event
     * may remain pending while either BL or IMASK blocks the SCI priority. */
    if(!wire.flight || !wire.arrived)return 0u;
    if(guest_bl || priority()<=guest_imask) {++blocked_events;return 0u;}
    ++eligible_events;CHECK(kui_toy_pilot_sci_irq()==0u);return 1u;
}
static void elapsed_event(uint32_t ticks) {advance_clock(ticks);(void)deliver_guest_event();}
static int checked_service(void) {
    unsigned polls=wire.polls,takes=wire.takes,fetches=wire.fetches;
    uint32_t blocks=wire.stats.blocks;
    int result=kui_toy_pilot_sci_service(&service);++foreground_calls;
    CHECK(wire.stats.blocks-blocks<=EXPECT_BLOCKS && !wire.waits);
#if KUI_TOY_PILOT_STARTUP_IRQ_PROFILE == 2
    CHECK(wire.polls-polls<=1u && wire.takes-takes<=1u && wire.fetches-fetches<=1u);
    CHECK(wire.stats.blocks-blocks<=1u);
#else
    (void)polls;(void)takes;(void)fetches;
#endif
    return result;
}
static void finish_clocked(void) {
    guest_bl=false;guest_imask=0u;
    for(unsigned n=0;n<1000u && service.pending;++n) {
        if(!wire.flight)(void)checked_service();
        elapsed_event(100u);
    }
    CHECK(!service.pending && !service.error && !hw.leased && !wire.flight);
}
static void descriptor_and_hook_ports(void) {
    setup(true,false);
    const struct kui_toy_pilot_sci_budget_stats *b=kui_toy_pilot_sci_budget_snapshot();
    CHECK(b->profile==EXPECT_REPORT && b->block_limit==24u && b->tick_limit==12000u);
    /* Non-SCI priority fields and the original SCI nibble are deliberately
     * populated. The production write seam already checks preserved bits. */
    hw.iprb=0xa39bu;uint32_t token=request(45000u,3u,OUT,false);
    wire.receiver_delay=0u;visit(false);
    CHECK(priority()==EXPECT_PRIORITY && (hw.iprb&0xff0fu)==0xa30bu);
    CHECK(hw.vbr!=GAME_VBR && hw.leased && wire.flight);
    /* The game changes another field while DATA owns SCI. Hook/unhook must
     * keep that live field and restore only the captured SCI priority. */
    hw.iprb=(uint16_t)((hw.iprb&0x00f0u)|0x5c0au);
    (void)checked_service();
    CHECK(priority()==EXPECT_PRIORITY && (hw.iprb&0xff0fu)==0x5c0au);
    kui_toy_pilot_sci_cancel(&service);
    CHECK(hw.iprb==0x5c9au && hw.vbr==GAME_VBR && !hw.leased && !wire.flight);
    CHECK(gd(KUI_GD_ABORT,token,0u)==0);(void)gd(KUI_GD_CHECK,token,STATUS);
    hw.iprb=0x5a0fu;cleanup();
}
static void guest_mask_and_between_GD_progress(void) {
    setup(true,true);uint32_t token=request(45000u,8u,OUT,false);
    wire.receiver_delay=50u;visit(false);
    guest_bl=true;guest_imask=6u;unsigned calls=kui_toy_pilot_sci_snapshot()->calls;
    elapsed_event(100u);
    CHECK(wire.arrived && service.pending && !service.completed_bytes);
    CHECK(!kui_toy_pilot_sci_snapshot()->irq_calls && !hw.bytes);
    guest_bl=false;elapsed_event(100u);
#if KUI_TOY_PILOT_STARTUP_IRQ_PROFILE == 0
    CHECK(!kui_toy_pilot_sci_snapshot()->irq_calls && !hw.bytes && wire.arrived);
    guest_imask=0u;
#else
    CHECK(kui_toy_pilot_sci_snapshot()->irq_blocks==1u && wire.stats.blocks==1u);
#endif
    /* No GD CHECK/EXEC/pump/service calls occur until all remaining blocks
     * arrive. Each accepted IRQ performs one actual finite engine step. */
    for(unsigned n=0;n<100u && service.pending;++n)elapsed_event(100u);
    CHECK(!service.pending && service.completed_bytes==8u*2048u && !service.error);
    CHECK(kui_toy_pilot_sci_snapshot()->calls==calls);
    CHECK(kui_toy_pilot_sci_snapshot()->call_blocks==0u);
    CHECK(kui_toy_pilot_sci_snapshot()->irq_blocks==32u);
    CHECK(kui_toy_pilot_sci_budget_snapshot()->service_calls==0u);
    between_gd_irq_blocks=kui_toy_pilot_sci_snapshot()->irq_blocks;
    compare(45000u,8u,OUT,false);
    CHECK(hw.vbr==GAME_VBR && hw.iprb==0x5a0fu && !hw.leased && !wire.flight);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_NOT_FOUND);cleanup();
}
static void foreground_single_step_or_budget(void) {
    setup(true,true);uint32_t token=request(45000u,16u,OUT,false);
    wire.receiver_delay=1u;visit(false);arrive();
    unsigned polls=wire.polls,takes=wire.takes;
    CHECK(checked_service()==KUI_TOY_SCI_PENDING);
    fast_foreground_blocks=wire.stats.blocks;
#if KUI_TOY_PILOT_STARTUP_IRQ_PROFILE == 2
    CHECK(wire.stats.blocks==1u && wire.polls-polls==1u && wire.takes-takes==1u);
    CHECK(kui_toy_pilot_sci_budget_snapshot()->step_exits==1u);
#else
    CHECK(wire.stats.blocks==24u && wire.polls-polls>1u && wire.takes-takes>1u);
    CHECK(kui_toy_pilot_sci_budget_snapshot()->block_exits==1u);
#endif
    finish_clocked();compare(45000u,16u,OUT,false);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED);cleanup();

    setup(true,false);token=request(45000u,8u,OUT,false);visit(false);
    hw.tick_step=0u;polls=wire.polls;
    CHECK(checked_service()==KUI_TOY_SCI_PENDING);
    stalled_polls=wire.polls-polls;
    CHECK(stalled_polls==EXPECT_STEPS && !hw.bytes && wire.flight);
    CHECK(kui_toy_pilot_sci_budget_snapshot()->step_exits==1u);
    kui_toy_pilot_sci_cancel(&service);
    CHECK(gd(KUI_GD_ABORT,token,0u)==0);(void)gd(KUI_GD_CHECK,token,STATUS);cleanup();

    setup(true,false);token=request(45000u,8u,OUT,false);
    /* Opening/launching an AN entry is itself one step, even before any
     * receiver is available for a poll. */
    CHECK(checked_service()==KUI_TOY_SCI_PENDING && wire.flight);
#if KUI_TOY_PILOT_STARTUP_IRQ_PROFILE == 2
    CHECK(wire.polls==0u && wire.takes==1u && wire.fetches==1u);
#endif
    kui_toy_pilot_sci_cancel(&service);(void)gd(KUI_GD_ABORT,token,0u);(void)gd(KUI_GD_CHECK,token,STATUS);cleanup();
}
static void fragmented_raw_and_cooked_payloads(void) {
    for(unsigned cooked=0u;cooked<2u;++cooked)for(unsigned scattered=0u;scattered<2u;++scattered) {
        setup(cooked!=0u,scattered!=0u);
        uint32_t token=request(45001u,MAX_SECTORS-1u,OUT,false);
        wire.receiver_delay=50u;visit(false);finish_clocked();
        compare(45001u,MAX_SECTORS-1u,OUT,false);
        CHECK(service.completed_bytes==(MAX_SECTORS-1u)*2048u);
        CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED);cleanup();
    }
}
static void CRC_retry_and_terminal_failure(void) {
    setup(true,true);uint32_t token=request(45000u,4u,OUT,false);
    wire.receiver_delay=50u;visit(false);wire.take_error=KUI_SCI_STREAM_CRC;
    guest_bl=false;guest_imask=0u;elapsed_event(100u);
    CHECK(!hw.bytes && !service.completed_bytes && ram[OUT-BEGIN]==0xa5u);
    CHECK(kui_toy_pilot_sci_snapshot()->retries==1u);
    wire.take_error=KUI_SCI_STREAM_OK;finish_clocked();compare(45000u,4u,OUT,false);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED);cleanup();

    setup(true,false);token=request(45000u,4u,OUT,false);
    wire.receiver_delay=50u;visit(false);wire.take_error=KUI_SCI_STREAM_CRC;
    for(unsigned n=0;n<50u && service.pending;++n)elapsed_event(100u);
    CHECK(!service.pending && service.error==KUI_GD_ERROR_IO && !hw.bytes);
    CHECK(kui_toy_pilot_sci_snapshot()->retries==9u && !hw.leased && !wire.flight);
    CHECK(hw.vbr==GAME_VBR && hw.iprb==0x5a0fu);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED);cleanup();

    setup(true,false);token=request(45000u,4u,OUT,false);wire.receiver_delay=50u;visit(false);
    wire.poll_error=KUI_SCI_STREAM_BUSY;elapsed_event(100u);
    CHECK(!service.pending && service.error==KUI_GD_ERROR_IO && !hw.bytes && !hw.leased);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED);cleanup();
    (void)0; /* all failure paths release the same actual lease */
}
static void revoked_IRQ_stops_before_receiver_consumption(void) {
    setup(true,true);uint32_t token=request(45000u,3u,OUT,false);
    wire.receiver_delay=50u;visit(false);guest_bl=false;guest_imask=0u;
    unsigned polls=wire.polls,takes=wire.takes;
    /* A matching token alone cannot authorize consuming an old transfer
     * after its destination tuple changes. The later publication guard is
     * separate; this early check must cancel before poll/take/verification. */
    service.destination=OUT2;hw.frozen=true;elapsed_event(100u);
    CHECK(wire.polls==polls && wire.takes==takes && !wire.stats.blocks);
    CHECK(!hw.bytes && !hw.leased && !wire.flight);
    CHECK(ram[OUT-BEGIN]==0xa5u && ram[OUT2-BEGIN]==0xa5u);
    CHECK(hw.vbr==GAME_VBR && hw.iprb==0x5a0fu);
    hw.frozen=false;(void)gd(KUI_GD_ABORT,token,0u);(void)gd(KUI_GD_CHECK,token,STATUS);cleanup();
}
static void last_block_and_retirement(void) {
    setup(true,false);uint32_t token=request(45000u,1u,OUT,false);
    wire.receiver_delay=50u;visit(false);guest_bl=false;guest_imask=0u;
    for(unsigned n=0;n<3u;++n)elapsed_event(100u);
    CHECK(service.pending && !service.completed_bytes && hw.leased && wire.flight);
    elapsed_event(100u);
    CHECK(!service.pending && service.completed_bytes==2048u && !hw.leased && !wire.flight);
    CHECK(kui_toy_pilot_sci_snapshot()->irq_blocks==4u && hw.vbr==GAME_VBR && hw.iprb==0x5a0fu);
    compare(45000u,1u,OUT,false);CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED);
    CHECK(kui_toy_pilot_sci_quiesce());kui_toy_pilot_sci_retire();
    CHECK(!kui_toy_pilot_sci_startup_active());
    struct kui_toy_pilot_sci_stats frozen=*kui_toy_pilot_sci_snapshot();
    struct kui_toy_pilot_sci_budget_stats frozen_budget=*kui_toy_pilot_sci_budget_snapshot();
    unsigned bytes=hw.bytes,fetches=wire.fetches;
    token=request(45000u,2u,OUT2,false);hw.frozen=true;advance_clock(10000u);
    CHECK(kui_toy_pilot_sci_service(&service)==KUI_TOY_SCI_OK && kui_toy_pilot_sci_irq()==1u);
    CHECK(kui_toy_pilot_sci_pump(&service)==KUI_TOY_SCI_OK);
    CHECK(!memcmp(&frozen,kui_toy_pilot_sci_snapshot(),sizeof(frozen)));
    CHECK(!memcmp(&frozen_budget,kui_toy_pilot_sci_budget_snapshot(),sizeof(frozen_budget)));
    CHECK(hw.bytes==bytes && wire.fetches==fetches && !hw.leased && hw.iprb==0x5a0fu);
    hw.frozen=false;(void)gd(KUI_GD_ABORT,token,0u);(void)gd(KUI_GD_CHECK,token,STATUS);cleanup();
}
int main(void) {
    descriptor_and_hook_ports();guest_mask_and_between_GD_progress();
    foreground_single_step_or_budget();fragmented_raw_and_cooked_payloads();
    CRC_retry_and_terminal_failure();cancellation_and_epochs();destination_failure();
    revoked_IRQ_stops_before_receiver_consumption();last_block_and_retirement();
    printf("Startup IRQ profile %u: %u checks passed; report=%u priority=%u steps=%u; between-GD IRQ blocks=%u fast-foreground blocks=%u stalled-polls=%u; eligible-events=%u blocked-events=%u foreground-calls=%u\n",
        KUI_TOY_PILOT_STARTUP_IRQ_PROFILE,checks,EXPECT_REPORT,EXPECT_PRIORITY,EXPECT_STEPS,
        between_gd_irq_blocks,fast_foreground_blocks,stalled_polls,eligible_events,blocked_events,foreground_calls);
    return 0;
}
