/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include "kui/disc_vmu_entry.h"
#include "kui/vmu_native_exit.h"
#include "kui/vmu_native_discovery.h"
#include "kui/vmu_native_heap.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <setjmp.h>

#define KUI_RETAIL_EXEC_ADDRESS UINT32_C(0x8c010000)
#define FRAME UINT32_C(0x8c00e800)
#define EXIT UINT32_C(0x8c018000)
#define STAGE_RETURN UINT32_C(0x8ce00100)
#define OWNED_RETURN UINT32_C(0x8c312100)
#define kui_vmu_observe_return_hook ((void (*)(void))(uintptr_t)STAGE_RETURN)
#define NATIVE_LOW_RETURN_HOOK native_terminal
#define KUI_VMU_NATIVE_PERSISTENT_PROBE 1
static struct kui_vmu_native_exit native_exits;
static struct kui_vmu_native_discovery native_driver;
static struct kui_vmu_native_heap native_heap;
static uint32_t exec_bytes,native_terminal,native_disc_entry_return_frame,native_disc_entry_return_entry;
static uint8_t original_entry[128];
static uint16_t persistent_exit_windows[KUI_VMU_NATIVE_EXIT_ENTRIES][6];
static unsigned checks,patch_calls,word_reads;
static bool skip_patch,damage_patch,damage_head,fail_frame_read;
static bool expect_report;
static jmp_buf report_return;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"%s:%u: %s\n",__FILE__,__LINE__,#x);exit(1);}} while(0)
static uint32_t native_word(uint32_t address) {
    ++word_reads;uint32_t value=*(volatile const uint32_t *)(uintptr_t)address;
    return fail_frame_read && address==((FRAME|UINT32_C(0x20000000))+20u)?value^1u:value;
}
static void native_report(uint32_t phase,uint32_t detail);
static void kui_native_passive_publish(uint32_t first,uint32_t last) {(void)first;(void)last;}
#include "disc-vmu-exit-writer-fixture.h"
static void readonly_patch_entry(uint32_t address,uint32_t target) {
    ++patch_calls;
    if(skip_patch) return;
    actual_patch_entry(address,target);
    if(damage_patch) ((uint8_t *)(uintptr_t)address)[0]^=1u;
    if(damage_head) ((uint8_t *)(uintptr_t)native_exits.entries[0])[0]^=1u;
}
static void native_report(uint32_t phase,uint32_t detail) {
    CHECK(expect_report && phase==9u && detail==7u);
    longjmp(report_return,1);
}
#include "../src/loader/disc_vmu_return_stage.inc"
#include "disc-vmu-exit-loop-fixture.h"

