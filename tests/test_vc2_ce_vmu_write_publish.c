/* SPDX-License-Identifier: GPL-3.0-only */
/* Real VC2 volatile endpoint plus real bounded publisher. The authored bus
 * and fresh-owner callbacks model publication only, never native CE timing,
 * cache behavior, SD access or durable saving. */
#include "kui/armada_ce_vmu_a2_publish.h"
#include "kui/vc2_ce_vmu_write_probe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct kui_vc2_ce_vmu_write_probe card;
static struct kui_armada_ce_vmu_a2_input input;
static struct kui_armada_ce_vmu_a2_publish_snapshot log_state;
static struct {
    uint32_t before[4];
    struct kui_armada_ce_vmu_a2_publish_scratch work;
    uint32_t after[4];
} protected_scratch;
static struct {
    uint32_t before[4];
    uint8_t bytes[0x2000];
    uint32_t after[4];
} guest;
static struct {
    uint8_t before[0x2000],request[140];
    struct {uint32_t bus,value;} writes[140];
    unsigned stores,backend_calls,admissions,fail_admission,fail_store,reply_mode;
    unsigned backend_done;
} f;
static uint8_t block_data[512];
static unsigned checks;
#define CHECK(condition) do {++checks;assert(condition);} while(0)
#define WORK (&protected_scratch.work)

