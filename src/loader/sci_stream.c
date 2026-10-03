/* SPDX-License-Identifier: GPL-3.0-only
 * Original K-UI implementation of the receive-only SCI stream that the SCI
 * async probe proved on the console (513-byte DMA, RDR tail byte, MSTP0 reset
 * between blocks; docs/evidence/sci-async-cmd18-stream-2026-10-02.md). */
#include "sci_stream.h"
#include "sci_sd_bus.h"

#define SMR UINT32_C(0xffe00000)
#define BRR UINT32_C(0xffe00004)
#define SCR UINT32_C(0xffe00008)
#define SSR UINT32_C(0xffe00010)
#define RDR UINT32_C(0xffe00014)
#define SCMR UINT32_C(0xffe00018)
#define SPTR UINT32_C(0xffe0001c)
#define STBCR UINT32_C(0xffc00004)
#define PDTR UINT32_C(0xff800030)
#define SAR UINT32_C(0xffa00010)
#define DAR UINT32_C(0xffa00014)
#define TCR UINT32_C(0xffa00018)
#define CHCR UINT32_C(0xffa0001c)
#define DMAOR UINT32_C(0xffa00040)
/* Channel 1: SCI receive request, byte units, destination incrementing; no
 * completion interrupt (the game's DMAC level stays as it is). RIE+RE then
 * clock the card without a transmitter; with SPTR.EIO only the receiver's
 * error (ERI) reaches the CPU, at the overrun that ends every reception. */
#define RX_DMA UINT32_C(0x4911)
#define RDRF 0x40u
#define ORER 0x20u
#define FLAGS 0x38u
#define TEND 0x04u
#define DMA_BYTES 513u
#define AREA_BYTES KUI_SCI_STREAM_AREA_BYTES

#ifdef KUI_SCI_STREAM_TEST
extern uint32_t kui_sci_stream_test_read(uint32_t address, unsigned width);
extern void kui_sci_stream_test_write(uint32_t address, uint32_t value, unsigned width);
extern void kui_sci_stream_test_settle(unsigned count);
extern uint32_t kui_sci_stream_test_physical(const void *area);
extern void kui_sci_stream_test_purge(const void *area, unsigned bytes);
extern void kui_sci_stream_test_fence(void);
#define rd8(a) ((uint8_t)kui_sci_stream_test_read(a, 1))
#define rd16(a) ((uint16_t)kui_sci_stream_test_read(a, 2))
#define rd32(a) kui_sci_stream_test_read(a, 4)
#define wr8(a,v) kui_sci_stream_test_write(a, (uint8_t)(v), 1)
#define wr32(a,v) kui_sci_stream_test_write(a, v, 4)
#define settle kui_sci_stream_test_settle
#define physical kui_sci_stream_test_physical
#define purge kui_sci_stream_test_purge
#define fence(area) kui_sci_stream_test_fence()
#else
#define rd8(a) (*(volatile uint8_t *)(uintptr_t)(a))
#define rd16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define rd32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define wr8(a,v) (rd8(a) = (uint8_t)(v))
#define wr32(a,v) (rd32(a) = (uint32_t)(v))
static void settle(unsigned n) {
    __asm__ __volatile__("1: dt %0\n\tbf 1b" : "+r"(n) : : "t", "memory");
}
static uint32_t physical(const void *area) { return (uint32_t)(uintptr_t)area & UINT32_C(0x1fffffff); }
static void purge(const void *area, unsigned bytes) {
    for(uintptr_t p = (uintptr_t)area; p < (uintptr_t)area + bytes; p += 32u)
        __asm__ __volatile__("ocbp @%0" : : "r"(p) : "memory");
}
/* An uncached read of main memory waits for the external bus, and the DMAC
 * goes before the CPU there: a transfer the channel had begun (RDR read, the
 * write held off by the game's own DMA) has landed when it returns. Stopping
 * the channel does not cancel such a transfer, so finish fences both before
 * the stop (requests it still takes) and after it (before counting). */
