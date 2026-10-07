/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent two-register SCI model with deferred DMA receive service.
 *
 * Primary register semantics: Renesas R01UH0456EJ0702 Rev. 7.02,
 * sections 15.2.2--15.2.4, 15.2.7 and 15.4 (pages 660--661, 668, 718):
 * https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware
 * TDR becomes available when its byte enters the transmit shift register;
 * another byte may therefore queue before the current byte finishes.
 * RDR holds a completed receive byte until read. A later receive with RDR
 * still full loses that incoming byte and latches ORER. A DMA RDR read clears
 * RDRF. The service delay below is a hypothetical finite arbitration delay,
 * not a measured Dreamcast delay or proof of this hardware failure's cause.
 *
 * Model time advances at register reads, independently of the driver's
 * algorithm. The delayed DMA service event and the wire-completion event
 * have separate clocks. No implementation loop or diagnostic code is reused.
 */
#include "sci_sd_bus.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SMR UINT32_C(0xffe00000)
#define BRR UINT32_C(0xffe00004)
#define SCR UINT32_C(0xffe00008)
#define TDR UINT32_C(0xffe0000c)
#define SSR UINT32_C(0xffe00010)
#define RDR UINT32_C(0xffe00014)
#define SCMR UINT32_C(0xffe00018)
#define SPTR UINT32_C(0xffe0001c)
#define STB UINT32_C(0xffc00004)
#define PCTR UINT32_C(0xff80002c)
#define PDTR UINT32_C(0xff800030)
#define SAR UINT32_C(0xffa00010)
#define DAR UINT32_C(0xffa00014)
#define TCR UINT32_C(0xffa00018)
#define CHCR UINT32_C(0xffa0001c)
#define CHCR2 UINT32_C(0xffa0002c)
#define TCR2 UINT32_C(0xffa00028)
#define DMAOR UINT32_C(0xffa00040)
#define TDRE 0x80u
#define RDRF 0x40u
#define ORER 0x20u
#define TEND 0x04u
#define DMA_BASE UINT32_C(0x0c300000)

static struct {
    uint8_t smr, brr, scr, tdr, ssr, rdr, scmr, sptr, stb;
    uint16_t pdtr;
    uint32_t pctr, sar, dar, tcr, chcr, dmaor;
    uint8_t *buffer;
    unsigned wire_ticks, service_ticks, wire_left, receive_age;
    unsigned started, completed, dma_bytes, cpu_reads, queued_count, overruns;
    unsigned reads, writes, steps, dma_starts, cache_purges, masks, restores;
    bool shifting, queued, pending_receive, masked, service_stalled;
    uint8_t shift_value, queued_value;
    uint8_t sent[513];
} hw;

static uint8_t wire_value(unsigned index) {
    return (uint8_t)(index * 73u + 0x5bu);
}
static uint8_t reverse_byte(uint8_t value) {
    uint8_t reversed = 0;
    for(unsigned bit = 0; bit < 8; ++bit) {
        reversed = (uint8_t)((reversed << 1) | (value & 1u));
        value >>= 1;
    }
    return reversed;
}
static uint16_t expected_crc(const uint8_t *bytes, size_t count) {
    uint16_t crc = 0;
    for(size_t i = 0; i < count; ++i) {
        crc ^= (uint16_t)bytes[i] << 8;
        for(unsigned bit = 0; bit < 8; ++bit)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000u) ? 0x1021u : 0u));
    }
    return crc;
}
static bool receive_dma_enabled(void) {
    return (hw.chcr & 3u) == 1u && ((hw.chcr >> 8) & 15u) == 9u &&
           (hw.scr & 0x50u) == 0x50u && (hw.dmaor & 7u) == 1u;
}
static void start_wire_byte(uint8_t value) {
    assert(!hw.shifting && hw.started < sizeof(hw.sent));
    hw.shifting = true;
    hw.shift_value = value;
    hw.wire_left = hw.wire_ticks;
    hw.sent[hw.started++] = value;
    hw.ssr |= TDRE; /* Holding register is empty while the shift register runs. */
    hw.ssr &= (uint8_t)~TEND;
}
static void submit_holding_register(void) {
    assert((hw.scr & 0x20u) && !hw.queued);
    hw.ssr &= (uint8_t)~TDRE;
    if(hw.shifting) {
        hw.queued = true;
        hw.queued_value = hw.tdr;
        ++hw.queued_count;
    } else start_wire_byte(hw.tdr);
}
static void service_receive_dma(void) {
    if(!hw.pending_receive || !receive_dma_enabled() || hw.service_stalled ||
       (hw.ssr & ORER)) return;
    if(++hw.receive_age < hw.service_ticks) return;
    assert(hw.buffer && hw.sar == (RDR & 0x1fffffffu) && hw.tcr);
    assert(hw.dar >= DMA_BASE && hw.dar - DMA_BASE < 512u);
    hw.buffer[hw.dar++ - DMA_BASE] = hw.rdr;
    ++hw.dma_bytes;
    hw.pending_receive = false;
    hw.ssr &= (uint8_t)~RDRF;
    if(!--hw.tcr) hw.chcr |= 2u;
}
static void advance(void) {
    ++hw.steps;
    /* Service an older receive before the next wire completion. A new
     * receive starts its own delay after this opportunity has passed. */
    service_receive_dma();
    if(!hw.shifting) return;
    assert(hw.wire_left);
    if(--hw.wire_left) return;
    hw.shifting = false;
    if(hw.scr & 0x10u) {
        if(hw.ssr & RDRF) {
            hw.ssr |= ORER;
            ++hw.overruns; /* Old RDR is retained; the new byte is lost. */
        } else {
            hw.rdr = reverse_byte(wire_value(hw.completed));
            hw.ssr |= RDRF;
            hw.pending_receive = true;
            hw.receive_age = 0;
        }
    }
    ++hw.completed;
    if(hw.queued) {
        hw.queued = false;
        start_wire_byte(hw.queued_value);
    } else hw.ssr |= TDRE | TEND;
}

