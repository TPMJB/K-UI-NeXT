/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_backing.h"
#include "../src/loader/sd_reader.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Raw read/write/stop callbacks are fault injected here; the actual CMD24/25
 * writer has its separate wire-level test_sci_sd_storage suite. CMD10 below
 * is exercised through the real identity parser and a byte-level bus model. */
struct mock {
    struct kui_loader_sd card;
    struct kui_loader_sd_stream stream;
    struct kui_vmu_backing backing;
    uint8_t cid[16],frame[6],reply[20];
    size_t frame_n,reply_at,reply_n;
    uint32_t ticks,bytes,reads,writes,syncs,stops,acquires,releases;
    bool selected,locked,acquired,reader_lease,deny_enter,deny_acquire;
    bool health,bad_crc,no_reply,reject,stall,frozen_ticks,reenter,reenter_callback,fail_release;
    int read_result,write_result,sync_result,stop_result;
    char events[128];size_t event_n;
    uint8_t media[1024][512];
};
static struct mock m;
static uint16_t crc16(const uint8_t *p,size_t n) {
    uint16_t crc=0;
    for(size_t i=0;i<n;i++) {
        crc^=(uint16_t)p[i]<<8;
        for(unsigned j=0;j<8;j++) crc=(uint16_t)((crc<<1)^((crc&0x8000)?0x1021:0));
    }
    return crc;
}
static uint8_t crc7(const uint8_t *p) {
    uint8_t crc=0;
    for(unsigned i=0;i<5;i++) for(unsigned j=0;j<8;j++) {
        bool bit=((p[i]>>(7u-j))&1u)!=0;
        bool feedback=((crc>>6)!=0)^bit;
        crc=(uint8_t)((crc<<1)&0x7fu);if(feedback) crc^=9u;
    }
    return (uint8_t)((crc<<1)|1u);
}
static void event(char e) {assert(m.event_n+1<sizeof(m.events));m.events[m.event_n++]=e;}
static void select_card(void *ctx,bool selected) {
    assert(ctx==&m && m.locked && m.acquired);
    m.selected=selected;
    if(!selected) m.frame_n=m.reply_at=m.reply_n=0;
}
static uint8_t transfer(void *ctx,uint8_t value,bool slow) {
    assert(ctx==&m && m.locked && m.acquired && !slow);
    ++m.bytes;if(!m.frozen_ticks) m.ticks+=125000u;
    if(!m.selected) return 0xff;
    if(m.stall) return 0;
    if(m.reply_at<m.reply_n) return m.reply[m.reply_at++];
    if(m.frame_n || value==0x4a) {
        m.frame[m.frame_n++]=value;
        if(m.frame_n==6) {
            event('C');assert(m.frame[0]==0x4a && m.frame[5]==crc7(m.frame));
            for(unsigned i=1;i<5;i++) assert(m.frame[i]==0);
            m.frame_n=0;
            if(!m.no_reply) {
                m.reply[0]=m.reject?4:0;m.reply[1]=0xfe;
                memcpy(m.reply+2,m.cid,16);uint16_t crc=crc16(m.cid,16)^m.bad_crc;
                m.reply[18]=(uint8_t)(crc>>8);m.reply[19]=(uint8_t)crc;
                m.reply_at=0;m.reply_n=20;
            }
        }
    }
    return 0xff;
}
static uint32_t ticks(void *ctx) {assert(ctx==&m);return m.ticks;}
static bool enter(void *ctx) {
    assert(ctx==&m && !m.locked);event('E');
    if(m.reenter) {
        uint8_t out[512];m.reenter=false;
        assert(kui_vmu_backing_read(&m.backing,100,out)==KUI_VMU_BACKING_BUSY);
        assert(m.backing.inflight && !m.locked);
    }
    if(m.deny_enter) return false;
    m.locked=true;return true;
}
static void leave(void *ctx) {assert(ctx==&m && m.locked && !m.acquired);event('L');m.locked=false;}
static int acquire(void *ctx) {
    assert(ctx==&m && m.locked && !m.acquired && !m.stream.active);
    ++m.acquires;event('A');
    if(m.deny_acquire) return KUI_LOADER_SD_NOT_READY;
    m.acquired=true;return KUI_LOADER_SD_OK;
}
static void release(void *ctx) {
    assert(ctx==&m && m.locked && m.acquired && !m.selected);
    ++m.releases;event('R');m.acquired=false;
    if(m.fail_release) m.health=false;
}
static bool healthy(void *ctx) {assert(ctx==&m && m.locked);return m.health;}
enum kui_loader_sd_result kui_loader_sd_read(struct kui_loader_sd *card,
    uint32_t lba,uint32_t count,void *out) {
    assert(card==&m.card && m.locked && m.acquired && count==1 && lba<1024 && !m.selected);
    ++m.reads;event('r');
    if(m.reenter_callback) {
        uint8_t nested[512];m.reenter_callback=false;
        assert(m.backing.reserved && m.backing.callback_active);
        assert(kui_vmu_backing_read(&m.backing,lba,nested)==KUI_VMU_BACKING_BUSY);
        assert(kui_vmu_backing_unreserve(&m.backing)==KUI_VMU_BACKING_BUSY);
        assert(kui_vmu_backing_reserve(&m.backing)==KUI_VMU_BACKING_BUSY);
        assert(m.locked && m.acquired && m.backing.reserved && m.backing.callback_active);
    }
    if(!m.read_result) memcpy(out,m.media[lba],512);
    return m.read_result;
}
enum kui_loader_sd_result kui_sci_sd_write(struct kui_loader_sd *card,
    uint32_t lba,uint32_t count,const uint8_t *in) {
    assert(card==&m.card && m.locked && m.acquired && count==1 && lba<1024 && !m.selected);
    ++m.writes;event('w');
    /* A failed write may already have reached media. Never retry blindly. */
    memcpy(m.media[lba],in,512);return m.write_result;
}
enum kui_loader_sd_result kui_sci_sd_sync(struct kui_loader_sd *card) {
    assert(card==&m.card && m.locked && m.acquired && !m.selected);
    ++m.syncs;event('y');return m.sync_result;
}
enum kui_loader_sd_result kui_loader_sd_stream_stop(struct kui_loader_sd *card,
    struct kui_loader_sd_stream *stream) {
    assert(card==&m.card && stream==&m.stream && m.locked && m.acquired && m.reader_lease);
    ++m.stops;event('S');m.stream.active=false;m.selected=false;return m.stop_result;
}
static struct kui_vmu_store_extent image[]={{100,128},{300,128}},journal[]={{900,2}};
static struct kui_vmu_store_config store_config;
static struct kui_vmu_backing_config config;
static struct kui_vmu_backing_bus bus;
static void setup(void) {
    memset(&m,0,sizeof(m));m.health=true;
    m.card=(struct kui_loader_sd){.bus={.ctx=&m,.select=select_card,.transfer=transfer,.ticks=ticks},
        .blocks=1024,.high_capacity=true,.ready=true};
    for(unsigned i=0;i<16;i++) m.cid[i]=(uint8_t)(i+32u);
    image[0]=(struct kui_vmu_store_extent){100,128};image[1]=(struct kui_vmu_store_extent){300,128};
    journal[0]=(struct kui_vmu_store_extent){900,2};
    store_config=(struct kui_vmu_store_config){.card_sectors=1024,.image_id={1},
        .image_extents=image,.image_extent_count=2,.journal_extents=journal,.journal_extent_count=1};
    config=(struct kui_vmu_backing_config){.identity={.sectors=1024,.high_capacity=true},
        .store=&store_config,.partition_first=32,.partition_sectors=992,.allow_writes=true};
    memcpy(config.identity.cid,m.cid,16);
    bus=(struct kui_vmu_backing_bus){.context=&m,.card=&m.card,.enter=enter,.leave=leave,
        .acquire=acquire,.release=release,.healthy=healthy,.reader_stream=&m.stream,.reader_lease=&m.reader_lease};
    assert(kui_vmu_backing_init(&m.backing,&bus,&config)==0);
}
static void idle(void) {assert(!m.locked && !m.acquired && !m.selected && !m.backing.inflight);}
static void mapping_cases(void) {
    setup();image[0].sectors=127;
    assert(kui_vmu_backing_init(&m.backing,&bus,&config)==KUI_VMU_BACKING_MAPPING && !m.bytes);
    setup();image[1].first_lba=200;
    assert(kui_vmu_backing_init(&m.backing,&bus,&config)==KUI_VMU_BACKING_MAPPING);
    setup();journal[0].first_lba=427;
    assert(kui_vmu_backing_init(&m.backing,&bus,&config)==KUI_VMU_BACKING_MAPPING);
    setup();journal[0].first_lba=1023;
    assert(kui_vmu_backing_init(&m.backing,&bus,&config)==KUI_VMU_BACKING_MAPPING);
    setup();config.partition_first=101;config.partition_sectors=923;
    assert(kui_vmu_backing_init(&m.backing,&bus,&config)==KUI_VMU_BACKING_MAPPING);
    setup();config.partition_sectors=UINT64_MAX;
    assert(kui_vmu_backing_init(&m.backing,&bus,&config)==KUI_VMU_BACKING_MAPPING);
    setup();image[0].first_lba=UINT64_MAX-2;
    assert(kui_vmu_backing_init(&m.backing,&bus,&config)==KUI_VMU_BACKING_MAPPING);
    setup();config.identity.sectors=2048;
    assert(kui_vmu_backing_init(&m.backing,&bus,&config)==KUI_VMU_BACKING_IDENTITY);
    setup();memset(config.identity.cid,0xff,16);
    assert(kui_vmu_backing_init(&m.backing,&bus,&config)==KUI_VMU_BACKING_IDENTITY);
    setup();memset(store_config.image_id,0,16);
    assert(kui_vmu_backing_init(&m.backing,&bus,&config)==KUI_VMU_BACKING_IDENTITY);
    setup();struct kui_vmu_store_extent forbidden={400,2};config.forbidden=&forbidden;config.forbidden_count=1;
    assert(kui_vmu_backing_init(&m.backing,&bus,&config)==KUI_VMU_BACKING_MAPPING);
    forbidden.first_lba=500;
    assert(kui_vmu_backing_init(&m.backing,&bus,&config)==0);
    config.forbidden_count=SIZE_MAX;
    assert(kui_vmu_backing_init(&m.backing,&bus,&config)==KUI_VMU_BACKING_ARGUMENT);
    setup();bus.reader_lease=NULL;
    assert(kui_vmu_backing_init(&m.backing,&bus,&config)==KUI_VMU_BACKING_ARGUMENT);
}
static void read_cases(void) {
    uint8_t data[512];setup();memset(m.media[100],0x5a,512);
    struct kui_vmu_store_io io=kui_vmu_backing_io(&m.backing);
    assert(io.read_sector(io.context,100,data)==0 && data[511]==0x5a);
    assert(!strcmp(m.events,"EACrRL"));idle();
    uint32_t reads=m.reads,bytes=m.bytes;
    assert(kui_vmu_backing_read(&m.backing,228,data)==KUI_VMU_BACKING_MAPPING);
    assert(kui_vmu_backing_read(&m.backing,UINT64_MAX,data)==KUI_VMU_BACKING_MAPPING);
    assert(kui_vmu_backing_read(&m.backing,UINT64_C(1)<<32,data)==KUI_VMU_BACKING_MAPPING);
    assert(kui_vmu_backing_read(&m.backing,100,NULL)==KUI_VMU_BACKING_ARGUMENT);
    assert(m.reads==reads && m.bytes==bytes);idle();
    /* A later filesystem/staging object change cannot reauthorize sectors. */
    image[0].first_lba=600;config.identity.cid[0]^=1;
    assert(kui_vmu_backing_read(&m.backing,600,data)==KUI_VMU_BACKING_MAPPING);
    assert(kui_vmu_backing_read(&m.backing,100,data)==0);idle();
    setup();m.deny_enter=true;
    assert(kui_vmu_backing_read(&m.backing,100,data)==KUI_VMU_BACKING_BUSY && !m.acquires && !m.bytes);idle();
    setup();m.deny_acquire=true;
    assert(kui_vmu_backing_read(&m.backing,100,data)==KUI_VMU_BACKING_BUSY && !m.releases && !m.bytes);idle();
    m.deny_acquire=false;
    assert(kui_vmu_backing_read(&m.backing,100,data)==0);idle();
    setup();m.reenter=true;
    assert(kui_vmu_backing_read(&m.backing,100,data)==0 && m.reads==1 && m.releases==1);idle();
    setup();m.stream.active=true;m.reader_lease=m.acquired=m.selected=true;
    assert(kui_vmu_backing_read(&m.backing,100,data)==0);
    assert(!strcmp(m.events,"ESRACrRL") && !m.reader_lease);idle();
    setup();m.stream.active=true;m.reader_lease=m.acquired=m.selected=true;m.stop_result=KUI_LOADER_SD_TIMEOUT;
    assert(kui_vmu_backing_read(&m.backing,100,data)==KUI_VMU_BACKING_IO);
    assert(!strcmp(m.events,"ESRL") && m.backing.poisoned && !m.bytes && !m.card.ready);idle();
    setup();m.read_result=KUI_LOADER_SD_CRC;
    assert(kui_vmu_backing_read(&m.backing,100,data)==KUI_VMU_BACKING_IO && m.backing.poisoned);idle();
    setup();m.fail_release=true;
    assert(kui_vmu_backing_read(&m.backing,100,data)==KUI_VMU_BACKING_IO);
    assert(m.reads==1 && m.releases==1 && m.backing.poisoned && !m.card.ready);idle();
}
static void identity_cases(void) {
    uint8_t data[512];
    for(unsigned kind=0;kind<8;kind++) {
        setup();
        switch(kind) {
            case 0:m.cid[4]^=1;break;
            case 1:m.card.blocks--;break;
            case 2:m.bad_crc=true;break;
            case 3:m.no_reply=true;break;
            case 4:m.reject=true;break;
            case 5:m.stall=m.frozen_ticks=true;break;
            case 6:m.health=false;break;
            default:m.card.high_capacity=false;break;
        }
        int result=kui_vmu_backing_read(&m.backing,100,data);
        assert(result==KUI_VMU_BACKING_IO || result==KUI_VMU_BACKING_IDENTITY);
        assert(m.backing.poisoned && !m.card.ready && !m.reads && m.bytes<=1000001u);idle();
        uint32_t bytes=m.bytes,acquires=m.acquires;
        assert(kui_vmu_backing_read(&m.backing,100,data)==KUI_VMU_BACKING_POISONED);
        assert(m.bytes==bytes && m.acquires==acquires);idle();
    }
}
static void write_cases(void) {
    uint8_t data[512];memset(data,0x73,sizeof(data));setup();
    config.allow_writes=false;
    assert(kui_vmu_backing_init(&m.backing,&bus,&config)==0);
    assert(kui_vmu_backing_write(&m.backing,100,data)==KUI_VMU_BACKING_READ_ONLY);
    assert(kui_vmu_backing_sync(&m.backing)==KUI_VMU_BACKING_READ_ONLY && !m.bytes && !m.acquires);idle();
    setup();
#if KUI_VMU_EXPERIMENTAL
    assert(kui_vmu_backing_write(&m.backing,100,data)==0 && m.writes==1);
    assert(!strcmp(m.events,"EACwRL") && !memcmp(m.media[100],data,512));idle();
    assert(kui_vmu_backing_sync(&m.backing)==0 && m.syncs==1);idle();
    assert(kui_vmu_backing_write(&m.backing,901,data)==0);idle();
    uint32_t bytes=m.bytes;
    assert(kui_vmu_backing_write(&m.backing,902,data)==KUI_VMU_BACKING_MAPPING && m.bytes==bytes);
    for(unsigned kind=0;kind<2;kind++) {
        setup();
        if(kind) m.sync_result=KUI_LOADER_SD_TIMEOUT;else m.write_result=KUI_LOADER_SD_TIMEOUT;
        int result=kind?kui_vmu_backing_sync(&m.backing):kui_vmu_backing_write(&m.backing,100,data);
        assert(result==KUI_VMU_BACKING_IO && m.backing.poisoned && !m.card.ready);idle();
        bytes=m.bytes;
        assert(kui_vmu_backing_write(&m.backing,100,data)==KUI_VMU_BACKING_POISONED && m.bytes==bytes);
        assert(kui_vmu_backing_sync(&m.backing)==KUI_VMU_BACKING_POISONED && m.bytes==bytes);
    }
    setup();m.stream.active=true;m.reader_lease=m.acquired=m.selected=true;
    assert(kui_vmu_backing_write(&m.backing,100,data)==0);
    assert(!strcmp(m.events,"ESRACwRL") && !m.reader_lease);idle();
#else
    assert(kui_vmu_backing_write(&m.backing,100,data)==KUI_VMU_BACKING_READ_ONLY);
    assert(kui_vmu_backing_sync(&m.backing)==KUI_VMU_BACKING_READ_ONLY);
    assert(!m.bytes && !m.writes && !m.syncs && !m.acquires);idle();
#endif
}
static void reservation_cases(void) {
    uint8_t data[512];memset(data,0x49,512);setup();
    assert(kui_vmu_backing_unreserve(&m.backing)==KUI_VMU_BACKING_ARGUMENT && !m.bytes);
#if KUI_VMU_EXPERIMENTAL
    assert(kui_vmu_backing_reserve(&m.backing)==0);
    assert(m.locked && m.acquired && m.backing.reserved && m.backing.inflight);
    assert(m.acquires==1 && !m.releases && !strcmp(m.events,"EAC"));
    uint32_t bytes=m.bytes;
    assert(kui_vmu_backing_reserve(&m.backing)==KUI_VMU_BACKING_BUSY && m.bytes==bytes);
    assert(kui_vmu_backing_write(&m.backing,100,data)==0);
    m.deny_acquire=true; /* A reserved transaction never reacquires per sector. */
    assert(kui_vmu_backing_sync(&m.backing)==0);
    m.reenter_callback=true;
    assert(kui_vmu_backing_read(&m.backing,100,data)==0);
    assert(m.acquires==1 && !m.releases && m.locked && m.acquired);
    bytes=m.bytes;
    assert(kui_vmu_backing_write(&m.backing,1,data)==KUI_VMU_BACKING_MAPPING && m.bytes==bytes);
    assert(kui_vmu_backing_unreserve(&m.backing)==0);
    assert(m.releases==1 && !m.backing.reserved);idle();
    assert(kui_vmu_backing_unreserve(&m.backing)==KUI_VMU_BACKING_ARGUMENT && m.releases==1);
    setup();m.deny_acquire=true;
    assert(kui_vmu_backing_reserve(&m.backing)==KUI_VMU_BACKING_BUSY && !m.backing.poisoned);
    assert(!m.releases && !m.bytes);idle();
    m.deny_acquire=false;assert(kui_vmu_backing_reserve(&m.backing)==0);
    assert(kui_vmu_backing_unreserve(&m.backing)==0);idle();
    setup();m.cid[2]^=1;
    assert(kui_vmu_backing_reserve(&m.backing)==KUI_VMU_BACKING_IDENTITY && m.backing.poisoned);
    assert(m.acquires==1 && m.releases==1 && !m.backing.reserved);idle();
    for(unsigned kind=0;kind<5;kind++) {
        setup();assert(kui_vmu_backing_reserve(&m.backing)==0);
        if(kind==0) m.write_result=KUI_LOADER_SD_TIMEOUT;
        if(kind==1) m.sync_result=KUI_LOADER_SD_TIMEOUT;
        if(kind==2) m.cid[2]^=1;
        int result=kind==1?kui_vmu_backing_sync(&m.backing):
            kind>=3?KUI_VMU_BACKING_OK:kui_vmu_backing_write(&m.backing,100,data);
        if(kind>=3) {
            /* Final health failure is observable even when every store I/O
             * returned success; the bridge must withhold its final ACK. */
            if(kind==3) m.health=false;
            else m.fail_release=true;
            assert(kui_vmu_backing_unreserve(&m.backing)==KUI_VMU_BACKING_IO);
        } else {
            assert(result && m.backing.poisoned && m.backing.reserved && !m.releases);
            assert(m.locked && m.acquired);
            bytes=m.bytes;
            assert(kui_vmu_backing_write(&m.backing,100,data)==KUI_VMU_BACKING_POISONED && m.bytes==bytes);
            assert(kui_vmu_backing_unreserve(&m.backing)==KUI_VMU_BACKING_POISONED);
        }
        assert(m.backing.poisoned && !m.card.ready && !m.backing.reserved && m.releases==1);idle();
    }
#else
    assert(kui_vmu_backing_reserve(&m.backing)==KUI_VMU_BACKING_READ_ONLY && !m.bytes);idle();
#endif
}
int main(void) {
    mapping_cases();read_cases();identity_cases();write_cases();reservation_cases();
    puts("VMU bounded backing tests passed");return 0;
}
