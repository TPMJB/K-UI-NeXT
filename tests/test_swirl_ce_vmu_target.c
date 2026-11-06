/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthetic host fixture: no executable, hardware, SD provider or native hold. */
#include "kui/armada_ce_vmu_a2_publish.h"
#include "kui/armada_ce_vmu_readonly.h"
#include "kui/armada_ce_vmu_write.h"
#include "kui/ce_vmu_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Independent expected scalars supplied by the runner, not target macros. */
#ifndef TEST_EXPECTED_ADDRESS
#error "Runner must supply independent expected endpoint scalars"
#endif
_Static_assert(KUI_CE_VMU_ADDRESS==TEST_EXPECTED_ADDRESS,"selected wire address");
_Static_assert(KUI_CE_VMU_UNIT_INDEX==TEST_EXPECTED_INDEX,"selected source unit");
_Static_assert(KUI_CE_VMU_ATTACHMENT_BIT==TEST_EXPECTED_ATTACHMENT,"selected parent bit");
_Static_assert(KUI_VMU_EMU_FUNC_MEMCARD==0x02000000u,"function is independent of subunit");

static unsigned checks,scenarios;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);abort();}} while(0)
static struct kui_armada_ce_vmu_a2_input in;
static struct kui_armada_ce_vmu_a2_publish_scratch scratch;
static struct kui_armada_ce_vmu_a2_publish_snapshot snapshot;
static struct kui_armada_ce_vmu_readonly readonly;
static struct kui_armada_ce_vmu_write writer;
static struct {
    uint8_t rx[0x2000],before[0x2000],disk[512];
    uint32_t store_at[140];
    unsigned stores,backend_calls,admit_calls,reject_admit,final_granted;
    unsigned writable,reads,commit_attempts,commits,wrong_sender;
    int commit_result;
} f;

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(n>>(i*8u));
}
static int read_block(void *context,uint16_t block,uint8_t bytes[512]) {
    CHECK(context==&f && block==42u);++f.reads;memcpy(bytes,f.disk,512u);return 0;
}
static int commit_block(void *context,uint16_t block,const uint8_t bytes[512]) {
    CHECK(context==&f && block==42u);++f.commit_attempts;
    if(f.commit_result) return f.commit_result;
    memcpy(f.disk,bytes,512u);++f.commits;return 0;
}
static void fresh(unsigned writable) {
    memset(&f,0,sizeof(f));memset(&in,0,sizeof(in));
    memset(&writer,0,sizeof(writer));
    const struct kui_vmu_emulation_io io={&f,read_block,commit_block};
    CHECK(!kui_armada_ce_vmu_write_init(&writer,&io));
    CHECK(!kui_armada_ce_vmu_readonly_init(&readonly));
    CHECK(writer.protocol.address==TEST_EXPECTED_ADDRESS);
    CHECK(readonly.backend.protocol.address==TEST_EXPECTED_ADDRESS);
    f.writable=writable;
    for(unsigned i=0;i<512u;i++) f.disk[i]=(uint8_t)(i*7u+19u);
    in.pool_bus=0x0cfd1000u;in.pool_bytes=0x2000u;
    in.tx_bus=0x0cfd3000u;in.tx_bytes=0x100u;
    in.protected_bus=0x0cfca000u;in.protected_bytes=0x6000u;
    in.parent.request_header=0x01002009u;
    in.parent.response_header=0x03200008u|TEST_OTHER_ATTACHMENT;
    in.parent.request_function=0x01000000u;in.parent.response_function=0x01000000u;
    in.parent.rx_bus=in.pool_bus;in.parent.capacity=16u;
    in.target_present=1u;in.target.rx_bus=in.pool_bus+0x420u;
}
static void request(unsigned command,unsigned block,unsigned phase) {
    memset(&snapshot,0,sizeof(snapshot));memset(&scratch,0,sizeof(scratch));
    memset(in.target.request,0,sizeof(in.target.request));
    memset(f.rx,0xa5,sizeof(f.rx));
    f.stores=f.backend_calls=f.admit_calls=f.final_granted=0u;
    f.reject_admit=f.wrong_sender=0u;
    in.target.capacity=1020u;in.target.response_header=UINT32_MAX;
    in.target.request[0]=(uint8_t)command;
    in.target.request[1]=TEST_EXPECTED_ADDRESS;
    in.target.request_bytes=command==1u || command==2u?4u:command==12u?140u:12u;
    if(in.target.request_bytes!=4u) {
        in.target.request[3]=(uint8_t)(command==12u?34u:2u);
        /* MEMCARD stays 0x02000000 for A1 and A2. */
        in.target.request[7]=2u;in.target.request[9]=(uint8_t)phase;
        in.target.request[10]=(uint8_t)(block>>8);in.target.request[11]=(uint8_t)block;
    }
    put32(f.rx,in.parent.response_header);
    put32(f.rx+0x420u,UINT32_MAX);memcpy(f.before,f.rx,sizeof(f.rx));
}
static int backend(void *context,const uint8_t *p,size_t n,uint8_t *reply,size_t cap,size_t *produced) {
    CHECK(context==&f && !f.stores);++f.backend_calls;
    int result=f.writable?kui_armada_ce_vmu_write_handle(&writer,p,n,reply,cap,produced):
        kui_armada_ce_vmu_readonly_handle(&readonly,p,n,reply,cap,produced);
    if(f.wrong_sender && result==1) reply[2]=TEST_OTHER_ADDRESS;
    return result;
}
static bool admit(void *context) {
    CHECK(context==&f && !f.stores);++f.admit_calls;
    bool mutation=in.target_present && (in.target.request[0]==12u ||
        (in.target.request[0]==13u && in.target.request[9]==4u));
    if(mutation && f.admit_calls==1u) CHECK(!f.backend_calls);
    else CHECK(!in.target_present || f.backend_calls==1u);
    bool accepted=f.admit_calls!=f.reject_admit;
    if(accepted && (!mutation || f.admit_calls==2u)) f.final_granted=1u;
    return accepted;
}
static bool store32(void *context,uint32_t at,uint32_t value) {
    CHECK(context==&f && f.final_granted && (at&0xe0000000u)==0xa0000000u && !(at&3u));
    uint32_t bus=at&0x1fffffffu;
    CHECK(bus>=in.pool_bus && bus-in.pool_bus<=in.pool_bytes-4u);
    CHECK(bus==in.parent.rx_bus || (in.target_present && bus>=in.target.rx_bus &&
        bus-in.target.rx_bus<=in.target.capacity-4u));
    CHECK(f.stores<140u);f.store_at[f.stores++]=bus;
    put32(f.rx+bus-in.pool_bus,value);return true;
}
static unsigned publish(void) {
    return kui_armada_ce_vmu_a2_publish(&in,backend,&f,admit,&f,store32,&f,&scratch,&snapshot);
}
static void accepted(unsigned reply_command,unsigned bytes) {
    CHECK(publish()==KUI_ARMADA_CE_VMU_A2_PUBLISH_OK);
    CHECK(f.backend_calls==1u && f.stores==bytes/4u+1u);
    const uint8_t *reply=f.rx+0x420u;
    CHECK(le32(reply)==(reply_command|(TEST_EXPECTED_ADDRESS<<16)|((bytes/4u-1u)<<24)));
    CHECK(reply[0]==reply_command && reply[1]==0u && reply[2]==TEST_EXPECTED_ADDRESS);
    CHECK(reply[3]==bytes/4u-1u);
    CHECK(le32(f.rx)==(in.parent.response_header|TEST_EXPECTED_ATTACHMENT));
    CHECK((le32(f.rx)&TEST_OTHER_ATTACHMENT)==TEST_OTHER_ATTACHMENT);
    for(unsigned i=0;i<bytes/4u-1u;i++) CHECK(f.store_at[i]==in.target.rx_bus+4u+4u*i);
    CHECK(f.store_at[bytes/4u-1u]==in.target.rx_bus);
    CHECK(f.store_at[f.stores-1u]==in.parent.rx_bus);
    CHECK(!memcmp(f.rx+4u,f.before+4u,0x41cu));
    CHECK(!memcmp(f.rx+0x420u+bytes,f.before+0x420u+bytes,sizeof(f.rx)-0x420u-bytes));
    CHECK(!snapshot.fault[0] && snapshot.target[4]==bytes);
}
static void refused(unsigned reason,unsigned backend_calls,unsigned admissions) {
    unsigned result=publish();
    if(result!=reason || f.stores || f.backend_calls!=backend_calls || f.admit_calls!=admissions)
        fprintf(stderr,"scenario %u: result %u/%u stores %u backend %u/%u admits %u/%u\n",
            scenarios,result,reason,f.stores,f.backend_calls,backend_calls,f.admit_calls,admissions);
    CHECK(result==reason && !f.stores && f.backend_calls==backend_calls && f.admit_calls==admissions);
    CHECK(!memcmp(f.rx,f.before,sizeof(f.rx)) && snapshot.fault[0]==reason);
    CHECK(publish()==KUI_ARMADA_CE_VMU_A2_PUBLISH_STATE && !f.stores);
    CHECK(!memcmp(f.rx,f.before,sizeof(f.rx)));
}
static void metadata_and_read(void) {
    const unsigned commands[]={1u,2u,10u},sizes[]={116u,196u,32u},replies[]={5u,6u,8u};
    for(unsigned writable=0;writable<2u;writable++) {
        fresh(writable);
        for(unsigned k=0;k<3u;k++) {
            ++scenarios;request(commands[k],0u,0u);in.target.capacity=sizes[k];
            accepted(replies[k],sizes[k]);CHECK(le32(f.rx+0x424u)==0x02000000u);
        }
        ++scenarios;request(11u,42u,0u);in.target.capacity=524u;accepted(8u,524u);
        CHECK(le32(f.rx+0x424u)==0x02000000u && le32(f.rx+0x428u)==0x2a000000u);
        if(writable) CHECK(!memcmp(f.rx+0x42cu,f.disk,512u));
        else for(unsigned i=0;i<512u;i++) CHECK(f.rx[0x42cu+i]==0u);
        for(unsigned phase=0;phase<2u;phase++) {
            ++scenarios;request(13u,42u,phase);in.target.capacity=4u;accepted(7u,4u);
        }
        CHECK(!f.commits && !f.commit_attempts);
    }
}
static void wrong_endpoint(void) {
    uint8_t reply[524];size_t produced;
    for(unsigned writable=0;writable<2u;writable++) {
        ++scenarios;fresh(writable);request(11u,42u,0u);accepted(8u,524u);
        request(11u,42u,0u);in.target.request[1]=TEST_OTHER_ADDRESS;
        memset(reply,0x6d,sizeof(reply));produced=123u;unsigned reads=f.reads;
        int result=writable?kui_armada_ce_vmu_write_handle(&writer,in.target.request,12u,reply,sizeof(reply),&produced):
            kui_armada_ce_vmu_readonly_handle(&readonly,in.target.request,12u,reply,sizeof(reply),&produced);
        CHECK(!result && !produced && f.reads==reads);
        for(unsigned i=0;i<sizeof(reply);i++) CHECK(reply[i]==0x6du);
        CHECK(writable?writer.protocol.read_valid:readonly.read_valid);
        refused(KUI_ARMADA_CE_VMU_A2_PUBLISH_REQUEST,0u,0u);
        request(13u,42u,0u);accepted(7u,4u);
        ++scenarios;request(1u,0u,0u);f.wrong_sender=1u;
        refused(KUI_ARMADA_CE_VMU_A2_PUBLISH_REPLY,1u,0u);
    }
}
static void staged_write_and_sync(void) {
    uint8_t data[512],old[512];fresh(1u);memcpy(old,f.disk,512u);
    for(unsigned i=0;i<512u;i++) data[i]=(uint8_t)(i*13u+i/128u);
    for(unsigned phase=0;phase<4u;phase++) {
        ++scenarios;request(12u,42u,phase);in.target.capacity=4u;
        memcpy(in.target.request+12u,data+phase*128u,128u);accepted(7u,4u);
        CHECK(f.admit_calls==2u && !f.commit_attempts && !f.commits);
        CHECK(!memcmp(f.disk,old,512u));
    }
    ++scenarios;request(13u,42u,4u);f.commit_result=KUI_VMU_EMU_IO_BUSY;
    accepted(0xfcu,4u);CHECK(f.commit_attempts==1u && !f.commits && !memcmp(f.disk,old,512u));
    ++scenarios;request(13u,42u,4u);f.commit_result=KUI_VMU_EMU_IO_OK;
    accepted(7u,4u);CHECK(f.admit_calls==2u && f.commit_attempts==2u && f.commits==1u);
    CHECK(!memcmp(f.disk,data,512u) && writer.commits==1u);
    ++scenarios;request(13u,42u,4u);accepted(7u,4u);
    CHECK(f.commit_attempts==2u && f.commits==1u);
    ++scenarios;request(11u,42u,0u);accepted(8u,524u);CHECK(!memcmp(f.rx+0x42cu,data,512u));
}
static void fences_and_bounds(void) {
    ++scenarios;fresh(1u);request(1u,0u,0u);in.parent.response_header|=TEST_EXPECTED_ATTACHMENT;
    put32(f.rx,in.parent.response_header);memcpy(f.before,f.rx,sizeof(f.rx));
    refused(KUI_ARMADA_CE_VMU_A2_PUBLISH_CONFLICT,0u,0u);
    const unsigned capacities[]={0u,3u,520u,525u,1024u};
    for(unsigned k=0;k<sizeof(capacities)/sizeof(capacities[0]);k++) {
        ++scenarios;fresh(1u);request(11u,42u,0u);in.target.capacity=capacities[k];
        refused(KUI_ARMADA_CE_VMU_A2_PUBLISH_SPAN,0u,0u);
    }
    ++scenarios;fresh(1u);request(1u,0u,0u);in.target.response_header=0x00010005u;
    put32(f.rx+0x420u,in.target.response_header);memcpy(f.before,f.rx,sizeof(f.rx));
    refused(KUI_ARMADA_CE_VMU_A2_PUBLISH_CONFLICT,0u,0u);
    ++scenarios;fresh(1u);request(11u,42u,0u);in.target.rx_bus=in.parent.rx_bus;
    refused(KUI_ARMADA_CE_VMU_A2_PUBLISH_OVERLAP,0u,0u);
    ++scenarios;fresh(1u);request(11u,42u,0u);f.reject_admit=1u;
    refused(KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION,1u,1u);
    CHECK(f.reads==1u && !f.commits);
    ++scenarios;fresh(1u);request(12u,42u,0u);f.reject_admit=1u;
    refused(KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION,0u,1u);
    CHECK(!writer.protocol.active && !f.commit_attempts);
    ++scenarios;fresh(1u);request(12u,42u,0u);f.reject_admit=2u;
    refused(KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION,1u,2u);
    CHECK(writer.protocol.active && !f.commit_attempts);
    ++scenarios;fresh(0u);request(1u,0u,0u);in.target_present=0u;f.reject_admit=1u;
    refused(KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION,0u,1u);
    ++scenarios;fresh(0u);request(1u,0u,0u);in.target_present=0u;
    CHECK(!publish() && f.stores==1u && !f.backend_calls && f.admit_calls==1u);
    CHECK(le32(f.rx)==(in.parent.response_header|TEST_EXPECTED_ATTACHMENT));
    CHECK(!memcmp(f.rx+4u,f.before+4u,sizeof(f.rx)-4u));
}
int main(void) {
    metadata_and_read();wrong_endpoint();staged_write_and_sync();fences_and_bounds();
    printf("{\"address\":%u,\"checks\":%u,\"scenarios\":%u,\"hardware_executed\":false}\n",
        TEST_EXPECTED_ADDRESS,checks,scenarios);return 0;
}
