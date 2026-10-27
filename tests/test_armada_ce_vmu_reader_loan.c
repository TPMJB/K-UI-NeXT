/* SPDX-License-Identifier: GPL-3.0-only */
/* Real CE reader/GD core and the byte-level CMD18/CMD12 card model. This
 * includes the ordinary-reader regression fixture, not a second loan model. */
#define main baseline_reader_main
#include "test_retail_async_ce.c"
#undef main

volatile uint32_t kui_retail_hook_active,kui_retail_hook_fault;
volatile uint32_t kui_retail_ce_deferred[2];
static uint32_t actual_sr;
static bool release_fails,inject_request;
static unsigned releases,stop_bytes;
static int32_t injected_token;
uint32_t kui_retail_async_vmu_test_sr(void) { return actual_sr; }
void kui_retail_async_vmu_test_set_sr(uint32_t sr) { actual_sr=sr; }
void kui_retail_ce_vmu_loan_wake(void) { take_events(); }
void kui_sci_sd_release(void) {
    CHECK((actual_sr&0xf0u)==0xf0u);
    CHECK(R.shared.port.acquired && !kui_sci_stream_busy());
    CHECK(R.shared.stream.state==0u);
    ++releases;R.shared.port.acquired=false;
    if(release_fails) R.shared.port.fault=true;
}
static uint8_t loan_transfer(void *context,uint8_t value,bool slow) {
    if(R.engine.vmu_loan_state==KUI_ARMADA_CE_VMU_LOAN_STOPPING) {
        CHECK(!(actual_sr&0x100000f0u) && !kui_retail_hook_active);
        ++stop_bytes;
        if(inject_request) {
            inject_request=false;
            unsigned before=bus_bytes;
            injected_token=request(KUI_GD_PIOREAD,45030u,2u,VBASE+0x800u);
            CHECK(status(injected_token)==KUI_GD_PROCESSING);
            CHECK(gd(KUI_GD_EXEC,0u,0u)==0);
            CHECK(bus_bytes==before && !R.engine.active);
            CHECK(R.shared.service.lba==45030u && R.shared.service.count==2u);
            put(PARAM,45120u+150u);put(PARAM+4u,1u);
            CHECK(R.shared.service.lba==45030u && R.shared.service.count==2u);
        }
    }
    return bus_transfer(context,value,slow);
}
static void prepare(bool opened) {
    actual_sr=0u;kui_retail_hook_active=kui_retail_hook_fault=0u;
    kui_retail_ce_deferred[0]=kui_retail_ce_deferred[1]=0u;
    release_fails=inject_request=false;injected_token=0;stop_bytes=releases=0;
    setup(2000u,true);mode(2048u);
    if(opened) {
        int32_t token=request(KUI_GD_DMAREAD,45001u,1u,OUTPUT);
        CHECK(finish(token,8u)==KUI_GD_COMPLETED);
        CHECK(!R.shared.service.command && !R.engine.active && !R.engine.piece_set);
        CHECK(R.engine.opened && R.shared.port.acquired);
    }
    R.shared.card.device.sd.bus.transfer=loan_transfer;
}
static void test_stop_preemption_and_real_pending_token(void) {
    prepare(true);
    unsigned stop=card.cmd12,acquires=hw.acquires;
    struct kui_loader_sd copied={0};inject_request=true;
    CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_OK);
    CHECK(actual_sr==0u && stop_bytes && releases==1u && card.cmd12==stop+1u);
    CHECK(!R.engine.opened && !R.shared.port.acquired && !kui_retail_hook_active);
    CHECK(copied.ready && copied.blocks==2048u && copied.high_capacity);
    CHECK(R.engine.vmu_loan_state==KUI_ARMADA_CE_VMU_LOAN_WRITER);
    CHECK(R.shared.service.storage_deferred && injected_token>0);
    unsigned before=bus_bytes;
    for(unsigned i=0;i<8u;++i) {
        CHECK(status(injected_token)==KUI_GD_PROCESSING);
        CHECK(get(STATUS+12u)==1u && !get(STATUS+8u));
        CHECK(gd(KUI_GD_EXEC,0u,0u)==0);
        CHECK(kui_retail_async_irq()==1u);
    }
    CHECK(bus_bytes==before && hw.acquires==acquires && !hw.virtual_writes);
    untouched(vram+0x800u,4096u);
    unsigned drive=ev.drive;
    CHECK(kui_retail_async_vmu_loan_end(true)==KUI_ARMADA_CE_VMU_LOAN_OK);
    CHECK(!R.shared.service.storage_deferred && !R.engine.vmu_loan_state);
    CHECK(actual_sr==0u && ev.drive>drive && bus_bytes==before);
    CHECK(finish(injected_token,8u)==KUI_GD_COMPLETED);
    CHECK(hw.acquires==acquires+1u);
    reference_read(45030u,2u);CHECK(!memcmp(vram+0x800u,expected,4096u));
}
static void test_dma_transfer_metadata_during_loan(void) {
    prepare(false);struct kui_loader_sd copied={0};
    CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_OK);
    unsigned before=bus_bytes;
    int32_t token=request(KUI_RETAIL_GD_DMAREAD_STREAM,45010u,2u,0u);
    put(PIECE,OUTPUT&0x1fffffffu);put(PIECE+4u,4096u);
    CHECK(gd(KUI_GD_DMA_TRANSFER,(uint32_t)token,PIECE)==0);
    CHECK(gd(KUI_GD_DMA_CHECK,0u,STATUS)==1 && get(STATUS)==4096u);
    CHECK(status(token)==KUI_RETAIL_GD_STREAMING && bus_bytes==before);
    CHECK(kui_retail_async_vmu_loan_end(true)==KUI_ARMADA_CE_VMU_LOAN_OK);
    for(unsigned i=0;i<10000u && R.shared.service.pending;++i) {
        (void)interrupts(8u);elapse();(void)gd(KUI_GD_DMA_CHECK,0u,STATUS);
    }
    CHECK(status(token)==KUI_GD_COMPLETED);
    reference_read(45010u,2u);CHECK(!memcmp(ram+OUTPUT-BEGIN,expected,4096u));
}
static void test_pio_offer_and_abort_reset(void) {
    static const uint32_t cancel[]={KUI_GD_ABORT,KUI_GD_INIT,KUI_GD_RESET};
    for(unsigned i=0;i<3u;++i) {
        prepare(false);struct kui_loader_sd copied={0};
        CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_OK);
        int32_t token=request(KUI_RETAIL_GD_PIOREAD_STREAM,45010u,2u,0u);
        unsigned before=bus_bytes;
        CHECK(gd(KUI_GD_PIO_CHECK,(uint32_t)token,STATUS)==0 && !get(STATUS));
        put(PIECE,VBASE);put(PIECE+4u,4096u);
        CHECK(gd(KUI_GD_PIO_TRANSFER,(uint32_t)token,PIECE)==-1);
        CHECK(!R.shared.service.completed_bytes && !R.shared.service.callback_due);
        CHECK(gd(cancel[i],(uint32_t)token,0u)==0);
        CHECK(R.shared.service.storage_deferred && bus_bytes==before);
        CHECK(kui_retail_async_vmu_loan_end(true)==KUI_ARMADA_CE_VMU_LOAN_OK);
        CHECK(!R.shared.service.storage_deferred && !R.engine.active);
    }
}
static void test_idle_context_and_immutable_busy(void) {
    struct kui_loader_sd copied,before;
    prepare(false);memset(&copied,0xa5,sizeof(copied));before=copied;
    for(unsigned bit=4u;bit<8u;++bit) {
        actual_sr=1u<<bit;unsigned bytes=bus_bytes;
        CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_CONTEXT);
        CHECK(bus_bytes==bytes && !memcmp(&copied,&before,sizeof(copied)));
    }
    actual_sr=0x10000000u;
    CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_CONTEXT);
    actual_sr=0u;
    uint32_t *busy[]={&R.shared.service.pending,&R.shared.service.executing,
        &R.shared.service.callback_due,&R.engine.active,&R.engine.spill_bytes,&R.engine.in_irq};
    for(unsigned i=0;i<sizeof(busy)/sizeof(busy[0]);++i) {
        *busy[i]=1u;unsigned bytes=bus_bytes;
        CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_BUSY);
        CHECK(bus_bytes==bytes && !R.engine.vmu_loan_state && !kui_retail_hook_active);
        CHECK(!memcmp(&copied,&before,sizeof(copied)));*busy[i]=0u;
    }
    kui_retail_hook_active=1u;
    CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_BUSY);
    CHECK(kui_retail_hook_active==1u);kui_retail_hook_active=0u;
    R.engine.piece_set=1u;
    CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_BUSY);
    R.engine.piece_set=0u;
    kui_retail_ce_deferred[0]=1u;
    CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_BUSY);
    kui_retail_ce_deferred[0]=0u;
    CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_OK);
    CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_BUSY);
    actual_sr=0xf0u;
    CHECK(kui_retail_async_vmu_loan_end(true)==KUI_ARMADA_CE_VMU_LOAN_CONTEXT);
    CHECK(R.engine.vmu_loan_state==KUI_ARMADA_CE_VMU_LOAN_WRITER);
    actual_sr=0u;CHECK(kui_retail_async_vmu_loan_end(true)==KUI_ARMADA_CE_VMU_LOAN_OK);
}
static void test_quarantine_stop_release_and_provider_fault(void) {
    for(unsigned fault=0;fault<3u;++fault) {
        prepare(fault<2u);struct kui_loader_sd copied={0};
        if(fault==0u) m.healthy=false;
        if(fault==1u) release_fails=true;
        enum kui_armada_ce_vmu_loan_result result=kui_retail_async_vmu_loan_begin(&copied);
        if(fault<2u) CHECK(result==KUI_ARMADA_CE_VMU_LOAN_FAULT);
        else {
            CHECK(result==KUI_ARMADA_CE_VMU_LOAN_OK);
            (void)request(KUI_GD_PIOREAD,45010u,2u,VBASE);
            CHECK(kui_retail_async_vmu_loan_end(false)==KUI_ARMADA_CE_VMU_LOAN_FAULT);
        }
        CHECK(R.engine.vmu_loan_state==KUI_ARMADA_CE_VMU_LOAN_QUARANTINED);
        CHECK(!R.shared.card.device.sd.ready && kui_retail_hook_fault);
        unsigned bytes=bus_bytes;
        CHECK(gd(KUI_GD_RESET,0u,0u)==0);
        int32_t token=request(KUI_GD_PIOREAD,45010u,2u,VBASE);
        CHECK(status(token)==KUI_GD_FAILED);
        CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_FAULT);
        CHECK(kui_retail_async_irq()==1u && bus_bytes==bytes);
    }
}
static void test_active_receiver_and_release_invariants(void) {
    prepare(false);struct kui_loader_sd copied,before;
    memset(&copied,0xa5,sizeof(copied));before=copied;
    int32_t token=request(KUI_GD_DMAREAD,45010u,2u,OUTPUT);
    unsigned bytes=bus_bytes;
    CHECK(kui_sci_stream_busy());
    CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_BUSY);
    CHECK(bus_bytes==bytes && !memcmp(&copied,&before,sizeof(copied)));
    CHECK(finish(token,8u)==KUI_GD_COMPLETED);
    CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_OK);
    CHECK(kui_retail_async_vmu_loan_end(true)==KUI_ARMADA_CE_VMU_LOAN_OK);
    CHECK(kui_retail_async_vmu_loan_end(true)==KUI_ARMADA_CE_VMU_LOAN_FAULT);
    CHECK(R.engine.vmu_loan_state==KUI_ARMADA_CE_VMU_LOAN_QUARANTINED);
    for(unsigned fault=0u;fault<5u;++fault) {
        prepare(false);
        CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_OK);
        switch(fault) {
        case 0u:R.shared.port.acquired=true;break;
        case 1u:R.shared.port.fault=true;break;
        case 2u:R.shared.stream.state=1u;break;
        case 3u:R.shared.service.storage_deferred=0u;break;
        default:R.engine.vmu_loan_generation=0u;break;
        }
        bytes=bus_bytes;
        CHECK(kui_retail_async_vmu_loan_end(true)==KUI_ARMADA_CE_VMU_LOAN_FAULT);
        CHECK(R.engine.vmu_loan_state==KUI_ARMADA_CE_VMU_LOAN_QUARANTINED && bus_bytes==bytes);
    }
    prepare(false);R.engine.vmu_loan_generation=UINT32_MAX;bytes=bus_bytes;
    CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_FAULT);
    CHECK(R.engine.vmu_loan_state==KUI_ARMADA_CE_VMU_LOAN_QUARANTINED && bus_bytes==bytes);
}
static void test_retained_terminal_handle_can_loan(void) {
    for(unsigned failed=0u;failed<2u;++failed) {
        prepare(false);struct kui_loader_sd copied={0};
        int32_t token=request(KUI_GD_DMAREAD,45010u,2u,OUTPUT);
        if(failed) CHECK(gd(KUI_GD_ABORT,(uint32_t)token,0u)==0);
        for(unsigned i=0u;i<1000u && (R.shared.service.pending || kui_sci_stream_busy());++i) {
            (void)interrupts(8u);elapse();
        }
        CHECK(!R.shared.service.pending && R.shared.service.command && !R.engine.active);
        CHECK(!kui_sci_stream_busy() && !R.engine.spill_bytes && !R.engine.piece_direct);
        if(!failed) {
            CHECK(R.engine.piece_set && R.engine.piece_bytes==4096u &&
                R.engine.piece_filled==R.engine.piece_bytes);
            /* A retained terminal handle does not excuse an unfilled piece. */
            --R.engine.piece_filled;unsigned before=bus_bytes;
            CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_BUSY);
            CHECK(bus_bytes==before && !R.engine.vmu_loan_state);
            ++R.engine.piece_filled;
        }
        struct kui_retail_gd retained=R.shared.service;
        retained.storage_deferred=1u;
        CHECK(kui_retail_async_vmu_loan_begin(&copied)==KUI_ARMADA_CE_VMU_LOAN_OK);
        CHECK(!memcmp(&R.shared.service,&retained,sizeof(retained)));
        CHECK(!R.engine.opened && !R.shared.port.acquired && !kui_retail_hook_active);
        unsigned before=bus_bytes;
        CHECK(status(token)==(failed?KUI_GD_FAILED:KUI_GD_COMPLETED));
        CHECK(get(STATUS+4u)==(failed?KUI_GD_ERROR_CANCELLED:0u));
        CHECK(!R.shared.service.command && bus_bytes==before);
        CHECK(kui_retail_async_vmu_loan_end(true)==KUI_ARMADA_CE_VMU_LOAN_OK);
        CHECK(bus_bytes==before && !R.engine.vmu_loan_state);
        if(!failed) {
            reference_read(45010u,2u);CHECK(!memcmp(ram+OUTPUT-BEGIN,expected,4096u));
        }
    }
}
int main(void) {
    baseline_reader_main();
    test_stop_preemption_and_real_pending_token();
    test_dma_transfer_metadata_during_loan();
    test_pio_offer_and_abort_reset();
    test_idle_context_and_immutable_busy();
    test_quarantine_stop_release_and_provider_fault();
    test_active_receiver_and_release_invariants();
    test_retained_terminal_handle_can_loan();
    printf("CE VMU SCI loan: %u checks; unmasked stop, real pending GD tokens, copied parameters, lazy resume, zero-I/O loan, PIO offers and fault quarantine passed\n",checks);
    return 0;
}
