/* SPDX-License-Identifier: GPL-3.0-only */
#include "native_return_hardware.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAPLE_ADDR UINT32_C(0xa05f6c04)
#define MAPLE_SELECT UINT32_C(0xa05f6c10)
#define MAPLE_ENABLE UINT32_C(0xa05f6c14)
#define MAPLE_STATE UINT32_C(0xa05f6c18)
#define MAPLE_PROTECT UINT32_C(0xa05f6c8c)
#define CACHE_EVENT UINT32_C(0xfeedcafe)
extern uint32_t *kui_native_return_hw_test_receive(void);
extern const uint32_t *kui_native_return_hw_test_transmit(void);
extern void kui_native_return_hw_test_reset(void);
struct entry {uint32_t address,value;};
static struct entry regs[64],writes[64];
static unsigned reg_count,write_count,cache_calls,reads,physical_calls,checks;
static uint32_t sr,tx_address,rx_address;
static enum {COMPLETE,TIMEOUT_ABORTS,TIMEOUT_STUCK} behavior;
static bool started;
static uint32_t reply[4],settle_address;
static unsigned settle_reads;
#define CHECK(x) do {++checks;assert(x);} while(0)
static uint32_t *value(uint32_t address) {
    for(unsigned i=0;i<reg_count;i++)if(regs[i].address==address)return &regs[i].value;
    assert(reg_count<64u);regs[reg_count]=(struct entry){address,0};return &regs[reg_count++].value;
}
uint32_t kui_native_return_hw_test_read(uint32_t address) {
    ++reads;
    if(address==settle_address && settle_reads && !--settle_reads)*value(address)=0;
    if(address==MAPLE_STATE && started && behavior==COMPLETE) {
        memcpy(kui_native_return_hw_test_receive(),reply,sizeof(reply));
        *value(address)=0;started=false;
    }
    return *value(address);
}
void kui_native_return_hw_test_write(uint32_t address,uint32_t data) {
    assert(write_count<64u);writes[write_count++]=(struct entry){address,data};
    if(address==MAPLE_STATE && data==1u)started=true;
    if(address==MAPLE_STATE && data==0u && behavior==TIMEOUT_STUCK)return;
    *value(address)=data;
}
uint32_t kui_native_return_hw_test_sr(void) {return sr;}
uint32_t kui_native_return_hw_test_physical(const void *p) {
    ++physical_calls;
    if(p==kui_native_return_hw_test_transmit())return tx_address;
    assert(p==kui_native_return_hw_test_receive());return rx_address;
}
void kui_native_return_hw_test_cache_off(void) {
    ++cache_calls;assert(write_count<64u);writes[write_count++]=(struct entry){CACHE_EVENT,0};
}
static void reset(void) {
    kui_native_return_hw_test_reset();memset(regs,0,sizeof(regs));memset(writes,0,sizeof(writes));
    reg_count=write_count=cache_calls=reads=physical_calls=0;sr=UINT32_C(0x500000f0);
    tx_address=UINT32_C(0x0c800000);rx_address=UINT32_C(0x0c800020);behavior=COMPLETE;started=false;settle_address=settle_reads=0;
    reply[0]=UINT32_C(0x03200008);reply[1]=UINT32_C(0x01000000);
    reply[2]=UINT32_C(0x8070ffff);reply[3]=UINT32_C(0x80808080);
    *value(MAPLE_ADDR)=UINT32_C(0x0c444400);*value(MAPLE_PROTECT)=UINT32_C(0x61554041);
    *value(MAPLE_SELECT)=1u;*value(MAPLE_ENABLE)=1u;
}
static void event(unsigned index,uint32_t address,uint32_t data) {
    CHECK(index<write_count && writes[index].address==address && writes[index].value==data);
}
static void disabled_terminal(void) {
    CHECK(*value(MAPLE_ENABLE)==0u && *value(MAPLE_SELECT)==0u);
    for(unsigned i=0;i<write_count;i++)if(writes[i].address==MAPLE_SELECT)CHECK(writes[i].value==0u);
}
static void controller(void) {
    uint32_t buttons;
    reset();reply[2]&=~UINT32_C(0x00000008);
    CHECK(kui_native_return_controller(&buttons)==1 && buttons==8u);
    const uint32_t *tx=kui_native_return_hw_test_transmit();
    CHECK(tx[0]==UINT32_C(0x80000001) && tx[1]==rx_address &&
        tx[2]==UINT32_C(0x01002009) && tx[3]==UINT32_C(0x01000000));
    event(0,MAPLE_ENABLE,0);event(1,MAPLE_SELECT,0);event(2,MAPLE_PROTECT,UINT32_C(0x6155404f));
    event(3,MAPLE_ADDR,tx_address);event(4,MAPLE_ENABLE,1);event(5,MAPLE_STATE,1);
    event(write_count-4u,MAPLE_ADDR,UINT32_C(0x0c444400));event(write_count-3u,MAPLE_SELECT,0);
    event(write_count-2u,MAPLE_PROTECT,UINT32_C(0x61554041));event(write_count-1u,MAPLE_ENABLE,0);
    disabled_terminal();CHECK(!cache_calls);
    reset();reply[0]=UINT32_C(0x03230008);reply[2]=UINT32_C(0x1234fffa);
    CHECK(kui_native_return_controller(&buttons)==1 && buttons==5u);disabled_terminal();
    const uint32_t bad_headers[]={UINT32_C(0x03200007),UINT32_C(0x02200008),UINT32_C(0x03400008),
        UINT32_C(0x03200108),UINT32_C(0xffffffff)};
    for(unsigned i=0;i<sizeof(bad_headers)/sizeof(*bad_headers);i++) {
        reset();reply[0]=bad_headers[i];buttons=123u;
        CHECK(kui_native_return_controller(&buttons)==0 && buttons==0u);disabled_terminal();
    }
    reset();reply[1]=UINT32_C(0x02000000);CHECK(!kui_native_return_controller(&buttons) && !buttons);
    reset();CHECK(kui_native_return_controller(NULL)==-1 && !write_count && !physical_calls);
    const uint32_t bad_sr[]={0,UINT32_C(0x000000f0),UINT32_C(0x40000000),UINT32_C(0x500000e0)};
    for(unsigned i=0;i<sizeof(bad_sr)/sizeof(*bad_sr);i++) {
        reset();sr=bad_sr[i];buttons=123u;
        CHECK(kui_native_return_controller(&buttons)==-1 && !buttons && !write_count && !physical_calls);
    }
    reset();*value(MAPLE_STATE)=1u;buttons=123u;
    CHECK(kui_native_return_controller(&buttons)==-1 && !buttons && !write_count && !physical_calls);
    const uint32_t bad_addresses[]={UINT32_C(0x0c800001),UINT32_C(0x0c00ffe0),UINT32_C(0x0d000000)};
    for(unsigned i=0;i<sizeof(bad_addresses)/sizeof(*bad_addresses);i++) {
        reset();tx_address=bad_addresses[i];CHECK(kui_native_return_controller(&buttons)==-1 && !write_count);
        reset();rx_address=bad_addresses[i];CHECK(kui_native_return_controller(&buttons)==-1 && !write_count);
    }
    reset();rx_address=UINT32_C(0x0cffffe0);CHECK(kui_native_return_controller(&buttons)==-1 && !write_count);
    reset();behavior=TIMEOUT_ABORTS;buttons=123u;
    CHECK(kui_native_return_controller(&buttons)==0 && !buttons && reads>=1000000u);disabled_terminal();
    CHECK(*value(MAPLE_ADDR)==UINT32_C(0x0c444400) && *value(MAPLE_PROTECT)==UINT32_C(0x61554041));
    started=false;behavior=COMPLETE;reply[2]&=~8u;
    CHECK(kui_native_return_controller(&buttons)==1 && buttons==8u);disabled_terminal();
    reset();behavior=TIMEOUT_STUCK;
    CHECK(kui_native_return_controller(&buttons)==-1 && reads>=2000000u);disabled_terminal();
    unsigned prior=write_count;started=false;behavior=COMPLETE;*value(MAPLE_STATE)=0u;
    CHECK(kui_native_return_controller(&buttons)==-1 && write_count==prior);
    CHECK(!kui_native_return_quiesce() && write_count==prior && !cache_calls);
}
static void quiesce(void) {
    reset();
    for(unsigned channel=0;channel<4u;channel++) {
        *value(UINT32_C(0xa05f7800)+channel*32u+20u)=1u;
        *value(UINT32_C(0xffa0000c)+channel*16u)=UINT32_C(0x00004910)|(channel<<4)|(channel==1u?0u:1u);
    }
    *value(UINT32_C(0xa05f7c14))=1u;
    *value(UINT32_C(0xa0702c00))=0x80u;
    CHECK(kui_native_return_quiesce() && cache_calls==1u);
    for(unsigned channel=0;channel<4u;channel++)event(channel,UINT32_C(0xa05f7800)+channel*32u+20u,0);
    event(4,UINT32_C(0xa05f7c14),0);
    event(5,MAPLE_ENABLE,0);
    for(unsigned bank=0;bank<3u;bank++)for(unsigned kind=0;kind<3u;kind++)
        event(6u+bank*3u+kind,UINT32_C(0xa05f6910)+bank*16u+kind*4u,0);
    for(unsigned channel=0;channel<4u;channel++)
        event(15u+channel,UINT32_C(0xffa0000c)+channel*16u,(UINT32_C(0x00004910)|(channel<<4))&~1u);
    event(19,UINT32_C(0xa0702c00),0x81u);event(20,CACHE_EVENT,0);
    CHECK(write_count==21u && *value(MAPLE_ENABLE)==0u && !*value(UINT32_C(0xa05f7c14)));
    reset();sr=0;CHECK(!kui_native_return_quiesce() && !write_count && !cache_calls);
    reset();*value(MAPLE_STATE)=1u;CHECK(!kui_native_return_quiesce() && !write_count && !cache_calls);
    reset();*value(UINT32_C(0xffa0001c))=1u;CHECK(!kui_native_return_quiesce() && !write_count && !cache_calls);
    reset();*value(UINT32_C(0xa05f7418))=1u;CHECK(!kui_native_return_quiesce() && !write_count && !cache_calls);
    for(unsigned channel=0;channel<4u;channel++) {
        reset();*value(UINT32_C(0xa05f7800)+channel*32u+24u)=1u;
        CHECK(!kui_native_return_quiesce() && !cache_calls && write_count==channel+1u);
        for(unsigned done=0;done<=channel;done++)event(done,UINT32_C(0xa05f7800)+done*32u+20u,0);
    }
    const uint32_t blocked[]={UINT32_C(0xa05f7c18),UINT32_C(0xa05f6820),UINT32_C(0xa05f6808)};
    for(unsigned i=0;i<sizeof(blocked)/sizeof(*blocked);i++) {
        reset();*value(blocked[i])=1u;*value(UINT32_C(0xa05f7c14))=1u;
        CHECK(!kui_native_return_quiesce() && !cache_calls && write_count==5u);
        event(4,UINT32_C(0xa05f7c14),0);
    }
    const uint32_t drain[]={MAPLE_STATE,UINT32_C(0xa05f7418),UINT32_C(0xa05f7818),
        UINT32_C(0xa05f7878),UINT32_C(0xa05f7c18),UINT32_C(0xa05f6820),UINT32_C(0xa05f6808)};
    for(unsigned i=0;i<sizeof(drain)/sizeof(*drain);i++) {
        reset();*value(drain[i])=1u;settle_address=drain[i];settle_reads=4u;
        CHECK(kui_native_return_quiesce() && cache_calls==1u && !*value(drain[i]));
        event(write_count-1u,CACHE_EVENT,0);
    }
}
int main(void) {
    controller();quiesce();
    printf("PASS terminal return hardware: %u checks; typed controller/Start, passive refusals, bounded confirmed/stuck aborts, no game VBlank resurrection, DMA drain and cache-off ordering\n",checks);
    return 0;
}
