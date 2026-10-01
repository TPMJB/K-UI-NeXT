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
#define TDRE 0x80u
#define RDRF 0x40u
#define ORER 0x20u
#define TEND 0x04u
#define TE 0x20u
#define RE 0x10u
#define MAX_BYTES 2048u
#define SAR1 0xffa00010u
#define DAR1 0xffa00014u
#define TCR1 0xffa00018u
#define CHCR1 0xffa0001cu
#define DMAOR 0xffa00040u
#define DMA_BASE 0x0c100000u

static struct {
    uint8_t smr,brr,scr,tdr,ssr,rdr,ptr,stb;
    uint32_t pctr;
    uint16_t pdtr;
    unsigned writes, reads, polls, bytes, delays, received, tdr_writes;
    unsigned byte_polls, remaining, fault_byte;
    uint8_t sent[MAX_BYTES], receive_value, queued_value;
    uint32_t sar, dar, tcr, chcr, dmaor;
    unsigned dma_writes, dma_starts, dma_bytes, dma_complete_delay;
    unsigned cache_purges, irq_disables, irq_restores;
    uint8_t *dma_buffer;
    size_t dma_size;
    bool timeout, overrun, active, receive_enabled, patterned_rx, queued;
    bool dma_available, dma_stall, dma_completion_stall, dma_error, dma_late_error, irq_disabled;
} hw;

