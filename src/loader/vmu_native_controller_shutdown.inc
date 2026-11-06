/* SPDX-License-Identifier: GPL-3.0-only */
/* A controller capture grants no ownership after the original SDK stops its
 * Maple hardware. This cancellation capability is deliberately separate from
 * the normal transfer proof: it grants no RX, MMIO, storage or reader work.
 * No descriptor or configuration ABI changes, or retained mutable state. */
static bool controller_shutdown_span(uint32_t address,unsigned bytes) {
    uint32_t begin=physical(config.driver.load_address),at=physical(address);
    return (address&UINT32_C(0xe0000000))==(config.driver.load_address&UINT32_C(0xe0000000)) &&
        ram(address,bytes,2u) && at>=begin && at-begin<=config.driver.image_bytes &&
        bytes<=config.driver.image_bytes-(at-begin);
}
/* Keep PC-relative roles and source aliases intact, but observe current RAM
 * through P2. A secondary DMA may have replaced bytes behind stale P1 lines. */
static bool controller_shutdown_source_read(uint32_t address,unsigned width,uint32_t *value) {
    return read_value(NULL,physical(address)|UINT32_C(0xa0000000),width,value);
}
static bool controller_shutdown_literal(uint32_t pc,unsigned width,uint32_t *value) {
    uint32_t op,address;
    if(!controller_shutdown_span(pc,2u) || !controller_shutdown_source_read(pc,2u,&op)) return false;
    address=width==4u?((pc+4u)&~3u)+4u*(op&255u):pc+4u+2u*(op&255u);
    return controller_shutdown_span(address,width) && controller_shutdown_source_read(address,width,value);
}
static bool controller_shutdown_source(void) {
    /* Uniform SDK consumer followed by its exact finish routine. Bind every
     * instruction, internal branch/delay, source literal and both complete
     * timer helpers. PC-relative displacements may relocate only when their
     * decoded roles still agree. No title, image hash or address whitelist. */
    static const uint16_t body[]={
        /* Save R8..R14/PR; get timer, delta helper, manager and first limit. */
        0x2fe6,0x2fd6,0x2fc6,0x2fb6,0x2fa6,0x2f96,0x4f22,0xdd00,
        0x4d0b,0x0009,0xdb00,0x6903,0xde00,0xda00,0xa007,0xec01,
        /* Bounded MDST wait, using only the bound timer helpers. */
        0x4d0b,0x0009,0x6503,0x4b0b,0x6493,0x30a7,0x8905,0x9000,
        0x62e2,0x032e,0x5136,0x21c8,0x8bf2,
        /* Disable MDST/MDEN/MDTSEL; install protection, timeout and MDSTAR. */
        0x9000,0xe400,0x62e2,0x032e,0x1346,0x63e2,0x023e,0x1245,
        0x63e2,0x023e,0x1244,0x63e2,0xd100,0x023e,0x9000,0x0216,
        0x9000,0x63e2,0xd100,0x023e,0x9000,0x0216,0x9000,0x63e2,
        0xd100,0x023e,0x4d0b,0x1211,
        /* Second bounded MDST wait and exact callee-saved restoration. */
        0x9a00,0xa007,0x6903,0x4d0b,0x0009,0x6503,0x4b0b,0x6493,
        0x30a7,0x8905,0x9000,0x62e2,0x032e,0x5136,0x21c8,0x8bf2,
        0x4f26,0xe000,0x69f6,0x6af6,0x6bf6,0x6cf6,0x6df6,0x000b,0x6ef6
    };
    static const uint8_t longs[]={14u,20u,24u,26u,82u,94u,106u};
    static const struct {uint8_t offset;uint16_t value;} words[]={
        {46u,0x10f4u},{58u,0x10f4u},{86u,0x008cu},{90u,0x10f4u},
        {98u,0x0080u},{102u,0x10f4u},{114u,0x4268u},{134u,0x10f4u}
    };
    uint32_t entry=config.driver.consumer+0xa2u,value,timers[2];
    if(config.driver.family!=KUI_VMU_NATIVE_FAMILY_UNIFORM_V1 ||
       entry<config.driver.consumer || !controller_shutdown_span(entry,sizeof(body))) return false;
    for(unsigned i=0;i<sizeof(body)/sizeof(body[0]);i++) {
        uint32_t expected=body[i];
        if(!controller_shutdown_source_read(entry+2u*i,2u,&value) ||
           (((expected&0xf000u)==0xd000u || (expected&0xf000u)==0x9000u)?
            (value&0xff00u)!=expected:value!=expected)) return false;
    }
    for(unsigned i=0;i<sizeof(longs);i++) {
        if(!controller_shutdown_literal(entry+longs[i],4u,&value)) return false;
        if(i<2u) timers[i]=value;
        else {
            uint32_t expected=i==2u?config.driver.manager_cell:i==3u?100000u:
                i==4u?UINT32_C(0x6155404f):i==5u?UINT32_C(0xc3500000):UINT32_C(0x0cff0000);
            if(value!=expected) return false;
        }
    }
    for(unsigned i=0;i<sizeof(words)/sizeof(words[0]);i++)
        if(!controller_shutdown_literal(entry+words[i].offset,2u,&value) || value!=words[i].value)
            return false;
    /* syTmrGetCount: ~TMU0 count; syTmrDiffCount: current - starting count.
     * These are source identities, never invoked by the cancellation path. */
    static const uint16_t timer[5]={0xd200,0xe0ff,0x6322,0x000b,0x3038};
    static const uint16_t difference[3]={0x6053,0x000b,0x3048};
    if(!controller_shutdown_span(timers[0],sizeof(timer)) ||
       !controller_shutdown_span(timers[1],sizeof(difference))) return false;
    for(unsigned i=0;i<5u;i++)
        if(!controller_shutdown_source_read(timers[0]+2u*i,2u,&value) ||
           (i?(value!=timer[i]):(value&0xff00u)!=timer[i])) return false;
    for(unsigned i=0;i<3u;i++)
        if(!controller_shutdown_source_read(timers[1]+2u*i,2u,&value) || value!=difference[i]) return false;
    return controller_shutdown_literal(timers[0],4u,&value) && value==UINT32_C(0xffd8000c);
}
/* This reader accepts only indices into the reviewed readable-register
 * table. MDAPRO is write-only: its exact source write is proved above, never
 * sampled as hardware evidence. General RAM/MMIO admission is unchanged. */
