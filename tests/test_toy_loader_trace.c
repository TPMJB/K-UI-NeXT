/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual GD/adapter differential replay. Storage costs and PVR samples are
 * independently controlled fixtures, not measured console or IRQ behavior. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "kui/toy_pilot_gd.h"
#include "kui/toy_loader_trace.h"
#if KUI_TOY_PILOT_LOADER_TRACE
/* Include the production module to seed saturation corner cases without a
 * production mutation API. All algorithms under test are its real code. */
#include "../src/loader/toy_loader_trace.c"
#include "../src/loader/toy_loader_trace_report.c"
#endif

#define BEGIN UINT32_C(0x8c100000)
#define PARAM (BEGIN+0x100u)
#define STATUS (BEGIN+0x200u)
#define OUTPUT (BEGIN+0x1000u)
static uint8_t ram[384u*1024u];
static struct kui_retail_gd service;
static struct kui_toy_pilot_snapshot audio;
static uint32_t timeline,frqcr,tcor,tcr,tstr,line,period,vbi,framebuffer;
static uint32_t maps,checks,reads,mailboxes,deny,fail_read,refuse_audio,check_exec;
static uint32_t mmio_reads,model_sr=UINT32_C(0x40000021);
#if KUI_TOY_PILOT_LOADER_TRACE
static unsigned destroy_on_reset,missing_snapshot;
static struct retail_display_state *reset_display;
static uint32_t *reset_timing;
static struct kui_retail_gd_diagnostics *reset_diag;
#endif
static uint64_t protocol_digest=UINT64_C(14695981039346656037);
static unsigned protocol_calls;

extern int32_t kui_toy_pilot_gd_dispatch(struct kui_retail_gd *,uint32_t,
    uint32_t,uint32_t,uintptr_t);