static void setup(void) {
    memset((void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,0,0x10000u);
    memset(&native_exits,0,sizeof(native_exits));
    memset(&native_driver,0,sizeof(native_driver));memset(&native_heap,0,sizeof(native_heap));
    native_exits=(struct kui_vmu_native_exit){.version=KUI_VMU_NATIVE_EXIT_VERSION,
        .bytes=sizeof(native_exits),.family=KUI_VMU_NATIVE_EXIT_FAMILY,
        .load=KUI_RETAIL_EXEC_ADDRESS,.image_bytes=0x10000u,.count=3u,
        .entries={EXIT,EXIT+0x100u,EXIT+0x200u,0u}};
    native_driver.submit=KUI_RETAIL_EXEC_ADDRESS+0x3000u;
    native_heap.initializer=KUI_RETAIL_EXEC_ADDRESS+0x2000u;
    native_heap.release=KUI_RETAIL_EXEC_ADDRESS+0x2400u;
    exec_bytes=native_exits.image_bytes;native_terminal=STAGE_RETURN;
    skip_patch=damage_patch=damage_head=fail_frame_read=expect_report=false;patch_calls=word_reads=0u;
    for(unsigned i=0u;i<4u;i++) for(unsigned j=0u;j<6u;j++) persistent_exit_windows[i][j]=(uint16_t)(0xe000u+i*16u+j);
    for(unsigned i=0u;i<native_exits.count;i++)
        memcpy((void *)(uintptr_t)native_exits.entries[i],persistent_exit_windows[i],12u);
    uint32_t *frame=(void *)(uintptr_t)FRAME;
    for(unsigned i=0u;i<21u;i++) frame[i]=UINT32_C(0x12340000)+i;
    frame[5]=UINT32_C(0x8ce01234);
    const uint16_t startup[]={0x2fe6u,0x4f22u};
    memcpy((void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,startup,sizeof(startup));
}
static void unchanged(uint32_t *frame,const uint32_t before[21]) {
    for(unsigned i=0u;i<21u;i++) CHECK(frame[i]==before[i]);
}
static void refused_before_write(uint32_t *frame) {
    uint32_t before[21];memcpy(before,(const void *)(uintptr_t)FRAME,sizeof(before));
    CHECK(!native_disc_entry_return_bind(frame));
    CHECK(!patch_calls && !word_reads);
    unchanged((void *)(uintptr_t)FRAME,before);
}
static void photographed_case_and_retarget(void) {
    setup();struct kui_disc_vmu_entry_failure failure;
    CHECK(!kui_disc_vmu_entry_drops_inherited_pr((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
        exec_bytes,KUI_RETAIL_EXEC_ADDRESS,&failure));
    CHECK(failure.address==KUI_RETAIL_EXEC_ADDRESS+2u && failure.opcode==0x4f22u &&
        failure.reason==KUI_DISC_VMU_ENTRY_PR_READ);
    uint32_t *frame=(void *)(uintptr_t)FRAME,before[21];memcpy(before,frame,sizeof(before));
    CHECK(native_disc_entry_return_bind(frame));CHECK(!native_disc_entry_return_error);
    CHECK(patch_calls==1u && word_reads==1u && frame[5]==EXIT);
    for(unsigned i=0u;i<21u;i++) CHECK(frame[i]==(i==5u?EXIT:before[i]));
    uint16_t stage_patch[6];CHECK(kui_vmu_native_patch_encode(EXIT,STAGE_RETURN,stage_patch));
    CHECK(!memcmp((const void *)(uintptr_t)EXIT,stage_patch,12u));
    CHECK(!memcmp(persistent_exit_windows[0],stage_patch,12u));
    /* The original MOV.L R14,@-SP / STS.L PR,@-SP prologue now saves a
     * game-owned PR. No later attempt to find/rewrite this reused slot occurs. */
    uint32_t *sp=(void *)(uintptr_t)(FRAME+84u);
    *--sp=frame[6];*--sp=frame[5];uint32_t *saved_pr=sp;
    CHECK(*saved_pr==EXIT);
    /* Execute the unchanged production install loop extracted by the runner.
     * The prearmed window must match exactly before the owned retarget. */
    native_terminal=OWNED_RETURN;
    actual_installer_exit_loop();
    CHECK(*saved_pr==EXIT);
    uint16_t owned_patch[6];CHECK(kui_vmu_native_patch_encode(*saved_pr,OWNED_RETURN,owned_patch));
    CHECK(!memcmp((const void *)(uintptr_t)*saved_pr,owned_patch,12u));
    CHECK(!memcmp((const void *)(uintptr_t)(*saved_pr|UINT32_C(0x20000000)),owned_patch,12u));
}
static void frame_admission(void) {
    const uintptr_t invalid[]={0u,1u,FRAME+2u,0x8c0080fcu,0x8c00f3b0u,0x9c00e800u,
        UINT64_C(0x18c00e800)};
    for(unsigned i=0u;i<sizeof(invalid)/sizeof(invalid[0]);i++) {setup();refused_before_write((void *)invalid[i]);}
    const uint32_t valid[]={FRAME,FRAME|UINT32_C(0x20000000),FRAME&UINT32_C(0x1fffffff),
        UINT32_C(0x8c008100),UINT32_C(0x8c00f3ac)};
    for(unsigned i=0u;i<sizeof(valid)/sizeof(valid[0]);i++) {
        setup();CHECK(native_disc_entry_return_bind((void *)(uintptr_t)valid[i]));
        CHECK(*(const uint32_t *)(uintptr_t)(((valid[i]&UINT32_C(0x1fffffff))|UINT32_C(0xa0000000))+20u)==EXIT);
    }
}
static void immutable_admission(void) {
    for(unsigned mode=0u;mode<16u;mode++) {
        setup();
        switch(mode) {
        case 0:native_exits.version=0u;break;case 1:native_exits.count=4u;break;
        case 2:native_exits.load+=4u;break;case 3:--native_exits.image_bytes;break;
        case 4:exec_bytes=128u;native_exits.image_bytes=exec_bytes;break;
        case 5:native_terminal+=2u;break;case 6:native_exits.entries[0]|=UINT32_C(0x20000000);break;
        case 7:++native_exits.entries[0];break;case 8:native_exits.entries[0]=KUI_RETAIL_EXEC_ADDRESS;break;
        case 9:native_exits.entries[0]=KUI_RETAIL_EXEC_ADDRESS+exec_bytes-10u;break;
        case 10:native_driver.submit=EXIT+2u;break;
        case 11:native_heap.initializer=EXIT-0x100u;break;
        case 12:native_heap.release=EXIT-16u;break;
        case 13:native_exits.entries[1]=EXIT+10u;break;
        case 14:persistent_exit_windows[0][2]^=1u;break;
        case 15:((uint16_t *)(uintptr_t)EXIT)[5]^=1u;break;
        }
        refused_before_write((void *)(uintptr_t)FRAME);
    }
    setup();native_exits.family=KUI_VMU_NATIVE_EXIT_PAIR_ONLY;native_exits.count=2u;native_exits.entries[2]=0u;
    CHECK(native_disc_entry_return_bind((void *)(uintptr_t)FRAME));
    setup();native_exits.family=KUI_VMU_NATIVE_EXIT_PVR_CLEAR;native_exits.entries[0]=EXIT+2u;
    memcpy((void *)(uintptr_t)native_exits.entries[0],persistent_exit_windows[0],12u);
    CHECK(native_disc_entry_return_bind((void *)(uintptr_t)FRAME));
    uint16_t halfword_patch[6];CHECK(kui_vmu_native_patch_encode(EXIT+2u,STAGE_RETURN,halfword_patch));
    CHECK(!memcmp((const void *)(uintptr_t)(EXIT+2u),halfword_patch,12u));
    setup();native_exits.family=KUI_VMU_NATIVE_EXIT_CRT_BOOTSTRAP;native_exits.count=4u;native_exits.entries[3]=EXIT+0x300u;
    memcpy((void *)(uintptr_t)native_exits.entries[3],persistent_exit_windows[3],12u);
    CHECK(native_disc_entry_return_bind((void *)(uintptr_t)FRAME));
}
static void setup_early_pair(uint32_t alignment) {
    setup();native_exits.family=KUI_VMU_NATIVE_EXIT_EARLY_PAIR;
    native_exits.count=2u;native_exits.entries[0]=EXIT+alignment;
    native_exits.entries[1]=EXIT+alignment+8u;native_exits.entries[2]=0u;
    uint16_t original[10];for(unsigned i=0;i<10u;i++) original[i]=(uint16_t)(0xe100u+i);
    memcpy((void *)(uintptr_t)native_exits.entries[0],original,sizeof(original));
    for(unsigned i=0;i<2u;i++)
        memcpy(persistent_exit_windows[i],(const void *)(uintptr_t)native_exits.entries[i],12u);
}
static void early_pair_return_and_retarget(void) {
    for(uint32_t alignment=0u;alignment<=2u;alignment+=2u) {
        setup_early_pair(alignment);
        uint32_t *frame=(void *)(uintptr_t)FRAME,before[21];memcpy(before,frame,sizeof(before));
        uint32_t entry=native_exits.entries[1];uint16_t head[4],patch[6];
        CHECK(native_disc_entry_return_bind(frame));
        CHECK(frame[5]==entry && native_disc_entry_return_entry==entry && !native_disc_entry_return_error);
        for(unsigned i=0;i<21u;i++) CHECK(frame[i]==(i==5u?entry:before[i]));
        CHECK(kui_vmu_native_patch_encode_head(native_exits.entries[0],head));
        CHECK(kui_vmu_native_patch_encode(entry,STAGE_RETURN,patch));
        CHECK(!memcmp((const void *)(uintptr_t)native_exits.entries[0],head,sizeof(head)));
        CHECK(!memcmp((const void *)(uintptr_t)entry,patch,sizeof(patch)));
        for(unsigned i=0;i<2u;i++)
            CHECK(!memcmp(persistent_exit_windows[i],(const void *)(uintptr_t)native_exits.entries[i],12u));
        native_terminal=OWNED_RETURN;actual_installer_exit_loop();
        CHECK(frame[5]==entry && patch_calls==3u);
        CHECK(kui_vmu_native_patch_encode(entry,OWNED_RETURN,patch));
        CHECK(!memcmp((const void *)(uintptr_t)(entry|UINT32_C(0x20000000)),patch,sizeof(patch)));
        CHECK(!memcmp((const void *)(uintptr_t)native_exits.entries[0],head,sizeof(head)));
    }
    /* The companion head is also written, so it needs the full ownership and
     * immutable-window proof even when the selected veneer is disjoint. */
    for(unsigned mode=0u;mode<6u;mode++) {
        setup_early_pair(0u);
        switch(mode) {
        case 0:native_driver.submit=EXIT-4u;break;
        case 1:persistent_exit_windows[0][0]^=1u;break;
        case 2:persistent_exit_windows[1][5]^=1u;break;
        case 3:native_exits.entries[1]+=2u;break;
        case 4:native_heap.release=EXIT-16u;break;
        case 5:native_exits.entries[0]|=UINT32_C(0x20000000);
            native_exits.entries[1]|=UINT32_C(0x20000000);break;
        }
        refused_before_write((void *)(uintptr_t)FRAME);
    }
    for(unsigned mode=0u;mode<3u;mode++) {
        setup_early_pair(0u);uint32_t before[21];memcpy(before,(void *)(uintptr_t)FRAME,sizeof(before));
        skip_patch=mode==0u;damage_patch=mode==1u;damage_head=mode==2u;
        CHECK(!native_disc_entry_return_bind((void *)(uintptr_t)FRAME));
        CHECK(native_disc_entry_return_error==0x73u && patch_calls==1u);
        unchanged((void *)(uintptr_t)FRAME,before);
    }
    setup_early_pair(0u);CHECK(native_disc_entry_return_bind((void *)(uintptr_t)FRAME));
    ((uint8_t *)(uintptr_t)native_exits.entries[0])[0]^=1u;
    native_terminal=OWNED_RETURN;expect_report=true;
    if(!setjmp(report_return)) {actual_installer_exit_loop();CHECK(false);}
    CHECK(patch_calls==1u);
}
static void failed_publication(void) {
    for(unsigned mode=0u;mode<3u;mode++) {
        setup();uint32_t before[21];memcpy(before,(const void *)(uintptr_t)FRAME,sizeof(before));
        skip_patch=mode==0u;damage_patch=mode==1u;fail_frame_read=mode==2u;
        CHECK(!native_disc_entry_return_bind((void *)(uintptr_t)FRAME));CHECK(patch_calls==1u);
        CHECK(native_disc_entry_return_error==(mode==2u?0x74u:0x73u));
        if(mode<2u) unchanged((void *)(uintptr_t)FRAME,before);
        else {CHECK(((uint32_t *)(uintptr_t)FRAME)[5]==EXIT);CHECK(word_reads==1u);}
    }
    setup();CHECK(native_disc_entry_return_bind((void *)(uintptr_t)FRAME));
    ((uint8_t *)(uintptr_t)EXIT)[0]^=1u;
    native_terminal=OWNED_RETURN;expect_report=true;
    if(!setjmp(report_return)) {actual_installer_exit_loop();CHECK(false);}
    CHECK(patch_calls==1u); /* The stale first window is refused before retarget. */
}
int main(void) {
    FILE *backing=tmpfile();CHECK(backing && !ftruncate(fileno(backing),0x1000000));
    for(unsigned i=0u;i<2u;i++) {
        void *at=(void *)(uintptr_t)(i?UINT32_C(0xac000000):UINT32_C(0x8c000000));
        CHECK(mmap(at,0x1000000u,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED_NOREPLACE,fileno(backing),0)==at);
    }
    photographed_case_and_retarget();frame_admission();immutable_admission();
    early_pair_return_and_retarget();failed_publication();
    CHECK(!munmap((void *)(uintptr_t)UINT32_C(0x8c000000),0x1000000u));
    CHECK(!munmap((void *)(uintptr_t)UINT32_C(0xac000000),0x1000000u));CHECK(!fclose(backing));
    printf("Physical-disc entry return binder: %u checks; photographed4F22, exactframePR, checked prearm and owned retarget, invalid windows and publication faults\n",checks);
    return 0;
}
