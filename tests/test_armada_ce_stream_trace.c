/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Exercise the actual bounded recorder with authored timestamps/counters.
 * This does not model console scheduling or assert hardware timing. */
#include "../src/loader/armada_ce_stream_trace.c"

static void reset(void) { memset(&trace,0,sizeof(trace));memset(&supply_trace,0,sizeof(supply_trace)); }
static struct kui_armada_ce_stream_underrun_snapshot capture(void) {
    struct kui_armada_ce_stream_underrun_snapshot result;
    kui_armada_ce_stream_underrun_snapshot(&result);return result;
}
static void sample(uint32_t now,uint32_t source,uint32_t ready,uint32_t staged,
                   const struct kui_armada_ce_stream_trace_supply *s) {
    kui_armada_ce_stream_trace_observe(now,source,ready,4u,staged,0x203u,s,false);
}
int main(void) {
    unsigned cases=0;
    reset();
    struct kui_armada_ce_stream_underrun_snapshot out=capture(),zero={0};
    assert(!memcmp(&out,&zero,sizeof(out)));++cases;

    /* No read/entry/progress evidence is unavailable, not a measurement of
     * zero. A supplied object without the active flag cannot fabricate it. */
    struct kui_armada_ce_stream_trace_supply p={1u,2u,512u,37632u,1u,50u,20u,2u};
    kui_armada_ce_stream_trace_observe(70u,200u,64u,128u,32u,2u,&p,true);
    out=capture();assert(out.rows[0][0]==70u && out.rows[0][1]==64u &&
        out.rows[0][2]==128u && out.rows[0][3]==32u && out.rows[0][4]==UINT32_MAX);
    for(unsigned i=0;i<5u;i++) assert(out.rows[1][i]==UINT32_MAX);
    assert(out.rows[2][0]==UINT32_MAX && out.rows[2][1]==0u);
    for(unsigned i=0;i<5u;i++) assert(out.rows[3][i]==UINT32_MAX);
    ++cases;

    reset();
    kui_armada_ce_stream_trace_source_begin(100u);
    sample(100u,1000u,9000u,0u,&p);
    p.data_blocks+=10u;p.audio_blocks+=5u;p.preemptions++;
    sample(200u,2000u,8000u,64u,&p);
    p.data_blocks+=20u;p.audio_blocks+=7u;
    sample(300u,3000u,7000u,128u,&p);
    /* This observation does not rotate the 100ms ring or its delta base. */
    p.data_blocks++;sample(340u,3100u,6000u,192u,&p);
    kui_armada_ce_stream_trace_worker_begin(350u);
    kui_armada_ce_stream_trace_sleep(1u);
    kui_armada_ce_stream_trace_worker_begin(390u);
    kui_armada_ce_stream_trace_source_progress(370u);
    kui_armada_ce_stream_trace_ring_progress(380u);
    kui_armada_ce_stream_trace_budget_yield();kui_armada_ce_stream_trace_budget_yield();
    kui_armada_ce_stream_trace_copy_retry();
    p.data_blocks+=3u;p.audio_blocks+=2u;p.preemptions++;
    p.bytes=4096u;p.retries=3u;p.owner=1u;
    /* Freeze before inserting the 400ms sample. Recovery cannot erase the
     * three observations leading to the underrun. */
    kui_armada_ce_stream_trace_observe(400u,4000u,320u,640u,448u,0x207u,&p,true);
    out=capture();assert(out.rows[0][4]==300u);
    const uint32_t ages[5]={40u,1u,10u,30u,20u};
    assert(!memcmp(out.rows[1],ages,sizeof(ages)));
    const uint32_t transport[5]={1u,1u,4096u,37632u,3u};
    assert(!memcmp(out.rows[2],transport,sizeof(transport)));
    const uint32_t differences[5]={4u,2u,1u,2u,1u};
    assert(!memcmp(out.rows[3],differences,sizeof(differences)));
    for(unsigned i=0;i<3u;i++) {
        assert(out.rows[4u+i][0]==100u*(i+1u));
        assert(out.rows[4u+i][1]==1000u*(i+1u));
        assert((out.rows[4u+i][4]&(3u<<16))==(2u<<16));
        assert((out.rows[4u+i][4]&(15u<<20))==(1u<<20));
    }++cases;

    struct kui_armada_ce_stream_underrun_snapshot frozen=out;
    for(unsigned i=0;i<8u;i++) {
        p.data_blocks+=99u;p.audio_blocks+=100u;
        kui_armada_ce_stream_trace_source_progress(500u+i*100u);
        kui_armada_ce_stream_trace_ring_progress(500u+i*100u);
        kui_armada_ce_stream_trace_budget_yield();
        sample(500u+i*100u,5000u+i,10000u,0u,&p);
    }
    out=capture();assert(!memcmp(&out,&frozen,sizeof(out)));++cases;
    kui_armada_ce_stream_trace_observe(1300u,6000u,16u,128u,256u,0x203u,&p,true);
    out=capture();assert(out.rows[0][0]==1300u && out.rows[0][1]==16u);
    assert(out.rows[4][0]==1000u && out.rows[5][0]==1100u && out.rows[6][0]==1200u);++cases;

    /* Unsigned deltas retain their meaning across one clock/counter wrap. */
    reset();p.data_blocks=UINT32_MAX-2u;p.audio_blocks=UINT32_MAX-1u;p.preemptions=UINT32_MAX;
    sample(UINT32_MAX-99u,0u,512u,0u,&p);
    kui_armada_ce_stream_trace_worker_begin(UINT32_MAX-19u);
    kui_armada_ce_stream_trace_worker_begin(5u);
    kui_armada_ce_stream_trace_source_progress(UINT32_MAX-9u);
    kui_armada_ce_stream_trace_ring_progress(UINT32_MAX-4u);
    p.data_blocks=2u;p.audio_blocks=1u;p.preemptions=1u;
    kui_armada_ce_stream_trace_observe(20u,0u,4u,128u,0u,0x203u,&p,true);
    out=capture();assert(out.rows[1][0]==25u && out.rows[1][2]==15u &&
        out.rows[1][3]==30u && out.rows[1][4]==25u);
    assert(out.rows[3][0]==5u && out.rows[3][1]==3u && out.rows[3][2]==2u);++cases;

    /* Missing transport evidence in the preceding sample is not interpreted
     * as a zero baseline for subsequent cumulative block counts. */
    reset();kui_armada_ce_stream_trace_observe(100u,0u,256u,0u,0u,2u,NULL,false);
    kui_armada_ce_stream_trace_budget_yield();
    kui_armada_ce_stream_trace_observe(150u,0u,4u,128u,0u,3u,&p,true);
    out=capture();assert(out.rows[3][0]==UINT32_MAX && out.rows[3][1]==UINT32_MAX &&
        out.rows[3][2]==UINT32_MAX && out.rows[3][3]==1u && out.rows[3][4]==0u);++cases;
    puts("armada event-correlated underrun trace: 7 cases passed");
    assert(cases==7u);return 0;
}
