/* SPDX-License-Identifier: GPL-3.0-only */
/* Included by the persistent worker. All storage is inside its proved lease;
 * no guest buffer, game timer, SD log or live display is modified. */
#if KUI_NATIVE_PERF_CAPTURE
__asm__(".global __kui_native_perf_capture_worker\n"
        ".set __kui_native_perf_capture_worker,1\n");
#if KUI_NATIVE_PERF_FULL_SUBMIT
__asm__(".global __kui_native_perf_full_submit_worker\n"
        ".set __kui_native_perf_full_submit_worker,1\n");
#endif
enum native_perf_kind {
    NATIVE_PERF_SPLIT_SUBMIT,NATIVE_PERF_FULL_SUBMIT,
    NATIVE_PERF_FAST_COMPLETE,NATIVE_PERF_FULL_COMPLETE,
    NATIVE_PERF_READ_LEASE,NATIVE_PERF_WRITE_LEASE,
    NATIVE_PERF_FREE,NATIVE_PERF_EMPTY
};
static struct {
    struct kui_vmu_native_profile timing[2];
    uint32_t active,hold,polls,wrapped;
    uint32_t last_valid,last_tick,wall_ticks,gap_max,clock_bad,flags;
    uint32_t reads,commits,writes,busy,faults;
    uint32_t reader_begin[18],reader_end[18];
} perf_capture;
static void native_perf_reader_snapshot(uint32_t out[18]) {
    memset(out,0,18u*sizeof(*out));
#if KUI_NATIVE_READER_DIAGNOSTIC
    const kui_retail_async_region_type *r=reader_capture_region();
    if(!r)return;
    /* This is a cold observation of the already owned region. Do not follow
     * a game manifest or interpret Legacy vector instructions as SDK cells. */
    uint32_t map=((config.lease.payload&UINT32_C(0x1fffffff))|UINT32_C(0x80000000))+
        KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES;
    if((uintptr_t)r->engine.manifest!=map)return;
    out[0]=1u;out[1]=r->engine.stats.irq_blocks;out[2]=r->engine.stats.call_blocks;
    out[3]=r->shared.stream.stats.blocks;out[4]=r->shared.stream.stats.polled;
    out[5]=r->shared.stream.stats.crc_errors;out[6]=r->shared.stream.stats.overruns;
    out[7]=r->shared.stream.stats.starts;out[8]=r->shared.service.token;
    out[9]=r->shared.service.completed_bytes;out[10]=r->shared.service.pending;
    out[11]=r->shared.stream.state|(r->engine.active<<8)|(r->engine.hooked<<9);
    out[12]=r->vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]==KUI_NATIVE_SDK_SCI_COOKIE;
    if(out[12])out[13]=r->vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX];
#ifndef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    __asm__ volatile("stc vbr,%0":"=r"(out[14]));
    out[15]=*(volatile const uint16_t *)(uintptr_t)UINT32_C(0xffd00008);
#endif
    out[16]=state.complete_sr;out[17]=r->shared.stream.stats.foreign;
