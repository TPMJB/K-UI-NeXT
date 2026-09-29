/* SPDX-License-Identifier: GPL-3.0-only */
/* The W5500's SPI link on this console: the SH-4's SCI port in synchronous
 * mode through KOS's SCI driver (dc/sci.h). TXD1 carries MOSI, RXD1 MISO
 * and SCK1 the clock; chip select is port A pin 7, which KOS drives as a
 * GPIO on a retail console. The SD card stays on SCIF, which nothing here
 * touches. */
#include "kui/network_w5500.h"
#include <arch/dmac.h>
#include <arch/irq.h>
#include <arch/timer.h>
#include <dc/sci.h>
#include <dc/syscalls.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <string.h>

/* The first level also reads by DMA. KOS's programmed reads wait for each
 * byte before clocking the next; with DMA channel 1 emptying the receive
 * register, the CPU can clock bytes out back to back. This is K-UI's own
 * transfer, not KOS's sci_spi_dma_read_data: that one never sets the SCI's
 * RIE bit, so the SCI never asks the DMA controller for a byte, and it then
 * waits in dma_wait_complete for an interrupt it did not enable (the console
 * locked up with it, commit 389f9ca). Here RIE is set only for the length of
 * a read, the SCI's own interrupt is masked, channel 1 is programmed directly,
 * and every wait has a deadline: a read that does not finish in time is read
 * again by programmed I/O. After DMA_GIVE_UP failures in a row, DMA stays
 * off. The wiring check, with its 1 KB read, runs through DMA first. */
static const uint32_t rates[] = {SCI_SPI_BAUD_12M500K, SCI_SPI_BAUD_12M500K, SCI_SPI_BAUD_6M250K, SCI_SPI_BAUD_3M125K,
                                 SCI_SPI_BAUD_1M562K};
static const char *const names[] = {"12.5 MHz with DMA", "12.5 MHz", "6.25 MHz", "3.125 MHz", "1.5625 MHz"};
#define DMA_LEVEL 0u
#define DMA_BYTES 4096u  /* one piece of a longer read */
#define DMA_MIN 16u      /* shorter reads are quicker programmed */
#define DMA_GIVE_UP 4u

/* SH-4 registers, as the hardware manual names them: the SCI's, and DMA
 * channel 1's. */
#define SCSCR1 (*(volatile uint8_t *)0xffe00008u)
#define SCTDR1 (*(volatile uint8_t *)0xffe0000cu)
#define SCSSR1 (*(volatile uint8_t *)0xffe00010u)
#define SCRDR1 (*(volatile uint8_t *)0xffe00014u)
#define SCRDR1_ADDR 0xffe00014u
#define SCR_RIE 0x40u
#define SSR_TDRE 0x80u
#define SSR_RDRF 0x40u
#define SSR_ORER 0x20u
#define SSR_FER 0x10u
#define SSR_PER 0x08u
#define SSR_TEND 0x04u
#define SAR1 (*(volatile uint32_t *)0xffa00010u)
#define DAR1 (*(volatile uint32_t *)0xffa00014u)
#define TCR1 (*(volatile uint32_t *)0xffa00018u)
#define CHCR1 (*(volatile uint32_t *)0xffa0001cu)
#define DMAOR (*(volatile uint32_t *)0xffa00040u)
#define CHCR_DE 0x1u
#define CHCR_TE 0x2u
/* Destination increments, source fixed, the SCI's receive request, cycle
 * steal, one byte at a time, no interrupt. */
#define CHCR_SCI_RX ((1u << 14) | ((uint32_t)DMA_REQUEST_SCI_RECEIVE << 8) | ((uint32_t)DMA_UNITSIZE_8BIT << 4) | CHCR_DE)

static bool running, dma;
static unsigned dma_failures;
static uint8_t dma_buffer[DMA_BYTES] __attribute__((aligned(32)));
static uint8_t reversed[256];

/* DMA is on, with no address error or NMI stop, and channel 1 is idle. */
static bool dma_usable(void) { return (DMAOR & 0x7u) == 0x1u && (!(CHCR1 & CHCR_DE) || (CHCR1 & CHCR_TE)); }

