/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_maple_observe.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t ram[1024], source;
static unsigned reads, stops;
static unsigned metadata_reads;
static int metadata_only;
static uint32_t responses[32u*256u], dma_busy;
static unsigned response_reads;
static uint32_t sdk_ready,sdk_lock,sdk_manager,sdk_lock_pointer,sdk_maple;
static uint32_t dma_trigger,dma_enable;

uint32_t kui_vmu_maple_observer_test_read(uint32_t address) {
    ++reads;
    if(address == UINT32_C(0xa05f6c04))
        return source;
    if(address == UINT32_C(0xa05f6c10))
        return dma_trigger;
    if(address == UINT32_C(0xa05f6c18)) return dma_busy;
    if(address == UINT32_C(0xa05f6c14))
        return dma_enable;
    if(address == UINT32_C(0x8c1223a4))
        return sdk_manager;
    if(address == UINT32_C(0x8c12239c)) return sdk_maple;
    if(address == UINT32_C(0x8c122268)) return sdk_ready;
    if(address == UINT32_C(0x8c0c0964)) return sdk_lock_pointer;
    if(address == UINT32_C(0xac0c0968)) return sdk_lock;
    if(address>=0xac200000u && address<0xac200000u+sizeof(responses)) {
        unsigned index=(address-0xac200000u)/4u;
        assert(!(address&3u) && !(index%256u)); /* Headers only, no response payload. */
        ++response_reads;return responses[index];
    }
    if(address == UINT32_C(0xacfcffc0))
        return UINT32_C(0x80000008); /* Payload would cross the allocator header. */
    assert(address >= UINT32_C(0xac100000));
    assert(!(address & 3u));
    assert(address < UINT32_C(0xac100000) + sizeof(ram));
    unsigned index=(address-UINT32_C(0xac100000))/4u;
    if(metadata_only && index>=3u) {
        assert(index<5u); /* Never read the BLOCKWRITE save-data words. */
        ++metadata_reads;
    }
    return ram[index];
}

void kui_vmu_maple_observer_test_stop(void) {
    ++stops;
}

static void initialize_window(unsigned stop_after,unsigned post_after) {
    struct kui_vmu_maple_observe_config config = {
        KUI_VMU_MAPLE_OBSERVE_MAGIC, KUI_VMU_MAPLE_OBSERVE_VERSION,
        sizeof(config), stop_after, post_after, {0}
    };
    source = UINT32_C(0x0c100000);
    reads = stops = 0;
    metadata_reads=0;metadata_only=0;
    response_reads=0;dma_busy=0;sdk_ready=0;sdk_lock=0x80u;
    dma_trigger=0;dma_enable=1;sdk_manager=0x8c1212a8u;
    sdk_lock_pointer=0xac0c0968u;sdk_maple=0xa05f6c00u;
    memset(responses,0,sizeof(responses));
    memset(ram, 0, sizeof(ram));
    assert(kui_vmu_maple_observer_init(&config) == 0);
}
static void initialize(unsigned stop_after) {initialize_window(stop_after,stop_after);}

static void mixed_owned_observation(void) {
    const struct kui_vmu_maple_observe_snapshot *s = &kui_vmu_maple_observer_snapshot;
    initialize(2u);
    ram[0] = 0;
    ram[1] = UINT32_C(0x0c200000);
    ram[2] = UINT32_C(0x00002001); /* A controller DEVINFO. */
    ram[3] = UINT32_C(0x80000002);
    ram[4] = UINT32_C(0x0c200400);
    ram[5] = UINT32_C(0x0200020b); /* A2 card block read. */
    ram[6] = UINT32_C(0x02000000);
    ram[7] = UINT32_C(0x05000000);
    kui_vmu_maple_observer_visit(0x400000f1u);
    assert(s->starts == 1u && s->descriptors == 2u);
    assert(s->ports[0] == 2u && s->units[0] == 1u && s->units[2] == 1u);
    assert(s->commands[1] == 1u && s->commands[11] == 1u);
    assert(s->controller_seen == 1u && s->rx_bad == 0u);
    assert(s->source_min == source && s->source_max == source);
    assert(s->source_samples[0] == source && s->descriptors_max == 2u);
    assert(reads == 14u && stops == 0u); /* Only two metadata words; never RX/save data. */
    assert(s->first_subunit_start==1u && s->first_card_start==1u);
    assert(s->subunit_requests==1u && s->first[0].ordinal==2u);
    assert(s->first[0].payload0==0x02000000u && s->first[0].payload1==0x05000000u);
    assert(s->entry_sr_last == 0x400000f1u && s->entry_masks == (1u << 15));
    assert(s->manager_last == 0xa05f6c00u && !s->manager_other);
    kui_vmu_maple_observer_visit(0x40000000u);
    assert(s->starts == 2u && stops == 1u);
    assert(s->initialized == 2u);
    kui_vmu_maple_observer_visit(0u);
    assert(s->starts == 2u && stops == 1u && reads == 28u);
    assert(s->stop_reason==KUI_VMU_OBSERVE_HARD_LIMIT);
}

