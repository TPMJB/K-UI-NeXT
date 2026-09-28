/* SPDX-License-Identifier: GPL-3.0-only */
/* See sci_port.h. Port A's registers are the SH-4's: PCTRA holds two bits
 * per pin (the low one set: output; the high one set: no pull-up) and PDTRA
 * the pins' levels, at the addresses KOS's SCI driver uses for GPIO7. */
#include "sci_port.h"
#include <dc/sci.h>
#include <stdint.h>

#define PCTRA (*(volatile uint32_t *)0xff80002cu)
#define PDTRA (*(volatile uint16_t *)0xff800030u)
#define PIN_SELECT 6u
#define PIN_READY 5u

static const uint32_t rates[KUI_SCI_RATES] = {SCI_SPI_BAUD_12M500K, SCI_SPI_BAUD_6M250K, SCI_SPI_BAUD_3M125K,
                                              SCI_SPI_BAUD_1M562K};
static bool running;
static unsigned selected = 7u;

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
    /* No DMA buffer: every transfer is programmed I/O. KOS takes GPIO7 as
     * its chip select and leaves it high, so a device there stays
     * deselected while another is used. */
    if(sci_init(rates[rate], SCI_MODE_SPI, SCI_CLK_INT, 0) != SCI_OK) return false;
    running = true;
    selected = select;
    if(select == PIN_SELECT) {
        /* High before it drives, so the device never sees a stray select. */
        pin_high(PIN_SELECT, true);
        pin_mode(PIN_SELECT, 1u);
    }
    pin_mode(PIN_READY, 0u);
    return true;
}
void kui_sci_close(void) {
    if(!running) return;
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
