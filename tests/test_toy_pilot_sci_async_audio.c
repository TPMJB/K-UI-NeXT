/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the independent physical card, clock-driven DMA and ordinary image
 * reader oracle. No audio cursor or engine state is replicated here. */
#define main shared_sci_baseline_fixture_main
#include "test_toy_pilot_sci.c"
#undef main

struct audio_output {
    uint8_t before[32], bytes[2352], after[32];
};
static struct audio_output audio0,audio1;
static void clear_audio(struct audio_output *out) { memset(out,0xa5,sizeof(*out)); }
static void guards(const struct audio_output *out) {
    for(unsigned i=0;i<sizeof(out->before);++i) CHECK(out->before[i]==0xa5u);
    for(unsigned i=0;i<sizeof(out->after);++i) CHECK(out->after[i]==0xa5u);
}
static void untouched(const struct audio_output *out) {
    guards(out);
    for(unsigned i=0;i<sizeof(out->bytes);++i) CHECK(out->bytes[i]==0xa5u);
}
static void audio_matches(uint32_t lba,const struct audio_output *out) {
    CHECK(kui_retail_image_read(&reference,lba,1u,KUI_GAME_SECTOR_RAW,
        expected,sizeof(expected))==KUI_GAME_OK);
    CHECK(!memcmp(out->bytes,expected,sizeof(out->bytes))); guards(out);
}
static int audio_visit(uint32_t lba,uint32_t generation,struct audio_output *out) {
    unsigned takes=wire.takes,waits=wire.waits;
    const struct kui_toy_pilot_sci_stats *stats=kui_toy_pilot_sci_snapshot();
    uint32_t blocks=stats->call_blocks+stats->irq_blocks;
    int result=kui_toy_pilot_sci_audio_read(lba,generation,out->bytes);
    /* Handoff may check an empty freshly opened successor receiver, but
     * only one verified physical payload may be consumed in this entry. */
    CHECK(wire.takes-takes<=2u && stats->call_blocks+stats->irq_blocks-blocks<=1u && wire.waits==waits);
    if(result!=KUI_TOY_SCI_OK) untouched(out);
    else guards(out);
    return result;
}
static void audio_finish(uint32_t lba,uint32_t generation,struct audio_output *out) {
    for(unsigned i=0;i<100u;++i) {
        advance_clock(100u);
        int result=audio_visit(lba,generation,out);
        CHECK(result!=KUI_TOY_SCI_FAULT);
        if(result==KUI_TOY_SCI_OK) {audio_matches(lba,out);return;}
    }
    fprintf(stderr,"Unfinished audio lba=%u generation=%u: opens=%u fetches=%u takes=%u flight=%u leased=%u owner=%u\n",
        lba,generation,wire.opens,wire.fetches,wire.takes,wire.flight,hw.leased,
        kui_toy_pilot_sci_snapshot()->card_owned);
    CHECK(!"Audio did not finish with independently progressing DMA");
}
static void audio_cleanup(void) {
    kui_toy_pilot_sci_audio_cancel(); cleanup();
    CHECK(hw.acquires==hw.releases && !wire.waits);
}

static void scheduled_completion_and_repeated_consumption(void) {
    setup(false,true); clear_audio(&audio0);
    hw.tick_step=0u; wire.receiver_delay=80u;
    CHECK(audio_visit(5u,1u,&audio0)==KUI_TOY_SCI_PENDING);
    CHECK(wire.flight && hw.leased);
    unsigned fetches=wire.fetches;
    for(unsigned i=0;i<20u;++i) CHECK(audio_visit(5u,1u,&audio0)==KUI_TOY_SCI_PENDING);
    CHECK(wire.fetches==fetches && !kui_toy_pilot_sci_snapshot()->irq_calls);
    audio_finish(5u,1u,&audio0);
    CHECK(!hw.leased && !wire.flight);
    unsigned reads=wire.fetches,claims=hw.acquires;
    memset(audio0.bytes,0x73,sizeof(audio0.bytes));
    CHECK(audio_visit(5u,1u,&audio0)==KUI_TOY_SCI_OK);
    for(unsigned i=0;i<sizeof(audio0.bytes);++i) CHECK(audio0.bytes[i]==0x73u);
    CHECK(wire.fetches==reads && hw.acquires==claims); audio_cleanup();
}