#endif
}
static void native_perf_sample(struct kui_vmu_native_profile_sample *sample) {
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    extern void kui_vmu_native_persistent_test_clock(struct kui_vmu_native_profile_sample *,uint32_t *);
    uint32_t ccr;
    kui_vmu_native_persistent_test_clock(sample,&ccr);
#else
    sample->frqcr=*(volatile const uint16_t *)(uintptr_t)UINT32_C(0xffc00000);
    sample->tcor=*(volatile const uint32_t *)(uintptr_t)UINT32_C(0xffd80008);
    sample->tcr=*(volatile const uint16_t *)(uintptr_t)UINT32_C(0xffd80010);
    sample->tstr=*(volatile const uint8_t *)(uintptr_t)UINT32_C(0xffd80004);
    sample->counter=*(volatile const uint32_t *)(uintptr_t)UINT32_C(0xffd8000c);
#endif
}
static bool native_perf_gesture(uint32_t controls) {
    /* B only among A/B/X/Y/Start. This excludes both session and old report
     * chords; triggers are the raw right/left bytes, not button bits. */
    return !(controls&2u) && (controls&UINT32_C(0x060c))==UINT32_C(0x060c) &&
        ((controls>>16)&255u)>=200u && (controls>>24)>=200u;
}
static void native_perf_begin(struct kui_vmu_native_profile_span *span) {
    if(!perf_capture.active)return;
    struct kui_vmu_native_profile_sample sample;
    native_perf_sample(&sample);kui_vmu_native_profile_begin(span,&sample);
}
static void native_perf_end(unsigned kind,const struct kui_vmu_native_profile_span *span) {
    if(!perf_capture.active)return;
    struct kui_vmu_native_profile_sample sample;
    native_perf_sample(&sample);
    kui_vmu_native_profile_end(&perf_capture.timing[kind/4u],
        (enum kui_vmu_native_profile_kind)(kind%4u),span,&sample);
}
static void native_perf_report(void) __attribute__((noreturn));
static void native_perf_report(void) {
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    kui_vmu_native_passive_test_terminal(19u,0u);
    __builtin_unreachable();
#else
    struct retail_display_state display;memcpy(display.regs,config.display,sizeof(display.regs));
    for(;;) {
        for(unsigned page=0u;page<2u;page++) {
            retail_display_restore(&display);
            retail_display_line("NATIVE PERFORMANCE " KUI_BUILD_ID);
            retail_display_line("HEX / 781250HZ / N OK BAD SUM MAX");
            const char *const labels[8]={"SPLIT SUB","FULL SUB","FAST END","FULL END",
                "READ LOAN","WRITE LOAN","FREE","EMPTY"};
            for(unsigned i=page*4u;i<page*4u+4u;i++) {
                const struct kui_vmu_native_profile_metric *m=&perf_capture.timing[i/4u].metric[i%4u];
                retail_display_values(labels[i],(uint32_t[5]){m->calls,m->valid,m->invalid,m->ticks_total,m->ticks_max},5u);
            }
            retail_display_values("VMU FULL PREFETCH POLLS",(uint32_t[4]){
                !reader_only(),KUI_NATIVE_PERF_FULL_SUBMIT,0u,perf_capture.polls},4u);
            retail_display_line("NESTED TIMES / DO NOT SUM");
            retail_display_pause(300u);
        }
        retail_display_restore(&display);
        retail_display_line("NATIVE PERFORMANCE " KUI_BUILD_ID);
        retail_display_values("WALL GAP BAD FLAGS",(uint32_t[4]){perf_capture.wall_ticks,
            perf_capture.gap_max,perf_capture.clock_bad,perf_capture.flags},4u);
        retail_display_values("READ COMMIT PHASE BUSY FAULT",(uint32_t[5]){
            durable_card.reads-perf_capture.reads,durable_card.commits-perf_capture.commits,
            durable_card.write_phases-perf_capture.writes,durable_card.sd_busy-perf_capture.busy,
            durable_card.sd_faults-perf_capture.faults},5u);
        retail_display_values("METRIC FLAGS",(uint32_t[2]){
            perf_capture.timing[0].flags,perf_capture.timing[1].flags},2u);
        retail_display_line("CLK BREAK1 SAT2 / PHOTO POWER OFF");
        retail_display_pause(300u);
        retail_display_restore(&display);
        retail_display_line("NATIVE DEMAND / WINDOW DELTAS");
        const uint32_t *a=perf_capture.reader_begin,*b=perf_capture.reader_end;
        uint32_t delta[8]={0};
        if(a[0] && b[0])for(unsigned i=1u;i<8u;i++)delta[i]=b[i]-a[i];
        retail_display_values("SNAP BEGIN END SDK",(uint32_t[3]){a[0],b[0],b[12]},3u);
        retail_display_values("IRQ CALL DMA PIO",(uint32_t[4]){
            delta[1],delta[2],delta[3],delta[4]},4u);
        retail_display_values("CRC OVR START FOREIGN",(uint32_t[4]){
            delta[5],delta[6],delta[7],a[0] && b[0]?b[17]-a[17]:0u},4u);
        retail_display_values("TOKEN BYTES PEND STATE",b+8u,4u);
        retail_display_values("OWNER VBR IPRB MAPLE SR",b+13u,4u);
        retail_display_line("SDK COOKIE / OWNER WORD IS NOT ROUTE PROOF");
        retail_display_pause(300u);
    }
#endif
}
/* Runs only after the existing complete response/publication guards, with no
 * pending Maple pair or storage loan. Every structural driver uses this path;
 * it does not depend on the older exact-title PROFILE mode. */
static void native_perf_poll(bool gesture) {
    if(!perf_capture.active) {
        perf_capture.hold=gesture?perf_capture.hold+1u:0u;
        if(perf_capture.hold<KUI_NATIVE_PERF_CAPTURE_HOLD)return;
        memset(&perf_capture,0,sizeof(perf_capture));perf_capture.active=1u;
        perf_capture.reads=durable_card.reads;perf_capture.commits=durable_card.commits;
        perf_capture.writes=durable_card.write_phases;perf_capture.busy=durable_card.sd_busy;
        perf_capture.faults=durable_card.sd_faults;
        native_perf_reader_snapshot(perf_capture.reader_begin);
        struct kui_vmu_native_profile_span empty;
        native_perf_begin(&empty);native_perf_end(NATIVE_PERF_EMPTY,&empty);
        perf_capture.last_valid=empty.valid;
        perf_capture.last_tick=empty.begin.counter;
        return;
    }
    struct kui_vmu_native_profile_sample sample;native_perf_sample(&sample);
    bool valid=kui_vmu_native_profile_sample_valid(&sample);
    if(valid && perf_capture.last_valid) {
        uint32_t gap=perf_capture.last_tick-sample.counter;
        if(gap>KUI_VMU_NATIVE_PROFILE_MAX_INTERVAL) {
            perf_capture.flags|=1u;valid=false;
        } else {
            if(gap>perf_capture.gap_max)perf_capture.gap_max=gap;
            if(gap>UINT32_MAX-perf_capture.wall_ticks) {
                perf_capture.wall_ticks=UINT32_MAX;perf_capture.flags|=2u;
            } else perf_capture.wall_ticks+=gap;
        }
    }
    if(!valid) {++perf_capture.clock_bad;perf_capture.flags|=1u;}
    perf_capture.last_valid=valid;perf_capture.last_tick=sample.counter;
    ++perf_capture.polls;
    /* Once a clock break is observed, the duration is incomplete. Retain the
     * bounded poll fallback rather than label disjoint epochs as twenty sec. */
    if((perf_capture.polls>=KUI_NATIVE_PERF_CAPTURE_POLLS ||
       (!perf_capture.flags && perf_capture.wall_ticks>=
           KUI_NATIVE_PERF_CAPTURE_SECONDS*KUI_VMU_NATIVE_PROFILE_HZ)) &&
       !state.pending && !controller_pair.pending && !overlay.pending &&
       !durable_card.handling && !durable_card.inflight && !durable_card.protocol.active) {
        native_perf_reader_snapshot(perf_capture.reader_end);
        native_perf_report();
    }
}
#endif
