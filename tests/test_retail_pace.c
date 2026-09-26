/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_pace.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned assertions;
#define CHECK(x) do { ++assertions; assert(x); } while(0)

static void sample(struct kui_retail_pace *p, uint32_t line, uint32_t vbi, uint32_t fb) {
    /* Upper register bits are unrelated status/vblank-out fields. */
    kui_retail_pace_sample(p, 0x3c00u | line, 0x01500000u | vbi, fb);
}

static void sampling(void) {
    struct kui_retail_pace p;
    memset(&p, 0, sizeof(p));
    sample(&p, 100, 260, 0x200000);
    CHECK(p.line == 100 && p.top == 100 && p.vbi == 260 && p.frames == 0 && p.still == 0);
    sample(&p, 150, 260, 0x200000);
    sample(&p, 261, 260, 0x200000);
    CHECK(p.frames == 0 && p.still == 0 && p.top == 261);
    sample(&p, 3, 260, 0x200000); /* wrap */
    CHECK(p.frames == 1 && p.still == 1 && p.top == 261);
    sample(&p, 3, 260, 0x200000); /* same line: no wrap is inferred */
    CHECK(p.frames == 1 && p.still == 1);
    sample(&p, 2, 260, 0x200000);
    CHECK(p.frames == 2 && p.still == 2);
    sample(&p, 1, 260, 0x600000); /* a flip clears stillness, not the frame count */
    CHECK(p.frames == 3 && p.still == 0 && p.fb == 0x600000);
    p.per = 1234;
    sample(&p, 1, 520, 0x600000); /* new video timing relearns */
    CHECK(p.vbi == 520 && p.top == 1 && p.per == 0 && p.frames == 3);
}

static void still_screen(struct kui_retail_pace *p, uint32_t frame, uint32_t vbi) {
    memset(p, 0, sizeof(*p));
    for(uint32_t i = 0; i < KUI_RETAIL_PACE_STILL_FRAMES; ++i) {
        sample(p, frame - 1u, vbi, 0x200000);
        sample(p, 10, vbi, 0x200000);
    }
    CHECK(p->still == KUI_RETAIL_PACE_STILL_FRAMES);
}

static void budgets(void) {
    struct kui_retail_pace p;
    still_screen(&p, 262, 260);
    CHECK(kui_retail_pace_budget(&p, 2, 8) == 2); /* nothing measured yet */
    p.per = 76u * 16u;
    /* Line 10 to the second vblank: 250 + 262 - 32 = 480 lines = 6 sectors. */
    CHECK(kui_retail_pace_budget(&p, 2, 8) == 6);
    CHECK(kui_retail_pace_budget(&p, 2, 5) == 5);
    CHECK(kui_retail_pace_budget(&p, 3, 3) == 3);
    sample(&p, 261, 260, 0x200000); /* just after vblank-in: 1 + 260 + 230 */
    CHECK(kui_retail_pace_budget(&p, 2, 8) == 6);
    sample(&p, 250, 260, 0x200000); /* late: 10 + 230 = 240 lines */
    CHECK(kui_retail_pace_budget(&p, 2, 8) == 3);
    p.per = 200u * 16u;
    CHECK(kui_retail_pace_budget(&p, 2, 8) == 2); /* never below normal */
    p.per = 76u * 16u;
    p.still = KUI_RETAIL_PACE_STILL_FRAMES - 1u;
    CHECK(kui_retail_pace_budget(&p, 2, 8) == 2);
    p.still = KUI_RETAIL_PACE_STILL_FRAMES; p.vbi = 63;
    CHECK(kui_retail_pace_budget(&p, 2, 8) == 2); /* implausible timing */
    /* Before any sample reaches the vblank line, vbi+1 stands in for the
     * frame, which can only make the budget smaller. */
    still_screen(&p, 200, 260);
    p.per = 76u * 16u;
    CHECK(p.top == 199 && kui_retail_pace_budget(&p, 2, 8) == 6);
    /* VGA: twice the scanlines per frame and per sector. */
    still_screen(&p, 525, 520);
    p.per = 152u * 16u;
    CHECK(kui_retail_pace_budget(&p, 2, 8) == 6);
}

static void measurement(void) {
    struct kui_retail_pace p;
    still_screen(&p, 262, 260);
    uint32_t frames = p.frames;
    sample(&p, 162, 260, 0x200000);
    kui_retail_pace_measure(&p, frames, 10, 2);
    CHECK(p.per == 76u * 16u);
    /* A step through two wraps: 261 -> 0 -> 0 -> 150. */
    frames = p.frames; sample(&p, 261, 260, 0x200000);
    sample(&p, 5, 260, 0x200000); sample(&p, 200, 260, 0x200000);
    sample(&p, 100, 260, 0x200000);
    CHECK(p.frames == frames + 2);
    kui_retail_pace_measure(&p, frames, 20, 6);
    CHECK(p.per == (2u * 262u + 80u) * 16u / 6u);
    uint32_t slow = p.per;
    /* Faster steps lower the estimate by an eighth of the difference. */
    frames = p.frames; sample(&p, 150, 260, 0x200000);
    kui_retail_pace_measure(&p, frames, 100, 1);
    CHECK(p.per == slow - (slow - 50u * 16u) / 8u);
    /* Implausible or empty steps leave it unchanged. */
    uint32_t kept = p.per;
    kui_retail_pace_measure(&p, frames, 100, 0);
    kui_retail_pace_measure(&p, p.frames - 4u, 100, 2);
    kui_retail_pace_measure(&p, p.frames, 151, 2);
    CHECK(p.per == kept);
}