static void fragmented_raw_and_audio_only_foreground(void) {
    for(unsigned cooked=0;cooked<2u;++cooked) for(unsigned scattered=0;scattered<2u;++scattered) {
        setup(cooked!=0u,scattered!=0u); hw.tick_step=10u; wire.receiver_delay=80u;
        for(uint32_t lba=4u;lba<12u;++lba) {
            clear_audio(&audio0);
            CHECK(audio_visit(lba,17u,&audio0)==KUI_TOY_SCI_PENDING);
            /* Game executes and calls foreground service, with every SCI
             * interrupt masked and no game-data request or handle. */
            bool complete=false;
            for(unsigned visit_count=0;visit_count<20u;++visit_count) {
                advance_clock(100u);
                unsigned takes=wire.takes,budgets=wire.token_budgets;
                (void)kui_toy_pilot_sci_service(NULL);
                CHECK(wire.takes-takes<=4u && wire.token_budgets==budgets+1u && !wire.waits);
                untouched(&audio0); /* service never publishes worker PCM */
                int result=audio_visit(lba,17u,&audio0);
                CHECK(result!=KUI_TOY_SCI_FAULT);
                if(result==KUI_TOY_SCI_OK) {complete=true;break;}
            }
            CHECK(complete && !kui_toy_pilot_sci_snapshot()->irq_calls);
            audio_matches(lba,&audio0);
        }
        audio_cleanup();
    }
}

static void interrupt_completion_has_no_output_authority(void) {
    setup(false,true); clear_audio(&audio0); hw.tick_step=0u; wire.receiver_delay=80u;
    CHECK(audio_visit(5u,19u,&audio0)==KUI_TOY_SCI_PENDING);
    for(unsigned i=0;i<12u && wire.flight;++i) {
        advance_clock(100u);
        CHECK(kui_toy_pilot_sci_irq()==0u); untouched(&audio0);
    }
    CHECK(!wire.flight && !hw.leased && kui_toy_pilot_sci_snapshot()->irq_blocks>=5u);
    CHECK(audio_visit(5u,19u,&audio0)==KUI_TOY_SCI_OK); audio_matches(5u,&audio0);
    audio_cleanup();

    /* A finished private sector still has no authority once cancellation
     * or a newer tuple supersedes it before the worker consumes it. */
    setup(false,true); clear_audio(&audio0); clear_audio(&audio1);
    hw.tick_step=0u; wire.receiver_delay=80u;
    CHECK(audio_visit(5u,20u,&audio0)==KUI_TOY_SCI_PENDING);
    for(unsigned i=0;i<12u && wire.flight;++i) {
        advance_clock(100u); CHECK(kui_toy_pilot_sci_irq()==0u); untouched(&audio0);
    }
    CHECK(!wire.flight && !hw.leased);
    CHECK(audio_visit(6u,21u,&audio1)==KUI_TOY_SCI_PENDING);
    audio_finish(6u,21u,&audio1); untouched(&audio0); audio_cleanup();
}

