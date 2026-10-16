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

struct fake_sd {
    uint8_t media[1024][512],cid[16],frame[6],reply[520],write[514],pending[512];
    uint32_t ticks,bytes,write_lba,pending_lba;
    unsigned frame_n,reply_n,reply_at,write_at,write_phase,busy;
    unsigned reads,writes,statuses,cids,stops,acquires,releases,enters,leaves;
    unsigned fail_write,fail_status,fail_read,fail_busy;
    bool selected,owned,locked,reader_lease,streaming,pending_valid,deny_acquire;
};
static struct fake_sd sd;
static struct kui_loader_sd card;
static struct kui_loader_sd_stream stream;
static struct kui_vmu_backing backing;
static struct kui_vmu_store store;
static kui_vmu_emulation_t protocol;
static struct kui_vmu_store_extent image[]={{100,128},{400,128}};
static struct kui_vmu_store_extent journal[]={{900,1},{950,1}};
static struct kui_vmu_store_extent protected[]={{32,68},{600,200}};
static struct kui_vmu_store_config store_config;
static struct kui_vmu_backing_config backing_config;
static uint8_t response[524];static size_t response_bytes;

static uint16_t crc16(const uint8_t *p,size_t n) {
    uint16_t crc=0;
    for(size_t i=0;i<n;i++) {
        crc^=(uint16_t)p[i]<<8;
        for(unsigned j=0;j<8;j++) crc=(uint16_t)((crc<<1)^((crc&0x8000u)?0x1021u:0));
    }
    return crc;
}
static uint8_t crc7(const uint8_t *p) {
    unsigned crc=0;
    for(unsigned i=0;i<5;i++) for(unsigned bit=0;bit<8;bit++) {
        unsigned feedback=((crc>>6)^((p[i]>>(7u-bit))&1u))&1u;
        crc=(crc<<1)&127u;if(feedback) crc^=9u;
    }
    return (uint8_t)((crc<<1)|1u);
}
static bool allowed(uint32_t lba) {
    return (lba>=100 && lba<228) || (lba>=400 && lba<528) || lba==900 || lba==950;
}
static uint32_t image_lba(unsigned block) {assert(block<256);return block<128?100+block:400+block-128;}
static void queue_payload(const uint8_t *p,size_t count,bool corrupt) {
    assert(count<=512 && sd.reply_at==sd.reply_n);
    sd.reply_at=0;sd.reply[0]=0;sd.reply[1]=0xfe;
    memcpy(sd.reply+2,p,count);uint16_t crc=crc16(p,count)^corrupt;
    sd.reply[count+2]=(uint8_t)(crc>>8);sd.reply[count+3]=(uint8_t)crc;
    sd.reply_n=(unsigned)count+4;
}
static void command(void) {
    assert(sd.frame[5]==crc7(sd.frame));
    unsigned cmd=sd.frame[0]&63u;
    uint32_t arg=(uint32_t)sd.frame[1]<<24|(uint32_t)sd.frame[2]<<16|
        (uint32_t)sd.frame[3]<<8|sd.frame[4];
    sd.frame_n=0;sd.reply_at=sd.reply_n=0;
    if(cmd==10) {assert(!arg && !sd.streaming);++sd.cids;queue_payload(sd.cid,16,false);}
    else if(cmd==17) {
        assert(arg<1024 && allowed(arg) && !sd.streaming);
        ++sd.reads;queue_payload(sd.media[arg],512,sd.reads==sd.fail_read);
    } else if(cmd==24) {
        assert(arg<1024 && allowed(arg) && !sd.streaming && !sd.pending_valid && !sd.write_phase);
        ++sd.writes;sd.write_lba=arg;sd.write_phase=1;
        sd.reply[0]=0;sd.reply_n=1;
    } else if(cmd==13) {
        assert(!arg && !sd.streaming && !sd.busy && !sd.pending_valid);
        ++sd.statuses;sd.reply[0]=0;
        sd.reply[1]=sd.statuses==sd.fail_status?0x20:0;sd.reply_n=2;
    } else if(cmd==12) {
        assert(!arg && sd.streaming);++sd.stops;sd.streaming=false;
        sd.reply[0]=0xff;sd.reply[1]=0;sd.reply_n=2; /* CMD12 stuff + R1. */
    } else assert(!"Unexpected SD command, metadata/erase/reinitialization forbidden");
}
static void select_card(void *ctx,bool selected) {
    assert(ctx==&sd && sd.locked && sd.owned);
    sd.selected=selected;
    if(!selected) {assert(!sd.write_phase);sd.reply_at=sd.reply_n=sd.frame_n=0;}
}
static uint8_t transfer(void *ctx,uint8_t value,bool slow) {
    assert(ctx==&sd && sd.locked && sd.owned && !slow);
    ++sd.bytes;++sd.ticks;
    if(!sd.selected) return 0xff;
    if(sd.reply_at<sd.reply_n) return sd.reply[sd.reply_at++];
    if(sd.busy) {
        if(sd.writes==sd.fail_busy) return 0;
        if(!--sd.busy && sd.pending_valid) {
            memcpy(sd.media[sd.pending_lba],sd.pending,512);sd.pending_valid=false;
        }
        return 0;
    }
    if(sd.write_phase==2) {
        sd.write[sd.write_at++]=value;
        if(sd.write_at==514) {
            uint16_t crc=crc16(sd.write,512);
            assert(sd.write[512]==(uint8_t)(crc>>8) && sd.write[513]==(uint8_t)crc);
            sd.write_phase=0;sd.reply_at=0;sd.reply_n=1;sd.busy=3;
            if(sd.writes==sd.fail_write) {
                /* Device rejection after an uncertain torn physical write. */
                memcpy(sd.media[sd.write_lba],sd.write,200);sd.reply[0]=0x0d;
            } else {
                memcpy(sd.pending,sd.write,512);sd.pending_lba=sd.write_lba;
                sd.pending_valid=true;sd.reply[0]=5;
            }
        }
        return 0xff;
    }
    if(sd.write_phase==1) {
        assert(value==0xff || value==0xfe);
        if(value==0xfe) {sd.write_phase=2;sd.write_at=0;}
        return 0xff;
    }
    if(sd.frame_n || (value&0xc0u)==0x40u) {
        sd.frame[sd.frame_n++]=value;if(sd.frame_n==6) command();
    }
    return 0xff;
}
static uint32_t ticks(void *ctx) {assert(ctx==&sd);return sd.ticks;}
static bool enter(void *ctx) {assert(ctx==&sd && !sd.locked);++sd.enters;sd.locked=true;return true;}
static void leave(void *ctx) {assert(ctx==&sd && sd.locked && !sd.owned);++sd.leaves;sd.locked=false;}
static int acquire(void *ctx) {
    assert(ctx==&sd && sd.locked && !sd.owned && !sd.selected && !sd.streaming);
    if(sd.deny_acquire) return KUI_LOADER_SD_NOT_READY;
    ++sd.acquires;sd.owned=true;return KUI_LOADER_SD_OK;
}
static void release(void *ctx) {
    assert(ctx==&sd && sd.locked && sd.owned && !sd.selected);
    ++sd.releases;sd.owned=false;
}
static void idle(void) {
    assert(!sd.owned && !sd.locked && !sd.selected && !backing.inflight);
    assert(sd.acquires==sd.releases && sd.enters==sd.leaves);
}
static int read_block(void *ctx,uint16_t block,uint8_t data[512]) {
    assert(ctx==&store);return kui_vmu_store_read(&store,block,data)?KUI_VMU_EMU_IO_ERROR:KUI_VMU_EMU_IO_OK;
}
static int commit_block(void *ctx,uint16_t block,const uint8_t data[512]) {
    assert(ctx==&store);
    int result=kui_vmu_store_write(&store,block,data);
    if(!result) {
        assert(!sd.pending_valid && !sd.busy && card.ready && !backing.poisoned);
        assert(!memcmp(sd.media[image_lba(block)],data,512));
    }
    return result?KUI_VMU_EMU_IO_ERROR:KUI_VMU_EMU_IO_OK;
}
static int reopen(void) {
    assert(!sd.owned && !sd.locked);
    /* A controlled fresh card session after reboot. Preserve only durable
     * media + CID. Do not let poisoned runtime instances resume themselves. */
    sd.selected=sd.streaming=sd.pending_valid=sd.reader_lease=false;
    sd.frame_n=sd.reply_at=sd.reply_n=sd.write_phase=sd.busy=0;
    sd.fail_write=sd.fail_status=sd.fail_read=sd.fail_busy=0;sd.deny_acquire=false;
    stream=(struct kui_loader_sd_stream){0};
    card=(struct kui_loader_sd){.bus={.ctx=&sd,.select=select_card,.transfer=transfer,.ticks=ticks},
        .blocks=1024,.high_capacity=true,.ready=true};
    const struct kui_vmu_backing_bus bus={.context=&sd,.card=&card,.enter=enter,.leave=leave,
        .acquire=acquire,.release=release,.reader_stream=&stream,.reader_lease=&sd.reader_lease};
    assert(kui_vmu_backing_init(&backing,&bus,&backing_config)==0);
    const struct kui_vmu_store_io io=kui_vmu_backing_io(&backing);
    int result=kui_vmu_store_open(&store,&io,&store_config);
    if(!result) {
        const struct kui_vmu_emulation_io protocol_io={&store,read_block,commit_block};
        assert(kui_vmu_emulation_init(&protocol,1,&protocol_io)==0);
    }
    idle();return result;
}
static void setup(void) {
    memset(&sd,0,sizeof(sd));
    for(unsigned lba=0;lba<1024;lba++) memset(sd.media[lba],0x59,512);
    memset(sd.media[950],0,512); /* Empty preallocated sidecar marker. */
    for(unsigned i=0;i<16;i++) sd.cid[i]=(uint8_t)(0x20+i);
    store_config=(struct kui_vmu_store_config){.card_sectors=1024,.image_id={0x41,0x42},
        .image_extents=image,.image_extent_count=2,.journal_extents=journal,.journal_extent_count=2};
    backing_config=(struct kui_vmu_backing_config){.identity={.sectors=1024,.high_capacity=true},
        .store=&store_config,.partition_first=32,.partition_sectors=992,
        .forbidden=protected,.forbidden_count=2,.allow_writes=true};
    memcpy(backing_config.identity.cid,sd.cid,16);
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
int main(void) {
#if !KUI_VMU_EXPERIMENTAL
#error "Cross-layer save integration requires explicit KUI_VMU_EXPERIMENTAL=1"
#endif
    success_cases();failure_cases();
    puts("PASS VMU protocol/store/SD integration: all blocks, durable ACKs, replay, identity, arbitration, bounded faults");
    return 0;
}
