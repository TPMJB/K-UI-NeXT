/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the two real GD cores; the original low core validates submissions
 * and the asynchronous high core alone services startup cooked DATA. */
#define main shared_gd_fixture_main
#define kui_toy_pilot_request startup_original_request
#include "test_toy_pilot_gd_async.c"
#undef kui_toy_pilot_request
#undef main

static bool startup_retired,refused_mailbox,quiesce_fault;
static unsigned quiesces,phase_latches,mailbox_order,quiesce_order,latch_order,retire_order,order;
bool kui_toy_pilot_sci_startup_active(void) {return !startup_retired;}
bool kui_toy_pilot_sci_quiesce(void) {
    ++quiesces;quiesce_order=++order;
    assert(!engine_live && fixture_service.command==KUI_RETAIL_GD_PLAY);
    return !quiesce_fault;
}
void kui_toy_pilot_sci_retire(void) {
    assert(phase_latches && mailbox_order>quiesce_order);
    startup_retired=true;retire_order=++order;
}
void kui_toy_loader_schedule_host_play(void) {
    ++phase_latches;latch_order=++order;
    assert(mailbox_order>quiesce_order);
}
uint32_t kui_toy_pilot_request(uint32_t command,uint32_t a,uint32_t b,uint32_t c) {
    if(command==KUI_RETAIL_GD_PLAY) {
        assert(quiesce_order && !quiesce_fault);mailbox_order=++order;
        if(refused_mailbox) {++audio_requests;return 0u;}
    }
    return startup_original_request(command,a,b,c);
}
static void startup_init(void) {
    startup_retired=refused_mailbox=quiesce_fault=false;
    quiesces=phase_latches=mailbox_order=quiesce_order=latch_order=retire_order=order=0u;
    fixture_init();
}
static uint32_t startup_play(void) {
    fixture_put(PARAM,2u);fixture_put(PARAM+4u,2u);fixture_put(PARAM+8u,0u);
    int32_t result=fixture_dispatch(KUI_RETAIL_GD_PLAY,PARAM,KUI_GD_REQUEST);
    assert(result>0);return (uint32_t)result;
}
static int startup_baseline_read(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *out) {
    (void)context;(void)lba;++physical_reads;memset(out,0x42,count*bytes);return 0;
}
static void accepted_play_restores_original_data(void) {
    startup_init();uint32_t token=fixture_request(KUI_GD_DMAREAD);produce=true;
    while(fixture_service.pending)(void)fixture_dispatch(0u,0u,KUI_GD_EXEC);
    assert(fixture_dispatch(token,STATUS,KUI_GD_CHECK)==KUI_GD_COMPLETED);
    token=startup_play();
    assert(startup_retired && phase_latches==1u && quiesces==1u);
    assert(quiesce_order<mailbox_order && mailbox_order<latch_order && latch_order<retire_order);
    assert(fixture_dispatch(0u,0u,KUI_GD_EXEC)==0);
    assert(fixture_dispatch(token,STATUS,KUI_GD_CHECK)==KUI_GD_COMPLETED);
    unsigned before_services=services,before_pumps=pumps;
    fixture_service.ops.read=startup_baseline_read;
    fixture_put(PARAM,150u);fixture_put(PARAM+4u,3u);fixture_put(PARAM+8u,DATA);
    token=(uint32_t)fixture_dispatch(KUI_GD_DMAREAD,PARAM,KUI_GD_REQUEST);
    assert(token && fixture_service.pending && !engine_live);
    assert(fixture_dispatch(0u,0u,KUI_GD_EXEC)==0 && fixture_service.completed_bytes==4096u);
    assert(fixture_dispatch(token,STATUS,KUI_GD_CHECK)==KUI_GD_COMPLETED);
    assert(physical_reads==2u && services==before_services && pumps==before_pumps);
    for(unsigned i=0;i<6144u;i++)assert(fixture_ram[DATA-BEGIN+i]==0x42u);
    assert(fixture_dispatch(0u,0u,KUI_GD_RESET)==0 && startup_retired);
    assert(!phase_latches || phase_latches==1u);
}
static void rejected_play_keeps_startup(void) {
    startup_init();
    assert(fixture_dispatch(KUI_RETAIL_GD_PLAY,BEGIN+sizeof(fixture_ram)-4u,KUI_GD_REQUEST)==0);
    assert(!quiesces && !audio_requests && !phase_latches && !startup_retired);

    startup_init();refused_mailbox=true;uint32_t token=startup_play();
    assert(!fixture_service.pending && fixture_service.error==KUI_GD_ERROR_UNAVAILABLE);
    assert(quiesces==1u && audio_requests==1u && !phase_latches && !startup_retired);
    assert(fixture_dispatch(token,STATUS,KUI_GD_CHECK)==KUI_GD_FAILED);
    fixture_put(PARAM,150u);fixture_put(PARAM+4u,3u);fixture_put(PARAM+8u,DATA);
    (void)fixture_request(KUI_GD_PIOREAD);
    assert(engine_live && pumps==1u && !physical_reads);
}
static void teardown_failure_cannot_publish_audio(void) {
    startup_init();quiesce_fault=true;uint32_t token=startup_play();
    assert(quiesces==1u && !audio_requests && !mailbox_order && !phase_latches && !startup_retired);
    assert(!fixture_service.pending && fixture_service.error==KUI_GD_ERROR_UNAVAILABLE);
    assert(fixture_dispatch(token,STATUS,KUI_GD_CHECK)==KUI_GD_FAILED);
}
static void raw_data_keeps_original_route(void) {
    startup_init();fixture_service.ops.read=startup_baseline_read;
    fixture_service.sector_bytes=2352u;
    fixture_put(PARAM,166u);fixture_put(PARAM+4u,2u);fixture_put(PARAM+8u,DATA);
    uint32_t token=(uint32_t)fixture_dispatch(KUI_GD_DMAREAD,PARAM,KUI_GD_REQUEST);
    assert(token && fixture_service.pending && !engine_live && !pumps);
    assert(!fixture_dispatch(0u,0u,KUI_GD_EXEC));
    assert(fixture_dispatch(token,STATUS,KUI_GD_CHECK)==KUI_GD_COMPLETED);
    assert(physical_reads==1u && !services && !quiesces && !phase_latches);
}
int main(void) {
    startup_init();data_progress_and_handles();
    startup_init();validation_and_cancellation();
    accepted_play_restores_original_data();rejected_play_keeps_startup();
    teardown_failure_cannot_publish_audio();raw_data_keeps_original_route();
    puts("Startup DATA GD adapter: acceptance boundary, rejected mailbox, cleanup failure, original raw/audio/post-PLAY routing and terminal handles pass");
    return 0;
}
