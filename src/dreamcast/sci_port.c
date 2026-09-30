/* SPDX-License-Identifier: GPL-3.0-only */
/* See sci_port.h. Port A's registers are the SH-4's: PCTRA holds two bits
 * per pin (the low one set: output; the high one set: no pull-up) and PDTRA
 * the pins' levels, at the addresses KOS's SCI driver uses for GPIO7. */
#include "sci_port.h"
#include <arch/dmac.h>
#include <arch/irq.h>
#include <arch/timer.h>
#include <dc/sci.h>
#include <kos/irq.h>
#include <kos/timer.h>
#include <stdint.h>

#define PCTRA (*(volatile uint32_t *)0xff80002cu)
#define PDTRA (*(volatile uint16_t *)0xff800030u)
#define PIN_SELECT 6u
#define PIN_READY 5u

static const uint32_t rates[KUI_SCI_RATES] = {SCI_SPI_BAUD_12M500K, SCI_SPI_BAUD_6M250K, SCI_SPI_BAUD_3M125K,
                                              SCI_SPI_BAUD_1M562K};
static bool running;
static unsigned selected = 7u, rate_open;

/* DMA (kui_sci_dma_transfer). KOS's programmed transfers wait for each byte
 * before clocking the next; with DMA channel 1 emptying the receive register,
 * the CPU clocks bytes out back to back. This is K-UI's own transfer, not
 * KOS's sci_spi_dma_read_data: that one never sets the SCI's RIE bit, so the
 * SCI never asks the DMA controller for a byte, and it then waits in
 * dma_wait_complete for an interrupt it did not enable (the console locked up
 * with it, commit 389f9ca). Here RIE is set only for the length of a piece,
 * the SCI's own interrupt is masked, channel 1 is programmed directly, and
 * every wait has a deadline. It works on the owner's console with the W5500
 * (uploads from about 370 to about 500 KiB/s). */
#define DMA_BYTES 4096u  /* one piece of a longer transfer */
#define SCSCR1 (*(volatile uint8_t *)0xffe00008u)
#define SCTDR1 (*(volatile uint8_t *)0xffe0000cu)
#define SCSSR1 (*(volatile uint8_t *)0xffe00010u)
#define SCRDR1 (*(volatile uint8_t *)0xffe00014u)
#define SCTDR1_ADDR 0xffe0000cu
#define SCRDR1_ADDR 0xffe00014u
#define SCR_TIE 0x80u
#define SCR_RIE 0x40u
#define SCR_TE 0x20u
#define SCR_RE 0x10u
#define SSR_TDRE 0x80u
#define SSR_RDRF 0x40u
#define SSR_ORER 0x20u
#define SSR_FER 0x10u
#define SSR_PER 0x08u
#define SSR_TEND 0x04u
/* Written to SCSSR1 just after reading TDRE as 1: the 0 clears TDRE and
 * starts the byte in SCTDR1; writing 1 leaves the other flags as they are
 * (RDRF included, which the DMA clears as it reads), and MPBT stays 0. */
#define SSR_CLEAR_TDRE 0x7cu
/* Written to SCSSR1 to clear RDRF and the receive errors, leaving TDRE
 * (a 1 written changes nothing) and MPBT alone. */
#define SSR_CLEAR_RECEIVE 0x84u
#define SAR1 (*(volatile uint32_t *)0xffa00010u)
#define DAR1 (*(volatile uint32_t *)0xffa00014u)
#define TCR1 (*(volatile uint32_t *)0xffa00018u)
#define CHCR1 (*(volatile uint32_t *)0xffa0001cu)
#define DMAOR (*(volatile uint32_t *)0xffa00040u)
#define CHCR_DE 0x1u
#define CHCR_TE 0x2u
#define CHCR_IE 0x4u
/* Destination increments, source fixed, the SCI's receive request, cycle
 * steal, one byte at a time, no interrupt. */
