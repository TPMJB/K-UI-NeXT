/* SPDX-License-Identifier: GPL-3.0-only */
/* Authored SDK roles exercise Skies' uniform receive layout and its observed
 * late TMU callback registration through the actual compact worker. Original
 * game instructions are not embedded; this is a lifecycle regression, not a
 * complete Skies execution or timing model. Both allocation sizes and submit
 * builds run the same packets and durable card transactions. */
#define KUI_SPLIT_CONTROLLER_TEST_MAIN retained_split_reference_main
#include "test_vmu_native_split_controller.c"

#ifndef KUI_SKIES_TEST_LEASE_BYTES
#error "Supply the expected delivered lease size for this lifecycle variant"
#endif
_Static_assert(KUI_VMU_NATIVE_PASSIVE_SPAN==KUI_SKIES_TEST_LEASE_BYTES,
    "Exercise the expected delivered allocation size");
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES==KUI_SKIES_TEST_LEASE_BYTES,
    "The complete lease matches the delivered worker allocation");

static uint32_t output_digest;
static unsigned polling_pairs,registered_callbacks,storage_roundtrips;

static void skies_lifecycle_setup(void) {
    split_prepare();
    struct kui_vmu_native_layout *l=&input.layout;
    l->rx_layout=KUI_VMU_NATIVE_RX_UNIFORM48;l->inputs=1u;
    memset(l->input_values,0,sizeof(l->input_values));memset(l->indices,0,sizeof(l->indices));
    l->input_values[0]=0xac500004u;l->indices[0]=0xac400000u;l->rx_base=0xac500020u;
    put(l->input_array,4u,l->input_values[0]);put(l->index_array,4u,l->indices[0]);
    put(0x0c300fc8u,4u,l->rx_base);
    for(unsigned i=0u;i<48u;i++) {
        l->rx_slots[i]=l->rx_base+i*1024u;put(0x0c300fe8u+i*4u,4u,l->rx_slots[i]);
    }
    /* Ready bottom reservation, matching the compact stage's original
     * allocator transaction. No worker-reader tail remains allocated. */
    input.lease.begin=input.lease.header;
    uint32_t upper=input.lease.payload+input.lease.lease_bytes;
    const struct kui_vmu_native_heap *h=&input.lease.owner;
    put(h->begin_cell,4u,input.lease.begin);put(h->begin_copy_cell,4u,input.lease.begin);
    put(h->sentinel,4u,upper);put(h->cursor_cell,4u,h->sentinel);
    put(upper,4u,h->sentinel);put(upper+4u,4u,input.lease.remaining);
    CHECK(kui_vmu_native_heap_bottom_ready(&input.lease,read_heap,NULL,1u));
    arm();install_hooks();
    CHECK(split_controller_geometry() && !live_plan.ready &&
        config.lease.lease_bytes==KUI_VMU_NATIVE_PASSIVE_SPAN &&
        config.lease.payload==config.lease.begin+32u);
}

static void skies_wrapper_submit(void) {
    if(setjmp(terminal)==0) kui_vmu_native_passive_split_submit(&frame);
    else {
        fprintf(stderr,"Skies lifecycle submit refused: %u/%08x\n",terminal_error,state.runtime_result);
        CHECK(false);
    }
    CHECK(state.pending && state.submitted==state.completed+1u);
#if KUI_NATIVE_VMU_ONLY_FULL_SUBMIT
    CHECK(controller_pair.pending!=NATIVE_CONTROLLER_CAPTURED);
    CHECK(controller_pair.pending==NATIVE_CONTROLLER_PREPARED || overlay.pending);
#else
    CHECK(controller_pair.pending==NATIVE_CONTROLLER_CAPTURED && !overlay.pending);
#endif
}

static void skies_poll(unsigned conditions,unsigned allinfo,unsigned padding,
    uint32_t callback_cell,uint32_t callback) {
    split_batch(conditions,allinfo,padding);
    uint32_t source=value_at(0xa05f6c04u);
    uint8_t original[128];memcpy(original,memory+source-0x0c000000u,sizeof(original));
    skies_wrapper_submit();
    /* The real Skies timer setter is invoked after initial null binding.
     * This fixture changes its admitted callback cell between SDK visits;
     * callbacks are never executed under our IMASK15 transaction. */
    put(callback_cell,4u,callback);++registered_callbacks;
    split_done(conditions,allinfo,UINT32_C(0x01000000));
    /* Empty ports and real controllers are valid on both retained lanes.
     * Do not use the generic split fixture's authored noncontroller main-unit
     * functions: the full physical path deliberately has a narrower type. */
    for(unsigned port=1u;port<4u;port++) {
        if(padding) put(rx_at(port*6u),4u,UINT32_MAX);
        else put(rx_at(port*6u)+4u,4u,UINT32_C(0x01000000));
    }
    uint8_t expected[4][1024];
    for(unsigned port=0u;port<4u;port++) memcpy(expected[port],rx_bytes(port*6u),1024u);
    expected[0][2]|=2u;
    complete_return();
    CHECK(live_plan.ready && config.runtime.rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48 &&
        !state.pending && !controller_pair.pending && !overlay.pending);
    CHECK(!memcmp(original,memory+source-0x0c000000u,sizeof(original)));
    for(unsigned port=0u;port<4u;port++) {
        CHECK(!memcmp(expected[port],rx_bytes(port*6u),1024u));
        output_digest=kui_retail_crc32(output_digest,rx_bytes(port*6u),1024u);
    }
    CHECK(kui_vmu_native_heap_bottom_ready(&config.lease,read_heap,NULL,1u));
    ++polling_pairs;idle();
}

