/* SPDX-License-Identifier: GPL-3.0-only */
/* Model native copy-back stores and tag discard, not console timing. The
 * real bus and lease owners run together so nested SR restoration is covered. */
#include "kui/toy_pilot_bus.h"
#include "kui/toy_pilot_lease.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define SOUND UINT32_C(0xa0800000)
#define SOUND_END UINT32_C(0xa09f4000)
#define LOWER UINT32_C(0xa0830040)
#define QUEUE UINT32_C(0xa080b200)
#define NOTIFY UINT32_C(0xa080b400)
#define FIFO UINT32_C(0xa05f688c)
#define G2_DMA UINT32_C(0xa05f7800)
#define TCNT0 UINT32_C(0xffd8000c)
#define HOST_QUEUE UINT32_C(0x8c112b08)
#define HOST_PRODUCER UINT32_C(0x8c112b0c)
#define HOST_NOTIFY UINT32_C(0x8c112b10)
#define HOST_COMMANDS UINT32_C(0x8c0a8924)
#define INSTALLED UINT32_C(0x8c0af74c)
#define MANAGER UINT32_C(0x8c10e55c)
#define TAIL UINT32_C(0x7e8)
#define RECORD_BYTES 20u
#define CANARY UINT32_C(0x54544352)
#define INITIAL_SR UINT32_C(0x60000353)
#define SR_BL UINT32_C(0x10000000)

enum event_kind { NATIVE_WRITE, SOUND_WRITE, PUBLISH, UNMASK };
static struct {
    uint32_t base, bytes;
    uint8_t cache[4096], ram[4096];
} native[4];
static struct {
    uint8_t sound[SOUND_END-SOUND];
    uint32_t sr, ticks, fifo_stuck, dma[32];
    unsigned sound_writes, native_writes, expire_write, stuck_write, discards;
    struct { enum event_kind kind; uint32_t address, bytes; } events[64];
    unsigned count;
} hw;
static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while(0)

static unsigned region(uint32_t address, unsigned bytes) {
    for(unsigned i=0;i<4u;i++)
        if(address>=native[i].base && address-native[i].base<native[i].bytes &&
           bytes<=native[i].bytes-(address-native[i].base)) return i;
    assert(false); return 0;
}
static uint32_t native_get(uint32_t address, unsigned bytes, bool backing) {
    unsigned i=region(address,bytes);
    uint32_t value=0;
    assert(bytes==2u || bytes==4u);
    memcpy(&value,(backing?native[i].ram:native[i].cache)+address-native[i].base,bytes);
    return value;
}
static void native_put(uint32_t address,uint32_t value,unsigned bytes,bool backing) {
    unsigned i=region(address,bytes);
    assert(bytes==2u || bytes==4u);
    memcpy(native[i].cache+address-native[i].base,&value,bytes);
    if(backing) memcpy(native[i].ram+address-native[i].base,&value,bytes);
}
static void native_discard(void) {
    for(unsigned i=0;i<4u;i++) memcpy(native[i].cache,native[i].ram,native[i].bytes);
    ++hw.discards;
}
static void event(enum event_kind kind,uint32_t address,uint32_t bytes) {
    assert(hw.count<sizeof(hw.events)/sizeof(hw.events[0]));
    hw.events[hw.count].kind=kind;
    hw.events[hw.count].address=address;
    hw.events[hw.count++].bytes=bytes;
}
static uint32_t sound_get(uint32_t address) {
    uint32_t value;
    assert(address>=SOUND && address<=SOUND_END-4u && !(address&3u));
    memcpy(&value,hw.sound+address-SOUND,4u);
    return value;
}
static void sound_put(uint32_t address,uint32_t value) {
    assert(address>=SOUND && address<=SOUND_END-4u && !(address&3u));
    memcpy(hw.sound+address-SOUND,&value,4u);
}
static void activity_reset(void) {
    hw.count=hw.sound_writes=hw.native_writes=hw.discards=0;
    hw.expire_write=hw.stuck_write=hw.fifo_stuck=0;
}
static void reset(void) {
    memset(&hw,0,sizeof(hw));
    memset(native,0,sizeof(native));
    native[0].base=MANAGER&~UINT32_C(31); native[0].bytes=4096u;
    native[1].base=HOST_QUEUE&~UINT32_C(31); native[1].bytes=32u;
    native[2].base=HOST_COMMANDS&~UINT32_C(31); native[2].bytes=32u;
    native[3].base=INSTALLED&~UINT32_C(31); native[3].bytes=32u;
    hw.sr=INITIAL_SR;
    native_put(INSTALLED,1u,4u,true);
    native_put(HOST_QUEUE,QUEUE,4u,true);
    native_put(HOST_PRODUCER,0xffffu,2u,true);
    native_put(HOST_NOTIFY,NOTIFY,4u,true);
    native_put(HOST_COMMANDS,77u,4u,true);
    native_put(MANAGER,1u,4u,true);
    native_put(MANAGER+4u,SOUND_END-LOWER,4u,true);
    native_put(MANAGER+8u,SOUND_END-LOWER,4u,true);
    native_put(MANAGER+12u,LOWER,4u,true);
    native_put(MANAGER+16u,LOWER,4u,true);
    native_put(MANAGER+20u,SOUND_END,4u,true);
}
static void sr_write(uint32_t value) {
    bool unmask=(hw.sr&0xf0u)==0xf0u && (value&0xf0u)!=0xf0u;
    hw.sr=value;
    if(unmask) {
        event(UNMASK,0u,0u);
        /* Model the title discarding native data-cache tags immediately
         * after the outer transaction. Nested bus return stays masked. */
        native_discard();
    }
}
uint32_t kui_toy_pilot_bus_test_sr_read(void) { return hw.sr; }
void kui_toy_pilot_bus_test_sr_write(uint32_t value) { sr_write(value); }
uint32_t kui_toy_pilot_lease_test_sr_read(void) { return hw.sr; }
void kui_toy_pilot_lease_test_sr_write(uint32_t value) { sr_write(value); }

