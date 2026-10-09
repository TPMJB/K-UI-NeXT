/* SPDX-License-Identifier: GPL-3.0-only */
/* Executed by tools/test_toy_pilot_cadence.py. */
#include "toy_pilot_async_fixture.h"

int main(int argc,char **argv) {
    uint32_t cadence=argc>1?(uint32_t)strtoul(argv[1],0,0):26042u;
    uint32_t spacing=argc>2?(uint32_t)strtoul(argv[2],0,0):7813u;
    uint32_t gap_ticks=argc>3?(uint32_t)strtoul(argv[3],0,0):66732u;
    uint32_t gap_phase=argc>4?(uint32_t)strtoul(argv[4],0,0):0u;
    uint32_t pub_phase=argc>5?(uint32_t)strtoul(argv[5],0,0):0u;
    uint32_t gap_period=argc>6?(uint32_t)strtoul(argv[6],0,0):32u;
    uint32_t gap_streak=argc>7?(uint32_t)strtoul(argv[7],0,0):1u;
    uint32_t gap_start=argc>8?(uint32_t)strtoul(argv[8],0,0):0u;
    uint32_t slow_start=argc>9?(uint32_t)strtoul(argv[9],0,0):0u;
    prepare(60u);repeat_pcm=true;strict_source_reads=true;
    raw_ticks=3589u;raw_tail_period=32u;raw_tail_ticks=8830u;
    serial_cursor=true;serial_period=7813u;serial_gap=8u;serial_right_delay=1000u;
    serial_offset=pub_phase;
    assert(kui_toy_pilot_request(CMD_PLAY,2u,2u,15u)==3u);ready();
    uint32_t next=now,max_visit=0u,max_reads=0u,max_gap=0u,last_enter=now;
    unsigned update,hook=0;
    for(update=0u;update<300u;update++) {
        if(next<now) next=now;
        uint32_t first=next;
        for(hook=0u;hook<2u;hook++) {
            uint32_t scheduled=first;
            if(hook) scheduled+=update>=gap_start && (update+gap_period-gap_phase)%gap_period<gap_streak?gap_ticks:spacing;
            if(scheduled<now) scheduled=now;
            advance(scheduled-now);
            uint32_t entered=now,before_reads=reads;
            if(entered-last_enter>max_gap) max_gap=entered-last_enter;
            last_enter=entered;
            visit(0u);
            uint32_t spent=now-entered,raw_count=reads-before_reads;
            if(spent>max_visit) max_visit=spent;
            if(raw_count>max_reads) max_reads=raw_count;
            assert(raw_count<=4u && !owner.stats.fault && !reports &&
                   !owner.stats.raw_errors && !owner.stats.queue_errors &&
                   !owner.stats.stale_actions && !owner.stats.active_bank_writes);
            if(owner.stats.recovery_last_reason) goto ended;
        }
        next=first+(update>=slow_start?cadence:26042u);
    }
ended:
    printf("cad=%u spacing=%u gap=%u phase=%u pub=%u period=%u streak=%u start=%u slowstart=%u update=%u hook=%u reason=%u site=%u filled=%u physical=%u cursor=%u remaining=%u age=%u sample=%u probe=%u window=%u visits=%u maxvisit=%u maxreads=%u maxgap=%u frames=%u bankends=%u played=%u consumed=%u fillstream=%u proofage=%u observedage=%u probeseen=%u\n",cadence,spacing,gap_ticks,gap_phase,pub_phase,gap_period,gap_streak,gap_start,slow_start,update,hook,owner.stats.recovery_last_reason,owner.stats.reserve_last_site,owner.stats.reserve_last_bank_filled,physical_position(),owner.ring_cursor,owner.stats.reserve_last_remaining,owner.stats.reserve_last_proof_age,owner.stats.reserve_last_sample_age,owner.stats.reserve_last_probe_age,owner.stats.reserve_last_window_ticks,owner.stats.reserve_last_window_calls,max_visit,max_reads,max_gap,checked_frames,owner.stats.bank_ends,owner.ring_played,owner.ring_consumed,owner.ring_fill_stream,now-owner.ring_tick,now-owner.ring_observed_tick,owner.ring_probe_seen);
    if(!owner.stats.recovery_last_reason) assert(starts==1u && running && !silent_frames);
    else {
        /* Terminate at the first recovery, then independently dispatch STOP
         * and check that its still-live full-ring fence admits no new work. */
        uint32_t old_reads=reads,old_copies=copies;
        assert(stop_queued && owner.stop_wait);
        visit(0u);assert(!running && reads==old_reads && copies==old_copies);
        for(unsigned i=0u;i<20u;i++) {
            visit(7813u);assert(reads==old_reads && copies==old_copies);
        }
    }
    return owner.stats.recovery_last_reason?1:0;
}
