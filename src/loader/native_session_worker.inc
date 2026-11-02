/* SPDX-License-Identifier: GPL-3.0-only */
/* Included by the separately linked session-controls worker only. Every
 * retained map, mutable byte and parked-menu buffer belongs to its lease. */
static uint32_t session_word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static kui_retail_async_region_type *session_region;
static uint32_t session_manifest_cell,session_firmware[9];
static bool native_session_return_guard(void);
static int native_session_control_visit(uint32_t operation,
    const struct kui_retail_manifest *next,struct kui_loader_sd *return_card);
static kui_native_session_control session_control(void) {return native_session_control_visit;}
static bool session_low_span(uint32_t address,uint32_t bytes) {
    return !(address&3u) && address>=UINT32_C(0x8c004000) &&
        address<=UINT32_C(0x8c007ba0)-bytes;
}
static void session_purge(uint32_t address,uint32_t bytes) {
    uint32_t end=address+bytes;
    for(uint32_t at=address&~31u;at<end;at+=32u)
        __asm__ volatile("ocbp @%0"::"r"(at):"memory");
}
static bool session_bind(void) {
    uint32_t target=config.session_control;
    if((target&1u) || target<UINT32_C(0x8c004028) || target>=UINT32_C(0x8c007ba0))return false;
    kui_native_session_control bind=(kui_native_session_control)(uintptr_t)target;
    uint32_t address=(uint32_t)bind(KUI_NATIVE_SESSION_BIND,NULL,NULL);
    if(!session_low_span(address,sizeof(struct kui_native_session_binding)))return false;
    const struct kui_native_session_binding *b=(const void *)(uintptr_t)address;
    if(b->magic!=KUI_NATIVE_SESSION_BINDING_MAGIC || b->version!=KUI_NATIVE_SESSION_BINDING_VERSION ||
       b->bytes!=sizeof(*b) || !session_low_span(b->region_cell,4u) ||
       !session_low_span(b->manifest_cell,4u) || !session_low_span(b->firmware_snapshot,sizeof(session_firmware)))return false;
    if(b->region_cell==b->manifest_cell ||
       (b->region_cell>=b->firmware_snapshot && b->region_cell-b->firmware_snapshot<sizeof(session_firmware)) ||
       (b->manifest_cell>=b->firmware_snapshot && b->manifest_cell-b->firmware_snapshot<sizeof(session_firmware)))return false;
    uint32_t region=(physical(config.lease.payload)|UINT32_C(0x80000000))+KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET;
    uint32_t map=(physical(config.lease.payload)|UINT32_C(0x80000000))+KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES;
    if(*(const uint32_t *)(uintptr_t)b->region_cell!=region ||
       *(const uint32_t *)(uintptr_t)b->manifest_cell!=map)return false;
    session_region=(void *)(uintptr_t)region;session_manifest_cell=b->manifest_cell;
    memcpy(session_firmware,(const void *)(uintptr_t)b->firmware_snapshot,sizeof(session_firmware));
    config.session_control=0u;return true;
}
static bool session_next_valid(const struct kui_retail_manifest *next) {
    uint32_t address=(uint32_t)(uintptr_t)next;
    uint32_t area=address&UINT32_C(0xe0000000),p=physical(address);
    const struct kui_retail_manifest *active=session_region->engine.manifest;
    if(!next || (address&7u) || (area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000)) ||
       p<physical(config.lease.payload)+80u ||
       p>physical(config.lease.payload)+KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES-0x1000u-sizeof(*next) ||
       next->storage_transport!=KUI_STORAGE_SCI || next->reader!=KUI_RETAIL_READER_ASYNC ||
       (next->flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED)) ||
       next->card_sectors!=active->card_sectors || next->partition_start!=active->partition_start ||
       next->partition_end!=active->partition_end || !next->track_count ||
       next->track_count+next->extent_count>KUI_RETAIL_ASYNC_SLOTS)return false;
    /* The only selectable maps are immutable records decoded into this set. */
    for(uint32_t i=0;i<session_discs.count;++i)
        if(next==(const void *)session_discs.maps[i])return true;
    return false;
}
static int native_session_switch_transaction(struct kui_loader_sd *loan) {
    kui_retail_async_region_type *r=session_region;
    struct kui_retail_gd *s=&r->shared.service;
    struct kui_retail_async *e=&r->engine;
    if(!loan || !loan->ready || !native_session_return_guard() ||
       e->native_loan_state!=KUI_VMU_NATIVE_ASYNC_LOAN_WRITER || !s->initialized ||
       s->pending || s->executing || e->active || e->in_irq || e->opened || e->hooked ||
       e->release.armed || e->release.depth || r->shared.stream.state ||
       (session_switch.operation!=KUI_NATIVE_SESSION_RETURN &&
        session_switch.operation!=KUI_NATIVE_SESSION_DISC_OPEN && session_switch.operation!=KUI_NATIVE_SESSION_DISC_CLOSE))return -1;
    int result=0;
    if(session_switch.operation==KUI_NATIVE_SESSION_DISC_OPEN)result=kui_retail_gd_disc_open(s);
    else if(session_switch.operation==KUI_NATIVE_SESSION_DISC_CLOSE) {
        const struct kui_retail_manifest *next=session_switch.next;
        if(!session_next_valid(next))return -1;
        result=kui_retail_gd_disc_close(s,next);
        if(!result) {
            e->manifest=next;memset(&e->cursor,0,sizeof(e->cursor));
            e->token=e->destination=e->since=e->failed=e->retries=0u;
            e->disc_generation=s->disc_generation;e->target=20u;
            session_purge(session_manifest_cell,4u);
            *(const struct kui_retail_manifest **)(uintptr_t)(session_manifest_cell|UINT32_C(0x20000000))=next;
        }
    } else session_switch.card=*loan;
    session_switch.result=result?-1:1;
    return result;
}
static void session_restore_firmware(void) {
    session_purge(UINT32_C(0x8c0000bc),8u);session_purge(UINT32_C(0x8c0000e0),4u);
    session_purge(UINT32_C(0x8c001000),12u);session_purge(UINT32_C(0x8c0010f0),12u);
    *(volatile uint32_t *)(uintptr_t)UINT32_C(0xac0000bc)=session_firmware[0];
    *(volatile uint32_t *)(uintptr_t)UINT32_C(0xac0000c0)=session_firmware[1];
    *(volatile uint32_t *)(uintptr_t)UINT32_C(0xac0000e0)=session_firmware[2];
    for(unsigned i=0;i<3u;++i) {
        *(volatile uint32_t *)(uintptr_t)(UINT32_C(0xac001000)+i*4u)=session_firmware[i+3u];
        *(volatile uint32_t *)(uintptr_t)(UINT32_C(0xac0010f0)+i*4u)=session_firmware[i+6u];
    }
    extern void kui_native_session_invalidate(void);
    kui_native_session_invalidate();
}
static int native_session_control_visit(uint32_t operation,
    const struct kui_retail_manifest *next,struct kui_loader_sd *return_card) {
    kui_retail_async_region_type *r=session_region;
    if(!r || !native_session_return_guard() || !r->shared.service.initialized ||
       r->shared.card.transport!=KUI_STORAGE_SCI)return -1;
    struct kui_retail_gd *s=&r->shared.service;
    if(operation==KUI_NATIVE_SESSION_STATUS) {
        if(s->disc_lid==KUI_RETAIL_DISC_OPEN && s->disc_observed)(void)kui_retail_gd_disc_absent(s);
        uint32_t lid=s->disc_lid==KUI_RETAIL_DISC_READY?KUI_NATIVE_SESSION_READY:
            s->disc_lid==KUI_RETAIL_DISC_OPEN?KUI_NATIVE_SESSION_OPEN:KUI_NATIVE_SESSION_EMPTY;
        return (int)(lid|(s->disc_observed?KUI_NATIVE_SESSION_OBSERVED:0u));
    }
    if(operation!=KUI_NATIVE_SESSION_DISC_OPEN && operation!=KUI_NATIVE_SESSION_DISC_CLOSE &&
       operation!=KUI_NATIVE_SESSION_RETURN)return -1;
    if((operation==KUI_NATIVE_SESSION_DISC_OPEN && s->disc_lid!=KUI_RETAIL_DISC_READY) ||
       (operation==KUI_NATIVE_SESSION_DISC_CLOSE && (!session_next_valid(next) || s->disc_lid==KUI_RETAIL_DISC_READY)))return -1;
    if(s->pending || s->executing || r->engine.active || r->engine.in_irq || r->engine.release.depth ||
       (operation==KUI_NATIVE_SESSION_DISC_CLOSE && !s->disc_observed))return 0;
    if(r->engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_QUARANTINED)return -1;
    if(r->engine.native_loan_state!=KUI_VMU_NATIVE_ASYNC_LOAN_IDLE)return 0;
    uint32_t visit=r->vector600[KUI_NATIVE_RA_ENTRY_INDEX];
    uint32_t first=(physical(config.lease.payload)|UINT32_C(0x80000000))+KUI_NATIVE_RA_AUX_OFFSET;
    if((visit&1u) || visit<first || visit>=first+KUI_NATIVE_RA_AUX_IMAGE_BYTES)return -1;
    if(!((kui_native_ra_visit)(uintptr_t)visit)(KUI_NATIVE_RA_RESET,0u,r,0u))return 0;
    session_switch.active=UINT32_C(0x4b535754);session_switch.operation=operation;
    session_switch.next=next;session_switch.result=-1;
    int (*lease)(kui_vmu_sd_transaction,void *)=(void *)(uintptr_t)config.sd_lease;
    int result=lease(kui_vmu_native_persistent_sd_transaction,&session_switch);
    session_switch.active=0u;
    if(result==KUI_VMU_SD_LEASE_BUSY)return 0;
    if(result!=KUI_VMU_SD_LEASE_OK || session_switch.result!=1)return -1;
    if(operation==KUI_NATIVE_SESSION_RETURN) {
        if(return_card)*return_card=session_switch.card;
        session_restore_firmware();
    }
    return 1;
}
static bool native_session_worker_prepare(void) {
    uint32_t source=config.session_wire;
    if(source<UINT32_C(0x8ce00000) || source>UINT32_C(0x8cfe0000)-KUI_NATIVE_SESSION_WIRE_BYTES ||
       (source&3u) || config.session_wire_bytes!=KUI_NATIVE_SESSION_WIRE_BYTES || !session_bind())return false;
    const uint8_t *wire=(const void *)(uintptr_t)source;
    if(!kui_native_session_wire_valid(wire,KUI_NATIVE_SESSION_WIRE_BYTES,true))return false;
    uint32_t flags=session_word(wire+KUI_NATIVE_SESSION_FLAGS_OFFSET);
    uint32_t address=(physical(config.lease.payload)|UINT32_C(0x80000000))+KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES;
    if(!kui_vmu_native_background_map_address(config.lease.payload,address) ||
       !kui_native_disc_set_prepare_wire(&session_discs,wire,KUI_NATIVE_SESSION_WIRE_BYTES,
           (const void *)(uintptr_t)(address|UINT32_C(0x20000000))))return false;
    memset(&session_return_plan,0,sizeof(session_return_plan));
    if(flags&KUI_NATIVE_SESSION_FLAG_RETURN) {
        if(!kui_native_return_decode(&session_return_plan,wire+KUI_NATIVE_SESSION_RETURN_OFFSET,
            KUI_NATIVE_SESSION_RETURN_BYTES))return false;
        const struct kui_retail_manifest *active=session_region->engine.manifest;
        if(session_return_plan.card_sectors!=active->card_sectors ||
           session_return_plan.partition_first!=active->partition_start ||
           session_return_plan.partition_end!=active->partition_end)return false;
        session.return_available=kui_native_return_lease_safe(&session_return_plan,config.lease.payload,
            config.lease.lease_bytes)?1u:0u;
    }
    config.session_wire=0u;config.session_wire_bytes=0u;session.configured=1u;return true;
}
struct native_session_return_reader {struct kui_loader_sd *card;struct kui_loader_sd_stream stream;};
static bool native_session_return_read(void *context,uint32_t lba,uint8_t sector[512]) {
    struct native_session_return_reader *reader=context;
    if(reader->stream.active && reader->stream.next_lba!=lba)return false;
    if(!reader->stream.active) {
        uint32_t available=0u;
        for(unsigned i=0;i<session_return_plan.extent_count;i++) {
            const struct kui_native_return_extent *e=&session_return_plan.extents[i];
            if(lba>=e->card_lba && lba-e->card_lba<e->blocks) {
                available=e->blocks-(lba-e->card_lba);break;
            }
        }
        if(available>KUI_LOADER_SD_MAX_READ_BLOCKS)available=KUI_LOADER_SD_MAX_READ_BLOCKS;
        if(!available || kui_loader_sd_stream_start(reader->card,&reader->stream,lba,available)!=KUI_LOADER_SD_OK)return false;
    }
    return kui_loader_sd_stream_next(reader->card,&reader->stream,sector)==KUI_LOADER_SD_OK;
}
static int native_session_return_transaction(struct kui_loader_sd *loan) {
    if(!loan || loan->blocks<session_return_plan.card_sectors || !loan->ready || loan->slow)return -1;
    struct native_session_return_reader reader={.card=loan};
    session.result=(uint32_t)kui_native_return_load(&session_return_plan,config.lease.payload,
        config.lease.lease_bytes,(void *)(uintptr_t)UINT32_C(0xac010000),
        UINT32_C(0x400000),native_session_return_read,&reader);
    if(kui_loader_sd_stream_stop(loan,&reader.stream)!=KUI_LOADER_SD_OK)session.result=KUI_NATIVE_RETURN_IO;
    return session.result==KUI_NATIVE_RETURN_OK?0:-1;
}
/* This is called AFTER a proved physical completion is published. A pending
 * VMU operation is never discarded merely because the report keys are held.
 * The disc gesture uses Start and explicitly excludes the four face buttons;
 * it never changes the game framebuffer or its PVR state. */