static bool controller_shutdown_mmio_read(unsigned index,uint32_t *value) {
    static const uint32_t addresses[]={UINT32_C(0xa05f6c18),UINT32_C(0xa05f6c14),
        UINT32_C(0xa05f6c10),UINT32_C(0xa05f6c80),UINT32_C(0xa05f6c04)};
    if(index>=sizeof(addresses)/sizeof(addresses[0])) return false;
#ifdef KUI_VMU_NATIVE_PASSIVE_HOST_TEST
    return kui_vmu_native_passive_test_read(addresses[index],4u,value);
#else
    *value=*(volatile const uint32_t *)(uintptr_t)addresses[index];return true;
#endif
}
static bool controller_shutdown_mmio_state(uint32_t source,uint32_t enabled) {
    /* Check idle first and last. MSYS reserved bits are not evidence: only
     * the timeout, trigger-selection and speed fields can carry authority.
     * A stopped image authorizes only abandoning this physical capture. */
    static const uint32_t values[]={0u,0u,0u,UINT32_C(0xc3500000),UINT32_C(0x0cff0000)};
    uint32_t value;
    for(unsigned i=0;i<5u;i++) {
        uint32_t mask=i==3u?UINT32_C(0xffff130f):UINT32_MAX;
        uint32_t expected=i==1u?enabled:i==4u?source:values[i];
        if(!controller_shutdown_mmio_read(i,&value) || (value&mask)!=expected) return false;
    }
    return controller_shutdown_mmio_read(0u,&value) && !value;
}
static bool controller_shutdown_mmio(void) {
    return controller_shutdown_mmio_state(UINT32_C(0x0cff0000),0u);
}
/* The SDK continues to poll after finish, with MDEN clear. It still rebuilds
 * MDSTAR and calls its consumer although no DMA ran. Keep those original
 * instructions transparent, without treating their idle callbacks as owned
 * transfers or reading any RX data. The first enabled submission must pass
 * the complete ordinary admission again. */
static bool controller_suspended(const struct kui_vmu_native_passive_frame *f,unsigned phase) {
    uint32_t enabled,source;
    if(!f || !state.armed || state.pending || state.submitted!=state.completed ||
       overlay.pending || !overlay.admitted || durable_card.faulted ||
       (!reader_only() && !durable_card.ready) ||
       !stack_valid() || !controller_shutdown_mmio_read(1u,&enabled)) fail(phase?0x47u:0x46u);
    if(enabled>1u || !controller_shutdown_mmio_read(4u,&source) ||
       (source!=physical(config.tx_base) && source!=physical(config.tx_base)+KUI_VMU_NATIVE_TX_BANK_BYTES))
        fail(phase?0x47u:0x46u);
    unsigned bank=source==physical(config.tx_base)?0u:1u;
    state.source=source;controller_pair.bank=bank;
    if(!controller_pair_valid() || !installed_hooks_valid() || !lease_valid() ||
       !controller_shutdown_source() || !controller_shutdown_mmio_state(source,enabled) ||
       !transfer_frame_valid(f,bank,phase) || !transfer_sdk_context_valid(bank,phase) ||
       !controller_layout_valid() || !chain(bank,false) || !chain(bank,true) ||
       !controller_shutdown_mmio_state(source,enabled)) fail(phase?0x47u:0x46u);
    if(!phase && enabled==1u) {
        controller_pair.pending=NATIVE_CONTROLLER_IDLE;return false;
    }
    return true;
}
static bool controller_shutdown_valid(const struct kui_vmu_native_passive_frame *f) {
    /* A current ownership failure is never converted to publication. Only
     * the exact stopped-MDSTAR case can nominate the separate cancellation
     * proof. Every failed attempt retains a specific refusal witness. */
    return state.runtime_result==UINT32_C(0x20020) &&
        state.fault_at==UINT32_C(0xa05f6c04) && state.fault_value==UINT32_C(0x0cff0000) &&
        controller_pair.pending==NATIVE_CONTROLLER_PREPARED && live_plan.ready &&
        controller_pair_valid() && installed_hooks_valid() && lease_valid() &&
        controller_shutdown_source() && controller_shutdown_mmio() &&
        transfer_frame_valid(f,controller_pair.bank,1u) &&
        transfer_sdk_context_valid(controller_pair.bank,1u) && controller_layout_valid() &&
        controller_shutdown_mmio();
}
