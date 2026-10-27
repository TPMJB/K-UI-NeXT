/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_maple_observe.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Independent scalar fixtures. P1 SDK state and P2 DMA bytes deliberately
 * occupy different callback ranges, so a wrong alias cannot silently pass. */
#define MANAGER UINT32_C(0x8c290340)
#define MANAGER_POINTER UINT32_C(0x8c29143c)
#define LOCK_POINTER UINT32_C(0x8c11cd24)
#define LOCK UINT32_C(0xac11cd28)
#define GUN UINT32_C(0x8c25972c)
#define RX UINT32_C(0xac1bd920)
#define TX UINT32_C(0xac1c9940)
#define PHYSICAL UINT32_C(0x1fffffff)

static uint32_t sdk[0x10f8u/4u],tx[2][0x3000u/4u],responses[48];
static uint32_t manager_pointer,lock_pointer,lock_byte,gun,source;
static uint32_t trigger,enable,busy;
static unsigned reads,manager_reads,table_reads,rx_reads,lock_reads,stops,bank,chain_words;
static unsigned private_payload_begin,private_payload_end;

uint32_t kui_vmu_maple_observer_test_read(uint32_t address) {
    ++reads;
    switch(address) {
    case 0xa05f6c04u:return source;
    case 0xa05f6c10u:return trigger;
    case 0xa05f6c14u:return enable;
    case 0xa05f6c18u:return busy;
    case MANAGER_POINTER:return manager_pointer;
    case LOCK_POINTER:return lock_pointer;
    case LOCK:++lock_reads;return lock_byte;
    case GUN:return gun;
    default:break;
    }
    if(address>=MANAGER && address<MANAGER+sizeof(sdk)) {
        assert(!(address&3u));
        ++manager_reads;
        if(address>=MANAGER+0xfe8u && address<MANAGER+0x10a8u) ++table_reads;
        return sdk[(address-MANAGER)/4u];
    }
    if(address>=TX && address<TX+sizeof(tx)) {
        unsigned b=(address-TX)/0x3000u,word=((address-TX)%0x3000u)/4u;
        assert(!(address&3u));
        assert(b<2u && word<chain_words); /* No walk beyond the captured chain. */
        assert(word<private_payload_begin || word>=private_payload_end);
        return tx[b][word];
    }
    if(address>=RX && address<RX+0xc000u) {
        assert((address-RX)%0x400u==0u); /* Response headers only; no RX payload. */
        ++rx_reads;
        return responses[(address-RX)/0x400u];
    }
    /* Includes bad manager/lock pointer dereferences and uncached SDK state. */
    fprintf(stderr,"Unexpected Silver host read: %08x\n",address);
    assert(!"observer read outside the admitted scalar fixture");
    return 0;
}

void kui_vmu_maple_observer_test_stop(void) {++stops;}

static void initialize(unsigned selected_bank,unsigned stop_after) {
    struct kui_vmu_maple_observe_config config={
        KUI_VMU_MAPLE_OBSERVE_MAGIC,KUI_VMU_MAPLE_OBSERVE_VERSION,
        sizeof(config),stop_after,stop_after,{0},0,0,0
    };
    memset(sdk,0,sizeof(sdk));memset(tx,0,sizeof(tx));
    bank=selected_bank;chain_words=0u;
    private_payload_begin=private_payload_end=0u;
    manager_pointer=MANAGER;lock_pointer=LOCK;lock_byte=0x80u;gun=0u;
    source=(TX+bank*0x3000u)&PHYSICAL;trigger=busy=0u;enable=1u;
    sdk[0xfc0u/4u]=1u;sdk[0xfc4u/4u]=TX;sdk[0xfc8u/4u]=RX;
    sdk[0xfccu/4u]=TX+bank*0x3000u;
    sdk[0x10a8u/4u]=TX;sdk[0x10acu/4u]=TX+0x3000u;
    sdk[0x10b0u/4u]=TX+bank*0x3000u;
    sdk[0x10b4u/4u]=sdk[0x10b8u/4u]=bank;
    sdk[0x10f4u/4u]=0xa05f6c00u;
    for(unsigned i=0;i<48u;i++) {
        sdk[0xfe8u/4u+i]=RX+i*0x400u;
        responses[i]=0x1c000005u+(i<<16);
    }
    reads=manager_reads=table_reads=rx_reads=lock_reads=stops=0u;
    assert(kui_vmu_maple_observer_init(&config)==0);
}