static void audio_only_finite_service_and_worker_cadence(void) {
    setup(false,true); clear_audio(&audio0); hw.tick_step=0u; wire.receiver_delay=1000000u;
    CHECK(audio_visit(5u,27u,&audio0)==KUI_TOY_SCI_PENDING);
    unsigned polls=wire.polls,budgets=wire.token_budgets;
    CHECK(kui_toy_pilot_sci_service(NULL)==KUI_TOY_SCI_PENDING);
    CHECK(wire.polls-polls==1024u && wire.token_budgets==budgets+1u && !wire.waits);
    untouched(&audio0); audio_cleanup();

    setup(false,true); clear_audio(&audio0); hw.tick_step=10u; wire.receiver_delay=1000000u;
    hw.ticks=UINT32_MAX-100u;
    CHECK(audio_visit(5u,28u,&audio0)==KUI_TOY_SCI_PENDING);
    uint32_t began=hw.ticks; polls=wire.polls;
    CHECK(kui_toy_pilot_sci_service(NULL)==KUI_TOY_SCI_PENDING);
    CHECK(wire.polls-polls<=151u && hw.ticks-began>=1500u && hw.ticks-began<=1520u);
    untouched(&audio0); audio_cleanup();

    setup(false,true); clear_audio(&audio0); hw.tick_step=0u; wire.token_pending=true;
    CHECK(audio_visit(5u,29u,&audio0)==KUI_TOY_SCI_PENDING);
    CHECK(wire.token_bytes==256u && !wire.flight);
    budgets=wire.token_budgets; unsigned fetches=wire.fetches;
    CHECK(kui_toy_pilot_sci_service(NULL)==KUI_TOY_SCI_PENDING);
    CHECK(wire.token_budgets==budgets+1u && wire.fetches==fetches+1u && wire.token_bytes==512u);
    untouched(&audio0); audio_cleanup();

    /* An intentionally sparse 80-ms worker-only schedule demonstrates
     * why this API also needs independent IRQ or foreground progress.
     * This is a scheduling bound, not a model of measured hardware speed. */
    setup(false,true); clear_audio(&audio0); hw.tick_step=0u; wire.receiver_delay=80u;
    CHECK(audio_visit(5u,30u,&audio0)==KUI_TOY_SCI_PENDING);
    began=hw.ticks; unsigned worker_visits=0u; int result=KUI_TOY_SCI_PENDING;
    while(result==KUI_TOY_SCI_PENDING && worker_visits<12u) {
        advance_clock(62500u); ++worker_visits;
        result=audio_visit(5u,30u,&audio0);
    }
    CHECK(result==KUI_TOY_SCI_OK && worker_visits>=5u && !kui_toy_pilot_sci_snapshot()->irq_calls);
    CHECK(hw.ticks-began>=5u*62500u); audio_matches(5u,&audio0);
    printf("Sparse masked-IRQ audio-only schedule: %u worker visits at 80 ms; foreground/IRQ progress is required\n",
        worker_visits); audio_cleanup();
}

static void priority_and_pending_data_handle(void) {
    setup(true,true); clear_audio(&audio0); hw.tick_step=0u; wire.receiver_delay=80u;
    uint32_t token=request(45000u,8u,OUT,false); visit(false);
    uint32_t data_lba=wire.in_lba; unsigned fetches=wire.fetches,bytes=hw.bytes;
    CHECK(audio_visit(5u,23u,&audio0)==KUI_TOY_SCI_PENDING);
    CHECK(wire.in_lba==data_lba && wire.fetches==fetches && hw.bytes==bytes);
    advance_clock(40u);
    CHECK(wire.partial_bytes==64u);
    CHECK(audio_visit(5u,23u,&audio0)==KUI_TOY_SCI_PENDING);
    CHECK(wire.in_lba==data_lba && wire.fetches==fetches && hw.bytes==bytes);
    advance_clock(40u);
    CHECK(audio_visit(5u,23u,&audio0)==KUI_TOY_SCI_PENDING);
    CHECK(hw.bytes-bytes<=512u && service.pending && service.token==token);
    audio_finish(5u,23u,&audio0);
    CHECK(service.pending && !service.error && service.token==token);
    /* The audio lease was released; the waiting data handle may already
     * have armed its own receiver before the worker consumes the sector. */
    CHECK(kui_toy_pilot_sci_snapshot()->audio_claims==
        kui_toy_pilot_sci_snapshot()->audio_releases);
    hw.tick_step=10u; wire.receiver_delay=20u; finish_foreground();
    compare(45000u,8u,OUT,false);
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED); audio_cleanup();
}

