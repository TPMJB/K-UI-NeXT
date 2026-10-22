/* SPDX-License-Identifier: GPL-3.0-only */
/* Production service -> protocol -> redo store -> immutable backing -> actual
 * SD reader/writer. Only the SPI card, shared bus and IRQ lock are modeled. */
#include "kui/vmu_service.h"
#include "vmu_sd_model.h"
#include <stdio.h>

static struct kui_vmu_service service;
static uint8_t response[KUI_VMU_EMU_MAX_RESPONSE_BYTES];
static size_t response_bytes;

static void idle(void) {
    assert(!sd.owned && !sd.locked && !sd.selected);
    assert(!service.backing.inflight && !service.backing.reserved);
    assert(!service.backing.callback_active && !service.handling);
    assert(sd.acquires==sd.releases && sd.enters==sd.leaves);
}
static int initialize(void) {
    return kui_vmu_service_init(&service,&model_bus,&backing_config,1);
}
static int reopen(void) {
    model_reopen();memset(&service,0,sizeof(service));
    int result=initialize();idle();return result;
}
static void setup(void) {
    model_reset();memset(&service,0,sizeof(service));
    assert(initialize()==KUI_VMU_SERVICE_OK);idle();
}
static void request(const uint8_t *frame,size_t bytes) {
    assert(kui_vmu_service_handle(&service,frame,bytes,response,sizeof(response),&response_bytes)==1);
    assert(response_bytes==4u+(size_t)response[3]*4u);idle();
}
static void frame_for(uint8_t frame[12],unsigned command,unsigned block,unsigned part) {
    const uint8_t base[12]={0,1,0,2,0,0,0,2,0,0,0,0};
    memcpy(frame,base,12);frame[0]=(uint8_t)command;frame[9]=(uint8_t)part;
    frame[10]=(uint8_t)(block>>8);frame[11]=(uint8_t)block;
}
static void stage(unsigned block,const uint8_t data[512]) {
    unsigned bytes=sd.bytes,writes=sd.writes,statuses=sd.statuses;
    for(unsigned part=0;part<4;part++) {
        uint8_t frame[140];frame_for(frame,12,block,part);frame[3]=34;
        memcpy(frame+12,data+128*part,128);request(frame,sizeof(frame));
        assert(response[0]==7 && response_bytes==4);
    }
    assert(sd.bytes==bytes && sd.writes==writes && sd.statuses==statuses);
}
static void finalize(unsigned block,unsigned expected) {
    uint8_t frame[12];frame_for(frame,13,block,4);request(frame,sizeof(frame));
    assert(response[0]==expected);
    if(expected==0xfb) assert(response_bytes==8 && response[7]==8);
    else assert(response_bytes==4);
}
static void read_block(unsigned block,const uint8_t expected[512]) {
    uint8_t frame[12];frame_for(frame,11,block,0);request(frame,sizeof(frame));
    assert(response[0]==8 && response_bytes==524 && !memcmp(response+12,expected,512));
}
static void reset(void) {
    const uint8_t frame[]={3,1,0,0};request(frame,sizeof(frame));
    assert(response[0]==7 && response_bytes==4);
}
static void guards(void) {
    for(unsigned lba=0;lba<1024;lba++) if(!allowed(lba))
        for(unsigned i=0;i<512;i++) assert(sd.media[lba][i]==0x59);
}
static void success_cases(void) {
    uint8_t data[512];setup();
    for(unsigned block=0;block<256;block++) {
        for(unsigned i=0;i<512;i++) data[i]=(uint8_t)(i*3u+block);
        stage(block,data);unsigned acquisitions=sd.acquires,writes=sd.writes,statuses=sd.statuses;
        finalize(block,7);
        assert(sd.acquires==acquisitions+1 && sd.writes==writes+4 && sd.statuses==statuses+8);
        assert(!sd.pending_valid && !sd.busy && sd.health && card.ready && !service.faulted);
        assert(!memcmp(sd.media[image_lba(block)],data,512));
        /* A repeated sync after a lost reply acknowledges the same durable
         * result without another transaction or write. */
        unsigned bytes=sd.bytes;finalize(block,7);assert(sd.bytes==bytes);
        read_block(block,data);
    }
    assert(reopen()==0);read_block(255,data);guards();
    /* The service returns a surviving CMD18 game-reader lease before its
     * single reserved save transaction. */
    memset(data,0x61,512);stage(42,data);
    sd.owned=sd.selected=sd.reader_lease=sd.streaming=true;++sd.acquires;
    stream.active=true;stream.remaining=3;
    finalize(42,7);assert(sd.stops==1 && !sd.reader_lease && !stream.active);
    guards();
    /* Caller config arrays may expire after successful initialization. */
    image[0].first_lba=0;journal[1].first_lba=0;store_config.image_id[0]^=1;
    read_block(42,data);memset(data,0x62,512);stage(42,data);finalize(42,7);
    image[0].first_lba=100;journal[1].first_lba=950;store_config.image_id[0]^=1;
    assert(reopen()==0);read_block(42,data);guards();
}
static void busy_cases(void) {
    uint8_t data[512],old[512],frame[12];memset(data,0x73,512);memset(old,0x59,512);
    for(unsigned deny=0;deny<2;deny++) {
        model_reset();memset(&service,0,sizeof(service));
        sd.deny_enter=deny==0;sd.deny_acquire=deny==1;
        assert(initialize()==KUI_VMU_SERVICE_BUSY);
        assert(!service.ready && !service.faulted && !service.store.ready && !sd.bytes);idle();
        sd.deny_enter=sd.deny_acquire=false;assert(initialize()==0);idle();
        stage(42,data);unsigned bytes=sd.bytes;
        sd.deny_enter=deny==0;sd.deny_acquire=deny==1;
        finalize(42,0xfc);assert(sd.bytes==bytes && !service.faulted && service.store.ready);
        /* Initial contention must also make reads retryable. It must never
         * enter store_read and convert a temporary BUSY into a sticky error. */
        frame_for(frame,11,43,0);request(frame,sizeof(frame));
        assert(response[0]==0xfc && response_bytes==4 && sd.bytes==bytes && service.store.ready);
        sd.deny_enter=sd.deny_acquire=false;
        finalize(42,7);read_block(42,data);read_block(43,old);guards();
    }
}
static void assert_fault_latched(void) {
    assert(service.faulted && !service.store.ready);
    unsigned bytes=sd.bytes;
    /* Protocol RESET does not clear storage poison or permit an uncertain
     * journal transaction to be retried from a live game. */
    reset();uint8_t frame[12];frame_for(frame,11,42,0);request(frame,sizeof(frame));
    assert(response[0]==0xfb && response[7]==8);
    uint8_t data[512];memset(data,0x84,512);stage(43,data);finalize(43,0xfb);
    assert(sd.bytes==bytes && initialize()==KUI_VMU_SERVICE_BLOCKED);idle();
}
static void failure_cases(void) {
    uint8_t data[512],old[512];memset(data,0xa7,512);memset(old,0x59,512);
    for(unsigned kind=0;kind<2;kind++) for(unsigned n=1;n<=(kind?8u:4u);n++) {
        setup();stage(42,data);
        if(kind) sd.fail_status=sd.statuses+n;else sd.fail_write=sd.writes+n;
        finalize(42,0xfb);assert(service.backing.poisoned && !card.ready);
        assert(service.last_backing_result==KUI_VMU_BACKING_IO);
        assert(service.last_store_result==KUI_VMU_STORE_IO);assert_fault_latched();guards();
        assert(reopen()==0);
        bool committed=kind?n>=5:n==4;
        read_block(42,committed?data:old);guards();
    }
    setup();stage(42,data);sd.fail_busy=sd.writes+4;finalize(42,0xfb);
    assert(sd.pending_valid && sd.bytes<1100000u);assert_fault_latched();
    assert(reopen()==0);read_block(42,data);guards();
    setup();sd.fail_read=sd.reads+1;
    uint8_t frame[12];frame_for(frame,11,42,0);request(frame,sizeof(frame));
    assert(response[0]==0xfb && response[7]==8);assert_fault_latched();
    assert(reopen()==0);read_block(42,old);guards();
    setup();stage(42,data);sd.cid[3]^=1;unsigned writes=sd.writes;finalize(42,0xfb);
    assert(sd.writes==writes && service.last_backing_result==KUI_VMU_BACKING_IDENTITY);
    assert_fault_latched();sd.cid[3]^=1;assert(reopen()==0);read_block(42,old);guards();
    /* Every write/sync can succeed while restoring the shared bus fails.
     * The final ACK must still be withheld and that session must stay shut. */
    setup();stage(42,data);sd.fail_release=true;finalize(42,0xfb);
    assert(service.last_store_result==0 && service.last_backing_result==KUI_VMU_BACKING_IO);
    assert(!sd.health && !card.ready && !memcmp(sd.media[image_lba(42)],data,512));
    assert_fault_latched();assert(reopen()==0);read_block(42,data);guards();
    /* Initial recovery failures latch too; the same object cannot reset its
     * backing to bypass a failed initialization. */
    model_reset();memset(&service,0,sizeof(service));sd.fail_read=1;
    assert(initialize()==KUI_VMU_SERVICE_STORAGE && service.faulted && !service.ready);
    unsigned bytes=sd.bytes;assert(initialize()==KUI_VMU_SERVICE_BLOCKED && sd.bytes==bytes);idle();
    assert(reopen()==0);read_block(42,old);
    model_reset();memset(&service,0,sizeof(service));sd.fail_release=true;
    assert(initialize()==KUI_VMU_SERVICE_STORAGE && service.faulted && !service.ready);idle();
}
static bool nested_initialization;
static void reenter(void) {
    const uint8_t frame[]={1,1,0,0};uint8_t out[196];size_t bytes=37;
    assert(service.handling);
    assert(kui_vmu_service_handle(&service,frame,sizeof(frame),out,sizeof(out),&bytes)==
        (nested_initialization?KUI_VMU_SERVICE_HANDLE_NOT_READY:KUI_VMU_SERVICE_HANDLE_REENTRANT));
    assert(bytes==0 && initialize()==KUI_VMU_SERVICE_BLOCKED && service.handling);
}
static void admission_cases(void) {
    const uint8_t frame[]={1,1,0,0};uint8_t out[196];size_t bytes=37;
    memset(&service,0,sizeof(service));
    assert(kui_vmu_service_handle(&service,frame,4,out,sizeof(out),&bytes)==KUI_VMU_SERVICE_HANDLE_NOT_READY && !bytes);
    assert(kui_vmu_service_handle(NULL,frame,4,out,sizeof(out),&bytes)==-1);
    model_reset();assert(kui_vmu_service_init(&service,&model_bus,&backing_config,0x20)==KUI_VMU_SERVICE_ARGUMENT);
    assert(!sd.bytes && !service.faulted && !service.handling);
    model_bus.healthy=NULL;assert(initialize()==KUI_VMU_SERVICE_ARGUMENT && !sd.bytes);
    model_bus.healthy=healthy;backing_config.allow_writes=false;
    assert(initialize()==KUI_VMU_SERVICE_ARGUMENT && !sd.bytes);
    backing_config.allow_writes=true;
    image[0].first_lba=99; /* Intersects a protected allocation. */
    assert(initialize()==KUI_VMU_SERVICE_ARGUMENT && !sd.bytes && !service.faulted);
    image[0].first_lba=100;
    nested_initialization=true;sd.once_on_enter=reenter;
    assert(initialize()==0);idle();assert(initialize()==KUI_VMU_SERVICE_BLOCKED);
    nested_initialization=false;sd.once_on_transfer=reenter;
    uint8_t old[512];memset(old,0x59,512);read_block(42,old);idle();
    unsigned io=sd.bytes;uint8_t read_frame[12];frame_for(read_frame,11,42,0);
    assert(kui_vmu_service_handle(&service,read_frame,12,out,sizeof(out),&bytes)==-2 && !bytes);
    assert(sd.bytes==io && !service.handling && !service.faulted);
}
int main(void) {
#if !KUI_VMU_EXPERIMENTAL
#error "VMU service tests require explicit KUI_VMU_EXPERIMENTAL=1"
#endif
    admission_cases();success_cases();busy_cases();failure_cases();
    puts("PASS VMU service: durable ACKs, shared leases, BUSY retry, release faults, recovery and reentrancy");
    return 0;
}
