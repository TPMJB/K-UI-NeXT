/* SPDX-License-Identifier: BSD-3-Clause
 * SCI synchronous mode, bit order and PA7 mapping adapted from KallistiOS
 * kernel/arch/dreamcast/hardware/sci.c, Copyright (C) 2025 Ruslan Rostovtsev.
 * Pinned upstream: fcfa7d869471591ca1c777543261a7bfea7cb726.
 * K-UI adaptation Copyright (C) 2026 K-UI contributors.
 * See LICENSES/LICENSE.KOS for the retained conditions and disclaimer.
 */
#include "sci_sd_bus.h"

#if defined(KUI_ON_CONSOLE) || defined(KUI_SCI_SD_TEST)
#define SMR UINT32_C(0xffe00000)
#define BRR UINT32_C(0xffe00004)
#define SCR UINT32_C(0xffe00008)
#define TDR UINT32_C(0xffe0000c)
#define SSR UINT32_C(0xffe00010)
#define RDR UINT32_C(0xffe00014)
#define PTR UINT32_C(0xffe00018)
#define STB UINT32_C(0xffc00004)
#define PCTR UINT32_C(0xff80002c)
#define PDTR UINT32_C(0xff800030)
#define CS UINT16_C(0x0080)
#define CS_MODE UINT32_C(0xc000)
#define CS_OUTPUT UINT32_C(0x4000)
#define TDRE 0x80u
#define RDRF 0x40u
#define ERRORS 0x38u
#define TEND 0x04u
#define POLLS 10000u
#define DMA_SAR UINT32_C(0xffa00010)
#define DMA_DAR UINT32_C(0xffa00014)
#define DMA_COUNT UINT32_C(0xffa00018)
#define DMA_CONTROL UINT32_C(0xffa0001c)
#define DMA_OPERATION UINT32_C(0xffa00040)
#define DMA_RX UINT32_C(0x4911)
#define DMA_TX UINT32_C(0x1811)
#define DMA_END 2u

#ifdef KUI_SCI_SD_TEST
extern uint32_t kui_sci_sd_test_read(uint32_t address, unsigned width);
extern void kui_sci_sd_test_write(uint32_t address, uint32_t value, unsigned width);
extern void kui_sci_sd_test_delay(uint32_t count);
extern uint32_t kui_sci_sd_test_irq_disable(void);
extern void kui_sci_sd_test_irq_restore(uint32_t value);
extern uint32_t kui_sci_sd_test_dma_address(const void *buffer, size_t count);
extern void kui_sci_sd_test_cache_purge(void *buffer, size_t count);
#define rd8(a) ((uint8_t)kui_sci_sd_test_read(a, 1))
#define rd16(a) ((uint16_t)kui_sci_sd_test_read(a, 2))
#define rd32(a) kui_sci_sd_test_read(a, 4)
#define wr8(a,v) kui_sci_sd_test_write(a, (uint8_t)(v), 1)
#define wr16(a,v) kui_sci_sd_test_write(a, (uint16_t)(v), 2)
#define wr32(a,v) kui_sci_sd_test_write(a, v, 4)
#else
#define rd8(a) (*(volatile uint8_t *)(uintptr_t)(a))
#define rd16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define rd32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define wr8(a,v) (rd8(a) = (uint8_t)(v))
#define wr16(a,v) (rd16(a) = (uint16_t)(v))
#define wr32(a,v) (rd32(a) = (uint32_t)(v))
#endif

static struct {
    uint32_t work, mode;
    uint16_t data;
    uint8_t smr, brr, scr, ptr, standby;
    bool acquired, fault, slow;
} port;
static bool wait_flag(uint8_t flag);
#ifndef KUI_RETAIL_TRANSPORT
static struct kui_sci_sd_stats stats;
static uint64_t (*profile_clock)(void *);
static void *profile_context;
#define COUNT(field) (++stats.field)
void kui_sci_sd_stats_get(struct kui_sci_sd_stats *out) { if(out) *out = stats; }
void kui_sci_sd_profile_timer(uint64_t (*now_us)(void *), void *ctx) {
    profile_clock = now_us; profile_context = ctx;
}
static uint64_t profile_time(void) {
    return profile_clock ? profile_clock(profile_context) : 0;
}
#else
#define COUNT(field) ((void)0)
#endif