/* Deliberately independent of the driver's lookup/bit-twiddling method. */
static uint8_t reversed(uint8_t value) {
    uint8_t result=0;
    for(unsigned bit=0;bit<8;++bit) {
        result=(uint8_t)((result<<1)|(value&1u));
        value>>=1;
    }
    return result;
}
static uint8_t received_byte(unsigned index) {
    return (uint8_t)(index*37u+11u); /* Every value once per 256 bytes. */
}
/* SD's CRC16-CCITT, computed bit by bit as an independent test oracle. */
static uint16_t crc16_reference(const uint8_t *bytes,size_t count) {
    uint16_t crc=0;
    for(size_t i=0;i<count;++i) {
        crc^=(uint16_t)bytes[i]<<8;
        for(unsigned bit=0;bit<8;++bit)
            crc=(uint16_t)((crc<<1)^((crc&0x8000u)?0x1021u:0));
    }
    return crc;
}
struct profile_clock {
    uint64_t values[4];
    unsigned calls;
};
static struct profile_clock profile_clock_at(uint64_t start) {
    return (struct profile_clock){{start,start+11u,start+28u,start+57u},0};
}
static uint64_t profile_now(void *ctx) {
    struct profile_clock *clock=ctx;
    /* Reading the profiling clock must never interrupt the wire stream. */
    assert(!hw.active && !hw.queued && !(hw.scr&0xc0));
    assert(clock && clock->calls<4);
    return clock->values[clock->calls++];
}
static void same_profile(const struct kui_sci_sd_stats *a,const struct kui_sci_sd_stats *b) {
    assert(a->profiled_rx_blocks==b->profiled_rx_blocks && a->profiled_tx_blocks==b->profiled_tx_blocks);
    assert(a->rx_setup_us==b->rx_setup_us && a->rx_transfer_us==b->rx_transfer_us && a->rx_check_us==b->rx_check_us);
    assert(a->tx_setup_us==b->tx_setup_us && a->tx_transfer_us==b->tx_transfer_us);
}
static bool dma_running(unsigned request) {
    return (hw.chcr&3u)==1u && ((hw.chcr>>8)&15u)==request;
}
static uint8_t *dma_pointer(uint32_t address) {
    assert(hw.dma_buffer && address>=DMA_BASE && address-DMA_BASE<hw.dma_size);
    return hw.dma_buffer+(address-DMA_BASE);
}
static void start_byte(uint8_t value) {
    assert(!hw.active && hw.bytes<MAX_BYTES);
    hw.sent[hw.bytes]=value;
    hw.receive_value=hw.patterned_rx
        ? reversed(received_byte(hw.bytes)) : hw.rdr;
    ++hw.bytes;
    hw.receive_enabled=(hw.scr&RE)!=0;
    hw.active=true;
    hw.remaining=hw.byte_polls;
    hw.ssr&=(uint8_t)~TEND;
}
static void submit_byte(uint8_t value) {
    assert((hw.scr&TE) && hw.smr==0x80 && !hw.queued);
    hw.ssr&=(uint8_t)~TDRE;
    if(hw.active) {
        hw.queued=true;
        hw.queued_value=value;
    } else start_byte(value);
}
static void progress_byte(void) {
    /* A real holding register may be queued while the previous byte shifts.
     * This distinguishes TDRE from completion in both CPU and DMA paths. */
    if(hw.active && !(hw.fault_byte==hw.bytes && hw.timeout)) {
        assert(hw.remaining);
        if(!--hw.remaining) {
            hw.active=false;
            if(hw.dma_error && hw.fault_byte==hw.bytes) hw.dmaor|=4u;
            if(hw.fault_byte==hw.bytes && hw.overrun) hw.ssr|=ORER;
            else if(hw.receive_enabled) {
                if(hw.ssr&RDRF) hw.ssr|=ORER;
                hw.rdr=hw.receive_value;
                hw.ssr|=RDRF;
                if(dma_running(9) && (hw.scr&0x40) && !hw.dma_stall && (hw.dmaor&7u)==1u) {
                    assert(hw.sar==0x1fe00014 && hw.tcr);
                    *dma_pointer(hw.dar++)=hw.rdr;
                    ++hw.dma_bytes;
                    hw.ssr&=(uint8_t)~RDRF;
                    if(!--hw.tcr) hw.dma_complete_delay=2;
                }
            }
            if(hw.queued) {
                hw.queued=false;
                start_byte(hw.queued_value);
            } else hw.ssr|=TEND;
        }
    }
    if(!hw.queued) hw.ssr|=TDRE;
    if(hw.dma_complete_delay && !hw.dma_completion_stall && !--hw.dma_complete_delay) {
        hw.chcr|=2u;
        if(hw.dma_late_error) hw.dmaor|=4u;
    }
    if(dma_running(8) && hw.tcr && (hw.scr&0x80) && (hw.ssr&TDRE) &&
       !hw.dma_stall && (hw.dmaor&7u)==1u) {
        assert(hw.dar==0x1fe0000c && hw.tcr);
        submit_byte(*dma_pointer(hw.sar++));
        ++hw.dma_bytes;
        if(!--hw.tcr && !hw.dma_completion_stall) {
            hw.chcr|=2u; /* Last wire byte is still pending. */
            if(hw.dma_late_error) hw.dmaor|=4u;
        }
    }
}
uint32_t kui_sci_sd_test_irq_disable(void) {
    assert(!hw.irq_disabled);
    hw.irq_disabled=true; ++hw.irq_disables; return 0x1234u;
}
void kui_sci_sd_test_irq_restore(uint32_t saved) {
    assert(saved==0x1234u && hw.irq_disabled && !hw.active && !hw.queued);
    assert(!(hw.scr&0xc0));
    hw.irq_disabled=false; ++hw.irq_restores;
}
uint32_t kui_sci_sd_test_dma_address(const void *buffer,size_t count) {
    if(!hw.dma_available) return 0;
    assert(buffer && !((uintptr_t)buffer&31u) && count==512);
    hw.dma_buffer=(uint8_t *)(uintptr_t)buffer;
    hw.dma_size=count;
    return DMA_BASE;
}
void kui_sci_sd_test_cache_purge(void *buffer,size_t count) {
    assert(buffer==hw.dma_buffer && count==hw.dma_size && count==512);
    ++hw.cache_purges;
}
uint32_t kui_sci_sd_test_read(uint32_t address, unsigned width) {
    ++hw.reads;
    switch(address) {
        case SAR1: assert(width==4); return hw.sar;
        case DAR1: assert(width==4); return hw.dar;
        case TCR1: assert(width==4); progress_byte(); return hw.tcr;
        case CHCR1: assert(width==4); progress_byte(); return hw.chcr;
        case DMAOR: assert(width==4); return hw.dmaor;
        case SMR: assert(width==1); return hw.smr;
        case BRR: assert(width==1); return hw.brr;
        case SCR: assert(width==1); return hw.scr;
        case SSR:
            assert(width==1); ++hw.polls; progress_byte(); return hw.ssr;
        case RDR:
            assert(width==1 && (hw.ssr&RDRF)); ++hw.received; return hw.rdr;
        case PTR: assert(width==1); return hw.ptr;
        case STB: assert(width==1); return hw.stb;
        case PCTR: assert(width==4); return hw.pctr;
        case PDTR: assert(width==2); return hw.pdtr;
        default: assert(!"Unexpected register read (other DMA channels/timers forbidden)"); return 0;
    }
}
void kui_sci_sd_test_write(uint32_t address,uint32_t value,unsigned width) {
    ++hw.writes;
    switch(address) {
        case SAR1: case DAR1: case TCR1: case CHCR1:
            assert(width==4 && hw.dma_available && hw.irq_disabled);
            ++hw.dma_writes;
            if(address==SAR1) hw.sar=value;
            else if(address==DAR1) hw.dar=value;
            else if(address==TCR1) hw.tcr=value;
            else {
                if(value&1u) {
                    assert(value==0x4911 || value==0x1811);
                    assert(hw.tcr==512 && hw.cache_purges);
                    ++hw.dma_starts;
                }
                if(!(value&1u) && (hw.chcr&1u) && !hw.tcr &&
                   !hw.timeout && !hw.overrun && !hw.dma_stall &&
                   !hw.dma_completion_stall && !hw.dma_error) assert(hw.chcr&2u);
                hw.chcr=value;
                if(!(value&1u)) hw.dma_complete_delay=0;
            }
            break;
        case SMR: assert(width==1); hw.smr=value; break;
        case BRR: assert(width==1 && !hw.active); hw.brr=value; break;
        case SCR:
            assert(width==1);
            if(value&TE) {
                /* DMA request enables are permitted only with CPU interrupts
                 * masked; receive-only autonomous clocks are never allowed. */
                assert(value==TE || value==(TE|RE) ||
                       (hw.irq_disabled && (value==0x70 || value==0xa0)));
                assert(hw.smr==0x80);
            } else {
                assert(!(value&RE));
                if(hw.active) {
                    /* A fault may abort a transfer; normal mode changes must
                     * wait for the last bit first. */
                    assert(hw.fault_byte==hw.bytes || hw.dma_stall || (hw.ssr&ORER) || (hw.dmaor&6u));
                    hw.active=false;
                    hw.queued=false;
                    hw.ssr|=TDRE|TEND;
                }
            }
            hw.scr=value; break;
        case TDR:
            assert(width==1 && (hw.ssr&TDRE)); ++hw.tdr_writes; hw.tdr=value; break;
        case SSR: {
            assert(width==1);
            bool start=!(value&TDRE) && (hw.ssr&TDRE);
            hw.ssr&=value;
            if(start) submit_byte(hw.tdr);
            break;
        }
        case PTR: assert(width==1); hw.ptr=value; break;
        case STB: assert(width==1); hw.stb=value; break;
        case PCTR: assert(width==4); hw.pctr=value; break;
        case PDTR:
            assert(width==2);
            /* Deselect must not cut short a healthy wire transfer. */
            if(!(hw.pdtr&0x80) && (value&0x80)) assert(!hw.active);
            hw.pdtr=value; break;
        default: assert(!"Unexpected register write (global DMA/other channels/timers forbidden)");
    }
}
void kui_sci_sd_test_delay(uint32_t count) { assert(count>=32); ++hw.delays; }
static void reset(void) {
    kui_sci_sd_release(); memset(&hw,0,sizeof(hw));
    hw.smr=0x21; hw.brr=7; hw.scr=3; hw.ssr=TDRE|TEND; hw.ptr=0x0a;
    hw.stb=0x81; hw.pctr=0xabcd1234; hw.pdtr=0x1256;
    hw.byte_polls=3;
    hw.sar=0x0c002000; hw.dar=0x0c004000; hw.tcr=7;
    hw.chcr=0x4000; hw.dmaor=0x0301;
}
static void restored(void) {
    assert(hw.smr==0x21 && hw.brr==7 && hw.scr==3 && hw.ptr==0x0a);
    assert(hw.stb==0x81 && hw.pctr==0xabcd1234 && hw.pdtr==0x1256);
    assert(!hw.active && !hw.queued && !hw.irq_disabled);
    assert(hw.sar==0x0c002000 && hw.dar==0x0c004000 && hw.tcr==7);
    assert(hw.chcr==0x4000 && hw.dmaor==0x0301);
}
static void test_ownership(const struct kui_loader_sd_bus *bus) {
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
    assert(kui_sci_sd_healthy() && hw.bytes==2 && hw.received==2);
    bus->select(NULL,false); assert(hw.pdtr&0x80);
    kui_sci_sd_release(); restored();
    unsigned writes=hw.writes; kui_sci_sd_release(); assert(hw.writes==writes);

    reset(); assert(kui_sci_sd_acquire()==KUI_LOADER_SD_OK);
    /* Restore only the owned bits, preserving changes by other GPIO users. */
    hw.stb^=0x40; hw.pctr^=0x10000002; hw.pdtr^=0x0101;
    kui_sci_sd_release();
    assert(hw.stb==(0x81^0x40) && hw.pctr==(0xabcd1234^0x10000002));
    assert(hw.pdtr==(0x1256^0x0101));
    assert(hw.smr==0x21 && hw.brr==7 && hw.scr==3 && hw.ptr==0x0a);
}
static void test_scalar_failures(const struct kui_loader_sd_bus *bus) {
    for(unsigned fault=0;fault<2;++fault) {
        reset(); assert(kui_sci_sd_acquire()==KUI_LOADER_SD_OK);
        hw.timeout=!fault; hw.overrun=fault; hw.fault_byte=1;
        assert(bus->transfer(NULL,0xff,true)==0xff && !kui_sci_sd_healthy());
        assert(hw.polls<10020 && hw.scr==0);
        unsigned polls=hw.polls, writes=hw.writes;
        assert(bus->transfer(NULL,0xff,true)==0xff && hw.polls==polls && hw.writes==writes);
        kui_sci_sd_release(); restored();
    }
}
static void test_blocks(const struct kui_loader_sd_bus *bus) {
    uint8_t tx[512], rx[514];
    assert(bus->transfer_block);
    for(unsigned mode=0;mode<3;++mode) {
        for(size_t count=1;count<=512;++count) {
            for(unsigned variant=0;variant<4;++variant) {
                unsigned slow=variant&1u;
                bool with_crc=(variant&2u)!=0;
                reset(); assert(kui_sci_sd_acquire()==KUI_LOADER_SD_OK);
                hw.patterned_rx=true;
                /* Exercise both minimum and delayed readiness. */
                hw.byte_polls=slow?5:2;
                bus->select(NULL,true);
                for(size_t i=0;i<count;++i) tx[i]=(uint8_t)i;
                memset(rx,0x5a,sizeof(rx));
                uint32_t before=bus->ticks(NULL);
                unsigned delays=hw.delays;
                uint16_t crc=0xa55a;
                assert(bus->transfer_block(NULL,mode==0?NULL:tx,mode==1?NULL:rx+1,count,slow!=0,with_crc?&crc:NULL));
                assert(crc==(with_crc?crc16_reference(mode==1?tx:rx+1,count):0xa55a));
                assert(kui_sci_sd_healthy() && hw.bytes==count && !hw.active);
                assert(bus->ticks(NULL)-before==count*(slow?256u:8u));
                assert(hw.brr==(slow?31:0) && !(hw.pdtr&0x80));
                assert(hw.delays==delays+(slow?0:1));
                for(size_t i=0;i<count;++i) {
                    assert(hw.sent[i]==(mode==0?0xff:reversed(tx[i])));
                    if(mode!=1) assert(rx[i+1]==received_byte((unsigned)i));
                }
                assert(rx[0]==0x5a && rx[count+1]==0x5a);
                if(mode==1) {
                    for(size_t i=0;i<sizeof(rx);++i) assert(rx[i]==0x5a);
                } else assert(hw.received==count);
                /* A block must hand the port back to ordinary command IO,
                 * including the write-only path which may disable receive. */
                assert(bus->transfer(NULL,0x40,slow!=0)==received_byte((unsigned)count));
                assert(hw.sent[count]==reversed(0x40) && hw.bytes==count+1);
                bus->select(NULL,false);
                assert(hw.pdtr&0x80);
                kui_sci_sd_release(); restored();
            }
        }
    }
    reset(); assert(kui_sci_sd_acquire()==KUI_LOADER_SD_OK);
    hw.patterned_rx=true;
    for(size_t i=0;i<sizeof(tx);++i) tx[i]=(uint8_t)i;
    uint16_t crc=0xa55a;
    assert(bus->transfer_block(NULL,tx,tx,sizeof(tx),false,&crc));
    assert(crc==crc16_reference(tx,sizeof(tx)));
    for(size_t i=0;i<sizeof(tx);++i) {
        assert(hw.sent[i]==reversed((uint8_t)i));
        assert(tx[i]==received_byte((unsigned)i));
    }
    /* Fast -> slow -> fast changes occur only at a completed-byte boundary. */
    assert(bus->transfer_block(NULL,NULL,rx,1,true,NULL) && hw.brr==31);
    assert(bus->transfer_block(NULL,NULL,rx,1,false,NULL) && hw.brr==0);
    kui_sci_sd_release(); restored();
}
static void test_block_arguments(const struct kui_loader_sd_bus *bus) {
    uint8_t byte=0x5a;
    uint16_t crc=0xa55a;
    struct profile_clock clock=profile_clock_at(0);
    struct kui_sci_sd_stats before,after;
    kui_sci_sd_profile_timer(profile_now,&clock);
    kui_sci_sd_stats_get(&before);
    reset();
    unsigned reads=hw.reads, writes=hw.writes;
    assert(!bus->transfer_block(NULL,NULL,&byte,1,false,&crc));
    assert(hw.reads==reads && hw.writes==writes && byte==0x5a && crc==0xa55a);
    assert(kui_sci_sd_acquire()==KUI_LOADER_SD_OK);
    reads=hw.reads; writes=hw.writes;
    uint32_t ticks=bus->ticks(NULL);
    assert(!bus->transfer_block(NULL,NULL,NULL,1,false,&crc));
    assert(!bus->transfer_block(NULL,&byte,NULL,0,false,&crc));
    assert(!bus->transfer_block(NULL,NULL,&byte,513,false,&crc));
    assert(hw.reads==reads && hw.writes==writes && byte==0x5a && crc==0xa55a);
    assert(bus->ticks(NULL)==ticks && kui_sci_sd_healthy());
    kui_sci_sd_stats_get(&after); same_profile(&before,&after);
    assert(clock.calls==0);
    kui_sci_sd_profile_timer(NULL,NULL);
    kui_sci_sd_release(); restored();
}
static void test_block_failures(const struct kui_loader_sd_bus *bus) {
    uint8_t tx[512]={0}, rx[512];
    for(unsigned fault=0;fault<2;++fault) {
        for(unsigned writing=0;writing<2;++writing) {
            reset(); assert(kui_sci_sd_acquire()==KUI_LOADER_SD_OK);
            hw.patterned_rx=true; hw.fault_byte=137;
            hw.timeout=!fault; hw.overrun=fault;
            memset(rx,0x5a,sizeof(rx));
            bus->select(NULL,true);
            uint16_t crc=0xa55a;
            assert(!bus->transfer_block(NULL,writing?tx:NULL,writing?NULL:rx,sizeof(rx),false,&crc));
            assert(crc==0xa55a);
            assert(!kui_sci_sd_healthy() && hw.scr==0 && !hw.active);
            assert(hw.bytes==137 && hw.polls<12000);
            if(!writing) {
                for(unsigned i=0;i<136;++i) assert(rx[i]==received_byte(i));
                for(size_t i=136;i<sizeof(rx);++i) assert(rx[i]==0x5a);
            }
            unsigned reads=hw.reads, writes=hw.writes;
            assert(!bus->transfer_block(NULL,NULL,rx,1,false,&crc));
            assert(crc==0xa55a);
            assert(bus->transfer(NULL,0xff,false)==0xff);
            assert(hw.reads==reads && hw.writes==writes && hw.bytes==137);
            kui_sci_sd_release(); restored();
            /* The latch is lease-local: reacquiring restores operation. */
            hw.timeout=false; hw.overrun=false; hw.fault_byte=0;
            assert(kui_sci_sd_acquire()==KUI_LOADER_SD_OK);
            assert(bus->transfer(NULL,0xff,false)==received_byte(137));
            kui_sci_sd_release(); restored();
        }
    }
}
static void dma_ready(const struct kui_loader_sd_bus *bus) {
    reset(); assert(kui_sci_sd_acquire()==KUI_LOADER_SD_OK);
    hw.patterned_rx=true;
    assert(bus->transfer(NULL,0xff,false)==received_byte(0));
    hw.bytes=0; hw.received=0; hw.polls=0; hw.tdr_writes=0;
    hw.dma_available=true;
    bus->select(NULL,true);
}
static void test_dma_blocks(const struct kui_loader_sd_bus *bus) {
    _Alignas(32) uint8_t storage[576], tx[512];
    uint8_t *rx=storage+32;
    for(size_t i=0;i<sizeof(tx);++i) tx[i]=(uint8_t)i;
    for(unsigned writing=0;writing<2;++writing) {
        for(unsigned with_crc=0;with_crc<2;++with_crc) {
            dma_ready(bus);
            memset(storage,0x5a,sizeof(storage));
            uint32_t before=bus->ticks(NULL);
            uint16_t crc=0xa55a;
            assert(bus->transfer_block(NULL,writing?tx:NULL,writing?NULL:rx,512,false,with_crc?&crc:NULL));
            assert(crc==(with_crc?crc16_reference(writing?tx:rx,512):0xa55a));
            assert(kui_sci_sd_healthy() && hw.bytes==512 && hw.dma_bytes==512);
            assert(hw.dma_starts==1 && hw.cache_purges==1 && hw.received==0);
            assert(hw.tdr_writes==(writing?0u:1u)); /* One RX dummy seed, 512 wire bytes. */
            assert(hw.irq_disables==hw.irq_restores && !hw.irq_disabled);
            assert(!hw.active && !hw.queued && hw.chcr==0x4000 && hw.tcr==7);
            assert(hw.sar==0x0c002000 && hw.dar==0x0c004000 && hw.dmaor==0x0301);
            assert(bus->ticks(NULL)-before==512u*8u);
            for(unsigned i=0;i<512;++i) {
                assert(hw.sent[i]==(writing?reversed(tx[i]):0xff));
                if(!writing) assert(rx[i]==received_byte(i));
            }
            for(unsigned i=0;i<32;++i) assert(storage[i]==0x5a && storage[i+544]==0x5a);
            assert(bus->transfer(NULL,0x40,false)==received_byte(512));
            assert(hw.sent[512]==reversed(0x40));
            bus->select(NULL,false);
            kui_sci_sd_release(); restored();
        }
    }
}
static void test_dma_unavailable(const struct kui_loader_sd_bus *bus) {
    _Alignas(32) uint8_t rx[512];
    const uint32_t chcr_states[]={0x4001,0x4002,0x4004};
    const uint32_t dmaor_states[]={0x0300,0x0303,0x0305};
    for(unsigned which=0;which<2;++which) {
        for(unsigned state=0;state<3;++state) {
            dma_ready(bus);
            if(which) hw.dmaor=dmaor_states[state];
            else hw.chcr=chcr_states[state];
            uint32_t chcr=hw.chcr, dmaor=hw.dmaor;
            uint16_t crc=0xa55a;
            assert(bus->transfer_block(NULL,NULL,rx,512,false,&crc));
            assert(crc==crc16_reference(rx,512));
            assert(hw.bytes==512 && !hw.dma_writes && !hw.dma_starts && !hw.cache_purges);
            assert(hw.chcr==chcr && hw.dmaor==dmaor);
            assert(hw.irq_disables==hw.irq_restores && !hw.irq_disabled);
            for(unsigned i=0;i<512;++i) assert(rx[i]==received_byte(i));
            kui_sci_sd_release();
            assert(hw.chcr==chcr && hw.dmaor==dmaor);
            hw.chcr=0x4000; hw.dmaor=0x0301; restored();
        }
    }
}
static void test_dma_failures(const struct kui_loader_sd_bus *bus) {
    _Alignas(32) uint8_t storage[576], tx[512]={0};
    for(unsigned writing=0;writing<2;++writing) {
        for(unsigned fault=0;fault<6;++fault) {
            dma_ready(bus);
            memset(storage,0x5a,sizeof(storage));
            hw.fault_byte=137;
            hw.timeout=fault==0; hw.overrun=fault==1; hw.dma_stall=fault==2;
            hw.dma_completion_stall=fault==3; hw.dma_error=fault==4;
            hw.dma_late_error=fault==5; /* AE and TE become visible together. */
            struct profile_clock clock=profile_clock_at(1000);
            struct kui_sci_sd_stats before,after;
            kui_sci_sd_profile_timer(profile_now,&clock);
            kui_sci_sd_stats_get(&before);
            uint16_t crc=0xa55a;
            assert(!bus->transfer_block(NULL,writing?tx:NULL,writing?NULL:storage+32,512,false,&crc));
            assert(crc==0xa55a);
            kui_sci_sd_stats_get(&after); same_profile(&before,&after);
            assert(!kui_sci_sd_healthy() && hw.scr==0 && !hw.active && !hw.queued);
            assert(hw.bytes<=((fault==3 || fault==5)?512u:139u) && hw.polls<30000 && hw.dma_starts==1);
            if(fault==3) assert(hw.bytes==512 && hw.dma_bytes==512);
            if(fault==5) assert(hw.dma_bytes==512);
            assert(hw.dmaor==(fault>=4?0x0305u:0x0301u));
            assert(hw.irq_disables==hw.irq_restores && !hw.irq_disabled);
            assert(hw.sar==0x0c002000 && hw.dar==0x0c004000 && hw.tcr==7 && hw.chcr==0x4000);
            for(unsigned i=0;i<32;++i) assert(storage[i]==0x5a && storage[i+544]==0x5a);
            unsigned reads=hw.reads, writes=hw.writes;
            assert(!bus->transfer_block(NULL,NULL,storage+32,512,false,&crc));
            assert(crc==0xa55a);
            assert(bus->transfer(NULL,0xff,false)==0xff);
            assert(hw.reads==reads && hw.writes==writes);
            kui_sci_sd_profile_timer(NULL,NULL);
            kui_sci_sd_release();
            assert(hw.dmaor==(fault>=4?0x0305u:0x0301u));
            hw.dmaor=0x0301; restored();
        }
    }
}
static void test_dma_profile(const struct kui_loader_sd_bus *bus) {
    _Alignas(32) uint8_t rx[512],tx[512]={0};
    struct kui_sci_sd_stats before,after;
    struct profile_clock clock=profile_clock_at(0);
    /* NULL unregisters a previously installed clock; ordinary transfers
     * must not call it or add timing samples while profiling is disabled. */
    kui_sci_sd_profile_timer(profile_now,&clock);
    kui_sci_sd_profile_timer(NULL,NULL);
    dma_ready(bus);
    kui_sci_sd_stats_get(&before);
    assert(bus->transfer_block(NULL,NULL,rx,512,false,NULL));
    kui_sci_sd_stats_get(&after); same_profile(&before,&after);
    assert(clock.calls==0);
    kui_sci_sd_release(); restored();
    for(unsigned writing=0;writing<2;++writing) {
        dma_ready(bus);
        /* The RX sequence crosses the uint64 counter boundary. */
        clock=profile_clock_at(writing?1000:UINT64_MAX-12u);
        kui_sci_sd_profile_timer(profile_now,&clock);
        kui_sci_sd_stats_get(&before);
        assert(bus->transfer_block(NULL,writing?tx:NULL,writing?NULL:rx,512,false,NULL));
        kui_sci_sd_stats_get(&after);
        assert(clock.calls==(writing?3u:4u));
        assert(after.profiled_rx_blocks==before.profiled_rx_blocks+!writing);
        assert(after.profiled_tx_blocks==before.profiled_tx_blocks+writing);
        assert(after.rx_setup_us==before.rx_setup_us+(writing?0u:11u));
        assert(after.rx_transfer_us==before.rx_transfer_us+(writing?0u:17u));
        assert(after.rx_check_us==before.rx_check_us+(writing?0u:29u));
        assert(after.tx_setup_us==before.tx_setup_us+(writing?11u:0u));
        assert(after.tx_transfer_us==before.tx_transfer_us+(writing?17u:0u));
        assert(hw.bytes==512 && hw.dma_bytes==512 && !hw.active && !hw.queued);
        kui_sci_sd_profile_timer(NULL,NULL);
        kui_sci_sd_release(); restored();
    }
    dma_ready(bus);
    clock=profile_clock_at(0);
    kui_sci_sd_profile_timer(profile_now,&clock);
    kui_sci_sd_stats_get(&before);
    assert(bus->transfer_block(NULL,NULL,rx,16,false,NULL));
    kui_sci_sd_stats_get(&after); same_profile(&before,&after);
    assert(clock.calls==0 && hw.bytes==16 && !hw.dma_starts);
    kui_sci_sd_profile_timer(NULL,NULL);
    kui_sci_sd_release(); restored();
}
int main(void) {
    const struct kui_loader_sd_bus *bus=kui_sci_sd_bus(); assert(bus);
    assert(crc16_reference((const uint8_t *)"123456789",9)==0x31c3);
    test_ownership(bus);
    test_scalar_failures(bus);
    test_blocks(bus);
    test_block_arguments(bus);
    test_block_failures(bus);
    test_dma_blocks(bus);
    test_dma_unavailable(bus);
    test_dma_failures(bus);
    test_dma_profile(bus);
    puts("SCI SD bus: polled/DMA data, CRC, profiling, bounded faults and ownership restoration passed");
}
