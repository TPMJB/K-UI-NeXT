/* SPDX-License-Identifier: GPL-3.0-only */
#include "fmv_toy_profile.h"
#undef KUI_TOY_FMV_IDLE_REARM
#define KUI_TOY_FMV_IDLE_REARM 0
#ifndef KUI_TOY_DEMAND_TEST_PROFILE
#define KUI_TOY_DEMAND_TEST_PROFILE 3
#endif
#define KUI_TOY_LOADER_SCHEDULE_REPORT_HOST_TEST 1
#include "../src/loader/toy_loader_schedule_report.c"
#include <assert.h>
#include <stdio.h>
static struct kui_toy_pilot_sci_stats engine;
static uint32_t clock_value=UINT32_MAX,map_calls;
static uint8_t bytes[2048];
uint32_t kui_toy_loader_schedule_report_host_read(uint32_t a,unsigned n) {
    (void)n;
    switch(a) {
    case 0xffc00000u:return 0x0e0au;
    case 0xffd80008u:return UINT32_MAX;
    case 0xffd80010u:return 2u;
    case 0xffd80004u:return 1u;
    case 0xffd8000cu:return clock_value--;
    default:assert(0);return 0;
    }
}
const struct kui_toy_pilot_sci_stats *kui_toy_pilot_sci_snapshot(void) {return &engine;}
static uint8_t *map(void *context,uint32_t address,uint32_t count,int writing) {
    (void)context;assert(address==0x8c100000u && count==2048u && !writing);
    ++map_calls;return bytes;
}
int main(void) {
    struct kui_retail_gd s={0};s.ops.map=map;
    kui_toy_loader_schedule_report_begin(&s);
    s.command=KUI_GD_DMAREAD;s.token=7u;s.lba=150u;s.count=2u;
    s.destination=0x8c100000u;s.sector_bytes=2048u;s.pending=1u;
    kui_toy_loader_schedule_report_end(&s,KUI_GD_REQUEST,KUI_GD_DMAREAD,7);
    for(unsigned i=0;i<3u;++i) {
        kui_toy_loader_schedule_report_begin(&s);s.completed_bytes=2048u;
        kui_toy_loader_schedule_report_end(&s,KUI_GD_EXEC,0u,0);
    }
    const uint32_t *q=schedule_report+48u;
    assert(q[8]==2048u && q[2]==150u && q[3]==2u && (q[7]&2u));
#if KUI_TOY_DEMAND_SKIP_FINGERPRINT
    assert(kui_toy_demand_test_fingerprint_skips()==1u && !map_calls);
    assert((q[7]&1024u) && !(q[7]&1u) && !q[11]);
#else
    assert(!kui_toy_demand_test_fingerprint_skips() && map_calls==1u);
    assert((q[7]&1u) && !(q[7]&1024u) && q[11]==0xf1e8ba9eu);
#endif
    assert(!s.error && s.pending && s.completed_bytes==2048u);
    puts("PASS: diagnostic fingerprint is measured once or explicitly skipped once, without changing DATA credit");
}