static void invalid_sources_and_patterns(void) {
    const struct kui_vmu_maple_observe_snapshot *s = &kui_vmu_maple_observer_snapshot;
    initialize(10u);
    source = UINT32_C(0x0d000000); /* Never dereferenced. */
    kui_vmu_maple_observer_visit(0u);
    assert(s->malformed == 1u && reads == 6u && stops == 1u);
    initialize(10u);
    source = UINT32_C(0x0c100001); /* Never dereferenced. */
    kui_vmu_maple_observer_visit(0u);
    assert(s->malformed == 1u && reads == 6u && stops == 1u);
    initialize(10u);
    source = UINT32_C(0x0c100000);
    ram[0] = UINT32_C(0x80000200); /* Unsupported gun descriptor. */
    kui_vmu_maple_observer_visit(0u);
    assert(s->unsupported == 1u && s->incomplete == 1u && reads == 7u && stops == 1u);
    initialize(10u);
    ram[0] = UINT32_C(0x80000000);
    ram[1] = UINT32_C(0xac200000); /* CPU alias is not a physical RX address. */
    ram[2] = UINT32_C(0x00002001);
    kui_vmu_maple_observer_visit(0u);
    assert(s->descriptors == 1u && s->rx_bad == 1u && stops == 1u);
    initialize(10u);
    ram[0] = UINT32_C(0x80000000);
    ram[1] = UINT32_C(0x0c200000);
    ram[2] = UINT32_C(0x01002001); /* Inconsistent packet length. */
    kui_vmu_maple_observer_visit(0u);
    assert(s->malformed == 1u && s->descriptors == 0u && stops == 1u);
}

static void maximum_bounded_walk(void) {
    const struct kui_vmu_maple_observe_snapshot *s = &kui_vmu_maple_observer_snapshot;
    unsigned i;
    initialize(10u);
    for(i = 0; i < 33u; ++i) {
        ram[i * 3u] = i == 32u ? UINT32_C(0x80000000) : 0u;
        ram[i * 3u + 1u] = UINT32_C(0x0c200000) + i * 1024u;
        ram[i * 3u + 2u] = UINT32_C(0x00002001);
    }
    kui_vmu_maple_observer_visit(0u);
    assert(s->descriptors == 32u && s->descriptors_max == 32u);
    assert(s->incomplete == 1u && reads == 102u && stops == 1u);
}

static void protects_live_lease_before_dma(void) {
    const struct kui_vmu_maple_observe_snapshot *s = &kui_vmu_maple_observer_snapshot;
    const uint32_t bad_sources[] = {0x0cfcffe0u, 0x0cfd0000u, 0x0cfdffe0u,
        0x0cfe0000u, 0xacfd0000u};
    const uint32_t bad_rx[] = {0x0cfcfc00u, 0x0cfcffe0u, 0x0cfd0000u,
        0x0cfdffe0u, 0xacfd0000u, 0x0cfd0001u};
    for(unsigned i=0;i<sizeof(bad_sources)/sizeof(bad_sources[0]);i++) {
        initialize(100u); source=bad_sources[i];
        kui_vmu_maple_observer_visit(0u);
        assert(stops==1u && s->malformed==1u && s->descriptors==0u && reads==6u);
    }
    initialize(100u);source=0x0cfcffc0u;
    kui_vmu_maple_observer_visit(0u);
    assert(stops==1u && s->malformed==1u && !s->descriptors && reads==7u);
    for(unsigned i=0;i<sizeof(bad_rx)/sizeof(bad_rx[0]);i++) {
        initialize(100u);
        ram[0]=0x80000000u;ram[1]=bad_rx[i];ram[2]=0x00002001u;
        kui_vmu_maple_observer_visit(0u);
        assert(stops==1u && s->rx_bad==1u && s->descriptors==1u);
        assert(s->last_rx==bad_rx[i] && s->initialized==2u);
    }
    /* The exact title already owns fixed high scratch beyond the worker. */
    initialize(100u);
    ram[0]=0x80000000u;ram[1]=0x0cfe0000u;ram[2]=0x00002001u;
    kui_vmu_maple_observer_visit(0u);
    assert(!stops && !s->rx_bad && s->descriptors==1u);
}

