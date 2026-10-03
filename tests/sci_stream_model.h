/* SPDX-License-Identifier: GPL-3.0-only */
/* Host model shared by the stream and game-reader tests: an SPI-mode card
 * answering CMD18/CMD12 byte by byte, SCI registers with MSTP0 module reset,
 * and DMA channel 1 receiving bit-reversed bytes until the receiver overruns.
 * Include once, in one test file. */
#ifndef KUI_SCI_STREAM_MODEL_H
#define KUI_SCI_STREAM_MODEL_H
#include "sci_stream.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SMR 0xffe00000u
#define BRR 0xffe00004u
#define SCR 0xffe00008u
#define SSR 0xffe00010u
#define RDR 0xffe00014u
#define SCMR 0xffe00018u
#define SPTR 0xffe0001cu
#define STBCR 0xffc00004u
#define PDTR 0xff800030u
#define SAR 0xffa00010u
#define DAR 0xffa00014u
#define TCR 0xffa00018u
#define CHCR 0xffa0001cu
#define DMAOR 0xffa00040u
#define RDRF 0x40u
#define ORER 0x20u
#define AREA_BASE 0x0c100000u
#define LIMIT 4096u

static uint8_t rev8(uint8_t x) {
    uint8_t r = 0;
    for(unsigned i = 0; i < 8; ++i) if(x & (1u << i)) r |= (uint8_t)(0x80u >> i);
    return r;
}
static uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t crc = 0;
    for(size_t i = 0; i < n; ++i) {
        crc ^= (uint16_t)(p[i] << 8);
        for(unsigned b = 0; b < 8; ++b) crc = (uint16_t)(crc & 0x8000u ? (unsigned)(crc << 1) ^ 0x1021u : (unsigned)(crc << 1));
    }
    return crc;
}
static uint8_t data_at(uint32_t lba, unsigned i) {
    return (uint8_t)(lba * 7u + i * 13u + (i >> 8) * 3u + (lba >> 3) + 0x5a);
}
/* The card's bytes: data_at unless a test supplies an image. */
static uint8_t (*card_content)(uint32_t lba, unsigned i) = data_at;

/* ---- Card ---- */
static struct {
    bool streaming, start_after_queue, high_capacity;
    uint32_t lba, frame, gap, nac, nac_first, busy, blocks;
    uint8_t cmd[6], queue[64];
    unsigned cmd_len, qhead, qlen;
    uint32_t corrupt_lba, corrupt_count, bad_token_lba, bad_token_count;
    unsigned cmd18, cmd12, idle_cmd12, clocks;
} card;
static void enqueue(uint8_t value) { assert(card.qlen < sizeof(card.queue)); card.queue[(card.qhead + card.qlen++) % sizeof(card.queue)] = value; }
static uint8_t stream_byte(void) {
    uint32_t f = card.frame++;
    uint8_t value;
    if(f < card.gap) value = 0xff;
    else if(f == card.gap) {
        value = 0xfe;
        if(card.bad_token_count && card.lba == card.bad_token_lba) {--card.bad_token_count; value = 0xfc;}
    } else if(f <= card.gap + 512u) {
        value = card_content(card.lba, f - card.gap - 1u);
        if(card.corrupt_count && card.lba == card.corrupt_lba && f - card.gap - 1u == 77u)
            value ^= 0x10u;
    } else {
        uint8_t block[512];
        for(unsigned i = 0; i < 512; ++i) block[i] = card_content(card.lba, i);
        uint16_t crc = crc16(block, 512);
        value = f == card.gap + 513u ? (uint8_t)(crc >> 8) : (uint8_t)crc;
        if(f == card.gap + 514u) {
            if(card.corrupt_count && card.lba == card.corrupt_lba) --card.corrupt_count;
            ++card.lba; card.frame = 0; card.gap = card.nac;
        }
    }
    return value;
}
static void command(void) {
    uint8_t index = card.cmd[0] & 0x3fu;
    uint32_t argument = (uint32_t)card.cmd[1] << 24 | (uint32_t)card.cmd[2] << 16 |
        (uint32_t)card.cmd[3] << 8 | card.cmd[4];
    if(index == 12) {
        ++card.cmd12;
        if(!card.streaming && !card.start_after_queue) {
            ++card.idle_cmd12;
            enqueue(0xff); enqueue(0x04);
            return;
        }
        card.streaming = card.start_after_queue = false;
        card.qlen = 0;
        enqueue(0x3c); /* stuff byte: whatever was on the line */
        enqueue(0x00);
        for(uint32_t i = 0; i < card.busy; ++i) enqueue(0x00);
        return;
    }
    if(index == 18) {
        ++card.cmd18;
        assert(!card.streaming);
        uint32_t lba = card.high_capacity ? argument : argument >> 9;
        assert(card.high_capacity || !(argument & 511u));
        enqueue(0xff);
        if(lba >= card.blocks) {enqueue(0x40); return;}
        enqueue(0x00);
        card.lba = lba; card.frame = 0; card.gap = card.nac_first;
        card.start_after_queue = true;
        return;
    }
    assert(!"unexpected command");
}
static uint8_t card_clock(uint8_t mosi) {
    uint8_t out;
    ++card.clocks;
    if(card.qlen) {out = card.queue[card.qhead]; card.qhead = (card.qhead + 1u) % sizeof(card.queue); --card.qlen;}
    else if(card.start_after_queue) {card.start_after_queue = false; card.streaming = true; out = stream_byte();}
    else if(card.streaming) out = stream_byte();
    else out = 0xff;
    if(card.cmd_len || (mosi & 0xc0u) == 0x40u) {
        card.cmd[card.cmd_len++] = mosi;
        if(card.cmd_len == 6) {card.cmd_len = 0; command();}
    }
    return out;
}

