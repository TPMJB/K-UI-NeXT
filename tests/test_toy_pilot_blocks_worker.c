/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent PCM, physical ownership, source, and lifecycle block regressions. */
#include "toy_pilot_async_fixture.h"

static void clean(void) {
    assert(!owner.stats.fault && !reports && !owner.stats.recovery_last_reason);
    assert(!owner.stats.raw_errors && !owner.stats.queue_errors &&
           !owner.stats.stale_actions && !owner.stats.active_bank_writes);
    for(unsigned i=0u;i<8u;i++) assert(!owner.stats.recovery_counts[i]);
}
static void serial_setup(void) {
    serial_cursor=true;serial_period=7813u;serial_gap=8u;serial_right_delay=1000u;
}
static void repeat_start(uint32_t sectors,unsigned fast_channel,uint32_t skew) {
    prepare(sectors);repeat_pcm=true;strict_source_reads=true;
    physical_phase[fast_channel]=skew;serial_setup();
    assert(kui_toy_pilot_request(CMD_PLAY,2u,2u,15u)==3u);ready();
    clean();
}
static void finite_lengths_cross_source_block_and_ring_edges(void) {
    const uint32_t lengths[]={1u,6u,7u,55u,56u};
    for(unsigned n=0u;n<sizeof(lengths)/sizeof(*lengths);n++) {
        prepare(lengths[n]);strict_source_reads=true;serial_setup();
        /* A one-sector source can reach finite EOF before the two changed
         * publications establish START ownership. Observe the entire finite
         * lifecycle, without requiring an intermediate PLAYING snapshot. */
        for(unsigned i=0u;i<2500u && owner.stats.state!=KUI_TOY_PILOT_EOF;i++) {
            visit(7813u);clean();
        }
        assert(owner.stats.state==KUI_TOY_PILOT_EOF && owner.stats.finite_ends==1u);
        assert(starts==1u && !running && !stop_queued && reads==lengths[n]);
        assert(checked_frames>=source_frames && silent_frames);
        uint32_t old_reads=reads,old_copies=copies;
        for(unsigned i=0u;i<20u;i++) visit(7813u);
        assert(reads==old_reads && copies==old_copies);clean();
    }
}
static void repeat_source_wrap_and_independent_channel_boundaries(void) {
    const uint32_t lengths[]={1u,7u};
    const uint32_t skews[]={0u,24u,64u};
    for(unsigned n=0u;n<sizeof(lengths)/sizeof(*lengths);n++)
    for(unsigned s=0u;s<sizeof(skews)/sizeof(*skews);s++)
    for(unsigned fast=0u;fast<2u;fast++) {
        repeat_start(lengths[n],fast,skews[s]);
        for(unsigned i=0u;i<450u;i++) { visit(7813u);clean(); }
        assert(checked_frames>4u*32768u && starts==1u && running && !silent_frames);
        assert(owner.ring_played>4u*32768u && owner.ring_consumed>4u*32768u);
        assert(owner.ring_played>=owner.ring_consumed &&
               owner.ring_played-owner.ring_consumed<=64u);
        assert(owner.ring_fill_stream>4u*32768u);
    }
}
static void delayed_observation_validates_multiple_blocks(void) {
    repeat_start(200u,0u,0u);
    assert(owner.ring_played<4096u && voice_rendered<4096u);
    uint32_t target=3u*4096u+256u;
    advance(kui_toy_ring_ticks(target-voice_rendered)+1u);
    uint32_t before=owner.ring_fill_stream;
    assert(owner.ring_played<4096u && physical_position()/4096u==3u);
    for(unsigned i=0u;i<12u && owner.ring_played<target;i++) { visit(7813u);clean(); }
    assert(owner.ring_played>=target && owner.stats.bank_ends>=3u);
    assert(owner.ring_fill_stream>before);
    for(unsigned i=0u;i<180u;i++) { visit(7813u);clean(); }
    assert(checked_frames>2u*32768u && starts==1u && !silent_frames);
}
static void delayed_wrap_refills_in_absolute_order(void) {
    repeat_start(200u,0u,24u);
    while(owner.ring_played<6u*4096u+256u) { visit(7813u);clean(); }
    uint32_t next_turn=(voice_rendered/32768u+1u)*32768u;
    uint32_t target=next_turn+4096u+256u;
    uint32_t before=owner.ring_played;
    assert(target-before<16384u);
    advance(kui_toy_ring_ticks(target-voice_rendered)+1u);
    for(unsigned i=0u;i<12u && owner.ring_played<target;i++) { visit(7813u);clean(); }
    assert(owner.ring_played>=target && owner.ring_consumed>next_turn);
    for(unsigned i=0u;i<250u;i++) { visit(7813u);clean(); }
    assert(starts==1u && !silent_frames && checked_frames>3u*32768u);
}
static void real_channel_boundary_retains_both_blocks(void) {
    for(unsigned fast=0u;fast<2u;fast++)
    for(unsigned missing=0u;missing<2u;missing++) {
        prepare(200u);repeat_pcm=true;strict_source_reads=true;
        physical_phase[fast]=24u;
        assert(kui_toy_pilot_request(CMD_PLAY,2u,2u,15u)==3u);ready();
        assert(owner.ring_played<4096u && voice_rendered<4080u);
        if(missing) owner.model.banks[1].filled-=2u;
        read_ticks=1u;
        advance(kui_toy_ring_ticks(4080u-voice_rendered)+1u);
        bool accepted=ring_observe();
        uint32_t low=owner.stats.cursor_left<owner.stats.cursor_right?
            owner.stats.cursor_left:owner.stats.cursor_right;
        uint32_t high=owner.stats.cursor_left>owner.stats.cursor_right?
            owner.stats.cursor_left:owner.stats.cursor_right;
        assert(low<4096u && high>=4096u && owner.ring_consumed==low);
        if(missing) {
            assert(!accepted && owner.stop_wait && stop_queued);
            assert(owner.stats.recovery_last_reason==KUI_TOY_PILOT_RECOVERY_MISSING_HALF);
            assert(owner.ring_played==4096u && owner.ring_consumed<4096u);
        } else {
            assert(accepted && !owner.stop_wait && !owner.stats.recovery_last_reason);
            assert(owner.model.banks[0].state==KUI_TOY_PILOT_BANK_PLAYING &&
                   owner.model.banks[1].state==KUI_TOY_PILOT_BANK_PLAYING);
            assert(owner.model.active_bank==1u && !owner.stats.bank_ends);
            assert(!ring_write_allowed(0u,KUI_TOY_PILOT_RESERVE_COPY_PLANE));
            advance(kui_toy_ring_ticks(32u)+1u);
            assert(ring_observe() && owner.ring_consumed>=4096u);
            assert(owner.stats.bank_ends==1u &&
                   owner.model.banks[0].state==KUI_TOY_PILOT_BANK_EMPTY &&
                   owner.model.banks[1].state==KUI_TOY_PILOT_BANK_PLAYING);
            assert(ring_write_allowed(0u,KUI_TOY_PILOT_RESERVE_FILL_BEGIN));
        }
        assert(!owner.stats.fault && !reports && !owner.stats.active_bank_writes);
    }
}
static void missing_intermediate_block_requests_stop(void) {
    for(unsigned stale=0u;stale<2u;stale++) {
        repeat_start(200u,0u,0u);
        assert(owner.ring_played<4096u && owner.model.banks[2].state==KUI_TOY_PILOT_BANK_READY);
        /* Corrupt readiness metadata, not cursor/proof timestamps. Block3 is
         * intact, so checking only the final destination would miss this. */
        if(stale) owner.model.banks[2].first_frame+=4096u;
        else owner.model.banks[2].filled-=2u;
        uint32_t target=3u*4096u+256u;
        advance(kui_toy_ring_ticks(target-voice_rendered)+1u);
        for(unsigned i=0u;i<12u && !owner.stats.recovery_last_reason;i++) visit(7813u);
        assert(owner.stats.recovery_last_reason==KUI_TOY_PILOT_RECOVERY_MISSING_HALF);
        assert(owner.ring_played==2u*4096u && owner.stop_wait && stop_queued);
        uint32_t old_reads=reads,old_copies=copies;
        visit(0u); /* Apply STOP, without shortening its physical fence. */
        assert(!running && reads==old_reads && copies==old_copies);
        for(unsigned i=0u;i<20u;i++) { visit(7813u);assert(reads==old_reads && copies==old_copies); }
        assert(!owner.stats.fault && !reports && !owner.stats.active_bank_writes);
    }
}
static void defer_first_right_plane(void) {
    repeat_start(200u,0u,0u);defer_right_copy=true;
    for(unsigned i=0u;i<120u && defer_right_copy;i++) { visit(7813u);clean(); }
    assert(!defer_right_copy && retry_pending && owner.stats.bus_deferrals);
    assert(owner.model.banks[deferred_bank].state==KUI_TOY_PILOT_BANK_FILLING);
    assert(owner.model.banks[deferred_bank].filled==deferred_filled &&
           owner.fill_frame==deferred_fill_frame && owner.ring_fill_stream==deferred_stream);
    assert(owner.raw_lba==deferred_raw_lba && owner.raw_generation==deferred_raw_generation);
    assert(!memcmp(owner.raw,deferred_raw,sizeof(deferred_raw)));
}
static void deferred_right_retries_cached_sector(void) {
    defer_first_right_plane();
    for(unsigned i=0u;i<12u && retry_pending;i++) { visit(7813u);clean(); }
    assert(!retry_pending && owner.ring_fill_stream>deferred_stream);
    for(unsigned i=0u;i<100u;i++) { visit(7813u);clean(); }
}
static void partial_stereo_pause_retains_full_ring_fence(void) {
    defer_first_right_plane();
    uint32_t old_reads=reads,old_copies=copies;
    assert(kui_toy_pilot_request(CMD_PAUSE,0u,0u,0u)==4u);visit(0u);
    assert(stop_queued && owner.stop_wait && reads==old_reads && copies==old_copies);
    visit(0u);assert(!running && stopped_live_voice);
    uint32_t stopped=stop_dispatched;
    uint32_t full=kui_toy_ring_ticks(32768u);
    while(now-stopped<full) {
        visit(7813u);assert(reads==old_reads && copies==old_copies);
        if(now-stopped<full) {
            assert(owner.stats.applied_generation!=4u);
            assert(owner.ring_fill_stream==deferred_stream);
        }
    }
    for(unsigned i=0u;i<100u && owner.stats.applied_generation!=4u;i++) {
        visit(7813u);assert(reads==old_reads && copies==old_copies);
    }
    assert(owner.stats.state==KUI_TOY_PILOT_PAUSED && owner.stats.applied_generation==4u);
    assert(now-stopped>=full && !owner.ring_queued && !owner.ring_fill_stream);clean();
}
static void partial_stereo_revoke_forbids_further_work(void) {
    defer_first_right_plane();uint32_t old_reads=reads,old_copies=copies;
    uint32_t before=sr;kui_toy_pilot_worker_revoke();assert(sr==before);
    /* Independently model the existing native global stop during shutdown. */
    render();running=looping=false;flag_delay=6u;
    for(unsigned i=0u;i<200u;i++) visit(7813u);
    assert(owner.disabled && !owner.sdk_ready && reads==old_reads && copies==old_copies);
    assert(!owner.stats.fault && !reports && !owner.stats.active_bank_writes);
}
int main(void) {
    assert(KUI_TOY_PILOT_BLOCKS==8u && KUI_TOY_PILOT_BANK_FRAMES==4096u);
    finite_lengths_cross_source_block_and_ring_edges();
    repeat_source_wrap_and_independent_channel_boundaries();
    delayed_observation_validates_multiple_blocks();
    delayed_wrap_refills_in_absolute_order();
    real_channel_boundary_retains_both_blocks();
    missing_intermediate_block_requests_stop();
    deferred_right_retries_cached_sector();
    partial_stereo_pause_retains_full_ring_fence();
    partial_stereo_revoke_forbids_further_work();
    puts("eight-block physical PCM and lifecycle regressions: ok");
    return 0;
}
