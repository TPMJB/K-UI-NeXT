/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include "kui/vmu_native_heap.h"
#include "kui/vmu_native_heap_budget.h"
#include "kui/hash.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#define KUI_RETAIL_EXEC_ADDRESS UINT32_C(0x8c010000)
static struct kui_vmu_native_heap native_heap;
static struct kui_vmu_native_heap_call native_call;
static unsigned native_arena,exec_bytes=0x1000u,checks,budgets,reports;
static int budget_result;
static jmp_buf refusal;
#define CHECK(x) do {++checks;assert(x);} while(0)
static bool native_read(void *context,uint32_t address,uint32_t *value) {
    (void)context;(void)address;(void)value;CHECK(false);return false;
}
/* Only the reviewed dispatch private-pool producer is stubbed. The new
 * direct routing must never call it or publish a reserve address. */
enum kui_vmu_native_heap_budget_result kui_vmu_native_heap_budget_prepare(
    const void *image,size_t bytes,const struct kui_vmu_native_heap *heap,
    const struct kui_vmu_native_heap_call *call,kui_vmu_native_heap_read read,
    void *context,uint32_t cost,struct kui_vmu_native_heap_budget *out) {
    CHECK((uintptr_t)image==KUI_RETAIL_EXEC_ADDRESS && bytes==exec_bytes && heap==&native_heap);
    CHECK(call==&native_call && read==native_read && !context && cost==0x30020u);
    ++budgets;memset(out,0,sizeof(*out));
    if(!kui_vmu_native_heap_metadata_valid(heap))return KUI_VMU_NATIVE_HEAP_BUDGET_INVALID;
    if(budget_result==KUI_VMU_NATIVE_HEAP_BUDGET_FOUND)out->reserve_address=0x8c010050u;
    return budget_result;
}
static void native_report(uint32_t phase,uint32_t detail) {
    CHECK(phase==6u && detail==0x64u);++reports;longjmp(refusal,1);
}
#include "allocator-stage-fixture.h"
static void setup(bool direct) {
    native_heap=(struct kui_vmu_native_heap){.version=1u,.bytes=sizeof(native_heap),
        .family=direct?KUI_VMU_NATIVE_HEAP_DIRECT32:KUI_VMU_NATIVE_HEAP_TAIL32,
        .layout=direct?KUI_VMU_NATIVE_HEAP_DIRECT:KUI_VMU_NATIVE_HEAP_DISPATCH2,
        .load=KUI_RETAIL_EXEC_ADDRESS,.image_bytes=0x1000u,
        .initializer=KUI_RETAIL_EXEC_ADDRESS+0x100u,.initializer_slot=KUI_RETAIL_EXEC_ADDRESS+0x80u,
        .initializer_target=KUI_RETAIL_EXEC_ADDRESS+0x100u,
        .allocate=KUI_RETAIL_EXEC_ADDRESS+0x100u+(direct?0xaeu:0xe2u),
        .release=KUI_RETAIL_EXEC_ADDRESS+0x100u+(direct?0x160u:0x1b0u),
        .sentinel=UINT32_C(0x8c300000),.cursor_cell=UINT32_C(0x8c300020),
        .begin_cell=UINT32_C(0x8c300024),.bytes_cell=UINT32_C(0x8c300028),
        .begin_copy_cell=UINT32_C(0x8c30002c)};
    if(!direct) {native_heap.dispatch_cell=native_heap.sentinel-4u;native_heap.dispatch_table=KUI_RETAIL_EXEC_ADDRESS+0x900u;}
    native_call.load=KUI_RETAIL_EXEC_ADDRESS+0x40u;
    native_arena=budgets=reports=0u;budget_result=KUI_VMU_NATIVE_HEAP_BUDGET_NONE;
    CHECK(kui_vmu_native_heap_metadata_valid(&native_heap));
}
static void digest_extent(void (*digest)(uint8_t[32])) {
    uint8_t first[32],changed[32];
    for(unsigned direct=0u;direct<2u;direct++) {
        setup(direct);uint8_t *code=(void *)(uintptr_t)native_heap.initializer;
        memset(code,0x5a,0x300u);digest(first);
        uint32_t bytes=direct?0x268u:0x1d0u;
        code[bytes-1u]^=1u;digest(changed);CHECK(memcmp(first,changed,32u));code[bytes-1u]^=1u;
        code[bytes]^=1u;digest(changed);CHECK(!memcmp(first,changed,32u));code[bytes]^=1u;
        code[0x264u]^=1u;digest(changed);CHECK((memcmp(first,changed,32u)!=0)==(direct!=0u));code[0x264u]^=1u;
    }
}
int main(void) {
    void *ram=mmap((void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,0x1000u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);CHECK(ram!=(void *)-1);
    digest_extent(image_code_sha);digest_extent(disc_code_sha);
    struct kui_vmu_native_heap_lease lease={.lease_bytes=0x30000u};
    struct kui_vmu_native_heap_budget budget;
    setup(true);memset(&budget,0xa5,sizeof(budget));native_budget_prepare(&lease,&budget);
    const struct kui_vmu_native_heap_budget zero={0};CHECK(!memcmp(&budget,&zero,sizeof(budget)) && !budgets && !reports);
    for(unsigned fault=0u;fault<4u;fault++) {
        setup(true);
        if(fault==0u)native_heap.layout=KUI_VMU_NATIVE_HEAP_DISPATCH2;
        else if(fault==1u)native_heap.dispatch_cell=native_heap.sentinel-4u;
        else if(fault==2u)native_heap.dispatch_table=KUI_RETAIL_EXEC_ADDRESS+0x900u;
        else native_heap.initializer_slot=native_heap.initializer_target=0u;
        if(!setjmp(refusal)) {native_budget_prepare(&lease,&budget);CHECK(false);}
        CHECK(reports==1u && !budgets);
    }
    setup(false);native_budget_prepare(&lease,&budget);CHECK(budgets==1u && !reports);
    setup(false);budget_result=KUI_VMU_NATIVE_HEAP_BUDGET_FOUND;
    native_budget_prepare(&lease,&budget);CHECK(budgets==1u && !reports && budget.reserve_address==0x8c010050u);
    setup(false);budget_result=KUI_VMU_NATIVE_HEAP_BUDGET_INVALID;
    if(!setjmp(refusal)) {native_budget_prepare(&lease,&budget);CHECK(false);}
    CHECK(budgets==1u && reports==1u);
    setup(false);native_heap.family=99u;
    if(!setjmp(refusal)) {native_budget_prepare(&lease,&budget);CHECK(false);}
    CHECK(budgets==1u && reports==1u);CHECK(!munmap(ram,0x1000u));
    printf("Native installer allocator: %u checks; exact digest bounds and direct/dispatch budget admission\n",checks);
}
