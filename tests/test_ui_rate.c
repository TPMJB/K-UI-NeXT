/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/ui_rate.h"
#include <assert.h>
#include <stdio.h>

/* Simulate the UI loop: ~40 ms per iteration (33 ms sleep plus the loop's own
 * work), redraw when the predicate says so, and record when each draw happened
 * so a test can count draws inside any window of the run. */
static uint64_t draw_at[4096];
static unsigned n_draws;
static void run(unsigned seconds, bool busy, unsigned hz_at(uint64_t)) {
    uint64_t last = 0;
    bool was_busy = false;
    n_draws = 0;
    for(uint64_t t = 1000; t < 1000 + seconds * 1000ull; t += 40) {
        if(kui_ui_redraw_due(busy, was_busy, hz_at(t), t, last)) {
            assert(n_draws < 4096);
            draw_at[n_draws++] = t; last = t;
        }
        was_busy = busy;
    }
}
/* Draws in the [from_s, to_s) seconds of the run. */
static unsigned draws_in(unsigned from_s, unsigned to_s) {
    unsigned n = 0;
    for(unsigned i = 0; i < n_draws; ++i)
        if(draw_at[i] >= 1000 + from_s * 1000ull && draw_at[i] < 1000 + to_s * 1000ull) ++n;
    return n;
}
static unsigned four(uint64_t t) { (void)t; return 4; }
static unsigned zero(uint64_t t) { (void)t; return 0; }
static unsigned full(uint64_t t) { (void)t; return KUI_OPT_UI_FULL; }
/* The bench's sequence: full for 10 s, then 8, then 2, then 0, then full again. */
static unsigned bench_sweep(uint64_t t) {
    uint64_t s = (t - 1000) / 1000;
    return s < 10 ? KUI_OPT_UI_FULL : s < 20 ? 8 : s < 30 ? 2 : s < 40 ? 0 : KUI_OPT_UI_FULL;
}
static void games_idle_cadence(void) {
    assert(kui_ui_poll_delay(true,false)==8);
    assert(kui_ui_poll_delay(true,true)==33);
    assert(kui_ui_poll_delay(false,false)==33);
    assert(kui_ui_poll_delay(false,true)==33);
    assert(kui_ui_games_redraw_due(true,false,false,0,false,5000,0));
    assert(!kui_ui_games_redraw_due(true,false,false,0,false,5008,5000));
    assert(!kui_ui_games_redraw_due(true,false,false,0,false,5032,5000));
    assert(kui_ui_games_redraw_due(true,false,false,0,false,5033,5000));
    /* Input and a freshly published list/detail do not wait for the cadence. */
    assert(kui_ui_games_redraw_due(true,false,false,0,true,5008,5000));
    assert(kui_ui_games_redraw_due(true,false,true,0,false,5008,5000));
    /* Faster Games input never overrides busy caps, including a held button. */
    assert(!kui_ui_games_redraw_due(true,true,true,0,true,6000,5000));
    assert(!kui_ui_games_redraw_due(true,true,true,2,true,5499,5000));
    assert(kui_ui_games_redraw_due(true,true,true,2,true,5500,5000));
    assert(kui_ui_games_redraw_due(true,true,false,0,false,5008,5000));
    assert(kui_ui_games_redraw_due(false,false,false,0,false,5008,5000));
    uint64_t last=0;unsigned polls=0,draws=0;
    for(uint64_t t=1000;t<2000;t+=kui_ui_poll_delay(true,false)) {
        ++polls;
        if(kui_ui_games_redraw_due(true,false,false,0,false,t,last)) {
            if(last) assert(t-last>=33u);
            last=t;++draws;
        }
    }
    assert(polls==125 && draws==25); /* Controls poll independently of redraws. */
}