static void fence(const void *area) {
    (void)*(volatile uint32_t *)(((uintptr_t)area & UINT32_C(0x1fffffff)) | UINT32_C(0xa0000000));
}
#endif

typedef uint32_t alias_word __attribute__((__may_alias__));
enum { CLOSED, PAUSED, DMA, LOST };
#ifdef KUI_RETAIL_ASYNC
/* The game reader keeps this state between its interrupt vectors. */
#include "retail_async.h"
#define s (kui_retail_async_region.shared.stream)
#else
static struct kui_sci_stream_state s;
#endif

static uint8_t byte(uint8_t value) { return s.card->bus.transfer(s.card->bus.ctx, value, false); }
static void select(bool selected) { s.card->bus.select(s.card->bus.ctx, selected); }
static uint32_t reverse(uint32_t x) {
    /* Each byte's bits in reverse order, bytes in place: SCI is LSB first. */
    uint32_t t = (x ^ (x >> 1)) & UINT32_C(0x55555555);
    x ^= t ^ (t << 1);
    t = (x ^ (x >> 2)) & UINT32_C(0x33333333);
    x ^= t ^ (t << 2);
    t = (x ^ (x >> 4)) & UINT32_C(0x0f0f0f0f);
    return x ^ t ^ (t << 4);
}
static inline uint16_t crc16(uint16_t crc, uint8_t data) {
    uint32_t x = (crc >> 8) ^ data;
    x ^= x >> 4;
    return (uint16_t)((crc << 8) ^ (x << 12) ^ (x << 5) ^ x);
}
static uint8_t crc7(const uint8_t *p) {
    unsigned c = 0;
    for(unsigned i = 0; i < 5; ++i) for(unsigned j = 0; j < 8; ++j) {
        c <<= 1;
        if(((p[i] >> (7u - j)) ^ (c >> 7)) & 1u) c ^= 9u;
    }
    return (uint8_t)((c << 1) | 1u);
}
/* Bus faults latch: the card is then unusable until reinitialized. */
static bool healthy(void) { return kui_sci_sd_healthy(); }
static bool channel_idle(void) { return !(rd32(CHCR) & 7u) && (rd32(DMAOR) & 7u) == 1u; }

enum kui_sci_stream_result kui_sci_stream_open(const struct kui_loader_sd *card,
    uint8_t *area0, uint8_t *area1, bool unknown) {
    s.card = card;
    s.area[0] = area0; s.area[1] = area1;
    s.state = unknown ? LOST : CLOSED;
    s.ready[0] = s.ready[1] = 0; s.kept = 0; s.unrepaired = 0;
    s.sptr = rd8(SPTR);
    /* A deselected idle byte at the fast rate: the bus leaves its slow
     * acquisition rate and the card sees whole bytes. */
    select(false);
    (void)byte(0xff);
    if(!healthy() || (s.sptr & 0x0au) || rd8(SCR) != 0x30u || rd8(BRR) != 0)
        return KUI_SCI_STREAM_RESET;
    return KUI_SCI_STREAM_OK;
}
bool kui_sci_stream_busy(void) { return s.state == DMA; }
const struct kui_sci_stream_stats *kui_sci_stream_stats(void) { return &s.stats; }
void kui_sci_stream_discard(void) { s.ready[0] = s.ready[1] = 0; s.kept = 0; }

