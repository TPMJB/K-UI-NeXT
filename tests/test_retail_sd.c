/* SPDX-License-Identifier: GPL-3.0-only */
#include "retail_sd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define SCR UINT32_C(0xffe80008)
#define FCR UINT32_C(0xffe80018)
#define FDR UINT32_C(0xffe8001c)
#define PTR UINT32_C(0xffe80020)

static struct {
    uint16_t scr, fcr, fdr, ptr;
    struct { uint32_t address; uint16_t value; } writes[128];
    unsigned count, delays, samples, init_calls;
    uint8_t incoming;
    bool sampling;
    enum kui_loader_sd_result init_result;
} fake;

/* Any access outside this exact four-register set fails. In particular these
 * tests prohibit writes to TMU, SCFSR/SCLSR and FIFO data/reset interfaces. */
uint16_t kui_retail_sd_test_read16(uint32_t address) {
    switch(address) {
        case SCR: return fake.scr;
        case FCR: return fake.fcr;
        case FDR: return fake.fdr;
        case PTR:
            if(fake.sampling) {
                assert(fake.samples < 8);
                assert(fake.ptr & 0x10u); /* sample on the high clock edge */
                unsigned bit = (fake.incoming >> (7u - fake.samples++)) & 1u;
                return (fake.ptr & (uint16_t)~1u) | bit;
            }
            return fake.ptr;
        default: assert(!"unexpected MMIO read"); return 0;
    }
}

void kui_retail_sd_test_write16(uint32_t address, uint16_t value) {
    assert(fake.count < sizeof(fake.writes) / sizeof(fake.writes[0]));
    fake.writes[fake.count].address = address;
    fake.writes[fake.count++].value = value;
    switch(address) {
        case SCR: fake.scr = value; break;
        case FCR:
            /* Existing reset bits must never be newly asserted. */
            assert(!(value & 6u));
            fake.fcr = value;
            break;
        case PTR: fake.ptr = value; break;
        default: assert(!"unexpected MMIO write");
    }
}

void kui_retail_sd_test_delay(void) { ++fake.delays; }

/* Protocol details are tested by test_loader_sd.c. Here this boundary stub
 * checks only the new wrapper's pin lease lifecycle and callback ownership. */
enum kui_loader_sd_result kui_loader_sd_init_bus(
    struct kui_loader_sd *card, const struct kui_loader_sd_bus *bus) {
    ++fake.init_calls;
    card->bus = *bus;
    bus->begin(bus->ctx);
    assert(fake.scr == 0 && !(fake.fcr & 8u));
    assert(fake.ptr == 0xe2u);
    bus->select(bus->ctx, true);
    assert(fake.ptr == 0xa2u);
    bus->select(bus->ctx, false);
    assert(fake.ptr == 0xe2u);
    card->high_capacity = true;
    card->blocks = UINT64_C(0x1000000);
    card->ready = fake.init_result == KUI_LOADER_SD_OK;
    if(!card->ready)
        bus->end(bus->ctx);
    return fake.init_result;
}

static void reset(void) {
    kui_retail_sd_release();
    memset(&fake, 0, sizeof(fake));
    fake.scr = 3; /* allowed clock-selection state is preserved */
    fake.fcr = 0xb8;
    fake.ptr = 0x4c;
}

static void restored(void) {
    assert(fake.scr == 3 && fake.fcr == 0xb8 && fake.ptr == 0x4c);
    assert(fake.fdr == 0);
}

static void blocked_serial_is_untouched(void) {
    for(unsigned bit = 8; bit <= 128; bit <<= 1) {
        reset();
        fake.scr |= bit;
        assert(kui_retail_sd_acquire() == KUI_LOADER_SD_UNSUPPORTED);
        assert(fake.count == 0);
        kui_retail_sd_release();
        assert(fake.count == 0 && fake.scr == (3u | bit));
    }
    const uint16_t queued[] = {1, 16, 0x100, 0x1000, 0x1010};
    for(unsigned n = 0; n < sizeof(queued) / sizeof(queued[0]); ++n) {
        reset();
        fake.fdr = queued[n];
        assert(kui_retail_sd_acquire() == KUI_LOADER_SD_UNSUPPORTED);
        assert(fake.count == 0 && fake.fdr == queued[n]);
    }
}

