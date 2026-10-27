/* SPDX-License-Identifier: GPL-3.0-only */
#if KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION
/* The original SDK owns this unchanged physical DMA. This capture grants no
 * RX publication, virtual packet, storage call, or MDSTAR change. All fresh
 * hook/lifetime/layout/response checks still precede our one parent-byte store
 * in completion. Mixed/accessory/virtual/storage batches use the old submit.
 * With pending0, snapshot is tentative scratch; a miss publishes no pair. */
static bool split_separate(uint32_t a,uint32_t bytes,uint32_t b,uint32_t other) {
    return a+bytes<=b || b+other<=a;
}
static bool split_controller_geometry(void) {
    uint32_t tx=physical(config.tx_base),guard=physical(config.lease.payload)-32u;
    uint32_t guard_bytes=config.lease.lease_bytes+32u;
    if(config.lease.lease_bytes>UINT32_MAX-32u || physical(config.lease.payload)<32u ||
       !ram(config.driver.manager_cell,4u,4u) || !ram(config.driver.lock_cell,8u,4u) ||
       !ram(config.driver.manager_initial,0x1100u,4u) || !ram(config.driver.completion_slot,4u,4u) ||
       !ram(config.tx_base,2u*KUI_VMU_NATIVE_TX_BANK_BYTES,32u) ||
       !ram(guard,guard_bytes,1u) ||
       !split_separate(tx,2u*KUI_VMU_NATIVE_TX_BANK_BYTES,guard,guard_bytes)) return false;
    unsigned nonzero=0u;
    for(unsigned i=0u;i<KUI_VMU_NATIVE_RX_SLOTS;i++) {
        uint32_t raw=config.layout.rx_slots[i],rx=physical(raw);
        if(!raw) continue;
        if((raw&UINT32_C(0xe0000000))!=UINT32_C(0xa0000000) || !ram(raw,1024u,32u) ||
           !split_separate(rx,1024u,guard,guard_bytes) ||
           !split_separate(rx,1024u,tx,2u*KUI_VMU_NATIVE_TX_BANK_BYTES) ||
           !split_separate(rx,1024u,physical(config.driver.manager_initial),0x1100u) ||
           !split_separate(rx,1024u,physical(config.driver.manager_cell),4u) ||
           !split_separate(rx,1024u,physical(config.driver.lock_cell),8u) ||
           !split_separate(rx,1024u,physical(config.driver.completion_slot),4u)) return false;
        for(unsigned j=0u;j<i;j++) if(config.layout.rx_slots[j] &&
            !split_separate(rx,1024u,physical(config.layout.rx_slots[j]),1024u)) return false;
        ++nonzero;
    }
    return nonzero!=0u;
}
/* These direct words are admitted immutable roles or a bounded word inside
 * one of the two arm-proved TX banks. Keep the SDK's own pointer aliases;
 * descriptor/RX buffers themselves use the coherent P2 alias. */