static void rejects_invalid_configuration(void) {
    struct kui_vmu_maple_observe_config config = {0};
    assert(kui_vmu_maple_observer_init(NULL) == -1);
    assert(kui_vmu_maple_observer_init(&config) == -1);
    config.magic = KUI_VMU_MAPLE_OBSERVE_MAGIC;
    config.version = KUI_VMU_MAPLE_OBSERVE_VERSION;
    config.bytes = sizeof(config);
    assert(kui_vmu_maple_observer_init(&config) == -1);
    config.stop_after = UINT32_C(1000001);
    assert(kui_vmu_maple_observer_init(&config) == -1);
    config.stop_after=18000u;config.post_after=1200u;
    assert(kui_vmu_maple_observer_init(&config)==0);
    config.version=1u;assert(kui_vmu_maple_observer_init(&config)==-1);
    config.version=KUI_VMU_MAPLE_OBSERVE_VERSION;config.bytes=72u;
    assert(kui_vmu_maple_observer_init(&config)==-1);
    config.bytes=sizeof(config);config.stop_after=18001u;
    assert(kui_vmu_maple_observer_init(&config)==-1);
    config.stop_after=18000u;config.post_after=0u;
    assert(kui_vmu_maple_observer_init(&config)==-1);
    config.stop_after=3u;config.post_after=4u;
    assert(kui_vmu_maple_observer_init(&config)==-1);
}

static void packet(unsigned unit,unsigned command,unsigned words,uint32_t function,uint32_t location) {
    ram[0]=0x80000000u|words;ram[1]=0x0c200000u;
    ram[2]=(words<<24)|(unit<<8)|command;
    ram[3]=function;ram[4]=location;
}

static void no_card_hard_bound_and_discovery_only(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    initialize_window(18000u,1200u);packet(0x20u,1u,0u,0,0);
    for(unsigned i=0;i<17999u;i++) kui_vmu_maple_observer_visit(0x60000161u);
    assert(!stops && s->starts==17999u && !s->first_subunit_start && !s->first_card_start);
    kui_vmu_maple_observer_visit(0x60000161u);
    assert(stops==1u && s->starts==18000u && s->stop_reason==KUI_VMU_OBSERVE_HARD_LIMIT);
    assert(!s->subunit_requests && !s->first[0].ordinal && !s->last[0].ordinal);
    initialize_window(20u,3u);packet(1u,1u,0u,0,0);
    for(unsigned i=0;i<19u;i++) kui_vmu_maple_observer_visit(0u);
    assert(!stops && s->first_subunit_start==1u && !s->first_card_start);
    kui_vmu_maple_observer_visit(0u);
    assert(stops==1u && s->subunit_requests==20u && s->stop_reason==KUI_VMU_OBSERVE_HARD_LIMIT);
}

static void late_card_window_and_hard_cap(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    initialize_window(18000u,1200u);packet(0x20u,9u,1u,0x01000000u,0);
    for(unsigned i=0;i<12499u;i++) kui_vmu_maple_observer_visit(0u);
    packet(1u,11u,2u,0x02000000u,0x12340000u);
    kui_vmu_maple_observer_visit(0u);
    assert(s->first_subunit_start==12500u && s->first_card_start==12500u && !stops);
    packet(0x20u,9u,1u,0x01000000u,0);
    for(unsigned i=0;i<1199u;i++) kui_vmu_maple_observer_visit(0u);
    assert(!stops && s->starts==13699u);
    kui_vmu_maple_observer_visit(0u);
    assert(stops==1u && s->starts==13700u && s->stop_reason==KUI_VMU_OBSERVE_CARD_WINDOW);
    initialize_window(10u,4u);packet(0x20u,1u,0u,0,0);
    for(unsigned i=0;i<8u;i++) kui_vmu_maple_observer_visit(0u);
    packet(1u,10u,1u,0x02000000u,0);kui_vmu_maple_observer_visit(0u);
    assert(!stops && s->first_card_start==9u);
    kui_vmu_maple_observer_visit(0u);
    assert(stops==1u && s->starts==10u && s->stop_reason==KUI_VMU_OBSERVE_HARD_LIMIT);
}