static void append(unsigned port,unsigned ordinal,unsigned command,unsigned words,int last) {
    static const unsigned addresses[6]={0x20u,1u,2u,4u,8u,16u};
    uint32_t *p=&tx[bank][chain_words];
    assert(port<4u && ordinal<6u && words<=2u);
    p[0]=(last?0x80000000u:0u)|(port<<16)|words;
    p[1]=(RX+bank*0x6000u+(port*6u+ordinal)*0x400u)&PHYSICAL;
    p[2]=(words<<24)|(port<<22)|(((port<<6)|addresses[ordinal])<<8)|command;
    if(words) p[3]=0x02000000u;
    if(words>1u) p[4]=0x05000000u;
    chain_words+=3u+words;
}

static void submit(void) {kui_vmu_maple_observer_visit(0x600000f1u);}
static void completed_context(void) {
    sdk[0xfc0u/4u]=0u;
    sdk[0x10b4u/4u]=sdk[0x10b8u/4u]=bank^1u;
}
static void complete(void) {
    kui_vmu_maple_observer_complete(0x600000f1u,0x8c12b7a6u);
}

static void both_banks_and_all_decoded_slots(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    assert(KUI_VMU_MAPLE_PROFILE==2 && KUI_VMU_READONLY_PROOF==0);
    assert(KUI_VMU_MAPLE_OBSERVE_COMPLETION_PR==0x8c12b7a6u);
    assert(KUI_VMU_OBS_MANAGER_POINTER==MANAGER_POINTER && KUI_VMU_OBS_MANAGER==MANAGER);
    assert(KUI_VMU_OBS_RX_BASE==RX && KUI_VMU_OBS_TX_BASE==TX);
    for(unsigned b=0;b<2u;b++) {
        initialize(b,100u);
        for(unsigned slot=0;slot<24u;slot++) append(slot/6u,slot%6u,1u,0u,slot==23u);
        submit();
        assert(!stops && s->descriptors==24u && s->completion_pending==24u);
        assert(table_reads>=48u && rx_reads==0u && s->proof_mode==0u);
        for(unsigned p=0;p<4u;p++) assert(s->ports[p]==6u);
        for(unsigned u=0;u<6u;u++) assert(s->units[u]==4u);
        const unsigned checked=table_reads;
        completed_context();complete();
        assert(!stops && s->completion_matched==1u && !s->completion_pending);
        assert(!s->completion_guard_flags && s->completion_rx_headers==24u && rx_reads==24u);
        assert(table_reads>=checked+48u && lock_reads>=2u);
        assert(s->main_a_rx==((RX+b*0x6000u)&PHYSICAL));
        assert(s->main_a_response==responses[b*24u]);
        assert(s->subunit_rx==((RX+b*0x6000u+23u*0x400u)&PHYSICAL));
        assert(s->subunit_response==responses[b*24u+23u]);
    }
}

static void reject_each_live_table_entry_at_both_boundaries(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    for(unsigned i=0;i<48u;i++) {
        initialize(i&1u,100u);append(0u,1u,1u,0u,1);
        sdk[0xfe8u/4u+i]^=4u;submit();
        assert(stops==1u && s->stop_reason==KUI_VMU_OBSERVE_UNSAFE);
        assert(s->proof_bank_fail==1u && (s->completion_guard_flags&0x100u));
        assert(!s->completion_pending && !rx_reads);

        initialize(i&1u,100u);append(0u,1u,1u,0u,1);submit();
        assert(!stops && s->completion_pending==1u);
        completed_context();sdk[0xfe8u/4u+i]^=4u;complete();
        assert(!s->completion_matched && s->completion_pending==1u && !rx_reads);
        assert(s->completion_guard_flags==0x100u && s->proof_bank_fail==1u);
        assert(s->completion_bad_context==1u);
    }
}

enum fault {BAD_MANAGER,BAD_LOCK_POINTER,BAD_LOCK,GUN_ACTIVE,BAD_TX_BASE,BAD_RX_BASE,
    BAD_TX_ZERO,BAD_TX_ONE,BAD_PENDING_TX,BAD_RX_SELECTOR,BAD_TX_SELECTOR,BAD_ENDIAN,
    BAD_READY,BAD_MAPLE,BAD_TRIGGER,BAD_ENABLE,BAD_BUSY,FAULT_COUNT};

