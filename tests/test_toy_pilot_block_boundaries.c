/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent physical stereo boundary and absolute source-epoch checks. */
#include "toy_pilot_async_fixture.h"

static void stereo_boundary(unsigned fast, bool missing) {
    prepare(200u);repeat_pcm=true;strict_source_reads=true;
    physical_phase[fast]=24u;
    assert(kui_toy_pilot_request(CMD_PLAY,2u,2u,15u)==3u);ready();
    assert(owner.ring_played<4096u && voice_rendered<4080u);
    if(missing) owner.model.banks[1].filled-=2u;
    /* Real hardware playback and independently timed bus reads produce an
     * accepted pair on opposite sides of4096. No proof timestamp or cursor
     * publication is fabricated by the test. */
    read_ticks=1u;
    advance(kui_toy_ring_ticks(4080u-voice_rendered)+1u);
    bool accepted=ring_observe();
    uint32_t low=owner.stats.cursor_left<owner.stats.cursor_right?
        owner.stats.cursor_left:owner.stats.cursor_right;
    uint32_t high=owner.stats.cursor_left>owner.stats.cursor_right?
        owner.stats.cursor_left:owner.stats.cursor_right;
    assert(low<4096u && high>=4096u && owner.ring_consumed==low);
    if(missing) {
        /* A fast capture in missing PCM cannot credit frames the slower
         * channel has not yet captured from the preceding valid block. */
        assert(!accepted && owner.stop_wait && stop_queued);
        assert(owner.stats.recovery_last_reason==KUI_TOY_PILOT_RECOVERY_MISSING_HALF);
        assert(owner.ring_played==4096u && owner.ring_consumed<4096u);
        assert(owner.resume_frame==(low&~1u));
    } else {
        assert(accepted && !owner.stop_wait && !owner.stats.recovery_last_reason);
        assert(owner.model.banks[0].state==KUI_TOY_PILOT_BANK_PLAYING);
        assert(owner.model.banks[1].state==KUI_TOY_PILOT_BANK_PLAYING);
        assert(owner.model.active_bank==1u && !owner.stats.bank_ends);
        assert(!ring_write_allowed(0u,KUI_TOY_PILOT_RESERVE_COPY_PLANE));
        advance(kui_toy_ring_ticks(32u)+1u);
        assert(ring_observe());
        assert(owner.ring_consumed>=4096u && owner.stats.bank_ends==1u);
        assert(owner.model.banks[0].state==KUI_TOY_PILOT_BANK_EMPTY);
        assert(owner.model.banks[1].state==KUI_TOY_PILOT_BANK_PLAYING);
        assert(ring_write_allowed(0u,KUI_TOY_PILOT_RESERVE_FILL_BEGIN));
    }
    assert(!owner.stats.fault && !reports && !owner.stats.active_bank_writes);
}

static void wrapped_intermediate_epoch_is_checked(bool stale_generation) {
    prepare(200u);repeat_pcm=true;strict_source_reads=true;
    assert(kui_toy_pilot_request(CMD_PLAY,2u,2u,15u)==3u);ready();
    while(owner.ring_played<6u*4096u+256u) {
        visit(7813u);
        assert(!owner.stats.fault && !reports && !owner.stats.recovery_last_reason);
    }
    assert(owner.ring_played<7u*4096u && voice_rendered<32768u);
    assert(owner.model.banks[7].state==KUI_TOY_PILOT_BANK_READY);
    assert(owner.model.banks[0].state==KUI_TOY_PILOT_BANK_READY);
    assert(owner.model.banks[0].first_frame==32768u);
    assert(owner.model.banks[1].state==KUI_TOY_PILOT_BANK_READY);
    assert(owner.model.banks[1].first_frame==32768u+4096u);
    /* PCM remains correct. Only the middle block's readiness tag is stale.
     * A jump from slot6 to slot1 crosses slot7 and slot0, so validating only
     * the final physical slot would falsely accept this old ring epoch. */
    if(stale_generation) --owner.model.banks[0].generation;
    else owner.model.banks[0].first_frame-=32768u;
    uint32_t target=32768u+4096u+256u;
    assert(target-owner.ring_played<16384u);
    uint32_t before_reads=reads,before_copies=copies;
    read_ticks=1u;
    advance(kui_toy_ring_ticks(target-voice_rendered)+1u);
    assert(!ring_observe());
    assert(owner.stats.recovery_last_reason==KUI_TOY_PILOT_RECOVERY_MISSING_HALF);
    assert(owner.ring_played==32768u && owner.ring_consumed==32768u);
    assert(owner.resume_frame==32768u && owner.stop_wait && stop_queued);
    assert(reads==before_reads && copies==before_copies);
    visit(0u);
    assert(!running && owner.stop_wait);
    for(unsigned i=0u;i<20u;i++) {
        visit(7813u);
        assert(reads==before_reads && copies==before_copies);
    }
    assert(!owner.stats.fault && !reports && !owner.stats.active_bank_writes);
}

int main(void) {
    for(unsigned fast=0u;fast<2u;fast++) {
        stereo_boundary(fast,false);stereo_boundary(fast,true);
    }
    wrapped_intermediate_epoch_is_checked(false);
    wrapped_intermediate_epoch_is_checked(true);
    puts("physical stereo boundaries and intermediate absolute epochs: ok");
    return 0;
}
