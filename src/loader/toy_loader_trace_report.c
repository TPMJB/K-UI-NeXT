/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_decision_observer.h"
#if KUI_TOY_DECISION_PROFILE
#include "kui/toy_pilot.h"
#include "kui/toy_loader_trace_report.h"
#include "kui/toy_report_export.h"
#include "toy_pilot_sci.h"
#include "retail_display.h"
#include <string.h>
extern uint32_t kui_toy_pilot_request(uint32_t,uint32_t,uint32_t,uint32_t);
extern const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void);
static struct retail_display_state stopped_display;
static uint32_t stopped_words[16];
#if KUI_TOY_TEXT_REPORT
static int stopped_text_result;
#endif
void kui_toy_loader_trace_report_capture(const struct retail_display_state *display,
    const uint32_t *timing,const struct kui_retail_gd_diagnostics *diag) {
    (void)timing;(void)diag;stopped_display=*display;
    kui_toy_decision_freeze(); /* History is immutable before receiver cleanup. */
    const struct kui_toy_pilot_sci_stats *s=kui_toy_pilot_sci_snapshot();
    memcpy(stopped_words,s,16u);
    stopped_words[4]=s->errors;stopped_words[5]=s->retries;
    stopped_words[6]=s->active_token;stopped_words[7]=s->card_owned;
    memcpy(stopped_words+12u,kui_toy_pilot_sci_startup_snapshot(),16u);
    const struct kui_toy_pilot_snapshot *p=kui_toy_pilot_snapshot();
    stopped_words[8]=p?p->state:UINT32_MAX;stopped_words[9]=p?p->fault:UINT32_MAX;
    stopped_words[10]=p?p->raw_bytes:0u;stopped_words[11]=p?p->updater_entries:0u;
#if KUI_TOY_TEXT_REPORT
    bool quiesced=kui_toy_pilot_sci_quiesce();
    uint32_t reset=kui_toy_pilot_request(KUI_TOY_PILOT_RESET,0u,0u,0u);
    stopped_text_result=kui_toy_report_export_save(kui_toy_decision_snapshot(),
        stopped_words,quiesced && reset!=0u);
#else
    (void)kui_toy_pilot_sci_quiesce();
    (void)kui_toy_pilot_request(KUI_TOY_PILOT_RESET,0u,0u,0u);
#endif
}
const uint32_t *kui_toy_loader_trace_report_page(unsigned kind,unsigned page) {
    (void)kind;(void)page;return NULL;
}
void kui_toy_loader_trace_terminal(const struct retail_display_state *display,
    const uint32_t *timing,const struct kui_retail_gd_diagnostics *diag,uint32_t reserved) {
    (void)reserved;kui_toy_loader_trace_report_capture(display,timing,diag);
    const struct kui_toy_decision_report *r=kui_toy_decision_snapshot();
#if KUI_TOY_TEXT_REPORT
    for(;;) {
        retail_display_restore(&stopped_display);
        retail_display_line(stopped_text_result==KUI_REPORT_FILE_OK?
            "REPORT TXT SAVED - POWER OFF":"REPORT TXT FAILED - RETAIN THIS SCREEN");
        retail_display_line(r->enabled?"KUI/REPORTS/TOYOBS.TXT":"KUI/REPORTS/TOYCLEAN.TXT");
        retail_display_hex("REPORT RESULT",(uint32_t)stopped_text_result);
        uint32_t identity[4]={1u+r->enabled,r->enabled,r->frozen,r->count};
        retail_display_values("PROFILE OBS FROZEN BINS",identity,4);
        retail_display_pause(240u);
    }
#else
    for(;;) {
        retail_display_restore(&stopped_display);
        uint32_t identity[4]={1u+r->enabled,r->enabled,r->frozen,r->count};
        retail_display_values("TOY TICKS781250 PROFILE OBS FROZEN BINS",identity,4);
        retail_display_values("FIRST VBR LAST EXPECTED NESTED",&r->first_vbr,4);
        retail_display_values("AP CALLS IRQ CALLBLK IRQBLK",stopped_words,4);
        retail_display_values("AP ERROR RETRY ACTIVE OWNED",stopped_words+4u,4);
        retail_display_values("AUDIO STATE FAULT RAWBYTES UPDATER",stopped_words+8u,4);
        retail_display_values("LAST CMD LBA COUNT SECTORBYTES",&r->last_command,4);
        uint32_t status[5]={r->freeze_tick,r->freeze_valid,r->last_sample_age,r->expired_bins,r->invalid_clocks};
        retail_display_values("STOP VALID AGE EXPIRED BADCLK",status,5);
        retail_display_values("AP ENABLED RETIRED QUIESCE FAIL",stopped_words+12u,4);
        retail_display_pause(240u);
        if(!r->enabled)continue;
        for(unsigned i=0;i<8u;++i) {
            const struct kui_toy_decision_bin *b=&r->bins[(r->head+1u+i)&7u];
            if(!b->sequence)continue;
            retail_display_restore(&stopped_display);
            const uint32_t *w=(const uint32_t *)(const void *)b;
            retail_display_values("SEQ EPOCH FIRST LAST FLAGS",w,5);
            retail_display_values("GD VALID DATA BYTES GDTICKS",w+5u,5);
            retail_display_values("GDMAX GAPN GAPTICKS GAPMAX SERVN",w+10u,5);
            retail_display_values("SVALID STICKS SMAX WAITN CREDITN",w+15u,5);
            retail_display_values("WAITT CREDITT OTHERT ZERO APCALLBLK",w+20u,5);
            retail_display_values("APIRQ ERR RETRY MASK0-6 MASK7-14",w+25u,5);
            retail_display_values("MASK15 BL OWN OTHER PRIORITY",w+30u,5);
            retail_display_values("UPDATER REQ DONE MODE MAXSECT",w+35u,5);
            retail_display_pause(240u);
        }
    }
#endif
}
#else
/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_pilot.h"
#include "kui/retail_gd.h"
#include "kui/toy_loader_trace.h"
#include "kui/toy_loader_trace_report.h"
#include "kui/fmv_test.h"
#include "kui/toy_demand_test.h"
#if KUI_FMV_TEST
#include "toy_pilot_sci.h"
#endif
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE
#include "kui/toy_loader_schedule_report.h"
#endif
#if KUI_TOY_PILOT_DATA_PROBE
#include "kui/toy_loader_data_probe.h"
#endif
#include "retail_display.h"
#include <stddef.h>
#include <string.h>
#if !KUI_TOY_PILOT_LOADER_TRACE
#error The terminal report belongs only to the isolated high trace profile
#endif
extern uint32_t kui_toy_pilot_request(uint32_t,uint32_t,uint32_t,uint32_t);
extern const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void);
/* Every mutable object is in the already admitted high P2 lease. */
static struct retail_display_state stopped_display;
static uint32_t pilot_words[KUI_TOY_DEMAND_TEST_PROFILE?12u:(KUI_FMV_TEST?112u:128u)];
#if !KUI_FMV_TEST
static uint32_t trace_words[KUI_TOY_LOADER_TRACE_WORDS];
static uint32_t page;
#endif
#if KUI_FMV_TEST
#if KUI_TOY_DEMAND_TEST_PROFILE
static struct kui_toy_demand_test_stats fmv_demand;
static uint32_t fmv_fingerprint_skips;
#else
static uint32_t fmv_rearm[4],fmv_transport[4];
#endif
#if KUI_TOY_DEMAND_TEST_PROFILE
/* Direct views of frozen words avoid duplicate snapshots and keep the
 * complete five-page report inside the established worker reservation. */
