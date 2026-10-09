/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual worker with the existing independent ARM/PCM fixture. Redirect only
 * its copy call so every upload is checked before the fixture enforces the
 * physical ownership rules. The fixture's ramp-producing raw callback remains
 * installed: calling it would fail both the zero-data and zero-read checks. */
#define KUI_TOY_PILOT_WORKER_TEST 1
#define kui_toy_pilot_bus_copy synthetic_checked_copy
#include "../src/loader/toy_pilot_worker.c"
#undef kui_toy_pilot_bus_copy
#define TOY_WORKER_SOURCE "../include/kui/toy_pilot.h"
#include "toy_pilot_async_fixture.h"

#if !KUI_TOY_PILOT_SYNTHETIC_SOURCE
#error This regression requires the explicitly enabled synthetic source profile
#endif

static uint32_t observed_requests,next_lba,track_first_lba,track_end_lba;
static uint32_t checked_upload_bytes;
static bool source_loops;

enum kui_toy_pilot_bus_result synthetic_checked_copy(uint32_t address,
    const void *source,uint32_t bytes) {
    const uint8_t *p=source;
    assert(!reads && bytes && !(bytes&3u));
    for(uint32_t i=0u;i<bytes;i++) assert(!p[i]);
    checked_upload_bytes+=bytes;
    if(owner.stats.raw_calls!=observed_requests) {
        assert(owner.stats.raw_calls==observed_requests+1u);
        assert(owner.raw_lba==next_lba && owner.raw_generation==owner.model.generation);
        assert(owner.raw_lba==owner.first_fad-150u+owner.fill_frame/588u);
        for(uint32_t i=0u;i<sizeof(owner.raw);i++) assert(!owner.raw[i]);
        observed_requests=owner.stats.raw_calls;
        ++next_lba;
        if(source_loops && next_lba==track_end_lba) next_lba=track_first_lba;
    }
    return kui_toy_pilot_bus_copy(address,source,bytes);
}

static void clean(void) {
    assert(!reads && !reports && !owner.stats.fault && !owner.stats.raw_errors);
    assert(!owner.stats.queue_errors && !owner.stats.stale_actions && !owner.stats.active_bank_writes);
    assert(!owner.stats.recovery_last_reason);
    for(unsigned i=0u;i<8u;i++) assert(!owner.stats.recovery_counts[i]);
    assert(owner.stats.raw_calls==observed_requests);
    assert(owner.stats.raw_bytes==owner.stats.raw_calls*2352u);
    assert(owner.stats.raw_read_timing_calls==owner.stats.raw_calls);
}

static void zero_ring(void) {
    assert(pcm_frames[0]==32768u && pcm_frames[1]==32768u);
    for(unsigned channel=0u;channel<2u;channel++)
        for(uint32_t frame=0u;frame<32768u;frame++)
            assert(!sample(pcm_address[channel]+frame*2u));
}

static void control_prepare(uint32_t sectors) {
    prepare(sectors);
    /* Ramp PCM assertions are replaced by complete zero-upload/ring checks;
     * the independent cursor clock and active-block checks remain enabled. */
    check_pcm=false;
    observed_requests=checked_upload_bytes=0u;
    track_first_lba=next_lba=50u;track_end_lba=50u+sectors;source_loops=false;
    serial_cursor=true;serial_period=7813u;serial_gap=8u;serial_right_delay=1000u;
}

static void source_offsets_and_split_sector_cache(void) {
    control_prepare(200u);data_command=16u;
    for(unsigned i=0u;i<20u && (owner.handled_generation!=2u || owner.stop_wait);i++) visit(13021u);
    assert(owner.stats.state==KUI_TOY_PILOT_PREFILL && !owner.ring_queued && !owner.stats.raw_calls);
    data_command=0u;
    for(unsigned q=0u;q<7u;q++) {
        memset(owner.raw,0xa5,sizeof(owner.raw));
        uint32_t before=owner.stats.raw_calls,stream=owner.ring_fill_stream;
        fill_quantum();clean();
        assert(owner.stats.raw_calls==before+1u && owner.ring_fill_stream==stream+588u);
        assert(owner.fill_frame==(q+1u)*588u && owner.raw_lba==50u+q);
    }
    assert(owner.model.banks[0].state==KUI_TOY_PILOT_BANK_READY &&
           owner.model.banks[0].filled==4096u && owner.model.banks[1].filled==20u);
    assert(checked_upload_bytes==4116u*4u);
    /* Native data command17 still excludes the synthetic fill. */
    data_command=17u;uint32_t before=owner.stats.raw_calls,stream=owner.ring_fill_stream;
    fill_quantum();assert(owner.stats.raw_calls==before && owner.ring_fill_stream==stream);clean();
}

static void finite_end_retains_source_and_fad_timeline(void) {
    const uint32_t lengths[]={1u,7u,56u};
    for(unsigned n=0u;n<sizeof(lengths)/sizeof(*lengths);n++) {
        control_prepare(lengths[n]);
        for(unsigned i=0u;i<2500u && owner.stats.state!=KUI_TOY_PILOT_EOF;i++) {
            visit(7813u);clean();
        }
        assert(owner.stats.state==KUI_TOY_PILOT_EOF && owner.stats.finite_ends==1u);
        assert(owner.stats.raw_calls==lengths[n] && owner.stats.filled_frames==source_frames);
        assert(owner.stats.position_fad==200u+lengths[n] && owner.resume_frame==source_frames);
        assert(starts==1u && !running && !stop_queued && checked_upload_bytes>=source_frames*4u);
        zero_ring();
        uint32_t old_requests=owner.stats.raw_calls,old_copies=copies;
        for(unsigned i=0u;i<20u;i++) visit(7813u);
        assert(owner.stats.raw_calls==old_requests && copies==old_copies);clean();
    }
}

