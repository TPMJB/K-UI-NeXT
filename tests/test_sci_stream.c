/* SPDX-License-Identifier: GPL-3.0-only */
/* Host model of the game reader's SCI stream: an SPI-mode card answering
 * CMD18/CMD12 byte by byte, SCI registers with MSTP0 module reset, and DMA
 * channel 1 receiving bit-reversed bytes until the receiver overruns. */
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
        value = data_at(card.lba, f - card.gap - 1u);
        if(card.corrupt_count && card.lba == card.corrupt_lba && f - card.gap - 1u == 77u)
            value ^= 0x10u;
    } else {
        uint8_t block[512];
        for(unsigned i = 0; i < 512; ++i) block[i] = data_at(card.lba, i);
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
    unsigned rx_delay, delay, overrun_after, received;
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
            if(m.overrun_after) m.overrun_after = 0;
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
static struct kui_loader_sd sd;
static uint8_t areas[2][KUI_SCI_STREAM_AREA_BYTES] __attribute__((aligned(32)));

static void reset_model(void) {
    memset(&card, 0, sizeof(card));
    card.nac = 1; card.nac_first = 40; card.busy = 6; card.blocks = 100000; card.high_capacity = true;
    memset(&m, 0, sizeof(m));
    m.smr = 0x80u; m.brr = 0; m.scr = 0x30u; m.ssr = 0x84u; m.sptr = 0; m.cs_high = true; m.healthy = true;
    m.sar = 0x11111111u; m.dar = 0x22222222u; m.tcr = 0x33u; m.chcr = 0; m.dmaor = 0x8201u;
    m.areas[0] = areas[0]; m.areas[1] = areas[1];
    memset(&sd, 0, sizeof(sd));
    sd.bus.select = bus_select; sd.bus.transfer = bus_transfer;
    sd.high_capacity = true; sd.ready = true; sd.blocks = card.blocks;
    memset(areas, 0xa5, sizeof(areas));
}
static void open_stream(bool unknown) {
    assert(kui_sci_stream_open(&sd, areas[0], areas[1], unknown) == KUI_SCI_STREAM_OK);
}
static void check_block(uint32_t lba, const uint8_t *p) {
    assert(p);
    for(unsigned i = 0; i < 512; ++i) assert(p[i] == data_at(lba, i));
}
static void check_restored(void) {
    assert(m.scr == 0x30u && m.smr == 0x80u && m.brr == 0 && m.scmr == 0 && m.sptr == 0);
    assert(!(m.ssr & 0x78u) && !m.rx && !(m.stbcr & 1u));
}
static void check_channel_restored(void) {
    assert(m.sar == 0x11111111u && m.dar == 0x22222222u && m.tcr == 0x33u && m.chcr == 0);
}

/* A consumer as the resident uses it: take the next block, fetch the one
 * after it before copying, refetch after any failure. */
static unsigned drive(uint32_t first, uint32_t count, uint8_t *out, bool overlap, bool irq) {
    unsigned failures = 0;
    uint32_t want = first;
    while(want < first + count) {
        enum kui_sci_stream_result r;
        assert(failures < 20000u);
        if(kui_sci_stream_busy()) {
            r = kui_sci_stream_wait();
            if(r != KUI_SCI_STREAM_OK) {++failures; continue;}
        }
        const uint8_t *p = kui_sci_stream_take(want, &r);
        if(r == KUI_SCI_STREAM_CRC) {
            ++failures;
            if(kui_sci_stream_fetch(want, LIMIT, irq) != KUI_SCI_STREAM_OK) ++failures;
            continue;
        }
        if(p) {
            if(overlap && want + 1u < first + count &&
               kui_sci_stream_fetch(want + 1u, LIMIT, irq) != KUI_SCI_STREAM_OK) ++failures;
            memcpy(out + (size_t)(want - first) * 512u, p, 512);
            ++want;
            continue;
        }
        assert(r == KUI_SCI_STREAM_PENDING);
        if(kui_sci_stream_fetch(want, LIMIT, irq) != KUI_SCI_STREAM_OK) ++failures;
    }
    return failures;
}
static void check_range(uint32_t first, uint32_t count, const uint8_t *out) {
    for(uint32_t b = 0; b < count; ++b) check_block(first + b, out + (size_t)b * 512u);
}
static uint8_t out[512 * 300];

static void test_sequential(void) {
    reset_model();
    open_stream(false);
    const struct kui_sci_stream_stats *st = kui_sci_stream_stats();
    struct kui_sci_stream_stats before = *st;
    assert(drive(100, 64, out, true, false) == 0);
    check_range(100, 64, out);
    assert(st->starts - before.starts == 1 && st->continued - before.continued == 63);
    assert(st->blocks - before.blocks == 64 && st->stops == before.stops && !st->polled);
    assert(card.cmd18 == 1 && !card.cmd12 && m.dma_starts == 64 && m.module_resets == 64);
    assert(m.purges == 64 && !m.irq_starts && m.armed_chcr == 0x4911u);
    assert(st->max_token_bytes == 40);
    check_restored(); check_channel_restored();
    /* The stream continues where it paused, without another CMD18. */
    assert(drive(164, 10, out, false, true) == 0);
    check_range(164, 10, out);
    assert(card.cmd18 == 1 && m.irq_starts == 10 && m.armed_chcr == 0x4915u);
    /* Elsewhere: CMD12 with its busy interval, then a new CMD18. */
    assert(drive(5000, 3, out, true, false) == 0);
    check_range(5000, 3, out);
    assert(card.cmd18 == 2 && card.cmd12 == 1 && !card.idle_cmd12 && st->stops == 1);
    assert(kui_sci_stream_stop() == KUI_SCI_STREAM_OK);
    assert(card.cmd12 == 2 && !card.streaming && m.cs_high);
    check_restored(); check_channel_restored();
}
static void test_pending_and_kept(void) {
    reset_model();
    open_stream(false);
    m.delay = 3;
    enum kui_sci_stream_result r;
    assert(kui_sci_stream_fetch(7, LIMIT, true) == KUI_SCI_STREAM_OK && kui_sci_stream_busy());
    assert(!kui_sci_stream_take(7, &r) && r == KUI_SCI_STREAM_PENDING);
    assert(kui_sci_stream_poll() == KUI_SCI_STREAM_PENDING);
    assert(kui_sci_stream_fetch(8, LIMIT, true) == KUI_SCI_STREAM_BUSY);
    unsigned polls = 1;
    while((r = kui_sci_stream_poll()) == KUI_SCI_STREAM_PENDING) ++polls;
    assert(r == KUI_SCI_STREAM_OK && !kui_sci_stream_busy() && polls <= 4);
    const uint8_t *p = kui_sci_stream_take(7, &r);
    check_block(7, p);
    /* The next block arrives in the other area while block 7 stays usable;
     * a request that starts in block 7 again gets it without a second read. */
    assert(kui_sci_stream_fetch(8, LIMIT, true) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
    const uint8_t *again = kui_sci_stream_take(7, &r);
    assert(again == p && r == KUI_SCI_STREAM_OK);
    check_block(7, again);
    assert(kui_sci_stream_stats()->kept);
    /* Re-fetching the block after the kept one never overwrites the kept
     * area, even when the other area still holds an untaken block. */
    assert(kui_sci_stream_fetch(9, LIMIT, true) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
    check_block(7, again);
    check_block(9, kui_sci_stream_take(9, &r));
    check_block(9, kui_sci_stream_take(9, &r));
    assert(!kui_sci_stream_take(7, &r) && r == KUI_SCI_STREAM_PENDING);
    kui_sci_stream_discard();
    assert(!kui_sci_stream_take(9, &r) && r == KUI_SCI_STREAM_PENDING);
}
static void test_crc_and_token(void) {
    reset_model();
    open_stream(false);
    const struct kui_sci_stream_stats *st = kui_sci_stream_stats();
    struct kui_sci_stream_stats before = *st;
    card.corrupt_lba = 205; card.corrupt_count = 1;
    card.bad_token_lba = 210; card.bad_token_count = 1;
    assert(drive(200, 20, out, true, true) == 2);
    check_range(200, 20, out);
    assert(st->crc_errors - before.crc_errors == 1 && st->token_errors - before.token_errors == 1);
    assert(card.cmd18 == 3 && card.cmd12 == 2);
    check_restored(); check_channel_restored();
}
static void test_overrun_and_stall(void) {
    reset_model();
    open_stream(false);
    enum kui_sci_stream_result r;
    m.overrun_after = 300;
    assert(kui_sci_stream_fetch(40, LIMIT, true) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_poll() == KUI_SCI_STREAM_OVERRUN && !kui_sci_stream_busy());
    assert(m.module_resets == 1);
    check_restored(); check_channel_restored();
    assert(kui_sci_stream_stats()->overruns);
    /* The card is mid-block: CMD12 aborts it, and CMD18 starts again. */
    assert(kui_sci_stream_fetch(40, LIMIT, true) == KUI_SCI_STREAM_OK);
    assert(card.cmd12 == 1 && card.cmd18 == 2);
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
    check_block(40, kui_sci_stream_take(40, &r));
    /* A receiver that never finishes ends as an overrun after the bound. */
    m.stall = true;
    assert(kui_sci_stream_fetch(41, LIMIT, true) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OVERRUN);
    check_restored(); check_channel_restored();
    m.stall = false;
    assert(drive(41, 5, out, true, true) == 0);
    check_range(41, 5, out);
}
static void test_channel_busy_and_foreign(void) {
    reset_model();
    open_stream(false);
    enum kui_sci_stream_result r;
    const struct kui_sci_stream_stats *st = kui_sci_stream_stats();
    struct kui_sci_stream_stats before = *st;
    /* Someone else's transfer on channel 1: programmed reception instead. */
    m.chcr = 1u;
    unsigned bytes_before = bus_bytes;
    assert(kui_sci_stream_fetch(300, LIMIT, true) == KUI_SCI_STREAM_OK && !kui_sci_stream_busy());
    assert(st->polled - before.polled == 1 && m.chcr == 1u && !m.dma_starts);
    assert(bus_bytes - bytes_before >= 514u + 40u);
    check_block(300, kui_sci_stream_take(300, &r));
    assert(kui_sci_stream_fetch(301, LIMIT, true) == KUI_SCI_STREAM_OK);
    check_block(301, kui_sci_stream_take(301, &r));
    assert(card.cmd18 == 1 && st->continued - before.continued == 1);
    m.chcr = 0;
    /* The channel taken over mid-block: the SCI is still handed back. */
    m.foreign_during_rx = true;
    assert(kui_sci_stream_fetch(302, LIMIT, true) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_poll() == KUI_SCI_STREAM_BUSY);
    assert(st->foreign - before.foreign == 1 && m.sar == 0x0c200000u);
    check_restored();
    m.sar = 0x11111111u; m.dar = 0x22222222u; m.tcr = 0x33u; m.chcr = 0;
    assert(drive(302, 4, out, true, true) == 0);
    check_range(302, 4, out);
    check_channel_restored();
}
static void test_unknown_and_gapless(void) {
    /* After a bus fault the card may still stream: the first fetch stops it. */
    reset_model();
    card.streaming = true; card.lba = 77; card.gap = 3;
    open_stream(true);
    assert(drive(90, 3, out, true, false) == 0);
    check_range(90, 3, out);
    assert(card.cmd12 == 1 && !card.idle_cmd12 && card.cmd18 == 1);
    /* An idle card answers CMD12 with "illegal command": still stopped. */
    reset_model();
    open_stream(true);
    assert(drive(91, 2, out, true, false) == 0);
    check_range(91, 2, out);
    assert(card.idle_cmd12 == 1 && card.cmd18 == 1);
    /* A card with no gap between blocks loses each next token to the
     * receiver's overrun byte; every block then needs a restart. */
    reset_model();
    card.nac = 0;
    open_stream(false);
    unsigned failures = drive(500, 8, out, true, false);
    check_range(500, 8, out);
    assert(failures == 7 && card.cmd18 == 8);
}
static void test_byte_addressed(void) {
    reset_model();
    card.high_capacity = false; sd.high_capacity = false;
    open_stream(false);
    assert(drive(1234, 3, out, true, true) == 0);
    check_range(1234, 3, out);
}
static void test_random_faults(void) {
    reset_model();
    open_stream(false);
    srand(12345);
    uint32_t lba = 1000;
    for(unsigned round = 0; round < 400; ++round) {
        unsigned count = 1u + (unsigned)rand() % 40u;
        int fault = rand() % 10;
        if(fault == 0) {card.corrupt_lba = lba + (unsigned)rand() % count; card.corrupt_count = 1;}
        if(fault == 1) m.overrun_after = 1u + (unsigned)rand() % 512u;
        if(fault == 2) {card.bad_token_lba = lba + (unsigned)rand() % count; card.bad_token_count = 1;}
        if(fault == 3) m.chcr = 1u;
        m.delay = (unsigned)rand() % 4u;
        card.nac = 1u + (unsigned)rand() % 3u;
        bool irq = rand() & 1;
        (void)drive(lba, count, out, rand() % 4 != 0, irq);
        check_range(lba, count, out);
        if(fault == 3) m.chcr = 0;
        m.overrun_after = 0;
        check_restored();
        if(!(rand() % 5)) lba += 1000u + (unsigned)rand() % 5000u;
        else lba += count;
        if(lba > 90000u) lba = (unsigned)rand() % 1000u;
    }
    const struct kui_sci_stream_stats *st = kui_sci_stream_stats();
    printf("random: blocks %u polled %u starts %u continued %u kept %u overruns %u crc %u token %u\n",
        st->blocks, st->polled, st->starts, st->continued, st->kept, st->overruns, st->crc_errors, st->token_errors);
    assert(st->crc_errors && st->overruns && st->token_errors && st->polled);
    assert(kui_sci_stream_stop() == KUI_SCI_STREAM_OK);
    check_restored(); check_channel_restored();
}
static void test_bus_fault(void) {
    reset_model();
    m.sptr = 0x02u;
    assert(kui_sci_stream_open(&sd, areas[0], areas[1], false) == KUI_SCI_STREAM_RESET);
    reset_model();
    m.healthy = false;
    assert(kui_sci_stream_open(&sd, areas[0], areas[1], false) == KUI_SCI_STREAM_RESET);
    reset_model();
    open_stream(false);
    /* Beyond the card: CMD18 is rejected. */
    assert(kui_sci_stream_fetch(card.blocks, LIMIT, true) == KUI_SCI_STREAM_COMMAND);
    m.healthy = false;
    assert(kui_sci_stream_fetch(3, LIMIT, true) == KUI_SCI_STREAM_RESET);
}

int main(void) {
    test_sequential();
    test_pending_and_kept();
    test_crc_and_token();
    test_overrun_and_stall();
    test_channel_busy_and_foreign();
    test_unknown_and_gapless();
    test_byte_addressed();
    test_bus_fault();
    test_random_faults();
    puts("sci stream: ok");
    return 0;
}