static void skies_virtual_transfer(unsigned command,unsigned block,unsigned phase,const uint8_t *data) {
    stage_batch(command,block);
    uint32_t source=physical(input.tx_base)+packet_bank*0x3000u;
    if(command!=1u && command!=2u && command!=3u)
        put(source+32u,4u,(block<<24)|(phase<<8));
    if(command==12u) {CHECK(data);memcpy(memory+source+36u-0x0c000000u,data,128u);}
    if(setjmp(terminal)==0) kui_vmu_native_passive_split_submit(&frame);
    else {fprintf(stderr,"Skies virtual submit refused %u/%08x\n",terminal_error,state.runtime_result);CHECK(false);}
    CHECK(overlay.pending && !controller_pair.pending && state.pending);
    put(0xa05f6c18u,4u,1u);finish_batch();
    CHECK(kui_vmu_native_heap_bottom_ready(&config.lease,read_heap,NULL,1u));
}

static void skies_save_read_reopen(unsigned seed) {
    uint8_t data[512];for(unsigned n=0u;n<sizeof(data);n++) data[n]=(uint8_t)(n^seed);
    unsigned before=lease_calls;
    for(unsigned phase=0u;phase<4u;phase++) {
        skies_virtual_transfer(12u,42u,phase,data+phase*128u);
        CHECK(value_at(rx_at(2u))==UINT32_C(0x00020007));
    }
    CHECK(lease_calls==before);
    skies_virtual_transfer(13u,42u,4u,NULL);
    CHECK(value_at(rx_at(2u))==UINT32_C(0x00020007) && release_saw_unpublished);
    skies_virtual_transfer(11u,42u,0u,NULL);
    CHECK(value_at(rx_at(2u))==UINT32_C(0x82020008) &&
        !memcmp(rx_bytes(2u)+12u,data,sizeof(data)) && !durable_card.faulted);
    output_digest=kui_retail_crc32(output_digest,rx_bytes(2u),524u);
    model_reopen();
    struct kui_vmu_backing backing={0};struct kui_vmu_store reopened={0};
    CHECK(!kui_vmu_backing_init(&backing,&model_bus,&backing_config));
    struct kui_vmu_store_io io=kui_vmu_backing_io(&backing);
    CHECK(!kui_vmu_store_open(&reopened,&io,&store_config));
    CHECK(!kui_vmu_store_checkpoint(&reopened));
    CHECK(!memcmp(sd.media[image_lba(42u)],data,sizeof(data)));
    output_digest=kui_retail_crc32(output_digest,sd.media[image_lba(42u)],sizeof(data));
    ++storage_roundtrips;
}

static void skies_bad_callback_preserves_physical_rx(void) {
    const uint32_t invalid[]={UINT32_C(0x8ce00000),UINT32_C(0xec087498),1u};
    for(unsigned timer=0u;timer<2u;timer++) for(unsigned i=0u;i<3u;i++) {
        skies_lifecycle_setup();
        uint32_t cell=timer?input.layout.timer2_callback:input.layout.timer1_callback;
        skies_poll(1u,0u,0u,cell,0u);
        split_batch(1u,0u,0u);skies_wrapper_submit();
        put(cell,4u,invalid[i]);split_done(1u,0u,UINT32_C(0x01000000));
        for(unsigned port=1u;port<4u;port++)
            put(rx_at(port*6u)+4u,4u,UINT32_C(0x01000000));
        uint8_t before[4][1024];
        for(unsigned port=0u;port<4u;port++) memcpy(before[port],rx_bytes(port*6u),1024u);
        reject_complete();
        CHECK(terminal_error==0x47u &&
            state.runtime_result==(UINT32_C(0x40026)|((5u+timer)<<8)) &&
            state.fault_at==cell && state.fault_value==invalid[i]);
        for(unsigned port=0u;port<4u;port++) CHECK(!memcmp(before[port],rx_bytes(port*6u),1024u));
        CHECK(!lease_calls && !sd.bytes && !sd.writes && !durable_card.commits);
    }
}

int main(void) {
    const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    for(unsigned a=0u;a<3u;a++) for(unsigned timer=0u;timer<2u;timer++) {
        skies_lifecycle_setup();
        uint32_t cell=timer?input.layout.timer2_callback:input.layout.timer1_callback;
        for(unsigned round=0u;round<8u;round++) {
            skies_poll(round&1u?1u:0u,round&2u?15u:0u,round&1u,
                cell,aliases[a]|UINT32_C(0x0c087498));
            skies_virtual_transfer(1u,0u,0u,NULL);
            skies_virtual_transfer(11u,43u,0u,NULL);
            CHECK(value_at(rx_at(2u))==UINT32_C(0x82020008));
            put(cell,4u,0u);
        }
        skies_save_read_reopen(0x63u+a*2u+timer);
    }
    skies_bad_callback_preserves_physical_rx();
    printf("Skies uniform lifecycle: lease=%u mode=%u polls=%u callbacks=%u durable_roundtrips=%u digest=%08x checks=%u\n",
        (unsigned)KUI_VMU_NATIVE_PASSIVE_SPAN,(unsigned)KUI_NATIVE_VMU_ONLY_FULL_SUBMIT,polling_pairs,registered_callbacks,
        storage_roundtrips,output_digest,checks);
    return 0;
}
