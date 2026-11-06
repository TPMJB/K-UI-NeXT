/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the production GD core, CE background reader and VMU loan against
 * the existing SCI/card fixture. Peripheral timing remains modeled; callback
 * ownership and terminal-event decisions execute in production code. */
#define main baseline_reader_main
#include "test_retail_async_ce.c"
#undef main

volatile uint32_t kui_retail_hook_active,kui_retail_hook_fault;
volatile uint32_t kui_retail_ce_deferred[2];
static uint32_t actual_sr;
uint32_t kui_retail_async_vmu_test_sr(void) { return actual_sr; }
void kui_retail_async_vmu_test_set_sr(uint32_t sr) { actual_sr=sr; }
void kui_retail_ce_vmu_loan_wake(void) { take_events(); }
void kui_sci_sd_release(void) {
    CHECK((actual_sr&0xf0u)==0xf0u);
    CHECK(R.shared.port.acquired && !kui_sci_stream_busy());
    CHECK(R.shared.stream.state==0u);
    R.shared.port.acquired=false;
}
static void prepare_lifecycle(void) {
    actual_sr=0u;kui_retail_hook_active=kui_retail_hook_fault=0u;
    kui_retail_ce_deferred[0]=kui_retail_ce_deferred[1]=0u;
    setup(2000u,true);mode(2048u);
}
static int callback_clear(void) {
    prepare_lifecycle();
    struct kui_loader_sd copied={0};
    CHECK(gd(KUI_GD_PIO_CALLBACK,VBASE+0x30000u,0u)==0);
    int32_t token=request(KUI_RETAIL_GD_PIOREAD_STREAM,45010u,1u,0u);
    CHECK(token>0);
    put(PIECE,VBASE);put(PIECE+4u,2048u);
    CHECK(gd(KUI_GD_PIO_TRANSFER,(uint32_t)token,PIECE)==0);
    CHECK(!R.shared.service.pending && R.shared.service.callback_due==1u);
    CHECK(status(token)==KUI_GD_COMPLETED && !R.shared.service.command);
    CHECK(gd(KUI_GD_PIO_CALLBACK,0u,0u)==0 && !R.shared.service.pio_callback);
    for(unsigned i=0u;i<4u;i++)CHECK(gd(KUI_GD_EXEC,0u,0u)==0);
    unsigned due=R.shared.service.callback_due;
    enum kui_armada_ce_vmu_loan_result loan=kui_retail_async_vmu_loan_begin(&copied);
    CHECK(!R.shared.service.command && !R.shared.service.pending &&
          !R.engine.active && !R.engine.piece_set);
    printf("{\"case\":\"callback-clear\",\"callback_due\":%u,\"loan_result\":%u}\n",due,loan);
    if(due || loan!=KUI_ARMADA_CE_VMU_LOAN_OK)return 1;
    CHECK(kui_retail_async_vmu_loan_end(true)==KUI_ARMADA_CE_VMU_LOAN_OK);
    return 0;
}
static int dma_failure(bool piece) {
    prepare_lifecycle();
    int32_t token=request(KUI_RETAIL_GD_DMAREAD_STREAM,45010u,4u,0u);
    CHECK(token>0);
    if(piece) {
        put(PIECE,OUTPUT);put(PIECE+4u,4096u);
        CHECK(gd(KUI_GD_DMA_TRANSFER,(uint32_t)token,PIECE)==0);
        CHECK(R.shared.service.pending && R.shared.service.xfer_left>0u);
    } else CHECK(!R.shared.service.xfer_left);
    /* The initial data-ready wake was already consumed by the driver.
     * An unrecoverable card fault must now retire its waiting DMA owner. */
    ev.drive=ev.dma_end=0u;m.healthy=false;
    for(unsigned i=0u;i<1000u && R.shared.service.pending;i++) {
        elapse();(void)interrupts(8u);(void)gd(KUI_GD_EXEC,0u,0u);
    }
    CHECK(!R.shared.service.pending && R.shared.service.error==KUI_GD_ERROR_IO &&
          !R.shared.service.xfer_left);
    printf("{\"case\":\"dma-failure-%s\",\"drive_wakes\":%u,\"dma_end_wakes\":%u,\"error\":%u}\n",
        piece?"active-piece":"no-piece",ev.drive,ev.dma_end,R.shared.service.error);
    return ev.drive>0u && (piece?ev.dma_end>0u:ev.dma_end==0u)?0:1;
}
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    if(!strcmp(argv[1],"callback-clear"))return callback_clear();
    if(!strcmp(argv[1],"dma-failure-active-piece"))return dma_failure(true);
    if(!strcmp(argv[1],"dma-failure-no-piece"))return dma_failure(false);
    return 2;
}