static void spinning(void) {
    struct kui_retail_pace p;
    memset(&p, 0, sizeof(p));
    sample(&p, 40, 260, 1);
    for(unsigned i = 1; i < KUI_RETAIL_PACE_SPIN_CALLS; ++i) {
        if(i % 4 == 0) sample(&p, p.line + 1u, 260, 1);
        CHECK(kui_retail_pace_spin(&p) == 0);
    }
    CHECK(kui_retail_pace_spin(&p) == 1 && p.spin == 0);
    CHECK(kui_retail_pace_spin(&p) == 0 && p.spin == 1);
    sample(&p, p.line + 2u, 260, 1); /* a gap of two lines starts over */
    CHECK(kui_retail_pace_spin(&p) == 0 && p.spin == 1);
    for(unsigned i = 1; i < KUI_RETAIL_PACE_SPIN_CALLS - 1u; ++i)
        CHECK(kui_retail_pace_spin(&p) == 0);
    uint32_t line = p.line;
    sample(&p, 300, 260, 1); sample(&p, line, 260, 1); /* one frame later */
    CHECK(kui_retail_pace_spin(&p) == 0 && p.spin == 1);
    /* Once-per-frame polls at an identical line never accumulate. */
    for(unsigned i = 0; i < 64; ++i) {
        sample(&p, 261, 260, 1); sample(&p, line, 260, 1);
        CHECK(kui_retail_pace_spin(&p) == 0);
    }
}

/* Scanline-level model of a game that runs the GD server once per vblank,
 * either in its interrupt handler or right after waiting for vblank. One
 * step costs `overhead` plus `cost` scanlines per sector; samples are taken
 * at entry and after each sector, as the resident's block reads do. */
struct model {
    uint32_t frame, vbi, cost, overhead, latency, flips, work, maximum;
    uint64_t now;
    struct kui_retail_pace pace;
    uint32_t fb, sectors, steps, extended, double_cross, max_step;
};
static void model_sample(struct model *m) {
    uint64_t f = m->now / m->frame;
    if(m->flips && f % m->flips == 0) m->fb = (uint32_t)f;
    sample(&m->pace, (uint32_t)(m->now % m->frame), m->vbi, m->fb);
}
static uint64_t next_vbi(const struct model *m, uint64_t t) {
    uint64_t base = t - t % m->frame + m->vbi;
    return base > t ? base : base + m->frame;
}
static void model_run(struct model *m, uint32_t frames) {
    m->now = m->vbi + m->latency;
    for(uint64_t end = (uint64_t)frames * m->frame; m->now < end;) {
        model_sample(m);
        uint32_t f0 = m->pace.frames, l0 = m->pace.line;
        uint32_t n = kui_retail_pace_budget(&m->pace, 2, m->maximum ? m->maximum : 8);
        uint64_t start = m->now, first = next_vbi(m, start);
        m->now += m->overhead;
        for(uint32_t i = 0; i < n; ++i) {
            m->now += m->cost;
            model_sample(m);
        }
        kui_retail_pace_measure(&m->pace, f0, l0, n);
        ++m->steps; m->sectors += n;
        if(n > 2) ++m->extended;
        if(n > m->max_step) m->max_step = n;
        /* Crossing one vblank is intended; a second would lose one. */
        if(m->now > first + m->frame) ++m->double_cross;
        /* The next server call follows the next vblank-in the step did not
         * already cover; a pending crossed vblank runs as soon as we return.
         * A main loop with its own work instead waits for a fresh vblank. */
        uint64_t after = next_vbi(m, m->now + m->work);
        m->now = (m->work || m->now <= first ? after : m->now) + m->latency;
    }
}

static void simulations(void) {
    /* NTSC field timing, ~4.8 ms/sector, handler 1 ms after vblank-in. */
    struct model m = {.frame = 262, .vbi = 260, .cost = 76, .overhead = 5, .latency = 16};
    model_run(&m, 1200);
    CHECK(m.double_cross == 0 && m.max_step == 6);
    CHECK(m.sectors * 100u >= 1200u * 280u); /* about 3 sectors per frame, not 2 */
    /* Presenting frames keeps every step at the accepted two sectors, even
     * when flips slow to one every eighth frame. */
    struct model busy = {.frame = 262, .vbi = 260, .cost = 76, .overhead = 5, .latency = 16,
                         .flips = 8};
    model_run(&busy, 1200);
    CHECK(busy.extended == 0 && busy.max_step == 2 && busy.sectors == 2u * busy.steps);
    /* VGA, and a card 25% slower than measured on the owner's console. */
    struct model vga = {.frame = 525, .vbi = 520, .cost = 190, .overhead = 10, .latency = 32};
    model_run(&vga, 1200);
    CHECK(vga.double_cross == 0 && vga.extended > 0);
    CHECK(vga.sectors * 100u >= 1200u * 230u);
    /* PAL: 20 ms frames allow more sectors; the maximum still applies. */
    struct model pal = {.frame = 312, .vbi = 310, .cost = 75, .overhead = 5, .latency = 16};
    model_run(&pal, 1200);
    CHECK(pal.double_cross == 0 && pal.max_step > 6 && pal.max_step <= 8);
    /* A main loop that works after each step, then waits for the next vblank,
     * may lose a frame to a longer step, but never falls below the fixed
     * two-sector rate for the same work. */
    for(uint32_t work = 1; work < 262; work += 7) {
        struct model paced = {.frame = 262, .vbi = 260, .cost = 76, .overhead = 5,
                              .latency = 16, .work = work};
        struct model fixed = paced;
        fixed.maximum = 2;
        model_run(&paced, 600); model_run(&fixed, 600);
        CHECK(paced.double_cross == 0 && paced.sectors >= fixed.sectors);
    }
}

int main(void) {
    sampling(); budgets(); measurement(); spinning(); simulations();
    printf("retail read pacing: %u checks passed\n", assertions);
    return 0;
}
