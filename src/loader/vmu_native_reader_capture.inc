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
    if(r)r->engine.stats.execs=r->engine.stats.exec_int=0u;
}
static void reader_capture_poll(const struct kui_vmu_native_profile_sample *clock) {
    kui_retail_async_region_type *r=reader_capture_region();
    if(!r)return;
    const struct kui_retail_async *a=&r->engine;
    const struct kui_sci_stream_stats *s=&r->shared.stream.stats;
    const struct kui_retail_gd *g=&r->shared.service;
    struct kui_reader_capture_sample sample={
        .tick=clock->counter,.valid=kui_vmu_native_profile_sample_valid(clock)?1u:0u,
        .call_max=a->stats.execs,.irq_max=a->stats.exec_int,
        .irq_blocks=a->stats.irq_blocks,.call_blocks=a->stats.call_blocks,.polled=s->polled,
        .crc=s->crc_errors,.token_errors=s->token_errors,.foreign=s->foreign,
        .repaired=s->repaired,.overruns=s->overruns,.deferred=s->deferred,
        .function=g->diag.last_function,.command=g->command,.token=g->token,
        .block=a->cursor.block,.done=a->cursor.done,.pending=g->pending};
    r->engine.stats.execs=r->engine.stats.exec_int=0u;
    kui_reader_capture_add(&reader_capture,&sample);
}
#ifndef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
static void reader_capture_report(const struct retail_display_state *display) {
    retail_display_restore(display);
    retail_display_line("B READER STALL CAPTURE - RAM ONLY");
    retail_display_line("HEX VALUES - 781250 TICKS PER SECOND");
    retail_display_values("SAMPLES BAD GAP MAX CALL MAX IRQ MAX",(uint32_t[5]){
        reader_capture.samples,reader_capture.bad,reader_capture.gap_max,
        reader_capture.call_max,reader_capture.irq_max},5u);
    const struct kui_reader_capture_sample *a=&reader_capture.first,*b=&reader_capture.previous;
    retail_display_values("IRQ BLOCKS CALL BLOCKS PIO CRC TOKEN ERR",(uint32_t[5]){
        b->irq_blocks-a->irq_blocks,b->call_blocks-a->call_blocks,b->polled-a->polled,
        b->crc-a->crc,b->token_errors-a->token_errors},5u);
    retail_display_values("FOREIGN REPAIRS OVERRUN DEFERRED CLOCK BAD",(uint32_t[5]){
        b->foreign-a->foreign,b->repaired-a->repaired,b->overruns-a->overruns,
        b->deferred-a->deferred,diagnostic.clock_invalid},5u);
    retail_display_line("CALL AND IRQ ARE DELIVERY VISIT TIMES");
    retail_display_line("GAP IS BETWEEN MAPLE COMPLETION SAMPLES");
    retail_display_line("BAD CLOCK MEANS DO NOT CONVERT TO TIME");
    retail_display_line("NEXT TWO PAGES KEEP THE FOUR LONGEST GAPS");
    retail_display_pause(300u);
    for(unsigned first=0u;first<reader_capture.top_count;first+=2u) {
        retail_display_restore(display);
        retail_display_line("LONGEST CONTROLLER POLL GAPS - DESCENDING");
        retail_display_line("GAP CALL IRQ ARE TICKS - COUNTERS ARE DELTAS");
        for(unsigned i=first;i<first+2u && i<reader_capture.top_count;++i) {
            const struct kui_reader_capture_event *e=&reader_capture.top[i];
            retail_display_values("INDEX GAP CALL MAX IRQ MAX PENDING",(uint32_t[5]){
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