static void all_menu_input_latency(void) {
    assert(kui_ui_menu_poll_delay(false)==8u);
    assert(kui_ui_menu_poll_delay(true)==33u);
    assert(kui_ui_menu_wait_ms(false,false)==8u);
    assert(kui_ui_menu_wait_ms(false,true)==0u);
    assert(kui_ui_menu_wait_ms(true,false)==33u);
    assert(kui_ui_menu_wait_ms(true,true)==33u);
    /* The first press, a direction change, and a completed list all repaint
     * immediately, even one millisecond after the preceding frame. */
    assert(kui_ui_menu_redraw_due(false,false,0,true,5001,5000));
    assert(kui_ui_menu_redraw_due(false,false,2,true,5001,5000));
    assert(kui_ui_menu_redraw_due(false,true,0,false,5001,5000));
    assert(kui_ui_menu_redraw_due(false,false,0,false,5000,0));
    assert(!kui_ui_menu_redraw_due(false,false,0,false,5032,5000));
    assert(kui_ui_menu_redraw_due(false,false,0,false,5033,5000));
    /* Interactive polling is not permission to defeat a running benchmark's
     * quiet screen or a disc operation's capped redraw rate. */
    assert(!kui_ui_menu_redraw_due(true,true,0,true,6000,5000));
    assert(!kui_ui_menu_redraw_due(true,true,2,true,5499,5000));
    assert(kui_ui_menu_redraw_due(true,true,2,true,5500,5000));
    assert(kui_ui_menu_redraw_due(true,false,0,false,5001,5000));
    unsigned slowest_poll=0;
    /* Place a short new press at every possible millisecond in a polling
     * window. Its first observed edge must schedule a draw without waiting
     * for either the 33 ms idle cadence or the 400 ms hold-repeat deadline. */
    for(unsigned press_at=1;press_at<=1000u;press_at++) {
        unsigned next_poll=((press_at+7u)/8u)*8u;
        unsigned elapsed=next_poll-press_at;
        if(elapsed>slowest_poll) slowest_poll=elapsed;
        assert(elapsed<8u);
        assert(kui_ui_menu_redraw_due(false,false,2,true,5000u+next_poll,
            5000u+next_poll-1u));
    }
    assert(slowest_poll==7u);
    uint64_t last=0;unsigned polls=0,draws=0;
    for(uint64_t t=1000;t<2000;t+=kui_ui_menu_poll_delay(false)) {
        ++polls;
        if(kui_ui_menu_redraw_due(false,false,2,false,t,last)) {
            if(last) assert(t-last>=33u);
            last=t;++draws;
        }
    }
    assert(polls==125u && draws==25u);
    /* A slow repaint finishing after the 33 ms cadence still leaves input
     * polls before the next background repaint. Its completed timestamp is
     * authoritative; a fresh edge can always force the next frame sooner. */
    for(unsigned cost=1u;cost<=250u;cost++) {
        uint64_t start=5000u,completed=start+cost;
        assert(!kui_ui_menu_wait_ms(false,true));
        assert(!kui_ui_menu_redraw_due(false,false,2,false,completed,completed));
        assert(!kui_ui_menu_redraw_due(false,false,2,false,completed+32u,completed));
        assert(kui_ui_menu_redraw_due(false,false,2,false,completed+33u,completed));
        assert(kui_ui_menu_redraw_due(false,false,2,true,completed,completed));
    }
}

static void menu_navigation_edges(void) {
    enum { UP=1u, DOWN=2u, ACTION=4u };
    unsigned held=0;
    uint64_t repeat_at=0;
    assert(kui_ui_navigation_repeat(DOWN,DOWN,&held,&repeat_at,1000)==DOWN);
    assert(held==DOWN && repeat_at==1250u);
    assert(!kui_ui_navigation_repeat(DOWN,0,&held,&repeat_at,1249));
    assert(kui_ui_navigation_repeat(DOWN,0,&held,&repeat_at,1250)==DOWN);
    assert(repeat_at==1330u);
    assert(!kui_ui_navigation_repeat(DOWN,0,&held,&repeat_at,1329));
    assert(kui_ui_navigation_repeat(DOWN,0,&held,&repeat_at,1330)==DOWN);
    /* Opposite direction takes effect on its first edge, without waiting for
     * the previous direction's repeat timer. */
    assert(kui_ui_navigation_repeat(UP,UP,&held,&repeat_at,1331)==UP);
    assert(repeat_at==1581u);
    assert(!kui_ui_navigation_repeat(0,0,&held,&repeat_at,1332));
    assert(kui_ui_navigation_repeat(UP,UP,&held,&repeat_at,1333)==UP);
    assert(repeat_at==1583u);
    /* An action can arrive during a held direction but never enters repeat. */
    assert(kui_ui_navigation_repeat(UP,ACTION,&held,&repeat_at,1334)==ACTION);
    assert(!kui_ui_navigation_repeat(UP,0,&held,&repeat_at,1582));
    assert(kui_ui_navigation_repeat(UP,0,&held,&repeat_at,1583)==UP);
    assert(kui_ui_navigation_repeat(UP,0,&held,&repeat_at,9000)==UP);
    assert(repeat_at==9080u); /* One step, no burst after a slow operation. */
    assert(!kui_ui_navigation_repeat(0,0,&held,&repeat_at,9001));
    assert(!kui_ui_navigation_repeat(0,0,&held,&repeat_at,9999));
}

