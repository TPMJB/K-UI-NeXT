/* SPDX-License-Identifier: BSD-3-Clause
 *
 * SCIF pin wiring and edge order adapted from pinned KallistiOS scif-spi.c:
 * Copyright (C) 2012 Lawrence Sebald
 * Copyright (C) 2023, 2025 Ruslan Rostovtsev
 * Copyright (C) 2024 Paul Cercueil
 * Upstream: fcfa7d869471591ca1c777543261a7bfea7cb726.
 * K-UI adaptation Copyright (C) 2026 K-UI contributors.
 * See LICENSES/LICENSE.KOS for the retained BSD conditions and disclaimer.
 *
 * This bus deliberately differs from the accepted standalone probe bus: it
 * never resets a FIFO, clears serial status, changes a timer, or owns SCIF
 * between requests. The existing sd_reader.c remains the read-only protocol.
 */
#include "retail_sd.h"

#if defined(KUI_ON_CONSOLE) || defined(KUI_RETAIL_SD_TEST)

#define SC_SCR UINT32_C(0xffe80008)
#define SC_FCR UINT32_C(0xffe80018)
#define SC_FDR UINT32_C(0xffe8001c)
#define SC_PTR UINT32_C(0xffe80020)
#define SC_ACTIVE UINT16_C(0x00f8) /* TIE, RIE, TE, RE, REIE */
#define SC_FIFO_COUNTS UINT16_C(0x1f1f)
#define SC_MODEM UINT16_C(0x0008)
#define PIN_RTSIO UINT16_C(0x0080)
#define PIN_RTSDT UINT16_C(0x0040) /* active-low SD chip select */
#define PIN_CTSIO UINT16_C(0x0020)
#define PIN_CTSDT UINT16_C(0x0010) /* SD clock */
#define PIN_SPB2IO UINT16_C(0x0002)
#define PIN_SPB2DT UINT16_C(0x0001)

#ifdef KUI_RETAIL_SD_TEST
/* Only the test build substitutes MMIO and the fixed instruction delay. */
extern uint16_t kui_retail_sd_test_read16(uint32_t address);
extern void kui_retail_sd_test_write16(uint32_t address, uint16_t value);
extern void kui_retail_sd_test_delay(void);
#define read16 kui_retail_sd_test_read16
#define write16 kui_retail_sd_test_write16
#else
static uint16_t read16(uint32_t address) {
    return *(volatile uint16_t *)(uintptr_t)address;
}
static void write16(uint32_t address, uint16_t value) {
    *(volatile uint16_t *)(uintptr_t)address = value;
}
#endif

static struct {
    uint32_t work;
    uint16_t saved_scr, saved_fcr, saved_ptr, pins;
    bool acquired;
} port;

enum kui_loader_sd_result kui_retail_sd_acquire(void) {
    if(port.acquired)
        return KUI_LOADER_SD_NOT_READY;

    uint16_t scr = read16(SC_SCR);
    if((scr & SC_ACTIVE) || (read16(SC_FDR) & SC_FIFO_COUNTS))
        return KUI_LOADER_SD_UNSUPPORTED;

    /* Caller-masked interrupts keep the validation/snapshot/claim atomic. No
     * transmitter or receiver is active, and neither FIFO contains data. */
    port.saved_scr = scr;
    port.saved_fcr = read16(SC_FCR);
    port.saved_ptr = read16(SC_PTR);
    port.pins = PIN_RTSIO | PIN_RTSDT | PIN_CTSIO | PIN_SPB2IO;
    write16(SC_SCR, 0);
    /* Only modem-control ownership changes; FIFO reset and threshold bits
     * keep their original values. In particular we never assert FIFO reset. */
    write16(SC_FCR, port.saved_fcr & (uint16_t)~SC_MODEM);
    write16(SC_PTR, port.pins);
    port.acquired = true;
    return KUI_LOADER_SD_OK;
}