/* Data token after the card's wait (0xff); TEND before the DMA starts. */
static enum kui_sci_stream_result token(uint32_t limit) {
    for(uint32_t n = 0; n < limit; ++n) {
        uint8_t value = byte(0xff);
        if(value == 0xfe) {
            if(n > s.stats.max_token_bytes) s.stats.max_token_bytes = n;
            for(unsigned k = 0; k < 64u && !(rd8(SSR) & TEND); ++k) {}
            return KUI_SCI_STREAM_OK;
        }
        if(value != 0xff) break;
    }
    ++s.stats.token_errors;
    return healthy() ? KUI_SCI_STREAM_TOKEN : KUI_SCI_STREAM_RESET;
}
/* R1, or 0xff if none came within the response window. */
static uint8_t command(const uint8_t packet[6], bool stop) {
    for(unsigned i = 0; i < 6; ++i) (void)byte(packet[i]);
    if(stop) (void)byte(0xff); /* CMD12's stuff byte */
    uint8_t value = 0xff;
    for(unsigned i = 0; i < 20u && (value & 0x80u); ++i) value = byte(0xff);
    return value;
}
/* CMD12 into the stream, its R1b busy, deselection and an idle byte. An
 * idle card answers "illegal command" (0x04): it is stopped all the same. */
static enum kui_sci_stream_result stop_card(void) {
    static const uint8_t stop[6] = {0x4c, 0, 0, 0, 0, 0x61};
    select(true);
    enum kui_sci_stream_result result = command(stop, true) & ~0x04u ?
        KUI_SCI_STREAM_COMMAND : KUI_SCI_STREAM_OK;
    for(uint32_t n = 0; byte(0xff) != 0xffu; ++n)
        if(n >= 200000u || !healthy()) {result = KUI_SCI_STREAM_COMMAND; break;}
    select(false);
    (void)byte(0xff);
    s.state = result == KUI_SCI_STREAM_OK ? CLOSED : LOST;
    return healthy() ? result : KUI_SCI_STREAM_RESET;
}
static enum kui_sci_stream_result start(uint32_t lba, uint32_t limit) {
    uint32_t address = s.card->high_capacity ? lba : lba << 9;
    uint8_t packet[6] = {0x52, (uint8_t)(address >> 24), (uint8_t)(address >> 16),
        (uint8_t)(address >> 8), (uint8_t)address, 0};
    packet[5] = crc7(packet);
    select(true);
    for(uint32_t n = 0; byte(0xff) != 0xffu;)
        if(++n >= 50000u || !healthy()) return KUI_SCI_STREAM_COMMAND;
    s.state = LOST; /* from here on, CMD18 may be running */
    ++s.stats.starts;
    uint8_t r1 = command(packet, false);
    if(!healthy()) return KUI_SCI_STREAM_RESET;
    return r1 ? KUI_SCI_STREAM_COMMAND : token(limit);
}
static enum kui_sci_stream_result module_reset(void) {
    uint8_t before = rd8(STBCR);
    if((before & 1u) || rd8(SCR) || (rd8(SSR) & (RDRF | FLAGS)) ||
       (rd8(SPTR) & 0x8au) != 0x82u || !(rd16(PDTR) & 0x80u)) return KUI_SCI_STREAM_RESET;
    wr8(STBCR, before | 1u);
    bool stopped = false, resumed = false;
    for(unsigned n = 0; n < 8u && !stopped; ++n) stopped = rd8(STBCR) & 1u;
    settle(64);
    wr8(STBCR, rd8(STBCR) & ~1u);
    for(unsigned n = 0; n < 8u && !resumed; ++n) resumed = !(rd8(STBCR) & 1u);
    if(!resumed || !stopped) return KUI_SCI_STREAM_RESET;
    settle(64);
    if(rd8(SCR) || rd8(SMR) || rd8(BRR) != 0xffu || rd8(SCMR) ||
       (rd8(SSR) & 0xfcu) != 0x84u || (rd8(SPTR) & 0x8au) != 0x82u) {
        wr8(SCR, 0);
        return KUI_SCI_STREAM_RESET;
    }
    return KUI_SCI_STREAM_OK;
}
/* After reception: deselect, reset the module after an overrun, and restore
 * the synchronous fast settings the bus uses for command bytes. */