static void init_and_bit_edges(void) {
    struct kui_loader_sd card = {0};
    reset();
    assert(kui_retail_sd_init(&card) == KUI_LOADER_SD_OK);
    assert(fake.init_calls == 1 && card.ready && card.high_capacity);
    restored();
    assert(kui_retail_sd_acquire() == KUI_LOADER_SD_OK);
    unsigned writes = fake.count;
    assert(kui_retail_sd_acquire() == KUI_LOADER_SD_NOT_READY);
    assert(fake.count == writes);

    card.bus.select(card.bus.ctx, true);
    unsigned start = fake.count;
    uint32_t before = card.bus.ticks(card.bus.ctx);
    fake.incoming = 0x3c;
    fake.sampling = true;
    assert(card.bus.transfer(card.bus.ctx, 0xa5, true) == 0x3c);
    fake.sampling = false;
    assert(fake.samples == 8 && fake.delays == 16);
    assert(card.bus.ticks(card.bus.ctx) - before == 125);
    assert(fake.count - start == 16);
    for(unsigned bit = 0; bit < 8; ++bit) {
        uint16_t tx = (0xa5u >> (7u - bit)) & 1u;
        assert(fake.writes[start + bit * 2].address == PTR);
        assert(fake.writes[start + bit * 2].value == (0xa2u | tx));
        assert(fake.writes[start + bit * 2 + 1].address == PTR);
        assert(fake.writes[start + bit * 2 + 1].value == (0xb2u | tx));
    }

    /* Receive-only fast path: every possible incoming byte must retain the
     * same sixteen pin writes, eight high-edge samples and work accounting.
     * Also check the generic command path without slow initialization delays. */
    for(unsigned value = 0; value < 257; ++value) {
        fake.count = fake.samples = fake.delays = 0;
        fake.incoming = (uint8_t)value;
        uint8_t outgoing = value == 256 ? 0xa5 : 0xff;
        before = card.bus.ticks(card.bus.ctx);
        fake.sampling = true;
        assert(card.bus.transfer(card.bus.ctx, outgoing, false) == fake.incoming);
        fake.sampling = false;
        assert(fake.delays == 0 && fake.samples == 8 && fake.count == 16);
        assert(card.bus.ticks(card.bus.ctx) - before == 125);
        for(unsigned bit = 0; bit < 8; ++bit) {
            uint16_t tx = (outgoing >> (7u - bit)) & 1u;
            assert(fake.writes[bit * 2].address == PTR);
            assert(fake.writes[bit * 2].value == (0xa2u | tx));
            assert(fake.writes[bit * 2 + 1].address == PTR);
            assert(fake.writes[bit * 2 + 1].value == (0xb2u | tx));
        }
    }
    kui_retail_sd_release();
    restored();
    assert(card.ready && card.blocks == UINT64_C(0x1000000));
    writes = fake.count;
    kui_retail_sd_release();
    assert(fake.count == writes);

    /* No callback can manipulate serial hardware without a successful lease. */
    card.bus.select(card.bus.ctx, true);
    assert(card.bus.transfer(card.bus.ctx, 0, false) == 0xff);
    assert(fake.count == writes);

    /* Each new lease snapshots current game configuration, not startup state. */
    fake.scr = 1; fake.fcr = 0x40; fake.ptr = 0x20;
    assert(kui_retail_sd_acquire() == KUI_LOADER_SD_OK);
    kui_retail_sd_release();
    assert(fake.scr == 1 && fake.fcr == 0x40 && fake.ptr == 0x20);
}

static void init_errors_release_once(void) {
    struct kui_loader_sd card = {0};
    reset();
    fake.init_result = KUI_LOADER_SD_CRC;
    assert(kui_retail_sd_init(&card) == KUI_LOADER_SD_CRC);
    assert(!card.ready && fake.init_calls == 1);
    restored();
    /* Three acquisition writes, two selects and four release writes. */
    assert(fake.count == 9);
    assert(kui_retail_sd_acquire() == KUI_LOADER_SD_OK);
    kui_retail_sd_release();
    restored();

    reset();
    fake.scr |= 0x20;
    card.ready = true;
    assert(kui_retail_sd_init(&card) == KUI_LOADER_SD_UNSUPPORTED);
    assert(!card.ready && fake.count == 0 && fake.init_calls == 0);
    assert(kui_retail_sd_init(NULL) == KUI_LOADER_SD_ARGUMENT);
    assert(fake.count == 0);
}

int main(void) {
    blocked_serial_is_untouched();
    init_and_bit_edges();
    init_errors_release_once();
    puts("retail SD ownership, restoration, bit edges and work budgets passed");
    return 0;
}