/* One piece of a read: its first byte by programmed I/O, which also leaves
 * KOS's driver with transmit and receive on, as it expects; the rest by DMA
 * while the CPU clocks out dummy bytes. */
static bool dma_piece(uint8_t *in, size_t bytes) {
    if(sci_spi_read_data(in, 1) != SCI_OK) return false;
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
        SCTDR1 = 0xff;
        SCSSR1 &= (uint8_t)~SSR_TDRE;
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
 * error or stale byte left for the programmed read that follows. */
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
static bool read_data(const uint8_t header[3], uint8_t *in, size_t bytes) {
    if(dma && bytes >= DMA_MIN) {
        bool ok = true;
        for(size_t at = 0; ok && at < bytes; at += DMA_BYTES)
            ok = dma_piece(in + at, bytes - at < DMA_BYTES ? bytes - at : DMA_BYTES);
        if(ok) {
            dma_failures = 0;
            return true;
        }
        dma_recover();
        if(++dma_failures >= DMA_GIVE_UP) dma = false;
        /* Reading changes nothing on the chip: read it all again. */
        sci_spi_set_cs(false);
        sci_spi_set_cs(true);
        if(sci_spi_write_data(header, 3) != SCI_OK) return false;
    }
    return sci_spi_read_data(in, bytes) == SCI_OK;
}
static bool frame(void *ctx, const uint8_t header[3], const uint8_t *out, uint8_t *in, size_t bytes) {
    (void)ctx;
    if(!running) return false;
    sci_spi_set_cs(true);
    bool ok = sci_spi_write_data(header, 3) == SCI_OK;
    if(ok && bytes) ok = out ? sci_spi_write_data(out, bytes) == SCI_OK : read_data(header, in, bytes);
    sci_spi_set_cs(false);
    return ok;
}
static uint64_t now_ms(void *ctx) { (void)ctx; return timer_ms_gettime64(); }
/* KOS: thd_sleep(0) is thd_pass(). */
static void pause_ms(void *ctx, unsigned ms) { (void)ctx; thd_sleep(ms); }
static const struct kui_w5500_bus bus = {NULL, frame, now_ms, pause_ms};

static bool open_level(unsigned level) {
    if(level >= sizeof(rates) / sizeof(rates[0])) return false;
    if(running) { sci_shutdown(); running = false; }
    for(unsigned b = 0; b < 256u; ++b) {
        unsigned r = 0;
        for(unsigned bit = 0; bit < 8u; ++bit) r |= ((b >> bit) & 1u) << (7u - bit);
        reversed[b] = (uint8_t)r;
    }
    dma = level == DMA_LEVEL && dma_usable();
    dma_failures = 0;
    /* RIE also raises the SCI's own interrupt, which nothing handles. */
    if(dma) irq_set_priority(IRQ_SRC_SCI1, IRQ_PRIO_MASKED);
    running = sci_init(rates[level], SCI_MODE_SPI, SCI_CLK_INT, 0) == SCI_OK;
    return running;
}
static void close_port(void) {
    if(running) sci_shutdown();
    running = false;
}
static const char *speed(unsigned level) {
    if(level == DMA_LEVEL && !dma) return "12.5 MHz (DMA failed)";
    return level < sizeof(names) / sizeof(names[0]) ? names[level] : "?";
}
static void mac(uint8_t out[6]) {
    /* The W5500 has no address of its own. A locally administered one is
     * made from the console's unique ID, so it stays the same each time. */
    uint64_t id = syscall_sysinfo_id(), hash = UINT64_C(0xcbf29ce484222325);
    for(unsigned i = 0; i < 8; ++i) { hash ^= (uint8_t)(id >> (i * 8u)); hash *= UINT64_C(0x100000001b3); }
    if(!id || id == UINT64_MAX) hash = UINT64_C(0x4b5549000001);
    out[0] = 0x02;
    for(unsigned i = 1; i < 6; ++i) out[i] = (uint8_t)(hash >> ((i - 1u) * 8u));
}
static const struct kui_w5500_port port = {&bus, sizeof(rates) / sizeof(rates[0]), open_level, close_port, speed, mac};
const struct kui_w5500_port *kui_w5500_console_port(void) { return &port; }
