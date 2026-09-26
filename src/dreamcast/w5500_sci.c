/* SPDX-License-Identifier: GPL-3.0-only */
/* The W5500's SPI link on this console: the SH-4's SCI port in synchronous
 * mode through KOS's SCI driver (dc/sci.h). TXD1 carries MOSI, RXD1 MISO
 * and SCK1 the clock; chip select is port A pin 7, which KOS drives as a
 * GPIO on a retail console. The SD card stays on SCIF, which nothing here
 * touches. */
#include "kui/network_w5500.h"
#include <arch/timer.h>
#include <dc/sci.h>
#include <dc/syscalls.h>
#include <kos/thread.h>
#include <string.h>

static const uint32_t rates[] = {SCI_SPI_BAUD_12M500K, SCI_SPI_BAUD_6M250K, SCI_SPI_BAUD_3M125K, SCI_SPI_BAUD_1M562K};
static const char *const names[] = {"12.5 MHz", "6.25 MHz", "3.125 MHz", "1.5625 MHz"};
static bool running;

static bool frame(void *ctx, const uint8_t header[3], const uint8_t *out, uint8_t *in, size_t bytes) {
    (void)ctx;
    if(!running) return false;
    sci_spi_set_cs(true);
    bool ok = sci_spi_write_data(header, 3) == SCI_OK;
    if(ok && bytes) ok = (out ? sci_spi_write_data(out, bytes) : sci_spi_read_data(in, bytes)) == SCI_OK;
    sci_spi_set_cs(false);
    return ok;
}
static uint64_t now_ms(void *ctx) { (void)ctx; return timer_ms_gettime64(); }
static void pause_ms(void *ctx, unsigned ms) {
    (void)ctx;
    if(ms) thd_sleep((int)ms); else thd_pass();
}
static const struct kui_w5500_bus bus = {NULL, frame, now_ms, pause_ms};

static bool open_level(unsigned level) {
    if(level >= sizeof(rates) / sizeof(rates[0])) return false;
    if(running) { sci_shutdown(); running = false; }
    /* No DMA buffer: every transfer is programmed I/O. */
    running = sci_init(rates[level], SCI_MODE_SPI, SCI_CLK_INT, 0) == SCI_OK;
    return running;
}
static void close_port(void) {
    if(running) sci_shutdown();
    running = false;
}
static const char *speed(unsigned level) { return level < sizeof(names) / sizeof(names[0]) ? names[level] : "?"; }
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