#define CHCR_SCI_RX ((1u << 14) | ((uint32_t)DMA_REQUEST_SCI_RECEIVE << 8) | ((uint32_t)DMA_UNITSIZE_8BIT << 4) | CHCR_DE)
/* Source increments, destination fixed, the SCI's transmit request. */
#define CHCR_SCI_TX ((1u << 12) | ((uint32_t)DMA_REQUEST_SCI_TRANSMIT << 8) | ((uint32_t)DMA_UNITSIZE_8BIT << 4) | CHCR_DE)
/* Both fixed: the same byte again and again. */
#define CHCR_SCI_IDLE (((uint32_t)DMA_REQUEST_SCI_TRANSMIT << 8) | ((uint32_t)DMA_UNITSIZE_8BIT << 4) | CHCR_DE)
/* TMU1, which KOS leaves unused: its start bit, counter and control. */
#define TMU_TSTR (*(volatile uint8_t *)0xffd80004u)
#define TMU_TCOR1 (*(volatile uint32_t *)0xffd80014u)
#define TMU_TCNT1 (*(volatile uint32_t *)0xffd80018u)
#define TMU_TCR1 (*(volatile uint16_t *)0xffd8001cu)
#define TSTR_STR1 0x02u
/* Underflow interrupt on, count at Pphi/4, and the underflow flag cleared. */
#define TCR_UNIE 0x0020u
static uint8_t dma_buffer[DMA_BYTES] __attribute__((aligned(32)));
static uint8_t reversed[256];
static void async_on(bool on);

/* 0: input with pull-up; 1: output. */
static void pin_mode(unsigned pin, uint32_t mode) {
    PCTRA = (PCTRA & ~(UINT32_C(3) << (pin * 2u))) | (mode << (pin * 2u));
}
static void pin_high(unsigned pin, bool high) {
    uint16_t bit = (uint16_t)(1u << pin);
    PDTRA = high ? (uint16_t)(PDTRA | bit) : (uint16_t)(PDTRA & ~bit);
}
bool kui_sci_open(unsigned rate, unsigned select) {
    kui_sci_close();
    if(rate >= KUI_SCI_RATES || (select != 6u && select != 7u)) return false;
    for(unsigned b = 0; b < 256u; ++b) {
        unsigned r = 0;
        for(unsigned bit = 0; bit < 8u; ++bit) r |= ((b >> bit) & 1u) << (7u - bit);
        reversed[b] = (uint8_t)r;
    }
    /* RIE, which DMA needs, also raises the SCI's own interrupt; nothing
     * handles it. */
    irq_set_priority(IRQ_SRC_SCI1, IRQ_PRIO_MASKED);
    /* No KOS DMA buffer: KOS's transfers are programmed I/O, and DMA is
     * kui_sci_dma_transfer's own. KOS takes GPIO7 as its chip select and
     * leaves it high, so a device there stays deselected while another is
     * used. */
    if(sci_init(rates[rate], SCI_MODE_SPI, SCI_CLK_INT, 0) != SCI_OK) return false;
    running = true;
    selected = select;
    rate_open = rate;
    if(select == PIN_SELECT) {
        /* High before it drives, so the device never sees a stray select. */
        pin_high(PIN_SELECT, true);
        pin_mode(PIN_SELECT, 1u);
    }
    pin_mode(PIN_READY, 0u);
    async_on(true);
    return true;
}
void kui_sci_close(void) {
    if(!running) return;
    async_on(false);
    /* An input again: the connector's pull-up keeps the device deselected. */
    if(selected == PIN_SELECT) pin_mode(PIN_SELECT, 0u);
    sci_shutdown();
    running = false;
}
bool kui_sci_running(void) { return running; }
void kui_sci_select(bool active) {
    if(selected == PIN_SELECT) pin_high(PIN_SELECT, !active);
    else sci_spi_set_cs(active);
}
bool kui_sci_ready(void) { return (PDTRA >> PIN_READY) & 1u; }

/* DMA is on, with no address error or NMI stop, and channel 1 is idle. */
bool kui_sci_dma_ready(void) {
    return running && (DMAOR & 0x7u) == 0x1u && (!(CHCR1 & CHCR_DE) || (CHCR1 & CHCR_TE));
}
/* One piece: its first byte by a KOS programmed transfer, which also leaves
 * KOS's driver with transmit and receive on, as it expects; the rest by DMA
 * while the CPU clocks out the outgoing bytes (or 0xff). */
