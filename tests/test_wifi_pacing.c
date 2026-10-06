/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the actual console transport and the application's checked
 * feedback using the real bridge/link behind an emulated SCI peripheral.
 * The slave may still be unarmed when clocked; virtual time makes the
 * scheduler and end-of-transfer gaps observable without wall-clock sleeps. */
#include "wifi_model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/dreamcast/wifi_sci.c"

static uint64_t clock_us, armed_us, last_end, last_start;
static unsigned slave_delay, yields, yield_us, early, clocks, corrupt;
static bool running, wired, arm_pending, physical_level, dma_available;
static bool stale_once, hold_idle, wrong_echo;
static uint8_t previous[KWL_FRAME_MAX], idle_frame[KWL_FRAME_MAX];
static size_t previous_len, idle_len;

static void armed(void) {
    if(arm_pending && clock_us >= armed_us) {
        physical_level = !physical_level;
        arm_pending = false;
    }
}
uint64_t timer_us_gettime64(void) { armed(); return clock_us++; }
uint64_t timer_ms_gettime64(void) { return clock_us / 1000u; }
void thd_pass(void) { ++yields; clock_us += yield_us; armed(); }
void thd_sleep(unsigned ms) { clock_us += (uint64_t)ms * 1000u; armed(); }
bool kui_sci_open(unsigned rate, unsigned select) {
    assert(rate < KUI_SCI_RATES);
    running = wifi_model_port.open((select == 7u ? WIFI_MODEL_RATES : 0u) + (rate ? 1u : 0u));
    return running;
}
void kui_sci_close(void) { wifi_model_port.close(); running = false; }
bool kui_sci_running(void) { return running; }
void kui_sci_select(bool active) { (void)active; }
bool kui_sci_ready(void) { armed(); return wired && physical_level; }
bool kui_sci_dma_ready(void) { return dma_available; }
static bool peripheral(const uint8_t *out, uint8_t *in, size_t bytes) {
    assert(running);
    last_start = clock_us;
    ++clocks;
    armed();
    if(stale_once || hold_idle) {
        const uint8_t *source = hold_idle ? idle_frame : previous;
        size_t n = hold_idle ? idle_len : previous_len;
        assert(n && bytes >= n);
        memcpy(in, source, n);
        memset(in + n, 0, bytes - n);
        stale_once = false;
    } else if(arm_pending) {
        /* Unarmed clocks never reach the bridge and do not postpone its
         * already scheduled re-arm. */
        memset(in, 0xff, bytes);
        ++early;
    } else {
        bool ready;
        assert(wifi_model_port.bus->transfer(NULL, out, in, bytes, &ready));
        if(wrong_echo && (in[2] & KWL_F_DATA)) {
            const uint8_t *cursor = in + KWL_HEADER;
            struct kwm message;
            while(kwm_next(&cursor, in + KWL_HEADER + kwl_get16(in + 6), &message)) {
                if(message.type == KWM_ECHO_R && message.len) {
                    /* A CRC-correct frame containing the wrong echo is
                     * not sufficient to enable the faster pacing. */
                    ((uint8_t *)message.body)[0] ^= 1u;
                    kwl_put32(in + 12, kwl_crc32(kwl_crc32(0, in, 12), in + KWL_HEADER, kwl_get16(in + 6)));
                }
            }
        }
        bool damaged = corrupt != 0;
        if(corrupt) { in[12] ^= 1u; --corrupt; }
        if(!damaged) {
            memcpy(previous, in, bytes);
            previous_len = bytes;
            if(!(in[2] & KWL_F_DATA)) { memcpy(idle_frame, in, bytes); idle_len = bytes; }
        }
        arm_pending = true;
        armed_us = clock_us + (bytes * 16u + 24u) / 25u + slave_delay;
    }
    clock_us += (bytes * 16u + 24u) / 25u;
    last_end = clock_us;
    return true;
}
bool kui_sci_dma_transfer(const uint8_t *out, uint8_t *in, size_t bytes) { return peripheral(out, in, bytes); }
int sci_spi_rw_data(const uint8_t *out, uint8_t *in, size_t bytes) { return peripheral(out, in, bytes) ? SCI_OK : -1; }

