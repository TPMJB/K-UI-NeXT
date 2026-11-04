/* SPDX-License-Identifier: GPL-3.0-only */
#include "fmv_toy_profile.h"
#undef KUI_TOY_FMV_IDLE_REARM
#define KUI_TOY_FMV_IDLE_REARM 0
#ifndef KUI_TOY_DEMAND_TEST_PROFILE
#define KUI_TOY_DEMAND_TEST_PROFILE 1
#endif
#include "../src/loader/toy_loader_trace_report.c"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
static struct kui_toy_loader_trace_report trace_fixture;
static struct kui_toy_loader_data_probe_report probe_fixture;
static uint32_t sched_fixture[KUI_TOY_SCHEDULE_REPORT_WORDS];
static struct kui_toy_demand_test_stats demand_fixture;
static struct kui_toy_pilot_snapshot audio_fixture;
static jmp_buf terminal_exit;
static unsigned order,pages,lines,identity_seen,yields_seen,sr_seen,audio_seen;
const uint32_t *kui_toy_loader_trace_words(void) {return (const void *)&trace_fixture;}
void kui_toy_loader_trace_freeze(void) {assert(order++==0u);trace_fixture.frozen=1u;}
const uint32_t *kui_toy_loader_data_probe_words(void) {return (const void *)&probe_fixture;}
void kui_toy_loader_data_probe_freeze(void) {assert(order++==1u);probe_fixture.frozen=1u;}
const struct kui_toy_demand_test_stats *kui_toy_demand_test_snapshot(void) {
    assert(order++==2u);return &demand_fixture;
}
uint32_t kui_toy_demand_test_fingerprint_skips(void) {
    return KUI_TOY_DEMAND_SKIP_FINGERPRINT?9u:0u;
}
const uint32_t *kui_toy_loader_schedule_report_words(void) {return sched_fixture;}
void kui_toy_loader_schedule_report_freeze(void) {
    assert(order++==3u);memset(&demand_fixture,0,sizeof(demand_fixture));
}
const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void) {return &audio_fixture;}
uint32_t kui_toy_pilot_request(uint32_t command,uint32_t a,uint32_t b,uint32_t c) {
    assert(order++==4u && command==KUI_TOY_PILOT_RESET && !a && !b && !c);
    memset(&audio_fixture,0,sizeof(audio_fixture));return 1u;
}
void retail_display_restore(const struct retail_display_state *s) {(void)s;lines=0u;}
void retail_display_line(const char *s) {(void)s;assert(++lines<=16u);}
void retail_display_values(const char *label,const uint32_t *words,unsigned count) {
    assert(count==4u);
    lines+=2u;assert(lines<=16u); /* 108..444 at the real 22-pixel line pitch. */
    if(!strcmp(label,"PROFILE CRC32SKIPS PLAYS SAT")) {
        assert(words[0]==KUI_TOY_DEMAND_TEST_PROFILE);
        assert(words[1]==(KUI_TOY_DEMAND_SKIP_FINGERPRINT?9u:0u));++identity_seen;
    } else if(!strcmp(label,"VISITS STEPS WAITPOLLS YIELDS")) {
        assert(words[0]==10u && words[1]==20u && words[2]==30u && words[3]==40u);++yields_seen;
    } else if(!strcmp(label,"BEFORE MASK 0-6 7-14 15 BL")) {
        assert(words[0]==7u && words[1]==2u && words[2]==3u && words[3]==4u);++sr_seen;
    } else if(!strcmp(label,"AUDIO STATE COMMAND TRACK GENERATION")) {
        assert(words[0]==11u && words[1]==22u && words[2]==33u && words[3]==44u);++audio_seen;
    }
}
void retail_display_pause(uint32_t frames) {
    assert(frames==360u);if(++pages==5u)longjmp(terminal_exit,1);
}
int main(void) {
    demand_fixture=(struct kui_toy_demand_test_stats){10u,20u,30u,40u,0u,0u,0u,0u};
    audio_fixture.magic=KUI_TOY_PILOT_MAGIC;audio_fixture.version=8u;audio_fixture.bytes=448u;
    audio_fixture.state=11u;audio_fixture.command=22u;audio_fixture.track=33u;audio_fixture.generation=44u;
    probe_fixture.phase[0].sr_buckets[6]=7u;probe_fixture.phase[0].sr_buckets[7]=2u;
    probe_fixture.phase[0].sr_buckets[15]=3u;probe_fixture.phase[0].sr_buckets[20]=4u;
    struct retail_display_state display={0};
    kui_toy_loader_trace_report_capture(&display,NULL,NULL);
    assert(order==5u && trace_fixture.frozen && probe_fixture.frozen);
    assert(!kui_toy_loader_trace_report_page(0u,0u));
    if(!setjmp(terminal_exit))fmv_terminal();
    assert(pages==5u && identity_seen==1u && yields_seen==1u && sr_seen==1u && audio_seen==1u);
    puts("PASS: five-page Toy report freezes before cleanup, preserves engagement/audio, and labels inherited caller SR");
}
