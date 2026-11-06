/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_phase_clock.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define PMCR UINT32_C(0xff000084)
#define FRQCR UINT32_C(0xffc00000)
#define HIGH UINT32_C(0xff100004)
#define LOW UINT32_C(0xff100008)
#define RTC_HIGH UINT32_C(0xa0710000)
#define RTC_LOW UINT32_C(0xa0710004)
#define FIFO UINT32_C(0xa05f688c)
static unsigned reads,pm_reads,hi_reads,lo_reads,rtc_reads,sr_reads;
static int fast_only,coherent_on_fourth,unstable_late;
static uint32_t pmcr,frqcr,g2_busy,fifo_busy,low_value,rtc_value,sr;
static int change_mode,change_frequency,unstable,frozen,rtc_rollover,g2_changes;
static int late_mode,late_frequency,torn_rtc;
uint32_t kui_smash_phase_clock_test_sr(void) {assert(!fast_only);++sr_reads;return sr;}

uint16_t kui_smash_phase_clock_test_read16(uint32_t address) {
    ++reads;
    if(address==PMCR) {
        ++pm_reads;
        return (uint16_t)(((change_mode && pm_reads>1u) || (late_mode && lo_reads>=2u))?0xc024u:pmcr);
    }
    assert(address==FRQCR);
    return (uint16_t)(((change_frequency && pm_reads>1u) || (late_frequency && lo_reads>=2u))?0xe09u:frqcr);
}
uint32_t kui_smash_phase_clock_test_read32(uint32_t address) {
    ++reads;
    if(fast_only)assert(address==HIGH || address==LOW);
    if(address==HIGH) {
        ++hi_reads;
        if(coherent_on_fourth) {unsigned n=(hi_reads-1u)%8u;return 0x1234u+(n<6u?n:6u);}
        return unstable || (unstable_late && hi_reads>2u)?hi_reads:0x1234u;
    }
    if(address==LOW) {++lo_reads;if(!frozen)low_value+=1000u;return low_value;}
    if(address==FIFO)return fifo_busy;
    if(address>=UINT32_C(0xa05f7800) && address<UINT32_C(0xa05f7880)) {
        uint32_t offset=(address-UINT32_C(0xa05f7800))%32u;
        assert(offset==20u || offset==24u);
        return g2_busy || (g2_changes && rtc_reads)?1u:0u;
    }
    assert(address==RTC_HIGH || address==RTC_LOW);
    ++rtc_reads;
    uint32_t current=rtc_rollover && rtc_reads>2u?UINT32_C(0x12350000):rtc_value;
    if(torn_rtc)current+=rtc_reads;
    return address==RTC_HIGH?current>>16:current&0xffffu;
}
static void fresh(void) {
    reads=pm_reads=hi_reads=lo_reads=rtc_reads=sr_reads=0u;
    fast_only=coherent_on_fourth=unstable_late=0;
    pmcr=0xc023u;frqcr=0xe0au;g2_busy=fifo_busy=0u;
    low_value=10000u;rtc_value=UINT32_C(0x1234ffff);
    sr=UINT32_C(0x500000f0);
    change_mode=change_frequency=unstable=frozen=rtc_rollover=g2_changes=0;
    late_mode=late_frequency=torn_rtc=0;
}
static struct kui_smash_phase_stamp sample(void) {
    struct kui_smash_phase_stamp s;memset(&s,0xa7,sizeof(s));
    kui_smash_phase_clock_read(&s);assert(!s.reserved);assert(reads<256u);return s;
}
static void sampling(void) {
    fresh();struct kui_smash_phase_stamp s=sample();
    assert((s.flags&7u)==7u && s.pmcr==0xc023u && (s.frqcr&0xfffu)==0xe0au);
    assert(s.cycles_hi==0x1234u && s.cycles_lo>10000u && s.rtc_seconds==rtc_value);
    assert(hi_reads>=2u && lo_reads>=2u); /* progress is observed without any ISR */
    fresh();pmcr=0u;s=sample();assert(!(s.flags&KUI_SMASH_PHASE_CYCLES));
    fresh();pmcr=0x8023u;s=sample();assert(!(s.flags&KUI_SMASH_PHASE_CYCLES));
    fresh();pmcr=0xc024u;s=sample();assert(!(s.flags&KUI_SMASH_PHASE_CYCLES));
    fresh();change_mode=1;s=sample();assert(!(s.flags&KUI_SMASH_PHASE_CYCLES));
    fresh();late_mode=1;s=sample();assert(!(s.flags&KUI_SMASH_PHASE_CYCLES));
    fresh();unstable=1;s=sample();assert(!(s.flags&KUI_SMASH_PHASE_CYCLES));
    fresh();frozen=1;s=sample();assert(!(s.flags&KUI_SMASH_PHASE_CYCLES));
    fresh();frqcr=0xe09u;s=sample();
    assert((s.flags&KUI_SMASH_PHASE_CYCLES) && !(s.flags&KUI_SMASH_PHASE_NOMINAL_MS));
    fresh();change_frequency=1;s=sample();assert(!(s.flags&KUI_SMASH_PHASE_NOMINAL_MS));
    fresh();late_frequency=1;s=sample();assert(!(s.flags&KUI_SMASH_PHASE_NOMINAL_MS));
    fresh();g2_busy=1u;s=sample();assert(!(s.flags&KUI_SMASH_PHASE_RTC) && !rtc_reads);
    fresh();sr=UINT32_C(0x40000000);s=sample();
    assert((s.flags&KUI_SMASH_PHASE_CYCLES) && !(s.flags&KUI_SMASH_PHASE_RTC) && !rtc_reads);
    for(unsigned bit=0u;bit<6u;bit++)if((1u<<bit)&0x31u) {
        fresh();fifo_busy=1u<<bit;s=sample();assert(!(s.flags&KUI_SMASH_PHASE_RTC) && !rtc_reads);
    }
    fresh();g2_changes=1;s=sample();assert(!(s.flags&KUI_SMASH_PHASE_RTC));
    fresh();rtc_rollover=1;s=sample();
    assert((s.flags&KUI_SMASH_PHASE_RTC) && s.rtc_seconds==UINT32_C(0x12350000));
    fresh();torn_rtc=1;s=sample();assert(!(s.flags&KUI_SMASH_PHASE_RTC));
}
static struct kui_smash_phase_stamp fast_sample(void) {
    struct kui_smash_phase_stamp s;memset(&s,0xa7,sizeof(s));fast_only=1;
    kui_smash_phase_clock_read_fast(&s);fast_only=0;
    assert(!s.reserved && !s.rtc_seconds && !s.g2_state && !sr_reads && !rtc_reads);
    assert(!(s.flags&(KUI_SMASH_PHASE_RTC|KUI_SMASH_PHASE_BUSY_RTC|KUI_SMASH_PHASE_TORN_RTC)));
    /* Two samples, each at most four HI/LOW/HI attempts, plus three mode pairs. */
    assert(reads<=30u && hi_reads<=16u && lo_reads<=8u && pm_reads<=3u);
    return s;
}
static void fast_sampling(void) {
    fresh();kui_smash_phase_clock_read_fast(NULL);assert(!reads && !sr_reads);
    fresh();struct kui_smash_phase_stamp full=sample();
    fresh();struct kui_smash_phase_stamp s=fast_sample();
    assert(s.flags==3u && s.pmcr==full.pmcr && s.frqcr==full.frqcr &&
        s.cycles_hi==full.cycles_hi && s.cycles_lo==full.cycles_lo);
    for(unsigned mask=0u;mask<=15u;mask++) {
        fresh();sr=UINT32_C(0x40000000)|(mask<<4);g2_busy=fifo_busy=1u;torn_rtc=1;
        s=fast_sample();assert(s.flags==3u);
    }
    fresh();pmcr=0u;s=fast_sample();assert(s.flags==KUI_SMASH_PHASE_BAD_CPU_CONFIG && !hi_reads && !lo_reads);
    fresh();pmcr=0x8023u;s=fast_sample();assert(s.flags==KUI_SMASH_PHASE_BAD_CPU_CONFIG);
    fresh();pmcr=0xc024u;s=fast_sample();assert(s.flags==KUI_SMASH_PHASE_BAD_CPU_CONFIG);
    fresh();change_mode=1;s=fast_sample();assert(s.flags==KUI_SMASH_PHASE_BAD_CPU_CONFIG);
    fresh();late_mode=1;s=fast_sample();assert(s.flags==KUI_SMASH_PHASE_BAD_CPU_CONFIG);
    fresh();change_frequency=1;s=fast_sample();assert(s.flags==KUI_SMASH_PHASE_BAD_CPU_CONFIG);
    fresh();late_frequency=1;s=fast_sample();assert(s.flags==KUI_SMASH_PHASE_BAD_CPU_CONFIG);
    fresh();frqcr=0xe09u;s=fast_sample();assert(s.flags==KUI_SMASH_PHASE_CYCLES);
    fresh();unstable=1;s=fast_sample();assert(s.flags==KUI_SMASH_PHASE_TORN_COUNTER && reads==14u);
    fresh();unstable_late=1;s=fast_sample();assert(s.flags==KUI_SMASH_PHASE_TORN_COUNTER && reads==19u);
    fresh();frozen=1;s=fast_sample();assert(s.flags==KUI_SMASH_PHASE_STOPPED_COUNTER);
    fresh();coherent_on_fourth=1;s=fast_sample();assert(s.flags==3u && reads==30u && hi_reads==16u && lo_reads==8u);
}
static void deltas(void) {
    struct kui_smash_phase_stamp a={.flags=7u,.pmcr=0xc023u,.frqcr=0xe0au,
        .cycles_hi=0x1234u,.cycles_lo=100u,.rtc_seconds=1000u};
    struct kui_smash_phase_stamp b=a;struct kui_smash_phase_delta d;
    b.cycles_lo+=UINT32_C(4000000000);b.rtc_seconds+=20u;
    kui_smash_phase_clock_delta(&a,&b,&d);
    assert((d.flags&7u)==7u && d.cycles==UINT64_C(4000000000) && d.cpu_ms==20000u && d.rtc_seconds==20u && !d.reserved);
    for(unsigned seconds=19u;seconds<=21u;seconds++) {
        b.rtc_seconds=a.rtc_seconds+seconds;kui_smash_phase_clock_delta(&a,&b,&d);
        assert((d.flags&KUI_SMASH_PHASE_NOMINAL_MS) && !(d.flags&KUI_SMASH_PHASE_CLOCK_DISAGREEMENT));
    }
    b.rtc_seconds=a.rtc_seconds;kui_smash_phase_clock_delta(&a,&b,&d);
    assert((d.flags&KUI_SMASH_PHASE_CLOCK_DISAGREEMENT) && !(d.flags&KUI_SMASH_PHASE_NOMINAL_MS));
    assert((d.flags&KUI_SMASH_PHASE_CYCLES) && d.cycles==UINT64_C(4000000000));
    /* Same PMCR/FRQCR endpoints cannot hide an implausible modulo48 reset. */
    b=a;b.cycles_hi=0u;b.cycles_lo=0u;b.rtc_seconds++;
    kui_smash_phase_clock_delta(&a,&b,&d);
    assert((d.flags&KUI_SMASH_PHASE_BAD_DELTA) && !(d.flags&(KUI_SMASH_PHASE_CYCLES|KUI_SMASH_PHASE_NOMINAL_MS)));
    /* A small plausible reset cannot be detected with no independent RTC. */
    a.flags=3u;b=a;b.cycles_lo+=200000u;kui_smash_phase_clock_delta(&a,&b,&d);
    assert((d.flags&3u)==3u && d.cpu_ms==1u);
    a.flags=7u;
    a.cycles_hi=65535u;a.cycles_lo=UINT32_MAX-99u;
    b=a;b.cycles_hi=0u;b.cycles_lo=100u;b.rtc_seconds++;
    kui_smash_phase_clock_delta(&a,&b,&d);
    assert((d.flags&KUI_SMASH_PHASE_CYCLES) && d.cycles==200u);
    a.flags=0u;kui_smash_phase_clock_delta(&a,&b,&d);
    assert(!(d.flags&7u));
    a=b;b.pmcr=0xc024u;kui_smash_phase_clock_delta(&a,&b,&d);
    assert(!(d.flags&KUI_SMASH_PHASE_CYCLES));
}
int main(void) {
    sampling();fast_sampling();deltas();
    puts("PASS Smash phase clock: read-only masked-progress and four-address fast samples, bounded split/wrap, invalid mode/frequency, RTC/G2 refusal and20s elapsed delta");
    return 0;
}