uint32_t kui_toy_loader_trace_host_read(uint32_t address,unsigned bytes) {
    ++mmio_reads;
    switch(address) {
    case 0xffc00000u:assert(bytes==2u);return frqcr;
    case 0xffd80008u:assert(bytes==4u);return tcor;
    case 0xffd80010u:assert(bytes==2u);return tcr;
    case 0xffd80004u:assert(bytes==1u);return tstr;
    case 0xffd8000cu:assert(bytes==4u);return ~timeline;
    case 0xa05f810cu:assert(bytes==4u);return line;
    case 0xa05f80ccu:assert(bytes==4u);return vbi;
    case 0xa05f80d8u:assert(bytes==4u);return (period-1u)<<16;
    case 0xa05f8050u:assert(bytes==4u);return framebuffer;
    default:assert(!"Telemetry read outside the reviewed read-only MMIO set");return 0u;
    }
}
static void tick(uint32_t amount) {timeline+=amount;}
static void digest(const void *bytes,size_t count) {
    const uint8_t *input=bytes;
    for(size_t i=0;i<count;i++) {protocol_digest^=input[i];protocol_digest*=UINT64_C(1099511628211);}
}
static uint8_t *map(void *context,uint32_t address,uint32_t bytes,int access) {
    (void)context;++maps;assert(access>=0 && access<=KUI_RETAIL_MAP_VALIDATE);
    if(address<BEGIN || address-BEGIN>sizeof(ram) || bytes>sizeof(ram)-(address-BEGIN) || address==deny)
        return NULL;
    return access==KUI_RETAIL_MAP_VALIDATE?ram+sizeof(ram):ram+address-BEGIN;
}
static int check(void *context,uint32_t lba,uint32_t count,uint32_t bytes) {
    (void)context;++checks;
    return (bytes==2048u || bytes==2352u) && lba>=45000u && lba<=45300u && count<=45300u-lba?0:-1;
}
static int read_data(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *output) {
    (void)context;++reads;assert(!check(NULL,lba,count,bytes));
    assert(model_sr==UINT32_C(0x40000021)); /* No assembly SR model is claimed. */
    /* Exactly100 fixture ticks per physical512-byte block: this distinguishes
     * byte delivery from independently inserted service and CHECK delays. */
    tick(count*(bytes==2048u?4u:5u)*100u);
    if(reads==fail_read) return -1;
    for(uint32_t n=0;n<count;n++) for(uint32_t i=0;i<bytes;i++)
        ((uint8_t *)output)[n*bytes+i]=(uint8_t)((lba+n)*13u+i*3u);
    return 0;
}
uint32_t kui_toy_pilot_request(uint32_t command,uint32_t a,uint32_t b,uint32_t c) {
    ++mailboxes;
#if KUI_TOY_PILOT_LOADER_TRACE
    if(destroy_on_reset && command==KUI_TOY_PILOT_RESET) {
        assert(!a && !b && !c);++destroy_on_reset;
        memset(&audio,0xcc,sizeof(audio));memset(reset_display,0xdd,sizeof(*reset_display));
        memset(reset_timing,0xee,24u);memset(reset_diag,0xff,sizeof(*reset_diag));
        kui_toy_loader_trace_host_reset();return 1u;
    }
#endif
    if(refuse_audio && (command==KUI_RETAIL_GD_PLAY || command==KUI_RETAIL_GD_PLAY2)) return 0u;
    audio.command=command;audio.parameters[0]=a;audio.parameters[1]=b;audio.parameters[2]=c;
    return ++audio.generation;
}
const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void) {
#if KUI_TOY_PILOT_LOADER_TRACE
    if(missing_snapshot) return NULL;
#endif
    return &audio;
}
static int32_t base(uint32_t a,uint32_t b,uint32_t c,uint32_t function) {
    /* The real low wrapper can pace a DATA step before answering CHECK.
     * Force this optional scheduling opportunity, but retain its real core
     * for both execution and CHECK rather than fabricating credit/status. */
    if(check_exec && function==KUI_GD_CHECK && service.pending &&
       (service.command==KUI_GD_PIOREAD || service.command==KUI_GD_DMAREAD))
        assert(!kui_retail_gd_dispatch(&service,0u,0u,0u,KUI_GD_EXEC));
    return kui_retail_gd_dispatch(&service,a,b,c,function);
}
static int32_t call(uint32_t function,uint32_t a,uint32_t b) {
    int32_t result=kui_toy_pilot_gd_dispatch(&service,a,b,function,(uintptr_t)base);
    ++protocol_calls;digest(&result,sizeof(result));
    /* Exclude callback/context/track pointers: their linked addresses vary
     * across off/on executables. Every scalar production GD field remains. */
    size_t offset=offsetof(struct kui_retail_gd,sector_part);
    digest((const uint8_t *)&service+offset,sizeof(service)-offset);
    digest(&audio,sizeof(audio));digest(ram,sizeof(ram));
    const uint32_t side_effects[]={maps,checks,reads,mailboxes,model_sr};
    digest(side_effects,sizeof(side_effects));return result;
}
static void put(uint32_t address,uint32_t value) {memcpy(ram+address-BEGIN,&value,4u);}
static uint32_t get(uint32_t address) {uint32_t value;memcpy(&value,ram+address-BEGIN,4u);return value;}
static void init(void) {
    static const union kui_retail_slot tracks[]={
        {.track={.start_lba=0u,.end_lba=16u,.control=4u|KUI_RETAIL_TRACK_COOKED}},
        {.track={.start_lba=16u,.end_lba=32u,.control=0u}},
        {.track={.start_lba=45000u,.end_lba=45300u,.control=4u}}
    };
    const struct kui_gd_ops ops={NULL,map,check,read_data};
    assert(!kui_retail_gd_init(&service,tracks,3u,&ops,BEGIN,BEGIN+sizeof(ram)));
    audio=(struct kui_toy_pilot_snapshot){.generation=1u,.applied_generation=1u,
        .state=KUI_TOY_PILOT_STOPPED,.driver_verified=1u,.sdk_init_result=1u,.position_fad=166u};
    memset(ram,0xa5,sizeof(ram));
    timeline=0u;frqcr=0xe0au;tcor=UINT32_MAX;tcr=2u;tstr=1u;
    line=0u;period=262u;vbi=260u;framebuffer=0x200000u;
    maps=checks=reads=mailboxes=deny=fail_read=refuse_audio=mmio_reads=check_exec=0u;
#if KUI_TOY_PILOT_LOADER_TRACE
    kui_toy_loader_trace_host_reset();destroy_on_reset=missing_snapshot=0u;
#endif
}
#if KUI_TOY_PILOT_LOADER_TRACE
static const struct kui_toy_loader_trace_report *view(void) {
    assert(kui_toy_loader_trace_word_count()==KUI_TOY_LOADER_TRACE_WORDS);
    const struct kui_toy_loader_trace_report *p=(const void *)kui_toy_loader_trace_words();
    assert(p->magic==KUI_TOY_LOADER_TRACE_MAGIC && p->words==416u && p->tick_hz==781250u);
    return p;
}
#endif
static uint32_t submit_data(uint32_t count,uint32_t command) {
    put(PARAM,45150u);put(PARAM+4u,count);put(PARAM+8u,OUTPUT);put(PARAM+12u,0u);
    int32_t token=call(KUI_GD_REQUEST,command,PARAM);assert(token>0);
    assert(service.command==command && service.pending && !service.completed_bytes);
    return (uint32_t)token;
}
static void terminal_checks(uint32_t token,int32_t expected) {
    uint32_t command=service.command,credited=service.completed_bytes;
    assert(command && !service.pending);
    assert(call(KUI_GD_REQUEST,KUI_GD_NOP,0u)==0 && service.command==command);
    assert(call(KUI_GD_CHECK,token+1u,STATUS)==KUI_GD_FAILED && service.command==command);
    assert(get(STATUS)==5u && service.completed_bytes==credited);
    deny=STATUS;assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED && service.command==command);
    deny=0u;tick(27u);
    assert(call(KUI_GD_CHECK,token,STATUS)==expected && !service.command && !service.pending);
    assert(get(STATUS+8u)==credited && !get(STATUS+12u));
    assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_NOT_FOUND);
}
static void read_round(uint32_t count,uint32_t bytes,uint32_t command) {
    init();
    put(PARAM,0u);put(PARAM+4u,bytes==2352u?0x1000u:0x2000u);put(PARAM+8u,0u);put(PARAM+12u,bytes);
    assert(!call(KUI_GD_DATATYPE,PARAM,0u));
    uint32_t token=submit_data(count,command),credited=0u;
    while(service.pending) {
        tick(7u);assert(!call(KUI_GD_EXEC,0u,0u));
        assert(service.completed_bytes>credited && service.completed_bytes<=count*bytes);
        credited=service.completed_bytes;
        if(service.pending) {
            assert(call(KUI_GD_CHECK,token+1u,STATUS)==KUI_GD_FAILED && service.pending);
            assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_PROCESSING);
            assert(get(STATUS+8u)==credited && get(STATUS+12u)==4u);
        }
    }
    terminal_checks(token,KUI_GD_COMPLETED);
    for(uint32_t n=0;n<count;n++) for(uint32_t i=0;i<bytes;i++)
        assert(ram[OUTPUT-BEGIN+n*bytes+i]==(uint8_t)((45000u+n)*13u+i*3u));
    for(unsigned i=0;i<32u;i++) assert(ram[OUTPUT-BEGIN+count*bytes+i]==0xa5u);
