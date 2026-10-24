/* SPDX-License-Identifier: GPL-3.0-only */
/* Only the diagnostic Background worker uses this private capture. It shares
 * the reader's P1 alias under the existing IMASK15 Maple hook serialization.
 * No register setup, guest buffer writes or additional allocation occurs. */
#include "kui/native_reader_capture.h"
__asm__(".global __kui_native_reader_stall_capture\n"
        ".set __kui_native_reader_stall_capture,1\n");
#include "kui/vmu_native_background.h"
/* Compile the console's checked layout view without changing the worker's
 * synchronous storage implementation or its callback ownership model. */
#ifndef KUI_RETAIL_ASYNC
#define KUI_READER_CAPTURE_LAYOUT_VIEW 1
#define KUI_RETAIL_ASYNC 1
#define KUI_RETAIL_GD_ASYNC 1
#define KUI_RETAIL_TRANSPORT 1
#define KUI_SCI_SD_NO_BLOCK 1
#define KUI_GENERAL_SCI_RESUME_FIX 1
#endif
#include "retail_async.h"
#ifdef KUI_READER_CAPTURE_LAYOUT_VIEW
#undef KUI_RETAIL_ASYNC
#undef KUI_RETAIL_GD_ASYNC
#undef KUI_RETAIL_TRANSPORT
#undef KUI_SCI_SD_NO_BLOCK
#undef KUI_GENERAL_SCI_RESUME_FIX
#undef KUI_READER_CAPTURE_LAYOUT_VIEW
#endif
static struct kui_reader_capture reader_capture;
static kui_retail_async_region_type *reader_capture_region(void);
#if KUI_NATIVE_COOP_PUMP
__asm__(".global __kui_native_cooperative_reader\n.set __kui_native_cooperative_reader,1\n");
static struct {
    uint32_t calls,blocks,busy,faults;
    uint32_t max_lba,max_count;
} reader_cooperative;
static void __attribute__((noipa)) reader_cooperative_service(void) {
    /* Called only after successful, fully proved physical controller
     * completion, after publication. It neither issues VMU packets nor
     * grants the storage writer. Interrupts remain masked throughout. */
    if(config.diagnostics!=KUI_VMU_NATIVE_DIAGNOSTICS_NORMAL &&
       config.diagnostics!=KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE) return;
    kui_retail_async_region_type *r=reader_capture_region();
    if(!r || !r->engine.active || durable_card.inflight || durable_card.handling) return;
    uint32_t before=r->engine.stats.irq_blocks;
    ++reader_cooperative.calls;
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    extern uint32_t kui_native_cooperative_test_vbr(void);
    extern uint16_t kui_native_cooperative_test_iprb(void);
    extern void kui_native_cooperative_test_restore(uint32_t,uint16_t);
    uint32_t caller_vbr=kui_native_cooperative_test_vbr();
    uint16_t caller_iprb=kui_native_cooperative_test_iprb();
    extern int kui_native_cooperative_test_visit(void);
    int result=kui_native_cooperative_test_visit();
#else
    uint32_t caller_vbr;__asm__ volatile("stc vbr,%0":"=r"(caller_vbr)::"memory");
    uint16_t caller_iprb=*(volatile const uint16_t *)(uintptr_t)UINT32_C(0xffd00008);
    int (*visit)(kui_vmu_sd_transaction,void *)=(void *)(uintptr_t)config.sd_lease;
    int result=visit(NULL,(void *)(uintptr_t)KUI_RETAIL_NATIVE_PUMP_COOKIE);
#endif
    /* The original SDK handler must resume with its own vectors and SCI
     * field, as on the reader's existing releasing entries. Its pending
     * return trampoline may re-arm the reader after that handler returns.
     * A caller already using the reader vectors retains normal idle unhook. */
    if(caller_vbr!=(uint32_t)(uintptr_t)r-UINT32_C(0x100)) {
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
        kui_native_cooperative_test_restore(caller_vbr,caller_iprb);
#else
        __asm__ volatile("ldc %0,vbr"::"r"(caller_vbr):"memory");
        volatile uint16_t *iprb=(volatile uint16_t *)(uintptr_t)UINT32_C(0xffd00008);
        *iprb=(uint16_t)((*iprb&~0x00f0u)|(caller_iprb&0x00f0u));
        (void)*iprb;
#endif
    }
    if(result==KUI_VMU_SD_LEASE_BUSY)++reader_cooperative.busy;
    else if(result!=KUI_VMU_SD_LEASE_OK)++reader_cooperative.faults;
    reader_cooperative.blocks+=r->engine.stats.irq_blocks-before;
}
#endif
static kui_retail_async_region_type *reader_capture_region(void) {
    uint32_t address=(config.lease.payload&UINT32_C(0x1fffffff))|UINT32_C(0x80000000);
    address+=KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET;
    if(!kui_vmu_native_background_region_address(config.lease.payload,address)) return NULL;
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    extern void *kui_vmu_native_reader_test_region(uint32_t);
    return kui_vmu_native_reader_test_region(address);
#else
    return (void *)(uintptr_t)address;
#endif
}
static void reader_capture_start(void) {
    kui_reader_capture_reset(&reader_capture);
    kui_retail_async_region_type *r=reader_capture_region();
#if KUI_NATIVE_REQUEST_TIMING
    if(r)r->engine.stats.releases=0u;
    memset(&reader_cooperative,0,sizeof(reader_cooperative));
#else
    if(r)r->engine.stats.execs=r->engine.stats.exec_int=0u;
#endif
}
static void reader_capture_poll(const struct kui_vmu_native_profile_sample *clock) {
    kui_retail_async_region_type *r=reader_capture_region();
    if(!r)return;
    const struct kui_retail_async *a=&r->engine;
    const struct kui_sci_stream_stats *s=&r->shared.stream.stats;
    const struct kui_retail_gd *g=&r->shared.service;
    struct kui_reader_capture_sample sample={
        .tick=clock->counter,.valid=kui_vmu_native_profile_sample_valid(clock)?1u:0u,
#if KUI_NATIVE_REQUEST_TIMING
        .call_max=a->stats.releases,
        .irq_max=g->pending && a->token==g->token?a->stats.hooks-clock->counter:0u,
        .irq_blocks=a->stats.irq_blocks-reader_cooperative.blocks,
#else
        .call_max=a->stats.execs,.irq_max=a->stats.exec_int,.irq_blocks=a->stats.irq_blocks,
#endif
        .call_blocks=a->stats.call_blocks,.polled=s->polled,
        .crc=s->crc_errors,.token_errors=s->token_errors,.foreign=s->foreign,
        .repaired=s->repaired,.overruns=s->overruns,.deferred=s->deferred,
        .function=g->diag.last_function,.command=g->command,.token=g->token,
        .block=a->cursor.block,.done=a->cursor.done,.pending=g->pending};
#if KUI_NATIVE_REQUEST_TIMING
    if(sample.valid && sample.call_max<=KUI_VMU_NATIVE_PROFILE_MAX_INTERVAL &&
       sample.call_max>reader_capture.call_max) {
        reader_cooperative.max_lba=a->native_max_lba;
        reader_cooperative.max_count=a->native_max_count;
    }
    r->engine.stats.releases=0u;
#else
    r->engine.stats.execs=r->engine.stats.exec_int=0u;
#endif
    kui_reader_capture_add(&reader_capture,&sample);
#if KUI_NATIVE_REQUEST_TIMING
    /* The request maximum was cleared at the gesture, so unlike counter
     * deltas this first interval already belongs to the capture window. */
    if(reader_capture.samples==1u && sample.valid &&
       sample.call_max<=KUI_VMU_NATIVE_PROFILE_MAX_INTERVAL &&
       sample.irq_max<=KUI_VMU_NATIVE_PROFILE_MAX_INTERVAL) {
        reader_capture.call_max=sample.call_max;reader_capture.irq_max=sample.irq_max;
    }
#endif
}
#ifndef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
static void reader_capture_report(const struct retail_display_state *display) {
    retail_display_restore(display);
#if KUI_NATIVE_REQUEST_TIMING
    retail_display_line("HYBRID READER - COMPLETE REQUEST TIMING");
#else
    retail_display_line("B READER STALL CAPTURE - RAM ONLY");
#endif
    retail_display_line("HEX VALUES - 781250 TICKS PER SECOND");
    retail_display_values(
#if KUI_NATIVE_REQUEST_TIMING
        "SAMPLES BAD GAP MAX READ MAX AGE MAX",
#else
        "SAMPLES BAD GAP MAX CALL MAX IRQ MAX",
#endif
        (uint32_t[5]){
        reader_capture.samples,reader_capture.bad,reader_capture.gap_max,
        reader_capture.call_max,reader_capture.irq_max},5u);
    const struct kui_reader_capture_sample *a=&reader_capture.first,*b=&reader_capture.previous;
    retail_display_values("IRQ BLOCKS CALL BLOCKS PIO CRC TOKEN ERR",(uint32_t[5]){
        b->irq_blocks-a->irq_blocks,b->call_blocks-a->call_blocks,b->polled-a->polled,
        b->crc-a->crc,b->token_errors-a->token_errors},5u);
    retail_display_values("FOREIGN REPAIRS OVERRUN DEFERRED CLOCK BAD",(uint32_t[5]){
        b->foreign-a->foreign,b->repaired-a->repaired,b->overruns-a->overruns,
        b->deferred-a->deferred,diagnostic.clock_invalid},5u);
#if KUI_NATIVE_REQUEST_TIMING
    retail_display_values("PUMPS BLOCKS BUSY FAULTS",(uint32_t[4]){
        reader_cooperative.calls,reader_cooperative.blocks,reader_cooperative.busy,reader_cooperative.faults},4u);
    retail_display_values("LONGEST COMPLETE READ LBA SECTORS",(uint32_t[2]){
        reader_cooperative.max_lba,reader_cooperative.max_count},2u);
#else
    retail_display_line("CALL AND IRQ ARE DELIVERY VISIT TIMES");
#endif
    retail_display_line("GAP IS BETWEEN MAPLE COMPLETION SAMPLES");
    retail_display_line("BAD CLOCK MEANS DO NOT CONVERT TO TIME");
    retail_display_line("NEXT TWO PAGES KEEP THE FOUR LONGEST GAPS");
    retail_display_pause(300u);
    for(unsigned first=0u;first<reader_capture.top_count;first+=2u) {
        retail_display_restore(display);
        retail_display_line("LONGEST CONTROLLER POLL GAPS - DESCENDING");
#if KUI_NATIVE_REQUEST_TIMING
        retail_display_line("GAP READ AGE TICKS - COUNTERS ARE DELTAS");
#else
        retail_display_line("GAP CALL IRQ ARE TICKS - COUNTERS ARE DELTAS");
#endif
        for(unsigned i=first;i<first+2u && i<reader_capture.top_count;++i) {
            const struct kui_reader_capture_event *e=&reader_capture.top[i];
            retail_display_values(
#if KUI_NATIVE_REQUEST_TIMING
                "INDEX GAP READ MAX AGE MAX PENDING",
#else
                "INDEX GAP CALL MAX IRQ MAX PENDING",
#endif
                (uint32_t[5]){
                i,e->gap,e->call_max,e->irq_max,e->pending},5u);
            retail_display_values("IRQ BLOCKS CALL BLOCKS PIO CRC TOKEN ERR",(uint32_t[5]){
                e->irq_blocks,e->call_blocks,e->polled,e->crc,e->token_errors},5u);
            retail_display_values("FN CMD PREV TOKEN TOKEN BLOCK",(uint32_t[5]){
                e->function,e->command,e->previous_token,e->token,e->block},5u);
        }
        retail_display_line("PHOTO ALL THREE READER PAGES THEN POWER OFF");
        retail_display_pause(300u);
    }
}
#endif