static enum kui_sci_stream_result handoff(bool overrun) {
    select(false);
    if(!healthy() || !(rd16(PDTR) & 0x80u)) return KUI_SCI_STREAM_RESET;
    if(overrun) {
        enum kui_sci_stream_result result = module_reset();
        if(result != KUI_SCI_STREAM_OK) return result;
    }
    wr8(SCR, 0); wr8(SCMR, 0); wr8(SMR, 0x80u); wr8(BRR, 0);
    settle(64); /* more than one bit time at 12.5 MHz */
    wr8(SPTR, s.sptr); wr8(SCR, 0x30u);
    uint8_t status = rd8(SSR);
    if(rd8(SCR) != 0x30u || rd8(SMR) != 0x80u || rd8(BRR) != 0 ||
       (status & (RDRF | FLAGS)) || (status & 0x84u) != 0x84u) return KUI_SCI_STREAM_RESET;
    return KUI_SCI_STREAM_OK;
}
/* Receive the area's bytes from offset on by DMA: RIE+RE clock the card
 * without a transmitter, the channel takes each byte, and the receiver keeps
 * the one after the last in RDR and stops on the next one's overrun. */
static enum kui_sci_stream_result start_dma(uint32_t area, uint32_t offset) {
    /* TxD latched high as GPIO before the transmitter is turned off. */
    wr8(SPTR, 0x83u);
    if((rd8(SPTR) & 0x8au) != 0x82u) return KUI_SCI_STREAM_RESET;
    wr8(SCR, 0);
    wr32(CHCR, 0);
    wr32(SAR, RDR & UINT32_C(0x1fffffff));
    wr32(DAR, physical(s.area[area]) + offset);
    wr32(TCR, DMA_BYTES - offset);
    wr32(CHCR, RX_DMA);
    wr8(SCR, 0x50u);
    s.state = DMA;
    return KUI_SCI_STREAM_OK;
}
/* The block whose token was just read, into the area not holding the last
 * block taken (an untaken block there is dropped): by DMA when channel 1 is
 * idle and polled is false, otherwise by programmed transfers (514 bytes, so
 * the card's gap byte is left for the next token search), which cannot
 * overrun. */
static enum kui_sci_stream_result receive(bool polled) {
    uint32_t area = s.fill ^ 1u;
    if(s.kept == area + 1u) area ^= 1u;
    uint8_t *p = s.area[area];
    s.fill = area;
    s.ready[area] = 0;
    s.lost[area] = 0;
    settle(64); /* the token byte's last edge */
    if(polled || !channel_idle()) {
        for(unsigned i = 0; i < DMA_BYTES; ++i) p[i] = byte(0xff);
        s.rdr[area] = byte(0xff);
        select(false);
        if(!healthy()) return KUI_SCI_STREAM_RESET;
        s.wire[area] = 0;
        s.ready[area] = 1;
        s.ready_lba[area] = s.position++;
        s.state = PAUSED;
        ++s.stats.polled;
        return KUI_SCI_STREAM_OK;
    }
    purge(p, AREA_BYTES);
    for(unsigned i = 0; i < 4; ++i) s.saved[i] = rd32(SAR + 4u * i);
    s.wire[area] = 1;
    return start_dma(area, 0);
}
enum kui_sci_stream_result kui_sci_stream_fetch(uint32_t lba, uint32_t token_limit, bool polled) {
    if(s.state == DMA) return KUI_SCI_STREAM_BUSY;
    enum kui_sci_stream_result result;
    if(s.state == PAUSED && s.position == lba) {
        select(true);
        result = token(token_limit);
        if(result != KUI_SCI_STREAM_OK) {s.state = LOST; return result;}
        ++s.stats.continued;
    } else {
        if(s.state != CLOSED && (result = stop_card()) != KUI_SCI_STREAM_OK) return result;
        result = start(lba, token_limit);
        if(result != KUI_SCI_STREAM_OK) return result;
    }
    s.position = lba;
    result = receive(polled);
    if(result != KUI_SCI_STREAM_OK) s.state = LOST;
    return result;
}
/* End a reception: let the receiver take the second CRC byte and stop on the
 * next byte's overrun, stop the SCI and channel, restore the channel, keep
 * the RDR byte, then hand the SCI back to command bytes. A channel someone
 * else reprogrammed is left alone; the SCI is handed back in every case. */