static void delay(uint32_t count) {
#ifdef KUI_SCI_SD_TEST
    kui_sci_sd_test_delay(count);
#else
    __asm__ __volatile__("1: dt %0\n\tbf 1b\n\t" : "+r"(count) : : "t", "memory");
#endif
}

enum kui_loader_sd_result kui_sci_sd_acquire(void) {
    if(port.acquired) return KUI_LOADER_SD_NOT_READY;
    uint8_t scr = rd8(SCR), status = rd8(SSR);
    /* TIE/RIE/TE/RE/TEIE or unread/error state belong to another user. */
    if((scr & 0xf4u) || (status & (RDRF | ERRORS)))
        return KUI_LOADER_SD_UNSUPPORTED;
    port.scr = scr; port.smr = rd8(SMR); port.brr = rd8(BRR);
    port.ptr = rd8(PTR); port.standby = rd8(STB) & 1u;
    port.mode = rd32(PCTR) & CS_MODE; port.data = rd16(PDTR) & CS;
    if(port.standby) {
        wr8(STB, rd8(STB) & ~1u);
        /* Module standby leaves CPG running (SH7750 manual 9.6.2).
         * Confirm the write; the required BRR settling wait follows below. */
        (void)rd8(STB);
    }
    wr8(SCR, 0);
    wr16(PDTR, rd16(PDTR) | CS); /* Deselected before changing pin direction. */
    wr32(PCTR, (rd32(PCTR) & ~CS_MODE) | CS_OUTPUT);
    wr8(PTR, 0); wr8(SMR, 0x80u);
    wr8(BRR, 31); /* 50 MHz / (4 * 32) = 390625 Hz during card setup. */
    delay(1024u); /* >= one slow bit after BRR changes. */
    port.slow = true; port.fault = false; port.acquired = true;
    return KUI_LOADER_SD_OK;
}

void kui_sci_sd_release(void) {
    if(!port.acquired) return;
    /* Full duplex transfers consume their received byte; stop on a latched
     * timeout before returning controls. Never attempt an unbounded drain. */
    if(!port.fault) (void)wait_flag(TEND);
    wr8(SCR, 0);
    wr16(PDTR, rd16(PDTR) | CS);
    uint8_t status = rd8(SSR);
    if(status & RDRF) (void)rd8(RDR);
    if(status & (RDRF | ERRORS)) wr8(SSR, status & ~(RDRF | ERRORS));
    wr8(SMR, port.smr); wr8(BRR, port.brr); wr8(PTR, port.ptr);
    wr32(PCTR, (rd32(PCTR) & ~CS_MODE) | port.mode);
    wr16(PDTR, (rd16(PDTR) & (uint16_t)~CS) | port.data);
    wr8(SCR, port.scr);
    wr8(STB, (rd8(STB) & ~1u) | port.standby);
    port.acquired = false;
}

static void begin(void *ctx) { (void)ctx; }
static void end(void *ctx) { (void)ctx; kui_sci_sd_release(); }
static void select_card(void *ctx, bool selected) {
    (void)ctx;
    if(port.acquired) {
        if(!selected && !port.fault) (void)wait_flag(TEND);
        wr16(PDTR, selected ? rd16(PDTR) & (uint16_t)~CS : rd16(PDTR) | CS);
    }
}
static uint32_t reverse(uint32_t x) {
    /* Reverse each byte independently, retaining its position in the word. */
    uint32_t t = (x ^ (x >> 1)) & UINT32_C(0x55555555);
    x ^= t ^ (t << 1);
    t = (x ^ (x >> 2)) & UINT32_C(0x33333333);
    x ^= t ^ (t << 2);
    t = (x ^ (x >> 4)) & UINT32_C(0x0f0f0f0f);
    return x ^ t ^ (t << 4);
}
/* DMA RX and the staging array have proven 32-byte alignment. This type may
 * alias the caller's byte array without relaxing aliasing for the whole build. */
