/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual runtime, CE protocol, deferred queue, SD provider, SD command/CRC
 * implementation and redo store. Only native SR/SP/loan and the separately
 * tested PIO MMIO boundary are replaced; media changes solely on SPI bytes. */
#include "armada_ce_vmu_sd_fixture.h"
#include "kui/armada_ce_vmu_runtime.h"
#include "kui/armada_ce_vmu_a2_publish.h"
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define OWNER UINT32_C(0x0030a60)
static uint32_t actual_sr=UINT32_C(0x400000f0);
static uintptr_t actual_sp=KUI_ARMADA_CE_VMU_WORKER_STACK_TOP-128u;
static bool owner_valid=true;
struct kui_armada_ce_vmu_a2_publish_snapshot kui_armada_ce_vmu_a2_log;
uint32_t kui_armada_ce_vmu_runtime_test_sr(void) {return actual_sr;}
void kui_armada_ce_vmu_runtime_test_set_sr(uint32_t value) {actual_sr=value;mask=value&0xf0u;}
uintptr_t kui_armada_ce_vmu_runtime_test_sp(void) {return actual_sp;}
bool kui_armada_ce_maple_runtime_owner(uint32_t owner) {
    CHECK(actual_sr==UINT32_C(0x400000f0) && owner==OWNER);return owner_valid;
}
int kui_armada_ce_vmu_runtime_test_loan_begin(struct kui_loader_sd *copy) {return loan_begin(copy);}
int kui_armada_ce_vmu_runtime_test_loan_end(bool healthy_card) {return loan_end(healthy_card);}
static void bind_runtime(void) {
    prepare_model();kui_armada_ce_vmu_runtime_test_set_sr(UINT32_C(0x400000f0));
    kui_armada_ce_vmu_runtime_handoff=handoff;
    struct kui_armada_ce_vmu_runtime_services *p=&kui_armada_ce_vmu_runtime_services;
    memset(p,0,sizeof(*p));p->loan_begin=0x8c008400u;p->loan_end=0x8c008500u;
    CHECK(!kui_armada_ce_vmu_runtime_services_seal(p));
    CHECK(!kui_armada_ce_vmu_runtime_bind(OWNER));
    CHECK(active_endpoint!=&endpoint && !sd.bytes && !active_endpoint->faulted);
}
static int handle(unsigned command,unsigned phase,unsigned block,const uint8_t *data,
    uint8_t response[524],size_t *produced) {
    uint8_t request[140]={0};request[0]=(uint8_t)command;request[1]=2u;
    request[3]=command==12u?34u:2u;request[7]=2u;
    request[9]=(uint8_t)phase;request[11]=(uint8_t)block;
    if(data) memcpy(request+12u,data,128u);
    return kui_armada_ce_vmu_runtime_handle(NULL,request,command==12u?140u:12u,
        response,524u,produced);
}
static int work(void) {
    kui_armada_ce_vmu_runtime_test_set_sr(UINT32_C(0x40000000));
    int result=kui_armada_ce_vmu_runtime_work(OWNER);
    CHECK(actual_sr==UINT32_C(0x40000000));idle();
    kui_armada_ce_vmu_runtime_test_set_sr(UINT32_C(0x400000f0));return result;
}
static void stage(uint8_t data[512]) {
    uint8_t response[524];size_t produced;
    for(unsigned i=0;i<512u;i++) data[i]=(uint8_t)(i*7u+17u);
    for(unsigned phase=0;phase<4u;phase++) {
        CHECK(handle(12u,phase,9u,data+128u*phase,response,&produced)==1);
        CHECK(produced==4u && response[0]==7u && !sd.bytes && !begin_calls);
    }
    CHECK(handle(13u,4u,9u,NULL,response,&produced)==KUI_ARMADA_CE_VMU_A2_BACKEND_DEFERRED);
    CHECK(!produced && !sd.bytes && !begin_calls);
}
static void file_error(void) {
    uint32_t first[5];memcpy(first,kui_armada_ce_vmu_a2_log.fault,sizeof(first));
    uint8_t response[524];size_t produced;
    CHECK(handle(13u,4u,9u,NULL,response,&produced)==1);
    CHECK(produced==8u && response[0]==0xfbu && response[3]==1u);
    unsigned before=sd.bytes;
    CHECK(work()==-1 && sd.bytes==before);
    CHECK(handle(13u,4u,9u,NULL,response,&produced)==1);
    CHECK(produced==8u && response[0]==0xfbu && sd.bytes==before);
    CHECK(!memcmp(first,kui_armada_ce_vmu_a2_log.fault,sizeof(first)));
}
static void reenter_during_wire(void) {
    uint8_t response[524];size_t produced=123u;
    CHECK(actual_sr==UINT32_C(0x40000000) && loan_owned && !mask);
    CHECK(kui_armada_ce_vmu_runtime_work(OWNER)==-2);
    CHECK(handle(11u,0u,9u,NULL,response,&produced)==-1);
}
static void complete_path(void) {
    bind_runtime();uint8_t data[512],response[524];size_t produced;
    stage(data);begin_result=1u;
    CHECK(work()==0 && begin_calls==1u && !sd.bytes && !end_calls);
    CHECK(handle(13u,4u,9u,NULL,response,&produced)==2 && !produced);
    uint8_t changed[128];memcpy(changed,data,128u);changed[17]^=1u;
    CHECK(handle(12u,0u,9u,changed,response,&produced)==1);
    CHECK(produced==4u && response[0]==0xfcu && !sd.bytes);
    begin_result=0u;sd.once_on_transfer=reenter_during_wire;
    CHECK(work()==1);
    CHECK(sd.writes==4u && sd.statuses==8u && active_endpoint->commits==1u);
    CHECK(port_returns==1u && end_calls==1u && !memcmp(sd.media[image_lba(9u)],data,512u));
    CHECK(active_endpoint->handoff.block_crc[9u]==kui_retail_crc32(0,data,512u));
    CHECK(!active_endpoint->handoff.seal && active_endpoint->bound);
    unsigned before=sd.bytes;
    CHECK(handle(13u,4u,9u,NULL,response,&produced)==1 && produced==4u && response[0]==7u);
    CHECK(handle(13u,4u,9u,NULL,response,&produced)==1 && produced==4u && response[0]==7u);
    CHECK(sd.bytes==before && active_endpoint->commits==1u);
    CHECK(handle(11u,0u,9u,NULL,response,&produced)==2 && !produced && sd.bytes==before);
    CHECK(work()==1 && active_endpoint->reads==1u && end_calls==2u && port_returns==2u);
    CHECK(handle(11u,0u,9u,NULL,response,&produced)==1);
    CHECK(produced==524u && response[0]==8u && !memcmp(response+12u,data,512u));
    before=sd.bytes;
    CHECK(handle(13u,0u,9u,NULL,response,&produced)==1 && produced==4u && response[0]==7u);
    CHECK(sd.bytes==before);
}
static void read_context(void) {
    bind_runtime();uint8_t response[524];size_t produced;
    CHECK(handle(11u,0u,127u,NULL,response,&produced)==2 && !produced && !sd.bytes);
    CHECK(kui_armada_ce_vmu_runtime_work(OWNER)==-2 && !sd.bytes);
    kui_armada_ce_vmu_runtime_test_set_sr(UINT32_C(0x40000000));
    CHECK(kui_armada_ce_vmu_runtime_work(OWNER+4u)==-2);
    actual_sp=KUI_ARMADA_CE_VMU_WORKER_STACK_BOTTOM-4u;
    CHECK(kui_armada_ce_vmu_runtime_work(OWNER)==-2);
    actual_sp=KUI_ARMADA_CE_VMU_WORKER_STACK_TOP;
    CHECK(kui_armada_ce_vmu_runtime_work(OWNER)==-2);
    actual_sp=KUI_ARMADA_CE_VMU_WORKER_STACK_TOP-128u;owner_valid=false;
    CHECK(kui_armada_ce_vmu_runtime_work(OWNER)==-2 && !sd.bytes && !begin_calls);
    owner_valid=true;kui_armada_ce_vmu_runtime_test_set_sr(UINT32_C(0x400000f0));
    CHECK(work()==1 && !sd.writes && active_endpoint->reads==1u);
    CHECK(handle(11u,0u,127u,NULL,response,&produced)==1 && produced==524u && response[0]==8u);
    CHECK(!memcmp(response+12u,sd.media[image_lba(127u)],512u));
}
static unsigned injected_write,injected_status,injected_release;
static void failed_commit(void) {
    bind_runtime();uint8_t data[512];stage(data);
    sd.fail_write=injected_write;sd.fail_status=injected_status;
    port_release_failure=injected_release==1u;end_failure=injected_release==2u;
    uint32_t old_crc=active_endpoint->handoff.block_crc[9u];
    CHECK(work()==-1 && !active_endpoint->commits && active_endpoint->faulted);
    CHECK(kui_armada_ce_vmu_a2_log.fault[0]==112u);
    CHECK(kui_armada_ce_vmu_a2_log.fault[1]==(uint32_t)active_endpoint->last_result);
    CHECK(kui_armada_ce_vmu_a2_log.fault[2]==
        (((uint32_t)active_endpoint->last_sd_result&0xffffu)|(active_endpoint->pio.fault<<16)));
    CHECK(kui_armada_ce_vmu_a2_log.fault[3]==(uint32_t)active_endpoint->last_loan_result);
    CHECK(kui_armada_ce_vmu_a2_log.fault[4]==active_endpoint->pio.detail);
    CHECK(active_endpoint->handoff.block_crc[9u]==old_crc && !active_endpoint->bound);
    CHECK(port_returns==1u && end_calls==1u);
    if(injected_release) CHECK(sd.writes==4u && !memcmp(sd.media[image_lba(9u)],data,512u));
    file_error();
}
static void busy_deadline(void) {
    bind_runtime();uint8_t data[512];stage(data);begin_result=1u;
    for(unsigned i=0;i<999u;i++) CHECK(work()==0);
    CHECK(work()==-1 && begin_calls==1000u && !sd.bytes && !end_calls && !port_claims);
    CHECK(!active_endpoint->commits && !active_endpoint->faulted);begin_result=0u;
    CHECK(kui_armada_ce_vmu_a2_log.fault[0]==113u);
    file_error();CHECK(begin_calls==1000u && !sd.bytes);
}
static void lose_owner(void) {owner_valid=false;}
static void owner_loss(void) {
    bind_runtime();uint8_t data[512];stage(data);sd.once_on_transfer=lose_owner;
    CHECK(work()==-1 && !sd.writes && active_endpoint->faulted && !active_endpoint->commits);
    CHECK(kui_armada_ce_vmu_a2_log.fault[0]==112u);
    CHECK(end_calls==1u && port_returns==1u && !sd.health);
    owner_valid=true;file_error();
}
static void unverified_port_release(void) {
    bind_runtime();uint8_t data[512],response[524];size_t produced;stage(data);
    port_release_unverified=1u;uint32_t old_crc=active_endpoint->handoff.block_crc[9u];
    kui_armada_ce_vmu_runtime_test_set_sr(UINT32_C(0x40000000));
    CHECK(kui_armada_ce_vmu_runtime_work(OWNER)==-1);
    CHECK(actual_sr==UINT32_C(0x40000000));
    CHECK(sd.writes==4u && !memcmp(sd.media[image_lba(9u)],data,512u));
    CHECK(sd.owned && sd.locked && !sd.health && active_endpoint->pio.acquired);
    CHECK(active_endpoint->faulted && !active_endpoint->commits && !active_endpoint->bound);
    CHECK(!active_endpoint->busy && !active_endpoint->store.io.context && !loan_owned);
    CHECK(active_endpoint->handoff.block_crc[9u]==old_crc && end_calls==1u && port_returns==1u);
    CHECK(kui_armada_ce_vmu_a2_log.fault[0]==112u);
    CHECK(kui_armada_ce_vmu_a2_log.fault[4]==(KUI_ARMADA_CE_VMU_SD_PIO_STAGE_STANDBY<<16));
    unsigned before=sd.bytes;
    CHECK(kui_armada_ce_vmu_runtime_work(OWNER)==-1 && sd.bytes==before && begin_calls==1u);
    kui_armada_ce_vmu_runtime_test_set_sr(UINT32_C(0x400000f0));
    CHECK(handle(13u,4u,9u,NULL,response,&produced)==1);
    CHECK(produced==8u && response[0]==0xfbu && sd.bytes==before); /* Never ACK. */
}
static unsigned run(void (*test)(void)) {
    int descriptors[2];CHECK(!pipe(descriptors));pid_t child=fork();CHECK(child>=0);
    if(!child) {
        close(descriptors[0]);checks=0;test();
        CHECK(write(descriptors[1],&checks,sizeof(checks))==(ssize_t)sizeof(checks));_exit(0);
    }
    close(descriptors[1]);unsigned count=0;
    ssize_t bytes=read(descriptors[0],&count,sizeof(count));close(descriptors[0]);
    int status=0;CHECK(waitpid(child,&status,0)==child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status)==0 && bytes==(ssize_t)sizeof(count));return count;
}
int main(void) {
    unsigned total=run(complete_path)+run(read_context)+run(busy_deadline)+run(owner_loss)+
        run(unverified_port_release);
    for(unsigned i=1u;i<=4u;i++) {
        injected_write=i;total+=run(failed_commit);injected_write=0u;
    }
    for(unsigned i=1u;i<=8u;i++) {injected_status=i;total+=run(failed_commit);injected_status=0u;}
    for(unsigned i=1u;i<=2u;i++) {injected_release=i;total+=run(failed_commit);}
    printf("CE runtime + real SD provider: %u checks in19 fresh-state cases; real byte SPI redo/CRC/readback, "
        "release-before-ACK, immutable pending data, context/reentry, allfour torn writes/eight status barriers, "
        "unverified port ownership, owner loss and1000 BUSY quarantine passed\n",total);return 0;
}