static void __attribute__((noreturn)) fmv_terminal(void) {
    const struct kui_toy_loader_trace_report *r=(const void *)kui_toy_loader_trace_words();
    const struct kui_toy_loader_data_probe_report *d=(const void *)kui_toy_loader_data_probe_words();
    const uint32_t *s=kui_toy_loader_schedule_report_words();
    for(;;) {
        for(unsigned phase=0;phase<2u;++phase) {
            const struct kui_toy_loader_trace_phase *t=&r->phase[phase];
            retail_display_restore(&stopped_display);
#if KUI_TOY_DEMAND_TEST_PROFILE == 1
            retail_display_line("TOY DEMAND 1 - AP CONTROL");
#elif KUI_TOY_DEMAND_TEST_PROFILE == 2
            retail_display_line("TOY DEMAND 2 - PENDING DMA RETURN");
#else
            retail_display_line("TOY DEMAND 3 - NO DIAGNOSTIC CRC32");
#endif
            retail_display_line(phase?"AFTER ACCEPTED PLAY":"BEFORE ACCEPTED PLAY");
            retail_display_values("DATA ACCEPT REJECT SECTORS BYTES",&t->accepted_data,4);
            retail_display_values("SENT SECTORS BYTES EXEC CHECK",&t->delivered_sectors,4);
            retail_display_values("ZERO EXEC DONE FAIL ACK",&t->zero_progress_exec,4);
            retail_display_values("CALL TICKS SAMPLES TOTAL MAX MIN",&t->call_body.samples,4);
            retail_display_values("DATA TICKS SAMPLES TOTAL MAX MIN",&t->data_service_body.samples,4);
            retail_display_values("REQUEST TICKS N TOTAL MAX MIN",&t->request_complete.samples,4);
            retail_display_values("GAP TICKS N TOTAL MAX MIN",&t->pending_service_gap.samples,4);
            retail_display_pause(360u);
        }
        retail_display_restore(&stopped_display);
        retail_display_line("TOY DEMAND - ENGAGEMENT AND TRANSPORT");
        uint32_t identity[4]={KUI_TOY_DEMAND_TEST_PROFILE,fmv_fingerprint_skips,
            r->accepted_play_requests,s[10]|r->counters_saturated|d->saturated};
        retail_display_values("PROFILE CRC32SKIPS PLAYS SAT",identity,4);
        retail_display_values("VISITS STEPS WAITPOLLS YIELDS",&fmv_demand.service_calls,4);
        retail_display_values("RETRIES CREDIT ZERO MAX STEPS",&fmv_demand.retry_steps,4);
        retail_display_values("CALLS IRQ CALL BLOCKS IRQ BLOCKS",s+16u,4);
        retail_display_values("ERROR RETRY TOKEN YIELD POLLED",s+24u,4);
        retail_display_values("FLOW OVERRUN CRC TOKEN FOREIGN",s+196u,4);
        retail_display_values("REPAIR AHEAD DEFER TOKEN BYTES",s+200u,4);
        retail_display_line("POSITIVE YIELDS PROVE PROFILE 2 ENGAGED");
        retail_display_pause(360u);

        retail_display_restore(&stopped_display);
        retail_display_line("TOY DEMAND - ORIGINAL CALLER SR");
        for(unsigned phase=0;phase<2u;++phase) {
            const uint32_t *sr=d->phase[phase].sr_buckets;
            uint32_t row[4]={0u,0u,sr[15],0u};
            for(unsigned i=0;i<7u;++i)row[0]+=sr[i];
            for(unsigned i=7u;i<15u;++i)row[1]+=sr[i];
            for(unsigned i=16u;i<32u;++i)row[3]+=sr[i];
            retail_display_values(phase?"AFTER MASK 0-6 7-14 15 BL":"BEFORE MASK 0-6 7-14 15 BL",row,4);
        }
        retail_display_values("FP TICKS TOTAL MAX CLEANUP OK FAIL",s+12u,4);
        retail_display_values("BADCLK BADTIME LINEWRAPS FBCHANGES",&r->phase[0].invalid_clock_calls,4);
        retail_display_values("FIRST TICKS N TOTAL MAX MIN",&r->phase[0].request_first_credit.samples,4);
        retail_display_values("ACK TICKS N TOTAL MAX MIN",&r->phase[0].complete_acknowledge.samples,4);
        retail_display_values("PENDING TOKEN LBA COUNT DEST",&r->phase[0].outstanding_token,4);
        retail_display_line("CALLER SR BEFORE ENTRY; ENGINE RUNS MASKED");
        retail_display_pause(360u);

        retail_display_restore(&stopped_display);
        retail_display_line("TOY DEMAND - AUDIO AND LAST DATA");
        retail_display_values("AUDIO STATE COMMAND TRACK GENERATION",pilot_words,4);
        retail_display_values("RAW CALLS BYTES ERRORS GAPS",pilot_words+4u,4);
        retail_display_values("WORK GAP MAX TICKS ACTIVE L R",pilot_words+8u,4);
        if(s[6]) {
            const uint32_t *q=s+48u+((s[7]+7u)&7u)*KUI_TOY_SCHEDULE_RECORD_WORDS;
            retail_display_values("LAST LBA COUNT DEST SECTOR BYTES",q+2u,4);
            retail_display_values("CREDIT STATUS ERROR FIRST CRC32",q+8u,4);
        }
        retail_display_line("ALL 5 PAGES; HEX; TICKS AT 781250 HZ");
        retail_display_pause(360u);
    }
}
#else
static uint32_t milliseconds(uint32_t ticks) {
    return (uint32_t)((uint64_t)ticks*1000u/KUI_TOY_LOADER_TRACE_TICK_HZ);
}
static void __attribute__((noreturn)) fmv_terminal(void) {
    const struct kui_toy_loader_trace_report *r=(const void *)kui_toy_loader_trace_words();
    const struct kui_toy_pilot_snapshot *p=(const void *)pilot_words;
    for(;;) {
        for(unsigned phase=0;phase<2u;++phase) {
            const struct kui_toy_loader_trace_phase *t=&r->phase[phase];
            uint32_t row[4];
            retail_display_restore(&stopped_display);
            retail_display_line(KUI_TOY_FMV_IDLE_REARM?"TOY FMV - IDLE BACKGROUND CANDIDATE":"TOY FMV - CONTROL");
            retail_display_line(phase?"AFTER FIRST ACCEPTED CDDA PLAY":"BEFORE FIRST ACCEPTED CDDA PLAY");
            row[0]=t->delivered_bytes;row[1]=t->accepted_data;
            row[2]=t->data_completed;row[3]=t->data_failed;
            retail_display_values("BYTES REQUESTS DONE FAIL",row,4);
            row[0]=milliseconds(t->call_body.ticks_max);row[1]=milliseconds(t->call_body.ticks_total);
            row[2]=t->call_body.samples;row[3]=t->invalid_clock_calls;
            retail_display_values("CALL MS MAX TOTAL SAMPLES BADCLK",row,4);
            row[0]=milliseconds(t->data_service_body.ticks_max);
            row[1]=milliseconds(t->data_service_body.ticks_total);
            row[2]=t->zero_progress_exec;row[3]=t->invalid_intervals;
            retail_display_values("DATA MS MAX TOTAL ZERO BADTIME",row,4);
            row[0]=milliseconds(t->request_first_credit.ticks_max);
            row[1]=milliseconds(t->request_complete.ticks_max);
            row[2]=milliseconds(t->pending_service_gap.ticks_max);
            row[3]=t->request_complete.samples;
            retail_display_values("MS FIRST COMPLETE GAP VALID",row,4);
            row[0]=t->sequential;row[1]=t->overlapping;row[2]=t->forward_seek;row[3]=t->backward_seek;
            retail_display_values("SEQUENTIAL OVERLAP FWD BACK",row,4);
            row[0]=t->outstanding_lba;row[1]=t->outstanding_sectors;
            row[2]=t->outstanding_credited_bytes;row[3]=t->outstanding_token;
            retail_display_values("PENDING LBA SECTORS BYTES TOKEN",row,4);
            retail_display_line("VALUES HEX; TIMES MILLISECONDS; 6S/PAGE");
            retail_display_pause(360u);
        }
        retail_display_restore(&stopped_display);
        retail_display_line("TOY FMV - AUDIO AND STORAGE AT STOP");
        uint32_t row[4]={r->accepted_play_requests,p->state,p->command,p->track};
        retail_display_values("PLAY COUNT STATE COMMAND TRACK",row,4);
        row[0]=p->raw_calls;row[1]=p->raw_bytes;row[2]=p->raw_errors;row[3]=p->handoff_gaps;
        retail_display_values("AUDIO READS BYTES ERRORS GAPS",row,4);
        row[0]=p->active_left;row[1]=p->active_right;row[2]=p->generation;row[3]=p->applied_generation;
        retail_display_values("ACTIVE L R GENERATION APPLIED",row,4);
        retail_display_values("REARMS REFUSED ENABLED RETIRED",fmv_rearm,4);
        retail_display_values("DATA IRQ BLOCKS CALL BLOCKS ERR RETRY",fmv_transport,4);
        retail_display_line("FIRST PLAY IS A PHASE MARKER, NOT A MOVIE MARKER");
        retail_display_line("CAPTURE ALL 3 PAGES; POWER OFF TO EXIT");
        retail_display_pause(360u);
    }
}
#endif
#endif