static enum kui_sci_stream_result finish(void) {
    uint32_t area = s.fill, start = physical(s.area[area]), control = 0, count = 1;
    s.state = LOST;
    if(rd32(CHCR) & 2u)
        for(unsigned n = 0; n < 512u && !(rd8(SSR) & ORER); ++n) {}
    wr8(SCR, 0);
    fence(s.area[area]);
    uint32_t address = rd32(DAR);
    bool ours = rd32(SAR) == (RDR & UINT32_C(0x1fffffff)) && address >= start &&
        address <= start + DMA_BYTES;
    if(ours) {
        wr32(CHCR, rd32(CHCR) & ~UINT32_C(5));
        (void)rd32(CHCR);
        fence(s.area[area]);
        control = rd32(CHCR); count = rd32(TCR);
        ours = rd32(DAR) == start + DMA_BYTES - count;
        if(ours) {
            wr32(CHCR, 0);
            for(unsigned i = 0; i < 4; ++i) wr32(SAR + 4u * i, s.saved[i]);
        }
    }
    uint8_t status = rd8(SSR), tail = 0;
    if(status & RDRF) tail = rd8(RDR);
    wr8(SSR, status & ~(RDRF | FLAGS));
    enum kui_sci_stream_result result = handoff(status & ORER);
    if(result != KUI_SCI_STREAM_OK) return result;
    if(!ours) {
        ++s.stats.foreign;
        return KUI_SCI_STREAM_BUSY;
    }
    if(count || !(control & 2u) || !(status & RDRF)) {
        /* The channel was held off the bus (the game's own DMA) and the
         * receiver overran mid-block: the byte that overran is lost and the
         * card stopped after it. The channel has `at` bytes; usually it
         * also took the byte RDR held when it got the bus back, so byte
         * `at` is the lost one. If it had not yet (RDRF), RDR still holds
         * byte `at` and `at + 1` is lost. Once per block, if the lost byte
         * was data, carry on from the byte after it (the card simply
         * waited, deselected) and rebuild the lost one from the CRC when
         * the block is taken. */
        uint32_t at = DMA_BYTES - count, held = status & RDRF ? 1u : 0u, lost = at + held;
        if((status & ORER) && lost < 512u && !s.lost[area] && s.unrepaired < 2u) {
            s.lost[area] = (uint16_t)(lost + 1u);
            s.hold[area] = (uint8_t)held;
            s.held[area] = tail;
            ++s.stats.repaired;
            select(true);
            result = start_dma(area, lost + 1u);
            return result == KUI_SCI_STREAM_OK ? KUI_SCI_STREAM_PENDING : result;
        }
        ++s.stats.overruns;
        return KUI_SCI_STREAM_OVERRUN;
    }
    s.ready[area] = 1;
    s.ready_lba[area] = s.position++;
    s.rdr[area] = tail;
    s.state = PAUSED;
    ++s.stats.blocks;
    return KUI_SCI_STREAM_OK;
}
enum kui_sci_stream_result kui_sci_stream_poll(void) {
    if(s.state != DMA) return KUI_SCI_STREAM_OK;
    uint32_t control = rd32(CHCR);
    /* Still receiving, unless the channel stopped or the receiver overran. */
    if(!(control & 2u) && (control & 1u) && !(rd8(SSR) & ORER) &&
       (rd32(DMAOR) & 7u) == 1u) return KUI_SCI_STREAM_PENDING;
    return finish();
}
enum kui_sci_stream_result kui_sci_stream_wait(void) {
    for(uint32_t n = 0; n < 200000u; ++n) {
        enum kui_sci_stream_result result = kui_sci_stream_poll();
        if(result != KUI_SCI_STREAM_PENDING) return result;
    }
    return finish(); /* a stalled receiver ends as an overrun */
}
/* The byte whose loss leaves this CRC16 syndrome when it stands as zero
 * with `after` bytes behind it, or -1 if no single byte explains it. The
 * CRC (zero start) is linear: undo the `after` zero-byte shifts, bit by bit
 * (x^16+x^12+x^5+1 has its constant term, so each step is invertible). What
 * remains is one byte's own step, t ^ t << 5 ^ t << 12 with t = x ^ x >> 4:
 * its low byte gives t, t gives x, and the whole step must match. */