#if KUI_TOY_PILOT_LOADER_TRACE
    const struct kui_toy_loader_trace_phase *p=&view()->phase[0];
    assert(p->accepted_data==1u && p->requested_sectors==count && p->delivered_sectors==count);
    assert(p->requested_bytes==count*bytes && p->delivered_bytes==count*bytes);
    assert(p->progress_exec==reads && !p->progress_check && !p->zero_progress_exec);
    assert(p->data_service_body.samples==reads+2u*(reads-1u)+3u);
    assert(p->data_service_body.ticks_total==count*(bytes==2048u?4u:5u)*100u);
    assert(p->data_completed==1u && p->data_acknowledged==1u && !p->data_failed);
    assert(p->complete_acknowledge.samples==1u && p->complete_acknowledge.ticks_max==27u);
    assert(p->worst_retained==1u && p->worst[0].token==token);
    assert(p->worst[0].flags==(KUI_TOY_LOADER_TRACE_FIRST_VALID|KUI_TOY_LOADER_TRACE_COMPLETE_VALID|
        KUI_TOY_LOADER_TRACE_ACK_VALID|KUI_TOY_LOADER_TRACE_SUCCESS));
    assert(!view()->active_phase && !view()->accepted_play_requests && !p->invalid_intervals);
#endif
}
static void failures_and_cancellation(void) {
    init();fail_read=2u;uint32_t token=submit_data(5u,KUI_GD_PIOREAD);
    assert(!call(KUI_GD_EXEC,0u,0u) && service.completed_bytes==4096u && service.pending);
    assert(!call(KUI_GD_EXEC,0u,0u) && service.completed_bytes==4096u && !service.pending);
    assert(service.error==KUI_GD_ERROR_IO);terminal_checks(token,KUI_GD_FAILED);
    for(unsigned i=0;i<32u;i++) assert(ram[OUTPUT-BEGIN+4096u+i]==0xa5u);
#if KUI_TOY_PILOT_LOADER_TRACE
    assert(view()->phase[0].data_failed==1u && view()->phase[0].delivered_bytes==4096u);
    assert(view()->phase[0].zero_progress_exec==1u && view()->phase[0].data_acknowledged==1u);
#endif
    init();token=submit_data(5u,KUI_GD_DMAREAD);assert(!call(KUI_GD_EXEC,0u,0u));
    assert(call(KUI_GD_ABORT,token+1u,0u)==-1 && service.pending);
    assert(!call(KUI_GD_ABORT,token,0u) && !service.pending && service.completed_bytes==4096u);
    terminal_checks(token,KUI_GD_FAILED);assert(!mailboxes);
#if KUI_TOY_PILOT_LOADER_TRACE
    assert(view()->phase[0].data_aborted==1u && view()->phase[0].data_failed==1u);
#endif
    init();token=submit_data(5u,KUI_GD_PIOREAD);assert(!call(KUI_GD_EXEC,0u,0u));
    assert(!call(KUI_GD_RESET,0u,0u) && !service.command && !service.pending);
#if KUI_TOY_PILOT_LOADER_TRACE
    assert(view()->phase[0].data_reset==1u && view()->phase[0].delivered_bytes==4096u);
#endif
    /* A new request after RESET gets an independent lifetime. */
    token=submit_data(1u,KUI_GD_PIOREAD);assert(!call(KUI_GD_EXEC,0u,0u));
    terminal_checks(token,KUI_GD_COMPLETED);
}
static void paced_check_completion(void) {
    init();uint32_t token=submit_data(5u,KUI_GD_PIOREAD);
    assert(!call(KUI_GD_EXEC,0u,0u) && service.completed_bytes==4096u);
    check_exec=1u;tick(19u);
    assert(call(KUI_GD_CHECK,token+1u,STATUS)==KUI_GD_FAILED && service.pending);
    assert(service.completed_bytes==8192u && service.command==KUI_GD_PIOREAD);
    tick(23u);assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED);
    assert(!service.command && service.completed_bytes==5u*2048u);
