/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_readonly_overlay.h"
#include "kui/vmu_readonly.h"
#include "kui/vmu_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct kui_vmu_readonly_overlay overlay;
static struct kui_vmu_readonly card;
static struct kui_vmu_readonly_overlay_frame frames[32];
_Alignas(32) static uint32_t rx[32][256],before[32][256];
static unsigned calls,fail_call,corrupt;

static int handler(void *context,const uint8_t *request,size_t request_bytes,
    uint8_t *reply,size_t capacity,size_t *reply_bytes) {
    ++calls;
    /* Even a later backend failure must find ALL external responses intact. */
    assert(!memcmp(rx,before,sizeof(rx)));
    if(calls==fail_call) return 0;
    int result=kui_vmu_readonly_handle(context,request,request_bytes,reply,capacity,reply_bytes);
    if(result==1 && corrupt) {
        if(corrupt==1u) reply[1]=1u;
        if(corrupt==2u) reply[3]^=1u;
        if(corrupt==3u) reply[4]^=1u;
        if(corrupt==4u) *reply_bytes=SIZE_MAX;
        if(corrupt==5u && request[0]==11u) reply[8]^=1u;
    }
    return result;
}
static uint32_t header(unsigned cmd,unsigned address,unsigned words) {
    return cmd|(address<<8)|((address&192u)<<16)|(words<<24);
}
static void frame(unsigned index,unsigned cmd,unsigned address,unsigned words,
    uint32_t payload0,uint32_t payload1) {
    frames[index]=(struct kui_vmu_readonly_overlay_frame){header(cmd,address,words),
        payload0,payload1,0x0c200000u+1024u*index,1024u,(uint8_t *)(void *)rx[index],NULL};
}
static void physical(unsigned parent_command,unsigned subunits) {
    memset(rx,0xa5,sizeof(rx));
    for(unsigned i=0;i<32u;i++) rx[i][0]=UINT32_MAX;
    unsigned response=parent_command==1u?5u:parent_command==2u?6u:8u;
    unsigned words=parent_command==1u?28u:parent_command==2u?48u:3u;
    rx[0][0]=response|((32u|subunits)<<16)|(words<<24);
    rx[0][1]=0x01000000u;
}
static void fresh(void) {
    calls=fail_call=corrupt=0;
    assert(!kui_vmu_readonly_init(&card));
    assert(!kui_vmu_readonly_overlay_init(&overlay,handler,&card));
    for(unsigned i=0;i<32u;i++) frame(i,1u,1u,0,0,0);
    frame(0,1u,32u,0,0,0);physical(1u,0);
}
static int prepare(unsigned count) {
    return kui_vmu_readonly_overlay_prepare(&overlay,frames,count,
        0x0c100000u,4096u,0x0cfcffe0u,0x10020u);
}
static void capture(void) {memcpy(before,rx,sizeof(rx));}
static void unchanged(void) {
    assert(!memcmp(rx,before,sizeof(rx)));
    assert(!overlay.admitted && !overlay.pending && overlay.stats.failures==1u);
    assert(card.commit_attempts==0u);
}
static void prepare_refused(unsigned count) {
    capture();assert(prepare(count)<0);unchanged();assert(!calls);
}
static void completion_refused(unsigned count) {
    capture();assert(!prepare(count));assert(kui_vmu_readonly_overlay_complete(&overlay)<0);
    unchanged();
}

