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
static unsigned run_starts, run_stops, run_singles, run_blocks;
static bool fail_stop;

/* Protocol byte framing lives in test_loader_sd; these stubs check how the
 * retail image callback selects and bounds that already-tested protocol. */
enum kui_loader_sd_result kui_loader_sd_read(struct kui_loader_sd *card,
    uint32_t lba, uint32_t count, void *out) {
    /* Image runs no longer use CMD17; count any call as a failure below. */
    (void)card; (void)lba; (void)count; (void)out;
    ++run_singles;
    return KUI_LOADER_SD_COMMAND;
}
enum kui_loader_sd_result kui_loader_sd_stream_start(struct kui_loader_sd *card,
    struct kui_loader_sd_stream *stream, uint32_t lba, uint32_t count) {
    assert(card->ready && !stream->active && count >= 1 && count <= KUI_RETAIL_SD_STREAM_MAX);
    ++run_starts; stream->active = true; stream->next_lba = lba; stream->remaining = count;
    return KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_loader_sd_stream_next(struct kui_loader_sd *card,
    struct kui_loader_sd_stream *stream, void *out) {
    assert(card->ready && stream->active && stream->remaining);
    ++run_blocks; memset(out, (int)(stream->next_lba++ & 255), 512);
    if(!--stream->remaining) stream->active = false;
    return KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_loader_sd_stream_stop(struct kui_loader_sd *card,
    struct kui_loader_sd_stream *stream) {
    if(!stream->active) return KUI_LOADER_SD_OK;
    ++run_stops; stream->active = false; stream->remaining = 0;
    if(fail_stop) { card->ready = false; return KUI_LOADER_SD_TIMEOUT; }
    return KUI_LOADER_SD_OK;
}

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

static void adopt_local_callbacks(void) {
    reset();
    struct kui_loader_sd source = {0}, adopted = {0}, local = {0};
    assert(kui_retail_sd_init(&local) == KUI_LOADER_SD_OK);
    source = local;
    /* Source callbacks point at temporary stage code. Even NULL/foreign bus
     * pointers must be replaced, never retained or invoked during adoption. */
    memset(&source.bus, 0, sizeof(source.bus));
    source.bus.ctx = &source;
    unsigned writes = fake.count, inits = fake.init_calls;
    assert(kui_retail_sd_adopt(&adopted, &source) == KUI_LOADER_SD_OK);
    assert(fake.count == writes && fake.init_calls == inits);
    assert(adopted.bus.ctx == NULL && adopted.bus.begin == local.bus.begin);
    assert(adopted.bus.end == local.bus.end && adopted.bus.select == local.bus.select);
    assert(adopted.bus.transfer == local.bus.transfer && adopted.bus.ticks == local.bus.ticks);
    assert(adopted.ready && !adopted.slow && adopted.blocks == source.blocks);
    assert(adopted.high_capacity == source.high_capacity);
    memset(&source, 0, sizeof(source)); /* Discard the entire high-stage state. */
    assert(kui_retail_sd_acquire() == KUI_LOADER_SD_OK);
    assert(kui_retail_sd_adopt(&source, &adopted) == KUI_LOADER_SD_NOT_READY);
    adopted.bus.select(adopted.bus.ctx, true);
    assert(fake.ptr == 0xa2u);
    kui_retail_sd_release(); restored();
    assert(kui_retail_sd_adopt(&source, &source) == KUI_LOADER_SD_ARGUMENT);
    assert(kui_retail_sd_adopt(NULL, &adopted) == KUI_LOADER_SD_ARGUMENT);
    adopted.slow = true;
    assert(kui_retail_sd_adopt(&source, &adopted) == KUI_LOADER_SD_NOT_READY);
}

static void bounded_run_selection(void) {
    struct kui_loader_sd card = {.ready = true};
    struct kui_loader_sd_stream stream = {0};
    uint8_t out[512];
    /* One stream is capped at STREAM_MAX even when more blocks are available. */
    for(uint32_t i = 0; i < KUI_RETAIL_SD_STREAM_MAX; ++i) {
        assert(kui_retail_sd_read_run(&card, &stream, 100 + i, 90 - i, out) == KUI_LOADER_SD_OK);
        assert(out[0] == (uint8_t)(100 + i));
    }
    assert(!stream.active && run_starts == 1 && run_blocks == KUI_RETAIL_SD_STREAM_MAX);
    /* Short tails, even a single block, are streams bounded by what remains. */
    assert(kui_retail_sd_read_run(&card, &stream, 164, 1, out) == KUI_LOADER_SD_OK);
    assert(!stream.active && run_starts == 2 && out[0] == 164);
    assert(kui_retail_sd_read_run(&card, &stream, 200, 8, out) == KUI_LOADER_SD_OK);
    assert(stream.remaining == 7 && stream.next_lba == 201 && run_starts == 3);
    /* A new extent/request must never continue a prior stream blindly. */
    assert(kui_retail_sd_read_run(&card, &stream, 201, 2, out) == KUI_LOADER_SD_OK);
    assert(run_stops == 1 && run_starts == 4 && stream.remaining == 1 && stream.next_lba == 202);
    assert(kui_retail_sd_read_run(&card, &stream, 300, 10, out) == KUI_LOADER_SD_OK);
    assert(run_stops == 2 && run_starts == 5 && stream.remaining == 9);
    assert(kui_retail_sd_read_run(&card, &stream, 900, 2, out) == KUI_LOADER_SD_OK);
    assert(run_stops == 3 && run_starts == 6 && stream.remaining == 1);
    assert(kui_retail_sd_read_run(&card, &stream, 901, 1, out) == KUI_LOADER_SD_OK);
    assert(!stream.active && run_stops == 3 && run_starts == 6 && out[0] == (uint8_t)901);
    assert(kui_retail_sd_read_run(&card, &stream, 1000, 10, out) == KUI_LOADER_SD_OK);
    fail_stop = true;
    assert(kui_retail_sd_read_run(&card, &stream, 2000, 2, out) == KUI_LOADER_SD_TIMEOUT);
    assert(!stream.active && !card.ready && run_stops == 4 && run_starts == 7);
    assert(!run_singles);
}

int main(void) {
    blocked_serial_is_untouched();
    init_and_bit_edges();
    init_errors_release_once();
    adopt_local_callbacks();
    bounded_run_selection();
    puts("retail SD ownership, local adoption, bounded runs and bit edges passed");
    return 0;
}
