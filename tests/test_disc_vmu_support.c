/* SPDX-License-Identifier: GPL-3.0-only */
#include "../src/loader/disc_vmu_support.c"
#include <assert.h>
#include <stdio.h>

static unsigned checks,calls,loans,reads;
#define CHECK(x) do {++checks;assert(x);} while(0)
static uint32_t location;
static struct kui_vmu_native_passive_exports exported;
static bool read_ok,bind_ok;
static struct kui_disc_vmu_support_request request,captured;

uint32_t kui_disc_vmu_support_test_self(void) {return location;}
bool kui_disc_vmu_support_test_exports(uint32_t address,void *out,size_t bytes) {
    ++reads;
    CHECK(address==location-KUI_DISC_VMU_WORKER_BYTES && bytes==80u);
    if(!read_ok) return false;
    memcpy(out,&exported,bytes);return true;
}
bool kui_disc_vmu_transport_bind(struct kui_disc_vmu_transport *out,uint32_t selected,
    const uint8_t identity[16],const struct kui_loader_sd *card,
    kui_vmu_sd_transaction transaction,void *context) {
    ++calls;CHECK(out==&kui_disc_vmu_support_state.transport);
    captured=request;captured.transport=selected;memcpy(captured.cid,identity,16u);
    captured.card=*card;captured.worker_transaction=(uint32_t)(uintptr_t)transaction;
    captured.worker_context=(uint32_t)(uintptr_t)context;
    return bind_ok;
}
int kui_disc_vmu_transport_loan(struct kui_disc_vmu_transport *out,
    kui_vmu_sd_transaction transaction,void *context) {
    ++loans;CHECK(out==&kui_disc_vmu_support_state.transport);
    CHECK((uintptr_t)transaction==captured.worker_transaction &&
          (uintptr_t)context==captured.worker_context);
    return 123;
}
static void reset(void) {
    memset(&kui_disc_vmu_support_state,0,sizeof(kui_disc_vmu_support_state));
    memset(&request,0,sizeof(request));memset(&captured,0,sizeof(captured));
    calls=loans=reads=0u;read_ok=bind_ok=true;location=UINT32_C(0xac512000);
    request=(struct kui_disc_vmu_support_request){
        .version=1u,.bytes=sizeof(request),.transport=1u,
        .worker_begin=UINT32_C(0x8c500000),.lease_begin=UINT32_C(0x8c500000),
        .lease_bytes=KUI_DISC_VMU_LEASE_BYTES,
        .worker_transaction=UINT32_C(0x8c502000),.worker_context=UINT32_C(0xac506020),
        .worker_terminal=UINT32_C(0x8c501000),.worker_stack_top=location,
        .cid={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16},
        .card={.blocks=10000u,.ready=true,.high_capacity=true}
    };
    exported=(struct kui_vmu_native_passive_exports){
        .magic=KUI_VMU_NATIVE_PASSIVE_MAGIC,.version=4u,.bytes=80u,
        .terminal=request.worker_terminal,.sd_transaction=request.worker_transaction,
        .bss_begin=UINT32_C(0xac506000),.bss_end=UINT32_C(0xac50a000),
        .stack_bottom=UINT32_C(0xac511000),.stack_top=location,.memory_end=location
    };
}
static void refused(void) {
    CHECK(kui_disc_vmu_support_bind(&request)==-1);
    CHECK(!calls && !kui_disc_vmu_support_state.ready && !loans);
    CHECK(kui_disc_vmu_support_sd_lease(NULL,NULL)==KUI_VMU_SD_LEASE_FAULT && !loans);
}
int main(void) {
    for(unsigned alias=0u;alias<3u;alias++) {
        reset();request.lease_begin=(request.lease_begin&UINT32_C(0x1fffffff))|
            (alias==0u?0u:alias==1u?UINT32_C(0x80000000):UINT32_C(0xa0000000));
        CHECK(kui_disc_vmu_support_bind(&request)==0 && calls==1u && reads==1u);
        CHECK(kui_disc_vmu_support_state.ready==READY &&
            kui_disc_vmu_support_state.terminal==request.worker_terminal &&
            kui_disc_vmu_support_state.stack_top==request.worker_stack_top);
        CHECK(captured.transport==request.transport && !memcmp(captured.cid,request.cid,16u) &&
            captured.worker_transaction==request.worker_transaction &&
            captured.worker_context==request.worker_context && captured.card.blocks==10000u);
        CHECK(kui_disc_vmu_support_bind(&request)==-1 && calls==1u);
        memset(&request,0,sizeof(request));
        CHECK(kui_disc_vmu_support_sd_lease((kui_vmu_sd_transaction)(uintptr_t)captured.worker_transaction,
            (void *)(uintptr_t)captured.worker_context)==123 && loans==1u);
    }
    reset();CHECK(kui_disc_vmu_support_bind(NULL)==-1 && !calls && !reads);
    for(unsigned i=0u;i<18u;i++) {
        reset();
        switch(i) {
        case 0u:request.version=2u;break;
        case 1u:--request.bytes;break;
        case 2u:request.worker_begin|=UINT32_C(0x20000000);break;
        case 3u:request.lease_begin|=UINT32_C(0x40000000);break;
        case 4u:request.lease_begin+=32u;break;
        case 5u:request.lease_bytes-=32u;break;
        case 6u:request.worker_transaction+=2u;break;
        case 7u:request.worker_terminal+=2u;break;
        case 8u:request.worker_stack_top-=32u;break;
        case 9u:request.worker_context+=4u;break;
        case 10u:request.worker_context=exported.bss_end-32u;break;
        case 11u:request.worker_context=exported.bss_begin-32u;break;
        case 12u:location+=1u;break;
        case 13u:location=UINT32_C(0x8c512000);break;
        case 14u:location=UINT32_C(0xac010000);break;
        case 15u:location=UINT32_C(0xacffe000);break;
        case 16u:read_ok=false;break;
        case 17u:request.worker_context=UINT32_C(0x8c506020);break;
        }
        refused();
    }
    for(unsigned i=0u;i<16u;i++) {
        reset();
        switch(i) {
        case 0u:exported.magic^=1u;break;
        case 1u:exported.version=3u;break;
        case 2u:exported.bytes=64u;break;
        case 3u:exported.memory_end-=32u;break;
        case 4u:exported.stack_top-=32u;break;
        case 5u:exported.stack_bottom+=32u;break;
        case 6u:exported.bss_begin+=1u;break;
        case 7u:exported.bss_end-=1u;break;
        case 8u:exported.bss_begin=UINT32_C(0xac500020);break;
        case 9u:exported.bss_end=exported.bss_begin-32u;break;
        case 10u:exported.bss_end=exported.stack_bottom+32u;break;
        case 11u:exported.sd_transaction=request.worker_transaction=UINT32_C(0xac502000);break;
        case 12u:exported.sd_transaction=request.worker_transaction|=1u;break;
        case 13u:exported.terminal=request.worker_terminal=UINT32_C(0x8c500020);break;
        case 14u:exported.terminal=request.worker_terminal=UINT32_C(0x8c506000);break;
        case 15u:exported.terminal=request.worker_terminal=UINT32_C(0xac501000);break;
        }
        refused();
    }
    reset();bind_ok=false;CHECK(kui_disc_vmu_support_bind(&request)==-1 && calls==1u);
    CHECK(!kui_disc_vmu_support_state.ready && !kui_disc_vmu_support_state.terminal &&
        !kui_disc_vmu_support_state.stack_top);
    reset();kui_disc_vmu_support_state.terminal=1u;
    CHECK(kui_disc_vmu_support_bind(&request)==-1 && !calls && !reads);
    printf("Physical-disc support binding: %u checks; worker exports/aliases, lease ownership, exact callback forwarding and no retained stage context\n",checks);
    return 0;
}
