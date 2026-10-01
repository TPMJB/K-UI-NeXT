/* SPDX-License-Identifier: GPL-3.0-only */
#include "sci_sd_bus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define SMR 0xffe00000u
#define BRR 0xffe00004u
#define SCR 0xffe00008u
#define TDR 0xffe0000cu
#define SSR 0xffe00010u
#define RDR 0xffe00014u
#define PTR 0xffe00018u
#define STB 0xffc00004u
#define PCTR 0xff80002cu
#define PDTR 0xff800030u
static struct {
    uint8_t smr,brr,scr,tdr,ssr,rdr,ptr,stb;
    uint32_t pctr;
    uint16_t pdtr;
    unsigned writes, polls, bytes, delays;
    bool timeout, overrun;
} hw;
uint32_t kui_sci_sd_test_read(uint32_t address, unsigned width) {
    switch(address) {
        case SMR: assert(width==1); return hw.smr;
        case BRR: assert(width==1); return hw.brr;
        case SCR: assert(width==1); return hw.scr;
        case SSR: assert(width==1); ++hw.polls; return hw.ssr;
        case RDR: assert(width==1); return hw.rdr;
        case PTR: assert(width==1); return hw.ptr;
        case STB: assert(width==1); return hw.stb;
        case PCTR: assert(width==4); return hw.pctr;
        case PDTR: assert(width==2); return hw.pdtr;
        default: assert(!"Unexpected register read (DMA/timers forbidden)"); return 0;
    }
}
void kui_sci_sd_test_write(uint32_t address,uint32_t value,unsigned width) {
    ++hw.writes;
    switch(address) {
        case SMR: assert(width==1); hw.smr=value; break;
        case BRR: assert(width==1); hw.brr=value; break;
        case SCR: assert(width==1); hw.scr=value; break;
        case TDR: assert(width==1); hw.tdr=value; break;
        case SSR:
            assert(width==1);
            if(!(value&0x80) && (hw.ssr&0x80)) {
                assert(hw.scr==0x30 && hw.smr==0x80);
                ++hw.bytes;
                hw.ssr=hw.timeout?0:hw.overrun?0xa4:0xc4;
            } else hw.ssr&=value;
            break;
        case PTR: assert(width==1); hw.ptr=value; break;
        case STB: assert(width==1); hw.stb=value; break;
        case PCTR: assert(width==4); hw.pctr=value; break;
        case PDTR: assert(width==2); hw.pdtr=value; break;
        default: assert(!"Unexpected register write (DMA/timers forbidden)");
    }
}
void kui_sci_sd_test_delay(uint32_t count) { assert(count>=1024); ++hw.delays; }
static void reset(void) {
    kui_sci_sd_release(); memset(&hw,0,sizeof(hw));
    hw.smr=0x21; hw.brr=7; hw.scr=3; hw.ssr=0x84; hw.ptr=0x0a;
    hw.stb=0x81; hw.pctr=0xabcd1234; hw.pdtr=0x1256;
}
static void restored(void) {
    assert(hw.smr==0x21 && hw.brr==7 && hw.scr==3 && hw.ptr==0x0a);
    assert(hw.stb==0x81 && hw.pctr==0xabcd1234 && hw.pdtr==0x1256);
}
int main(void) {
    const struct kui_loader_sd_bus *bus=kui_sci_sd_bus(); assert(bus);
    for(unsigned bit=4;bit<=128;bit<<=1) {
        if(bit==8) continue;
        reset(); hw.scr|=bit;
        assert(kui_sci_sd_acquire()==KUI_LOADER_SD_UNSUPPORTED && !hw.writes);
    }
    for(unsigned bit=8;bit<=64;bit<<=1) {
        reset(); hw.ssr|=bit;
        assert(kui_sci_sd_acquire()==KUI_LOADER_SD_UNSUPPORTED && !hw.writes);
    }
    reset(); assert(kui_sci_sd_acquire()==KUI_LOADER_SD_OK);
    assert(kui_sci_sd_acquire()==KUI_LOADER_SD_NOT_READY);
    assert(hw.brr==31 && !(hw.stb&1) && (hw.pctr&0xc000)==0x4000 && (hw.pdtr&0x80));
    bus->select(NULL,true); assert(!(hw.pdtr&0x80));
    hw.rdr=0x96;
    uint32_t before=bus->ticks(NULL);
    assert(bus->transfer(NULL,0x12,true)==0x69 && hw.tdr==0x48 && hw.brr==31);
    assert(bus->ticks(NULL)-before==256);
    before=bus->ticks(NULL);
    assert(bus->transfer(NULL,0xa5,false)==0x69 && hw.tdr==0xa5 && hw.brr==0);
    assert(bus->ticks(NULL)-before==8);
    assert(kui_sci_sd_healthy() && hw.bytes==2);
    bus->select(NULL,false); assert(hw.pdtr&0x80);
    kui_sci_sd_release(); restored();
    unsigned writes=hw.writes; kui_sci_sd_release(); assert(hw.writes==writes);
    for(unsigned fault=0;fault<2;++fault) {
        reset(); assert(kui_sci_sd_acquire()==KUI_LOADER_SD_OK);
        hw.timeout=!fault; hw.overrun=fault;
        assert(bus->transfer(NULL,0xff,true)==0xff && !kui_sci_sd_healthy());
        assert(hw.polls<10010 && hw.scr==0);
        unsigned polls=hw.polls;
        assert(bus->transfer(NULL,0xff,true)==0xff && hw.polls==polls);
        kui_sci_sd_release(); restored();
    }
    puts("SCI SD bus: clocked byte order, ownership restoration and bounded failures passed");
}