typedef uint32_t alias_word __attribute__((__may_alias__));
static uint16_t data_crc(uint16_t crc, uint8_t data) {
    /* Fold eight x^16+x^12+x^5+1 steps without a second lookup table. */
    uint32_t x = (crc >> 8) ^ data;
    x ^= x >> 4;
    return (uint16_t)((crc << 8) ^ (x << 12) ^ (x << 5) ^ x);
}
static bool wait_flag(uint8_t flag) {
    for(unsigned n = 0; n < POLLS; ++n) {
        uint8_t status = rd8(SSR);
        if(status & ERRORS) break;
        if((status & flag) == flag) return true;
    }
    port.fault = true;
    wr8(SCR, 0);
    return false;
}
static bool prepare(bool slow) {
    if(!port.acquired || port.fault) return false;
    if(port.slow != slow) {
        if(!wait_flag(TEND)) return false;
        wr8(SCR, 0); wr8(BRR, slow ? 31 : 0); /* Fast: 12.5 MHz. */
        delay(1024u);
        port.slow = slow;
    }
    wr8(SCR, 0x30u); /* TE + RE; clock generated only for transmitted bytes. */
    return true;
}
static uint8_t transfer(void *ctx, uint8_t data, bool slow) {
    (void)ctx;
    /* Nominal wire ticks, not a wall-clock claim. The protocol also bounds
     * byte counts; the runtime supplies a hardware timer for its deadlines. */
    port.work += slow ? 256u : 8u;
    if(!prepare(slow)) return 0xff;
    if(!wait_flag(TDRE)) return 0xff;
    wr8(TDR, reverse(data));
    wr8(SSR, 0x7cu); /* Clear observed TDRE, preserve the other flags. */
    if(!wait_flag(RDRF)) return 0xff;
    uint8_t received = rd8(RDR);
    wr8(SSR, 0xbcu); /* Clear observed RDRF, preserve TDRE and errors. */
    return reverse(received);
}

static uint32_t mask_interrupts(void) {
#ifdef KUI_SCI_SD_TEST
    return kui_sci_sd_test_irq_disable();
#else
    uint32_t before, masked;
    __asm__ __volatile__("stc sr,%0" : "=r"(before));
    masked = before | 0xf0u;
    __asm__ __volatile__("ldc %0,sr" : : "r"(masked) : "t", "memory");
    return before;
#endif
}
static void restore_interrupts(uint32_t before) {
#ifdef KUI_SCI_SD_TEST
    kui_sci_sd_test_irq_restore(before);
#else
    __asm__ __volatile__("ldc %0,sr" : : "r"(before) : "t", "memory");
#endif
}
static uint32_t dma_address(void *buffer) {
    uintptr_t p = (uintptr_t)buffer;
    if(p & 31u) return 0;
#ifdef KUI_SCI_SD_TEST
    return kui_sci_sd_test_dma_address(buffer, 512);
#else
    /* Only the Dreamcast's 16 MiB RAM, through P1 or P2; never MMU mappings
     * or an arbitrary peripheral supplied by a caller. */
    if((p >> 24) != 0x8cu && (p >> 24) != 0xacu) return 0;
    p &= 0x1fffffffu;
    return p <= 0x0d000000u - 512u ? (uint32_t)p : 0;
#endif
}
static void purge_buffer(void *buffer) {
#ifdef KUI_SCI_SD_TEST
    kui_sci_sd_test_cache_purge(buffer, 512);
#else
    uintptr_t first = ((uintptr_t)buffer & 0x1fffffffu) | 0x80000000u;
    for(uintptr_t p = first; p < first + 512; p += 32)
        __asm__ __volatile__("ocbp @%0" : : "r"(p) : "memory");
#endif
}
static bool feed_read(void) {
    /* TDR retains its value after the shift register takes it. As in the
     * programmed read path, seed once and clear TDRE for each exact byte. */
    for(unsigned left = 512; left; --left) {
        unsigned n = POLLS;
        for(;;) {
            uint8_t status = rd8(SSR);
            if(status & ERRORS) return false;
            if(status & TDRE) break;
            if(!--n) return false;
        }
        if(left == 512) wr8(TDR, 0xff);
        wr8(SSR, 0x7cu);
    }
    return true;
}
#ifndef KUI_RETAIL_TRANSPORT
/* The game reader is read-only and carries neither this buffer nor TX DMA. */
static uint8_t dma_write_buffer[512] __attribute__((aligned(32)));
#endif

/* Original bounded DMAC implementation using SH7750 manual sections 14/15.
 * Return -1 before touching the channel when it cannot be borrowed. A
 * started transfer must never fall back at an uncertain SD-stream position. */