static inline __attribute__((always_inline)) bool split_controller_word(uint32_t address,uint32_t *value) {
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    return kui_vmu_native_passive_test_read(address,4u,value);
#else
    *value=*(volatile const uint32_t *)(uintptr_t)address;return true;
#endif
}
static inline __attribute__((always_inline)) bool split_controller_capture(
    const struct kui_vmu_native_passive_frame *f) {
    if(!f || !state.armed || state.pending || state.submitted!=state.completed ||
       state.submitted>=PAIR_LIMIT || controller_pair.pending || overlay.pending || !overlay.admitted ||
       kui_vmu_native_passive_resume!=config.driver.resume)
        return false;
    uint32_t sp=current_sp(),low=active_stack_low();
    unsigned mask=(f->sr>>4)&15u;
    if(((current_sr()>>4)&15u)!=15u || sp<low+256u || sp>=low+0x1000u ||
       (mask!=6u && mask!=15u) || f->r12!=1u ||
       !ram(f->r14,4u,4u) || physical(f->r14)!=physical(config.driver.manager_cell)) return false;
    uint32_t source,manager,lock,value;
    if(!split_controller_word(UINT32_C(0xa05f6c04),&source)) return false;
    unsigned bank;
    if(source==physical(config.tx_base)) bank=0u;
    else if(source==physical(config.tx_base)+KUI_VMU_NATIVE_TX_BANK_BYTES) bank=1u;
    else return false;
    if(!split_controller_word(f->r14,&manager) ||
       !ram(manager,0x1100u,4u) || physical(manager)!=physical(config.driver.manager_initial) ||
       !split_controller_word(config.driver.lock_cell,&lock) ||
       lock!=((physical(config.driver.lock_cell)+4u)|UINT32_C(0xa0000000)) ||
       !read_value(NULL,lock,1u,&value) || value!=0x80u) return false;
#define SPLIT_EXPECT(address,expected) do { \
    if(!split_controller_word((address),&value) || value!=(expected)) return false; \
} while(0)
    SPLIT_EXPECT(UINT32_C(0xa05f6c18),0u);
    SPLIT_EXPECT(UINT32_C(0xa05f6c10),0u);
    SPLIT_EXPECT(UINT32_C(0xa05f6c14),1u);
    SPLIT_EXPECT(manager+0xfc0u,1u);
    SPLIT_EXPECT(manager+0x10b0u,source|UINT32_C(0xa0000000));
    SPLIT_EXPECT(manager+0x10b4u,bank);
    SPLIT_EXPECT(manager+0x10b8u,bank);
    SPLIT_EXPECT(manager+0x10c4u,0u);
    SPLIT_EXPECT(manager+0x10f4u,UINT32_C(0xa05f6c00));
