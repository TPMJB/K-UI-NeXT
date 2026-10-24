/* SPDX-License-Identifier: GPL-3.0-only */
/* The actual persistent worker uses a relocated host fixture at the real
 * top-of-RAM payload predicted for SA2/Skies. Original allocator aliases stay
 * raw; only DMA, self-location and protected physical spans are canonical. */
#ifndef KUI_VMU_NATIVE_TEST_SELF_ADDRESS
#define KUI_VMU_NATIVE_TEST_SELF_ADDRESS UINT32_C(0xacfd0000)
#endif
#define main native_persistent_reference_main
#include "test_vmu_native_persistent.c"
#undef main

static struct kui_vmu_native_heap record_heap;
static uint32_t record_begin,record_heap_bytes;
static const uint32_t aliases[3]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
static uint32_t little32(const uint8_t *bytes) {
    return (uint32_t)bytes[0]|(uint32_t)bytes[1]<<8|
        (uint32_t)bytes[2]<<16|(uint32_t)bytes[3]<<24;
}
static void load_record(const char *path) {
    uint8_t wire[1024],digest[32];FILE *file=fopen(path,"rb");CHECK(file);
    CHECK(fread(wire,1,sizeof(wire),file)==sizeof(wire));CHECK(fgetc(file)==EOF);CHECK(!fclose(file));
    CHECK(!memcmp(wire,"KUINVR01",8u) && little32(wire+16u)==2u);
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,wire,992u);
    kui_sha256_digest(&hash,digest);CHECK(!memcmp(digest,wire+992u,sizeof(digest)));
    uint32_t words[18];_Static_assert(sizeof(words)==sizeof(record_heap),"scalar heap record changed");
    for(unsigned i=0;i<18u;i++) words[i]=little32(wire+608u+i*4u);
    memcpy(&record_heap,words,sizeof(record_heap));
    record_begin=little32(wire+692u);record_heap_bytes=little32(wire+696u);
    CHECK(physical(record_begin)+record_heap_bytes-0x30000u==physical(SELF_ADDRESS));
}
static void alias_worker(unsigned alias,unsigned runtime_alias) {
    CHECK(alias<3u && runtime_alias<3u);
    fixture();
    /* Keep the independent synthetic SDK/DMA fixture below the smallest
     * actual arena under test. Grandia's arena begins at 4 MiB; the original
     * toy TX/RX spans at 4/5 MiB would invent an invalid ownership overlap. */
    input.tx_base=input.layout.tx_base=UINT32_C(0xac240000);
    for(unsigned i=0;i<input.layout.inputs;i++) {
        unsigned id=input.layout.indices[i];
        uint32_t first=UINT32_C(0xac250000)+i*UINT32_C(0x820);
        input.layout.input_values[i]=first;
        input.layout.rx_slots[id]=first;input.layout.rx_slots[id+24u]=first+0x400u;
        put(input.layout.input_array+i*4u,4u,first);
        put(UINT32_C(0x0c300fe8)+id*4u,4u,first);
        put(UINT32_C(0x0c300fe8)+(id+24u)*4u,4u,first+0x400u);
    }
    put(UINT32_C(0x0c300fc4),4u,input.tx_base);
    put(UINT32_C(0x0c3010a8),4u,input.tx_base);
    put(UINT32_C(0x0c3010ac),4u,input.tx_base+0x3000u);
    put(UINT32_C(0x0c3010b0),4u,input.tx_base);
    put(UINT32_C(0xa05f6c04),4u,physical(input.tx_base));
    /* Model exactly the family initializer's fresh live ownership state,
     * then pass through production fresh/pre/post allocation validation. The
     * independent SH execution test establishes these real-image writes. */
    struct kui_vmu_native_heap *h=&record_heap;
    uint32_t begin=physical(record_begin)|aliases[alias];
    put(h->begin_cell,4u,begin);put(h->begin_copy_cell,4u,begin);
    put(h->bytes_cell,4u,record_heap_bytes);
    put(h->dispatch_cell,4u,h->dispatch_table);
    put(h->dispatch_table,4u,h->allocate);put(h->dispatch_table+4u,4u,h->release);
    put(h->sentinel+4u,4u,0u);put(h->sentinel,4u,begin);put(h->cursor_cell,4u,h->sentinel);
    put(begin,4u,h->sentinel);put(begin+4u,4u,record_heap_bytes/32u);
    struct kui_vmu_native_heap_lease lease;
    CHECK(kui_vmu_native_heap_fresh(h,read_heap,NULL,begin,record_heap_bytes,1u,0x30000u,&lease));
    CHECK(kui_vmu_native_heap_preallocate(&lease,read_heap,NULL,1u));
    put(begin+4u,4u,lease.remaining);put(lease.header+4u,4u,lease.units);
    CHECK(kui_vmu_native_heap_postallocate(&lease,read_heap,NULL,1u,lease.payload));
    CHECK(kui_vmu_native_heap_lease_valid(&lease,read_heap,NULL,1u));
    CHECK((lease.payload&0xe0000000u)==aliases[alias]);
    input.lease=lease;input.runtime.lease_address=physical(lease.payload)|aliases[runtime_alias];
    kui_vmu_native_readonly_release_resume=h->release+20u;
    prepare_handoff();model_card.bus.transfer_block=block_transfer;
    lease_calls=block_callbacks=0u;corrupt_before_loan=corrupt_after_loan=0u;
    release_saw_unpublished=false;arm();install_hooks();
    CHECK(!memcmp(&config.lease,&lease,sizeof(lease)));
    CHECK(issued_physical()==physical(SELF_ADDRESS)+0x18000u);
}
static void raw_ownership_is_not_canonicalized(void) {
    for(unsigned alias=0;alias<3u;alias++) {
        model_reset();alias_worker(alias,1u);
        struct kui_vmu_native_heap_lease original=input.lease;
        for(unsigned field=0;field<3u;field++) for(unsigned wrong=0;wrong<3u;wrong++) {
            if(wrong==alias) continue;
            struct kui_vmu_native_passive_config bad=input;
            uint32_t *value=field==0u?&bad.lease.begin:field==1u?&bad.lease.header:&bad.lease.payload;
            *value=physical(*value)|aliases[wrong];state.armed=0u;
            CHECK(kui_vmu_native_passive_arm(&bad,NULL)<0 && !state.armed);
            CHECK(!memcmp(&input.lease,&original,sizeof(original)));
        }
        /* Changing an owner's live begin alias must still break identity;
         * matching physical addresses do not authorize a different heap. */
        state.armed=0u;put(record_heap.begin_cell,4u,physical(input.lease.begin)|aliases[(alias+1u)%3u]);
        CHECK(kui_vmu_native_passive_arm(&input,NULL)<0 && !state.armed);
    }
}
static void invalid_runtime_and_self_spans(void) {
    for(unsigned alias=0;alias<3u;alias++) for(unsigned corruption=0;corruption<7u;corruption++) {
        model_reset();alias_worker(alias,0u);state.armed=0u;
        if(corruption==0u) input.lease.payload|=0x40000000u;
        if(corruption==1u) input.runtime.lease_address|=0x40000000u;
        if(corruption==2u) input.runtime.lease_address+=32u;
        if(corruption==3u) input.lease.payload+=32u;
        if(corruption==4u) input.lease.lease_bytes+=32u;
        if(corruption==5u) live_sp=STACK_HIGH;
        if(corruption==6u) bad_guard=1u;
        CHECK(kui_vmu_native_passive_arm(&input,NULL)<0 && !state.armed);
        CHECK(!lease_calls && !sd.bytes && !sd.writes);
    }
}
static void physical_rx_excludes_every_owner_alias(void) {
    for(unsigned alias=0;alias<3u;alias++) {
        model_reset();alias_worker(alias,1u);stage_batch(1u,0u);
        state.source=physical(input.tx_base);
        uint32_t saved=config.runtime.rx_slots[1];
        const uint32_t at[]={physical(input.lease.header),physical(input.lease.payload),
            physical(input.lease.payload)+WORKER_SPAN-32u,
            physical(input.lease.payload)+input.lease.lease_bytes-32u};
        for(unsigned i=0;i<sizeof(at)/sizeof(at[0]);i++) {
            config.runtime.rx_slots[1]=at[i]|0xa0000000u;
            put(state.source+20u,4u,at[i]);
            CHECK(!chain(0u,false));
            CHECK(!lease_calls && !sd.bytes && !state.pending);
        }
        config.runtime.rx_slots[1]=saved;
    }
}
static void release_returns(const struct kui_vmu_native_passive_frame *release_frame) {
    if(setjmp(terminal)==0) kui_vmu_native_readonly_release_check(release_frame);
    else CHECK(false);
}
static void unrelated_release_keeps_raw_alias(void) {
    for(unsigned alias=0;alias<3u;alias++) {
        model_reset();alias_worker(alias,1u);
        uint32_t header=input.lease.header-64u,payload=header+32u;
        put(input.lease.begin+4u,4u,input.lease.remaining-2u);put(header+4u,4u,2u);
        CHECK(lease_valid());
        struct kui_vmu_native_passive_frame release_frame=frame;release_frame.r4=payload;
        release_returns(&release_frame);
        CHECK(kui_vmu_native_lifetime_release_safe_ranges(&config.lease,read_heap,NULL,1u,payload,
            protected_ranges,protected_count));
        for(unsigned candidate=0;candidate<3u;candidate++) {
            uint32_t pointer=physical(payload)|aliases[candidate];
            CHECK(kui_vmu_native_lifetime_release_safe_ranges(&config.lease,read_heap,NULL,1u,pointer,
                protected_ranges,protected_count)==(candidate==alias));
            CHECK(!kui_vmu_native_lifetime_release_safe_ranges(&config.lease,read_heap,NULL,1u,
                physical(input.lease.payload)|aliases[candidate],protected_ranges,protected_count));
        }
    }
}
static void save_read_and_reopen(unsigned alias,unsigned runtime_alias) {
    uint8_t next[512],old[512];memset(next,0x40u+alias*3u+runtime_alias,sizeof(next));
    model_reset();alias_worker(alias,runtime_alias);
    memcpy(old,sd.media[image_lba(42u)],sizeof(old));
    transfer_batch(1u,0u,0u,NULL);transfer_batch(2u,0u,0u,NULL);transfer_batch(10u,0u,0u,NULL);
    CHECK(overlay.stats.devinfo && overlay.stats.allinfo && overlay.stats.media && !lease_calls);
    read_compare(42u,old);stage_block(42u,next);transfer_batch(13u,42u,4u,NULL);
    CHECK(value_at(rx_at(1))==0x00010007u && durable_card.commits==1u && release_saw_unpublished);
    read_compare(42u,next);CHECK(installed_hooks_valid() && lease_valid());idle();
    CHECK((config.lease.begin&0xe0000000u)==aliases[alias] &&
        (config.lease.payload&0xe0000000u)==aliases[alias]);
    /* Fresh management recovery, then a new aliased worker/lease bind. */
    model_reopen();struct kui_vmu_backing backing={0};struct kui_vmu_store store={0};
    CHECK(!kui_vmu_backing_init(&backing,&model_bus,&backing_config));
    struct kui_vmu_store_io io=kui_vmu_backing_io(&backing);
    CHECK(!kui_vmu_store_open(&store,&io,&store_config));CHECK(!kui_vmu_store_checkpoint(&store));
    alias_worker(alias,runtime_alias);read_compare(42u,next);idle();
    CHECK(!durable_card.commits && durable_card.reads==1u);
}
int main(int argc,char **argv) {
    CHECK(argc>=2);
    for(int record=1;record<argc;record++) {
        load_record(argv[record]);
        for(unsigned alias=0;alias<3u;alias++) for(unsigned runtime_alias=0;runtime_alias<3u;runtime_alias++)
            save_read_and_reopen(alias,runtime_alias);
        raw_ownership_is_not_canonicalized();invalid_runtime_and_self_spans();
        physical_rx_excludes_every_owner_alias();unrelated_release_keeps_raw_alias();
        printf("Aliased native lease record verified: %s at %08x\n",argv[record],physical(SELF_ADDRESS));
    }
    printf("Native worker lease aliases: %u checks passed; raw ownership retained, physical DMA/exclusion, save/reopen\n",checks);
    return 0;
}