static int dma_block(const uint8_t *tx, uint8_t *rx, uint16_t *crc_out) {
    void *buffer = rx;
    bool writing = tx != NULL;
#ifndef KUI_RETAIL_TRANSPORT
    if(writing) buffer = dma_write_buffer;
#else
    if(writing) return -1;
#endif
    uint32_t address = dma_address(buffer);
    if(!address) return -1;
    uint32_t before = mask_interrupts();
    /* TE is hardware-set and cannot be recreated by restoring a snapshot.
     * Leave completed/unacknowledged transfers and pending IRQs untouched. */
    uint32_t saved[4];
    saved[3] = rd32(DMA_CONTROL);
    if((saved[3] & 7u) || (rd32(DMA_OPERATION) & 7u) != 1u) {
        restore_interrupts(before); return -1;
    }
#ifndef KUI_RETAIL_TRANSPORT
    uint64_t setup_started = profile_time();
#endif
    for(unsigned i = 0; i < 3; ++i) saved[i] = rd32(DMA_SAR + 4u*i);
#ifndef KUI_RETAIL_TRANSPORT
    if(writing) {
        if((uintptr_t)tx & 3u) {
            for(unsigned i = 0; i < 512; ++i) dma_write_buffer[i] = reverse(tx[i]);
        } else {
            for(unsigned i = 0; i < 128; ++i)
                ((alias_word *)dma_write_buffer)[i] = reverse(((const alias_word *)tx)[i]);
        }
    }
#endif
    purge_buffer(buffer);
    wr32(DMA_CONTROL, 0);
    wr32(DMA_SAR, writing ? address : RDR & 0x1fffffffu);
    wr32(DMA_DAR, writing ? TDR & 0x1fffffffu : address);
    wr32(DMA_COUNT, 512);
    if(writing) wr8(SCR, 0); /* Change direction only between complete bytes. */
    wr32(DMA_CONTROL, writing ? DMA_TX : DMA_RX);
    /* The SCI request enable is required on Dreamcast hardware. CPU IRQs
     * remain masked until it is removed; DMAC completion IRQ is disabled. */
#ifndef KUI_RETAIL_TRANSPORT
    uint64_t transfer_started = profile_time();
#endif
    wr8(SCR, writing ? 0xa0u : 0x70u);
    port.work += 512u * 8u;
    uint16_t crc = 0;
    bool ok = true;
    if(!writing) ok = feed_read();
#ifndef KUI_RETAIL_TRANSPORT
    else {
        /* The DMA reads its reversed bounce buffer while the CPU checks
         * the immutable logical source; these accesses cannot race. */
        /* Keep source reads after transfer start so CPU and wire overlap. */
        __asm__ __volatile__("" : : : "memory");
        for(unsigned i = 0; i < 512; ++i) crc = data_crc(crc, tx[i]);
    }
#endif
    unsigned n = 0;
    while(ok && !(rd32(DMA_CONTROL) & DMA_END)) {
        if(++n == POLLS || (rd8(SSR) & ERRORS) ||
           (rd32(DMA_OPERATION) & 7u) != 1u) ok = false;
    }
    if(ok) ok = (rd32(DMA_OPERATION) & 7u) == 1u &&
        rd32(DMA_COUNT) == 0 && wait_flag(TEND);
    /* TEND can assert during the last bit: allow its final edge before
     * changing mode or returning to token/CRC operations. */
    if(ok) delay(32u);
    wr8(SCR, 0); /* Remove RIE/TIE before releasing the channel or CPU mask. */
    wr32(DMA_CONTROL, 0);
    /* A just-finishing request may have set TE while DE was being cleared.
     * Read that final status before clearing its hardware-set flag. */
    (void)rd32(DMA_CONTROL);
    wr32(DMA_CONTROL, 0);
#ifndef KUI_RETAIL_TRANSPORT
    uint64_t transfer_stopped = profile_time(), check_us = 0;
#endif
    if(ok && !writing) {
        /* The pre-DMA purge invalidated every complete buffer line. Only
         * now, after stopping DMA, may the CPU refill through the caller's
         * original P1/P2 pointer. Retain cached logical bytes for copying. */
        __asm__ __volatile__("" : : : "memory");
        for(unsigned i = 0; i < 128; ++i) {
            uint32_t word = reverse(((alias_word *)rx)[i]);
            ((alias_word *)rx)[i] = word;
            /* Consume logical bytes in memory order without reloading the
             * just-written cache line or testing the word boundary per byte. */
            for(unsigned byte = 0; byte < 4; ++byte) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
                crc = data_crc(crc, (uint8_t)word);
                word >>= 8;
#else
                crc = data_crc(crc, (uint8_t)(word >> 24));
                word <<= 8;
#endif
            }
        }
#ifndef KUI_RETAIL_TRANSPORT
        check_us = profile_time() - transfer_stopped;
#endif
    }
    for(unsigned i = 0; i < 4; ++i) wr32(DMA_SAR + 4u*i, saved[i]);
    if(ok) wr8(SCR, 0x30u);
    else port.fault = true;
    restore_interrupts(before);
    if(!ok) COUNT(failures);
    else if(writing) COUNT(tx_blocks);
    else COUNT(rx_blocks);