#if KUI_TOY_PILOT_LOADER_TRACE
    const struct kui_toy_loader_trace_phase *p=&view()->phase[0];
    assert(p->progress_exec==1u && p->progress_check==2u && p->delivered_sectors==5u);
    assert(p->data_completed==1u && p->data_acknowledged==1u);
    assert(p->complete_acknowledge.samples==1u && !p->complete_acknowledge.ticks_total);
    assert(p->worst[0].acknowledge_ticks==0u && p->data_service_body.samples==3u);
#endif
    init();token=submit_data(3u,KUI_GD_PIOREAD);assert(!call(KUI_GD_EXEC,0u,0u));
    check_exec=1u;tick(31u);
    assert(call(KUI_GD_CHECK,token+1u,STATUS)==KUI_GD_FAILED && !service.pending);
    assert(service.command==KUI_GD_PIOREAD && service.completed_bytes==3u*2048u);
#if KUI_TOY_PILOT_LOADER_TRACE
    assert(view()->phase[0].data_completed==1u && !view()->phase[0].data_acknowledged);
#endif
    tick(11u);assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED && !service.command);
#if KUI_TOY_PILOT_LOADER_TRACE
    assert(view()->phase[0].complete_acknowledge.ticks_total==11u);
    assert(view()->phase[0].progress_check==1u && view()->phase[0].worst[0].acknowledge_ticks==11u);