uint32_t kui_sci_sd_test_read(uint32_t address, unsigned width) {
    ++hw.reads;
    /* A P4 register read consumes model time; pure bookkeeping APIs do not. */
    advance();
    switch(address) {
        case SMR: assert(width == 1u); return hw.smr;
        case BRR: assert(width == 1u); return hw.brr;
        case SCR: assert(width == 1u); return hw.scr;
        case SSR: assert(width == 1u); return hw.ssr;
        case RDR:
            assert(width == 1u && (hw.ssr & RDRF));
            ++hw.cpu_reads; return hw.rdr;
        case SCMR: assert(width == 1u); return hw.scmr;
        case SPTR: assert(width == 1u); return hw.sptr;
        case STB: assert(width == 1u); return hw.stb;
        case PCTR: assert(width == 4u); return hw.pctr;
        case PDTR: assert(width == 2u); return hw.pdtr;
        case SAR: assert(width == 4u); return hw.sar;
        case DAR: assert(width == 4u); return hw.dar;
        case TCR: assert(width == 4u); return hw.tcr;
        case CHCR: assert(width == 4u); return hw.chcr;
        case DMAOR: assert(width == 4u); return hw.dmaor;
        case CHCR2: assert(width == 4u); return 0x12c0u;
        case TCR2: assert(width == 4u); return 0x80u;
        default: assert(!"Unexpected register read"); return 0;
    }
}
void kui_sci_sd_test_write(uint32_t address, uint32_t value, unsigned width) {
    ++hw.writes;
    switch(address) {
        case SMR: assert(width == 1u); hw.smr = (uint8_t)value; break;
        case BRR: assert(width == 1u && !hw.shifting); hw.brr = (uint8_t)value; break;
        case SCR:
            assert(width == 1u);
            if(!(value & 0x20u)) {
                assert(!hw.shifting || (hw.ssr & ORER) || hw.service_stalled);
                hw.shifting = hw.queued = false;
                hw.ssr |= TDRE | TEND;
            }
            if(value & 0xc0u) assert(hw.masked);
            hw.scr = (uint8_t)value; break;
        case TDR:
            assert(width == 1u && (hw.ssr & TDRE)); hw.tdr = (uint8_t)value; break;
        case SSR: {
            assert(width == 1u);
            bool kick = (hw.ssr & TDRE) && !(value & TDRE);
            hw.ssr &= (uint8_t)value;
            if(!(hw.ssr & RDRF)) hw.pending_receive = false;
            if(kick) submit_holding_register();
            break;
        }
        case SCMR: assert(width == 1u); hw.scmr = (uint8_t)value; break;
        case STB: assert(width == 1u); hw.stb = (uint8_t)value; break;
        case PCTR: assert(width == 4u); hw.pctr = value; break;
        case PDTR:
            assert(width == 2u);
            if((value & 0x80u) && !(hw.pdtr & 0x80u)) assert(!hw.shifting);
            hw.pdtr = (uint16_t)value; break;
        case SAR: assert(width == 4u && hw.masked); hw.sar = value; break;
        case DAR: assert(width == 4u && hw.masked); hw.dar = value; break;
        case TCR: assert(width == 4u && hw.masked); hw.tcr = value; break;
        case CHCR:
            assert(width == 4u && hw.masked);
            if(value & 1u) {
                assert(value == 0x4911u && hw.tcr == 512u && hw.cache_purges);
                ++hw.dma_starts;
            }
            hw.chcr = value; break;
        default: assert(!"Unexpected register write (other channel/global state forbidden)");
    }
}
void kui_sci_sd_test_delay(uint32_t count) {
    assert(count >= 32u);
    for(unsigned i = 0; i < count; ++i) advance();
}
uint32_t kui_sci_sd_test_irq_disable(void) {
    assert(!hw.masked); hw.masked = true; ++hw.masks; return 0x60000060u;
}
void kui_sci_sd_test_irq_restore(uint32_t value) {
    assert(value == 0x60000060u && hw.masked && !hw.shifting && !hw.queued);
    assert(!(hw.scr & 0xc0u)); hw.masked = false; ++hw.restores;
}
uint32_t kui_sci_sd_test_dma_address(const void *buffer, size_t count) {
    assert(buffer && !((uintptr_t)buffer & 31u) && count == 512u);
    hw.buffer = (uint8_t *)(uintptr_t)buffer; return DMA_BASE;
}
void kui_sci_sd_test_cache_purge(void *buffer, size_t count) {
    assert(buffer == hw.buffer && count == 512u); ++hw.cache_purges;
}