#ifndef KUI_RETAIL_TRANSPORT
    if(ok && profile_clock) {
        if(writing) {
            ++stats.profiled_tx_blocks;
            stats.tx_setup_us += transfer_started - setup_started;
            stats.tx_transfer_us += transfer_stopped - transfer_started;
        } else {
            ++stats.profiled_rx_blocks;
            stats.rx_setup_us += transfer_started - setup_started;
            stats.rx_transfer_us += transfer_stopped - transfer_started;
            stats.rx_check_us += check_us;
        }
    }
#endif
    if(ok && crc_out) *crc_out = crc;
    return ok;
}

static bool transfer_block(void *ctx, const uint8_t *tx, uint8_t *rx,
                           size_t count, bool slow, uint16_t *crc_out) {
    (void)ctx;
#ifdef KUI_RETAIL_TRANSPORT
    /* Resident block users are read-only. Keep runtime write support out of
     * the fixed low-memory image, including the programmed fallback. */
    if(tx) return false;
#endif
    if(!count || count > 512 || (!tx && !rx) || !port.acquired || port.fault)
        return false;
    if(!prepare(slow)) return false;
    if(!slow && count == 512 && (!tx || !rx)) {
        int result = dma_block(tx, rx, crc_out);
        if(result >= 0) return result != 0;
    }
    COUNT(polled_blocks);
    /* One byte in flight, so interrupt latency cannot overrun a second
     * receive. Commands and short/unaligned payloads need no DMAC ownership.
     * Start the next wire byte before reversing/storing the previous one. */
    if(!wait_flag(TDRE)) return false;
    wr8(TDR, tx ? reverse(tx[0]) : 0xff);
    wr8(SSR, 0x7cu);
    uint16_t crc = 0;
    for(size_t i = 0; i < count; ++i) {
        port.work += slow ? 256u : 8u;
        if(!wait_flag(RDRF | TDRE)) return false;
        uint8_t received = rd8(RDR);
        wr8(SSR, 0xbcu);
        if(i + 1 < count) {
            if(tx) wr8(TDR, reverse(tx[i+1]));
            wr8(SSR, 0x7cu);
        }
        uint8_t logical = rx ? reverse(received) : tx[i];
        if(rx) rx[i] = logical;
        crc = data_crc(crc, logical);
    }
    if(crc_out) *crc_out = crc;
    return true;
}
static uint32_t ticks(void *ctx) { (void)ctx; return port.work; }
static const struct kui_loader_sd_bus bus = {
    NULL, begin, end, select_card, transfer, ticks, transfer_block
};
const struct kui_loader_sd_bus *kui_sci_sd_bus(void) { return &bus; }
bool kui_sci_sd_healthy(void) { return port.acquired && !port.fault; }
#else
enum kui_loader_sd_result kui_sci_sd_acquire(void) { return KUI_LOADER_SD_UNSUPPORTED; }
void kui_sci_sd_release(void) {}
const struct kui_loader_sd_bus *kui_sci_sd_bus(void) { return NULL; }
bool kui_sci_sd_healthy(void) { return false; }
#ifndef KUI_RETAIL_TRANSPORT
void kui_sci_sd_stats_get(struct kui_sci_sd_stats *out) {
    if(out) *out = (struct kui_sci_sd_stats){0};
}
void kui_sci_sd_profile_timer(uint64_t (*now_us)(void *), void *ctx) {
    (void)now_us; (void)ctx;
}
#endif
#endif
