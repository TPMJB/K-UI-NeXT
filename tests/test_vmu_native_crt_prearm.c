/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the real prearm helper over mapped synthetic game RAM. Its entry
 * veneer must transfer to the retained low hook before the original CRT's
 * FPSCR/stack/BSS instructions can run. No retail bytes are embedded. */
#define _GNU_SOURCE
#include "kui/vmu_native_exit.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#define BASE UINT32_C(0x0c100000)
#define BYTES 4096u
#define NATIVE_LOW_RETURN_HOOK UINT32_C(0x8c004300)
static struct kui_vmu_native_exit native_exits;
static uint16_t persistent_exit_windows[KUI_VMU_NATIVE_EXIT_ENTRIES][6];
static unsigned checks,patches,publishes,fault;
#define CHECK(x) do {++checks;assert(x);} while(0)
static uint16_t half(uint32_t address) {
    const uint8_t *p=(const void *)(uintptr_t)address;
    return (uint16_t)(p[0]|(uint16_t)p[1]<<8);
}
static uint32_t word(uint32_t address) {
    return (uint32_t)half(address)|(uint32_t)half(address+2u)<<16;
}
static void readonly_patch_entry(uint32_t address,uint32_t target) {
    uint16_t patch[6];++patches;
    CHECK(kui_vmu_native_patch_encode(address,target,patch));
    if(fault==1u) return;
    memcpy((void *)(uintptr_t)address,patch,sizeof(patch));++publishes;
    if(fault==2u) ((uint8_t *)(uintptr_t)address)[11]^=1u;
}
#include "../src/loader/vmu_native_crt_bootstrap_stage.inc"

