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
/* Expand only the independent card/map fixture; the production producer,
 * cursor and arbiter remain linked from their ordinary source files. */
static void setup_long_audio(bool cooked,bool scattered) {
    setup(cooked,scattered);
    memset(&manifest,0,sizeof(manifest));memset(card_bytes,0xf3,sizeof(card_bytes));
    manifest.card_sectors=BLOCKS;manifest.partition_start=50u;manifest.partition_end=2000u;
    manifest.track_count=3u;manifest.session_lba=45000u;
    manifest.boot_lba=45001u;manifest.boot_bytes=4567u;
    strcpy(manifest.title,"Queued audio cadence fixture");strcpy(manifest.bootfile,"1ST_READ.BIN");
    static const uint32_t starts[]={0u,4u,45000u},ends[]={4u,164u,45032u};
    uint32_t used=0u;
    for(unsigned track=0;track<3u;++track) {
        struct kui_retail_track *t=&manifest.slots[track].track;
        *t=(struct kui_retail_track){.start_lba=starts[track],.end_lba=ends[track],
            .control=track==1u?0u:(uint8_t)(4u|(cooked?KUI_RETAIL_TRACK_COOKED:0u)),
            .first_extent=(uint16_t)(3u+manifest.extent_count)};
        uint32_t stride=kui_retail_track_sector_bytes(t),bytes=(t->end_lba-t->start_lba)*stride;
        uint32_t blocks=(bytes+511u)/512u;
        for(uint32_t first=0;first<blocks;) {
            uint32_t take=blocks-first>7u?7u:blocks-first,index=manifest.extent_count;
            uint32_t physical=scattered?100u+(index&1u?900u:0u)+(index>>1)*8u:100u+used;
            CHECK(3u+index<KUI_RETAIL_IMAGE_SLOTS && physical+take<2000u);
            manifest.slots[3u+manifest.extent_count++].extent=(struct kui_retail_extent){first,physical,take};
            ++t->extent_count;
            for(uint32_t p=0;p<take*512u && first*512u+p<bytes;++p) {
                uint32_t at=first*512u+p;
                uint32_t original=stride==2048u?at/2048u*2352u+16u+at%2048u:at;
                card_bytes[physical*512u+p]=source(track,original);
            }
            first+=take;used+=take;
        }
    }
    CHECK(kui_retail_manifest_validate(&manifest)==KUI_GAME_OK);
    CHECK(kui_retail_image_init(&reference,&manifest,read_block,NULL)==KUI_GAME_OK);
    kui_toy_pilot_sci_init(&manifest,&card,acquire,release);
    clear_audio(&audio0);clear_audio(&audio1);hw.tick_step=0u;wire.receiver_delay=1u;
}
static void plan(uint32_t lba,uint32_t end,uint32_t generation,uint32_t reserve) {
    CHECK(kui_toy_pilot_sci_audio_plan(lba,end,generation,reserve)!=KUI_TOY_SCI_FAULT);
    untouched(&audio0);
}
static void queued_prefetch_and_exact_range(void) {
    setup(false,true);clear_audio(&audio0);hw.tick_step=0u;wire.receiver_delay=80u;
    plan(4u,12u,90u,8192u);
    /* No worker read has admitted an output address. Independent interrupts
     * can still produce exactly four verified, complete RAW sectors. */
    advance_irq_to(10000u);untouched(&audio0);
    CHECK(!wire.flight && !hw.leased);
    unsigned reads=wire.fetches;
    for(unsigned n=0;n<20u;++n) {
        advance_clock(100u);(void)kui_toy_pilot_sci_service(NULL);untouched(&audio0);
    }
    CHECK(wire.fetches==reads && !wire.flight && !hw.leased);
    for(uint32_t lba=4u;lba<8u;++lba) {
        CHECK(audio_visit(lba,90u,&audio0)==KUI_TOY_SCI_OK);audio_matches(lba,&audio0);
        /* Repeated consumption cannot replace a reused PCM buffer. */
        memset(audio0.bytes,0x73,sizeof(audio0.bytes));
        CHECK(audio_visit(lba,90u,&audio0)==KUI_TOY_SCI_OK);
        for(unsigned i=0;i<sizeof(audio0.bytes);++i)CHECK(audio0.bytes[i]==0x73u);
        clear_audio(&audio0);
    }
    /* Consuming a ready head may arm a successor but cannot receive its
     * unelapsed physical payload or immediately manufacture a fifth head. */
    CHECK(audio_visit(8u,90u,&audio0)==KUI_TOY_SCI_PENDING);untouched(&audio0);
    advance_irq_to(hw.ticks+10000u);
    for(uint32_t lba=8u;lba<12u;++lba) {
        CHECK(audio_visit(lba,90u,&audio0)==KUI_TOY_SCI_OK);audio_matches(lba,&audio0);
        clear_audio(&audio0);
    }
    reads=wire.fetches;advance_irq_to(hw.ticks+10000u);
    CHECK(!wire.flight && !hw.leased && wire.fetches==reads);
    CHECK(audio_visit(12u,90u,&audio0)==KUI_TOY_SCI_FAULT);untouched(&audio0);
    audio_cleanup();

    setup(false,true);clear_audio(&audio0);hw.tick_step=0u;wire.receiver_delay=80u;
    plan(4u,8u,92u,0u);advance_irq_to(10000u);
    for(uint32_t lba=4u;lba<8u;++lba) {
        CHECK(audio_visit(lba,92u,&audio0)==KUI_TOY_SCI_OK);audio_matches(lba,&audio0);
        clear_audio(&audio0);
    }
    reads=wire.fetches;
    CHECK(audio_visit(8u,92u,&audio0)==KUI_TOY_SCI_FAULT);
    /* LBA8 is mapped audio in the same track but outside this plan. */
    advance_irq_to(hw.ticks+10000u);
    CHECK(wire.fetches==reads && !wire.flight && !hw.leased);untouched(&audio0);
    audio_cleanup();
}
static void queued_masked_irq_foreground_plan(void) {
    setup(false,true);clear_audio(&audio0);hw.tick_step=10u;wire.receiver_delay=80u;
    plan(4u,8u,95u,0u);
    for(unsigned n=0;n<30u && (wire.flight || hw.leased);++n) {
        advance_clock(100u);
        uint32_t blocks=kui_toy_pilot_sci_snapshot()->call_blocks;
        unsigned budgets=wire.token_budgets;
        CHECK(kui_toy_pilot_sci_service(NULL)!=KUI_TOY_SCI_FAULT);
        CHECK(kui_toy_pilot_sci_snapshot()->call_blocks-blocks<=KUI_TOY_SCI_SERVICE_BLOCKS);
        CHECK(wire.token_budgets==budgets+1u && !wire.waits);
        untouched(&audio0);
    }
    CHECK(!hw.leased && !wire.flight && !kui_toy_pilot_sci_snapshot()->irq_calls);
    for(uint32_t lba=4u;lba<8u;++lba) {
        CHECK(audio_visit(lba,95u,&audio0)==KUI_TOY_SCI_OK);audio_matches(lba,&audio0);
        clear_audio(&audio0);
    }
    audio_cleanup();
}
static void queued_range_replacement_and_cancellation(void) {
    /* Test both half-written physical DMA and a full, unconsumed queue.
     * STOP, seek and generation replacement must discard all older bytes. */
    for(unsigned ready=0;ready<2u;++ready)for(unsigned action=0;action<3u;++action) {
        setup(false,true);clear_audio(&audio0);hw.tick_step=0u;wire.receiver_delay=80u;
        plan(4u,12u,101u,0u);
        if(ready) {advance_irq_to(10000u);CHECK(!wire.flight && !hw.leased);}
        else {advance_clock(40u);CHECK(wire.partial_bytes==64u && wire.flight);}
        unsigned reads=wire.fetches;
        if(action==0u) {
            kui_toy_pilot_sci_audio_cancel();advance_clock(1000u);
            (void)kui_toy_pilot_sci_irq();(void)kui_toy_pilot_sci_service(NULL);
            CHECK(!wire.flight && !hw.leased && wire.fetches==reads);untouched(&audio0);
            plan(8u,12u,102u,0u);
        } else {
            /* Same-generation nonsequential seek is also a replacement. */
            plan(8u,12u,action==1u?102u:101u,0u);
        }
        CHECK(audio_visit(8u,action==2u?101u:102u,&audio0)==KUI_TOY_SCI_PENDING);
        advance_irq_to(hw.ticks+10000u);
        for(uint32_t lba=8u;lba<12u;++lba) {
            CHECK(audio_visit(lba,action==2u?101u:102u,&audio0)==KUI_TOY_SCI_OK);
            audio_matches(lba,&audio0);clear_audio(&audio0);
        }
        audio_cleanup();
    }
    static const uint32_t invalid[][3]={{4u,4u,110u},{4u,13u,110u},
        {45000u,45001u,110u},{4u,12u,0u},{UINT32_MAX,0u,110u}};
    for(unsigned n=0;n<sizeof(invalid)/sizeof(invalid[0]);++n) {
        setup(false,true);clear_audio(&audio0);hw.tick_step=0u;wire.receiver_delay=80u;
        plan(4u,12u,109u,0u);advance_clock(40u);
        CHECK(kui_toy_pilot_sci_audio_plan(invalid[n][0],invalid[n][1],invalid[n][2],0u)==KUI_TOY_SCI_FAULT);
        unsigned reads=wire.fetches;advance_clock(1000u);
        (void)kui_toy_pilot_sci_irq();(void)kui_toy_pilot_sci_service(NULL);
        CHECK(!wire.flight && !hw.leased && wire.fetches==reads);untouched(&audio0);
        audio_cleanup();
    }
}
static unsigned sustained_queued_schedule(bool occasional_gap,bool permanent_gap) {
    setup_long_audio(false,true);
    uint32_t lba=4u,last=0u,generation=121u;
    int32_t reserve=8192,min_reserve=reserve;
    unsigned deliveries=0u,max_burst=0u;bool saw_catchup=false,gap_resumed=false;
    const uint32_t gap_start=29u*781250u/60u+78u,gap_end=gap_start+62500u;
    plan(lba,164u,generation,(uint32_t)reserve);
    for(unsigned interval=0;;++interval) {
        uint32_t first=permanent_gap?interval*62500u:
            (uint32_t)((uint64_t)interval*781250u/60u);
        if(first>=2u*781250u)break;
        /* One bounded absence around 0.5 s, followed by normal cadence.
         * It is not a claim that four queue slots alone cover 80 ms. */
        if(occasional_gap && first>gap_start && first<gap_end)continue;
        if(occasional_gap && first>=gap_end && !gap_resumed) {first=gap_end;gap_resumed=true;}
        for(unsigned clustered=0;clustered<(permanent_gap?1u:2u);++clustered) {
            uint32_t when=first+clustered*78u;
            if(when>=2u*781250u)break;
            advance_irq_to(when);
            reserve-=(int32_t)(((uint64_t)when*44100u/781250u)-((uint64_t)last*44100u/781250u));
            last=when;if(reserve<min_reserve)min_reserve=reserve;
            if(!permanent_gap)CHECK(reserve>0);
            plan(lba,164u,generation,reserve>0?(uint32_t)reserve:0u);
            unsigned burst=0u;
            while(burst<4u && reserve<=8192-588) {
                int result=audio_visit(lba,generation,&audio0);CHECK(result!=KUI_TOY_SCI_FAULT);
                if(result==KUI_TOY_SCI_PENDING)break;
                audio_matches(lba,&audio0);clear_audio(&audio0);++lba;++deliveries;++burst;reserve+=588;
            }
            if(burst>max_burst)max_burst=burst;
            if(occasional_gap && first>=gap_end && burst>1u)saw_catchup=true;
            plan(lba,164u,generation,reserve>0?(uint32_t)reserve:0u);
        }
    }
    advance_irq_to(2u*781250u);
    reserve-=(int32_t)(2u*44100u-(uint64_t)last*44100u/781250u);
    if(!permanent_gap) {
        /* Score an actual final worker visit, rather than demanding sectors
         * whose playback deadline occurs after the final 60-Hz visit. */
        plan(lba,164u,generation,(uint32_t)reserve);
        for(unsigned n=0;n<4u && reserve<=8192-588;++n) {
            CHECK(audio_visit(lba,generation,&audio0)==KUI_TOY_SCI_OK);
            audio_matches(lba,&audio0);clear_audio(&audio0);++lba;++deliveries;reserve+=588;
        }
    }
    if(!permanent_gap) {
        if(deliveries<149u || reserve<8192-1176 || max_burst<2u)
            fprintf(stderr,"Queued cadence diagnostics: gap=%u deliveries=%u reserve=%d min=%d burst=%u lba=%u\n",
                occasional_gap,deliveries,reserve,min_reserve,max_burst,lba);
        CHECK(deliveries>=149u && reserve>=8192-1176 && min_reserve>0);
        CHECK(max_burst>=2u && (!occasional_gap || saw_catchup));
    } else CHECK(deliveries<=25u*4u && reserve<0);
    printf("%s %u sectors/2 s, largest worker burst %u, final reserve %d frames\n",
        permanent_gap?"Queued SCI sparse-cadence limit:":occasional_gap?"Queued SCI bounded-gap:":
        "Queued SCI sustained 60-Hz:",deliveries,max_burst,reserve);
    audio_cleanup();return deliveries;
}
static void sustained_delivery_with_multi_sector_worker(void) {
    (void)sustained_queued_schedule(false,false);
    (void)sustained_queued_schedule(true,false);
    (void)sustained_queued_schedule(false,true);
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
    queued_prefetch_and_exact_range(); queued_masked_irq_foreground_plan();
    queued_range_replacement_and_cancellation();
    sustained_delivery_with_multi_sector_worker(); legacy_raw_lease_is_not_admitted();
    supersession_cancel_and_private_partial_dma(); high_core_data_revoke_preserves_audio_identity();
    crc_retry_pio_and_failed_sector();
    printf("Shared asynchronous SCI audio: %u checks passed\n",checks);
    return 0;
}
