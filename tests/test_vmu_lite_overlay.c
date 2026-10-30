/* SPDX-License-Identifier: GPL-3.0-only */
/* VMU-lite stages one virtual reply. A2 owns one receive slot per bank, so a
 * second A2 target in one batch is refused before any backend call or store;
 * a single READ or WRITE still stages, calls the backend once and publishes. */
#include "kui/vmu_readonly_overlay.h"
#include "kui/vmu_readonly.h"
#include "kui/vmu_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

_Static_assert(KUI_VMU_READONLY_OVERLAY_STAGED_FRAMES==1u,"Build with the VMU-lite selector");
#define A2 2u
#define MEMCARD UINT32_C(0x02000000)
#define CONTROLLER UINT32_C(0x01000000)

static struct kui_vmu_readonly_overlay overlay;
static struct kui_vmu_readonly card;
static struct kui_vmu_readonly_overlay_frame frames[4];
_Alignas(32) static uint32_t rx[4][256],before[4][256];
static uint8_t write_payload[128],expected_write_payload[128];
static unsigned calls,checks;
#define CHECK(x) do {++checks;assert(x);} while(0)

static int read_handler(void *context,const uint8_t *request,size_t request_bytes,
    uint8_t *reply,size_t capacity,size_t *reply_bytes) {
    ++calls;CHECK(!memcmp(rx,before,sizeof(rx)));
    return kui_vmu_readonly_handle(context,request,request_bytes,reply,capacity,reply_bytes);
}
/* The WRITE request reaches the backend from the single staging slot. */
static int write_handler(void *context,const uint8_t *request,size_t request_bytes,
    uint8_t *reply,size_t capacity,size_t *reply_bytes) {
    (void)context;++calls;CHECK(!memcmp(rx,before,sizeof(rx)));
    CHECK(request_bytes==4u+4u*34u && capacity>=4u);
    CHECK(request[0]==12u && request[1]==A2 && request[3]==34u);
    CHECK(!memcmp(request+12u,expected_write_payload,sizeof(expected_write_payload)));
    CHECK(memcmp(request+12u,write_payload,sizeof(write_payload)));
    reply[0]=7u;reply[1]=0u;reply[2]=A2;reply[3]=0u;*reply_bytes=4u;
    return 1;
}
static int admit(void *context,uint32_t request,uint32_t payload0,uint32_t payload1) {
    (void)context;(void)request;(void)payload0;(void)payload1;return 1;
}
static uint32_t header(unsigned cmd,unsigned address,unsigned words) {
    return cmd|(address<<8)|((address&192u)<<16)|(words<<24);
}
static void frame(unsigned index,unsigned cmd,unsigned address,unsigned words,
    uint32_t payload0,uint32_t payload1) {
    frames[index]=(struct kui_vmu_readonly_overlay_frame){header(cmd,address,words),
        payload0,payload1,0x0c200000u+1024u*index,1024u,(uint8_t *)(void *)rx[index],NULL};
}
static void fresh(kui_vmu_readonly_overlay_handler handler) {
    calls=0u;
    CHECK(!kui_vmu_readonly_init_address(&card,A2));
    CHECK(!kui_vmu_readonly_overlay_init_backend_address(&overlay,A2,handler,admit,&card));
    memset(rx,0xa5,sizeof(rx));
    frame(0,9u,32u,1u,CONTROLLER,0);
    /* Physical GETCOND reply from main A with no attached subunits. */
    rx[0][0]=8u|(32u<<16)|(3u<<24);rx[0][1]=CONTROLLER;
    memcpy(before,rx,sizeof(rx));
}
static int prepare(unsigned count) {
    return kui_vmu_readonly_overlay_prepare(&overlay,frames,count,
        0x0c100000u,4096u,0x0cfcffe0u,0x12020u);
}

static void one_read_publishes(void) {
    uint8_t expected[512];
    for(unsigned block=0u;block<256u;block+=51u) {
        fresh(read_handler);frame(1,11u,A2,2u,MEMCARD,block<<24);
        CHECK(!prepare(2u) && overlay.pending && overlay.target_count==1u);
        CHECK(!memcmp(rx,before,sizeof(rx)) && !calls);
        CHECK(!kui_vmu_readonly_overlay_complete(&overlay) && calls==1u);
        CHECK(!kui_vmu_image_blank_block((uint16_t)block,expected));
        CHECK(rx[1][0]==0x82020008u && rx[1][1]==MEMCARD && rx[1][2]==block<<24);
        CHECK(!memcmp((uint8_t *)rx[1]+12u,expected,512u));
        CHECK(rx[0][0]==(before[0][0]|(A2<<16)) && !memcmp(rx[0]+1,before[0]+1,1020u));
        CHECK(!memcmp(rx[2],before[2],sizeof(rx[2])) && !memcmp(rx[3],before[3],sizeof(rx[3])));
    }
}

static void second_target_refused(void) {
    const unsigned commands[2]={11u,10u};
    for(unsigned second=0u;second<2u;second++) {
        fresh(read_handler);frame(1,11u,A2,2u,MEMCARD,7u<<24);
        frame(2,commands[second],A2,2u,MEMCARD,commands[second]==11u?8u<<24:0u);
        CHECK(prepare(3u)==KUI_VMU_OVERLAY_REQUEST);
        CHECK(!overlay.admitted && !overlay.pending && !calls);
        CHECK(overlay.stats.flags&KUI_VMU_OVERLAY_BAD_REQUEST);
        CHECK(!memcmp(rx,before,sizeof(rx)));
        /* A refused overlay stays refused: completion cannot publish. */
        CHECK(kui_vmu_readonly_overlay_complete(&overlay)<0 && !memcmp(rx,before,sizeof(rx)));
    }
}

static void one_write_uses_the_single_slot(void) {
    for(unsigned i=0u;i<sizeof(expected_write_payload);i++) expected_write_payload[i]=(uint8_t)(i*7u+3u);
    for(unsigned phase=0u;phase<4u;phase++) {
        memcpy(write_payload,expected_write_payload,sizeof(write_payload));
        fresh(write_handler);frame(1,12u,A2,34u,MEMCARD,(40u<<24)|(phase<<8));
        frames[1].write_payload=write_payload;
        CHECK(!prepare(2u) && !calls);
        /* The plan owns its own snapshot, never the caller's payload. */
        memset(write_payload,0,sizeof(write_payload));
        CHECK(!kui_vmu_readonly_overlay_complete(&overlay) && calls==1u);
        CHECK(rx[1][0]==0x00020007u && !memcmp(rx[1]+1,before[1]+1,1020u));
        CHECK(rx[0][0]==(before[0][0]|(A2<<16)));
    }
}

int main(void) {
    one_read_publishes();second_target_refused();one_write_uses_the_single_slot();
    printf("VMU-lite overlay: %u checks; one staged reply, a second A2 target refused before any call or store\n",
        checks);
    return 0;
}
