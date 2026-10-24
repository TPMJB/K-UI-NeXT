/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual protocol/store/CMD17/CMD24 implementation on the independent SPI
 * card oracle. The only substituted component is the scoped bus gateway. */
#include "kui/vmu_durable.h"
#include "kui/hash.h"
#include "kui/retail_image.h"
#include "vmu_sd_model.h"
#include <stdio.h>

static struct kui_vmu_durable durable;
static struct kui_vmu_durable_handoff handoff;
static uint8_t response[524];
static size_t response_bytes;
static unsigned lease_calls,block_callbacks;
static uint8_t presentation_address=1u;
static bool omit_callback,double_callback,busy_after_callback;
static bool release_saw_unpublished;

static bool block_transfer(void *context,const uint8_t *tx,uint8_t *rx,size_t count,
    bool slow,uint16_t *crc_out) {
    assert(context==&sd && !tx && rx && count==512u);++block_callbacks;
    for(size_t i=0;i<count;i++) rx[i]=transfer(context,0xffu,slow);
    if(crc_out) *crc_out=crc16(rx,count);
    return true;
}
static int exported_transaction(void *context,struct kui_loader_sd *loan) {
    assert(context==&durable);return kui_vmu_durable_transaction(context,loan);
}
static int gateway(kui_vmu_sd_transaction callback,void *context) {
    assert(callback==exported_transaction && context==&durable);++lease_calls;
    if(sd.deny_enter || sd.deny_acquire) return KUI_VMU_SD_LEASE_BUSY;
    assert(enter(&sd) && acquire(&sd)==KUI_LOADER_SD_OK);
    uint32_t old_crc=durable.handoff.block_crc[durable.block],old_commits=durable.commits,old_bound=durable.bound;
    unsigned old_committed=durable.protocol.committed_valid;
    struct kui_loader_sd loan=card;
    int result=omit_callback?0:callback(context,&loan);
    if(double_callback) result=callback(context,&loan);
    assert(durable.handoff.block_crc[durable.block]==old_crc && durable.commits==old_commits);
    assert(durable.bound==old_bound && !durable.store.io.context);
    assert(durable.protocol.committed_valid==old_committed); /* No protocol publication before release. */
    release_saw_unpublished=true;
    release(&sd);leave(&sd);
    if(result || !loan.ready || !sd.health) {card.ready=false;return KUI_VMU_SD_LEASE_FAULT;}
    if(busy_after_callback) return KUI_VMU_SD_LEASE_BUSY;
    return KUI_VMU_SD_LEASE_OK;
}
static void prepare_handoff(void) {
    memset(&handoff,0,sizeof(handoff));handoff.card_sectors=1024u;handoff.high_capacity=1u;
    memcpy(handoff.cid,sd.cid,16u);memcpy(handoff.image_id,store_config.image_id,16u);
    handoff.base_count=2u;handoff.journal_count=2u;
    for(unsigned i=0;i<2u;i++) {
        handoff.base[i]=(struct kui_vmu_durable_extent){(uint32_t)image[i].first_lba,image[i].sectors};
        handoff.journal[i]=(struct kui_vmu_durable_extent){(uint32_t)journal[i].first_lba,journal[i].sectors};
    }
    struct kui_sha256 hash;kui_sha256_init(&hash);
    for(unsigned block=0;block<256u;block++) {
        handoff.block_crc[block]=kui_retail_crc32(0,sd.media[image_lba(block)],512u);
        kui_sha256_update(&hash,sd.media[image_lba(block)],512u);
    }
    kui_sha256_digest(&hash,handoff.base_sha256);assert(!kui_vmu_durable_handoff_seal(&handoff));
}
static void cold_worker(void) {
    /* A new zeroed worker after a controlled stage checkpoint, not a live
     * backend reset/reinitialization API. */
    memset(&durable,0,sizeof(durable));card.bus.transfer_block=block_transfer;
    lease_calls=block_callbacks=0u;omit_callback=double_callback=busy_after_callback=false;
    release_saw_unpublished=false;
    assert(!(presentation_address==1u?kui_vmu_durable_init(&durable,&handoff,gateway,exported_transaction):
        kui_vmu_durable_init_address(&durable,presentation_address,&handoff,gateway,exported_transaction)));
    assert(durable.ready && !durable.bound && !sd.bytes && !durable.store.io.context);
}
static void setup(void) {model_reset();prepare_handoff();cold_worker();}
static void idle(void) {
    assert(!sd.locked && !sd.owned && !sd.selected && !durable.inflight && !durable.handling);
    assert(!durable.store.io.context && sd.acquires==sd.releases && sd.enters==sd.leaves);
    assert(card.bus.transfer_block==block_transfer);
}
static void frame_for(uint8_t frame[140],unsigned cmd,unsigned block,unsigned phase) {
    memset(frame,0,140u);frame[0]=(uint8_t)cmd;frame[1]=presentation_address;
    if(cmd==1u || cmd==2u) return;
    frame[3]=(uint8_t)(cmd==12u?34u:2u);frame[7]=2u;frame[9]=(uint8_t)phase;frame[11]=(uint8_t)block;
}
static void dispatch(const uint8_t *frame,size_t bytes) {
    memset(response,0xa5,sizeof(response));response_bytes=999u;
    assert(kui_vmu_durable_handle(&durable,frame,bytes,response,sizeof(response),&response_bytes)==1);
    assert(response_bytes==4u+4u*response[3] && response[2]==presentation_address);idle();
}
static void stage(unsigned block,const uint8_t data[512]) {
    unsigned bytes=sd.bytes,calls=lease_calls;
    for(unsigned phase=0;phase<4u;phase++) {
        uint8_t frame[140];frame_for(frame,12u,block,phase);memcpy(frame+12u,data+phase*128u,128u);
        dispatch(frame,sizeof(frame));assert(response[0]==7u && response_bytes==4u);
    }
    assert(sd.bytes==bytes && lease_calls==calls);
}
static void finish(unsigned block,unsigned expected) {
    uint8_t frame[140];frame_for(frame,13u,block,4u);dispatch(frame,12u);assert(response[0]==expected);
    if(expected==0xfbu) assert(response_bytes==8u && response[7]==8u);
}
static void read_compare(unsigned block,const uint8_t expected[512]) {
    uint8_t frame[140];frame_for(frame,11u,block,0u);dispatch(frame,12u);
    assert(response[0]==8u && response_bytes==524u && !memcmp(response+12u,expected,512u));
}
static void guards(void) {
    for(unsigned lba=0;lba<1024u;lba++) if(!allowed(lba))
        for(unsigned i=0;i<512u;i++) assert(sd.media[lba][i]==0x59u);
}
static void success_and_release_order(void) {
    uint8_t next[512],old[512];memset(next,0x73,sizeof(next));memset(old,0x59,sizeof(old));setup();
    uint8_t frame[140];frame_for(frame,1u,0u,0u);dispatch(frame,4u);assert(response[0]==5u && !lease_calls);
    stage(42u,next);unsigned reads=sd.reads,writes=sd.writes,statuses=sd.statuses;
    finish(42u,7u);
    assert(lease_calls==1u && durable.bound && release_saw_unpublished && durable.commits==1u);
    assert(sd.reads==reads+2u && sd.writes==writes+4u && sd.statuses==statuses+8u);
    assert(!block_callbacks && !memcmp(sd.media[image_lba(42u)],next,512u)); /* PIO throughout WRITE. */
    assert(durable.handoff.block_crc[42u]==kui_retail_crc32(0,next,512u) && !durable.handoff.seal);
    unsigned bytes=sd.bytes;finish(42u,7u);assert(sd.bytes==bytes && lease_calls==1u);
    /* A later read uses the original RX block callback; no resident bus
     * descriptor was changed by the earlier PIO transaction. */
    read_compare(42u,next);read_compare(43u,old);
    assert(block_callbacks==2u && durable.reads==2u && durable.commits==1u);guards();
}
static void first_read_and_copied_handoff(void) {
    setup();uint8_t old[512];memset(old,0x59,sizeof(old));
    handoff.cid[0]^=1u;handoff.base[0].lba=0u;handoff.block_crc[42u]^=1u;
    read_compare(42u,old);assert(durable.bound && sd.cids==1u && sd.reads==2u && block_callbacks==2u);
    assert(!sd.writes && !sd.statuses);guards();
    assert(kui_vmu_durable_init(&durable,&handoff,gateway,exported_transaction)!=0);
}
static void busy_before_acquisition_and_retry(void) {
    setup();uint8_t next[512];memset(next,0x81,sizeof(next));stage(42u,next);
    sd.deny_enter=true;finish(42u,0xfcu);
    assert(!sd.bytes && !sd.acquires && !durable.faulted && !durable.bound && !durable.commits);
    assert(durable.sd_busy==1u && durable.protocol.commit_attempted);
    sd.deny_enter=false;finish(42u,7u);assert(durable.commits==1u && durable.commit_attempts==2u);guards();
}
static void assert_terminal(void) {
    assert(durable.faulted && !durable.store.ready);unsigned bytes=sd.bytes;
    kui_vmu_emulation_reset(&durable.protocol);
    uint8_t frame[140];frame_for(frame,11u,42u,0u);dispatch(frame,12u);
    assert(response[0]==0xfbu && response[7]==8u && sd.bytes==bytes);
    assert(kui_vmu_durable_admit(&durable,(UINT32_C(0x0200000b)|((uint32_t)presentation_address<<8)),UINT32_C(0x02000000),0u)<0);guards();
}
static void faults_never_publish_commit(void) {
    uint8_t next[512];memset(next,0xa7,sizeof(next));
    for(unsigned kind=0;kind<2u;kind++) for(unsigned at=1u;at<=(kind?8u:4u);at++) {
        setup();uint32_t crc=durable.handoff.block_crc[42u];stage(42u,next);
        if(kind) sd.fail_status=at;else sd.fail_write=at;
        finish(42u,0xfbu);assert(!durable.commits && durable.handoff.block_crc[42u]==crc);
        assert(!durable.bound);assert_terminal();
    }
    setup();uint32_t crc=durable.handoff.block_crc[42u];stage(42u,next);sd.fail_release=true;
    finish(42u,0xfbu);assert(!memcmp(sd.media[image_lba(42u)],next,512u));
    assert(!durable.commits && durable.handoff.block_crc[42u]==crc && !durable.bound);assert_terminal();
    setup();stage(42u,next);sd.fail_busy=4u;finish(42u,0xfbu);assert_terminal();
    setup();sd.fail_read=2u;uint8_t frame[140];frame_for(frame,11u,42u,0u);dispatch(frame,12u);
    assert(response[0]==0xfbu && response[12]==0xa5u);assert_terminal();
    setup();sd.cid[0]^=1u;stage(42u,next);finish(42u,0xfbu);assert(!sd.reads && !sd.writes);assert_terminal();
    setup();sd.media[image_lba(42u)][0]^=1u;frame_for(frame,11u,42u,0u);dispatch(frame,12u);
    assert(response[0]==0xfbu && !durable.bound && response[12]==0xa5u);assert_terminal();
}
static void pending_journal_and_bad_gateway(void) {
    uint8_t next[512];memset(next,0x92,sizeof(next));setup();stage(42u,next);finish(42u,7u);
    /* A new worker may not perform recovery using a previous launch's seal.
     * Even a valid matching redo must return to high stage for checkpoint. */
    model_reopen();sd.bytes=sd.reads=sd.writes=sd.statuses=sd.cids=0u;cold_worker();
    uint8_t frame[140];frame_for(frame,11u,42u,0u);dispatch(frame,12u);
    assert(response[0]==0xfbu && durable.sd_last_result==KUI_VMU_DURABLE_SD_MARKER);
    assert(sd.reads==1u && !sd.writes && !sd.statuses);assert_terminal();
    for(unsigned kind=0;kind<3u;kind++) {
        setup();omit_callback=kind==0u;double_callback=kind==1u;busy_after_callback=kind==2u;
        frame_for(frame,11u,42u,0u);dispatch(frame,12u);
        assert(response[0]==0xfbu && !durable.bound);assert_terminal();
    }
    setup();sd.media[950u][0]=1u;frame_for(frame,11u,42u,0u);dispatch(frame,12u);
    assert(response[0]==0xfbu && !sd.writes && !sd.statuses);assert_terminal();
}
static void handoff_and_api_bounds(void) {
    setup();struct kui_vmu_durable_handoff original=handoff;
    for(size_t at=0;at<sizeof(handoff);at++) {
        ((uint8_t *)&handoff)[at]^=1u;assert(kui_vmu_durable_handoff_validate(&handoff));handoff=original;
    }
    for(unsigned kind=0;kind<5u;kind++) {
        handoff=original;
        if(kind==0u) handoff.base[0].blocks=127u;
        if(kind==1u) handoff.journal[0].lba=handoff.base[0].lba;
        if(kind==2u) handoff.base[2].blocks=1u;
        if(kind==3u) handoff.card_sectors=950u;
        if(kind==4u) memset(handoff.cid,0xff,16u);
        assert(kui_vmu_durable_handoff_seal(&handoff));
    }
    handoff=original;uint8_t frame[140];frame_for(frame,12u,42u,0u);
    unsigned bytes=sd.bytes;response_bytes=99u;
    assert(kui_vmu_durable_handle(&durable,frame,140u,response,7u,&response_bytes)==KUI_VMU_DURABLE_CAPACITY);
    assert(!response_bytes && !durable.protocol.phases && sd.bytes==bytes);
    assert(kui_vmu_durable_handle(&durable,frame,139u,response,sizeof(response),&response_bytes)==0);
    assert(!durable.protocol.phases && !sd.bytes);
    assert(kui_vmu_durable_handle(&durable,frame,140u,(uint8_t *)&durable,sizeof(durable),&response_bytes)<0);
    assert(kui_vmu_durable_handle(&durable,frame,140u,response,sizeof(response),(size_t *)&durable)<0);
    assert(durable.self==&durable && durable.ready && !durable.faulted);
    struct kui_vmu_durable moved=durable;
    assert(kui_vmu_durable_handle(&moved,frame,140u,response,sizeof(response),&response_bytes)<0);
}
int main(void) {
    for(presentation_address=1u;presentation_address<=2u;presentation_address++) {
        success_and_release_order();first_read_and_copied_handoff();busy_before_acquisition_and_retry();
        faults_never_publish_commit();pending_journal_and_bad_gateway();handoff_and_api_bounds();
        /* An A2 worker rejects A1 requests before lease acquisition. */
        uint8_t frame[140];frame_for(frame,1u,0u,0u);frame[1]=(uint8_t)(3u-presentation_address);
        unsigned leases=lease_calls;response_bytes=99u;
        assert(!kui_vmu_durable_handle(&durable,frame,4u,response,sizeof(response),&response_bytes));
        assert(!response_bytes && lease_calls==leases);
    }
    printf("Durable VMU: scoped identity/journal/readback/release, PIO writes, clean binding, retries and fault latching passed (state%zu handoff%zu)\n",
        sizeof(durable),sizeof(handoff));return 0;
}
