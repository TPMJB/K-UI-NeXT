/* SPDX-License-Identifier: GPL-3.0-only */
/* Exact Evolution1, NORMAL-only terminal observation. Until the gesture is
 * complete this reads only the already validated controller button word. */
static uint32_t native_wait_capture_value(uint32_t address,unsigned width) {
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    uint32_t value=UINT32_MAX;
    extern bool kui_native_wait_capture_test_read(uint32_t,unsigned,uint32_t *);
    (void)kui_native_wait_capture_test_read(address,width,&value);return value;
#else
    return width==1u?*(volatile const uint8_t *)(uintptr_t)address:
        width==2u?*(volatile const uint16_t *)(uintptr_t)address:
        *(volatile const uint32_t *)(uintptr_t)address;
#endif
}
static void native_wait_capture_snapshot(struct native_wait_capture *c) {
    uint32_t *s=c->snapshot;
    const kui_retail_async_region_type *r=reader_capture_region();
    for(unsigned i=0u;i<10u;i++) s[i]=UINT32_MAX;
    if(r) {
        s[0]=r->shared.service.command;s[1]=r->shared.service.token;
        s[2]=r->shared.service.pending;s[3]=r->shared.service.executing;
        s[4]=r->engine.active;s[5]=r->engine.in_irq;s[6]=r->engine.hooked;s[7]=r->engine.opened;
        s[8]=r->shared.stream.state;s[9]=r->shared.stream.position;
    }
    s[10]=native_wait_capture_value(UINT32_C(0xffa0001c),4u);
    s[11]=native_wait_capture_value(UINT32_C(0xffa00040),4u);
    /* SMR/BRR/SCR/SSR only. RDR is deliberately never observed. */
    s[12]=native_wait_capture_value(UINT32_C(0xffe00000),1u) |
        native_wait_capture_value(UINT32_C(0xffe00004),1u)<<8 |
        native_wait_capture_value(UINT32_C(0xffe00008),1u)<<16 |
        native_wait_capture_value(UINT32_C(0xffe00010),1u)<<24;
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    extern uint32_t kui_native_wait_capture_test_vbr(void);
    s[13]=kui_native_wait_capture_test_vbr();
#else
    __asm__ volatile("stc vbr,%0":"=r"(s[13])::"memory");
#endif
    s[14]=native_wait_capture_value(UINT32_C(0xffd00008),2u);
    s[15]=native_wait_capture_value(UINT32_C(0xff00001c),4u);
    s[16]=native_wait_capture_value(UINT32_C(0xff000028),4u);
    /* Fixed original-code addresses only. Never follow the saved getter,
     * R4, VBR, or any other value recovered from the interrupted frame. */
    s[17]=native_wait_capture_value(UINT32_C(0x8c1f3628),4u);
    s[18]=native_wait_capture_value(UINT32_C(0x8c1f362c),4u);
    s[19]=native_wait_capture_value(UINT32_C(0x8c1f1f36),2u) |
        native_wait_capture_value(UINT32_C(0x8c1f1f38),2u)<<16;
    s[20]=native_wait_capture_value(UINT32_C(0x8c1f1f3a),2u);
    s[21]=native_wait_capture_value(UINT32_C(0x8c1f1f80),4u);
    s[22]=native_wait_capture_value(UINT32_C(0x8c1f3608),4u);
    s[23]=native_wait_capture_value(UINT32_C(0x8c1f35c4),4u);
    s[24]=native_wait_capture_value(UINT32_C(0xa05f810c),4u);
    s[25]=native_wait_capture_value(UINT32_C(0xa05f80d8),4u);
    s[26]=native_wait_capture_value(UINT32_C(0xa05f80cc),4u);
    s[27]=native_wait_capture_value(UINT32_C(0xa05f80d0),4u);
    s[28]=native_wait_capture_value(UINT32_C(0xa05f6c04),4u);
    s[29]=native_wait_capture_value(UINT32_C(0xa05f6c18),4u);
    s[30]=native_wait_capture_value(UINT32_C(0xa05f6c14),4u);
    s[31]=native_wait_capture_value(UINT32_C(0xa05f6c10),4u);
}
#ifndef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
static void native_wait_capture_cpu(const char *first,const char *second,const struct native_wait_record *r) {
    retail_display_values(first,(uint32_t[5]){r->flags,r->pc,r->r0,r->r4,r->r12},5u);
    retail_display_values(second,(uint32_t[5]){r->r13,r->r14,r->ssr,r->vbr,r->isr_sr},5u);
}
#endif
static void native_wait_capture_report(const struct native_wait_capture *c) __attribute__((noreturn));
static void native_wait_capture_report(const struct native_wait_capture *c) {
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    kui_vmu_native_passive_test_terminal(18u,c->ready);__builtin_unreachable();
#else
    const struct native_wait_record empty={0};
    unsigned selected=c->first_bad!=UINT32_MAX?c->first_bad:
        c->first_wait!=UINT32_MAX?c->first_wait:c->valid?c->valid-1u:0u;
    const struct native_wait_record *chosen=c->valid?&c->record[selected]:&empty;
    const struct native_wait_record *latest=c->valid?&c->record[c->valid-1u]:&empty;
    struct retail_display_state display;memcpy(display.regs,config.display,sizeof(display.regs));
    for(;;) {
        retail_display_restore(&display);
        retail_display_line("EVO1 WAIT CPU 1/2");
        retail_display_values("TRY OK UNWIND READ READY",(uint32_t[5]){
            c->attempts,c->valid,c->unwind_fail,c->read_fail,c->ready},5u);
        retail_display_values("WAIT GOOD BAD BANK T-BAD",(uint32_t[5]){
            c->waits,c->operand_match,c->operand_bad,c->bank_bad,c->t_bad},5u);
        native_wait_capture_cpu("SEL FLAGS PC R0 R4 R12","SEL R13 R14 SSR VBR ISR",chosen);
        native_wait_capture_cpu("LAST FLAGS PC R0 R4 R12","LAST R13 R14 SSR VBR ISR",latest);
        retail_display_values("VID0 VID1 SCAN0 SCAN1 SEL PR",(uint32_t[5]){
            chosen->video_counter,latest->video_counter,chosen->scan_begin,latest->scan_begin,chosen->pr},5u);
        retail_display_line("PHOTO BOTH PAGES / POWER OFF");
        retail_display_pause(300u);
        retail_display_restore(&display);
        retail_display_line("EVO1 READER STATE 2/2");
        static const char *const label[7]={"GD CMD TOKEN PENDING EXEC ACTIVE",
            "IRQ HOOK OPEN STREAM POS","CHCR1 DMAOR SCI VBR IPRB",
            "CCR EVENT W28 W2C GETTER01","GETTER2 BASE RLIT MASK STAT",
            "LOAD VBL CTRL MDSTAR MDST","MDEN MDTSEL"};
        for(unsigned row=0u;row<7u;row++)
            retail_display_values(label[row],c->snapshot+row*5u,row==6u?2u:5u);
        retail_display_line("PHOTO BOTH PAGES / POWER OFF");
        retail_display_pause(300u);
    }
#endif
}
static void __attribute__((noipa)) native_wait_capture_observe(const struct kui_vmu_native_passive_frame *f) {
    if(config.diagnostics!=KUI_VMU_NATIVE_DIAGNOSTICS_NORMAL)return;
    if(!diagnostic.active) {
        diagnostic.gesture_count=diagnostic_fast_gesture()?diagnostic.gesture_count+1u:0u;
        if(diagnostic.gesture_count>=30u) {
            native_wait_capture_reset(&diagnostic_phase.wait);diagnostic.active=1u;
        }
        return; /* Collection begins on the NEXT proved controller completion. */
    }
    struct native_wait_capture *c=&diagnostic_phase.wait;
    if(native_wait_capture_add(c,(uint32_t)(uintptr_t)f,physical(config.lease.payload)-32u,
       config.lease.lease_bytes+32u,diagnostic_phase_read,NULL)) {
        native_wait_capture_snapshot(c);native_wait_capture_report(c);
    }
}