static void bounded_metadata_and_exact_trigger(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    const unsigned words[]={0u,1u,2u,34u};
    for(unsigned i=0;i<sizeof(words)/sizeof(words[0]);i++) {
        initialize_window(100u,20u);metadata_only=1;
        packet(1u,12u,words[i],0x02000000u,0x05000000u);
        for(unsigned p=5u;p<37u;p++) ram[p]=0xdecafbad;
        kui_vmu_maple_observer_visit(0u);
        assert(!stops && s->subunit_requests==1u);
        assert(metadata_reads==(words[i]>1u?2u:words[i]));
        assert(s->first[0].payload0==(words[i]?0x02000000u:0u));
        assert(s->first[0].payload1==(words[i]>1u?0x05000000u:0u));
        assert(s->first_card_start==(words[i]?1u:0u));
    }
    initialize_window(100u,20u);packet(1u,9u,1u,0x02000000u,0);
    kui_vmu_maple_observer_visit(0u);assert(!s->first_card_start);
    packet(1u,11u,2u,0x04000000u,0);
    kui_vmu_maple_observer_visit(0u);assert(!s->first_card_start);
    packet(0x20u,11u,2u,0x02000000u,0);
    kui_vmu_maple_observer_visit(0u);assert(!s->first_card_start && s->subunit_requests==2u);
    for(unsigned command=10u;command<=13u;command++) {
        initialize_window(100u,20u);packet(1u,command,2u,0x02000000u,0);
        kui_vmu_maple_observer_visit(0u);assert(s->first_card_start==1u);
    }
}

static void first_last_records_and_unsafe_after_trigger(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    initialize_window(1000u,1000u);
    for(unsigned i=1u;i<=13u;i++) {
        packet(1u,11u,2u,0x02000000u,i);
        kui_vmu_maple_observer_visit(0u);
    }
    assert(!stops && s->subunit_requests==13u);
    for(unsigned i=0;i<4u;i++) {
        assert(s->first[i].ordinal==i+1u && s->first[i].payload1==i+1u);
        assert(s->last[(13u+i)%4u].ordinal==i+10u);
        assert(s->last[(13u+i)%4u].payload1==i+10u);
    }
    packet(1u,11u,2u,0x02000000u,99u);ram[1]=0x0cfd0000u;metadata_only=1;
    kui_vmu_maple_observer_visit(0u);
    assert(stops==1u && s->stop_reason==KUI_VMU_OBSERVE_UNSAFE && s->subunit_requests==13u);
    assert(!metadata_reads && s->last[0].payload1==13u);
}

static void complete(void) {
    kui_vmu_maple_observer_complete(0x600000f1u,KUI_VMU_MAPLE_OBSERVE_COMPLETION_PR);
}

static void completed_headers_and_devinfo_retention(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    initialize_window(100u,20u);
    complete();
    assert(s->completion_calls==1u && s->completion_orphan==1u && !response_reads);
    packet(0x20u,1u,0u,0,0);kui_vmu_maple_observer_visit(0u);
    assert(s->completion_pending==1u && !s->completion_matched && !response_reads);
    responses[0]=0x1c002105u;complete();
    assert(s->completion_matched==1u && s->completion_rx_headers==1u);
    assert(s->main_a_response==0x1c002105u && s->main_a_rx==0x0c200000u);
    assert(!s->completion_pending && !s->completion_guard_flags);
    /* Subsequent main-A GETCOND must retain DEVINFO's subunit presence mask. */
    packet(0x20u,9u,1u,0x01000000u,0);kui_vmu_maple_observer_visit(0u);
    responses[0]=0x03002008u;complete();
    assert(s->main_a_response==0x1c002105u && response_reads==2u);
    packet(1u,11u,2u,0x02000000u,0x05000000u);
    kui_vmu_maple_observer_visit(0u);
    /* The consumer trace uses the captured request, not a later source reread. */
    ram[2]=0x00002001u;responses[0]=0x21000108u;complete();
    assert(s->subunit_request==0x0200010bu && s->subunit_response==0x21000108u);
    assert(s->subunit_rx==0x0c200000u && s->subunit_ordinal==3u);
    assert(s->completion_rx_headers==3u && response_reads==3u && responses[0]==0x21000108u);
    assert(s->completion_masks==(1u<<15) && s->completion_sr_last==0x600000f1u);
}

