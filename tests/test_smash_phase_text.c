/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_phase_text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Clock reads are not used by this pure format test. */
uint16_t kui_smash_phase_clock_test_read16(uint32_t a) {(void)a;return 0;}
uint32_t kui_smash_phase_clock_test_read32(uint32_t a) {(void)a;return 0;}
uint32_t kui_smash_phase_clock_test_sr(void) {return 0;}
static struct kui_smash_phase_trace trace;
static char output[KUI_SMASH_PHASE_TEXT_BYTES+1u];
static const struct kui_vmu_fs_stats fs={.reads=UINT32_MAX,.chain_steps=360448u,.fat_entries=UINT32_MAX,.directory_sectors=UINT32_MAX};
static const uint32_t io[6]={UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX};
static void fresh(void) {
    kui_smash_phase_trace_init(&trace);
    for(unsigned phase=1u;phase<=28u;phase++)if(phase<=5u||phase>=10u) {
        struct kui_smash_phase_entry *e=&trace.entries[trace.count++];
        e->phase=phase;e->stamp.flags=KUI_SMASH_PHASE_CYCLES|KUI_SMASH_PHASE_NOMINAL_MS|KUI_SMASH_PHASE_RTC;
        e->stamp.pmcr=0xc023u;e->stamp.frqcr=0xe0au;
        e->stamp.cycles_lo=phase*20000000u;e->stamp.rtc_seconds=1000u+phase/10u;
    }
    assert(trace.count==24u&&kui_smash_phase_trace_valid(&trace));
    memset(output,'?',sizeof(output));output[sizeof(output)-1u]=0;
}
static void rejected_session(const struct kui_smash_phase_session_stats *session) {
    fresh();
    assert(!kui_smash_phase_text_format_session(output,&trace,"0123456789ab","Shining Force",&fs,io,session));
    for(unsigned i=0u;i<KUI_SMASH_PHASE_TEXT_BYTES;i++)assert(output[i]=='?');
    assert(output[KUI_SMASH_PHASE_TEXT_BYTES]==0);
}
static void metadata_cases(void) {
    struct kui_smash_phase_session_stats session={.receipt_valid=1u,.native_gin_route=1u,
        .witness_sectors=15u,.witness_runs=2u,.metadata_io_cycles=4000000u,
        .metadata_io_flags=3u,.metadata_io_frqcr=0xe0au,.metadata_dma=7u,.metadata_fallback=9u};
    fresh();assert(kui_smash_phase_text_format_session(output,&trace,"0123456789ab","Shining Force",&fs,io,&session));
    assert(strstr(output,"Witness sectors=15 runs=2\n"));
    assert(strstr(output,"allocation_epoch=0\n"));
    assert(strstr(output,"Witness/admission: nominal_ms=100 rtc_seconds=0 flags=0x00000007\n"));
    assert(!strstr(output,"Allocation epoch validation:"));
    assert(strstr(output,"META12-13 total_ms=100 sd_ms=20 cpu_other_ms=80 ioF=0x00000003\n"));
    assert(strstr(output,"SDcycles=0x00000000003d0900 dma_rx=7 polled=9\n"));
    session.metadata_io_cycles=0u;
    fresh();assert(kui_smash_phase_text_format_session(output,&trace,"0123456789ab","Shining Force",&fs,io,&session));
    assert(strstr(output,"META12-13 total_ms=100 sd_ms=0 cpu_other_ms=100 ioF=0x00000003\n"));
    assert(strstr(output,"SDcycles=0x0000000000000000 dma_rx=7 polled=9\n"));
    session.metadata_io_flags=0u;
    fresh();assert(kui_smash_phase_text_format_session(output,&trace,"0123456789ab","Shining Force",&fs,io,&session));
    assert(strstr(output,"META12-13 total_ms=100 sd_ms=NA cpu_other_ms=NA ioF=0x00000000\n"));
    assert(strstr(output,"SDcycles=NA dma_rx=7 polled=9\n"));
    session.metadata_io_cycles=4000000u;session.metadata_io_flags=3u;session.metadata_io_frqcr=0xe09u;
    fresh();assert(kui_smash_phase_text_format_session(output,&trace,"0123456789ab","Shining Force",&fs,io,&session));
    assert(strstr(output,"META12-13 total_ms=100 sd_ms=NA cpu_other_ms=NA ioF=0x00000003\n"));
    assert(strstr(output,"SDcycles=NA dma_rx=7 polled=9\n"));
    session.metadata_io_frqcr=0xe0au;
    const uint64_t excessive[]={UINT64_C(20000001),UINT64_MAX};
    for(unsigned i=0u;i<sizeof(excessive)/sizeof(excessive[0]);i++) {
        session.metadata_io_cycles=excessive[i];
        fresh();assert(kui_smash_phase_text_format_session(output,&trace,"0123456789ab","Shining Force",&fs,io,&session));
        assert(strstr(output,"META12-13 total_ms=100 sd_ms=NA cpu_other_ms=NA ioF=0x00000003\n"));
        assert(strstr(output,"SDcycles=NA dma_rx=7 polled=9\n"));
    }
    session.metadata_io_cycles=4000000u;session.metadata_io_flags=UINT32_MAX;
    fresh();assert(kui_smash_phase_text_format_session(output,&trace,"0123456789ab","Shining Force",&fs,io,&session));
    assert(strstr(output,"META12-13 total_ms=100 sd_ms=NA cpu_other_ms=NA ioF=0xffffffff\n"));
    assert(strstr(output,"SDcycles=NA dma_rx=7 polled=9\n"));
    session=(struct kui_smash_phase_session_stats){.attaches=1u,.receipt_generation=7u,
        .receipt_sequence=9u,.receipt_valid=1u,.native_gin_route=1u,.allocation_epoch=1u};
    fresh();assert(kui_smash_phase_text_format_session(output,&trace,"0123456789ab","Shining Force",&fs,io,&session));
    assert(strstr(output,"full_admissions=0\n") && strstr(output,"attaches=1\n"));
    assert(strstr(output,"allocation_epoch=1\n"));
    assert(strstr(output,"Allocation epoch validation: nominal_ms=100 rtc_seconds=0 flags=0x00000007\n"));
    assert(strstr(output,"Witness sectors=0 runs=0\n"));
    assert(strstr(output,"META12-13 total_ms=100 sd_ms=NA cpu_other_ms=NA ioF=0x00000000\n"));
    assert(strstr(output,"SDcycles=NA dma_rx=0 polled=0\n"));
}
int main(void) {
    fresh();char longest[81];memset(longest,'S',80u);longest[80]=0;
    assert(kui_smash_phase_text_format(output,&trace,"0123456789ab",longest,&fs,io));
    assert(!memcmp(output,"KUI SMASH PHASE TRACE v1\nBUILD 0123456789ab\n",44u));
    assert(output[4095]=='\n' && !memchr(output,0,4096u));
    assert(strstr(output,"TOTAL first payload callback to installed worker: nominal_ms=2700 rtc_seconds=2"));
    assert(strstr(output,"filesystem_chain_steps=360448\n"));
    assert(strstr(output,"vmu_prep_crc_checked_sectors=4294967295\n"));
    assert(strstr(output,"Untimed: before first MDE callback; after install (SGA/BUP/options); report I/O."));
    assert(strstr(output,"matching modes cannot exclude counter reset."));
    assert(strstr(output,"cpu_other=elapsed outside SD callbacks."));
    assert(strstr(output,"Witness/admission: nominal_ms=100 rtc_seconds=0 flags=0x00000007\n"));
    assert(strstr(output,"Prefix verification: nominal_ms=500 rtc_seconds=1 flags=0x00000007\n"));
    assert(strstr(output,"SD init: nominal_ms=100 rtc_seconds=0 flags=0x00000007\n"));
    assert(!strstr(output,"Cold entry and SD init"));
    char legacy[sizeof(output)];memcpy(legacy,output,sizeof(legacy));
    assert(kui_smash_phase_text_format_session(output,&trace,"0123456789ab",longest,&fs,io,NULL));
    assert(!memcmp(legacy,output,sizeof(output)));
    struct kui_smash_phase_session_stats session={.full_admissions=UINT32_MAX,.attaches=UINT32_MAX,
        .receipt_generation=UINT32_MAX,.receipt_sequence=UINT32_MAX,.receipt_valid=1u,
        .receipt_blank=0u,.native_gin_route=1u,.boot2_calls=UINT32_MAX,
        .witness_sectors=UINT32_MAX,.witness_runs=UINT32_MAX,.allocation_epoch=1u,.metadata_io_cycles=4000000u,
        .metadata_io_flags=3u,.metadata_io_frqcr=0xe0au,.metadata_dma=UINT32_MAX,.metadata_fallback=UINT32_MAX};
    struct kui_smash_phase_session_stats original=session;
    struct kui_vmu_fs_stats all_fs=fs;all_fs.chain_steps=UINT32_MAX;
    fresh();
    assert(kui_smash_phase_text_format_session(output,&trace,"0123456789ab",longest,&all_fs,io,&session));
    assert(!memcmp(&session,&original,sizeof(session)));
    assert(!memcmp(output,"KUI SMASH PHASE TRACE v1\nBUILD 0123456789ab\n",44u));
    assert(output[4095]=='\n' && !memchr(output,0,4096u) && output[4096]==0);
    assert(strstr(output,"\nSession counters THIS_OWNER:\n"));
    assert(strstr(output,"full_admissions=4294967295\n"));
    assert(strstr(output,"attaches=4294967295\n"));
    assert(strstr(output,"receipt_generation=4294967295\n"));
    assert(strstr(output,"receipt_sequence=4294967295\n"));
    assert(strstr(output,"receipt_valid=1\n"));
    assert(strstr(output,"receipt_blank=0\n"));
    assert(strstr(output,"native_gin_route=1\n"));
    assert(strstr(output,"boot2_calls=4294967295\n"));
    assert(strstr(output,"allocation_epoch=1\n"));
    assert(strstr(output,"Allocation epoch validation: nominal_ms=100 rtc_seconds=0 flags=0x00000007\n"));
    assert(!strstr(output,"Witness/admission:"));
    assert(strstr(output,"Witness sectors=4294967295 runs=4294967295\n"));
    assert(strstr(output,"META12-13 total_ms=100 sd_ms=20 cpu_other_ms=80 ioF=0x00000003\n"));
    assert(strstr(output,"SDcycles=0x00000000003d0900 dma_rx=4294967295 polled=4294967295\n"));
    fresh();
    for(unsigned i=0u;i<trace.count;i++)trace.entries[i].stamp.rtc_seconds=2000u-i;
    assert(kui_smash_phase_text_format_session(output,&trace,"0123456789ab",longest,&all_fs,io,&session));
    assert(strstr(output,"nominal_ms=NA rtc_seconds=4294967295"));
    assert(strstr(output,"META12-13 total_ms=NA sd_ms=NA cpu_other_ms=NA ioF=0x00000003\n"));
    assert(output[4095]=='\n' && !memchr(output,0,4096u) && output[4096]==0);
    /* Keep the independent metadata interval valid at the one-hour bound,
     * while other RTC endpoints retain their longest modulo32-bit deltas. */
    fresh();
    for(unsigned i=0u;i<trace.count;i++)trace.entries[i].stamp.rtc_seconds=2000u-i;
    struct kui_smash_phase_stamp *metadata_first=NULL,*metadata_last=NULL;
    for(unsigned i=0u;i<trace.count;i++) {
        if(trace.entries[i].phase==12u)metadata_first=&trace.entries[i].stamp;
        if(trace.entries[i].phase==13u)metadata_last=&trace.entries[i].stamp;
    }
    assert(metadata_first && metadata_last);
    uint64_t peak_cycles=((uint64_t)metadata_first->cycles_hi<<32|metadata_first->cycles_lo)+UINT64_C(720000000000);
    metadata_last->cycles_hi=(uint32_t)(peak_cycles>>32);metadata_last->cycles_lo=(uint32_t)peak_cycles;
    metadata_last->rtc_seconds=metadata_first->rtc_seconds+3600u;
    struct kui_smash_phase_session_stats peak_session=session;peak_session.metadata_io_cycles=UINT64_C(520000000000);
    assert(kui_smash_phase_text_format_session(output,&trace,"0123456789ab",longest,&all_fs,io,&peak_session));
    assert(strstr(output,"META12-13 total_ms=3600000 sd_ms=2600000 cpu_other_ms=1000000 ioF=0x00000003\n"));
    assert(strstr(output,"nominal_ms=NA rtc_seconds=4294967295"));
    assert(output[4095]=='\n' && !memchr(output,0,4096u) && output[4096]==0);
    session=(struct kui_smash_phase_session_stats){.receipt_blank=1u};fresh();
    assert(kui_smash_phase_text_format_session(output,&trace,"0123456789ab","Shining Force",&fs,io,&session));
    assert(strstr(output,"receipt_valid=0\n") && strstr(output,"receipt_blank=1\n") &&
        strstr(output,"native_gin_route=0\n"));
    session=(struct kui_smash_phase_session_stats){0};fresh();
    assert(kui_smash_phase_text_format_session(output,&trace,"0123456789ab","Phantasy Star II",&fs,io,&session));
    session.receipt_valid=2u;rejected_session(&session);
    session=(struct kui_smash_phase_session_stats){.receipt_blank=2u};rejected_session(&session);
    session=(struct kui_smash_phase_session_stats){.native_gin_route=2u};rejected_session(&session);
    session=(struct kui_smash_phase_session_stats){.allocation_epoch=2u};rejected_session(&session);
    session=(struct kui_smash_phase_session_stats){.receipt_valid=1u,.receipt_blank=1u};rejected_session(&session);
    fresh();for(unsigned i=0;i<trace.count;i++)trace.entries[i].stamp.flags=KUI_SMASH_PHASE_BAD_CPU_CONFIG|KUI_SMASH_PHASE_BUSY_RTC;
    assert(kui_smash_phase_text_format(output,&trace,"0123456789ab","Shining Force",&fs,io));
    assert(strstr(output,"nominal_ms=NA rtc_seconds=NA")&&!strstr(output,"nominal_ms=0"));
    fresh();trace.entries[23].stamp.pmcr=0;
    assert(kui_smash_phase_text_format(output,&trace,"0123456789ab","Phantasy Star II",&fs,io));
    assert(strstr(output,"TOTAL first payload callback to installed worker: nominal_ms=NA"));
    fresh();trace.entries[23].stamp.rtc_seconds+=100u;
    assert(kui_smash_phase_text_format(output,&trace,"0123456789ab","Shining Force",&fs,io));
    assert(strstr(output,"TOTAL first payload callback to installed worker: nominal_ms=NA rtc_seconds=102"));
    fresh();trace.count--;
    assert(!kui_smash_phase_text_format(output,&trace,"0123456789ab","Shining Force",&fs,io)&&output[0]=='?');
    fresh();assert(!kui_smash_phase_text_format(output,&trace,"0123456789AG","Shining Force",&fs,io)&&output[0]=='?');
    fresh();assert(!kui_smash_phase_text_format(output,&trace,"0123456789ab","bad\nname",&fs,io)&&output[0]=='?');
    fresh();trace.overflow=1u;assert(!kui_smash_phase_text_format(output,&trace,"0123456789ab","Shining Force",&fs,io));
    metadata_cases();
    puts("PASS 4096-byte phase TXT:24 endpoints, max counters/name/session metrics, prefix/SD-init boundaries, metadata SD/CPU split and allocation epochs, optional legacy identity, real deltas, unavailable/reset/disagreeing clocks, immutable invalid flags");
    return 0;
}