static void discovery_and_all_blank_reads(void) {
    fresh();capture();assert(!prepare(1u));
    assert(!memcmp(rx,before,sizeof(rx)) && overlay.pending);
    assert(!kui_vmu_readonly_overlay_complete(&overlay));
    assert(rx[0][0]==(before[0][0]|0x00010000u) && !calls);
    assert(!memcmp((uint8_t *)rx[0]+4u,(uint8_t *)before[0]+4u,1020u));
    assert(overlay.admitted && !overlay.pending && overlay.stats.advertisements==1u);
    const unsigned cmds[3]={1u,2u,10u},sizes[3]={116u,196u,32u};
    for(unsigned t=0;t<3u;t++) {
        frame(1,cmds[t],1u,cmds[t]==10u?2u:0u,0x02000000u,0);
        physical(1u,2u);capture();assert(!prepare(2u));
        assert(!kui_vmu_readonly_overlay_complete(&overlay));
        assert(rx[0][0]==(before[0][0]|0x00010000u)); /* Preserve physical A2. */
        assert((rx[1][0]&0xffffffu)==(cmds[t]==1u?0x00010005u:cmds[t]==2u?0x00010006u:0x00010008u));
        assert(!memcmp((uint8_t *)rx[1]+sizes[t],(uint8_t *)before[1]+sizes[t],1024u-sizes[t]));
    }
    uint8_t expected[512];
    for(unsigned block=0;block<256u;block++) {
        frame(0,9u,32u,1u,0x01000000u,0);
        frame(1,11u,1u,2u,0x02000000u,block<<24);
        /* A malformed unrelated physical reply must remain entirely ignored. */
        frame(2,9u,0x60u,1u,0x01000000u,0);
        physical(9u,0);rx[2][0]=0xdeadc0deu;capture();
        assert(!prepare(3u));
        /* The plan owns request snapshots, not pointers back to caller metadata. */
        frames[1].payload1=UINT32_MAX;
        assert(!kui_vmu_readonly_overlay_complete(&overlay));
        assert(!kui_vmu_image_blank_block((uint16_t)block,expected));
        assert(rx[1][0]==0x82010008u && rx[1][1]==0x02000000u && rx[1][2]==block<<24);
        assert(!memcmp((uint8_t *)rx[1]+12u,expected,512u));
        assert(!memcmp(rx[2],before[2],1024u));
    }
    assert(overlay.stats.reads==256u && card.reads==256u && !card.commit_attempts);
    assert(overlay.stats.devinfo==1u && overlay.stats.allinfo==1u && overlay.stats.media==1u);
    assert(overlay.stats.replies==259u && overlay.stats.completed==260u);
}

static void virtual_read_batch(void) {
    frame(0,9u,32u,1u,0x01000000u,0);
    frame(1,11u,1u,2u,0x02000000u,0xff000000u);
    physical(9u,0);capture();assert(!prepare(2u));
    assert(!kui_vmu_readonly_overlay_complete(&overlay));
    assert(overlay.stats.last_request==header(11u,1u,2u));
    assert(overlay.stats.last_reply==0x82010008u);
    assert(overlay.stats.last_rx==frames[1].rx_physical);
}

static void request_reply_rx_telemetry_stays_correlated(void) {
    fresh();virtual_read_batch();
    physical(9u,0);capture();assert(!prepare(1u));
    assert(overlay.stats.last_request==header(9u,32u,1u));
    assert(!overlay.stats.last_reply && overlay.stats.last_rx==frames[0].rx_physical);
    assert(!kui_vmu_readonly_overlay_complete(&overlay));
    assert(overlay.stats.last_request==header(9u,32u,1u));
    assert(overlay.stats.last_reply==0x03210008u); /* Published controller DATA/3. */
    assert(overlay.stats.last_rx==frames[0].rx_physical);
    assert(rx[0][0]==(before[0][0]|0x00010000u));
    assert(!memcmp((uint8_t *)rx[0]+4u,(uint8_t *)before[0]+4u,1020u));
    assert(!memcmp(rx[1],before[1],sizeof(rx)-sizeof(rx[0])));

    fresh();virtual_read_batch();
    frame(1,12u,1u,0,0,0);physical(9u,0);capture();
    assert(prepare(2u)==KUI_VMU_OVERLAY_REQUEST);unchanged();
    assert(overlay.stats.last_request==header(12u,1u,0));
    assert(!overlay.stats.last_reply && overlay.stats.last_rx==frames[1].rx_physical);

    fresh();virtual_read_batch();
    physical(9u,0);rx[0][0]=UINT32_MAX;completion_refused(1u);
    assert(overlay.stats.last_request==header(9u,32u,1u));
    assert(overlay.stats.last_reply==UINT32_MAX && overlay.stats.last_rx==frames[0].rx_physical);

    fresh();virtual_read_batch();
    physical(9u,0);rx[1][0]=0x000100fbu;completion_refused(2u);
    assert(overlay.stats.last_request==header(11u,1u,2u));
    assert(overlay.stats.last_reply==0x000100fbu && overlay.stats.last_rx==frames[1].rx_physical);

    fresh();virtual_read_batch();
    physical(9u,0);fail_call=calls+1u;completion_refused(2u);
    assert(overlay.stats.last_request==header(11u,1u,2u));
    assert(!overlay.stats.last_reply && overlay.stats.last_rx==frames[1].rx_physical);
}

