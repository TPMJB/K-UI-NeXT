/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/loader/armada_ce_stream_demand_diagnostic.c"

static struct kui_retail_async_region fixture;
static struct kui_retail_demand_clock clock_now;
static unsigned clock_calls;
static bool attached;
bool kui_armada_ce_stream_transport_attached(void) {return attached;}
void kui_retail_demand_test_clock(struct kui_retail_demand_clock *out) {
    *out=clock_now;++clock_calls;
}
bool kui_retail_demand_test_region(uintptr_t address) {
    return address==(uintptr_t)&fixture;
}
void kui_armada_ce_excerpt_snapshot(struct kui_armada_ce_excerpt_snapshot *out) {
    memset(out,0,sizeof(*out));out->command=33u;
}
void kui_armada_ce_stream_progress(uint32_t out[3]) {out[0]=out[1]=out[2]=0u;}
void kui_armada_ce_stream_fault(uint32_t out[5]) {memset(out,0,5u*sizeof(*out));}
static void reset(void) {
    memset(&demand,0,sizeof(demand));memset(&fixture,0,sizeof(fixture));
    clock_now=(struct kui_retail_demand_clock){900u,1000u,2u,1u,0xe0au,0xc023u,100u,0u};
    clock_calls=0u;
    attached=true;
}
static void advance(uint32_t cycles) {
    uint32_t before=clock_now.cycles_lo;clock_now.cycles_lo+=cycles;
    if(clock_now.cycles_lo<before) ++clock_now.cycles_hi;
    clock_now.ticks=clock_now.ticks>=10u?clock_now.ticks-10u:clock_now.ticks+991u;
}
static const struct kui_retail_demand_page *event(uint32_t type,uint32_t fn,uint32_t value) {
    return kui_armada_ce_demand_event(type|(fn<<8),value,(uintptr_t)&fixture);
}
static const struct kui_retail_demand_page *page(unsigned p) {
    return event(KUI_RETAIL_DEMAND_REPORT,p,0u);
}
static void clock_contract(void) {
    struct kui_retail_demand_clock a={900u,1000u,2u,1u,0xe0au,0xc023u,0xfffffff0u,3u},b=a;
    b.ticks=800u;b.cycles_lo=0x20u;b.cycles_hi=4u;
    struct kui_retail_demand_elapsed t=kui_retail_demand_elapsed(&a,&b);
    assert(t.modulo==100u && t.cycles==48u);
    assert((t.flags&(KUI_RETAIL_DEMAND_TMU_VALID|KUI_RETAIL_DEMAND_CYCLES_VALID))==5u);
    /* The same moduloTMU difference can hide many complete CE tick periods.
     * No field marks it exact. A valid48-bit cycle count distinguishes them. */
    a.cycles_lo=100u;a.cycles_hi=0u;b.cycles_lo=6000100u;b.cycles_hi=0u;
    t=kui_retail_demand_elapsed(&a,&b);assert(t.modulo==100u && t.cycles==6000000u);
    b.pmcr=0u;t=kui_retail_demand_elapsed(&a,&b);
    assert((t.flags&KUI_RETAIL_DEMAND_TMU_VALID) && !(t.flags&KUI_RETAIL_DEMAND_CYCLES_VALID));
    b.pmcr=a.pmcr;b.tcor=1001u;t=kui_retail_demand_elapsed(&a,&b);
    assert(!(t.flags&KUI_RETAIL_DEMAND_TMU_VALID) && (t.flags&KUI_RETAIL_DEMAND_CYCLES_VALID));
    b.pmcr=0xe023u;t=kui_retail_demand_elapsed(&a,&b);
    assert(!(t.flags&KUI_RETAIL_DEMAND_CYCLES_VALID)); /* clear/reset in progress */
    b=a;t=kui_retail_demand_elapsed(&a,&b);assert(!(t.flags&KUI_RETAIL_DEMAND_CYCLES_VALID));
    b.cycles_lo=99u;t=kui_retail_demand_elapsed(&a,&b);
    assert(!(t.flags&KUI_RETAIL_DEMAND_CYCLES_VALID)); /* reset */
    b=a;b.cycles_hi=2u;t=kui_retail_demand_elapsed(&a,&b);
    assert(t.cycles==UINT32_MAX && (t.flags&KUI_RETAIL_DEMAND_CYCLES_CLIPPED));
    b=a;b.frqcr^=1u;t=kui_retail_demand_elapsed(&a,&b);assert(t.flags==0u);
    b=a;b.ticks=950u;b.cycles_lo+=50u;t=kui_retail_demand_elapsed(&a,&b);
    assert(t.modulo==951u && (t.flags&KUI_RETAIL_DEMAND_TMU_WRAP));
}
static void terminal_contract(void) {
    reset();fixture.engine.stats.irq_blocks=7u;fixture.engine.stats.call_blocks=9u;
    fixture.engine.pio_calls=3u;fixture.engine.pio_bytes=8192u;
    fixture.engine.queue_highwater=2016u;fixture.engine.queue_blocked=8u;
    fixture.shared.stream.stats.crc_errors=2u;
    fixture.shared.stream.stats.incomplete[2]=23u;
    struct kui_retail_async_region before=fixture;
    const struct kui_retail_demand_page *p=page(1u);
    assert(p->rows[0][0]==7u && p->rows[0][1]==9u && p->rows[0][3]==3u);
    assert(p->rows[2][0]==2016u && p->rows[4][0]==2u && p->rows[5][3]==23u);
    p=page(4u);for(unsigned i=0;i<5u;i++) assert(p->rows[0][i]==0u);
    assert(p->rows[1][4]==33u); /* inactive CDDA remains explicit */
    assert(clock_calls==0u && !memcmp(&before,&fixture,sizeof(fixture)));
    assert(kui_armada_ce_demand_event(KUI_RETAIL_DEMAND_GD_BEGIN,0u,0u)==NULL);
    assert(clock_calls==0u);
    p=page(0u);assert(p->rows[0][0]==1u && p->rows[0][1]==KUI_RETAIL_DEMAND_DIAGNOSTIC);
    assert(p->rows[0][2]==0xce000002u);
    attached=false;
    struct {unsigned char bytes[sizeof(demand)];} saved;
    memcpy(saved.bytes,&demand,sizeof(demand));
    assert(page(0u)==NULL && clock_calls==0u && !memcmp(saved.bytes,&demand,sizeof(demand)));
    attached=true;
}
static void hot_contract(void) {
    reset();
    event(KUI_RETAIL_DEMAND_GD_BEGIN,KUI_GD_REQUEST,0x40000000u);
    fixture.shared.service.command=KUI_GD_PIOREAD;fixture.shared.service.lba=45000u;
    fixture.shared.service.count=2u;fixture.shared.service.request_bytes=4096u;
    fixture.shared.service.token=1u;fixture.shared.service.pending=1u;
    fixture.engine.active=1u;advance(1000u);
    event(KUI_RETAIL_DEMAND_GD_END,KUI_GD_REQUEST,1u);
    event(KUI_RETAIL_DEMAND_GD_BEGIN,KUI_GD_EXEC,0x40000060u);
    fixture.engine.stats.call_blocks+=4u;fixture.shared.service.completed_bytes=2048u;
    fixture.shared.stream.stats.polled=2u;fixture.shared.stream.stats.token_bytes=10u;
    advance(4000u);event(KUI_RETAIL_DEMAND_GD_END,KUI_GD_EXEC,0u);
    event(KUI_RETAIL_DEMAND_GD_BEGIN,KUI_GD_PIO_TRANSFER,0x40000000u);
    fixture.engine.pio_calls=1u;fixture.engine.pio_bytes=4096u;
    fixture.engine.piece_bytes=3072u; /* partial failure must not shrink requestedsize */
    fixture.shared.service.completed_bytes=4096u;fixture.shared.service.pending=0u;
    advance(2000u);event(KUI_RETAIL_DEMAND_GD_END,KUI_GD_PIO_TRANSFER,0u);
    event(KUI_RETAIL_DEMAND_IRQ_BEGIN,0u,0u);advance(500u);
    event(KUI_RETAIL_DEMAND_IRQ_END,0u,0u);
    const struct kui_retail_demand_page *p=page(0u);
#if KUI_RETAIL_DEMAND_DIAGNOSTIC == 2
    assert(clock_calls==8u && p->rows[0][3]==3u && p->rows[0][4]==1u);
    assert(p->rows[1][0]==1u && p->rows[1][2]==1u && p->rows[3][2]==1u);
    assert(p->rows[4][1]==1u && p->rows[4][4]==1u);
    assert(p->rows[6][0]==4000u && p->rows[6][1]==500u && p->rows[6][2]==0u);
    p=page(1u);assert(p->rows[1][0]==4096u && p->rows[1][1]==4096u);
    p=page(2u);assert(p->rows[0][0]==1u && p->rows[0][1]==1u && p->rows[0][2]==0u);
    assert(p->rows[0][3]==7000u);
    assert(p->rows[1][0]==KUI_GD_EXEC && p->rows[1][4]==0x40000060u);
    assert(p->rows[2][0]==2048u && p->rows[2][1]==4u && p->rows[2][2]==2u);
    unsigned before=clock_calls;p=page(3u);assert(p->rows[0][0]==2u && clock_calls==before);
    assert(p->rows[0][1]==3u && p->rows[0][2]==1u && p->rows[0][3]==1000u && p->rows[0][4]==500u);
    assert(p->rows[1][0]==(KUI_GD_EXEC|(KUI_GD_PIOREAD<<8)) && p->rows[1][2]==2048u);
    p=page(4u);assert(p->rows[6][0]==7000u && p->rows[6][1]==0u && p->rows[6][2]==500u && p->rows[6][4]==4u);
    /* Unknown performance-counter state keeps counters, rejects timing. */
    event(KUI_RETAIL_DEMAND_GD_BEGIN,KUI_GD_CHECK,0x40000000u);
    clock_now.pmcr=0u;advance(100u);event(KUI_RETAIL_DEMAND_GD_END,KUI_GD_CHECK,1u);
    p=page(0u);assert(p->rows[0][3]==4u && p->rows[6][2]==1u);
    event(KUI_RETAIL_DEMAND_GD_END,KUI_GD_CHECK,1u);p=page(2u);assert(p->rows[6][4]==1u);
#else
    assert(clock_calls==0u && p->rows[0][3]==0u && p->rows[0][4]==0u);
    p=page(1u);assert(p->rows[0][1]==4u && p->rows[0][3]==1u && p->rows[0][4]==4096u);
    p=page(2u);assert(p->rows[0][0]==0u && p->rows[0][1]==0u);
#endif
}
int main(void) {
    clock_contract();terminal_contract();hot_contract();
    puts("CE demand diagnostic: clock, terminal-only, engagement, bytes and completion contracts OK");
    return 0;
}