static void repeat_wrap_retains_continuous_hardware_playback(void) {
    const uint32_t lengths[]={1u,7u};
    for(unsigned n=0u;n<sizeof(lengths)/sizeof(*lengths);n++) {
        control_prepare(lengths[n]);source_loops=repeat_pcm=true;
        physical_phase[1]=24u;
        assert(kui_toy_pilot_request(CMD_PLAY,2u,2u,15u)==3u);ready();zero_ring();
        for(unsigned i=0u;i<450u;i++) { visit(7813u);clean(); }
        assert(starts==1u && running && owner.ring_played>4u*32768u && owner.ring_consumed>4u*32768u);
        assert(owner.stats.filled_frames>4u*32768u && owner.fill_frame<source_frames);
        if(lengths[n]==1u) assert(owner.stats.raw_calls==1u); /* Same-LBA cache survives source loops. */
        else assert(owner.stats.raw_calls>4u*32768u/588u);
        assert(owner.stats.position_fad>=200u && owner.stats.position_fad<200u+lengths[n]);
        assert(owner.ring_played>=owner.ring_consumed && owner.ring_played-owner.ring_consumed<=64u);
        zero_ring();
    }
}

static void wait_applied(uint32_t generation,bool no_fills) {
    uint32_t before=owner.stats.raw_calls,before_copies=copies;
    for(unsigned i=0u;i<200u && owner.stats.applied_generation!=generation;i++) {
        visit(13021u);clean();
        if(no_fills) assert(owner.stats.raw_calls==before && copies==before_copies);
    }
    assert(owner.stats.applied_generation==generation);
}

static void pause_release_replacement_and_shutdown(void) {
    control_prepare(200u);ready();zero_ring();
    for(unsigned i=0u;i<8u;i++) { visit(13021u);clean(); }
    assert(owner.stats.position_fad>200u);
    assert(kui_toy_pilot_request(CMD_PAUSE,0u,0u,0u)==3u);visit(0u);
    assert(stop_queued && owner.stop_wait);wait_applied(3u,true);
    assert(owner.stats.state==KUI_TOY_PILOT_PAUSED && !running);
    uint32_t position=owner.stats.position_fad,resume=owner.resume_frame;
    uint32_t before=owner.stats.raw_calls,before_copies=copies,before_packets=packets;
    visit(CLOCK_GAP_LIMIT*2u);
    assert(owner.stats.position_fad==position && owner.stats.raw_calls==before &&
           copies==before_copies && packets==before_packets);clean();
    next_lba=50u+resume/588u;
    assert(kui_toy_pilot_request(CMD_RELEASE,0u,0u,0u)==4u);wait_applied(4u,false);
    assert(owner.ring_origin==resume && owner.stats.position_fad>=position && starts==2u && running);
    zero_ring();
    /* Replace a live source with another admitted selection at a different
     * disc offset. Generation fencing must discard the previous cached LBA. */
    admitted.track_count=3u;
    admitted.slots[2].track=(struct kui_retail_track){.start_lba=80u,.end_lba=87u,.extent_count=1u};
    uint32_t before_replacement=owner.stats.raw_calls;
    track_first_lba=next_lba=80u;track_end_lba=87u;source_loops=false;
    assert(kui_toy_pilot_request(CMD_PLAY,3u,3u,0u)==5u);
    wait_applied(5u,false);
    assert(owner.first_fad==230u && owner.end_fad==237u && owner.track==3u && owner.ring_origin==0u);
    assert(owner.stats.raw_calls==before_replacement+7u);
    assert(owner.raw_lba==86u && owner.raw_generation==5u && owner.fill_frame==7u*588u);
    zero_ring();
    before=owner.stats.raw_calls;before_copies=copies;before_packets=packets;
    uint32_t saved_sr=sr;kui_toy_pilot_worker_shutdown();assert(sr==saved_sr);
    /* Native shutdown owns the hardware global stop; the worker only revokes. */
    running=looping=false;flag_delay=6u;
    for(unsigned i=0u;i<20u;i++) visit(13021u);
    assert(owner.disabled && !owner.sdk_ready && owner.stats.state==KUI_TOY_PILOT_OFF);
    assert(owner.stats.raw_calls==before && copies==before_copies && packets==before_packets);
    assert(!kui_toy_pilot_request(CMD_PLAY,2u,2u,0u));clean();
}

int main(void) {
    source_offsets_and_split_sector_cache();
    finite_end_retains_source_and_fad_timeline();
    repeat_wrap_retains_continuous_hardware_playback();
    pause_release_replacement_and_shutdown();
    puts("synthetic source control: zero SCI callbacks and PCM, retained source/FAD, block cache, data exclusion, EOF, loop, pause/release, replacement and shutdown");
    return 0;
}
