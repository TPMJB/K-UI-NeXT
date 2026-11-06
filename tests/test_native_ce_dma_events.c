/* SPDX-License-Identifier: GPL-3.0-only */
/* The production binder/queue helper runs on an external original CE body.
 * Only address-to-host-storage translation is authored. No page tables,
 * scheduler, TLB handler, device interrupt or video executes here. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static _Alignas(4) uint8_t memory[0x200000];
static uint8_t kernel_before[sizeof(memory)];
static unsigned checks,cases;
#define CHECK(x) do {++checks;assert(x);}while(0)
static uintptr_t address(uint32_t a) {
    CHECK(a>=0x8c010000u && a<0x8c210000u);
    return (uintptr_t)(memory+a-0x8c010000u);
}
#define KUI_NATIVE_CE_EVENTS_ADDRESS(a) address(a)
#include "kui/native_ce_dma_events.h"
static uint32_t *word(uint32_t at) {return (uint32_t *)address(at);}
int main(int argc,char **argv) {
    CHECK(argc==2);
    FILE *f=fopen(argv[1],"rb");CHECK(f);
    CHECK(!fseek(f,2048,SEEK_SET));
    CHECK(fread(memory,1,1839104,f)==1839104);CHECK(!fclose(f));
    uint32_t bound[3]={0,0,0};
    CHECK(kui_native_ce_dma_bind(0x8c0120f0u,bound));
    CHECK(bound[0]==0x8c1d5b40u && bound[1]==0x8c1d5aa8u && bound[2]==0x8c1d5884u);
    ++cases;
    /* Reject every alteration to a fixed instruction bit, including the
     * exact CMP/EQ #0 immediate which resembles a wildcard instruction. */
    uint16_t *code=(uint16_t *)address(0x8c0126f0u);
    for(unsigned i=0;i<36u;i++) {
        uint16_t saved=code[i];
        unsigned wild=i==1u || i==10u || i==12u || i==14u || i==17u || i==19u || i==23u || i==26u || i==35u;
        for(unsigned bit=wild?8u:0u;bit<16u;bit++) {
            uint32_t untouched[3]={1,2,3};
            code[i]=saved^(1u<<bit);
            CHECK(!kui_native_ce_dma_bind(0x8c0120f0u,untouched));
            CHECK(untouched[0]==1 && untouched[1]==2 && untouched[2]==3);
            ++cases;
        }
        code[i]=saved;
    }
    const uint32_t invalid_vbr[]={0u,0xffffffffu,0x8c000000u,0x8c0120f1u,0x8cfff004u};
    for(unsigned i=0;i<sizeof(invalid_vbr)/sizeof(invalid_vbr[0]);i++) {
        uint32_t untouched[3]={1,2,3};
        CHECK(!kui_native_ce_dma_bind(invalid_vbr[i],untouched));
        CHECK(untouched[0]==1 && untouched[1]==2 && untouched[2]==3);++cases;
    }
    const uint32_t literals[]={0x8c012784u,0x8c012788u,0x8c01278cu};
    const uint32_t invalid_address[]={0u,0x8c000000u,0x8c1d5885u,0x8cffffdcu,0xac1d5884u};
    for(unsigned i=0;i<3u;i++)for(unsigned j=0;j<sizeof(invalid_address)/sizeof(invalid_address[0]);j++) {
        uint32_t *p=word(literals[i]),saved=*p,untouched[3]={1,2,3};
        *p=invalid_address[j];memcpy(kernel_before,memory,sizeof(memory));
        CHECK(!kui_native_ce_dma_bind(0x8c0120f0u,untouched));
        /* Private binding words may be partial on terminal rejection;
         * production must not write any kernel-owned byte while binding. */
        CHECK(!memcmp(kernel_before,memory,sizeof(memory)));*p=saved;++cases;
    }
    CHECK(kui_native_ce_dma_bind(0x8c0120f0u,bound));
    uint32_t *pending=word(bound[0]),*head=word(bound[1]);
    uint8_t *ring=(uint8_t *)(head+1),*resched=(uint8_t *)address(bound[2]);
    memset(ring,0xcc,32);*pending=5u;*head=31u;*resched=0xa0u;
    CHECK(kui_native_ce_dma_raise(bound,21u));
    CHECK(*pending==(5u|(1u<<13)) && *head==0u && ring[31]==13u && *resched==0xa1u);
    for(unsigned i=0;i<31u;i++)CHECK(ring[i]==0xcc);
    ++cases;
    CHECK(kui_native_ce_dma_raise(bound,21u));
    CHECK(*head==0u && ring[0]==0xcc && *pending==(5u|(1u<<13)));++cases;
    CHECK(kui_native_ce_dma_raise(bound,20u));
    CHECK(*pending==(5u|(1u<<12)|(1u<<13)) && *head==1u && ring[0]==12u && *resched==0xa1u);++cases;
    *pending=5u;*head=32u;*resched=0xa0u;
    CHECK(!kui_native_ce_dma_raise(bound,21u));
    CHECK(*pending==5u && *head==32u && *resched==0xa0u);++cases;
    /* Once CE acknowledges the pending bit, a later piece is queued again. */
    *head=1u;CHECK(kui_native_ce_dma_raise(bound,21u));
    CHECK(*head==2u && ring[1]==13u && *pending==(5u|(1u<<13)));++cases;
    printf("{\"checks\":%u,\"cases\":%u,\"original_kernel_bytes\":1839104,\"hardware_executed\":false}\n",checks,cases);
    return 0;
}
