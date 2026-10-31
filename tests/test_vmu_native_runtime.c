/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
struct word {uint32_t address,value;unsigned width;};
struct fixture {
    struct kui_vmu_native_discovery driver;
    struct kui_vmu_native_runtime_config config;
    struct kui_vmu_native_runtime_boundary boundary;
    struct word words[96];
    unsigned count,reads,fail_at,change_at;
};
static uint32_t canonical(uint32_t a) {
    uint32_t area=a&0xe0000000u,p=a&0x1fffffffu;
    return (area==0u || area==0x80000000u || area==0xa0000000u) &&
        p>=0x0c000000u && p<0x0d000000u?p:a;
}
static void put(struct fixture *f,uint32_t a,unsigned width,uint32_t value) {
    a=canonical(a);
    for(unsigned i=0;i<f->count;i++) if(f->words[i].address==a) {
        assert(f->words[i].width==width);f->words[i].value=value;return;
    }
    assert(f->count<sizeof(f->words)/sizeof(f->words[0]));
    f->words[f->count++]=(struct word){a,value,width};
}
static bool read_value(void *context,uint32_t a,unsigned width,uint32_t *value) {
    struct fixture *f=context;++f->reads;
    if(f->fail_at==f->reads) return false;
    a=canonical(a);
    for(unsigned i=0;i<f->count;i++) if(f->words[i].address==a && f->words[i].width==width) {
        *value=f->words[i].value;
        if(f->change_at==f->reads) *value^=1u;
        return true;
    }
    assert(!"unvalidated or unaligned read");return false;
}
static void fixture(struct fixture *f,uint32_t manager_alias,unsigned bank,unsigned phase) {
    memset(f,0,sizeof(*f));
    f->driver=(struct kui_vmu_native_discovery){
        .version=KUI_VMU_NATIVE_DISCOVERY_VERSION,.bytes=sizeof(f->driver),
        .family=KUI_VMU_NATIVE_FAMILY_UNIFORM_V1,.image_bytes=0x200000u,
        .load_address=0x8c010000u,.requirements=KUI_VMU_NATIVE_REQUIREMENTS,
        .manager_cell=0x0c3010fcu,.manager_initial=0x0c300000u,
        .lock_cell=0x0c011000u,.completion_call=0x8c012222u};
    f->config=(struct kui_vmu_native_runtime_config){
        .version=KUI_VMU_NATIVE_RUNTIME_VERSION,.bytes=sizeof(f->config),
        .lease_address=0x8c700000u,.lease_bytes=0x30000u,.lease_header_bytes=32u,
        .required_lease_bytes=0x20000u,.gun_flag=0x0c012300u,
        .timer1_callback=0x8c012304u,.timer2_callback=0xac012308u};
    f->boundary=(struct kui_vmu_native_runtime_boundary){
        .phase=phase,.worker_sr=0xf0u,.inherited_sr=phase?0xf0u:0x60u,
        .inherited_pr=0x0c012226u,.sdk_source_physical=0x0c400000u+bank*0x3000u,
        .dma_source_physical=0x0c400000u+bank*0x3000u,.dma_bytes=0x120u};
    uint32_t manager=manager_alias|0x0c300000u;
    put(f,0xa05f6c18u,4u,0u);put(f,0xa05f6c10u,4u,0u);
    put(f,0xa05f6c14u,4u,1u);put(f,0xa05f6c04u,4u,f->boundary.dma_source_physical);
    put(f,f->driver.manager_cell,4u,manager);put(f,f->driver.lock_cell,4u,0xac011004u);
    put(f,manager+0xfc4u,4u,0xac400000u);put(f,manager+0xfc8u,4u,0xac500000u);
    put(f,manager+0x10a8u,4u,0xac400000u);put(f,manager+0x10acu,4u,0xac403000u);
    put(f,manager+0x10b0u,4u,0xac400000u+bank*0x3000u);
    put(f,manager+0x10b4u,4u,bank^phase);put(f,manager+0x10b8u,4u,bank^phase);
    put(f,manager+0x10c4u,4u,0u);put(f,manager+0xfc0u,4u,phase?0u:1u);
    put(f,manager+0x10f4u,4u,0xa05f6c00u);put(f,0xac011004u,1u,0x80u);
    put(f,f->config.gun_flag,4u,0u);put(f,f->config.timer1_callback,4u,0u);
    put(f,f->config.timer2_callback,4u,0u);
    for(unsigned i=0;i<48u;i++) put(f,manager+0xfe8u+i*4u,4u,0xac500000u+i*1024u);
}
static void variable(struct fixture *f) {
    static const unsigned ids[12]={0,1,2,6,7,8,12,13,14,18,19,20};
    f->config.rx_layout=KUI_VMU_NATIVE_RX_EXPLICIT48;f->config.rx_base=0xac020020u;
    put(f,0x0c300fc8u,4u,f->config.rx_base);
    for(unsigned i=0;i<12u;i++) {
        f->config.rx_slots[ids[i]]=0xac500000u+i*0x820u;
        f->config.rx_slots[ids[i]+24u]=0xac500400u+i*0x820u;
    }
    for(unsigned i=0;i<48u;i++) put(f,0x0c300fe8u+i*4u,4u,f->config.rx_slots[i]);
}
/* A pd 1.46 boundary from an unmasked API wrapper: P1 lock cell, held lock. */
static void early(struct fixture *f) {
    fixture(f,0u,0u,0u);f->driver.family=KUI_VMU_NATIVE_FAMILY_EARLY_V1;
    f->driver.lock_cell=0x8c011000u;put(f,f->driver.lock_cell,4u,0x8c011004u);
    f->boundary.inherited_sr=0u;
}
static int run(struct fixture *f,struct kui_vmu_native_runtime_snapshot *out) {
    f->reads=0u;return kui_vmu_native_runtime_check(&f->driver,&f->config,&f->boundary,
        read_value,f,out);
}
static void reject(struct fixture *f,int expected) {
    struct kui_vmu_native_runtime_snapshot out,before;
    memset(&before,0xa5,sizeof(before));out=before;
    int result=run(f,&out);CHECK(result!=KUI_VMU_NATIVE_RUNTIME_VALID);
    if(expected) CHECK(result==expected);
    CHECK(!memcmp(&out,&before,sizeof(out)));
}
int main(void) {
    struct fixture f;struct kui_vmu_native_runtime_snapshot out;
    const uint32_t aliases[3]={0u,0x80000000u,0xa0000000u};
    /* Real games register/re-arm SDK timer callbacks after initial binding.
     * Their presence grants no executable or memory authority to the worker. */
    for(unsigned a=0;a<3u;a++) for(unsigned phase=0;phase<2u;phase++) {
        fixture(&f,0u,0u,phase);
        put(&f,f.config.timer1_callback,4u,aliases[a]|0x0c010000u);
        put(&f,f.config.timer2_callback,4u,aliases[a]|0x0c20fffeu);
        CHECK(run(&f,&out)==KUI_VMU_NATIVE_RUNTIME_VALID);
    }
    const uint32_t bad_timer[]={1u,0x8c00fffeu,0x8c210000u,0x8c20ffffu,
        0xec087498u,0x6c087498u,0xa05f6c00u,0xac700000u,0x8ce00000u};
    for(unsigned timer=0;timer<2u;timer++) for(unsigned i=0;i<sizeof(bad_timer)/sizeof(bad_timer[0]);i++) {
        fixture(&f,0u,0u,1u);
        put(&f,timer?f.config.timer2_callback:f.config.timer1_callback,4u,bad_timer[i]);
        reject(&f,KUI_VMU_NATIVE_RUNTIME_STATE);
    }
    CHECK(!kui_vmu_native_timer_callback_owned(NULL,0u));
    for(unsigned a=0;a<3u;a++) for(unsigned bank=0;bank<2u;bank++) for(unsigned phase=0;phase<2u;phase++) {
        fixture(&f,aliases[a],bank,phase);CHECK(run(&f,&out)==KUI_VMU_NATIVE_RUNTIME_VALID);
        CHECK(out.manager==(aliases[a]|0x0c300000u) && out.bank==bank);
        CHECK(out.lock==0xac011004u && out.rx_base==0xac500000u && out.tx_base==0xac400000u);
        CHECK(out.rx_slots_nonzero==48u && out.lease_guard_physical==0x0c6fffe0u);
        CHECK(out.lease_guard_bytes==0x30020u && f.driver.requirements==7u);
        variable(&f);CHECK(run(&f,&out)==KUI_VMU_NATIVE_RUNTIME_VALID);
        CHECK(out.rx_slots_nonzero==24u && out.rx_base==f.config.rx_base);
    }
    fixture(&f,0u,0u,0u);CHECK(run(&f,&out)==KUI_VMU_NATIVE_RUNTIME_VALID);
    unsigned total=f.reads;
    /* Fail every physical/MMIO read in turn: no later read or output write. */
    for(unsigned at=1u;at<=total;at++) {
        fixture(&f,0u,0u,0u);f.fail_at=at;reject(&f,KUI_VMU_NATIVE_RUNTIME_READ);
        CHECK(f.reads==at);
    }
    /* Change each mutable value actually sampled at a successful boundary. */
    for(unsigned at=1u;at<=total;at++) {
        fixture(&f,0u,0u,0u);f.change_at=at;reject(&f,0);
    }
    fixture(&f,0u,0u,1u);f.boundary.dma_source_physical=0x0c700020u;
    put(&f,0xa05f6c04u,4u,f.boundary.dma_source_physical);
    CHECK(run(&f,&out)==KUI_VMU_NATIVE_RUNTIME_VALID);
    /* Private DMA output may only lie inside the proven payload lease. */
    fixture(&f,0u,0u,1u);f.boundary.dma_source_physical=0x0c6fffe0u;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_POINTER);CHECK(!f.reads);
    fixture(&f,0u,0u,1u);f.boundary.dma_source_physical=0x0c72ffe0u;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_POINTER);CHECK(!f.reads);
    fixture(&f,0u,0u,0u);f.boundary.dma_bytes=8u;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_POINTER);CHECK(!f.reads);
    fixture(&f,0u,0u,0u);f.boundary.dma_bytes=0x3004u;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_POINTER);CHECK(!f.reads);
    fixture(&f,0u,0u,0u);f.boundary.dma_bytes=13u;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_POINTER);CHECK(!f.reads);
    fixture(&f,0u,0u,0u);f.boundary.dma_source_physical=0x0c700020u;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_POINTER);CHECK(!f.reads);
    fixture(&f,0u,0u,0u);f.config.lease_address=0xac500000u;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_CONFLICT);
    fixture(&f,0u,0u,0u);f.config.lease_address=0x0c506000u;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_CONFLICT);
    /* RX ending inside an allocator header is still an overlap. */
    fixture(&f,0u,0u,0u);f.config.lease_address=0xac50c000u;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_CONFLICT);
    fixture(&f,0u,0u,0u);f.config.lease_address=0x8c020000u;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_CONFLICT);CHECK(!f.reads);
    fixture(&f,0u,0u,0u);f.config.lease_address=0x8c100000u;
    f.config.lease_header_bytes=0x100001u;reject(&f,KUI_VMU_NATIVE_RUNTIME_INVALID);CHECK(!f.reads);
    fixture(&f,0u,0u,0u);f.config.lease_address=0x8cfe0000u;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_INVALID);CHECK(!f.reads);
    fixture(&f,0u,0u,0u);f.config.required_lease_bytes=f.config.lease_bytes+1u;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_INVALID);CHECK(!f.reads);
    fixture(&f,0u,0u,0u);f.config.timer1_callback=0xa05f6c00u;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_INVALID);CHECK(!f.reads);
    fixture(&f,0u,0u,0u);f.config.timer1_callback=f.config.timer2_callback;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_INVALID);CHECK(!f.reads);
    fixture(&f,0u,0u,0u);put(&f,f.driver.manager_cell,4u,0xa05f6c00u);
    reject(&f,KUI_VMU_NATIVE_RUNTIME_POINTER);CHECK(f.reads==6u);
    fixture(&f,0u,0u,0u);put(&f,f.driver.lock_cell,4u,0x8c011004u);
    reject(&f,KUI_VMU_NATIVE_RUNTIME_POINTER);CHECK(f.reads==6u);
    fixture(&f,0u,0u,0u);put(&f,0x0c300fc4u,4u,0x8c400000u);
    reject(&f,KUI_VMU_NATIVE_RUNTIME_POINTER);
    fixture(&f,0u,0u,0u);put(&f,0x0c300fc8u,4u,0xac400000u);
    reject(&f,KUI_VMU_NATIVE_RUNTIME_CONFLICT);
    fixture(&f,0u,0u,0u);f.boundary.worker_sr=0x60u;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_CONTEXT);CHECK(!f.reads);
    fixture(&f,0u,0u,0u);f.boundary.inherited_sr=0x50u;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_CONTEXT);CHECK(!f.reads);
    fixture(&f,0u,0u,1u);f.boundary.inherited_pr+=2u;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_CONTEXT);CHECK(!f.reads);
    fixture(&f,0u,0u,1u);f.boundary.inherited_pr=0xec012226u;
    reject(&f,KUI_VMU_NATIVE_RUNTIME_CONTEXT);CHECK(!f.reads);
    for(unsigned i=0;i<48u;i++) {
        fixture(&f,0u,0u,0u);variable(&f);
        put(&f,0x0c300fe8u+i*4u,4u,f.config.rx_slots[i]^32u);
        reject(&f,KUI_VMU_NATIVE_RUNTIME_STATE);
    }
    fixture(&f,0u,0u,0u);variable(&f);f.config.rx_slots[1]=f.config.rx_slots[0];
    put(&f,0x0c300fecu,4u,f.config.rx_slots[1]);reject(&f,KUI_VMU_NATIVE_RUNTIME_CONFLICT);
    fixture(&f,0u,0u,0u);variable(&f);f.config.rx_slots[0]=0xac6fffe0u;
    put(&f,0x0c300fe8u,4u,f.config.rx_slots[0]);reject(&f,KUI_VMU_NATIVE_RUNTIME_CONFLICT);
    fixture(&f,0u,0u,0u);variable(&f);f.config.rx_slots[0]=0x0c500000u;
    put(&f,0x0c300fe8u,4u,f.config.rx_slots[0]);reject(&f,KUI_VMU_NATIVE_RUNTIME_POINTER);
    fixture(&f,0u,0u,0u);variable(&f);memset(f.config.rx_slots,0,sizeof(f.config.rx_slots));
    for(unsigned i=0;i<48u;i++) put(&f,0x0c300fe8u+i*4u,4u,0u);
    reject(&f,KUI_VMU_NATIVE_RUNTIME_STATE);
    /* A pd 1.46 server runs at its caller's level holding its TAS lock, and
     * its lock cell keeps the image's cached pointer in either area. */
    const uint32_t early_lock[2]={0x0c011004u,0x8c011004u};
    for(unsigned a=0;a<2u;a++) for(unsigned phase=0;phase<2u;phase++) for(unsigned level=0;level<16u;level++) {
        fixture(&f,0u,0u,phase);f.driver.family=KUI_VMU_NATIVE_FAMILY_EARLY_V1;
        f.driver.lock_cell=early_lock[a]-4u;put(&f,f.driver.lock_cell,4u,early_lock[a]);
        f.boundary.inherited_sr=level<<4;
        CHECK(run(&f,&out)==KUI_VMU_NATIVE_RUNTIME_VALID && out.lock==early_lock[a]);
    }
    early(&f);CHECK(run(&f,&out)==KUI_VMU_NATIVE_RUNTIME_VALID);
    total=f.reads;
    for(unsigned at=1u;at<=total;at++) {
        early(&f);f.fail_at=at;reject(&f,KUI_VMU_NATIVE_RUNTIME_READ);CHECK(f.reads==at);
        early(&f);f.change_at=at;reject(&f,0);
    }
    /* Its P2 alias or another area is not the pointer it leaves; a free lock
     * means this server does not hold it. */
    const uint32_t early_wrong[3]={0xac011004u,0x0c011008u,0x8c011000u};
    for(unsigned i=0;i<3u;i++) {
        fixture(&f,0u,0u,0u);f.driver.family=KUI_VMU_NATIVE_FAMILY_EARLY_V1;f.driver.lock_cell=0x8c011000u;
        put(&f,f.driver.lock_cell,4u,early_wrong[i]);reject(&f,KUI_VMU_NATIVE_RUNTIME_POINTER);
    }
    early(&f);put(&f,0x8c011004u,1u,0u);reject(&f,KUI_VMU_NATIVE_RUNTIME_STATE);
    /* The pd 1.50 family keeps its inherited IMASK6/15 contract. */
    for(unsigned level=0;level<16u;level++) {
        if(level==6u || level==15u) continue;
        fixture(&f,0u,0u,0u);f.boundary.inherited_sr=level<<4;
        reject(&f,KUI_VMU_NATIVE_RUNTIME_CONTEXT);CHECK(!f.reads);
    }
    fixture(&f,0u,0u,0u);f.driver.family=3u;reject(&f,KUI_VMU_NATIVE_RUNTIME_INVALID);CHECK(!f.reads);
    fixture(&f,0u,0u,0u);f.driver.requirements=0u;reject(&f,KUI_VMU_NATIVE_RUNTIME_INVALID);CHECK(!f.reads);
    fixture(&f,0u,0u,0u);f.config.version++;reject(&f,KUI_VMU_NATIVE_RUNTIME_INVALID);CHECK(!f.reads);
    CHECK(kui_vmu_native_runtime_check(NULL,&f.config,&f.boundary,read_value,&f,&out)==KUI_VMU_NATIVE_RUNTIME_INVALID);
    CHECK(kui_vmu_native_runtime_check(&f.driver,&f.config,&f.boundary,NULL,&f,&out)==KUI_VMU_NATIVE_RUNTIME_INVALID);
    CHECK(kui_vmu_native_runtime_check(&f.driver,&f.config,&f.boundary,read_value,&f,NULL)==KUI_VMU_NATIVE_RUNTIME_INVALID);
    printf("Native runtime boundary validation: %u checks passed; no patch or ownership authority\n",checks);
    return 0;
}