static void corrupt(enum fault f,unsigned completed) {
    switch(f) {
    case BAD_MANAGER:manager_pointer=0xdeadc0deu;break;
    case BAD_LOCK_POINTER:lock_pointer=0xdeadc0deu;break;
    case BAD_LOCK:lock_byte=0u;break;
    case GUN_ACTIVE:gun=1u;break;
    case BAD_TX_BASE:sdk[0xfc4u/4u]+=32u;break;
    case BAD_RX_BASE:sdk[0xfc8u/4u]+=32u;break;
    case BAD_TX_ZERO:sdk[0x10a8u/4u]+=32u;break;
    case BAD_TX_ONE:sdk[0x10acu/4u]+=32u;break;
    case BAD_PENDING_TX:sdk[0x10b0u/4u]+=32u;break;
    case BAD_RX_SELECTOR:sdk[0x10b4u/4u]=2u;break;
    case BAD_TX_SELECTOR:sdk[0x10b8u/4u]^=1u;break;
    case BAD_ENDIAN:sdk[0x10c4u/4u]=1u;break;
    case BAD_READY:sdk[0xfc0u/4u]=completed?1u:0u;break;
    case BAD_MAPLE:sdk[0x10f4u/4u]=0xdeadc0deu;break;
    case BAD_TRIGGER:trigger=1u;break;
    case BAD_ENABLE:enable=0u;break;
    case BAD_BUSY:busy=1u;break;
    default:assert(!"unhandled context fault");
    }
}

static void context_failures_never_read_rx(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    for(enum fault f=BAD_MANAGER;f<FAULT_COUNT;f++) {
        initialize(0u,100u);append(0u,1u,1u,0u,1);corrupt(f,0u);submit();
        assert(stops==1u && !s->completion_pending && !rx_reads);
        assert(s->stop_reason==KUI_VMU_OBSERVE_UNSAFE);
        if(f==BAD_MANAGER || f==BAD_LOCK_POINTER) assert(!lock_reads);

        initialize(1u,100u);append(0u,1u,1u,0u,1);submit();
        assert(s->completion_pending==1u && !stops);
        completed_context();corrupt(f,1u);complete();
        assert(!rx_reads && !s->completion_matched && s->completion_pending==1u);
        assert(s->completion_guard_flags!=0u);
    }
    /* A physical alias of the object/lock is not the admitted pointer value. */
    initialize(0u,100u);append(0u,0u,1u,0u,1);manager_pointer=0xac290340u;submit();
    assert(stops==1u && !rx_reads);
    /* An aligned, in-RAM P1 pointer still is not the exact manager owner.
     * The callback also rejects every attempted read through this pointer. */
    initialize(0u,100u);append(0u,0u,1u,0u,1);manager_pointer=0x8c29f000u;submit();
    assert(stops==1u && !manager_reads && !rx_reads && !s->completion_pending);
    initialize(0u,100u);append(0u,0u,1u,0u,1);lock_pointer=0x8c11cd28u;submit();
    assert(stops==1u && !rx_reads && !lock_reads);
}

static void descriptor_identity_and_source_are_exact(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    const uint32_t bad_rx[]={0x0c1bd920u,0x0c1c3d20u,0xac1bdd20u,
        0x0c1bdd21u,0x0cfd0000u};
    for(unsigned i=0;i<sizeof(bad_rx)/sizeof(bad_rx[0]);i++) {
        initialize(0u,100u);append(0u,1u,11u,2u,1);tx[0][1]=bad_rx[i];submit();
        assert(stops==1u && s->rx_bad==1u && !s->completion_pending && !rx_reads);
    }
    const uint32_t bad_source[]={0x0c1c9960u,0xac1c9940u,0x0c100000u,0x0d000000u};
    for(unsigned i=0;i<sizeof(bad_source)/sizeof(bad_source[0]);i++) {
        initialize(0u,100u);append(0u,0u,1u,0u,1);source=bad_source[i];submit();
        assert(stops==1u && !s->completion_pending && !rx_reads);
    }
    initialize(0u,100u);append(0u,1u,1u,0u,1);submit();completed_context();
    source=(TX+0x3000u)&PHYSICAL;complete();
    assert(!rx_reads && !s->completion_matched && (s->completion_guard_flags&2u));
    /* A later chain edit must invalidate the captured schedule before RX. */
    initialize(0u,100u);append(0u,1u,11u,2u,1);submit();completed_context();
    tx[0][2]^=1u;complete();
    assert(!rx_reads && !s->completion_matched && (s->completion_guard_flags&0x100u));
}

