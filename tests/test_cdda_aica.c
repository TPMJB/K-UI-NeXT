/* SPDX-License-Identifier: GPL-3.0-only */
/* Standalone AICA/G2 register model. This proves software contracts, not
 * AICA bus timing or sound quality; those still require the console. */
#include "cdda_aica.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define AICA UINT32_C(0xa0700000)
#define SOUND_RAM UINT32_C(0xa0800000)
#define FIFO UINT32_C(0xa05f688c)
#define TCNT1 UINT32_C(0xffd80018)
#define G2_DMA UINT32_C(0xa05f7800)
#define LEFT KUI_CDDA_AICA_LEFT_OFFSET
#define RIGHT KUI_CDDA_AICA_RIGHT_OFFSET
static struct {
    uint32_t regs[0x3000u / 4u], dma[0x80u / 4u];
    uint8_t pcm[2][32768];
    uint32_t ticks, tick_step, positions[2];
    unsigned fifo_bytes, fifo_reads, pcm_writes, selectors, delays;
    unsigned starts, first_start_left, first_start_right, executions;
    unsigned crossing_write, busy_polls, advance_busy;
    bool running[2], stuck_fifo;
} hw;
static unsigned checks;
#define CHECK(t) do { ++checks; assert(t); } while(0)
static uint32_t reg(unsigned c, unsigned offset) {
    return hw.regs[(c * 0x80u + offset) / 4u];
}
static void reset(void) {
    memset(&hw, 0, sizeof(hw));
    memset(hw.regs, 0xff, sizeof(hw.regs));
    memset(hw.pcm, 0x7b, sizeof(hw.pcm));
    hw.tick_step = 100;
    hw.regs[0x2c00u / 4u] = 0x200u;
}
uint32_t kui_cdda_aica_test_read(uint32_t a, unsigned width) {
    assert(width == 4u);
    if(a == TCNT1) {
        hw.ticks += hw.tick_step;
        return ~hw.ticks;
    }
    if(a == FIFO) {
        ++hw.fifo_reads;
        if(hw.advance_busy && (hw.stuck_fifo || hw.busy_polls)) {
            for(unsigned c = 0; c < 2u; ++c)
                hw.positions[c] = (hw.positions[c] + hw.advance_busy) & 16383u;
        }
        if(hw.stuck_fifo) return 0x31u;
        if(hw.busy_polls) { --hw.busy_polls; return 0x10u; }
        hw.fifo_bytes = 0;
        return 0;
    }
    if(a >= G2_DMA && a < G2_DMA + 0x80u)
        return hw.dma[(a - G2_DMA) / 4u];
    assert(a >= AICA && a < AICA + 0x3000u);
    if(a == AICA + 0x2814u) {
        unsigned channel = (hw.regs[0x280cu / 4u] >> 8) & 63u;
        assert(channel < 2u);
        return 0xabcd0000u | hw.positions[channel];
    }
    return hw.regs[(a - AICA) / 4u];
}
void kui_cdda_aica_test_write(uint32_t a, uint32_t v, unsigned width) {
    if(a >= G2_DMA && a < G2_DMA + 0x80u) {
        assert(width == 4u);
        hw.dma[(a - G2_DMA) / 4u] = v;
        return;
    }
    for(unsigned i = 0; i < 4u; ++i)
        assert(hw.dma[(i * 0x20u + 0x1cu) / 4u] == 1u);
    assert(!hw.stuck_fifo);
    hw.fifo_bytes += width;
    assert(hw.fifo_bytes <= 32u);
    if(a >= SOUND_RAM + LEFT && a < SOUND_RAM + RIGHT + 32768u) {
        unsigned channel;
        uint32_t offset;
        if(a < SOUND_RAM + LEFT + 32768u) {
            channel = 0; offset = a - SOUND_RAM - LEFT;
        } else {
            channel = 1; offset = a - SOUND_RAM - RIGHT;
        }
        assert(offset + width <= sizeof(hw.pcm[channel]));
        if(hw.running[channel]) {
            assert(offset / 16384u != hw.positions[channel] / 8192u);
        }
        for(unsigned i = 0; i < width; ++i)
            hw.pcm[channel][offset + i] = (uint8_t)(v >> (8u * i));
        ++hw.pcm_writes;
        if(hw.crossing_write && hw.pcm_writes == hw.crossing_write)
            hw.positions[0] = hw.positions[1] = 8192u;
        return;
    }
    assert(a >= AICA && a < AICA + 0x3000u && width == 4u);
    unsigned offset = a - AICA;
    hw.regs[offset / 4u] = v;
    if(offset == 0x280cu) ++hw.selectors;
    if(offset < 0x2000u && !(offset % 0x80u) && (v & 0x8000u)) {
        ++hw.executions;
        bool starting = false;
        for(unsigned c = 0; c < 2u; ++c) {
            bool keyon = (reg(c, 0) & 0x4000u) != 0;
            if(keyon && !hw.running[c]) starting = true;
            hw.running[c] = keyon;
        }
        if(starting) {
            ++hw.starts;
            hw.first_start_left = reg(0, 0);
            hw.first_start_right = reg(1, 0);
            hw.positions[0] = hw.positions[1] = 0;
        }
        /* KYONEX is consumed by the shared key command. */
        hw.regs[offset / 4u] &= ~0x8000u;
    }
}
void kui_cdda_aica_test_delay(void) { ++hw.delays; }
static void init(void) {
    reset();
    CHECK(kui_cdda_aica_init() == KUI_CDDA_AICA_OK);
}
static int16_t sample(unsigned channel, unsigned frame) {
    unsigned offset = frame * 2u;
    return (int16_t)((uint16_t)hw.pcm[channel][offset] |
                    (uint16_t)hw.pcm[channel][offset + 1u] << 8);
}
static void setup_and_start(void) {
    init();
    CHECK(hw.regs[0x2c00u / 4u] == 0x201u);
    CHECK(hw.regs[0x2800u / 4u] == 0);
    CHECK(hw.regs[0x289cu / 4u] == 0 && hw.regs[0x28b4u / 4u] == 0);
    CHECK(hw.regs[0x28a4u / 4u] == 0x7ffu && hw.regs[0x28bcu / 4u] == 0x7ffu);
    for(unsigned c = 0; c < 64u; ++c) CHECK(!(reg(c, 0) & 0x4000u));
    for(unsigned c = 0; c < 2u; ++c) {
        CHECK(reg(c, 0) == 0x210u);
        CHECK(reg(c, 4) == (c ? 0x8000u : 0));
        CHECK(reg(c, 8) == 0 && reg(c, 12) == 16384u);
        CHECK(reg(c, 16) == 0x1fu && reg(c, 20) == 0x1fu);
        CHECK(reg(c, 24) == 0);
        CHECK(reg(c, 36) == (c ? 0x0f0fu : 0x0f1fu));
        CHECK(reg(c, 40) == 0x24u);
    }
    CHECK(kui_cdda_aica_start() == KUI_CDDA_AICA_OK);
    CHECK(hw.starts == 1 && hw.running[0] && hw.running[1]);
    CHECK((hw.first_start_left & 0xc000u) == 0xc000u);
    CHECK((hw.first_start_right & 0xc000u) == 0x4000u);
    CHECK(hw.regs[0x2800u / 4u] == 0x0fu);
    CHECK(kui_cdda_aica_start() == KUI_CDDA_AICA_NOT_READY);
    hw.regs[(2u * 0x80u + 36u) / 4u] = 0x1234u;
    CHECK(kui_cdda_aica_stop() == KUI_CDDA_AICA_OK);
    CHECK(!hw.running[0] && !hw.running[1]);
    CHECK(hw.regs[0x2800u / 4u] == 0);
    CHECK(reg(2, 36) == 0x1234u);
}
static void writes_and_bounds(void) {
    init();
    int16_t l[128], r[128];
    for(unsigned i = 0; i < 128; ++i) { l[i] = (int16_t)(i * 257u); r[i] = (int16_t)~l[i]; }
    CHECK(kui_cdda_aica_write_samples(0, 0, l, r, 128) == KUI_CDDA_AICA_OK);
    for(unsigned i = 0; i < 128; ++i) {
        CHECK(sample(0, i) == l[i]); CHECK(sample(1, i) == r[i]);
    }
    CHECK(kui_cdda_aica_write_samples(1, 1, l, r, 127) == KUI_CDDA_AICA_OK);
    CHECK(sample(0, 8192) == 0x7b7b && sample(1, 8192) == 0x7b7b);
    for(unsigned i = 0; i < 127; ++i) {
        CHECK(sample(0, 8193 + i) == l[i]); CHECK(sample(1, 8193 + i) == r[i]);
    }
    CHECK(sample(0, 8320) == 0x7b7b && sample(1, 8320) == 0x7b7b);
    CHECK(kui_cdda_aica_write_samples(1, 8191, l, r, 1) == KUI_CDDA_AICA_OK);
    CHECK(sample(0, 16383) == l[0] && sample(1, 16383) == r[0]);
    unsigned writes = hw.pcm_writes;
    CHECK(kui_cdda_aica_write_samples(2, 0, l, r, 1) == KUI_CDDA_AICA_ARGUMENT);
    CHECK(kui_cdda_aica_write_samples(0, 8192, l, r, 1) == KUI_CDDA_AICA_ARGUMENT);
    CHECK(kui_cdda_aica_write_samples(0, 8191, l, r, 2) == KUI_CDDA_AICA_ARGUMENT);
    CHECK(kui_cdda_aica_write_samples(0, 0, l, r, 129) == KUI_CDDA_AICA_ARGUMENT);
    CHECK(kui_cdda_aica_write_samples(0, 0, NULL, r, 1) == KUI_CDDA_AICA_ARGUMENT);
    CHECK(kui_cdda_aica_write_samples(0, 0, l, r, 0) == KUI_CDDA_AICA_ARGUMENT);
    CHECK(hw.pcm_writes == writes);
}
static void position_and_deadlines(void) {
    int16_t l[128] = {0}, r[128] = {0};
    init(); CHECK(kui_cdda_aica_start() == KUI_CDDA_AICA_OK);
    uint32_t frame = 0xfefeu;
    hw.positions[0] = 100; hw.positions[1] = 102;
    CHECK(kui_cdda_aica_position(&frame) == KUI_CDDA_AICA_OK && frame == 100);
    CHECK(kui_cdda_aica_write_samples(1, 0, l, r, 128) == KUI_CDDA_AICA_OK);
    CHECK(hw.delays >= 66u); /* both before+after every 8-frame burst */
    unsigned writes = hw.pcm_writes;
    CHECK(kui_cdda_aica_write_samples(0, 0, l, r, 128) == KUI_CDDA_AICA_ACTIVE_HALF);
    CHECK(hw.pcm_writes == writes && !hw.running[0] && !hw.running[1]);
    init(); CHECK(kui_cdda_aica_start() == KUI_CDDA_AICA_OK);
    hw.positions[0] = 8190; hw.positions[1] = 8193;
    CHECK(kui_cdda_aica_write_samples(1, 0, l, r, 128) == KUI_CDDA_AICA_ACTIVE_HALF);
    CHECK(hw.pcm_writes == 0);
    init(); CHECK(kui_cdda_aica_start() == KUI_CDDA_AICA_OK);
    hw.positions[0] = hw.positions[1] = 8170;
    CHECK(kui_cdda_aica_write_samples(1, 0, l, r, 128) == KUI_CDDA_AICA_ACTIVE_HALF);
    CHECK(hw.pcm_writes == 0);
    init(); CHECK(kui_cdda_aica_start() == KUI_CDDA_AICA_OK);
    hw.positions[0] = 16380; hw.positions[1] = 4;
    CHECK(kui_cdda_aica_position(&frame) == KUI_CDDA_AICA_OK && frame == 16380);
    hw.positions[0] = 100; hw.positions[1] = 200;
    frame = 123456;
    CHECK(kui_cdda_aica_position(&frame) == KUI_CDDA_AICA_PHASE && frame == 123456);
    CHECK(!hw.running[0] && !hw.running[1] && hw.regs[0x2800u / 4u] == 0);
    init(); CHECK(kui_cdda_aica_start() == KUI_CDDA_AICA_OK);
    hw.positions[0] = hw.positions[1] = 16384;
    CHECK(kui_cdda_aica_position(&frame) == KUI_CDDA_AICA_PHASE);
    init(); CHECK(kui_cdda_aica_start() == KUI_CDDA_AICA_OK);
    hw.positions[0] = hw.positions[1] = 8100;
    hw.busy_polls = 2; hw.advance_busy = 100;
    CHECK(kui_cdda_aica_write_samples(1, 0, l, r, 128) == KUI_CDDA_AICA_ACTIVE_HALF);
    CHECK(hw.pcm_writes == 0); /* deadline passed while FIFO drained */
    init(); CHECK(kui_cdda_aica_start() == KUI_CDDA_AICA_OK);
    hw.crossing_write = 8; /* crossing immediately after first32byte burst */
    CHECK(kui_cdda_aica_write_samples(1, 0, l, r, 128) == KUI_CDDA_AICA_ACTIVE_HALF);
    CHECK(hw.pcm_writes == 8 && !hw.running[0] && !hw.running[1]);
}
static void bounded_bus_faults(void) {
    reset(); hw.stuck_fifo = true;
    CHECK(kui_cdda_aica_init() == KUI_CDDA_AICA_TIMEOUT);
    CHECK(hw.fifo_reads < 1500u); /* TMU timeout across init+cleanup */
    reset(); hw.stuck_fifo = true; hw.tick_step = 0;
    CHECK(kui_cdda_aica_init() == KUI_CDDA_AICA_TIMEOUT);
    CHECK(hw.fifo_reads <= 40000u); /* finite fallback on a stopped timer */
    reset(); hw.dma[0x78u / 4u] = 1;
    CHECK(kui_cdda_aica_init() == KUI_CDDA_AICA_NOT_READY);
    CHECK(hw.regs[0x2c00u / 4u] == 0x200u); /* no G2 access during active DMA */
    init(); CHECK(kui_cdda_aica_start() == KUI_CDDA_AICA_OK);
    hw.stuck_fifo = true;
    uint32_t frame = 4242;
    CHECK(kui_cdda_aica_position(&frame) == KUI_CDDA_AICA_TIMEOUT && frame == 4242);
    CHECK(kui_cdda_aica_start() == KUI_CDDA_AICA_NOT_READY);
    hw.stuck_fifo = false;
    CHECK(kui_cdda_aica_init() == KUI_CDDA_AICA_OK);
}
static void restart_and_clock_wrap(void) {
    int16_t l[128] = {0}, r[128] = {0};
    init(); CHECK(kui_cdda_aica_start() == KUI_CDDA_AICA_OK);
    hw.positions[0] = 4567; hw.positions[1] = 4569;
    uint32_t played_position = 0;
    CHECK(kui_cdda_aica_position(&played_position) == KUI_CDDA_AICA_OK);
    CHECK(played_position == 4567);
    CHECK(kui_cdda_aica_stop() == KUI_CDDA_AICA_OK);
    /* The caller's saved cursor survives stop. Reprime before a fresh start;
     * the hardware key-on begins the ring at0, not the previous4567 frames. */
    CHECK(kui_cdda_aica_write_samples(0, 0, l, r, 128) == KUI_CDDA_AICA_OK);
    CHECK(kui_cdda_aica_write_samples(1, 0, l, r, 128) == KUI_CDDA_AICA_OK);
    CHECK(kui_cdda_aica_start() == KUI_CDDA_AICA_OK);
    uint32_t frame = 999;
    CHECK(kui_cdda_aica_position(&frame) == KUI_CDDA_AICA_OK && frame == 0);
    CHECK(played_position == 4567 && hw.starts == 2);
    /* Stop has no promise to retain a synchronized playback cursor. A legal
     * monitor state after key-off may have independently frozen channels. */
    CHECK(kui_cdda_aica_stop() == KUI_CDDA_AICA_OK);
    hw.positions[0] = 500; hw.positions[1] = 540;
    frame = 999;
    CHECK(kui_cdda_aica_position(&frame) == KUI_CDDA_AICA_PHASE && frame == 999);
    /* Bounded FIFO timeouts must work across the32-bit TMU wrap. */
    init(); CHECK(kui_cdda_aica_start() == KUI_CDDA_AICA_OK);
    hw.ticks = UINT32_MAX - 5000u;
    hw.stuck_fifo = true;
    unsigned reads = hw.fifo_reads, writes = hw.pcm_writes;
    CHECK(kui_cdda_aica_position(&frame) == KUI_CDDA_AICA_TIMEOUT && frame == 999);
    CHECK(hw.ticks < 200000u && hw.fifo_reads - reads < 1500u);
    CHECK(kui_cdda_aica_write_samples(1, 0, l, r, 128) == KUI_CDDA_AICA_NOT_READY);
    CHECK(hw.pcm_writes == writes);
}
int main(void) {
    setup_and_start(); writes_and_bounds(); position_and_deadlines(); bounded_bus_faults();
    restart_and_clock_wrap();
    printf("cdda AICA: %u checks passed\n", checks);
    return 0;
}