static void strict_request_and_capacity_guards(void) {
    for(unsigned cmd=0;cmd<256u;cmd++) {
        if(cmd==1u || cmd==2u || cmd==10u || cmd==11u) continue;
        fresh();frame(1,cmd,1u,0,0,0);prepare_refused(2u);
        assert(overlay.stats.flags&KUI_VMU_OVERLAY_BAD_REQUEST);
    }
    const uint32_t bad_locations[]={1u,0x100u,0x10000u,0x01010000u};
    for(unsigned i=0;i<4u;i++) {
        fresh();frame(1,11u,1u,2u,0x02000000u,bad_locations[i]);prepare_refused(2u);
    }
    fresh();frame(1,10u,1u,2u,0x02000000u,0x01000000u);prepare_refused(2u);
    fresh();frame(1,11u,1u,2u,0x01000000u,0);prepare_refused(2u);
    fresh();frame(1,1u,1u,1u,0,0);prepare_refused(2u);
    fresh();frame(1,11u,1u,1u,0x02000000u,0);prepare_refused(2u);
    const unsigned cmds[4]={1u,2u,10u,11u},sizes[4]={116u,196u,32u,524u};
    for(unsigned i=0;i<4u;i++) {
        fresh();frame(1,cmds[i],1u,i<2u?0u:2u,0x02000000u,0);
        frames[1].rx_capacity=sizes[i]-4u;prepare_refused(2u);
    }
    fresh();frame(0,9u,32u,1u,0x02000000u,0);prepare_refused(1u);
    fresh();frames[0].header=header(1u,1u,0);prepare_refused(1u);
    fresh();frame(1,1u,32u,0,0,0);prepare_refused(2u);
    fresh();frames[1].header=0x00004101u;prepare_refused(2u); /* Wrong host. */
    fresh();frames[1].header=0x00000301u;prepare_refused(2u); /* Multicast. */
}

static void span_alias_and_full_source_guards(void) {
    fresh();frames[1].rx_physical=0x0c100020u;prepare_refused(2u); /* Source padding. */
    fresh();frames[1].rx_physical=0x0cfcffc0u;prepare_refused(2u); /* Cross allocator header. */
    fresh();frames[1].rx_physical=0x0cfd0000u;prepare_refused(2u);
    fresh();frames[1].rx_physical=0x8c200400u;prepare_refused(2u); /* Alias rejected. */
    fresh();frames[1].rx_physical=0x0cffffffu;prepare_refused(2u);
    fresh();frames[1].rx_physical=0x0c200401u;prepare_refused(2u);
    fresh();frames[1].rx_capacity=UINT32_MAX;prepare_refused(2u);
    fresh();frames[1].rx_bytes=NULL;prepare_refused(2u);
    fresh();frames[1].rx_bytes=(uint8_t *)(uintptr_t)(UINTPTR_MAX-16u);prepare_refused(2u);
    fresh();frames[1].rx_physical=frames[0].rx_physical;prepare_refused(2u);
    fresh();frames[1].rx_bytes=frames[0].rx_bytes;prepare_refused(2u);
    fresh();frames[1].rx_bytes=(uint8_t *)frames;prepare_refused(2u);
    fresh();frames[1].rx_bytes=(uint8_t *)overlay.staged;prepare_refused(2u);
    fresh();capture();
    assert(kui_vmu_readonly_overlay_prepare(&overlay,frames,2u,0x0c100000u,
        UINT32_MAX,0x0cfcffe0u,0x10020u)<0);unchanged();
    fresh();capture();
    assert(kui_vmu_readonly_overlay_prepare(&overlay,frames,2u,0x0c100000u,
        4096u,0x0c100000u,4u)<0);unchanged();
    fresh();capture();
    assert(kui_vmu_readonly_overlay_prepare(&overlay,frames,2u,0x0c100000u,
        4096u,0x0cfcffe0u,0u)<0);unchanged();
    fresh();capture();
    assert(kui_vmu_readonly_overlay_prepare(&overlay,frames,2u,0x0c100000u,
        12u,0x0cfcffe0u,0x10020u)<0);unchanged(); /* Cannot contain two packets. */
}

