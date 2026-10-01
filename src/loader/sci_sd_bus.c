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

#ifdef KUI_SCI_SD_TEST
extern uint32_t kui_sci_sd_test_read(uint32_t address, unsigned width);
extern void kui_sci_sd_test_write(uint32_t address, uint32_t value, unsigned width);
extern void kui_sci_sd_test_delay(uint32_t count);
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
        delay(200000u); /* >=1 ms at 200 MHz, no borrowed hardware timer. */
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
static uint8_t reverse(uint8_t x) {
    x = (uint8_t)((x >> 4) | (x << 4));
    x = (uint8_t)(((x & 0xccu) >> 2) | ((x & 0x33u) << 2));
    return (uint8_t)(((x & 0xaau) >> 1) | ((x & 0x55u) << 1));
}
static bool wait_flag(uint8_t flag) {
    for(unsigned n = 0; n < POLLS; ++n) {
        uint8_t status = rd8(SSR);
        if(status & ERRORS) break;
        if(status & flag) return true;
    }
    port.fault = true;
    wr8(SCR, 0);
    return false;
}
static uint8_t transfer(void *ctx, uint8_t data, bool slow) {
    (void)ctx;
    /* Minimum wire time in the protocol's nominal 12.5 MHz ticks: a slow
     * byte takes 256 ticks, a fast byte eight. CPU/poll overhead only makes
     * the deadline more conservative; sd_reader also imposes finite byte
     * budgets. Retaining SCIF's 125 units would prematurely shorten SCI
     * token/busy waits as the bus gets faster. This is not wall-clock time. */
    port.work += slow ? 256u : 8u;
    if(!port.acquired || port.fault) return 0xff;
    if(port.slow != slow) {
        if(!wait_flag(TEND)) return 0xff;
        wr8(SCR, 0); wr8(BRR, slow ? 31 : 0); /* Fast: 12.5 MHz. */
        delay(1024u);
        port.slow = slow;
    }
    wr8(SCR, 0x30u); /* TE + RE; clock generated only for transmitted bytes. */
    if(!wait_flag(TDRE)) return 0xff;
    wr8(TDR, reverse(data));
    wr8(SSR, 0x7cu); /* Clear observed TDRE, preserve the other flags. */
    if(!wait_flag(RDRF)) return 0xff;
    uint8_t received = rd8(RDR);
    wr8(SSR, 0xbcu); /* Clear observed RDRF, preserve TDRE and errors. */
    return reverse(received);
}
static uint32_t ticks(void *ctx) { (void)ctx; return port.work; }
static const struct kui_loader_sd_bus bus = {NULL, begin, end, select_card, transfer, ticks};
const struct kui_loader_sd_bus *kui_sci_sd_bus(void) { return &bus; }
bool kui_sci_sd_healthy(void) { return port.acquired && !port.fault; }
#else
enum kui_loader_sd_result kui_sci_sd_acquire(void) { return KUI_LOADER_SD_UNSUPPORTED; }
void kui_sci_sd_release(void) {}
const struct kui_loader_sd_bus *kui_sci_sd_bus(void) { return NULL; }
bool kui_sci_sd_healthy(void) { return false; }
#endif
