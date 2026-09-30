/* SPDX-License-Identifier: GPL-3.0-only */
/* The Wi-Fi board's link on this console: the SCI port as an SPI bus
 * (sci_port.c), full duplex through KOS's sci_spi_rw_data, and at 12.5 MHz
 * by DMA (kui_sci_dma_transfer) from KUI_SCI_DMA_MIN bytes. Its chip select
 * is GPIO6 (the network connector's) or GPIO7 (the usual W5500 point), both
 * tried from the fastest rate down, and each transfer waits for the board's
 * READY line (GPIO5) to change, as firmware/kui-wifi/PROTOCOL.md says. A DMA
 * transfer that fails is reported as a failed transfer, which the link sends
 * again (it also checks each frame's CRC32); after DMA_GIVE_UP failures in a
 * row, DMA stays off. */
#include "kui/network_wifi.h"
#include "sci_port.h"
#include <dc/sci.h>
#include <kos/thread.h>
#include <kos/timer.h>

static const unsigned selects[] = {6u, 7u};
static const char *const names[] = {"12.5 MHz, select GPIO6", "6.25 MHz, select GPIO6", "3.125 MHz, select GPIO6",
                                    "1.5625 MHz, select GPIO6", "12.5 MHz, select GPIO7", "6.25 MHz, select GPIO7",
                                    "3.125 MHz, select GPIO7", "1.5625 MHz, select GPIO7"};
#define LEVELS (sizeof(names) / sizeof(names[0]))
#define DMA_GIVE_UP 4u
static bool dma;
static unsigned dma_failures;
/* The board answers within a few hundred microseconds; past that, the
 * wait lets other threads run. */
#define SPIN_US 200u
/* READY's level before the last transfer; -1 when not known. */
static int seen = -1;

static bool transfer(void *ctx, const uint8_t *out, uint8_t *in, size_t bytes, bool *ready) {
    (void)ctx;
    *ready = false;
    if(!kui_sci_running() || !bytes) return false;
    int level = kui_sci_ready();
    if(seen >= 0) {
        uint64_t start = timer_us_gettime64();
        while(level == seen) {
            uint64_t waited = timer_us_gettime64() - start;
            if(waited > KUI_WIFI_READY_MS * 1000u) break;
            if(waited > SPIN_US) thd_pass();
            level = kui_sci_ready();
        }
        *ready = level != seen;
    }
    /* After a wait that ran out, the current level is taken as armed. */
    seen = level;
    kui_sci_select(true);
    bool ok;
    if(dma && bytes >= KUI_SCI_DMA_MIN) {
        ok = kui_sci_dma_transfer(out, in, bytes);
        if(ok) dma_failures = 0;
        else if(++dma_failures >= DMA_GIVE_UP) dma = false;
    } else ok = sci_spi_rw_data(out, in, bytes) == SCI_OK;
    kui_sci_select(false);
    return ok;
}
static uint64_t now_ms(void *ctx) { (void)ctx; return timer_ms_gettime64(); }
/* KOS: thd_sleep(0) is thd_pass(). */
static void pause_ms(void *ctx, unsigned ms) { (void)ctx; thd_sleep(ms); }
static const struct kui_wifi_bus bus = {NULL, transfer, now_ms, pause_ms};

static bool open_level(unsigned level) {
    seen = -1;
    bool ok = level < LEVELS && kui_sci_open(level % KUI_SCI_RATES, selects[level / KUI_SCI_RATES]);
    dma = ok && !(level % KUI_SCI_RATES) && kui_sci_dma_ready();
    dma_failures = 0;
    return ok;
}
static void close_port(void) { kui_sci_close(); }
static const char *speed(unsigned level) {
    if(level < LEVELS && !(level % KUI_SCI_RATES) && dma)
        return level ? "12.5 MHz with DMA, select GPIO7" : "12.5 MHz with DMA, select GPIO6";
    return level < LEVELS ? names[level] : "?";
}
static const struct kui_wifi_port port = {&bus, LEVELS, KUI_SCI_RATES, open_level, close_port, speed};
const struct kui_wifi_port *kui_wifi_console_port(void) { return &port; }