static void native_session_controller_complete(uint32_t controls) {
    if(!session.configured)return;
    bool controller=controls!=UINT32_MAX;
    bool next=controller && !(controls&8u) && (controls&UINT32_C(0x0606))==UINT32_C(0x0606) &&
        ((controls>>16)&255u)>=200u && (controls>>24)>=200u;
    if(next) {
        if(session.disc_hold<90u)++session.disc_hold;
        if(session.disc_hold==90u && !session.disc_latched) {
            (void)kui_native_disc_set_request_next(&session_discs);session.disc_latched=1u;
        }
    } else {session.disc_hold=0u;session.disc_latched=0u;}
    bool save_pending=durable_card.inflight || durable_card.handling || durable_card.protocol.active || overlay.pending;
    if(controller && !save_pending && !session.exit_pending && session_discs.phase!=KUI_NATIVE_DISC_IDLE &&
       kui_native_disc_set_visit(&session_discs,session_control())<0)fail(UINT32_C(0x51));
    if(session.exit_pending && !save_pending) {
        if(session.return_available && !session.return_prepared) {
            struct kui_loader_sd card;
            kui_native_session_control control=session_control();
            int ready=control?control(KUI_NATIVE_SESSION_RETURN,NULL,&card):-1;
            if(!ready)return;
            if(ready<0 || card.blocks<session_return_plan.card_sectors)fail(UINT32_C(0x52));
            session.return_prepared=1u;
        }
        /* A terminal exit never waits for a guest that stopped probing the
         * lid. Busy readers still drain before RETURN succeeds above. */
        session_discs.phase=KUI_NATIVE_DISC_IDLE;session_discs.visits=0u;
        session.exit_pending=0u;fail(UINT32_C(0x4f));
    }
}
static void native_session_return_menu(void) __attribute__((noreturn));
static bool native_session_return_guard(void) {
    /* Gameplay storage guards require an in-flight Maple completion. The
     * parked screen follows complete RX publication and has no such request;
     * renew ownership without inventing a pending transfer or game callback. */
    return state.armed && !state.pending && state.submitted==state.completed &&
        lease_valid() && stack_valid() && installed_hooks_valid() &&
        kui_vmu_native_passive_consumer==config.driver.consumer;
}
static void native_session_return_menu(void) {
    bool released=false;
    if(session.return_available)retail_display_line("RELEASE KEYS / START RETURNS TO K-UI");
    else retail_display_line("RETURN UNAVAILABLE / PHOTO THEN POWER OFF");
    for(;;) {
        uint32_t buttons=0u;
        if(!session.return_available || session.attempted ||
           kui_native_return_controller(&buttons)!=1)continue;
        if(!buttons)released=true;
        if(!released || !(buttons&8u))continue;
        session.attempted=1u;
        /* The report is terminal already. No failure after the destructive
         * payload load can resume the old game or synthesize a save ACK. */
        if(durable_card.inflight || durable_card.handling || durable_card.protocol.active ||
           durable_card.faulted || overlay.pending || state.pending ||
           session_discs.phase!=KUI_NATIVE_DISC_IDLE || !native_session_return_guard()) {
            retail_display_line("RETURN REFUSED / SAVE OR DISC BUSY");continue;
        }
        if(!session.return_prepared || !kui_native_return_quiesce()) {
            retail_display_line("RETURN REFUSED / READER OR HARDWARE BUSY");continue;
        }
        retail_display_line("LOADING K-UI FROM SD");
        session.active=UINT32_C(0x4b52544e);session.result=KUI_NATIVE_RETURN_IO;
        int (*lease)(kui_vmu_sd_transaction,void *)=(void *)(uintptr_t)config.sd_lease;
        int result=lease(kui_vmu_native_persistent_sd_transaction,&session);
        session.active=0u;
        if(result==KUI_VMU_SD_LEASE_OK && session.result==KUI_NATIVE_RETURN_OK)
            kui_native_return_enter();
        retail_display_hex("K-UI RETURN FAILED",session.result);
        retail_display_line("PHOTO THEN POWER OFF");
    }
}