int main(void) {
    games_idle_cadence();
    all_menu_input_latency();
    menu_navigation_edges();
    /* Idle always redraws, whatever the cap says. */
    assert(kui_ui_redraw_due(false, false, 0, 5000, 5000));
    assert(kui_ui_redraw_due(false, false, 4, 5001, 5000));
    /* Unthrottled busy is today's loop: every iteration. */
    assert(kui_ui_redraw_due(true, true, KUI_OPT_UI_FULL, 5001, 5000));
    /* An operation starting or ending redraws once even at hz=0, so the screen
     * is never stale about what state it is in. */
    assert(kui_ui_redraw_due(true, false, 0, 5001, 5000));
    assert(kui_ui_redraw_due(false, true, 0, 5001, 5000));
    /* hz=0 while steadily busy never redraws. */
    assert(!kui_ui_redraw_due(true, true, 0, 5000 + 3600000ull, 5000));
    /* A finite rate waits for its period: 4 Hz is 250 ms. */
    assert(!kui_ui_redraw_due(true, true, 4, 5249, 5000));
    assert(kui_ui_redraw_due(true, true, 4, 5250, 5000));

    /* THE regression: a cap that changes mid-operation applies at once. Going
     * from unthrottled to 8 Hz (125 ms) must redraw within 125 ms of the last
     * draw, not wait for a deadline the old setting computed. */
    assert(!kui_ui_redraw_due(true, true, 8, 5100, 5000));
    assert(kui_ui_redraw_due(true, true, 8, 5125, 5000));
    /* From 0 to 2 Hz after a long silence: due immediately. */
    assert(kui_ui_redraw_due(true, true, 2, 60000, 5000));

    /* Steady rates over 10 s of ~40 ms iterations. */
    run(10, true, four);
    assert(n_draws >= 30 && n_draws <= 41);          /* ~4 per second */
    run(10, true, zero);
    assert(n_draws == 1);                            /* only the start-of-operation redraw */
    run(10, true, full);
    assert(n_draws == 250);                          /* every iteration, as today */
    run(10, false, zero);
    assert(n_draws == 250);                          /* idle ignores the cap */

    /* The bench's sweep: full, 8, 2, 0, full, 10 s each. Every pass must be
     * drawn at ITS OWN rate. This is the assertion that fails if a cap change
     * mid-operation is not applied at once: the 8 Hz and 2 Hz passes would be
     * frozen while the final 'full' pass hid it in a total. */
    run(50, true, bench_sweep);
    assert(draws_in(0, 10) == 250);
    assert(draws_in(10, 20) >= 55 && draws_in(10, 20) <= 70);   /* 125 ms period, 40 ms steps */
    assert(draws_in(20, 30) >= 17 && draws_in(20, 30) <= 21);   /* 500 ms period */
    assert(draws_in(30, 40) == 0);                              /* 0 Hz: silent, by design */
    assert(draws_in(40, 50) == 250);                            /* restored to full */

    puts("PASS ui rate: modeled all-menu input scheduling <8ms, Games input/redraw cadence, busy caps, idle/full/edges/zero, period, mid-operation rate change, bench sweep");
    return 0;
}