static bool dma_piece(const uint8_t *out, uint8_t *in, size_t bytes) {
    if((out ? sci_spi_rw_data(out, in, 1) : sci_spi_read_data(in, 1)) != SCI_OK) return false;
    size_t rest = bytes - 1u;
    if(!rest) return true;
    uint64_t deadline = timer_us_gettime64() + 2000u + rest;
    bool ok = true;
    CHCR1 = 0;
    SAR1 = hw_to_dma_addr(SCRDR1_ADDR);
    DAR1 = dma_map_dst(dma_buffer, rest);
    TCR1 = (uint32_t)rest;
    CHCR1 = CHCR_SCI_RX;
    SCSCR1 |= SCR_RIE;
    for(size_t i = 0; ok && i < rest; ++i) {
        for(unsigned spins = 0; !(SCSSR1 & SSR_TDRE);)
            if(!(++spins & 255u) && timer_us_gettime64() > deadline) { ok = false; break; }
        if(!ok) break;
        /* One read (the wait above) and two writes a byte, so the CPU keeps
         * up with the 12.5 MHz clock. */
        SCTDR1 = out ? reversed[out[1u + i]] : 0xffu;
        SCSSR1 = SSR_CLEAR_TDRE;
    }
    for(unsigned spins = 0; ok && !(SCSSR1 & SSR_TEND);)
        if(!(++spins & 255u) && timer_us_gettime64() > deadline) ok = false;
    for(unsigned spins = 0; ok && !(CHCR1 & CHCR_TE);)
        if(!(++spins & 255u) && timer_us_gettime64() > deadline) ok = false;
    SCSCR1 &= (uint8_t)~SCR_RIE;
    CHCR1 = 0;
    if(!ok || (SCSSR1 & (SSR_ORER | SSR_FER | SSR_PER))) return false;
    /* Drop any cached copy of the buffer, then read what the DMA wrote. */
    (void)dma_map_dst(dma_buffer, rest);
    for(size_t i = 0; i < rest; ++i) in[1u + i] = reversed[dma_buffer[i]];
    return true;
}
/* After a failed piece: the DMA stopped, the transmitter finished, and no
 * error or stale byte left for the programmed transfer that follows. */
static void dma_recover(void) {
    SCSCR1 &= (uint8_t)~SCR_RIE;
    CHCR1 = 0;
    uint64_t deadline = timer_us_gettime64() + 1000u;
    while(!(SCSSR1 & SSR_TEND) && timer_us_gettime64() < deadline) {}
    if(SCSSR1 & SSR_RDRF) {
        (void)SCRDR1;
        SCSSR1 &= (uint8_t)~SSR_RDRF;
    }
    if(SCSSR1 & (SSR_ORER | SSR_FER | SSR_PER)) SCSSR1 &= (uint8_t)~(SSR_ORER | SSR_FER | SSR_PER);
}
bool kui_sci_dma_transfer(const uint8_t *out, uint8_t *in, size_t bytes) {
    if(!running || !in || !bytes) return false;
    for(size_t at = 0; at < bytes; at += DMA_BYTES)
        if(!dma_piece(out ? out + at : NULL, in + at, bytes - at < DMA_BYTES ? bytes - at : DMA_BYTES)) {
            dma_recover();
            return false;
        }
    return true;
}

/* Async frames (kui_sci_async), for FTP transfers over the W5500: the data
 * moves by DMA channel 1 with no help from the CPU, which meanwhile reads
 * or writes the SD card, and channel 1's transfer-end interrupt finishes the
 * frame. Writes: transmit-only (as KOS's writes), the SCI asking the DMA for
 * each byte (TIE). Reads: receive-only. The SH-4 manual: with the internal
 * clock and only receiving, the SCI clocks continuously until an overrun or
 * until RE is cleared; with the DMA emptying the receive register the bytes
 * come back to back. After the last one the clock runs on for a byte or
 * two, which the W5500 answers with more of its buffer and changes nothing,
 * until the overrun stops it. KOS last set transmit-only (the header); the
 * frame's end sets that again.
 *
 * A read the DMA falls behind on (another bus master held the bus for more
 * than a byte's time: a screen redraw into video memory, music into sound
 * memory) overruns midway: the SCI stops its clock and the DMA waits for
 * bytes that never come. So TMU1 times every async frame, and one still
 * under way at its deadline is stopped and ends as failed, for its user to
 * try again. TMU1 counts at Pphi/4, the SCI's clock at 12.5 MHz: a byte
 * takes 8 counts there, twice that at each slower rate. */
