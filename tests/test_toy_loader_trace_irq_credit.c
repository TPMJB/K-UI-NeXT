/* SPDX-License-Identifier: GPL-3.0-only */
/* Production TRACE and async GD core against independently arriving payload
 * credits. No physical IRQ/MMIO timing is claimed by this host fixture. */
#include "kui/toy_loader_trace.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !KUI_TOY_PILOT_LOADER_TRACE || KUI_TOY_PILOT_DATA_BUDGET_PROFILE < 1 || \
    KUI_TOY_PILOT_DATA_BUDGET_PROFILE > 4 || !defined(KUI_RETAIL_GD_ASYNC)
#error IRQ credit fixture requires a new asynchronous DATA budget profile
#endif
#define BEGIN UINT32_C(0x8c100000)
#define PARAM (BEGIN+64u)
#define STATUS (BEGIN+128u)
#define OUTPUT (BEGIN+2048u)
static uint8_t ram[16384];
static struct kui_retail_gd service;
static uint32_t now,received_bytes,arrival_in_call;
static unsigned forbidden_reads,receiver_events;

uint32_t kui_toy_loader_trace_host_read(uint32_t address,unsigned width) {
    switch(address) {
    case 0xffc00000u:assert(width==2u);return 0xe0au;
    case 0xffd80008u:assert(width==4u);return UINT32_MAX;
    case 0xffd80010u:assert(width==2u);return 2u;
    case 0xffd80004u:assert(width==1u);return 1u;
    case 0xffd8000cu:assert(width==4u);return ~now;
    case 0xa05f810cu:assert(width==4u);return now%262u;
    case 0xa05f80ccu:assert(width==4u);return 260u;
    case 0xa05f80d8u:assert(width==4u);return 261u<<16;
    case 0xa05f8050u:assert(width==4u);return 0x200000u;
    default:assert(!"Unexpected TRACE MMIO access");return 0u;
    }
}
static uint8_t *map(void *context,uint32_t address,uint32_t bytes,int access) {
    (void)context;
    if(address<BEGIN || address-BEGIN>sizeof(ram) || bytes>sizeof(ram)-(address-BEGIN))return NULL;
    return access==KUI_RETAIL_MAP_VALIDATE?ram+sizeof(ram):ram+address-BEGIN;
}
static int check(void *context,uint32_t lba,uint32_t count,uint32_t width) {
    (void)context;
    return lba>=45000u && lba<=45032u && count<=45032u-lba && width==2048u?0:-1;
}
static int forbidden_read(void *context,uint32_t lba,uint32_t count,uint32_t width,void *out) {
    (void)context;(void)lba;(void)count;(void)width;(void)out;++forbidden_reads;
    assert(!"Async EXEC/CHECK performed synchronous storage I/O");return -1;
}
static void put(uint32_t address,uint32_t value) {memcpy(ram+address-BEGIN,&value,sizeof(value));}
static uint32_t get(uint32_t address) {uint32_t value;memcpy(&value,ram+address-BEGIN,sizeof(value));return value;}
static const struct kui_toy_loader_trace_phase *phase(void) {
    assert(kui_toy_loader_trace_word_count()==416u);
    const struct kui_toy_loader_trace_report *r=(const void *)kui_toy_loader_trace_words();
    assert(r->magic==KUI_TOY_LOADER_TRACE_MAGIC && r->version==KUI_TOY_LOADER_TRACE_VERSION &&
           r->phase_words==192u && !r->counters_saturated && !r->unmatched_ends && !r->nested_begins);
    return &r->phase[0];
}
/* The external receiver fills actual payload memory and calls the production
 * GD progress API, without entering TRACE. Polling cannot produce arrivals. */