#endif
}
static void play_boundary_and_freeze(void) {
    init();uint32_t token=submit_data(1u,KUI_GD_PIOREAD);
    assert(call(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,PARAM)==0); /* Busy data handle. */
    assert(!call(KUI_GD_EXEC,0u,0u));terminal_checks(token,KUI_GD_COMPLETED);
    put(PARAM,2u);put(PARAM+4u,2u);put(PARAM+8u,0u);
    deny=PARAM;assert(call(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,PARAM)==0);deny=0u;
    refuse_audio=1u;int32_t bad=call(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,PARAM);
    assert(bad>0 && !service.pending && service.error==KUI_GD_ERROR_UNAVAILABLE);
#if KUI_TOY_PILOT_LOADER_TRACE
    assert(!view()->active_phase && !view()->accepted_play_requests);
#endif
    terminal_checks((uint32_t)bad,KUI_GD_FAILED);refuse_audio=0u;
    int32_t accepted=call(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,PARAM);assert(accepted>0);
    uint32_t generation=audio.generation,boundary=~timeline;
    assert(service.pending && audio.applied_generation!=generation);
#if KUI_TOY_PILOT_LOADER_TRACE
    struct kui_toy_loader_trace_phase phase0=view()->phase[0];
    assert(view()->active_phase==1u && view()->accepted_play_requests==1u);
    assert(view()->boundary_token==(uint32_t)accepted && view()->boundary_generation==generation);
    assert(view()->boundary_tick==boundary && view()->boundary_clock_valid);
#else
    (void)boundary;
#endif
    assert(!call(KUI_GD_EXEC,0u,0u));terminal_checks((uint32_t)accepted,KUI_GD_COMPLETED);
    assert(audio.applied_generation!=generation); /* Acceptance is not audible-start proof. */
    token=submit_data(1u,KUI_GD_PIOREAD);assert(!call(KUI_GD_EXEC,0u,0u));
    terminal_checks(token,KUI_GD_COMPLETED);
    put(PARAM,166u);put(PARAM+4u,174u);put(PARAM+8u,0u);
    accepted=call(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY2,PARAM);assert(accepted>0);
    assert(!call(KUI_GD_EXEC,0u,0u));terminal_checks((uint32_t)accepted,KUI_GD_COMPLETED);
#if KUI_TOY_PILOT_LOADER_TRACE
    assert(!memcmp(&phase0,&view()->phase[0],sizeof(phase0)));
    assert(view()->accepted_play_requests==2u && view()->boundary_generation==generation);
    kui_toy_loader_trace_freeze();
    struct kui_toy_loader_trace_report preserved=*view();uint32_t previous_reads=mmio_reads;
    assert(!call(KUI_GD_RESET,0u,0u));
    memset(&audio,0xcc,sizeof(audio));memset(&service.diag,0xdd,sizeof(service.diag));
    assert(!memcmp(&preserved,view(),sizeof(preserved)) && mmio_reads==previous_reads);
#else
    assert(!call(KUI_GD_RESET,0u,0u));
    memset(&audio,0xcc,sizeof(audio));memset(&service.diag,0xdd,sizeof(service.diag));
#endif
}