void kui_retail_sd_release(void) {
    if(!port.acquired)
        return;
    /* Terminate our ownership with chip select high and clock low before
     * restoring exactly the controls we borrowed. Protocol read/init already
     * clock the required deselected trailing byte before reaching here. */
    write16(SC_PTR, (port.pins | PIN_RTSDT) & (uint16_t)~PIN_CTSDT);
    write16(SC_PTR, port.saved_ptr);
    write16(SC_FCR, port.saved_fcr);
    write16(SC_SCR, port.saved_scr);
    port.acquired = false;
}

static void bus_begin(void *context) {
    (void)context; /* init's checked acquisition has already claimed the pins */
}

static void bus_end(void *context) {
    (void)context;
    kui_retail_sd_release();
}

static void bus_select(void *context, bool selected) {
    (void)context;
    if(!port.acquired)
        return;
    if(selected)
        port.pins &= (uint16_t)~PIN_RTSDT;
    else
        port.pins |= PIN_RTSDT;
    write16(SC_PTR, port.pins);
}

static void slow_edge_delay(void) {
#ifdef KUI_RETAIL_SD_TEST
    kui_retail_sd_test_delay();
#else
    /* 512 dependent decrement/conditional-branch iterations require at least
     * 512 CPU cycles even with hot caches: >=2.56 us at stock 200 MHz. Fetch or
     * bus stalls only lengthen this initialization delay. No timer is touched.
     * BF is deliberately the non-delay-slot instruction. */
    uint32_t count = 512;
    __asm__ __volatile__("1: dt %0\n\tbf 1b\n\t"
                         : "+r"(count) : : "t", "memory");
#endif
}

static uint8_t bus_transfer(void *context, uint8_t data, bool slow) {
    (void)context;
    /* These are work units, not the nominal 12.5 MHz timer ticks of the probe
     * bus. Existing protocol spans consequently allow 50,000 bytes waiting for
     * ready/data and 200,000 bytes for init/read work. They provide finite work
     * limits only: no timeout here is claimed to measure elapsed seconds. */
    port.work += 125;
    if(!port.acquired)
        return 0xff;
    uint16_t pins = port.pins & (uint16_t)~(PIN_CTSDT | PIN_SPB2DT);
    uint8_t received = 0;
    for(unsigned bit_index = 0; bit_index < 8; ++bit_index) {
        uint16_t bit = (data >> (7u - bit_index)) & 1u;
        /* Tx is established before the CTS rising clock edge, matching the
         * independently sourced SCIF serial-SD wiring. */
        write16(SC_PTR, pins | bit);
        if(slow)
            slow_edge_delay();
        write16(SC_PTR, pins | bit | PIN_CTSDT);
        received = (uint8_t)((received << 1) | (read16(SC_PTR) & PIN_SPB2DT));
        if(slow)
            slow_edge_delay();
    }
    return received;
}

static uint32_t bus_ticks(void *context) {
    (void)context;
    return port.work;
}

enum kui_loader_sd_result kui_retail_sd_init(struct kui_loader_sd *card) {
    if(!card)
        return KUI_LOADER_SD_ARGUMENT;
    card->ready = false;
    enum kui_loader_sd_result result = kui_retail_sd_acquire();
    if(result != KUI_LOADER_SD_OK)
        return result;
    const struct kui_loader_sd_bus bus = {
        NULL, bus_begin, bus_end, bus_select, bus_transfer, bus_ticks
    };
    result = kui_loader_sd_init_bus(card, &bus);
    /* Failure may have called bus.end already; release is idempotent. Success
     * retains ready/capacity/CRC mode while returning the serial pin controls. */
    kui_retail_sd_release();
    return result;
}

#else

enum kui_loader_sd_result kui_retail_sd_init(struct kui_loader_sd *card) {
    (void)card;
    return KUI_LOADER_SD_UNSUPPORTED;
}
enum kui_loader_sd_result kui_retail_sd_acquire(void) {
    return KUI_LOADER_SD_UNSUPPORTED;
}
void kui_retail_sd_release(void) {}

#endif