static void completion_rejects_unowned_or_active_context(void) {
    struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    /* Every rejected condition leaves pending metadata intact and reads no RX. */
    for(unsigned fault=0;fault<12u;fault++) {
        initialize(100u);packet(1u,1u,0u,0,0);kui_vmu_maple_observer_visit(0u);
        switch(fault) {
        case 0: dma_busy=1u;break;
        case 1: source+=32u;break;
        case 2: sdk_manager=0xdeadc0deu;break; /* Never dereference this pointer. */
        case 3: sdk_lock_pointer=0xdeadc0deu;break; /* Nor this byte pointer. */
        case 4: sdk_ready=1u;break;
        case 5: sdk_lock=0u;break;
        case 6: sdk_maple=0xdeadc0deu;break;
        case 7: dma_trigger=1u;break;
        case 8: dma_enable=0u;break;
        case 9: s->completion_pending=33u;break;
        default: break;
        }
        kui_vmu_maple_observer_complete(fault==10u?0x600000e1u:0x600000f1u,
            fault==11u?0x8c0b5364u:KUI_VMU_MAPLE_OBSERVE_COMPLETION_PR);
        assert(!response_reads && !s->completion_matched && s->completion_pending);
        assert(s->completion_guard_flags==(fault==0u?4u:fault==1u?2u:
            fault<7u?16u:fault<9u?32u:fault==9u?64u:fault==10u?8u:1u));
        assert(s->completion_busy==(fault==0u) && s->completion_bad_mask==(fault==10u));
        assert(s->completion_bad_context==(fault!=0u && fault!=10u));
    }
}

static void completion_pending_lifetime_and_bounded_batch(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    initialize(100u);packet(1u,1u,0u,0,0);kui_vmu_maple_observer_visit(0u);
    packet(2u,1u,0u,0,0);kui_vmu_maple_observer_visit(0u);
    assert(s->completion_pending==1u && s->completion_overwritten==1u);
    responses[0]=UINT32_MAX;complete();
    assert(s->completion_matched==1u && s->subunit_request==0x00000201u);
    assert(s->subunit_response==UINT32_MAX && s->subunit_ordinal==2u);
    complete();assert(s->completion_orphan==1u && response_reads==1u);
    initialize(2u);packet(0x20u,1u,0u,0,0);kui_vmu_maple_observer_visit(0u);complete();
    assert(s->completion_matched==1u && !s->completion_pending);
    kui_vmu_maple_observer_visit(0u);complete();
    assert(stops==1u && s->starts==2u && s->completion_calls==1u && response_reads==1u);
    assert(!s->completion_pending); /* The final counted schedule is never submitted. */
    initialize(100u);
    for(unsigned i=0;i<32u;i++) {
        ram[i*3u]=i==31u?0x80000000u:0u;
        ram[i*3u+1u]=0x0c200000u+i*1024u;ram[i*3u+2u]=0x00002001u;
        responses[i*256u]=0x1c002005u+i;
    }
    kui_vmu_maple_observer_visit(0u);complete();
    assert(s->completion_pending==0u && s->completion_matched==1u && response_reads==32u);
    assert(s->completion_rx_headers==32u && !s->completion_guard_flags);
    assert(s->main_a_rx==0x0c207c00u && s->main_a_response==0x1c002024u);
}

int main(void) {
    mixed_owned_observation();
    invalid_sources_and_patterns();
    maximum_bounded_walk();
    protects_live_lease_before_dma();
    rejects_invalid_configuration();
    no_card_hard_bound_and_discovery_only();
    late_card_window_and_hard_cap();
    bounded_metadata_and_exact_trigger();
    first_last_records_and_unsafe_after_trigger();
    completed_headers_and_devinfo_retention();
    completion_rejects_unowned_or_active_context();
    completion_pending_lifetime_and_bounded_batch();
    puts("VMU read-only Maple observer worker tests passed");
    return 0;
}
