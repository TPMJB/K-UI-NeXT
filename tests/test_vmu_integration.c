/* SPDX-License-Identifier: GPL-3.0-only */
/* Real Maple protocol -> redo store -> immutable backing -> existing SD
 * reader/writer, over an independently authored SPI card model. SCI lease
 * callbacks enforce ownership. No filesystem/host disk shortcut serves I/O. */
#include "kui/vmu_emulation.h"
#include "kui/vmu_backing.h"
#include "../src/loader/sd_reader.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "vmu_sd_model.h"
static struct kui_vmu_backing backing;
static struct kui_vmu_store store;
static kui_vmu_emulation_t protocol;
static uint8_t response[524];static size_t response_bytes;
static bool reserve_transactions;

static void idle(void) {
    assert(!sd.owned && !sd.locked && !sd.selected && !backing.inflight);
    assert(sd.acquires==sd.releases && sd.enters==sd.leaves);
}
static int read_block(void *ctx,uint16_t block,uint8_t data[512]) {
    assert(ctx==&store);return kui_vmu_store_read(&store,block,data)?KUI_VMU_EMU_IO_ERROR:KUI_VMU_EMU_IO_OK;
}
static int commit_block(void *ctx,uint16_t block,const uint8_t data[512]) {
    assert(ctx==&store);
    if(reserve_transactions) {
        int reserved=kui_vmu_backing_reserve(&backing);
        if(reserved==KUI_VMU_BACKING_BUSY) return KUI_VMU_EMU_IO_BUSY;
        if(reserved) return KUI_VMU_EMU_IO_ERROR;
    }
    int result=kui_vmu_store_write(&store,block,data);
    if(reserve_transactions) {
        int released=kui_vmu_backing_unreserve(&backing);
        if(released) result=released;
    }
    if(!result) {
        assert(!sd.pending_valid && !sd.busy && card.ready && !backing.poisoned);
        assert(!memcmp(sd.media[image_lba(block)],data,512));
    }
    return result?KUI_VMU_EMU_IO_ERROR:KUI_VMU_EMU_IO_OK;
}
static int reopen(void) {
    assert(!sd.owned && !sd.locked);
    model_reopen();
    assert(kui_vmu_backing_init(&backing,&model_bus,&backing_config)==0);
    const struct kui_vmu_store_io io=kui_vmu_backing_io(&backing);
    int result=kui_vmu_store_open(&store,&io,&store_config);
    if(!result) {
        const struct kui_vmu_emulation_io protocol_io={&store,read_block,commit_block};
        assert(kui_vmu_emulation_init(&protocol,1,&protocol_io)==0);
    }
    idle();return result;
}
static void setup(void) {
    model_reset();
    reserve_transactions=false;
    assert(reopen()==0);
}
static void request(const uint8_t *p,size_t n) {
    assert(kui_vmu_emulation_handle(&protocol,p,n,response,sizeof(response),&response_bytes)==1);
    assert(response_bytes==4u+(size_t)response[3]*4u);idle();
}
static void phase(unsigned block,unsigned part,const uint8_t data[128]) {
    uint8_t frame[140]={12,1,0,34,0,0,0,2,0,0,0,0};
    frame[9]=(uint8_t)part;frame[10]=(uint8_t)(block>>8);frame[11]=(uint8_t)block;
    memcpy(frame+12,data,128);request(frame,sizeof(frame));
    assert(response_bytes==4 && response[0]==7);
}
static void stage(unsigned block,const uint8_t data[512]) {
    unsigned writes=sd.writes,statuses=sd.statuses;
    for(unsigned part=0;part<4;part++) phase(block,part,data+128*part);
    assert(sd.writes==writes && sd.statuses==statuses);
}
static void finalize(unsigned block,bool success) {
    uint8_t frame[12]={13,1,0,2,0,0,0,2,0,4,0,0};frame[11]=(uint8_t)block;
    request(frame,sizeof(frame));
    assert(response[0]==(success?7:0xfb) && response_bytes==(success?4u:8u));
    if(!success) assert(response[7]==8);
}
static void read(unsigned block,const uint8_t expected[512]) {
    uint8_t frame[12]={11,1,0,2,0,0,0,2,0,0,0,0};frame[11]=(uint8_t)block;
    request(frame,sizeof(frame));assert(response[0]==8 && response_bytes==524);
    assert(!memcmp(response+12,expected,512));
}
static void guards(void) {
    for(unsigned lba=0;lba<1024;lba++) if(!allowed(lba))
        for(unsigned i=0;i<512;i++) assert(sd.media[lba][i]==0x59);
}
static void success_cases(void) {
    setup();uint8_t data[512];
    for(unsigned block=0;block<256;block++) {
        for(unsigned i=0;i<512;i++) data[i]=(uint8_t)(block^i^(i>>8));
        stage(block,data);unsigned writes=sd.writes,statuses=sd.statuses;
        finalize(block,true);
        assert(sd.writes==writes+4 && sd.statuses==statuses+8);
        finalize(block,true);assert(sd.writes==writes+4); /* Lost ACK retry. */
        read(block,data);
    }
    assert(reopen()==0);read(255,data);guards();
    /* A game read stream must receive CMD12 and release before CID/writes. */
    for(unsigned i=0;i<512;i++) data[i]=(uint8_t)(i*19);
    stage(42,data);
    sd.owned=sd.selected=sd.reader_lease=sd.streaming=true;++sd.acquires;
    stream.active=true;stream.remaining=3;
    finalize(42,true);
    assert(sd.stops==1 && !sd.reader_lease && !stream.active);guards();
}
static void failure_cases(void) {
    uint8_t data[512],old[512];memset(data,0xa7,512);memset(old,0x59,512);
    for(unsigned kind=0;kind<2;kind++) for(unsigned n=1;n<=(kind?8u:4u);n++) {
        setup();stage(42,data);
        if(kind) sd.fail_status=sd.statuses+n;else sd.fail_write=sd.writes+n;
        finalize(42,false);assert(backing.poisoned && !card.ready && !store.ready);
        unsigned writes=sd.writes,bytes=sd.bytes;finalize(42,false);
        assert(sd.writes==writes && sd.bytes==bytes);guards();
        assert(reopen()==0);
        /* A committed marker permits replay even after the failed final ACK.
         * Before that marker commits, an interrupted save retains old data. */
        bool committed=kind?n>=5:n==4;
        read(42,committed?data:old);guards();
    }
    setup();stage(42,data);sd.cid[3]^=1;
    finalize(42,false);assert(backing.poisoned && !sd.writes);guards();
    sd.cid[3]^=1;assert(reopen()==0);read(42,old);
    setup();stage(42,data);sd.deny_acquire=true;
    finalize(42,false);assert(!sd.writes && !store.ready && !backing.poisoned);guards();
    assert(reopen()==0);read(42,old);
    setup();stage(42,data);sd.fail_busy=sd.writes+4;
    finalize(42,false);assert(backing.poisoned && sd.pending_valid && sd.bytes<1100000u);
    assert(reopen()==0);read(42,data);guards();
    setup();sd.fail_read=sd.reads+1;
    uint8_t frame[12]={11,1,0,2,0,0,0,2,0,0,0,42};request(frame,sizeof(frame));
    assert(response[0]==0xfb && backing.poisoned && !store.ready);guards();
}
static void reservation_cases(void) {
    uint8_t data[512];memset(data,0x36,sizeof(data));
    setup();reserve_transactions=true;stage(42,data);
    sd.deny_acquire=true;
    uint8_t frame[12]={13,1,0,2,0,0,0,2,0,4,0,42};
    request(frame,sizeof(frame));assert(response[0]==0xfc && response_bytes==4);
    assert(store.ready && !backing.poisoned && !sd.writes); /* Retry before store entry. */
    sd.deny_acquire=false;unsigned acquisitions=sd.acquires;
    finalize(42,true);
    assert(sd.acquires==acquisitions+1 && sd.writes==4 && sd.statuses==8);
    assert(!backing.reserved);read(42,data);guards();
    assert(reopen()==0);read(42,data);
    for(unsigned kind=0;kind<3;kind++) {
        setup();reserve_transactions=true;stage(42,data);
        if(kind==0) sd.fail_write=sd.writes+4;
        if(kind==1) sd.fail_status=sd.statuses+6;
        if(kind==2) sd.fail_busy=sd.writes+4;
        acquisitions=sd.acquires;finalize(42,false);
        assert(sd.acquires==acquisitions+1 && backing.poisoned && !backing.reserved);
        assert(!store.ready && !card.ready);idle();guards();
        assert(reopen()==0);read(42,data);guards();
    }
}
int main(void) {
#if !KUI_VMU_EXPERIMENTAL
#error "Cross-layer save integration requires explicit KUI_VMU_EXPERIMENTAL=1"
#endif
    success_cases();failure_cases();reservation_cases();
    puts("PASS VMU protocol/store/SD integration: all blocks, durable ACKs, replay, identity, arbitration, bounded faults");
    return 0;
}