uint32_t kui_toy_pilot_bus_test_read(uint32_t address,unsigned width) {
    assert((hw.sr&0xf0u)==0xf0u);
    if(address==HOST_PRODUCER) { assert(width==2u); return native_get(address,width,false); }
    assert(width==4u);
    if(address==TCNT0) return ~hw.ticks;
    if(address==FIFO) return hw.fifo_stuck;
    if(address>=G2_DMA && address<G2_DMA+0x80u) return hw.dma[(address-G2_DMA)/4u];
    if(address>=UINT32_C(0x8c000000) && address<UINT32_C(0x8d000000))
        return native_get(address,width,false);
    return sound_get(address);
}
void kui_toy_pilot_bus_test_write(uint32_t address,uint32_t value,unsigned width) {
    assert((hw.sr&0xf0u)==0xf0u);
    if(address==HOST_PRODUCER || address==HOST_COMMANDS) {
        assert(width==(address==HOST_PRODUCER?2u:4u));
        native_put(address,value,width,false); ++hw.native_writes;
        event(NATIVE_WRITE,address,width); return;
    }
    assert(width==4u);
    if(address>=QUEUE && address<QUEUE+512u && !(address&15u)) {
        /* Native producer ownership must reach RAM before the sound-side
         * command becomes visible, including a later stalled drain. */
        assert(hw.count && hw.events[hw.count-1u].kind==PUBLISH);
        assert(hw.events[hw.count-1u].address==HOST_PRODUCER &&
               hw.events[hw.count-1u].bytes==2u);
        assert(native_get(HOST_PRODUCER,2u,true)==(address-QUEUE)/16u);
    }
    sound_put(address,value); event(SOUND_WRITE,address,width);
    ++hw.sound_writes;
    if(hw.sound_writes==hw.expire_write) hw.ticks+=1563u;
    if(hw.sound_writes==hw.stuck_write) hw.fifo_stuck=0x31u;
}
uint32_t kui_toy_pilot_lease_test_read(uint32_t address) {
    assert((hw.sr&0xf0u)==0xf0u && address>=MANAGER && address<MANAGER+0xfb8u);
    return native_get(address,4u,false);
}
void kui_toy_pilot_lease_test_write(uint32_t address,uint32_t value) {
    assert((hw.sr&0xf0u)==0xf0u && address>=MANAGER && address<MANAGER+0xfb8u);
    assert(hw.sound_writes==1u && !hw.fifo_stuck);
    native_put(address,value,4u,false); ++hw.native_writes;
    event(NATIVE_WRITE,address,4u);
}
void kui_toy_pilot_cache_test_publish(uint32_t address,uint32_t bytes) {
    assert((hw.sr&0xf0u)==0xf0u && bytes && address+bytes>address);
    event(PUBLISH,address,bytes);
    /* OCBP publishes complete 32-byte lines, preserving unrelated dirty
     * native bytes sharing either boundary line. It never touches G2. */
    for(uint32_t line=address&~UINT32_C(31);line<address+bytes;line+=32u) {
        unsigned i=region(line,32u);
        memcpy(native[i].ram+line-native[i].base,native[i].cache+line-native[i].base,32u);
    }
}
static void published(uint32_t first,uint32_t first_bytes,uint32_t second,uint32_t second_bytes) {
    unsigned found=0;
    for(unsigned i=0;i<hw.count;i++) if(hw.events[i].kind==PUBLISH) {
        CHECK(hw.events[i].address==(found?second:first));
        CHECK(hw.events[i].bytes==(found?second_bytes:first_bytes));
        ++found;
    }
    CHECK(found==2u && hw.events[hw.count-1u].kind==UNMASK);
    CHECK(hw.sr==INITIAL_SR && hw.discards==1u);
}
static void no_publication(void) {
    for(unsigned i=0;i<hw.count;i++) CHECK(hw.events[i].kind!=PUBLISH);
    CHECK(!hw.native_writes && hw.sr==INITIAL_SR);
    CHECK(native_get(HOST_PRODUCER,2u,true)==0xffffu && native_get(HOST_COMMANDS,4u,true)==77u);
}
static void model_loss(void) {
    reset();
    native_put(HOST_PRODUCER,7u,2u,false);
    native_put(HOST_COMMANDS,100u,4u,false);
    native_put(MANAGER+4u,64u,4u,false);
    native_put(MANAGER+TAIL+12u,1u,4u,false);
    CHECK(native_get(HOST_PRODUCER,2u,false)==7u);
    native_discard();
    CHECK(native_get(HOST_PRODUCER,2u,false)==0xffffu);
    CHECK(native_get(HOST_COMMANDS,4u,false)==77u);
    CHECK(native_get(MANAGER+4u,4u,false)==SOUND_END-LOWER);
    CHECK(!native_get(MANAGER+TAIL+12u,4u,false));
}
static void queue_publication(void) {
    const uint32_t packet[4]={0x003eff90u,0x87654321u,0x11223344u,0xaabbccddu};
    const uint32_t producers[]={0xffffu,31u,7u};
    uint32_t slot;
    for(unsigned i=0;i<sizeof(producers)/sizeof(*producers);i++) {
        reset(); native_put(HOST_PRODUCER,producers[i],2u,true);
        native_put(HOST_PRODUCER+8u,0x12345678u,4u,false);
        native_put(HOST_COMMANDS-4u,0x87654321u,4u,false);
        slot=UINT32_MAX;
        uint32_t next=(producers[i]+1u)&31u;
        CHECK(kui_toy_pilot_bus_publish(packet,&slot)==KUI_TOY_PILOT_BUS_OK);
        published(HOST_PRODUCER,2u,HOST_COMMANDS,4u);
        CHECK(slot==QUEUE+next*16u && native_get(HOST_PRODUCER,2u,false)==next);
        CHECK(native_get(HOST_COMMANDS,4u,false)==78u);
        CHECK(native_get(HOST_PRODUCER+8u,4u,true)==0x12345678u);
        CHECK(native_get(HOST_COMMANDS-4u,4u,true)==0x87654321u);
        for(unsigned word=0;word<4u;word++) CHECK(sound_get(slot+word*4u)==packet[word]);
        activity_reset();
        CHECK(kui_toy_pilot_bus_publish(packet,&slot)==KUI_TOY_PILOT_BUS_OK);
        published(HOST_PRODUCER,2u,HOST_COMMANDS,4u);
        CHECK(slot==QUEUE+((next+1u)&31u)*16u && native_get(HOST_COMMANDS,4u,false)==79u);
    }
    reset(); hw.expire_write=3u; slot=UINT32_MAX;
    CHECK(kui_toy_pilot_bus_publish(packet,&slot)==KUI_TOY_PILOT_BUS_TIMEOUT);
    no_publication(); CHECK(slot==UINT32_MAX && !sound_get(QUEUE));
    reset(); sound_put(QUEUE,1u); slot=UINT32_MAX;
    CHECK(kui_toy_pilot_bus_publish(packet,&slot)==KUI_TOY_PILOT_BUS_BUSY);
    no_publication(); CHECK(slot==UINT32_MAX && !hw.sound_writes);
    reset(); native_put(INSTALLED,0u,4u,true);
    CHECK(kui_toy_pilot_bus_publish(packet,&slot)==KUI_TOY_PILOT_BUS_STATE);
    no_publication();
    reset(); hw.dma[0x14u/4u]=1u;
    CHECK(kui_toy_pilot_bus_publish(packet,&slot)==KUI_TOY_PILOT_BUS_BUSY);
    no_publication();
    reset(); hw.stuck_write=4u; slot=UINT32_MAX;
    CHECK(kui_toy_pilot_bus_publish(packet,&slot)==KUI_TOY_PILOT_BUS_PUBLISHED_STALLED);
    published(HOST_PRODUCER,2u,HOST_COMMANDS,4u);
    CHECK(slot==QUEUE && sound_get(QUEUE)==packet[0] && sound_get(NOTIFY)==UINT32_MAX);
    CHECK(native_get(HOST_PRODUCER,2u,false)==0u && native_get(HOST_COMMANDS,4u,false)==78u);
}
static void allocation_publication(void) {
    uint32_t address=UINT32_MAX,record=MANAGER+TAIL;
    reset();
    native_put(MANAGER+8u,SOUND_END-LOWER-4u,4u,true);
    native_put(MANAGER+8u,SOUND_END-LOWER,4u,false);
    native_put(record-4u,0x55667788u,4u,false);
    CHECK(kui_toy_pilot_lease_allocate(128u*1024u,32u,&address)==KUI_TOY_PILOT_BUS_OK);
    published(record,RECORD_BYTES,MANAGER+4u,4u);
    CHECK(address==UINT32_C(0xa09d3fe0) && hw.native_writes==6u);
    CHECK(native_get(record,4u,false)==address && native_get(record+4u,4u,false)==SOUND_END-4u);
    CHECK(native_get(record+8u,4u,false)==0x20020u && native_get(record+12u,4u,false)==1u);
    CHECK(!native_get(record+16u,4u,false) && sound_get(SOUND_END-4u)==CANARY);
    CHECK(native_get(MANAGER+4u,4u,false)==SOUND_END-LOWER-0x20020u);
    CHECK(native_get(MANAGER+8u,4u,true)==SOUND_END-LOWER);
    CHECK(native_get(record-4u,4u,true)==0x55667788u);
    activity_reset();
    native_put(record+16u,UINT32_C(0x8c06d700),4u,false);
    CHECK(kui_toy_pilot_lease_allocate(128u*1024u,32u,&address)==KUI_TOY_PILOT_BUS_OK);
    published(record+RECORD_BYTES,RECORD_BYTES,MANAGER+4u,4u);
    CHECK(address==UINT32_C(0xa09b3fc0));
    CHECK(native_get(record,4u,false)==UINT32_C(0xa09d3fe0));
    CHECK(native_get(record+RECORD_BYTES,4u,false)==address);
    CHECK(native_get(record+RECORD_BYTES+4u,4u,false)==UINT32_C(0xa09d3fdc));
    CHECK(native_get(record+RECORD_BYTES+8u,4u,false)==0x20020u);
    CHECK(native_get(record+RECORD_BYTES+12u,4u,false)==1u);
    CHECK(!native_get(record+RECORD_BYTES+16u,4u,false));
    CHECK(native_get(record+16u,4u,true)==UINT32_C(0x8c06d700));
    CHECK(native_get(MANAGER+4u,4u,false)==SOUND_END-LOWER-2u*0x20020u);
    for(unsigned mode=0;mode<3u;mode++) {
        reset(); address=UINT32_MAX;
        if(mode==0u) hw.stuck_write=1u; /* Canary written, final drain fails. */
        if(mode==1u) hw.dma[0x14u/4u]=1u;
        if(mode==2u) native_put(MANAGER+4u,64u,4u,true); /* Invalid native accounting. */
        const enum kui_toy_pilot_bus_result result[]={KUI_TOY_PILOT_BUS_TIMEOUT,
            KUI_TOY_PILOT_BUS_BUSY,KUI_TOY_PILOT_BUS_STATE};
        CHECK(kui_toy_pilot_lease_allocate(128u*1024u,32u,&address)==result[mode]);
        no_publication(); CHECK(address==UINT32_MAX && !native_get(record+12u,4u,false));
        CHECK(native_get(MANAGER+4u,4u,false)==(mode==2u?64u:SOUND_END-LOWER));
    }
    reset(); hw.sr=INITIAL_SR|SR_BL;
    CHECK(kui_toy_pilot_lease_allocate(128u,32u,&address)==KUI_TOY_PILOT_BUS_BUSY);
    CHECK(!hw.count && !hw.native_writes && !hw.sound_writes && hw.sr==(INITIAL_SR|SR_BL));
}
int main(void) {
    model_loss(); queue_publication(); allocation_publication();
    printf("Toy native cache publication: %u checks passed\n",checks);
    return 0;
}
