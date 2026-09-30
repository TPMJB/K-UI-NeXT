/* SPDX-License-Identifier: GPL-3.0-only */
/* The W5500's SPI link on this console: the SH-4's SCI port in synchronous
 * mode (sci_port.c). TXD1 carries MOSI, RXD1 MISO and SCK1 the clock. The
 * chip select is GPIO7 (port A pin 7, the usual W5500 point) or GPIO6 (the
 * network connector's); both are tried, each from the fastest level down:
 * 12.5 MHz with DMA reads (kui_sci_dma_transfer), 12.5 MHz without, then the
 * slower rates. The wiring check, with its 1 KB read, runs through DMA
 * first; if it fails there, the next level is the same clock without DMA. A
 * DMA read that fails later is read again by programmed I/O, as reading
 * changes nothing on the chip; after DMA_GIVE_UP failures in a row, DMA
 * stays off. The SD card stays on SCIF, which nothing here touches. */
#include "kui/network_w5500.h"
#include "sci_port.h"
#include <dc/sci.h>
#include <dc/syscalls.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <string.h>

static const unsigned selects[] = {7u, 6u};
static const char *const names[] = {
    "12.5 MHz with DMA", "12.5 MHz", "6.25 MHz", "3.125 MHz", "1.5625 MHz",
    "12.5 MHz with DMA, select GPIO6", "12.5 MHz, select GPIO6", "6.25 MHz, select GPIO6", "3.125 MHz, select GPIO6",
    "1.5625 MHz, select GPIO6"};
#define PER_SELECT (KUI_SCI_RATES + 1u)
#define LEVELS (sizeof(names) / sizeof(names[0]))
#define DMA_GIVE_UP 4u
static bool dma;
static unsigned dma_failures;

static bool read_data(const uint8_t header[3], uint8_t *in, size_t bytes) {
    if(dma && bytes >= KUI_SCI_DMA_MIN) {
        if(kui_sci_dma_transfer(NULL, in, bytes)) {
            dma_failures = 0;
            return true;
        }
        if(++dma_failures >= DMA_GIVE_UP) dma = false;
        /* Reading changes nothing on the chip: read it all again. */
        kui_sci_select(false);
        kui_sci_select(true);
        if(sci_spi_write_data(header, 3) != SCI_OK) return false;
    }
    return sci_spi_read_data(in, bytes) == SCI_OK;
}
static bool frame(void *ctx, const uint8_t header[3], const uint8_t *out, uint8_t *in, size_t bytes) {
    (void)ctx;
    if(!kui_sci_running()) return false;
    kui_sci_select(true);
    bool ok = sci_spi_write_data(header, 3) == SCI_OK;
    if(ok && bytes) ok = out ? sci_spi_write_data(out, bytes) == SCI_OK : read_data(header, in, bytes);
    kui_sci_select(false);
    return ok;
}
static uint64_t now_ms(void *ctx) { (void)ctx; return timer_ms_gettime64(); }
/* KOS: thd_sleep(0) is thd_pass(). */
static void pause_ms(void *ctx, unsigned ms) { (void)ctx; thd_sleep(ms); }
static const struct kui_w5500_bus bus = {NULL, frame, now_ms, pause_ms};

static bool open_level(unsigned level) {
    if(level >= LEVELS) return false;
    unsigned step = level % PER_SELECT;
    bool ok = kui_sci_open(step ? step - 1u : 0u, selects[level / PER_SELECT]);
    dma = ok && !step && kui_sci_dma_ready();
    dma_failures = 0;
    return ok;
}
static void close_port(void) { kui_sci_close(); }
static const char *speed(unsigned level) {
    if(level < LEVELS && !(level % PER_SELECT) && !dma)
        return level ? "12.5 MHz (DMA failed), select GPIO6" : "12.5 MHz (DMA failed)";
    return level < LEVELS ? names[level] : "?";
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
static const struct kui_w5500_port port = {&bus, LEVELS, open_level, close_port, speed, mac};
const struct kui_w5500_port *kui_w5500_console_port(void) { return &port; }