static void handoff_uses_the_same_service_admission(void) {
    setup(true,true); clear_audio(&audio0); hw.tick_step=0u; wire.receiver_delay=80u;
    uint32_t token=request(45000u,8u,OUT,false); visit(false);
    CHECK(audio_visit(5u,24u,&audio0)==KUI_TOY_SCI_PENDING);
    advance_clock(100u); wire.poll_tick_cost=1600u;
    unsigned polls=wire.polls; uint32_t began=hw.ticks;
    CHECK(kui_toy_pilot_sci_service(&service)==KUI_TOY_SCI_PENDING);
    CHECK(wire.polls==polls+1u && hw.ticks-began==1600u);
    CHECK(wire.flight && kui_toy_pilot_sci_snapshot()->card_owned==2u);
    untouched(&audio0); wire.poll_tick_cost=0u;
    audio_finish(5u,24u,&audio0);
    kui_toy_pilot_sci_cancel(&service); CHECK(gd(KUI_GD_ABORT,token,0u)==0);
    (void)gd(KUI_GD_CHECK,token,STATUS); audio_cleanup();

    /* The boundary branch counts as one of the finite steps even if the
     * timer does not advance. Its newly armed RAW receiver remains pending
     * for the other 1023 steps; a branch plus 1024 would exceed admission. */
    setup(true,true); clear_audio(&audio0); hw.tick_step=0u; wire.receiver_delay=80u;
    token=request(45000u,8u,OUT,false); visit(false);
    CHECK(audio_visit(5u,25u,&audio0)==KUI_TOY_SCI_PENDING);
    advance_clock(100u); polls=wire.polls;
    CHECK(kui_toy_pilot_sci_service(&service)==KUI_TOY_SCI_PENDING);
    CHECK(wire.polls-polls==1024u && wire.flight && hw.leased);
    untouched(&audio0); audio_finish(5u,25u,&audio0);
    kui_toy_pilot_sci_cancel(&service); CHECK(gd(KUI_GD_ABORT,token,0u)==0);
    (void)gd(KUI_GD_CHECK,token,STATUS); audio_cleanup();
}

static void advance_irq_to(uint32_t deadline) {
    while(wire.flight && wire.ready_tick<=deadline) {
        CHECK(wire.ready_tick>=hw.ticks);
        advance_clock(wire.ready_tick-hw.ticks);
        CHECK(kui_toy_pilot_sci_irq()==0u); untouched(&audio0);
    }
    CHECK(deadline>=hw.ticks); advance_clock(deadline-hw.ticks);
}
static unsigned sustained_worker_schedule(unsigned frames_per_second) {
    setup(false,true); clear_audio(&audio0); hw.tick_step=0u; wire.receiver_delay=1u;
    unsigned deliveries=0u; uint32_t lba=4u;
    /* The first schedule is two worker polls 99.84 us apart each 60-Hz
     * frame. The other is the same pair each 80-ms updater interval.
     * Card DMA is deliberately ideal and independent: IRQs complete each
     * block at its scheduled time, never as a side effect of a worker poll. */
    for(unsigned interval=0;;++interval) {
        uint32_t first=frames_per_second?
            (uint32_t)((uint64_t)interval*781250u/frames_per_second):interval*62500u;
        if(first>=781250u) break;
        for(unsigned clustered=0;clustered<2u;++clustered) {
            uint32_t when=first+clustered*78u;
            if(when>=781250u) break;
            advance_irq_to(when);
            int result=audio_visit(lba,91u,&audio0);
            CHECK(result!=KUI_TOY_SCI_FAULT);
            if(result==KUI_TOY_SCI_OK) {
                audio_matches(lba,&audio0); ++deliveries;
                lba=lba==11u?4u:lba+1u;
                /* A delivered sector advances the worker's LBA. The next
                 * request can be made only at a subsequent worker visit. */
                clear_audio(&audio0);
            }
        }
    }
    advance_irq_to(781250u); untouched(&audio0);
    CHECK(kui_toy_pilot_sci_snapshot()->audio_claims==
        kui_toy_pilot_sci_snapshot()->audio_releases);
    CHECK(hw.acquires==hw.releases && !wire.waits && !hw.leased);
    audio_cleanup(); return deliveries;
}
static void sustained_delivery_exposes_worker_cadence_limit(void) {
    unsigned frame_deliveries=sustained_worker_schedule(60u);
    unsigned slow_deliveries=sustained_worker_schedule(0u);
    CHECK(frame_deliveries==60u && frame_deliveries<75u);
    CHECK(slow_deliveries==13u && slow_deliveries<=25u);
    printf("Ideal independent IRQ transport: %u sectors/1 s with two clustered polls per 60-Hz frame; %u sectors/1 s with 80-ms clusters (CDDA needs 75)\n",
        frame_deliveries,slow_deliveries);
}