static void physical_conflicts_and_parent_reply_guards(void) {
    fresh();physical(1u,1u);completion_refused(2u);assert(!calls);
    assert(overlay.stats.flags&KUI_VMU_OVERLAY_PHYSICAL_CONFLICT);
    fresh();rx[1][0]=0x1c000105u;completion_refused(2u);assert(!calls);
    fresh();frame(2,1u,1u,0,0,0);rx[2][0]=0x000100fbu;
    completion_refused(3u);assert(!calls); /* Check every conflict before generation. */
    const uint32_t parent_bad[]={UINT32_MAX,0x1c002007u,0x1c002008u,0x1b002005u,
        0x1c002105u,0x1c004005u,0x1c000005u};
    for(unsigned i=0;i<sizeof(parent_bad)/sizeof(parent_bad[0]);i++) {
        fresh();rx[0][0]=parent_bad[i];completion_refused(2u);assert(!calls);
        assert(overlay.stats.flags&KUI_VMU_OVERLAY_BAD_PARENT_REPLY);
    }
    fresh();rx[0][1]=0x02000000u;completion_refused(2u);assert(!calls);
    fresh();frame(0,2u,32u,0,0,0);physical(2u,0);capture();assert(!prepare(2u));
    assert(!kui_vmu_readonly_overlay_complete(&overlay));
    fresh();frame(0,9u,32u,1u,0x01000000u,0);physical(9u,0);
    rx[0][1]=0x03000000u;completion_refused(2u);assert(!calls);
}

static void staging_failures_are_atomic(void) {
    fresh();frame(1,11u,1u,2u,0x02000000u,0);frame(2,1u,1u,0,0,0);
    fail_call=2u;completion_refused(3u);assert(calls==2u);
    assert(overlay.stats.flags&KUI_VMU_OVERLAY_BAD_HANDLER);
    assert(!overlay.stats.replies && !overlay.stats.advertisements);
    for(unsigned mode=1u;mode<=5u;mode++) {
        fresh();frame(1,11u,1u,2u,0x02000000u,0);corrupt=mode;
        completion_refused(2u);assert(calls==1u);
    }
    fresh();card.faulted=1u;completion_refused(2u);assert(calls==1u);
}

static void bounded_batch_and_terminal_state(void) {
    fresh();for(unsigned i=1u;i<32u;i++) frame(i,11u,1u,2u,0x02000000u,i<<24);
    capture();assert(!prepare(32u));assert(!kui_vmu_readonly_overlay_complete(&overlay));
    assert(overlay.stats.replies==31u && calls==31u && !card.commit_attempts);
    fresh();capture();assert(prepare(0u)<0);unchanged();
    fresh();capture();assert(prepare(33u)<0);unchanged();
    fresh();capture();assert(kui_vmu_readonly_overlay_complete(&overlay)<0);unchanged();
    fresh();capture();assert(!prepare(1u));assert(prepare(1u)<0);unchanged();
    assert(kui_vmu_readonly_overlay_complete(&overlay)<0 && !overlay.admitted);
    fresh();capture();
    assert(kui_vmu_readonly_overlay_init(&overlay,NULL,&card)==KUI_VMU_OVERLAY_ARGUMENT);
    unchanged(); /* An invalid reinitialization cannot leave admission active. */
    assert(kui_vmu_readonly_overlay_init(NULL,handler,&card)==KUI_VMU_OVERLAY_ARGUMENT);
    assert(kui_vmu_readonly_overlay_prepare(NULL,frames,1u,0,0,0,0)==KUI_VMU_OVERLAY_ARGUMENT);
    assert(kui_vmu_readonly_overlay_complete(NULL)==KUI_VMU_OVERLAY_ARGUMENT);
}

int main(void) {
    discovery_and_all_blank_reads();request_reply_rx_telemetry_stays_correlated();
    strict_request_and_capacity_guards();
    span_alias_and_full_source_guards();physical_conflicts_and_parent_reply_guards();
    staging_failures_are_atomic();bounded_batch_and_terminal_state();
    puts("VMU read-only overlay: atomic batches, blank-card reads and conflict guards passed");
    return 0;
}
