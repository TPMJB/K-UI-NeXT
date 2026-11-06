/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the actual terminal VMU/read-ahead renderer against a row bus.
 * The normal host worker tests keep their existing terminal interception. */
#define KUI_VMU_NATIVE_REPORT_HOST_RENDER 1
#define KUI_SPLIT_PREFETCH_TEST_MAIN held_prefetch_regression_not_run
#include "test_vmu_native_split_prefetch.c"

static struct {unsigned rows;char text[16][64];} pages[8];
static unsigned page_count,report_pauses,vmupages;
static bool vmu_counts_seen,pio_seen,frame_row_seen,fault_rows_seen;
static struct kui_native_ra_public expected_aux;
static jmp_buf report_complete;

void retail_display_restore(const struct retail_display_state *display) {
    CHECK(display && page_count<8u);++page_count;
}
void retail_display_line(const char *text) {
    CHECK(page_count && pages[page_count-1u].rows<16u && strlen(text)<64u);
    strcpy(pages[page_count-1u].text[pages[page_count-1u].rows++],text);
    if(!strcmp(text,"VMU / " KUI_VMU_NATIVE_VIRTUAL_NAME)) ++vmupages;
    if(!strcmp(text,"IMAGE/JOURNAL ONLY")) longjmp(report_complete,1);
}
void retail_display_hex(const char *label,uint32_t value) {
    char text[64];int n=snprintf(text,sizeof(text),"%s %08X",label,value);
    CHECK(n>0 && (size_t)n<sizeof(text));retail_display_line(text);
}
void retail_display_values(const char *legend,const uint32_t *values,unsigned count) {
    CHECK(values && count<=5u);
    if(!strcmp(legend,"SUB END RD PHASE SAVE")) {
        uint32_t expected[5]={100u,100u,5u,7u,6u};
        CHECK(count==5u && !memcmp(values,expected,sizeof(expected)));vmu_counts_seen=true;
    }
    if(!strcmp(legend,"PIO N EVT ONE OVER AUX")) {
        const uint32_t *pio=expected_aux.diagnostics.stage_result[0];
        uint32_t expected[5]={pio[KUI_NATIVE_RA_PIO_ATTEMPTS],pio[KUI_NATIVE_RA_AUX_MAX_CONTEXT],
            pio[KUI_NATIVE_RA_PIO_MAX_SINGLE_TICKS],
            pio[KUI_NATIVE_RA_PIO_WINDOW_OVERRUNS],expected_aux.stats.max_visit_ticks};
        CHECK(count==5u && !memcmp(values,expected,sizeof(expected)));pio_seen=true;
    }
    if(!strcmp(legend,"FRAME PR SR COPY PR SR"))frame_row_seen=true;
    if(!strcmp(legend,"AT VALUE EXPECT CHECK LEASE"))fault_rows_seen=true;
    retail_display_line(legend);
    char text[46];unsigned n=0u;
    for(unsigned i=0u;i<count;i++) {
        int written=snprintf(text+n,sizeof(text)-n,"%08X ",values[i]);
        CHECK(written==9);n+=9u;
    }
    text[n]=0;retail_display_line(text);
}
void retail_display_pause(uint32_t frames) {
    (void)frames;++report_pauses;CHECK(!"single terminal page must not pause/cycle");
}
static void report_case(bool held,bool bound,bool failure) {
    reader_bind();split_setup();
    struct kui_native_ra_exports *a=(void *)(uintptr_t)
        ((UINT32_C(0x8c700000)+KUI_NATIVE_RA_AUX_OFFSET)|UINT32_C(0x20000000));
    struct kui_native_ra_public *aux=a->context;memset(aux,0,sizeof(*aux));
    aux->stats.hits=11u;aux->stats.producer_blocks=12u;aux->stats.producer_visits=13u;
    aux->stats.heartbeat_visits=14u;aux->stats.max_visit_ticks=15u;
    for(unsigned i=0u;i<7u;i++)aux->diagnostics.stage_result[0][i]=0x100u+i;
    aux->diagnostics.stage_result[0][KUI_NATIVE_RA_AUX_MAX_CONTEXT]=UINT32_C(0x01001401);
    if(failure) {
        aux->diagnostics.failures=1u;
        aux->diagnostics.first.stage=KUI_NATIVE_RA_STAGE_FETCH;
        aux->diagnostics.first.result=KUI_SCI_STREAM_CRC;
        aux->diagnostics.last=aux->diagnostics.first;
    }
    expected_aux=*aux;if(!bound)a->magic=0u;
    state.error=held?0x4fu:0x47u;state.submitted=state.completed=100u;
    durable_card.reads=5u;durable_card.write_phases=7u;durable_card.commits=6u;
    uint8_t saved_state[sizeof(state)],saved_card[sizeof(durable_card)];
    memcpy(saved_state,&state,sizeof(state));memcpy(saved_card,&durable_card,sizeof(durable_card));
    unsigned reads=bus_reads,clocks=clock_reads,ticks=tick_visits,loans=lease_calls;
    memset(pages,0,sizeof(pages));page_count=report_pauses=vmupages=0u;
    vmu_counts_seen=pio_seen=frame_row_seen=fault_rows_seen=false;
    if(setjmp(report_complete)==0)report();
    CHECK(vmupages==1u && vmu_counts_seen && frame_row_seen && page_count==1u &&
          !report_pauses && pages[0].rows==16u);
    CHECK(!memcmp(saved_state,&state,sizeof(state)) && !memcmp(saved_card,&durable_card,sizeof(durable_card)));
    CHECK(!memcmp(aux,&expected_aux,sizeof(*aux)) && bus_reads==reads && clock_reads==clocks &&
          tick_visits==ticks && lease_calls==loans && !block_callbacks && !sd.bytes && !sd.writes);
    CHECK(!strcmp(pages[0].text[0],"VMU / " KUI_VMU_NATIVE_VIRTUAL_NAME));
    if(held && bound) {
        CHECK(pio_seen && !fault_rows_seen);
    } else {
        CHECK(!pio_seen && fault_rows_seen);
    }
}
int main(void) {
    const uintptr_t lease=UINT32_C(0x8c700000);
    map_exact(lease+KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES,4096u);
    map_exact((lease+KUI_NATIVE_RA_AUX_OFFSET)|UINT32_C(0x20000000),4096u);
    report_case(true,true,false);report_case(true,true,true);
    report_case(true,false,false);report_case(false,true,false);
    printf("Held VMU/read-ahead report: %u checks; actual VMU+PIO/max-context/overrun values, "
        "16-row limit, missing auxiliary and ordinary failures; no worker/SD/clock/stat mutation\n",checks);
    return 0;
}