static void device(bool ready_wire, unsigned rearm_us, bool with_dma) {
    wired = ready_wire;
    slave_delay = rearm_us;
    dma_available = with_dma;
    clock_us = armed_us = last_end = last_start = 0;
    yields = early = clocks = corrupt = 0;
    yield_us = 100u;
    arm_pending = physical_level = stale_once = hold_idle = wrong_echo = false;
    previous_len = idle_len = 0;
    struct wifi_model_options options = {false, 0, 0, !ready_wire, KWM_WIFI_ONLINE};
    wifi_model_start(&options);
}
static struct kui_wifi_session *start(bool ready_wire, unsigned rearm_us, bool with_dma) {
    device(ready_wire, rearm_us, with_dma);
    struct kui_wifi_session *s = calloc(1, sizeof(*s));
    assert(s && kui_wifi_session_find(s, kui_wifi_console_port(), NULL));
    return s;
}
static void finish(struct kui_wifi_session *s) {
    kui_wifi_session_end(s);
    free(s);
    wifi_model_stop();
}
static void echo(struct kui_wifi_session *s) {
    uint8_t data[2048];
    memset(data, (int)(s->host.counts.echo & 0xffu), sizeof data);
    uint32_t before = s->host.counts.echo;
    assert(kwh_echo(&s->host, data, sizeof data));
    for(unsigned i = 0; i < 32u && s->host.counts.echo == before; ++i) assert(kui_wifi_session_step(s));
    assert(s->host.counts.echo == before + 1u && s->host.echo_len == sizeof data);
    assert(!memcmp(s->host.echo, data, sizeof data));
}
static bool never(void) { return false; }
static void training(void) {
    device(false, 300u, true);
    wrong_echo = true;
    struct kui_wifi_session *s = calloc(1, sizeof(*s));
    assert(s && !kui_wifi_session_find(s, kui_wifi_console_port(), NULL));
    assert(s->answered && !pacing.trained && gap_us(NULL) == 20000u);
    free(s);
    wifi_model_stop();
    device(false, 300u, true);
    struct kui_app_status view;
    assert(kui_wifi_network_inspect(&view, NULL, never));
    assert(view.passed && strstr(view.lines[7], "READY absent; transfer gap 500 us"));
    wifi_model_stop();
    puts("PASS CRC-correct wrong echoes cannot train; inspection reports active fallback gap");
}
static void no_ready(void) {
    struct kui_wifi_session *s = start(false, 300u, true);
    assert(pacing.trained && !pacing.wired && !s->ready_changes && gap_us(NULL) == 500u);
    assert(s->host.counts.hello == 1u && s->host.counts.echo == 4u && yields);
    uint64_t before = last_end;
    /* A real scheduler handoff can take much longer than the intended gap.
     * Trained short waits must remain precise even with an 8 ms handoff. */
    yield_us = 8000u;
    unsigned previous_yields = yields;
    assert(kui_wifi_session_step(s));
    assert(last_start >= before + 500u && last_start < before + 550u && yields == previous_yields);
    unsigned sleep_gap = gap_us(NULL);
    clock_us += 5000u;
    before = clock_us;
    assert(kui_wifi_session_step(s));
    assert(last_start < before + 50u && gap_us(NULL) == sleep_gap);
    echo(s);
    assert(!early && !s->host.link.stats.bad);
    finish(s);
    printf("PASS full-frame HELLO/echo training; 500us gaps stay precise despite 8ms scheduler handoffs\n");
}
static void delayed(void) {
    struct kui_wifi_session *s = start(false, 2500u, false);
    echo(s);
    assert(early && gap_us(NULL) >= 4000u);
    for(unsigned i = 0; i < 8u && gap_us(NULL) < 4000u; ++i) echo(s);
    unsigned bounded = early;
    for(unsigned i = 0; i < 50u; ++i) echo(s);
    assert(early - bounded < 15u && gap_us(NULL) <= 20000u);
    assert(!s->host.link.stats.bad);
    finish(s);
    printf("PASS delayed slave rearm: premature clocks recover without losing echo bytes (PIO)\n");
}
static void errors(void) {
    struct kui_wifi_session *s = start(false, 300u, true);
    static const unsigned backoff[] = {1000u, 2000u, 4000u, 8000u, 20000u};
    for(unsigned i = 0; i < sizeof(backoff) / sizeof(backoff[0]); ++i) {
        corrupt = 1u;
        assert(kui_wifi_session_step(s));
        assert(gap_us(NULL) == backoff[i]);
    }
    assert(s->host.link.stats.bad == 5u);
    for(unsigned i = 0; i < 50u; ++i) assert(kui_wifi_session_step(s));
    assert(gap_us(NULL) == 20000u); /* idle valid CRCs cannot retrain */
    for(unsigned i = 0; i < 100u; ++i) echo(s);
    assert(gap_us(NULL) == 500u);
    /* A previously valid ECHO_R cannot be delivered twice, and its CRC is
     * not sufficient proof that an active transfer was accepted. */
    assert(previous[2] & KWL_F_DATA);
    uint32_t count = s->host.counts.echo, duplicates = s->host.link.stats.duplicates;
    uint8_t sample[2048] = {0};
    assert(kwh_echo(&s->host, sample, sizeof sample));
    stale_once = true;
    assert(kui_wifi_session_step(s));
    assert(s->host.counts.echo == count && s->host.link.stats.duplicates > duplicates && gap_us(NULL) == 1000u);
    for(unsigned i = 0; i < 32u && s->host.counts.echo == count; ++i) assert(kui_wifi_session_step(s));
    assert(s->host.counts.echo == count + 1u);
    finish(s);
    printf("PASS CRC failures back off 500us/1/2/4/8/20ms; genuine progress recovers; stale duplicates do not\n");
}
static void stall_reset(void) {
    struct kui_wifi_session *s = start(false, 300u, true);
    for(unsigned i = 0; i < 4u; ++i) assert(kui_wifi_session_step(s));
    assert(idle_len);
    uint8_t sample[64] = {0};
    assert(kwh_echo(&s->host, sample, sizeof sample));
    hold_idle = true;
    for(unsigned i = 0; i < 15u; ++i) assert(kui_wifi_session_step(s));
    assert(gap_us(NULL) == 20000u && !s->host.link.stats.bad);
    hold_idle = false;
    for(unsigned i = 0; i < 20u; ++i) assert(kui_wifi_session_step(s));
    assert(s->host.counts.echo == 5u);
    wifi_model_restart();
    arm_pending = false;
    for(unsigned i = 0; i < 8u && !s->host.counts.lost; ++i) assert(kui_wifi_session_step(s));
    assert(s->host.counts.lost && !pacing.trained && gap_us(NULL) == 20000u);
    for(unsigned i = 0; i < 10u; ++i) assert(kui_wifi_session_step(s));
    assert(s->host.ready && !pacing.trained); /* HELLO alone is insufficient */
    kui_wifi_session_end(s);
    assert(kui_wifi_session_find(s, kui_wifi_console_port(), NULL));
    assert(pacing.trained && gap_us(NULL) == 500u); /* all four echoes again */
    finish(s);
    printf("PASS active stale ACKs back off, reset requires full HELLO/echo retraining\n");
}
static void handshake(void) {
    struct kui_wifi_session *s = start(true, 12000u, true);
    assert(pacing.wired && gap_us(NULL) == 0u && s->ready_changes);
    uint64_t before = last_end;
    assert(kui_wifi_session_step(s));
    assert(last_start >= before + 12000u && !early);
    /* A disconnected/stuck line after it worked retains the 20ms timeout. */
    wired = false;
    before = clock_us;
    assert(kui_wifi_session_step(s));
    assert(last_start >= before + 20000u && gap_us(NULL) == 0u);
    finish(s);
    printf("PASS wired READY controls transfers and retains its 20ms timeout when stuck\n");
}
int main(void) {
    training();
    no_ready();
    delayed();
    errors();
    stall_reset();
    handshake();
    puts("test_wifi_pacing: all passed");
    return 0;
}