void kui_toy_loader_trace_report_capture(const struct retail_display_state *display,
    const uint32_t *timing,const struct kui_retail_gd_diagnostics *diag) {
    stopped_display=*display;
    kui_toy_loader_trace_freeze();
#if KUI_TOY_PILOT_DATA_PROBE && (!KUI_FMV_TEST || KUI_TOY_DEMAND_TEST_PROFILE)
    kui_toy_loader_data_probe_freeze();
#endif
#if KUI_TOY_DEMAND_TEST_PROFILE
    fmv_demand=*kui_toy_demand_test_snapshot();
    fmv_fingerprint_skips=kui_toy_demand_test_fingerprint_skips();
#endif
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE && (!KUI_FMV_TEST || KUI_TOY_DEMAND_TEST_PROFILE)
    kui_toy_loader_schedule_report_freeze();
#endif
    memset(pilot_words,0,sizeof(pilot_words));
    const struct kui_toy_pilot_snapshot *p=kui_toy_pilot_snapshot();
    _Static_assert(KUI_TOY_PILOT_API==8u && sizeof(*p)==448u,"retained audio report ABI");
    if(p && p->magic==KUI_TOY_PILOT_MAGIC && p->version==8u && p->bytes==448u) {
#if KUI_TOY_DEMAND_TEST_PROFILE
        pilot_words[0]=p->state;pilot_words[1]=p->command;
        pilot_words[2]=p->track;pilot_words[3]=p->generation;
        pilot_words[4]=p->raw_calls;pilot_words[5]=p->raw_bytes;
        pilot_words[6]=p->raw_errors;pilot_words[7]=p->handoff_gaps;
        pilot_words[8]=p->step_ticks_max;pilot_words[9]=p->service_gap_max;
        pilot_words[10]=p->active_left;pilot_words[11]=p->active_right;
#elif KUI_FMV_TEST
        memcpy(pilot_words,p,sizeof(*p));
#else
        memcpy(pilot_words,p,320u);
        memcpy(pilot_words+96u,(const uint8_t *)(const void *)p+320u,128u);
#endif
    }
#if !KUI_FMV_TEST
    pilot_words[80]=UINT32_C(0x47444d31);
    pilot_words[81]=2u;
    memcpy(pilot_words+82u,timing,24u);
    pilot_words[88]=diag->calls;pilot_words[89]=diag->requests;
    pilot_words[90]=diag->rejected;pilot_words[91]=diag->last_error;
#else
    (void)timing;(void)diag;
#endif
#if !KUI_FMV_TEST
    memset(trace_words,0,sizeof(trace_words));
    if(kui_toy_loader_trace_word_count()==KUI_TOY_LOADER_TRACE_WORDS)
        memcpy(trace_words,kui_toy_loader_trace_words(),sizeof(trace_words));
#endif
#if KUI_FMV_TEST && !KUI_TOY_DEMAND_TEST_PROFILE
    kui_toy_pilot_sci_fmv_snapshot(fmv_rearm);
    const struct kui_toy_pilot_sci_stats *s=kui_toy_pilot_sci_snapshot();
    fmv_transport[0]=s->irq_blocks;fmv_transport[1]=s->call_blocks;
    fmv_transport[2]=s->errors;fmv_transport[3]=s->retries;
#endif
    /* Mirrors the retained terminal's accepted RESET mailbox operation. */
    (void)kui_toy_pilot_request(KUI_TOY_PILOT_RESET,0u,0u,0u);
}
const uint32_t *kui_toy_loader_trace_report_page(unsigned trace,unsigned index) {
#if KUI_FMV_TEST
    (void)trace;(void)index;return NULL;
#else
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE
    if(trace==3u)return index<KUI_TOY_SCHEDULE_REPORT_WORDS/16u ?
        kui_toy_loader_schedule_report_words()+index*16u:NULL;
#endif
#if KUI_TOY_PILOT_DATA_PROBE
    if(trace==2u) return index<KUI_TOY_LOADER_DATA_PROBE_WORDS/16u ?
        kui_toy_loader_data_probe_words()+index*16u : NULL;
#endif
    if(trace) return index<KUI_TOY_LOADER_TRACE_WORDS/16u ? trace_words+index*16u : NULL;
    return index<8u ? pilot_words+index*16u : NULL;
#endif
}
void kui_toy_loader_trace_terminal(const struct retail_display_state *display,
    const uint32_t *timing,const struct kui_retail_gd_diagnostics *diag,uint32_t reserved) {
    (void)reserved;
    kui_toy_loader_trace_report_capture(display,timing,diag);
#if KUI_FMV_TEST
    fmv_terminal();
#else
    for(;;) {
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE
        for(unsigned order=0u;order<4u;order++) {
            const unsigned kinds[4]={3u,2u,0u,1u};
            unsigned kind=kinds[order];
            const uint32_t pages=kind==3u?KUI_TOY_SCHEDULE_REPORT_WORDS/16u:
                (kind==2u?KUI_TOY_LOADER_DATA_PROBE_WORDS/16u:
                (kind?KUI_TOY_LOADER_TRACE_WORDS/16u:8u));
#elif KUI_TOY_PILOT_DATA_PROBE
        for(unsigned order=0u;order<3u;order++) {
            unsigned kind=order?order-1u:2u;
            const uint32_t pages=kind==2u?KUI_TOY_LOADER_DATA_PROBE_WORDS/16u:
                (kind?KUI_TOY_LOADER_TRACE_WORDS/16u:8u);
#else
        for(unsigned kind=0u;kind<2u;kind++) {
            const uint32_t pages=kind?KUI_TOY_LOADER_TRACE_WORDS/16u:8u;
#endif
            for(page=0u;page<pages;page++) {
                const uint32_t *words=kui_toy_loader_trace_report_page(kind,page);
                retail_display_restore(&stopped_display);
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE
                retail_display_values(kind==3u?"SCHED PAGE":(kind==2u?"DATA PAGE":
                    (kind?"TRACE PAGE":"PILOT PAGE")),&page,1u);
#elif KUI_TOY_PILOT_DATA_PROBE
                retail_display_values(kind==2u?"DATA PAGE":(kind?"TRACE PAGE":"PILOT PAGE"),&page,1u);
#else
                retail_display_values(kind?"TRACE PAGE":"PILOT PAGE",&page,1u);
#endif
                for(unsigned row=0u;row<4u;row++)
                    retail_display_values("WORDS",words+row*4u,4u);
                retail_display_pause(120u);
            }
        }
    }
#endif
}

#endif /* KUI_TOY_DECISION_PROFILE */