#define READ_MARGIN 3200u   /* about 250 us */
#define WRITE_MARGIN 25000u /* about 2 ms: a write only slows down */
static struct {
    volatile bool busy;
    bool reading;
    uint8_t *in;
    size_t bytes;
    void (*done)(void *arg, bool ok);
    void *arg;
} async;
static bool async_ready; /* the interrupt handlers are in place */
static irq_cb_t tmu1_before;
/* The source of idle clocks: one byte, read again and again. */
static uint8_t idle_source[32] __attribute__((aligned(32)));

static uint32_t byte_counts(size_t bytes) { return (uint32_t)bytes * (8u << rate_open); }
/* TSTR also starts KOS's timers: changed with interrupts off. */
static void deadline_set(uint32_t counts) {
    irq_mask_t mask = irq_disable();
    TMU_TSTR &= (uint8_t)~TSTR_STR1;
    TMU_TCR1 = TCR_UNIE;
    TMU_TCNT1 = counts;
    TMU_TCOR1 = counts;
    TMU_TSTR |= TSTR_STR1;
    irq_restore(mask);
}
static void deadline_clear(void) {
    irq_mask_t mask = irq_disable();
    TMU_TSTR &= (uint8_t)~TSTR_STR1;
    TMU_TCR1 = 0;
    irq_restore(mask);
}

static void receive_clear(void) {
    if(SCSSR1 & SSR_RDRF) (void)SCRDR1;
    SCSSR1 = SSR_CLEAR_RECEIVE;
}
static bool transmit_ended(void) {
    uint64_t deadline = timer_us_gettime64() + 50u;
    while(!(SCSSR1 & SSR_TEND))
        if(timer_us_gettime64() > deadline) return false;
    return true;
}
/* Stops whatever the frame left running and hands the port back to KOS
 * as it was: transmit only, nothing pending. Interrupts off. */