static int rebuild(uint32_t syndrome, uint32_t after) {
    for(uint32_t n = after * 8u; n; --n)
        syndrome = syndrome & 1u ? ((syndrome ^ 0x1021u) >> 1) | 0x8000u : syndrome >> 1;
    uint32_t t = (syndrome ^ syndrome << 5) & 0xffu, x = t ^ t >> 4;
    return crc16(0, (uint8_t)x) == syndrome ? (int)x : -1;
}
const uint8_t *kui_sci_stream_take(uint32_t lba, enum kui_sci_stream_result *result) {
    for(unsigned i = 0; i < 2; ++i) {
        if(!s.ready[i] || s.ready_lba[i] != lba) continue;
        s.ready[i] = 0;
        uint8_t *area = s.area[i];
        uint32_t lost = s.lost[i];
        if(lost) {
            if(s.hold[i]) area[lost - 2u] = s.held[i];
            area[lost - 1u] = 0;
        }
        uint8_t high = area[512], low = s.rdr[i];
        bool wire = s.wire[i];
        uint16_t crc = 0;
        for(unsigned w = 0; w < 128u; ++w) {
            uint32_t word = ((alias_word *)area)[w];
            if(wire) ((alias_word *)area)[w] = word = reverse(word);
            for(unsigned b = 0; b < 4u; ++b) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
                crc = crc16(crc, (uint8_t)word); word >>= 8;
#else
                crc = crc16(crc, (uint8_t)(word >> 24)); word <<= 8;
#endif
            }
        }
        if(wire) {high = (uint8_t)reverse(high); low = (uint8_t)reverse(low);}
        uint16_t sent = (uint16_t)(high << 8 | low);
        if(crc != sent && lost) {
            /* The gap byte (0xff) or the next token (0xfe) where the second
             * CRC byte belongs: the resumed reception ran a byte ahead. */
            int x = -1;
            if((low | 1u) == 0xffu) ++s.stats.ahead;
            else if((x = rebuild((uint32_t)(crc ^ sent), 512u - lost)) < 0) ++s.unrepaired;
            if(x >= 0) {area[lost - 1u] = (uint8_t)x; crc = sent;}
        }
        if(crc != sent) {
            ++s.stats.crc_errors;
            *result = KUI_SCI_STREAM_CRC;
            return NULL;
        }
        s.kept = (uint8_t)(i + 1u);
        s.kept_lba = lba;
        *result = KUI_SCI_STREAM_OK;
        return area;
    }
    if(s.kept && s.kept_lba == lba) {
        ++s.stats.kept;
        *result = KUI_SCI_STREAM_OK;
        return s.area[s.kept - 1u];
    }
    *result = KUI_SCI_STREAM_PENDING;
    return NULL;
}
enum kui_sci_stream_result kui_sci_stream_stop(void) {
    enum kui_sci_stream_result result = KUI_SCI_STREAM_OK;
    if(s.state == DMA) result = kui_sci_stream_wait();
    if(result == KUI_SCI_STREAM_BUSY || result == KUI_SCI_STREAM_RESET) return result;
    s.ready[0] = s.ready[1] = 0;
    s.kept = 0;
    return s.state == CLOSED ? KUI_SCI_STREAM_OK : stop_card();
}