static void legacy_raw_lease_is_not_admitted(void) {
    setup(false,true); clear_audio(&audio0);
    hw.tick_step=0u; wire.receiver_delay=80u;
    unsigned opens=wire.opens,fetches=wire.fetches;
    CHECK(kui_toy_pilot_sci_audio_acquire()==KUI_TOY_SCI_FAULT);
    CHECK(!hw.leased && wire.opens==opens && wire.fetches==fetches);
    kui_toy_pilot_sci_audio_release();
    audio_finish(5u,31u,&audio0); audio_cleanup();
}

static void supersession_cancel_and_private_partial_dma(void) {
    /* Identity is the complete tuple: a new LBA, generation or output
     * address revokes old staging before its DMA fence. */
    for(unsigned changed=0;changed<3u;++changed) {
        setup(false,true); clear_audio(&audio0); clear_audio(&audio1);
        hw.tick_step=0u; wire.receiver_delay=80u;
        CHECK(audio_visit(5u,41u,&audio0)==KUI_TOY_SCI_PENDING);
        advance_clock(40u); CHECK(wire.partial_bytes==64u && wire.flight);
        uint32_t lba=changed==0u?6u:5u,generation=changed==1u?42u:41u;
        struct audio_output *target=changed==2u?&audio1:&audio0;
        CHECK(audio_visit(lba,generation,target)==KUI_TOY_SCI_PENDING);
        untouched(&audio0); advance_clock(100u); untouched(&audio0);
        audio_finish(lba,generation,target);
        if(changed==2u) untouched(&audio0);
        audio_cleanup();
    }
    setup(false,true); clear_audio(&audio0); hw.tick_step=0u; wire.receiver_delay=80u;
    CHECK(audio_visit(5u,51u,&audio0)==KUI_TOY_SCI_PENDING);
    advance_clock(40u); CHECK(wire.partial_bytes==64u && wire.flight);
    kui_toy_pilot_sci_audio_cancel();
    CHECK(!wire.flight && !hw.leased); advance_clock(1000u);
    CHECK(kui_toy_pilot_sci_irq()==1u); (void)kui_toy_pilot_sci_service(NULL);
    untouched(&audio0);
    audio_finish(6u,52u,&audio0); audio_cleanup();
}