static bool async_stop(void) {
    bool ended = true;
    deadline_clear();
    CHCR1 = 0;
    if(async.reading) {
        SCSCR1 &= (uint8_t)~(SCR_RE | SCR_RIE);
        kui_sci_select(false);
        receive_clear();
        timer_spin_delay_ns(1500);
        SCSCR1 |= SCR_TE;
    } else {
        SCSCR1 &= (uint8_t)~SCR_TIE;
        ended = transmit_ended();
        kui_sci_select(false);
    }
    async.busy = false;
    return ended;
}
static void dma_end(irq_t code, irq_context_t *context, void *data) {
    (void)code; (void)context; (void)data;
    bool finished = (CHCR1 & CHCR_TE) != 0;
    if(!async.busy) { CHCR1 = 0; return; }
    bool ok = async_stop() && finished;
    if(ok && async.reading) {
        (void)dma_map_dst(dma_buffer, async.bytes);
        for(size_t i = 0; i < async.bytes; ++i) async.in[i] = reversed[dma_buffer[i]];
    }
    async.done(async.arg, ok);
}
/* TMU1 at a frame's deadline. */
static void deadline_end(irq_t code, irq_context_t *context, void *data) {
    (void)code; (void)context; (void)data;
    TMU_TSTR &= (uint8_t)~TSTR_STR1;
    TMU_TCR1 = 0;
    /* One that has just ended is finished by its own interrupt, next. */
    if(!async.busy || (CHCR1 & CHCR_TE)) return;
    (void)async_stop();
    async.done(async.arg, false);
}
bool kui_sci_async(const uint8_t header[3], const uint8_t *out, uint8_t *in, size_t bytes,
                   void (*done)(void *arg, bool ok), void *arg) {
    if(!running || !async_ready || async.busy || !done || !bytes || bytes > DMA_BYTES || !out == !in) return false;
    kui_sci_select(true);
    if(sci_spi_write_data(header, 3) != SCI_OK) { kui_sci_select(false); return false; }
    async.reading = !out;
    async.in = in;
    async.bytes = bytes;
    async.done = done;
    async.arg = arg;
    async.busy = true;
    CHCR1 = 0;
    if(out) {
        for(size_t i = 0; i < bytes; ++i) dma_buffer[i] = reversed[out[i]];
        SAR1 = dma_map_src(dma_buffer, bytes);
        DAR1 = hw_to_dma_addr(SCTDR1_ADDR);
        TCR1 = (uint32_t)bytes;
        CHCR1 = CHCR_SCI_TX | CHCR_IE;
        deadline_set(byte_counts(bytes) * 3u + WRITE_MARGIN);
        /* The header has gone, so the transmit register is empty: the
         * first request comes at once. */
        SCSCR1 |= SCR_TIE;
    } else {
        SAR1 = hw_to_dma_addr(SCRDR1_ADDR);
        DAR1 = dma_map_dst(dma_buffer, bytes);
        TCR1 = (uint32_t)bytes;
        CHCR1 = CHCR_SCI_RX | CHCR_IE;
        deadline_set(byte_counts(bytes) + READ_MARGIN);
        /* Transmit off, a pause, then receive on (as KOS changes modes):
         * the clock starts at once. */
        SCSCR1 &= (uint8_t)~(SCR_TE | SCR_RE);
        timer_spin_delay_ns(1500);
        SCSCR1 |= SCR_RE | SCR_RIE;
    }
    return true;
}
/* Idle clocks: a write with no chip selected. Its first byte goes through
 * KOS, which leaves the port transmit-only (after a programmed read it is
 * not), then the DMA sends the rest. */
bool kui_sci_idle(size_t bytes, void (*done)(void *arg, bool ok), void *arg) {
    if(!running || !async_ready || async.busy || !done || bytes < 2u || bytes > DMA_BYTES) return false;
    idle_source[0] = 0xff;
    if(sci_spi_write_data(idle_source, 1) != SCI_OK) return false;
    async.reading = false;
    async.in = NULL;
    async.bytes = bytes - 1u;
    async.done = done;
    async.arg = arg;
    async.busy = true;
    CHCR1 = 0;
    SAR1 = dma_map_src(idle_source, 1);
    DAR1 = hw_to_dma_addr(SCTDR1_ADDR);
    TCR1 = (uint32_t)(bytes - 1u);
    CHCR1 = CHCR_SCI_IDLE | CHCR_IE;
    deadline_set(byte_counts(bytes) * 3u + WRITE_MARGIN);
    SCSCR1 |= SCR_TIE;
    return true;
}
void kui_sci_async_cancel(void) {
    irq_mask_t mask = irq_disable();
    if(async.busy) (void)async_stop();
    irq_restore(mask);
}
static void async_on(bool on) {
    if(async_ready) {
        kui_sci_async_cancel();
        deadline_clear();
        irq_set_handler(EXC_DMAC_DMTE1, NULL, NULL);
        irq_set_priority(IRQ_SRC_TMU1, IRQ_PRIO_MASKED);
        irq_set_handler(EXC_TMU1_TUNI1, tmu1_before.hdl, tmu1_before.data);
        async_ready = false;
    }
    if(!on) return;
    deadline_clear();
    tmu1_before = irq_get_handler(EXC_TMU1_TUNI1);
    if(irq_set_handler(EXC_DMAC_DMTE1, dma_end, NULL) || irq_set_handler(EXC_TMU1_TUNI1, deadline_end, NULL)) {
        irq_set_handler(EXC_DMAC_DMTE1, NULL, NULL);
        irq_set_handler(EXC_TMU1_TUNI1, tmu1_before.hdl, tmu1_before.data);
        return;
    }
    /* The DMA controller's level, as KOS sets it. */
    irq_set_priority(IRQ_SRC_TMU1, 3);
    async_ready = true;
}