static void lower_masks_and_wrong_return_skip_headers(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    for(unsigned mask=0u;mask<15u;mask++) {
        initialize(mask&1u,100u);append(0u,1u,1u,0u,1);submit();completed_context();
        kui_vmu_maple_observer_complete(0x60000001u|(mask<<4),0x8c12b7a6u);
        assert(!rx_reads && !s->completion_matched && s->completion_pending==1u);
        assert(s->completion_bad_mask==1u && s->completion_guard_flags==8u);
        assert(s->completion_masks==(1u<<mask));
        complete();
        assert(rx_reads==1u && s->completion_matched==1u && !s->completion_pending);
    }
    initialize(0u,100u);append(0u,1u,1u,0u,1);submit();completed_context();
    kui_vmu_maple_observer_complete(0x600000f1u,0x8c0b5362u); /* Toy PR. */
    assert(!rx_reads && !s->completion_matched && s->completion_guard_flags==1u);
    initialize(0u,100u);completed_context();complete();
    assert(s->completion_orphan==1u && !rx_reads);
}

static void completion_rechecks_metadata_and_padding(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    for(unsigned fault=0;fault<5u;fault++) {
        initialize(0u,100u);append(0u,0u,1u,0u,0);
        tx[0][chain_words++]=0x00000700u;append(0u,1u,1u,0u,1);
        submit();assert(s->completion_pending==2u);completed_context();
        switch(fault) {
        case 0:tx[0][0]|=0x80000000u;break; /* Premature LAST. */
        case 1:tx[0][1]+=0x400u;break; /* Different response slot. */
        case 2:tx[0][2]^=1u;break; /* Different captured command. */
        case 3:tx[0][3]=0x80000700u;break; /* Unsupported padding form. */
        default:tx[0][4]&=0x7fffffffu;break; /* Missing original LAST. */
        }
        complete();
        assert(!rx_reads && !s->completion_matched && s->completion_pending==2u);
        assert(s->completion_guard_flags==0x100u && s->proof_bank_fail==1u);
    }
}

static void padding_and_passive_header_only_completion(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    initialize(0u,100u);append(0u,0u,1u,0u,0);
    for(unsigned i=0;i<5u;i++) tx[0][chain_words++]=0x00000700u;
    append(0u,1u,11u,2u,1);responses[1]=0x82010008u;
    submit();
    assert(!stops && s->nop_words==5u && s->descriptors==2u && s->completion_pending==2u);
    assert(s->first[0].payload0==0x02000000u && s->first[0].payload1==0x05000000u);
    assert(!rx_reads);completed_context();complete();
    assert(rx_reads==2u && s->completion_rx_headers==2u && s->subunit_response==0x82010008u);
    assert(responses[1]==0x82010008u && !s->proof_replies && !s->proof_prepared);
    initialize(1u,2u);append(0u,0u,1u,0u,1);submit();completed_context();complete();
    sdk[0xfc0u/4u]=1u;sdk[0x10b4u/4u]=sdk[0x10b8u/4u]=bank;
    submit();assert(stops==1u && s->starts==2u && !s->completion_pending);
    const unsigned stopped_reads=reads;complete();submit();
    assert(reads==stopped_reads && s->completion_calls==1u && rx_reads==1u);
}

static void save_payload_is_never_read(void) {
    const struct kui_vmu_maple_observe_snapshot *s=&kui_vmu_maple_observer_snapshot;
    initialize(0u,100u);
    tx[0][0]=0x80000022u;tx[0][1]=0x0c1bdd20u;tx[0][2]=0x2200010cu;
    tx[0][3]=0x02000000u;tx[0][4]=0x05000000u;
    for(unsigned i=5u;i<37u;i++) tx[0][i]=0xfeed0000u+i;
    chain_words=37u;private_payload_begin=5u;private_payload_end=37u;
    submit();assert(!stops && s->completion_pending==1u);
    assert(s->first[0].payload0==0x02000000u && s->first[0].payload1==0x05000000u);
    tx[0][10]^=1u; /* Private data is neither fingerprinted nor copied. */
    completed_context();complete();
    assert(rx_reads==1u && s->completion_matched==1u && !s->completion_guard_flags);
}

int main(void) {
    assert(KUI_VMU_MAPLE_OBSERVE_DEFAULT_STOP==3600u);
    assert(KUI_VMU_MAPLE_OBSERVE_DEFAULT_POST==600u);
    both_banks_and_all_decoded_slots();
    reject_each_live_table_entry_at_both_boundaries();
    context_failures_never_read_rx();descriptor_identity_and_source_are_exact();
    lower_masks_and_wrong_return_skip_headers();completion_rechecks_metadata_and_padding();
    padding_and_passive_header_only_completion();
    save_payload_is_never_read();
    puts("Silver passive observer live ownership and completion guards: passed");
    return 0;
}
