/* SPDX-License-Identifier: GPL-3.0-only */
/* Real runtime binding + CE endpoint + deferred mailbox, with a fake scoped
 * storage provider and native SR/SP witnesses. Physical store/lease behavior
 * belongs to the separately linked provider integration tests. Each child
 * gets fresh final-location static state; production has no reset API. */
#include "kui/armada_ce_vmu_runtime.h"
#include "kui/armada_ce_vmu_sd.h"
#include "kui/armada_ce_vmu_a2_publish.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define OWNER UINT32_C(0x0030a60)
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); abort(); } } while(0)
static uint32_t sr=UINT32_C(0x400000f0);
static uintptr_t sp=KUI_ARMADA_CE_VMU_WORKER_STACK_TOP-128u;
static bool owner_valid=true;
static unsigned initialized,reads,commits,attempts,busy_count;
static int io_error,init_error;
static uint8_t saved[512];
struct kui_armada_ce_vmu_a2_publish_snapshot kui_armada_ce_vmu_a2_log;
uint32_t kui_armada_ce_vmu_runtime_test_sr(void) { return sr; }
void kui_armada_ce_vmu_runtime_test_set_sr(uint32_t value) { sr=value; }
uintptr_t kui_armada_ce_vmu_runtime_test_sp(void) { return sp; }
bool kui_armada_ce_maple_runtime_owner(uint32_t owner) {
    CHECK(sr==UINT32_C(0x400000f0));CHECK(owner==OWNER);return owner_valid;
}
int kui_armada_ce_vmu_runtime_test_loan_begin(struct kui_loader_sd *card) { (void)card;abort(); }
int kui_armada_ce_vmu_runtime_test_loan_end(bool healthy) { (void)healthy;abort(); }
int kui_armada_ce_vmu_sd_init(struct kui_armada_ce_vmu_sd *s,
    const struct kui_vmu_durable_handoff *h,const struct kui_armada_ce_vmu_sd_ops *ops) {
    CHECK(sr==UINT32_C(0x400000f0));CHECK(!kui_vmu_durable_handoff_validate(h));
    CHECK(ops->begin==kui_armada_ce_vmu_runtime_test_loan_begin);
    CHECK(ops->end==kui_armada_ce_vmu_runtime_test_loan_end);
    ++initialized;if(init_error)return -1;
    memset(s,0,sizeof(*s));s->self=s;s->ops=*ops;return 0;
}
static void provider_context(struct kui_armada_ce_vmu_sd *s) {
    CHECK(sr==UINT32_C(0x40000000));CHECK(s->self==s);
    CHECK(s->ops.guard.worker_ready(s->ops.guard.context));
    CHECK(sr==UINT32_C(0x40000000));
    CHECK(kui_armada_ce_vmu_runtime_work(OWNER)==-2);
    uint8_t req[12]={11,2,0,2,0,0,0,2,0,0,0,7},out[524];size_t n=0;
    CHECK(kui_armada_ce_vmu_runtime_handle(NULL,req,sizeof(req),out,sizeof(out),&n)==-1);
}
int kui_armada_ce_vmu_sd_read(void *context,uint16_t block,uint8_t out[512]) {
    struct kui_armada_ce_vmu_sd *s=context;provider_context(s);CHECK(block<256u);++attempts;
    if(busy_count){--busy_count;return KUI_VMU_EMU_IO_BUSY;}
    if(io_error)return KUI_VMU_EMU_IO_ERROR;
    memcpy(out,saved,512u);++reads;return KUI_VMU_EMU_IO_OK;
}
int kui_armada_ce_vmu_sd_commit(void *context,uint16_t block,const uint8_t in[512]) {
    struct kui_armada_ce_vmu_sd *s=context;provider_context(s);CHECK(block==9u);++attempts;
    if(busy_count){--busy_count;return KUI_VMU_EMU_IO_BUSY;}
    if(io_error)return KUI_VMU_EMU_IO_ERROR;
    memcpy(saved,in,512u);++commits;return KUI_VMU_EMU_IO_OK;
}
static void prepare(void) {
    struct kui_vmu_durable_handoff *h=&kui_armada_ce_vmu_runtime_handoff;
    memset(h,0,sizeof(*h));h->card_sectors=4096u;h->high_capacity=1;
    h->base_count=h->journal_count=1;h->base[0]=(struct kui_vmu_durable_extent){100u,256u};
    h->journal[0]=(struct kui_vmu_durable_extent){400u,2u};
    h->cid[0]=1;h->image_id[0]=2;h->base_sha256[0]=3;
    CHECK(!kui_vmu_durable_handoff_seal(h));
    struct kui_armada_ce_vmu_runtime_services *p=&kui_armada_ce_vmu_runtime_services;
    memset(p,0,sizeof(*p));p->loan_begin=0x8c008400u;p->loan_end=0x8c008500u;
    CHECK(!kui_armada_ce_vmu_runtime_services_seal(p));
    CHECK(!kui_armada_ce_vmu_runtime_services_validate(p));
    for(unsigned i=0;i<512u;i++)saved[i]=(uint8_t)(i*19u+3u);
}
static void bind(void) {prepare();CHECK(!kui_armada_ce_vmu_runtime_bind(OWNER));CHECK(initialized==1);}
static int handle(uint8_t command,unsigned phase,unsigned block,const uint8_t *data,
    uint8_t response[524],size_t *n) {
    uint8_t request[140]={0};request[0]=command;request[1]=2;
    request[3]=command==12u?34u:2u;request[7]=2;request[9]=(uint8_t)phase;request[11]=(uint8_t)block;
    if(data)memcpy(request+12u,data,128u);
    return kui_armada_ce_vmu_runtime_handle(NULL,request,command==12u?140u:12u,response,524u,n);
}
static int work(void) {sr=UINT32_C(0x40000000);int r=kui_armada_ce_vmu_runtime_work(OWNER);CHECK(sr==UINT32_C(0x40000000));sr|=0xf0u;return r;}
static void stage(uint8_t data[512]) {
    uint8_t response[524];size_t n;
    for(unsigned i=0;i<512u;i++)data[i]=(uint8_t)(i*7u+17u);
    for(unsigned phase=0;phase<4;phase++) {
        CHECK(handle(12,phase,9,data+128u*phase,response,&n)==1);CHECK(n==4u&&response[0]==7u);
        CHECK(attempts==0u);
    }
}
static void codec(void) {
    prepare();struct kui_armada_ce_vmu_runtime_services good=kui_armada_ce_vmu_runtime_services;
    CHECK(kui_armada_ce_vmu_runtime_services_validate(NULL));CHECK(kui_armada_ce_vmu_runtime_services_seal(NULL));
    for(unsigned byte=0;byte<sizeof(good);byte++)for(unsigned bit=0;bit<8;bit++) {
        struct kui_armada_ce_vmu_runtime_services changed=good;
        ((uint8_t *)&changed)[byte]^=(uint8_t)(1u<<bit);
        CHECK(kui_armada_ce_vmu_runtime_services_validate(&changed));
    }
    const uint32_t bad[]={0,0x0c008400u,0xac008400u,0x8c0082feu,0x8c00d800u,0x8c008401u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        struct kui_armada_ce_vmu_runtime_services changed=good;changed.loan_begin=bad[i];
        CHECK(kui_armada_ce_vmu_runtime_services_seal(&changed));CHECK(!changed.seal);
    }
}
static void binding(void) {
    prepare();sr=UINT32_C(0x40000000);CHECK(kui_armada_ce_vmu_runtime_bind(OWNER));
    sr=UINT32_C(0x500000f0);CHECK(kui_armada_ce_vmu_runtime_bind(OWNER));
    sr=0xf0u;CHECK(kui_armada_ce_vmu_runtime_bind(OWNER));sr=UINT32_C(0x400000f0);
    CHECK(kui_armada_ce_vmu_runtime_bind(OWNER+1u));CHECK(!initialized);
    CHECK(!kui_armada_ce_vmu_runtime_bind(OWNER));CHECK(!kui_armada_ce_vmu_runtime_bind(OWNER));
    CHECK(initialized==1u);CHECK(kui_armada_ce_vmu_runtime_bind(OWNER+4u));CHECK(!attempts);
}
static void bad_handoff(void) {
    prepare();kui_armada_ce_vmu_runtime_handoff.cid[4]^=1u;
    CHECK(kui_armada_ce_vmu_runtime_bind(OWNER));CHECK(!initialized);
    prepare();CHECK(kui_armada_ce_vmu_runtime_bind(OWNER));CHECK(!initialized);
}
static void bad_services(void) {
    prepare();kui_armada_ce_vmu_runtime_services.loan_begin+=2;
    CHECK(kui_armada_ce_vmu_runtime_bind(OWNER));CHECK(!initialized);
    prepare();CHECK(kui_armada_ce_vmu_runtime_bind(OWNER));CHECK(!initialized);
}
static void bad_provider_init(void) {
    prepare();init_error=1;CHECK(kui_armada_ce_vmu_runtime_bind(OWNER));CHECK(initialized==1u);
    init_error=0;CHECK(kui_armada_ce_vmu_runtime_bind(OWNER));CHECK(initialized==1u);CHECK(!attempts);
}
static void read_context(void) {
    bind();uint8_t response[524];size_t n=999;
    CHECK(handle(11,0,7,NULL,response,&n)==2);CHECK(!n&&!attempts);
    CHECK(kui_armada_ce_vmu_runtime_work(OWNER)==-2);CHECK(!attempts);
    sr=UINT32_C(0x40000000);
    CHECK(kui_armada_ce_vmu_runtime_work(OWNER+4)==-2);
    sp=KUI_ARMADA_CE_VMU_WORKER_STACK_BOTTOM-4u;CHECK(kui_armada_ce_vmu_runtime_work(OWNER)==-2);
    sp=KUI_ARMADA_CE_VMU_WORKER_STACK_TOP;CHECK(kui_armada_ce_vmu_runtime_work(OWNER)==-2);
    sp=KUI_ARMADA_CE_VMU_WORKER_STACK_TOP-128u;owner_valid=false;
    CHECK(kui_armada_ce_vmu_runtime_work(OWNER)==-2);CHECK(!attempts);owner_valid=true;sr|=0xf0u;
    CHECK(work()==1);CHECK(reads==1u);
    CHECK(handle(11,0,7,NULL,response,&n)==1);CHECK(n==524u&&response[0]==8u);
    CHECK(!memcmp(response+12u,saved,512u));CHECK(reads==1u);
    CHECK(handle(13,0,7,NULL,response,&n)==1);CHECK(n==4u&&response[0]==7u);CHECK(reads==1u);
}
static void write_success(void) {
    bind();uint8_t data[512],response[524];size_t n;stage(data);
    CHECK(handle(13,4,9,NULL,response,&n)==2);CHECK(n==0u&&!commits);
    busy_count=1;CHECK(work()==0);CHECK(!commits&&attempts==1u);
    CHECK(handle(13,4,9,NULL,response,&n)==2);CHECK(!n);
    CHECK(work()==1);CHECK(commits==1u&&attempts==2u);CHECK(!memcmp(saved,data,512u));
    CHECK(handle(13,4,9,NULL,response,&n)==1);CHECK(n==4u&&response[0]==7u);
    CHECK(handle(13,4,9,NULL,response,&n)==1);CHECK(n==4u&&response[0]==7u);CHECK(commits==1u&&attempts==2u);
    CHECK(handle(11,0,9,NULL,response,&n)==2);CHECK(!n);CHECK(work()==1);
    CHECK(handle(11,0,9,NULL,response,&n)==1);CHECK(n==524u&&!memcmp(response+12u,data,512u));
}
static void failed_write(void) {
    bind();uint8_t data[512],response[524];size_t n;stage(data);
    CHECK(handle(13,4,9,NULL,response,&n)==2);CHECK(!n);io_error=1;CHECK(work()==-1);
    CHECK(!commits&&attempts==1u);
    CHECK(kui_armada_ce_vmu_a2_log.fault[0]==113u);
    uint32_t first[5];memcpy(first,kui_armada_ce_vmu_a2_log.fault,sizeof(first));
    CHECK(handle(13,4,9,NULL,response,&n)==1);CHECK(n==8u&&response[0]==0xfbu);
    io_error=0;CHECK(handle(13,4,9,NULL,response,&n)==1);CHECK(n==8u&&response[0]==0xfbu);
    CHECK(work()==-1);CHECK(!commits&&attempts==1u);
    CHECK(!memcmp(first,kui_armada_ce_vmu_a2_log.fault,sizeof(first)));
}
static void busy_timeout(void) {
    bind();uint8_t data[512],response[524];size_t n;stage(data);
    CHECK(handle(13,4,9,NULL,response,&n)==2);CHECK(!n);busy_count=1001;
    for(unsigned i=0;i<999u;i++) CHECK(work()==0);
    CHECK(work()==-1);CHECK(!commits&&attempts==1000u);
    CHECK(kui_armada_ce_vmu_a2_log.fault[0]==113u);
    CHECK(handle(13,4,9,NULL,response,&n)==1);CHECK(n==8u&&response[0]==0xfbu);
    busy_count=0;CHECK(work()==-1);CHECK(!commits&&attempts==1000u);
}
int main(void) {
    void (*cases[])(void)={codec,binding,bad_handoff,bad_services,bad_provider_init,read_context,write_success,failed_write,busy_timeout};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++) {
        pid_t child=fork();CHECK(child>=0);
        if(!child){cases[i]();_exit(0);}
        int status=0;CHECK(waitpid(child,&status,0)==child);CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==0);
    }
    puts("CE runtime binding: sealed exact services/handoff, immutable init, masked endpoint/private unmasked worker, actual endpoint+mailbox reads/writes, BUSY then durable ACK, duplicate ACK and permanent provider errors passed");return 0;
}