#if KUI_TOY_PILOT_LOADER_TRACE
static void probe(uint32_t duration) {
    kui_toy_loader_trace_begin(&service,KUI_GD_EXEC,0u,0u);tick(duration);
    kui_toy_loader_trace_end(&service,KUI_GD_EXEC,0u,0u,0);
}
static void clock_histogram_and_saturation(void) {
    init();timeline=UINT32_MAX;probe(0u);
    assert(view()->first_clock_valid && view()->first_tick==0u);
    assert(view()->phase[0].call_body.samples==1u && !view()->phase[0].invalid_intervals);
    init();timeline=UINT32_MAX-3u;probe(8u);
    assert(view()->phase[0].call_body.ticks_total==8u); /* Counter crossing zero. */
    init();static const uint32_t ceilings[]={782u,1563u,3125u,6250u,12500u,25000u,50000u};
    for(unsigned i=0;i<7u;i++) {probe(ceilings[i]);probe(ceilings[i]+1u);}
    const struct kui_toy_loader_trace_metric *m=&view()->phase[0].call_body;
    assert(m->samples==14u && m->histogram[0]==1u && m->histogram[7]==1u);
    for(unsigned i=1;i<7u;i++) assert(m->histogram[i]==2u);
    init();probe(KUI_TOY_LOADER_TRACE_MAX_INTERVAL);probe(KUI_TOY_LOADER_TRACE_MAX_INTERVAL+1u);
    assert(view()->phase[0].call_body.samples==1u && view()->phase[0].invalid_intervals==1u);
    for(unsigned profile=0;profile<4u;profile++) {
        init();
        if(profile==0u) frqcr^=1u;else if(profile==1u) tcor=0xfffffffeu;
        else if(profile==2u) tcr=3u;else tstr=0u;
        probe(13u);assert(!view()->phase[0].call_body.samples);
        assert(view()->phase[0].invalid_clock_calls==1u && view()->phase[0].invalid_intervals==1u);
    }
    init();kui_toy_loader_trace_begin(&service,KUI_GD_EXEC,0u,0u);tstr=0u;tick(9u);
    kui_toy_loader_trace_end(&service,KUI_GD_EXEC,0u,0u,0);
    assert(view()->phase[0].invalid_clock_calls==1u && !view()->phase[0].call_body.samples);
    tstr=1u;probe(9u);assert(view()->phase[0].call_body.samples==1u);
    init();(void)view();report.phase[0].calls=UINT32_MAX;
    report.phase[0].call_body.samples=UINT32_MAX;
    report.phase[0].call_body.ticks_total=UINT32_MAX-3u;
    report.phase[0].call_body.histogram[0]=UINT32_MAX;
    probe(7u);assert(view()->counters_saturated && view()->phase[0].calls==UINT32_MAX);
    assert(view()->phase[0].call_body.samples==UINT32_MAX);
    assert(view()->phase[0].call_body.ticks_total==UINT32_MAX && view()->phase[0].call_body.histogram[0]==UINT32_MAX);
    init();kui_toy_loader_trace_end(&service,KUI_GD_EXEC,0u,0u,0);
    kui_toy_loader_trace_begin(&service,KUI_GD_EXEC,0u,0u);
    kui_toy_loader_trace_begin(&service,KUI_GD_CHECK,7u,STATUS);
    kui_toy_loader_trace_end(&service,KUI_GD_CHECK,7u,STATUS,0);
    kui_toy_loader_trace_end(&service,KUI_GD_EXEC,0u,0u,0);
    assert(view()->unmatched_ends==1u && view()->nested_begins==1u);
    assert(view()->phase[0].calls==1u && view()->phase[0].call_body.samples==1u);
}
static void pvr_and_outstanding(void) {
    init();line=250u;probe(1u);line=5u;framebuffer+=4u;probe(1u);
    assert(view()->phase[0].sampled_line_wraps==1u && view()->phase[0].sampled_fb_changes==1u);
    period=263u;vbi=261u;probe(1u);assert(view()->phase[0].pvr_geometry_changes==1u);
    line=500u;probe(1u);assert(view()->phase[0].invalid_pvr_samples==1u);
    line=7u;probe(1u);assert(view()->phase[0].sampled_line_wraps==1u);
    init();uint32_t token=submit_data(5u,KUI_GD_PIOREAD);assert(!call(KUI_GD_EXEC,0u,0u));
    kui_toy_loader_trace_freeze();const struct kui_toy_loader_trace_phase *p=&view()->phase[0];
    assert(p->outstanding_token==token && p->outstanding_lba==45000u && p->outstanding_sectors==5u);
    assert(p->outstanding_destination==OUTPUT && p->outstanding_credited_bytes==4096u);
    assert(p->outstanding_clock_valid && !p->outstanding_terminal_observed);
}
static void invalid_request_epoch(void) {
    init();uint32_t token=submit_data(1u,KUI_GD_PIOREAD);
    tstr=0u;assert(!call(KUI_GD_DRIVE,STATUS+32u,0u));tstr=1u;
    tick(17u);assert(!call(KUI_GD_EXEC,0u,0u));
    assert(view()->phase[0].data_completed==1u && view()->phase[0].call_body.samples==2u);
    assert(!view()->phase[0].request_first_credit.samples && !view()->phase[0].request_complete.samples);
    assert(view()->phase[0].worst_seen==1u && !view()->phase[0].worst_valid);
    terminal_checks(token,KUI_GD_COMPLETED);
    assert(view()->phase[0].complete_acknowledge.samples==1u);
    assert(view()->phase[0].invalid_intervals>=3u && !view()->phase[0].worst_retained);
}
static uint32_t submit_at(uint32_t lba,uint32_t count) {
    put(PARAM,lba+150u);put(PARAM+4u,count);put(PARAM+8u,OUTPUT);put(PARAM+12u,0u);
    int32_t token=call(KUI_GD_REQUEST,KUI_GD_PIOREAD,PARAM);assert(token>0);return (uint32_t)token;
}
static void finish(uint32_t token) {
    while(service.pending) assert(!call(KUI_GD_EXEC,0u,0u));
    assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED && !service.command);
}
static void request_shapes_and_worst(void) {
    init();static const uint32_t counts[]={1u,2u,4u,8u,16u,32u,64u,65u};
    uint32_t lba=45000u;
    for(unsigned i=0;i<8u;i++) {finish(submit_at(lba,counts[i]));lba+=counts[i];}
    const struct kui_toy_loader_trace_phase *p=&view()->phase[0];
    assert(p->accepted_data==8u && p->sequential==7u && p->peak_request_sectors==65u);
    for(unsigned i=0;i<8u;i++) assert(p->request_size_histogram[i]==1u);
    finish(submit_at(45126u,2u));finish(submit_at(45195u,1u));finish(submit_at(45000u,1u));
    assert(p->overlapping==1u && p->forward_seek==1u && p->backward_seek==1u);
    assert(!call(KUI_GD_RESET,0u,0u));finish(submit_at(45000u,1u));
    assert(p->backward_seek==1u && p->overlapping==1u); /* RESET revokes history. */
    init();
    for(unsigned i=0;i<10u;i++) {
        uint32_t token=submit_at(45000u+i,1u);tick(i*1000u);
        assert(!call(KUI_GD_EXEC,0u,0u));terminal_checks(token,KUI_GD_COMPLETED);
    }
    p=&view()->phase[0];
    assert(p->worst_seen==10u && p->worst_valid==10u && p->worst_retained==8u && p->worst_excluded==2u);
    unsigned seen=0u;
    for(unsigned slot=0;slot<8u;slot++) {
        const struct kui_toy_loader_trace_record *r=&p->worst[slot];
        assert(r->lba>=45002u && r->lba<=45009u && r->complete_ticks==(r->lba-45000u)*1000u+400u);
        assert(r->acknowledge_ticks==27u && r->destination==OUTPUT && r->sectors==1u);
        seen|=1u<<(r->lba-45002u);
    }
    assert(seen==255u);
    init();uint32_t original=submit_at(45000u,1u);tick(1000u);
    assert(!call(KUI_GD_EXEC,0u,0u));terminal_checks(original,KUI_GD_COMPLETED);
    assert(!call(KUI_GD_RESET,0u,0u));service.token=0x7fffffffu; /* Real handle wrap policy. */
    uint32_t reused=submit_at(45001u,1u);assert(reused==original);tick(2000u);
    assert(!call(KUI_GD_EXEC,0u,0u));tick(55u);
    assert(call(KUI_GD_CHECK,reused,STATUS)==KUI_GD_COMPLETED);
    p=&view()->phase[0];assert(p->worst_retained==2u);
    assert(p->worst[0].token==p->worst[1].token && p->worst[0].lba==45000u && p->worst[1].lba==45001u);
    assert(p->worst[0].acknowledge_ticks==27u && p->worst[1].acknowledge_ticks==55u);
}
void retail_display_restore(const struct retail_display_state *state) {
    (void)state;assert(!"Host capture tests must not claim video or enter terminal loop");
}
void retail_display_values(const char *legend,const uint32_t *values,unsigned count) {
    (void)legend;(void)values;(void)count;assert(!"Host capture tests must not display");
}
void retail_display_pause(uint32_t frames) {
    (void)frames;assert(!"Host capture tests must not wait");
}
static void captured_report_lifetime(void) {
    for(unsigned unavailable=0u;unavailable<5u;unavailable++) {
        init();probe(37u);
        uint32_t worker_seed[112];
        for(unsigned i=0;i<112u;i++) worker_seed[i]=0x5a000000u+i;
        worker_seed[0]=KUI_TOY_PILOT_MAGIC;worker_seed[1]=KUI_TOY_PILOT_API;
        worker_seed[2]=sizeof(audio);memcpy(&audio,worker_seed,sizeof(audio));
        if(unavailable==1u) missing_snapshot=1u;
        if(unavailable==2u) --audio.magic;
        if(unavailable==3u) --audio.version;
        if(unavailable==4u) --audio.bytes;
        struct retail_display_state display;
        for(unsigned i=0;i<14u;i++) display.regs[i]=0x12340000u+i;
        struct retail_display_state display_expected=display;
        uint32_t timing[6]={71u,72u,73u,74u,75u,76u};
        struct kui_retail_gd_diagnostics diag={.calls=81u,.requests=82u,.rejected=83u,.last_error=84u};
        kui_toy_loader_trace_freeze();struct kui_toy_loader_trace_report expected=*view();
        reset_display=&display;reset_timing=timing;reset_diag=&diag;destroy_on_reset=1u;
        kui_toy_loader_trace_report_capture(&display,timing,&diag);
        assert(destroy_on_reset==2u && !memcmp(&stopped_display,&display_expected,sizeof(stopped_display)));
        for(unsigned page_index=0u;page_index<8u;page_index++) {
            const uint32_t *words=kui_toy_loader_trace_report_page(0u,page_index);assert(words);
            for(unsigned col=0u;col<16u;col++) {
                unsigned at=page_index*16u+col;
                if(at<80u) assert(words[col]==(unavailable?0u:worker_seed[at]));
                else if(at>=96u) assert(words[col]==(unavailable?0u:worker_seed[at-16u]));
                else if(at==80u) assert(words[col]==0x47444d31u);
                else if(at==81u) assert(words[col]==2u);
                else if(at<88u) assert(words[col]==71u+at-82u);
                else if(at<92u) assert(words[col]==81u+at-88u);
                else assert(!words[col]);
            }
        }
        for(unsigned page_index=0u;page_index<26u;page_index++)
            assert(!memcmp(kui_toy_loader_trace_report_page(1u,page_index),
                (const uint32_t *)(const void *)&expected+page_index*16u,64u));
        assert(!kui_toy_loader_trace_report_page(0u,8u));
        assert(!kui_toy_loader_trace_report_page(0u,UINT32_MAX));
        assert(!kui_toy_loader_trace_report_page(1u,26u));
        assert(!kui_toy_loader_trace_report_page(1u,UINT32_MAX));
        /* Trace and report production owners are distinct: destroy the live
         * source a second time and verify all already captured pages again. */
        memset(&report,0x99,sizeof(report));
        for(unsigned page_index=0u;page_index<26u;page_index++)
            assert(!memcmp(kui_toy_loader_trace_report_page(1u,page_index),
                (const uint32_t *)(const void *)&expected+page_index*16u,64u));
        destroy_on_reset=missing_snapshot=0u;
    }
}
#endif
int main(void) {
    read_round(129u,2048u,KUI_GD_PIOREAD);read_round(3u,2352u,KUI_GD_DMAREAD);
    failures_and_cancellation();paced_check_completion();play_boundary_and_freeze();
    printf("PROTOCOL calls=%u digest=%016llx\n",protocol_calls,(unsigned long long)protocol_digest);
#if KUI_TOY_PILOT_LOADER_TRACE
    clock_histogram_and_saturation();pvr_and_outstanding();invalid_request_epoch();
    request_shapes_and_worst();captured_report_lifetime();
    puts("Toy loader trace production: first mailbox PLAY phase, wrong-token/invalid-destination CHECK, exact data/progress, cancellation/reset, clock wrap/zero/profile rejection, inclusive histograms, saturation, nested accounting, PVR observation and frozen evidence PASS");
#endif
    return 0;
}