static void receiver(uint32_t total_sectors) {
    assert(service.pending && service.command==KUI_GD_DMAREAD && service.sector_bytes==2048u);
    uint32_t end=total_sectors*2048u;
    assert(end>=received_bytes && end<=service.request_bytes);
    for(uint32_t i=received_bytes;i<end;i++)ram[OUTPUT-BEGIN+i]=(uint8_t)(i*7u+(i>>8));
    received_bytes=end;++receiver_events;now+=11u;
    kui_retail_gd_progress(&service,total_sectors,0u);
}
static int32_t call(uint32_t function,uint32_t a,uint32_t b) {
    kui_toy_loader_trace_begin(&service,function,a,b);
    now+=3u;
    if(arrival_in_call) {uint32_t total=arrival_in_call;arrival_in_call=0u;receiver(total);}
    int32_t result=kui_retail_gd_dispatch(&service,a,b,0u,function);
    now+=5u;kui_toy_loader_trace_end(&service,function,a,b,result);
    return result;
}
static void initialize_fixture(void) {
    static const union kui_retail_slot tracks[]={
        {.track={.start_lba=45000u,.end_lba=45032u,.control=4u}}
    };
    const struct kui_gd_ops ops={NULL,map,check,forbidden_read};
    assert(!kui_retail_gd_init(&service,tracks,1u,&ops,BEGIN,BEGIN+sizeof(ram)));
    memset(ram,0xa5,sizeof(ram));now=received_bytes=arrival_in_call=0u;
    forbidden_reads=receiver_events=0u;kui_toy_loader_trace_host_reset();
}
static uint32_t submit(uint32_t sectors) {
    put(PARAM,45150u);put(PARAM+4u,sectors);put(PARAM+8u,OUTPUT);put(PARAM+12u,0u);
    int32_t result=call(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM);
    assert(result>0 && service.pending && service.completed_bytes==0u);
    received_bytes=0u;return (uint32_t)result;
}
static void no_double_count(void) {
    initialize_fixture();uint32_t token=submit(4u);receiver(1u);
    /* Arrival precedes begin(), so the old visit-only diagnostic delta is
     * zero even though a real sector and payload are already available. */
    assert(!call(KUI_GD_EXEC,0u,0u));
    const struct kui_toy_loader_trace_phase *p=phase();
    assert(p->delivered_sectors==1u && p->delivered_bytes==2048u && p->progress_exec==1u);
    for(unsigned i=0;i<4u;i++) {
        assert(!call(KUI_GD_EXEC,0u,0u));
        assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_PROCESSING);
        assert(get(STATUS+8u)==2048u && p->delivered_sectors==1u && p->delivered_bytes==2048u);
    }
    receiver(2u);arrival_in_call=3u;
    assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_PROCESSING);
    assert(p->delivered_sectors==3u && p->delivered_bytes==6144u && p->progress_check==1u);
    assert(receiver_events==3u && !forbidden_reads);
    receiver(4u);assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED);
    assert(!service.command && p->delivered_sectors==4u && p->delivered_bytes==8192u);
    assert(p->data_completed==1u && p->data_acknowledged==1u && !p->data_failed);
    assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_NOT_FOUND);
    assert(p->delivered_sectors==4u && p->delivered_bytes==8192u);
    for(unsigned i=0;i<8192u;i++)assert(ram[OUTPUT-BEGIN+i]==(uint8_t)(i*7u+(i>>8)));
}
static void consumed_terminal_check(void) {
    for(unsigned inside=0u;inside<2u;inside++) {
        initialize_fixture();uint32_t token=submit(3u);
        if(inside)arrival_in_call=3u;else receiver(3u);
        assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED && !service.command);
        const struct kui_toy_loader_trace_phase *p=phase();
        assert(p->delivered_sectors==3u && p->delivered_bytes==6144u);
        assert(p->progress_check==1u && p->data_completed==1u && p->data_acknowledged==1u);
        assert(p->worst_retained==1u && p->worst[0].token==token);
        assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_NOT_FOUND);
        assert(p->delivered_sectors==3u && !forbidden_reads);
    }
}
static void cancellation_reset_and_reused_token(void) {
    initialize_fixture();uint32_t token=submit(4u);receiver(1u);
    assert(!call(KUI_GD_ABORT,token,0u));
    const struct kui_toy_loader_trace_phase *p=phase();
    assert(p->delivered_sectors==1u && p->delivered_bytes==2048u && p->data_aborted==1u);
    assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED);
    assert(p->data_acknowledged==1u && p->delivered_sectors==1u);

    initialize_fixture();token=submit(4u);receiver(1u);assert(!call(KUI_GD_EXEC,0u,0u));
    receiver(2u);assert(!call(KUI_GD_RESET,0u,0u));
    p=phase();assert(p->data_reset==1u && p->delivered_sectors==1u && p->delivered_bytes==2048u);
    /* RESET must neither subtract credits nor attribute reused counter
     * contents to an old handle. Reuse its numeric token deliberately. */
    service.token=token-1u;assert(submit(2u)==token);receiver(2u);
    assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED);
    assert(p->accepted_data==2u && p->delivered_sectors==3u && p->delivered_bytes==6144u);
    assert(p->data_completed==1u && p->data_acknowledged==1u && !p->ownership_lost);

    initialize_fixture();token=submit(3u);receiver(1u);
    assert(call(KUI_GD_ABORT,token+1u,0u)==-1 && service.pending);
    p=phase();assert(p->delivered_sectors==1u && !p->data_aborted);
    assert(!call(KUI_GD_ABORT,token,0u));
    assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED);
    assert(p->delivered_sectors==1u && p->data_aborted==1u && p->data_acknowledged==1u);
}
static void byte_boundary_observation(void) {
    initialize_fixture();uint32_t token=submit(3u);
    const struct kui_toy_loader_trace_phase *p=phase();
    /* Native DATA currently publishes whole sectors. TRACE nevertheless
     * observes byte credits; a producer exposing fragments must accumulate
     * crossings rather than independently round/truncate each new fragment.
     * These are explicit state observations, not a claimed native IRQ path. */
    service.completed_bytes=512u;assert(!call(KUI_GD_EXEC,0u,0u));
    assert(p->delivered_bytes==512u && p->delivered_sectors==0u);
    service.completed_bytes=1536u;assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_PROCESSING);
    assert(p->delivered_bytes==1536u && p->delivered_sectors==0u);
    service.completed_bytes=2560u;assert(!call(KUI_GD_EXEC,0u,0u));
    assert(p->delivered_bytes==2560u && p->delivered_sectors==1u);
    service.completed_bytes=4095u;assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_PROCESSING);
    assert(p->delivered_bytes==4095u && p->delivered_sectors==1u);
    service.completed_bytes=4096u;assert(!call(KUI_GD_EXEC,0u,0u));
    assert(p->delivered_bytes==4096u && p->delivered_sectors==2u);
    assert(!call(KUI_GD_ABORT,token,0u));
    assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED && p->delivered_sectors==2u);

    initialize_fixture();(void)submit(2u);
    service.completed_bytes=17u;service.sector_bytes=0u;
    assert(!call(KUI_GD_EXEC,0u,0u));
    assert(phase()->delivered_bytes==17u && phase()->delivered_sectors==0u);
    assert(!call(KUI_GD_RESET,0u,0u));assert(!forbidden_reads);
}
static void diagnostic_counter_independence(void) {
    initialize_fixture();uint32_t token=submit(2u);
    service.diag.sectors_read=UINT32_MAX;receiver(1u);
    assert(service.diag.sectors_read==0u);assert(!call(KUI_GD_EXEC,0u,0u));
    assert(phase()->delivered_sectors==1u && phase()->delivered_bytes==2048u);
    receiver(2u);assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED);
    assert(phase()->delivered_sectors==2u && phase()->delivered_bytes==4096u);
}
int main(void) {
    no_double_count();consumed_terminal_check();cancellation_reset_and_reused_token();
    byte_boundary_observation();diagnostic_counter_independence();
    printf("Budget TRACE IRQ credits profile %u: production GD completion/ACK, inter-entry arrival, byte crossings, cancellation/reset, reused handles and no duplicate credit PASS\n",KUI_TOY_PILOT_DATA_BUDGET_PROFILE);
    return 0;
}