#undef SPLIT_EXPECT
    unsigned at=0u,ports=0u,count=0u;
    while(at<CONTROLLER_WORDS) {
        uint32_t control;
        if(!split_controller_word((source+at*4u)|UINT32_C(0xa0000000),&control)) return false;
        controller_pair.snapshot[at]=control;
        if(control==UINT32_C(0x700)) {++at;continue;}
        unsigned words=control&255u,width=3u+words;
        if((control&~UINT32_C(0x800300ff)) || words>1u ||
           width>CONTROLLER_WORDS-at || count==CONTROLLER_FRAMES) return false;
        for(unsigned j=1u;j<width;j++) if(!split_controller_word(
            (source+(at+j)*4u)|UINT32_C(0xa0000000),&controller_pair.snapshot[at+j])) return false;
        uint32_t rx=controller_pair.snapshot[at+1u],header=controller_pair.snapshot[at+2u];
        unsigned port=(control>>16)&3u,command=header&255u;
        uint32_t endpoint=UINT32_C(0x00002000)|(port<<14)|(port<<22);
        bool query=!words && (command==1u || command==2u) && header==(endpoint|command);
        bool condition=words==1u && header==(endpoint|UINT32_C(0x01000009)) &&
            controller_pair.snapshot[at+3u]==UINT32_C(0x01000000);
        uint32_t expected=config.layout.rx_slots[bank*24u+port*6u];
        if((!query && !condition) || (ports&(1u<<port)) || !expected || rx!=physical(expected))
            return false;
        ports|=1u<<port;++count;at+=width;
        if(control&UINT32_C(0x80000000)) {
            if(!(ports&1u)) return false;
            controller_pair.source=source;controller_pair.bank=bank;controller_pair.words=at;
            controller_pair.count=0u;controller_pair.parent=CONTROLLER_FRAMES;
            state.source=source;state.submit_sr=f->sr;
            __asm__ volatile("":::"memory");
            state.pending=1u;++state.submitted;
            __asm__ volatile("":::"memory");
            controller_pair.pending=NATIVE_CONTROLLER_CAPTURED;
            return true;
        }
    }
    return false;
}
void __attribute__((noipa)) kui_vmu_native_passive_split_submit(
    const struct kui_vmu_native_passive_frame *f) {
    if(!split_controller_capture(f)) kui_vmu_native_passive_submit(f);
}
static void split_controller_prepare_complete(const struct kui_vmu_native_passive_frame *f) {
    uint32_t source=controller_pair.source;
    unsigned bank=controller_pair.bank,words=controller_pair.words;
    if(controller_pair.pending!=NATIVE_CONTROLLER_CAPTURED || bank>1u || !words ||
       words>CONTROLLER_WORDS || source!=state.source ||
       source!=physical(config.tx_base)+bank*KUI_VMU_NATIVE_TX_BANK_BYTES)
        CONTROLLER_FAIL(0x30u,0u,source,words,CONTROLLER_WORDS);
    if(!live_plan.ready) {
        struct kui_vmu_native_runtime_snapshot snapshot;
        if(!live_boundary(f,1u,source,words*4u,&snapshot)) fail(0x47u);
    }
    for(unsigned i=0u;i<words;i++) {
        uint32_t address=(source+i*4u)|UINT32_C(0xa0000000),value=UINT32_MAX;
        if(!split_controller_word(address,&value) || value!=controller_pair.snapshot[i])
            CONTROLLER_FAIL(0x31u,i,address,value,controller_pair.snapshot[i]);
    }
    /* Reparse independently at completion. No captured RX pointer is followed
     * until its exact slot matches the newly bound or existing live layout. */
    unsigned at=0u,count=0u,ports=0u,nops=0u,parent=CONTROLLER_FRAMES;
    while(at<words) {
        uint32_t control=controller_pair.snapshot[at];
        if(control==UINT32_C(0x700)) {++nops;++at;continue;}
        unsigned payload=control&255u,width=payload+3u,port=(control>>16)&3u;
        if((control&~UINT32_C(0x800300ff)) || payload>1u || width>words-at ||
           count==CONTROLLER_FRAMES || (ports&(1u<<port)))
            CONTROLLER_FAIL(0x32u,at,source+at*4u,control,0u);
        uint32_t header=controller_pair.snapshot[at+2u],rx=controller_pair.snapshot[at+1u];
        uint32_t endpoint=UINT32_C(0x00002000)|(port<<14)|(port<<22);
        unsigned command=header&255u;
        if(payload? (header!=(endpoint|UINT32_C(0x01000009)) ||
                    controller_pair.snapshot[at+3u]!=UINT32_C(0x01000000)):
                   ((command!=1u && command!=2u) || header!=(endpoint|command)))
            CONTROLLER_FAIL(0x33u,at,source+(at+2u)*4u,header,endpoint);
        unsigned slot=bank*24u+port*6u;
        uint32_t expected=config.runtime.rx_slots[slot];
        if(!expected || rx!=physical(expected) || expected!=config.layout.rx_slots[slot])
            CONTROLLER_FAIL(0x34u,slot,source+(at+1u)*4u,rx,physical(expected));
        controller_pair.frames[count].rx=rx;controller_pair.frames[count].header=header;
        if(!port) parent=count;
        ++count;ports|=1u<<port;at+=width;
        if(control&UINT32_C(0x80000000)) {
            if(at!=words || parent==CONTROLLER_FRAMES)
                CONTROLLER_FAIL(0x35u,at,source+at*4u,at,words);
            controller_pair.count=count;controller_pair.nops=nops;controller_pair.parent=parent;
            state.bytes=words*4u;state.descriptors=count;state.nops=nops;
            state.first_rx=controller_pair.frames[0].rx;state.rx_slots=live_plan.nonzero;
            ++overlay.stats.prepared;controller_pair.pending=NATIVE_CONTROLLER_PREPARED;
            return;
        }
    }
    CONTROLLER_FAIL(0x36u,at,source+at*4u,at,words);
}
#endif
