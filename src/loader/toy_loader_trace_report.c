/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_pilot.h"
#include "kui/retail_gd.h"
#include "kui/toy_loader_trace.h"
#include "kui/toy_loader_trace_report.h"
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
static uint32_t pilot_words[128];
static uint32_t trace_words[KUI_TOY_LOADER_TRACE_WORDS];
static uint32_t page;

void kui_toy_loader_trace_report_capture(const struct retail_display_state *display,
    const uint32_t *timing,const struct kui_retail_gd_diagnostics *diag) {
    stopped_display=*display;
    kui_toy_loader_trace_freeze();
    memset(pilot_words,0,sizeof(pilot_words));
    const struct kui_toy_pilot_snapshot *p=kui_toy_pilot_snapshot();
    _Static_assert(KUI_TOY_PILOT_API==8u && sizeof(*p)==448u,"retained audio report ABI");
    if(p && p->magic==KUI_TOY_PILOT_MAGIC && p->version==8u && p->bytes==448u) {
        memcpy(pilot_words,p,320u);
        memcpy(pilot_words+96u,(const uint8_t *)(const void *)p+320u,128u);
    }
    pilot_words[80]=UINT32_C(0x47444d31);
    pilot_words[81]=2u;
    memcpy(pilot_words+82u,timing,24u);
    pilot_words[88]=diag->calls;pilot_words[89]=diag->requests;
    pilot_words[90]=diag->rejected;pilot_words[91]=diag->last_error;
    memset(trace_words,0,sizeof(trace_words));
    if(kui_toy_loader_trace_word_count()==KUI_TOY_LOADER_TRACE_WORDS)
        memcpy(trace_words,kui_toy_loader_trace_words(),sizeof(trace_words));
    /* Mirrors the retained terminal's accepted RESET mailbox operation. */
    (void)kui_toy_pilot_request(KUI_TOY_PILOT_RESET,0u,0u,0u);
}
const uint32_t *kui_toy_loader_trace_report_page(unsigned trace,unsigned index) {
    if(trace) return index<KUI_TOY_LOADER_TRACE_WORDS/16u ? trace_words+index*16u : NULL;
    return index<8u ? pilot_words+index*16u : NULL;
}
void kui_toy_loader_trace_terminal(const struct retail_display_state *display,
    const uint32_t *timing,const struct kui_retail_gd_diagnostics *diag,uint32_t reserved) {
    (void)reserved;
    kui_toy_loader_trace_report_capture(display,timing,diag);
    for(;;) {
        for(unsigned kind=0u;kind<2u;kind++) {
            const uint32_t pages=kind?KUI_TOY_LOADER_TRACE_WORDS/16u:8u;
            for(page=0u;page<pages;page++) {
                const uint32_t *words=kui_toy_loader_trace_report_page(kind,page);
                retail_display_restore(&stopped_display);
                retail_display_values(kind?"TRACE PAGE":"PILOT PAGE",&page,1u);
                for(unsigned row=0u;row<4u;row++)
                    retail_display_values("WORDS",words+row*4u,4u);
                retail_display_pause(120u);
            }
        }
    }
}
