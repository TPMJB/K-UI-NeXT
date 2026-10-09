/* SPDX-License-Identifier: GPL-3.0-only */
/* The production eight-block worker against an independent physical PCM
 * consumer and an asynchronously scheduled complete-sector source. The
 * source never advances time or calls the synchronous raw callback. */
#define KUI_TOY_PILOT_SHARED_SCI 1
#define KUI_TOY_PILOT_ASYNC_CDDA 1
#include "toy_pilot_async_fixture.h"

uint8_t __toy_pilot_gd_stack_bottom[64],__toy_pilot_gd_stack_top[64];
static struct kui_toy_pilot_sci_stats transport;
struct async_receiver {
    uint32_t lba,generation,ready_tick;
    void *output;
    bool active,completed;
};
static struct async_receiver receiver;
static uint32_t sector_delay,attempts,requests,completions,cancels,synchronous_reads;
static bool transport_fault;

void kui_toy_pilot_sci_init(const struct kui_retail_manifest *m,
    const struct kui_loader_sd *c,enum kui_loader_sd_result (*a)(void),void (*r)(void)) {
    (void)m;(void)c;(void)a;(void)r;
}
int kui_toy_pilot_sci_pump(struct kui_retail_gd *s) { (void)s;return KUI_TOY_SCI_PENDING; }
int kui_toy_pilot_sci_service(struct kui_retail_gd *s) { (void)s;return KUI_TOY_SCI_PENDING; }
void kui_toy_pilot_sci_cancel(struct kui_retail_gd *s) { (void)s;++cancels;receiver.active=false; }
int kui_toy_pilot_sci_audio_acquire(void) {
    assert(!"The async worker must not request the synchronous raw lease");return KUI_TOY_SCI_FAULT;
}
void kui_toy_pilot_sci_audio_release(void) {
    assert(!"The async engine owns and releases its own physical lease");
}
void kui_toy_pilot_sci_audio_cancel(void) {
    ++cancels;memset(&receiver,0,sizeof(receiver));
}
const struct kui_toy_pilot_sci_stats *kui_toy_pilot_sci_snapshot(void) { return &transport; }
uint32_t kui_toy_pilot_sci_irq(void) { return 1u; }
static int forbidden_raw(uint32_t lba,uint32_t sectors,void *out) {
    (void)lba;(void)sectors;(void)out;++synchronous_reads;
    assert(!"The async worker invoked the low synchronous raw callback");return -1;
}
int kui_toy_pilot_sci_audio_read(uint32_t lba,uint32_t generation,void *output) {
    assert((sr&0xf0u)==0xf0u && output==owner.raw && !((uintptr_t)output&31u));
    assert(data_command==16u || data_command==17u);
    assert(generation==owner.model.generation && generation==owner.mailbox.generation);
    uint32_t entered=now;++attempts;
    if(transport_fault) return KUI_TOY_SCI_FAULT;
    if(!receiver.active || receiver.lba!=lba || receiver.generation!=generation || receiver.output!=output) {
        receiver=(struct async_receiver){.lba=lba,.generation=generation,.ready_tick=now+sector_delay,
            .output=output,.active=true};
        ++requests;
        assert(now==entered);return KUI_TOY_SCI_PENDING;
    }
    if(receiver.completed) { assert(now==entered);return KUI_TOY_SCI_OK; }
    /* Only independently elapsed game time makes the private source ready.
     * Intermediate verified blocks are never exposed to the worker. */
    if((int32_t)(now-receiver.ready_tick)<0) { assert(now==entered);return KUI_TOY_SCI_PENDING; }
    assert(lba>=50u && lba<50u+source_frames/588u && !stop_queued);
    if(strict_source_reads) {
        assert(lba==expected_read_lba);++expected_read_lba;
        if(expected_read_lba==50u+source_frames/588u && repeat_pcm) expected_read_lba=50u;
    }
    uint8_t *p=output;
    for(unsigned i=0;i<588u;i++) {
        uint16_t left=(uint16_t)((lba-50u)*588u+i),right=(uint16_t)~left;
        p[i*4u]=(uint8_t)left;p[i*4u+1u]=(uint8_t)(left>>8);
        p[i*4u+2u]=(uint8_t)right;p[i*4u+3u]=(uint8_t)(right>>8);
    }
    receiver.completed=true;++completions;++reads;copies_at_last_read=copies;
    assert(now==entered);return KUI_TOY_SCI_OK;
}
static void prepare_audio(uint32_t command,uint32_t sectors) {
    memset(&receiver,0,sizeof(receiver));memset(&transport,0,sizeof(transport));
    attempts=requests=completions=cancels=synchronous_reads=0u;transport_fault=false;
    sector_delay=1000000u;prepare(sectors);data_command=command;
    owner.config.read_raw=(uint32_t)(uintptr_t)forbidden_raw;
    for(unsigned n=0;n<20u;n++) visit(13021u);
    assert(owner.stats.state==KUI_TOY_PILOT_PREFILL && !owner.stop_wait && receiver.active && attempts);
    assert(!owner.stats.fault && !owner.stats.raw_errors && !reports && !reads && !copies);
    assert(owner.raw_request_generation==owner.model.generation && !owner.raw_generation);
}
static void finish_sector(void) {
    uint32_t previous=reads,frame=owner.fill_frame,stream=owner.ring_fill_stream,entry=sr;
    uint32_t calls=owner.stats.raw_calls,timing=owner.stats.raw_read_timing_calls;
    if(!receiver.active || receiver.completed) {
        fill_quantum();
        assert(reads==previous && owner.fill_frame==frame && owner.ring_fill_stream==stream);
    }
    assert(receiver.active && !receiver.completed);
    advance(sector_delay+1u);fill_quantum();
    assert(sr==entry && !owner.stats.fault && !reports && reads==previous+1u);
    assert(owner.stats.raw_calls==calls+1u && owner.stats.raw_read_timing_calls==timing+1u);
    assert(owner.stats.raw_bytes==reads*2352u && owner.raw_generation==owner.model.generation);
    assert(owner.raw_request_generation==0u && owner.fill_frame==frame+588u && owner.ring_fill_stream==stream+588u);
    assert(owner.stats.raw_read_ticks_last>=sector_delay && !synchronous_reads);
}
static void pending_then_stereo_delivery(uint32_t command) {
    prepare_audio(command,200u);
    if(command==17u) sr=entry_sr=UINT32_C(0x60000091);
    uint8_t initial_raw[2352];memcpy(initial_raw,owner.raw,sizeof(initial_raw));
    uint32_t before_attempts=attempts,frame=owner.fill_frame,stream=owner.ring_fill_stream,entry=sr;
    for(unsigned n=0;n<40u;n++) {
        fill_quantum();
        assert(sr==entry && attempts==before_attempts+n+1u && !owner.stats.fault && !owner.stats.raw_errors);
        assert(!reads && !copies && !owner.stats.raw_calls && !owner.stats.raw_bytes && !owner.stats.raw_read_timing_calls);
        assert(owner.fill_frame==frame && owner.ring_fill_stream==stream && owner.model.banks[0].filled==0u);
        assert(!owner.raw_generation && !memcmp(owner.raw,initial_raw,sizeof(initial_raw)));
    }
    strict_source_reads=true;expected_read_lba=50u;
    finish_sector();sector_delay=200u;
    for(unsigned n=1u;n<7u;n++) finish_sector();
    assert(requests==7u && completions==7u && reads==7u && !synchronous_reads);
    assert(owner.model.banks[0].state==KUI_TOY_PILOT_BANK_READY && owner.model.banks[0].filled==4096u);
    assert(owner.model.banks[1].state==KUI_TOY_PILOT_BANK_FILLING && owner.model.banks[1].filled==20u);
    for(uint32_t f=0;f<7u*588u;f++) {
        assert((uint16_t)sample(owner.stats.sound_address+f*2u)==(uint16_t)f);
        assert((uint16_t)sample(owner.stats.sound_address+65536u+f*2u)==(uint16_t)~f);
    }
    /* The second fragment at the 4096-frame boundary consumed the cached
     * tail, without another transport request or completed-sector count. */
    assert(owner.stats.raw_calls==7u && owner.stats.raw_read_timing_calls==7u);
}
static void cancellation_and_generation(void) {
    const uint32_t controls[]={CMD_PAUSE,CMD_STOP,KUI_TOY_PILOT_RESET};
    for(unsigned c=0;c<sizeof(controls)/sizeof(*controls);++c) {
        prepare_audio(16u,200u);uint32_t before=cancels,entry=sr,old_attempts=attempts;
        uint8_t initial_raw[2352];memcpy(initial_raw,owner.raw,sizeof(initial_raw));
        assert(kui_toy_pilot_request(controls[c],0u,0u,0u)==3u && cancels==before+1u);
        assert(!receiver.active && !owner.raw_request_generation);
        advance(sector_delay+1u);fill_quantum();
        assert(sr==entry && !reads && !copies && attempts==old_attempts && !owner.stats.raw_calls);
        assert(!memcmp(owner.raw,initial_raw,sizeof(initial_raw)));
    }
    prepare_audio(17u,200u);uint32_t entry=sr,old_attempts=attempts,before=cancels;
    kui_toy_pilot_worker_revoke();assert(sr==entry && cancels==before+1u && !receiver.active);
    advance(sector_delay+1u);fill_quantum();
    assert(!reads && !copies && attempts==old_attempts && !owner.raw_request_generation);

    prepare_audio(16u,200u);before=cancels;
    assert(kui_toy_pilot_request(CMD_PLAY,2u,2u,0u)==3u && cancels==before+1u);
    for(unsigned n=0;n<100u && (!receiver.active || receiver.generation!=3u);n++) visit(13021u);
    assert(receiver.active && receiver.generation==3u && receiver.lba==50u);
    assert(owner.model.generation==3u && owner.raw_request_generation==3u && !reads && !copies);
    strict_source_reads=true;expected_read_lba=50u;finish_sector();
    assert(reads==1u && owner.raw_generation==3u && completions==1u);

    /* Mandatory revoke can arrive immediately before the SR publication.
     * A ready old completion still cannot reach raw/PCM after that fence. */
    prepare_audio(16u,200u);advance(sector_delay+1u);entry=sr;old_attempts=attempts;
    revoke_before_mask=true;fill_quantum();
    assert(sr==entry && owner.disabled && diagnostic_revokes==1u);
    assert(!reads && !copies && attempts==old_attempts && !receiver.active && !owner.raw_request_generation);
}
static void transport_failure(void) {
    prepare_audio(16u,200u);uint32_t entry=sr;transport_fault=true;
    fill_quantum();
    assert(sr==entry && owner.stats.fault==KUI_TOY_PILOT_FAULT_CARD && owner.stats.raw_errors==1u);
    assert(!reads && !copies && !owner.stats.raw_calls && !owner.stats.raw_read_timing_calls);
    assert(!receiver.active && !owner.raw_request_generation && !synchronous_reads);

    /* A driver/clock fault can happen while a valid RAW receive is still
     * pending. Disable must revoke that tuple and its physical lease too,
     * before a ready old completion can ever reach raw or PCM. */
    prepare_audio(17u,200u);uint32_t before=cancels,old_attempts=attempts;
    uint8_t initial_raw[2352];memcpy(initial_raw,owner.raw,sizeof(initial_raw));
    visit(CLOCK_GAP_LIMIT+1u);
    assert(sr==entry_sr && owner.disabled && owner.stats.fault==KUI_TOY_PILOT_FAULT_CLOCK && reports==1u);
    assert(cancels==before+1u && !receiver.active && !owner.raw_request_generation);
    fill_quantum();
    assert(attempts==old_attempts && !reads && !copies && !owner.stats.raw_calls);
    assert(!memcmp(owner.raw,initial_raw,sizeof(initial_raw)));
}
static void physical_pcm_to_eof(void) {
    prepare_audio(17u,200u);sector_delay=200u;receiver.ready_tick=now+sector_delay;
    strict_source_reads=true;expected_read_lba=50u;ready();
    for(unsigned n=0;n<2000u && owner.stats.state!=KUI_TOY_PILOT_EOF;n++) {
        visit(6510u);assert(!owner.stats.fault && !reports);
    }
    assert(owner.stats.state==KUI_TOY_PILOT_EOF && !running && checked_frames>=source_frames);
    assert(reads==200u && completions==200u && owner.stats.raw_calls==200u);
    assert(owner.stats.raw_read_timing_calls==200u && owner.stats.raw_bytes==200u*2352u);
    assert(starts==1u && !owner.stats.raw_errors && !synchronous_reads);
}
int main(void) {
    pending_then_stereo_delivery(16u);pending_then_stereo_delivery(17u);
    cancellation_and_generation();transport_failure();physical_pcm_to_eof();
    puts("Async CDDA worker: delayed complete-sector delivery, exact SR, unchanged pending PCM, cancellation epochs, cached block tails and physical stereo EOF pass");
    return 0;
}