static void reset(unsigned wire_ticks, unsigned service_ticks, bool stalled) {
    kui_sci_sd_release();
    memset(&hw, 0, sizeof(hw));
    hw.smr = 0x21u; hw.brr = 7u; hw.scr = 3u; hw.ssr = TDRE | TEND;
    hw.scmr = 0x0au; hw.sptr = 5u; hw.stb = 0x81u;
    hw.pctr = 0xabcd1234u; hw.pdtr = 0x1256u;
    hw.sar = 0x0c100500u; hw.dar = 0x0c200500u; hw.tcr = 7u;
    hw.chcr = 0x4000u; hw.dmaor = 0x0301u;
    hw.wire_ticks = wire_ticks; hw.service_ticks = service_ticks;
    hw.service_stalled = stalled;
    assert(kui_sci_sd_acquire() == KUI_LOADER_SD_OK);
}
static void restored(uint32_t original_chcr) {
    assert(hw.smr == 0x21u && hw.brr == 7u && hw.scr == 3u && hw.scmr == 0x0au);
    assert(hw.stb == 0x81u && hw.pctr == 0xabcd1234u && hw.pdtr == 0x1256u);
    assert(hw.sar == 0x0c100500u && hw.dar == 0x0c200500u && hw.tcr == 7u);
    assert(hw.chcr == original_chcr && hw.dmaor == 0x0301u);
    assert(!hw.masked && !hw.shifting && !hw.queued && hw.masks == hw.restores);
}
static void guarded_read(unsigned wire_ticks, unsigned service_ticks, bool busy) {
    _Alignas(32) uint8_t storage[576];
    reset(wire_ticks, service_ticks, false);
    uint32_t original_chcr = busy ? 0x4001u : 0x4000u;
    hw.chcr = original_chcr;
    memset(storage, 0x5a, sizeof(storage));
    uint16_t crc = 0xa55au;
    const struct kui_loader_sd_bus *bus = kui_sci_sd_bus();
    assert(bus && bus->transfer_block);
#if KUI_RETAIL_SCI_DIAGNOSTIC
    struct kui_sci_sd_diagnostic before = *kui_sci_sd_diagnostic_get();
    assert(!before.phase);
#endif
    bool success = bus->transfer_block(NULL, NULL, storage + 32u, 512u, false, &crc);
#if KUI_SCI_DMA_PACED
    assert(success);
#else
    bool delayed = !busy && service_ticks > wire_ticks;
    assert(success == !delayed);
#endif
    for(unsigned i = 0; i < 32u; ++i)
        assert(storage[i] == 0x5au && storage[i + 544u] == 0x5au);
    if(success) {
        assert(kui_sci_sd_healthy() && hw.started == 512u && hw.completed == 512u);
        assert(!hw.overruns && crc == expected_crc(storage + 32u, 512u));
        for(unsigned i = 0; i < 512u; ++i) {
            assert(storage[i + 32u] == wire_value(i));
            assert(hw.sent[i] == 0xffu);
        }
        assert(hw.dma_bytes == (busy ? 0u : 512u));
        assert(hw.cpu_reads == (busy ? 512u : 0u));
        assert(hw.dma_starts == (busy ? 0u : 1u));
        assert(hw.cache_purges == (busy ? 0u : 1u));
#if KUI_SCI_DMA_PACED
        assert(!hw.queued_count);
#else
        if(!busy) assert(hw.queued_count);
#endif
    } else {
        assert(!kui_sci_sd_healthy() && crc == 0xa55au && hw.overruns && hw.queued_count);
        assert(hw.dma_starts == 1u && hw.dma_bytes < 512u && hw.started < 16u);
#if KUI_RETAIL_SCI_DIAGNOSTIC
        struct kui_sci_sd_diagnostic failure = *kui_sci_sd_diagnostic_get();
        assert(failure.phase == KUI_SCI_PHASE_FEED && failure.reason == KUI_SCI_REASON_SCI);
        assert((failure.ssr & ORER) && failure.scr == 0x70u && (failure.chcr1 & 1u));
        assert(failure.tcr1 && failure.tcr1 <= 512u && failure.dmaor == 0x0301u);
        assert(failure.chcr2 == 0x12c0u && failure.tcr2 == 0x80u);
#endif
        unsigned reads = hw.reads, writes = hw.writes;
        assert(!bus->transfer_block(NULL, NULL, storage + 32u, 512u, false, &crc));
        assert(bus->transfer(NULL, 0xffu, false) == 0xffu);
        assert(hw.reads == reads && hw.writes == writes);
#if KUI_RETAIL_SCI_DIAGNOSTIC
        assert(!memcmp(&failure, kui_sci_sd_diagnostic_get(), sizeof(failure)));
#endif
    }
#if KUI_RETAIL_SCI_DIAGNOSTIC
    struct kui_sci_sd_diagnostic after = *kui_sci_sd_diagnostic_get();
    assert(after.started - before.started == (busy ? 0u : 1u));
    assert(after.success - before.success == ((!busy && success) ? 1u : 0u));
    assert(after.fallback - before.fallback == (busy ? 1u : 0u));
    if(success) assert(!after.phase);
#endif
    assert(hw.steps < 500000u);
    kui_sci_sd_release(); restored(original_chcr);
#if KUI_RETAIL_SCI_DIAGNOSTIC
    assert(!memcmp(&after, kui_sci_sd_diagnostic_get(), sizeof(after)));
#endif
}
static void permanently_stalled_service(void) {
    _Alignas(32) uint8_t storage[576];
    reset(5u, 31u, true);
    memset(storage, 0x5a, sizeof(storage));
    uint16_t crc = 0xa55au;
    const struct kui_loader_sd_bus *bus = kui_sci_sd_bus();
    assert(!bus->transfer_block(NULL, NULL, storage + 32u, 512u, false, &crc));
    assert(!kui_sci_sd_healthy() && crc == 0xa55au && hw.dma_starts == 1u && !hw.dma_bytes);
    assert(hw.steps < 50000u);
#if KUI_RETAIL_SCI_DIAGNOSTIC
    struct kui_sci_sd_diagnostic failure = *kui_sci_sd_diagnostic_get();
    assert(failure.scr == 0x70u && failure.tcr1 == 512u && (failure.chcr1 & 1u));
    assert(failure.dmaor == 0x0301u && failure.chcr2 == 0x12c0u && failure.tcr2 == 0x80u);
#endif
#if KUI_SCI_DMA_PACED
    assert(hw.started == 1u && hw.completed == 1u && !hw.overruns && !hw.queued_count);
#if KUI_RETAIL_SCI_DIAGNOSTIC
    assert(failure.phase == KUI_SCI_PHASE_PACE && failure.reason == KUI_SCI_REASON_TIMEOUT);
    assert((failure.ssr & RDRF) && !(failure.ssr & ORER) && failure.expected == 511u);
    assert(failure.polls == 10000u);
#endif
#else
    assert(hw.overruns && hw.queued_count && hw.started < 16u);
#if KUI_RETAIL_SCI_DIAGNOSTIC
    assert(failure.phase == KUI_SCI_PHASE_FEED && failure.reason == KUI_SCI_REASON_SCI);
    assert(failure.ssr & ORER);
#endif
#endif
    for(unsigned i = 0; i < sizeof(storage); ++i) assert(storage[i] == 0x5au);
    kui_sci_sd_release(); restored(0x4000u);
#if KUI_RETAIL_SCI_DIAGNOSTIC
    assert(!memcmp(&failure, kui_sci_sd_diagnostic_get(), sizeof(failure)));
#endif
}
int main(void) {
    guarded_read(7u, 1u, false); /* Immediate DMA service: both paths must pass. */
    guarded_read(5u, 31u, false); /* Finite delay spans several wire bytes. */
    guarded_read(9u, 71u, false); /* Distinct wire/arbitration timing. */
    guarded_read(5u, 31u, true); /* An occupied channel must retain PIO fallback. */
    permanently_stalled_service();
    puts("SCI DMA pacing: independent delayed service, exact payload/CRC, guarded fallback and bounded stall PASS");
    return 0;
}