static void high_core_data_revoke_preserves_audio_identity(void) {
    /* These are high-core DATA revocations only. Native GD RESET also
     * invokes worker RESET, which revokes its audio generation separately. */
    for(unsigned reset=0;reset<2u;++reset) {
        setup(true,true); clear_audio(&audio0); hw.tick_step=0u; wire.receiver_delay=80u;
        uint32_t token=request(45000u,5u,OUT,false); visit(false);
        CHECK(audio_visit(5u,61u,&audio0)==KUI_TOY_SCI_PENDING);
        advance_clock(80u); CHECK(audio_visit(5u,61u,&audio0)==KUI_TOY_SCI_PENDING);
        advance_clock(80u); (void)audio_visit(5u,61u,&audio0);
        CHECK(wire.flight && !service.completed_bytes);
        kui_toy_pilot_sci_cancel(&service);
        CHECK(gd(reset?KUI_GD_RESET:KUI_GD_ABORT,reset?0u:token,0u)==0);
        untouched(&audio0); audio_finish(5u,61u,&audio0);
        CHECK(!service.pending && (reset?!service.command:service.error==KUI_GD_ERROR_CANCELLED));
        if(!reset) CHECK(gd(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED);
        audio_cleanup();
    }
    setup(false,true); clear_audio(&audio0); wire.receiver_delay=80u; hw.tick_step=0u;
    CHECK(audio_visit(5u,71u,&audio0)==KUI_TOY_SCI_PENDING);
    advance_clock(40u); kui_toy_pilot_sci_cancel(NULL); advance_clock(1000u);
    CHECK(!wire.flight && !hw.leased); (void)kui_toy_pilot_sci_service(NULL);
    untouched(&audio0); audio_cleanup();
}

static void crc_retry_pio_and_failed_sector(void) {
    setup(false,true); clear_audio(&audio0); hw.tick_step=0u; wire.receiver_delay=80u;
    CHECK(audio_visit(5u,81u,&audio0)==KUI_TOY_SCI_PENDING);
    wire.take_error=KUI_SCI_STREAM_CRC;
    for(unsigned retry_count=0;retry_count<2u;++retry_count) {
        advance_clock(100u); CHECK(audio_visit(5u,81u,&audio0)==KUI_TOY_SCI_PENDING);
    }
    CHECK(kui_toy_pilot_sci_snapshot()->retries>=2u && wire.stats.polled>=1u);
    wire.take_error=KUI_SCI_STREAM_OK;
    audio_finish(5u,81u,&audio0); audio_cleanup();

    setup(false,true); clear_audio(&audio0); hw.tick_step=0u; wire.receiver_delay=80u;
    CHECK(audio_visit(5u,82u,&audio0)==KUI_TOY_SCI_PENDING);
    wire.take_error=KUI_SCI_STREAM_CRC;
    int result=KUI_TOY_SCI_PENDING;
    for(unsigned i=0;i<10u && result==KUI_TOY_SCI_PENDING;++i) {
        advance_clock(100u); result=audio_visit(5u,82u,&audio0);
    }
    CHECK(result==KUI_TOY_SCI_FAULT && !wire.flight && !hw.leased);
    untouched(&audio0); audio_cleanup();

    setup(false,true); clear_audio(&audio0); hw.tick_step=0u; wire.receiver_delay=80u;
    CHECK(audio_visit(5u,83u,&audio0)==KUI_TOY_SCI_PENDING);
    wire.deferred_resume=true; advance_clock(100u);
    uint32_t block=wire.in_lba; unsigned reads=wire.fetches;
    CHECK(audio_visit(5u,83u,&audio0)==KUI_TOY_SCI_PENDING);
    CHECK(!wire.flight && hw.leased && wire.fetches==reads);
    CHECK(audio_visit(5u,83u,&audio0)==KUI_TOY_SCI_PENDING);
    CHECK(wire.flight && wire.in_lba==block && wire.fetches==reads+1u);
    audio_finish(5u,83u,&audio0); audio_cleanup();

    setup(false,true); clear_audio(&audio0); hw.tick_step=0u; wire.receiver_delay=80u;
    CHECK(audio_visit(5u,84u,&audio0)==KUI_TOY_SCI_PENDING);
    CHECK(audio_visit(5u,0u,&audio0)==KUI_TOY_SCI_FAULT);
    CHECK(kui_toy_pilot_sci_audio_read(5u,84u,NULL)==KUI_TOY_SCI_FAULT);
    untouched(&audio0); audio_finish(5u,84u,&audio0); audio_cleanup();
}

int main(void) {
    scheduled_completion_and_repeated_consumption();
    fragmented_raw_and_audio_only_foreground();
    interrupt_completion_has_no_output_authority(); audio_only_finite_service_and_worker_cadence();
    priority_and_pending_data_handle(); handoff_uses_the_same_service_admission();
    sustained_delivery_exposes_worker_cadence_limit(); legacy_raw_lease_is_not_admitted();
    supersession_cancel_and_private_partial_dma(); high_core_data_revoke_preserves_audio_identity();
    crc_retry_pio_and_failed_sector();
    printf("Shared asynchronous SCI audio: %u checks passed\n",checks);
    return 0;
}
