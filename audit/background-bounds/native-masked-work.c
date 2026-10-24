/* Audit-only: reuse the production end-to-end stream fixture, without changing it. */
#define main existing_suite_main
#include "/workspace/scratch/45de2288a575/K-UI-recovery/tests/test_retail_async.c"
#undef main
static void audit_case(bool cooked, bool busy, uint32_t count) {
    cooked_fixture=cooked;
    setup(GAME_VBR,X,48,false);
    mode(2048);
    m.chcr=busy?1u:0u;
    unsigned before=bus_bytes;
    int32_t handle=request(45001u,count,OUTPUT);
    unsigned after=bus_bytes;
    printf("{\"cooked\":%s,\"guest_dma_busy\":%s,\"sectors\":%u,\"request_pio_bytes\":%u,\"request_output_bytes\":%u,\"request_polled_blocks\":%u,\"request_pending\":%u,\"request_active\":%u,\"dma_in_flight\":%u",cooked?"true":"false",busy?"true":"false",count,after-before,R.shared.service.completed_bytes,kui_sci_stream_stats()->polled,R.shared.service.pending,R.engine.active,kui_sci_stream_busy());
    CHECK(finish(handle,1,1)==KUI_GD_COMPLETED);
    compare(45001u,count,OUTPUT);
    printf(",\"eventual_output_verified\":true}\n");
}
static void audit_irq(void) {
    cooked_fixture=true;
    setup(GAME_VBR,X,48,false); mode(2048);
    int32_t handle=request(45001u,64u,OUTPUT);
    m.foreign_during_rx=true; elapse();
    unsigned before=bus_bytes;
    CHECK(kui_retail_async_irq()==0u);
    unsigned after=bus_bytes;
    printf("{\"case\":\"guest_dma_takeover_during_irq\",\"irq_pio_bytes\":%u,\"irq_output_blocks\":%u,\"polled_blocks\":%u,\"cursor_sectors_done\":%u,\"active_after_irq\":%u",after-before,R.engine.stats.irq_blocks,kui_sci_stream_stats()->polled,R.engine.cursor.done,R.engine.active);
    CHECK(finish(handle,1,1)==KUI_GD_COMPLETED); compare(45001u,64u,OUTPUT);
    printf(",\"eventual_output_verified\":true}\n");
}
int main(void) {
 for(unsigned cooked=0;cooked<2;cooked++)for(unsigned busy=0;busy<2;busy++) {
  audit_case(cooked,busy,1u);audit_case(cooked,busy,64u);
 }
 audit_irq();
 return 0;
}