/* ---- SCI, port and DMA channel 1 ---- */
static struct {
    uint8_t smr, brr, scr, ssr, rdr, scmr, sptr, stbcr;
    bool cs_high, healthy, rx, stall;
    uint32_t sar, dar, tcr, chcr, dmaor;
    unsigned rx_delay, delay, overrun_after, overrun_again, received;
    /* After an overrun the channel, back on the bus, takes the byte RDR
     * still held (as the console does); false: it stays held off until the
     * reception is stopped, and RDR keeps that byte. */
    bool late_take;
    bool foreign_during_rx;
    uint8_t *areas[2];
    unsigned purges, module_resets, dma_starts, irq_starts, settles;
    uint32_t armed_chcr;
} m;
static uint8_t *memory(uint32_t address) {
    for(unsigned i = 0; i < 2; ++i) {
        uint32_t base = AREA_BASE + i * 0x1000u;
        if(address >= base && address < base + KUI_SCI_STREAM_AREA_BYTES) return m.areas[i] + (address - base);
    }
    assert(!"DMA outside the receive areas");
    return NULL;
}
static void receive(void) {
    if(m.stall) return;
    m.rx = false;
    if(m.foreign_during_rx) {m.sar = 0x0c200000u; m.foreign_during_rx = false;}
    for(;;) {
        uint8_t wire = rev8(card_clock(0xff));
        ++m.received;
        bool dma = (m.chcr & 1u) && m.tcr && m.sar == (RDR & 0x1fffffffu) && (m.dmaor & 7u) == 1u &&
            !(m.overrun_after && 513u - m.tcr == m.overrun_after);
        if(dma) {
            *memory(m.dar++) = wire;
            if(!--m.tcr) m.chcr |= 2u;
        } else if(!(m.ssr & RDRF)) {
            m.rdr = wire; m.ssr |= RDRF;
        } else {
            m.ssr |= ORER;
            if(m.overrun_after) {m.overrun_after = m.overrun_again; m.overrun_again = 0;}
            if(m.late_take && (m.chcr & 1u) && m.tcr && m.sar == (RDR & 0x1fffffffu)) {
                *memory(m.dar++) = m.rdr;
                m.ssr &= (uint8_t)~RDRF;
                if(!--m.tcr) m.chcr |= 2u;
            }
            return;
        }
    }
}
static void advance(void) {
    if(!m.rx) return;
    if(m.rx_delay) {--m.rx_delay; return;}
    receive();
}
static void module_reset(void) {
    m.scr = 0; m.smr = 0; m.brr = 0xff; m.scmr = 0; m.ssr = 0x84; m.rdr = 0; m.rx = false;
    ++m.module_resets;
}
uint32_t kui_sci_stream_test_read(uint32_t address, unsigned width) {
    switch(address) {
    case SMR: assert(width == 1); return m.smr;
    case BRR: assert(width == 1); return m.brr;
    case SCR: assert(width == 1); return m.scr;
    case SSR: assert(width == 1); advance(); return m.ssr;
    case RDR: assert(width == 1); return m.rdr;
    case SCMR: assert(width == 1); return m.scmr;
    case SPTR: assert(width == 1); return m.sptr;
    case STBCR: assert(width == 1); return m.stbcr;
    case PDTR: assert(width == 2); return m.cs_high ? 0x80u : 0;
    case SAR: assert(width == 4); return m.sar;
    case DAR: assert(width == 4); advance(); return m.dar;
    case TCR: assert(width == 4); advance(); return m.tcr;
    case CHCR: assert(width == 4); advance(); return m.chcr;
    case DMAOR: assert(width == 4); return m.dmaor;
    default: assert(!"unexpected read"); return 0;
    }
}
void kui_sci_stream_test_write(uint32_t address, uint32_t value, unsigned width) {
    assert(!(m.stbcr & 1u) || address == STBCR || address >= SAR);
    switch(address) {
    case SMR: assert(width == 1); m.smr = (uint8_t)value; break;
    case BRR: assert(width == 1); m.brr = (uint8_t)value; break;
    case SCR:
        assert(width == 1);
        m.scr = (uint8_t)value;
        if(!(value & 0x10u)) m.rx = false;
        else if(value == 0x50u) {
            assert(m.sptr == 0x83u && m.cs_high == false);
            m.rx = true; m.rx_delay = m.delay; ++m.dma_starts;
            m.armed_chcr = m.chcr;
            if(m.chcr & 4u) ++m.irq_starts;
        }
        break;
    case SSR: assert(width == 1); m.ssr = (uint8_t)((m.ssr & 0x07u) | (m.ssr & value & 0xf8u)); break;
    case SCMR: assert(width == 1); m.scmr = (uint8_t)value; break;
    case SPTR: assert(width == 1); m.sptr = (uint8_t)value; break;
    case STBCR:
        assert(width == 1);
        if((value & 1u) && !(m.stbcr & 1u)) module_reset();
        m.stbcr = (uint8_t)value;
        break;
    case SAR: assert(width == 4); m.sar = value; break;
    case DAR: assert(width == 4); m.dar = value; break;
    case TCR: assert(width == 4); m.tcr = value; break;
    case CHCR: assert(width == 4); m.chcr = (value & ~2u) | (m.chcr & value & 2u); break;
    default: assert(!"unexpected write");
    }
}
void kui_sci_stream_test_settle(unsigned count) { (void)count; ++m.settles; }
uint32_t kui_sci_stream_test_physical(const void *area) {
    for(unsigned i = 0; i < 2; ++i) if(area == m.areas[i]) return AREA_BASE + i * 0x1000u;
    assert(!"unknown area");
    return 0;
}
void kui_sci_stream_test_purge(const void *area, unsigned bytes) {
    assert(bytes == KUI_SCI_STREAM_AREA_BYTES && !((uintptr_t)area & 31u));
    ++m.purges;
}
bool kui_sci_sd_healthy(void) { return m.healthy; }

/* ---- Bus: programmed bytes at the fast rate ---- */
static unsigned bus_bytes;
static uint8_t bus_transfer(void *ctx, uint8_t data, bool slow) {
    (void)ctx;
    if(!m.healthy) return 0xff; /* a latched bus fault */
    assert(!slow && !(m.stbcr & 1u));
    assert(m.brr == 0 && m.smr == 0x80u && !m.rx);
    m.scr = 0x30u;
    ++bus_bytes;
    return m.cs_high ? 0xffu : card_clock(data);
}
static void bus_select(void *ctx, bool selected) {
    (void)ctx;
    /* The bus waits for TEND before deselecting; a pending error would
     * latch a fault there. */
    if(!selected) assert(!(m.ssr & 0x38u) && (m.ssr & 0x04u));
    m.cs_high = !selected;
    if(!selected) card.cmd_len = 0;
}
#endif