static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0u;i<4u;++i)p[i]=(uint8_t)(value>>(8u*i));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void reset(void) {
    memset(&card,0,sizeof(card));memset(&input,0,sizeof(input));
    memset(&log_state,0,sizeof(log_state));memset(&f,0,sizeof(f));
    memset(&guest,0xa5,sizeof(guest));memset(&protected_scratch,0x5a,sizeof(protected_scratch));
    memset(WORK,0,sizeof(*WORK));
    CHECK(!kui_vc2_ce_vmu_write_probe_init(&card));
    input.pool_bus=UINT32_C(0x0cfd1000);input.pool_bytes=0x2000u;
    input.tx_bus=UINT32_C(0x0cfd3000);input.tx_bytes=0x1000u;
    input.protected_bus=UINT32_C(0x0cfc4000);input.protected_bytes=0xc000u;
    input.parent.request_header=UINT32_C(0x01002009);
    input.parent.response_header=UINT32_C(0x03310008);
    input.parent.request_function=input.parent.response_function=UINT32_C(0x01000000);
    input.parent.rx_bus=input.pool_bus;input.parent.capacity=16u;
    input.target_present=1u;input.target.rx_bus=input.pool_bus+0x420u;
    for(unsigned i=0u;i<sizeof(block_data);++i)block_data[i]=(uint8_t)((i*13u+7u)^(i>>1));
}
static void begin_call(unsigned command,unsigned phase) {
    memset(&f,0,sizeof(f));
    memset(input.target.request,0,sizeof(input.target.request));
    input.target.request[0]=(uint8_t)command;input.target.request[1]=2u;
    input.target.request[3]=(uint8_t)(command==12u?34u:2u);
    input.target.request[7]=2u;input.target.request[9]=(uint8_t)phase;
    input.target.request[11]=42u;
    input.target.request_bytes=command==12u?140u:12u;
    input.target.capacity=command==12u?4u:8u;
    if(command==12u && phase<4u)memcpy(input.target.request+12u,block_data+phase*128u,128u);
    memcpy(f.request,input.target.request,sizeof(f.request));
    input.target.response_header=UINT32_MAX;
    put32(guest.bytes, input.parent.response_header);
    put32(guest.bytes+0x420u,UINT32_MAX);
    memcpy(f.before,guest.bytes,sizeof(f.before));
}
static bool admit(void *context) {
    CHECK(context==&f && !f.stores);
    ++f.admissions;
    CHECK(f.admissions<=2u);
    CHECK(f.admissions==1u?!f.backend_calls:f.backend_done);
    return f.admissions!=f.fail_admission;
}
static int backend(void *context,const uint8_t *request,size_t bytes,
    uint8_t *reply,size_t capacity,size_t *produced) {
    CHECK(context==&f && f.admissions==1u && !f.stores && !f.backend_calls);
    CHECK(bytes==input.target.request_bytes && !memcmp(request,f.request,bytes));
    CHECK((uintptr_t)request+bytes<=(uintptr_t)reply ||
        (uintptr_t)reply+capacity<=(uintptr_t)request);
    if(bytes==140u) {
        CHECK(request==(const uint8_t *)(const void *)WORK->reply+384u);
        CHECK(capacity==384u);
    } else CHECK(capacity==524u);
    ++f.backend_calls;
    int result=kui_vc2_ce_vmu_write_probe_handle(&card,request,bytes,reply,capacity,produced);
    CHECK(!memcmp(request,f.request,bytes));
    f.backend_done=1u;
    if(f.reply_mode==1u) {
        /* Canonical eight-byte FILEERR, deliberately too large for the
         * VC2 four-byte CMD12 RX allocation. The Armada exception is off. */
        static const uint8_t error[8]={0xfb,0,2,1,0,0,0,8};
        memcpy(reply,error,sizeof(error));*produced=sizeof(error);return 1;
    }
    if(f.reply_mode==2u) {*produced=0u;return KUI_ARMADA_CE_VMU_A2_BACKEND_DEFERRED;}
    return result;
}
static bool store(void *context,uint32_t at,uint32_t value) {
    CHECK(context==&f && f.admissions==2u && f.backend_done);
    CHECK((at&UINT32_C(0xe0000003))==UINT32_C(0xa0000000));
    uint32_t bus=at&UINT32_C(0x1fffffff);
    CHECK(bus>=input.pool_bus && bus-input.pool_bus<=input.pool_bytes-4u);
    CHECK(bus==input.parent.rx_bus || (bus>=input.target.rx_bus &&
        bus-input.target.rx_bus<=input.target.capacity-4u));
    CHECK(f.stores<sizeof(f.writes)/sizeof(f.writes[0]));
    f.writes[f.stores].bus=bus;f.writes[f.stores].value=value;++f.stores;
    if(f.stores==f.fail_store)return false;
    put32(guest.bytes+bus-input.pool_bus,value);return true;
}
static unsigned publish(void) {
    unsigned result=(unsigned)kui_armada_ce_vmu_a2_publish(&input,backend,&f,
        admit,&f,store,&f,WORK,&log_state);
    CHECK(!memcmp(input.target.request,f.request,input.target.request_bytes));
    for(unsigned i=0u;i<4u;++i) {
        CHECK(protected_scratch.before[i]==UINT32_C(0x5a5a5a5a));
        CHECK(protected_scratch.after[i]==UINT32_C(0x5a5a5a5a));
        CHECK(guest.before[i]==UINT32_C(0xa5a5a5a5));
        CHECK(guest.after[i]==UINT32_C(0xa5a5a5a5));
    }
    return result;
}
static void unchanged(void) {CHECK(!memcmp(f.before,guest.bytes,sizeof(f.before)));}
static void no_publication(unsigned expected) {
    CHECK(publish()==expected);CHECK(!f.stores);unchanged();
    CHECK(log_state.fault[0]==expected && log_state.status[0]==1u);
}
static void accept_phase(unsigned phase) {
    begin_call(12u,phase);
    CHECK(publish()==KUI_ARMADA_CE_VMU_A2_PUBLISH_OK);
    CHECK(f.admissions==2u && f.backend_calls==1u && f.stores==2u);
    CHECK(f.writes[0].bus==input.target.rx_bus &&
        f.writes[0].value==UINT32_C(0x00020007));
    CHECK(f.writes[1].bus==input.parent.rx_bus &&
        f.writes[1].value==(input.parent.response_header|UINT32_C(0x00020000)));
    CHECK(get32(guest.bytes+0x420u)==UINT32_C(0x00020007));
    CHECK((get32(guest.bytes)&UINT32_C(0x00110000))==UINT32_C(0x00110000));
    CHECK(!memcmp(guest.bytes+4u,f.before+4u,0x41cu));
    CHECK(!memcmp(guest.bytes+0x424u,f.before+0x424u,sizeof(f.before)-0x424u));
    CHECK(log_state.target[4]==4u && !log_state.fault[0]);
    CHECK(!memcmp(card.endpoint.protocol.staged+phase*128u,block_data+phase*128u,128u));
    CHECK(!memcmp((uint8_t *)(void *)WORK->reply+384u,f.request,140u));
}
static void stage_complete(void) {
    for(unsigned phase=0u;phase<4u;++phase)accept_phase(phase);
    CHECK(card.endpoint.protocol.phases==15u);
    CHECK(!memcmp(card.endpoint.protocol.staged,block_data,512u));
    CHECK(!kui_vc2_ce_vmu_write_probe_pending(&card));
}
static void phase4_pending(void) {
    reset();stage_complete();begin_call(13u,4u);
    const uint32_t replies=log_state.counts[4],completed=log_state.counts[2];
    CHECK(publish()==KUI_ARMADA_CE_VMU_A2_PUBLISH_PENDING);
    CHECK(f.admissions==2u && f.backend_calls==1u && !f.stores);unchanged();
    CHECK(log_state.counts[4]==replies && log_state.counts[2]==completed);
    CHECK(!log_state.target[4] && !log_state.fault[0] && !log_state.status[3]);
    CHECK(get32(guest.bytes+0x420u)==UINT32_MAX);
    CHECK(get32(guest.bytes)==input.parent.response_header);
    CHECK(kui_vc2_ce_vmu_write_probe_pending(&card));
    CHECK(!memcmp(card.endpoint.protocol.staged,block_data,512u));
    uint32_t snapshot[20];kui_vc2_ce_vmu_write_probe_snapshot(&card,snapshot);
    CHECK(snapshot[4]==4u && snapshot[5]==1u && snapshot[6]==42u && snapshot[7]==15u);
    /* Independently generated IEEE/zlib CRC for the complete authored block. */
    CHECK(snapshot[8]==UINT32_C(0xb880cf37) && snapshot[9]==1u && !snapshot[14]);
    for(unsigned phase=0u;phase<4u;++phase)CHECK(snapshot[10u+phase]==1u);
    CHECK(snapshot[17]==UINT32_C(0x0200020d) && snapshot[18]==UINT32_C(0x2a000400));
    CHECK(snapshot[19]==524u);
    /* A lost/no reply cannot start a second commit or leak AGAIN/ACK. */
    begin_call(13u,4u);CHECK(publish()==KUI_ARMADA_CE_VMU_A2_PUBLISH_PENDING);
    CHECK(!f.stores && f.admissions==2u && card.commit_attempts==1u);unchanged();
}
static void rejected_shapes_and_owners(void) {
    reset();begin_call(12u,0u);input.target.request_bytes=139u;
    no_publication(KUI_ARMADA_CE_VMU_A2_PUBLISH_ARGUMENT);CHECK(!f.backend_calls && !f.admissions);
    reset();begin_call(12u,0u);input.target.request[3]=33u;f.request[3]=33u;
    no_publication(KUI_ARMADA_CE_VMU_A2_PUBLISH_REQUEST);CHECK(!f.backend_calls);
    reset();begin_call(12u,0u);input.target.request[9]=4u;f.request[9]=4u;
    no_publication(KUI_ARMADA_CE_VMU_A2_PUBLISH_REQUEST);CHECK(!f.backend_calls);
    reset();begin_call(12u,0u);input.tx_bus=input.pool_bus;
    no_publication(KUI_ARMADA_CE_VMU_A2_PUBLISH_OVERLAP);CHECK(!f.backend_calls);
    reset();begin_call(12u,0u);input.target.rx_bus=input.parent.rx_bus;
    no_publication(KUI_ARMADA_CE_VMU_A2_PUBLISH_OVERLAP);CHECK(!f.backend_calls);
    reset();begin_call(12u,0u);input.target.rx_bus=input.pool_bus+0x1fffu;
    no_publication(KUI_ARMADA_CE_VMU_A2_PUBLISH_SPAN);CHECK(!f.backend_calls);
    reset();begin_call(12u,0u);f.fail_admission=1u;
    no_publication(KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION);
    CHECK(f.admissions==1u && !f.backend_calls && !card.endpoint.protocol.active);
    reset();begin_call(12u,0u);f.fail_admission=2u;
    no_publication(KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION);
    CHECK(f.admissions==2u && f.backend_calls==1u && card.endpoint.protocol.phases==1u);
    reset();stage_complete();begin_call(13u,4u);f.fail_admission=2u;
    no_publication(KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION);
    CHECK(kui_vc2_ce_vmu_write_probe_pending(&card) && card.commit_attempts==1u);
    reset();begin_call(12u,0u);f.fail_store=1u;
    CHECK(publish()==KUI_ARMADA_CE_VMU_A2_PUBLISH_STORE);
    CHECK(f.stores==1u && f.writes[0].bus==input.target.rx_bus);unchanged();
    CHECK(get32(guest.bytes)==input.parent.response_header);
}
static void oversized_reply_and_deferred_guards(void) {
    reset();begin_call(12u,0u);f.reply_mode=1u;
    no_publication(KUI_ARMADA_CE_VMU_A2_PUBLISH_REPLY);
    CHECK(f.backend_calls==1u && f.admissions==1u && log_state.target[4]==8u);
    reset();begin_call(12u,0u);f.reply_mode=2u;
    no_publication(KUI_ARMADA_CE_VMU_A2_PUBLISH_BACKEND);
    CHECK(f.backend_calls==1u && f.admissions==1u);
}
int main(void) {
    phase4_pending();rejected_shapes_and_owners();oversized_reply_and_deferred_guards();
    printf("PASS VC2 write publication: %u checks; four exact128-byte phase ACKs, "
        "target-before-parent publication, private request preserved, fresh-owner refusals, "
        "phase4 pending with zero RX stores/final ACK, staged CRC and immutable retry, "
        "VC2 FILEERR truncation refused; no storage backend\n",checks);
    return 0;
}