static void fixture(unsigned halfword) {
    memset((void *)(uintptr_t)BASE,0x5a,BYTES);
    memset(persistent_exit_windows,0,sizeof(persistent_exit_windows));
    native_exits=(struct kui_vmu_native_exit){.version=KUI_VMU_NATIVE_EXIT_VERSION,
        .bytes=sizeof(native_exits),.family=KUI_VMU_NATIVE_EXIT_CRT_BOOTSTRAP,
        .load=BASE,.image_bytes=BYTES,.count=4u,
        .entries={BASE+0x10u,BASE+0x40u,BASE+0x70u,BASE+0x100u+halfword*2u}};
    /* Synthetic initial FPSCR and stack setup. A correct cold veneer hides
     * all of these original instructions before the game is resumed. */
    const uint16_t original[6]={0x006au,0xc906u,0x406au,0xc700u,0x6f02u,0xd500u};
    memcpy((void *)(uintptr_t)native_exits.entries[3],original,sizeof(original));
    for(unsigned i=0;i<4u;i++)
        memcpy(persistent_exit_windows[i],(const void *)(uintptr_t)native_exits.entries[i],12u);
    patches=publishes=fault=0u;
}
static void execute_cold_entry(void) {
    uint32_t pc=native_exits.entries[3],sp=UINT32_C(0x0cf00000),fpscr=UINT32_C(0x00040001);
    uint8_t bss_before[32];memcpy(bss_before,(const void *)(uintptr_t)(BASE+0x800u),sizeof(bss_before));
    /* Independently decode the MOV.L/JMP/delay-slot semantics. Neither SP nor
     * FPSCR is assigned, and the original BSS loop is never reached. */
    uint16_t op=half(pc);CHECK((op&0xff00u)==0xd000u);
    uint32_t literal=((pc+4u)&~3u)+4u*(op&255u),target=word(literal);
    CHECK(half(pc+2u)==0x402bu && half(pc+4u)==0x0009u);
    pc=target;
    CHECK(pc==NATIVE_LOW_RETURN_HOOK && sp==UINT32_C(0x0cf00000) && fpscr==UINT32_C(0x00040001));
    CHECK(!memcmp(bss_before,(const void *)(uintptr_t)(BASE+0x800u),sizeof(bss_before)));
}
static void refused_unchanged(void) {
    uint8_t before[BYTES];uint16_t captured[4][6];
    memcpy(before,(const void *)(uintptr_t)BASE,sizeof(before));
    memcpy(captured,persistent_exit_windows,sizeof(captured));
    CHECK(!native_crt_bootstrap_prearm());CHECK(!patches && !publishes);
    CHECK(!memcmp(before,(const void *)(uintptr_t)BASE,sizeof(before)));
    CHECK(!memcmp(captured,persistent_exit_windows,sizeof(captured)));
}
int main(void) {
    void *ram=mmap((void *)(uintptr_t)BASE,BYTES,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    CHECK(ram==(void *)(uintptr_t)BASE);
    for(unsigned halfword=0u;halfword<2u;halfword++) {
        fixture(halfword);uint8_t before[BYTES];uint16_t captured[4][6];
        memcpy(before,ram,sizeof(before));memcpy(captured,persistent_exit_windows,sizeof(captured));
        CHECK(native_crt_bootstrap_prearm());CHECK(patches==1u && publishes==1u);
        execute_cold_entry();
        CHECK(!memcmp(persistent_exit_windows[3],(void *)(uintptr_t)native_exits.entries[3],12u));
        CHECK(!memcmp(persistent_exit_windows,captured,3u*12u));
        size_t offset=native_exits.entries[3]-BASE;
        CHECK(!memcmp(before,ram,offset));
        CHECK(!memcmp(before+offset+12u,(uint8_t *)ram+offset+12u,BYTES-offset-12u));
        /* A later installation sees the published veneer as its expected
         * window. Re-publication is safe and retains the same target. */
        CHECK(native_crt_bootstrap_prearm());CHECK(patches==2u && publishes==2u);
        execute_cold_entry();
    }
    for(unsigned byte=0u;byte<12u;byte++) for(unsigned bit=0u;bit<8u;bit++) {
        fixture(0u);((uint8_t *)(uintptr_t)native_exits.entries[3])[byte]^=(uint8_t)(1u<<bit);
        refused_unchanged();
    }
    for(unsigned invalid=0u;invalid<9u;invalid++) {
        fixture(0u);
        if(invalid==0u) native_exits.version=1u;
        else if(invalid==1u) native_exits.bytes=48u;
        else if(invalid==2u) native_exits.count=3u;
        else if(invalid==3u) native_exits.entries[3]=0u;
        else if(invalid==4u) native_exits.entries[3]=BASE-2u;
        else if(invalid==5u) native_exits.entries[3]=BASE+BYTES-10u;
        else if(invalid==6u) native_exits.entries[3]|=1u;
        else if(invalid==7u) native_exits.image_bytes=11u;
        else native_exits.count=5u;
        refused_unchanged();
    }
    for(unsigned kind=1u;kind<=2u;kind++) {
        fixture(0u);fault=kind;uint16_t captured[4][6];
        memcpy(captured,persistent_exit_windows,sizeof(captured));
        CHECK(!native_crt_bootstrap_prearm());CHECK(patches==1u && publishes==(kind==2u));
        CHECK(!memcmp(captured,persistent_exit_windows,sizeof(captured)));
    }
    fixture(0u);native_exits.family=KUI_VMU_NATIVE_EXIT_PVR_CLEAR;
    native_exits.count=3u;native_exits.entries[3]=0u;
    uint8_t before[BYTES];uint16_t captured[4][6];
    memcpy(before,ram,sizeof(before));memcpy(captured,persistent_exit_windows,sizeof(captured));
    CHECK(native_crt_bootstrap_prearm());CHECK(!patches && !publishes);
    CHECK(!memcmp(before,ram,sizeof(before)) && !memcmp(captured,persistent_exit_windows,sizeof(captured)));
    CHECK(munmap(ram,BYTES)==0);
    printf("Native CRT cold prearm: %u checks; cold and late entry, 96 source mutations, schema/span refusals\n",checks);
    return 0;
}
