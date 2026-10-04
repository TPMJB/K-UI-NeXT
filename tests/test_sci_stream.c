/* SPDX-License-Identifier: GPL-3.0-only */
/* The game reader's SCI stream against the shared card/SCI/DMA model. */
#include "sci_stream_model.h"

static struct kui_loader_sd sd;
static uint8_t areas[2][KUI_SCI_STREAM_AREA_BYTES] __attribute__((aligned(32)));

static void reset_model(void) {
    memset(&card, 0, sizeof(card));
    card.nac = 1; card.nac_first = 40; card.busy = 6; card.blocks = 100000; card.high_capacity = true;
    memset(&m, 0, sizeof(m));
    m.smr = 0x80u; m.brr = 0; m.scr = 0x30u; m.ssr = 0x84u; m.sptr = 0; m.cs_high = true; m.healthy = true;
    m.sar = 0x11111111u; m.dar = 0x22222222u; m.tcr = 0x33u; m.chcr = 0; m.dmaor = 0x8201u;
    m.areas[0] = areas[0]; m.areas[1] = areas[1];
    m.late_take = true; /* as on the console */
    memset(&sd, 0, sizeof(sd));
    sd.bus.select = bus_select; sd.bus.transfer = bus_transfer;
    sd.high_capacity = true; sd.ready = true; sd.blocks = card.blocks;
    memset(areas, 0xa5, sizeof(areas));
}
static void open_stream(bool unknown) {
    assert(kui_sci_stream_open(&sd, areas[0], areas[1], unknown) == KUI_SCI_STREAM_OK);
}
static void check_block(uint32_t lba, const uint8_t *p) {
    assert(p);
    for(unsigned i = 0; i < 512; ++i) assert(p[i] == data_at(lba, i));
}
static void check_restored(void) {
    assert(m.scr == 0x30u && m.smr == 0x80u && m.brr == 0 && m.scmr == 0 && m.sptr == 0);
    assert(!(m.ssr & 0x78u) && !m.rx && !(m.stbcr & 1u));
}
static void check_channel_restored(void) {
    assert(m.sar == 0x11111111u && m.dar == 0x22222222u && m.tcr == 0x33u && m.chcr == 0);
}

/* A consumer as the resident uses it: take the next block, fetch the one
 * after it before copying, refetch after any failure. interrupt_polls: it
 * looks as the reader's interrupt does (a mid-block overrun is left for the
 * next fetch) rather than waiting. */
static bool interrupt_polls;
static unsigned drive(uint32_t first, uint32_t count, uint8_t *out, bool overlap) {
    unsigned failures = 0;
    uint32_t want = first;
    while(want < first + count) {
        enum kui_sci_stream_result r;
        assert(failures < 20000u);
        if(kui_sci_stream_busy()) {
            if(interrupt_polls) {
                r = kui_sci_stream_poll(true);
                if(r == KUI_SCI_STREAM_PENDING) continue;
            } else r = kui_sci_stream_wait();
            if(r != KUI_SCI_STREAM_OK) {++failures; continue;}
        }
        const uint8_t *p = kui_sci_stream_take(want, &r);
        if(r == KUI_SCI_STREAM_CRC) {
            ++failures;
            if(kui_sci_stream_fetch(want, LIMIT, false) != KUI_SCI_STREAM_OK) ++failures;
            continue;
        }
        if(p) {
            if(overlap && want + 1u < first + count &&
               kui_sci_stream_fetch(want + 1u, LIMIT, false) != KUI_SCI_STREAM_OK) ++failures;
            memcpy(out + (size_t)(want - first) * 512u, p, 512);
            ++want;
            continue;
        }
        assert(r == KUI_SCI_STREAM_PENDING);
        if(kui_sci_stream_fetch(want, LIMIT, false) != KUI_SCI_STREAM_OK) ++failures;
    }
    return failures;
}
static void check_range(uint32_t first, uint32_t count, const uint8_t *out) {
    for(uint32_t b = 0; b < count; ++b) check_block(first + b, out + (size_t)b * 512u);
}
static uint8_t out[512 * 300];

static void test_sequential(void) {
    reset_model();
    open_stream(false);
    const struct kui_sci_stream_stats *st = kui_sci_stream_stats();
    struct kui_sci_stream_stats before = *st;
    assert(drive(100, 64, out, true) == 0);
    check_range(100, 64, out);
    assert(st->starts - before.starts == 1 && card.cmd18 == 1);
    assert(st->blocks - before.blocks == 64 && !st->polled);
    assert(card.cmd18 == 1 && !card.cmd12 && m.dma_starts == 64 && m.module_resets == 64);
    assert(m.purges == 64 && !m.irq_starts && m.armed_chcr == 0x4911u);
    check_restored(); check_channel_restored();
    /* The stream continues where it paused, without another CMD18. */
    assert(drive(164, 10, out, false) == 0);
    check_range(164, 10, out);
    assert(card.cmd18 == 1 && !m.irq_starts && m.armed_chcr == 0x4911u);
    /* Elsewhere: CMD12 with its busy interval, then a new CMD18. */
    assert(drive(5000, 3, out, true) == 0);
    check_range(5000, 3, out);
    assert(card.cmd18 == 2 && card.cmd12 == 1 && !card.idle_cmd12 && st->starts - before.starts == 2);
    assert(kui_sci_stream_stop() == KUI_SCI_STREAM_OK);
    assert(card.cmd12 == 2 && !card.streaming && m.cs_high);
    check_restored(); check_channel_restored();
}
static void test_pending_and_kept(void) {
    reset_model();
    open_stream(false);
    m.delay = 3;
    enum kui_sci_stream_result r;
    assert(kui_sci_stream_fetch(7, LIMIT, false) == KUI_SCI_STREAM_OK && kui_sci_stream_busy());
    assert(!kui_sci_stream_take(7, &r) && r == KUI_SCI_STREAM_PENDING);
    assert(kui_sci_stream_poll(false) == KUI_SCI_STREAM_PENDING);
    assert(kui_sci_stream_fetch(8, LIMIT, false) == KUI_SCI_STREAM_BUSY);
    unsigned polls = 1;
    while((r = kui_sci_stream_poll(false)) == KUI_SCI_STREAM_PENDING) ++polls;
    assert(r == KUI_SCI_STREAM_OK && !kui_sci_stream_busy() && polls <= 4);
    const uint8_t *p = kui_sci_stream_take(7, &r);
    check_block(7, p);
    /* The next block arrives in the other area while block 7 stays usable;
     * a request that starts in block 7 again gets it without a second read. */
    assert(kui_sci_stream_fetch(8, LIMIT, false) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
    const uint8_t *again = kui_sci_stream_take(7, &r);
    assert(again == p && r == KUI_SCI_STREAM_OK);
    check_block(7, again);
    assert(kui_sci_stream_stats()->kept);
    /* A block fetched while an arrived one awaits taking goes into the kept
     * block's area: the arrived block stays, the kept one gives way. */
    assert(kui_sci_stream_ready(8) && !kui_sci_stream_ready(7));
    assert(kui_sci_stream_fetch(9, LIMIT, false) == KUI_SCI_STREAM_OK && !kui_sci_stream_ready(8));
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK && kui_sci_stream_ready(9));
    check_block(8, kui_sci_stream_take(8, &r));
    check_block(9, kui_sci_stream_take(9, &r));
    check_block(9, kui_sci_stream_take(9, &r));
    assert(!kui_sci_stream_take(7, &r) && r == KUI_SCI_STREAM_PENDING);
    kui_sci_stream_discard();
    assert(!kui_sci_stream_take(9, &r) && r == KUI_SCI_STREAM_PENDING);
    /* Overlapped: each next block starts before the arrived one is taken. */
    assert(kui_sci_stream_fetch(10, LIMIT, false) == KUI_SCI_STREAM_OK);
    for(uint32_t lba = 10; lba < 16; ++lba) {
        assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK && kui_sci_stream_ready(lba));
        assert(kui_sci_stream_fetch(lba + 1u, LIMIT, false) == KUI_SCI_STREAM_OK && kui_sci_stream_busy());
        assert(!kui_sci_stream_ready(lba));
        check_block(lba, kui_sci_stream_take(lba, &r));
    }
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
    check_block(16, kui_sci_stream_take(16, &r));
}
static void test_crc_and_token(void) {
    reset_model();
    open_stream(false);
    const struct kui_sci_stream_stats *st = kui_sci_stream_stats();
    struct kui_sci_stream_stats before = *st;
    card.corrupt_lba = 205; card.corrupt_count = 1;
    card.bad_token_lba = 210; card.bad_token_count = 1;
    assert(drive(200, 20, out, true) == 2);
    check_range(200, 20, out);
    assert(st->crc_errors - before.crc_errors == 1 && st->token_errors - before.token_errors == 1);
    assert(card.cmd18 == 3 && card.cmd12 == 2);
    check_restored(); check_channel_restored();
}
static void test_overrun_and_stall(void) {
    reset_model();
    open_stream(false);
    enum kui_sci_stream_result r;
    const struct kui_sci_stream_stats *st = kui_sci_stream_stats();
    struct kui_sci_stream_stats before = *st;
    /* Held off the bus mid-block at byte `at`: RDR holds it and byte at+1
     * overruns and is lost. Back on the bus, the channel takes byte `at`
     * (as on the console), or it stays held off and RDR keeps it until the
     * reception stops. Either way reception resumes at at+2 with the card
     * where it stopped, and the CRC rebuilds the lost byte: no CMD12, no
     * new CMD18. */
    static const unsigned positions[] = {1, 2, 3, 77, 255, 256, 300, 508, 509, 510};
    const unsigned count = sizeof(positions) / sizeof(*positions);
    uint32_t lba = 40;
    for(unsigned late = 0; late < 2; ++late) {
        m.late_take = late;
        for(unsigned i = 0; i < count; ++i, ++lba) {
            unsigned resets = m.module_resets;
            m.overrun_after = positions[i];
            assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_OK);
            assert(kui_sci_stream_poll(false) == KUI_SCI_STREAM_PENDING && kui_sci_stream_busy());
            assert(m.chcr & 1u && m.dar == AREA_BASE + (m.dar & 0x1000u) + positions[i] + 2u);
            assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK && m.module_resets == resets + 2u);
            check_block(lba, kui_sci_stream_take(lba, &r));
            check_restored(); check_channel_restored();
        }
    }
    m.late_take = true;
    assert(st->repaired - before.repaired == 2u * count && st->overruns == before.overruns);
    assert(card.cmd18 == 1 && !card.cmd12 && st->crc_errors == before.crc_errors);
    /* The channel's take of that byte still in flight when the reception is
     * stopped (RDR read, the write held off the bus): stopping the channel
     * does not cancel it, and the fence after the stop lets it land before
     * the count is read, so the lost byte is still the one after it. */
    static const unsigned flights[] = {1, 77, 300, 509};
    for(unsigned i = 0; i < 4; ++i, ++lba) {
        unsigned fences = m.fences;
        m.overrun_after = flights[i]; m.in_flight = true;
        assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_OK);
        assert(kui_sci_stream_poll(false) == KUI_SCI_STREAM_PENDING && !m.flying && !m.in_flight);
        assert(m.fences - fences == 2u); /* before and after the stop */
        assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
        check_block(lba, kui_sci_stream_take(lba, &r));
        check_restored(); check_channel_restored();
    }
    assert(st->repaired - before.repaired == 2u * count + 4u && !st->ahead);
    assert(card.cmd18 == 1 && !card.cmd12 && st->crc_errors == before.crc_errors);
    /* A resumed reception that runs a byte ahead ends with the card's gap
     * byte where the second CRC byte belongs (or the next token, with no
     * gap): the block is fetched again without a rebuild, which would accept
     * the wrong bytes 1 time in 256, and repair stays on however often it
     * happens. */
    for(unsigned gap = 0; gap < 4u; ++gap, lba += 2) {
        card.nac = gap ? gap : 0;
        m.overrun_after = 50u + gap * 100u; m.ahead_on_resume = 1;
        assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_OK);
        assert(kui_sci_stream_poll(false) == KUI_SCI_STREAM_PENDING);
        assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK && !m.ahead_on_resume);
        assert(!kui_sci_stream_take(lba, &r) && r == KUI_SCI_STREAM_CRC);
        assert(st->ahead == gap + 1u && st->crc_errors - before.crc_errors == gap + 1u);
        card.nac = 1;
        assert(drive(lba, 2, out, true) == 0);
        check_range(lba, 2, out);
    }
    unsigned cmd12 = card.cmd12;
    m.overrun_after = 123;
    assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_poll(false) == KUI_SCI_STREAM_PENDING);
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
    check_block(lba, kui_sci_stream_take(lba, &r));
    assert(card.cmd12 == cmd12 && st->repaired - before.repaired == 2u * count + 9u);
    ++lba;
    before = *st;
    cmd12 = card.cmd12;
    /* Too late to rebuild (a CRC byte lost), or a second loss in the same
     * block: the stream restarts at that block. */
    static const unsigned late[] = {511, 512};
    for(unsigned i = 0; i < 2; ++i, ++lba) {
        m.overrun_after = late[i];
        assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_OK);
        assert(kui_sci_stream_poll(false) == KUI_SCI_STREAM_OVERRUN && !kui_sci_stream_busy());
        check_restored(); check_channel_restored();
        assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_OK);
        assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
        check_block(lba, kui_sci_stream_take(lba, &r));
    }
    m.overrun_after = 100; m.overrun_again = 200;
    assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_poll(false) == KUI_SCI_STREAM_PENDING);
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OVERRUN);
    assert(st->overruns - before.overruns == 3 && card.cmd12 - cmd12 == 2);
    assert(drive(lba, 2, out, true) == 0);
    check_range(lba, 2, out);
    lba += 2;
    /* Another fault in a repaired block is detected, not "rebuilt" away.
     * After a second such block the card is taken not to resume mid-block
     * as expected, so later overruns restart the stream (until it is opened
     * again); one alone (a stray bit error) leaves repair on. */
    for(unsigned fault = 0; fault < 2u; ++fault) {
        card.corrupt_lba = lba; card.corrupt_count = 1;
        m.overrun_after = 300;
        assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_OK);
        assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
        assert(!kui_sci_stream_take(lba, &r) && r == KUI_SCI_STREAM_CRC);
        assert(drive(lba, 3, out, true) == 0);
        check_range(lba, 3, out);
        lba += 3;
        if(fault) break;
        unsigned still = st->repaired;
        m.overrun_after = 40;
        assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_OK);
        assert(kui_sci_stream_poll(false) == KUI_SCI_STREAM_PENDING && st->repaired == still + 1u);
        assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
        check_block(lba, kui_sci_stream_take(lba, &r));
        lba += 1;
    }
    unsigned repaired = st->repaired, overruns = st->overruns;
    m.overrun_after = 40;
    assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_poll(false) == KUI_SCI_STREAM_OVERRUN);
    assert(st->repaired == repaired && st->overruns == overruns + 1u);
    assert(drive(lba, 2, out, true) == 0);
    check_range(lba, 2, out);
    lba += 2;
    open_stream(true); /* the card may still be streaming: CMD12 first */
    m.overrun_after = 40;
    assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_poll(false) == KUI_SCI_STREAM_PENDING && st->repaired == repaired + 1u);
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
    check_block(lba, kui_sci_stream_take(lba, &r));
    lba += 1;
    /* A receiver that never finishes ends as an overrun after the bound. */
    m.stall = true;
    assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OVERRUN);
    check_restored(); check_channel_restored();
    m.stall = false;
    assert(drive(lba, 5, out, true) == 0);
    check_range(lba, 5, out);
}
static void test_deferred_repair(void) {
    /* Seen from an interrupt, a mid-block overrun is not resumed there: the
     * SCI is handed back, the card waits deselected, nothing is in flight,
     * and the next fetch of that block resumes it in place (no CMD12, no
     * new CMD18), however the channel took the byte RDR held. */
    reset_model();
    open_stream(false);
    const struct kui_sci_stream_stats *st = kui_sci_stream_stats();
    struct kui_sci_stream_stats before = *st;
    enum kui_sci_stream_result r;
    static const unsigned positions[] = {1, 77, 300, 509};
    uint32_t lba = 20;
    for(unsigned late = 0; late < 2; ++late) {
        m.late_take = late;
        for(unsigned i = 0; i < 4; ++i, ++lba) {
            unsigned dma = m.dma_starts;
            m.overrun_after = positions[i];
            assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_OK);
            assert(kui_sci_stream_poll(true) == KUI_SCI_STREAM_PENDING && !kui_sci_stream_busy());
            assert(m.cs_high && m.dma_starts == dma + 1u);
            check_restored(); check_channel_restored();
            assert(!kui_sci_stream_take(lba, &r) && r == KUI_SCI_STREAM_PENDING);
            assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_OK && kui_sci_stream_busy());
            assert(m.dma_starts == dma + 2u && !m.cs_high);
            assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
            check_block(lba, kui_sci_stream_take(lba, &r));
        }
    }
    assert(st->deferred - before.deferred == 8 && st->repaired - before.repaired == 8);
    assert(st->overruns == before.overruns && st->crc_errors == before.crc_errors);
    assert(card.cmd18 == 1 && !card.cmd12);
    /* Another block asked for instead: the card is stopped mid-block. */
    m.late_take = true; m.overrun_after = 100;
    assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_poll(true) == KUI_SCI_STREAM_PENDING && !kui_sci_stream_busy());
    assert(drive(lba + 10u, 3, out, true) == 0);
    check_range(lba + 10u, 3, out);
    assert(card.cmd12 == 1 && card.cmd18 == 2);
    assert(kui_sci_stream_stop() == KUI_SCI_STREAM_OK);
    check_restored(); check_channel_restored();
}
static void test_channel_busy_and_foreign(void) {
    reset_model();
    open_stream(false);
    enum kui_sci_stream_result r;
    const struct kui_sci_stream_stats *st = kui_sci_stream_stats();
    struct kui_sci_stream_stats before = *st;
    /* Someone else's transfer on channel 1: programmed reception instead. */
    m.chcr = 1u;
    unsigned bytes_before = bus_bytes;
    assert(kui_sci_stream_fetch(300, LIMIT, false) == KUI_SCI_STREAM_OK && !kui_sci_stream_busy());
    assert(st->polled - before.polled == 1 && m.chcr == 1u && !m.dma_starts);
    assert(bus_bytes - bytes_before >= 514u + 40u);
    check_block(300, kui_sci_stream_take(300, &r));
    assert(kui_sci_stream_fetch(301, LIMIT, false) == KUI_SCI_STREAM_OK);
    check_block(301, kui_sci_stream_take(301, &r));
    assert(card.cmd18 == 1 && st->starts - before.starts == 1);
    m.chcr = 0;
    /* The channel taken over mid-block: the SCI is still handed back. */
    m.foreign_during_rx = true;
    assert(kui_sci_stream_fetch(302, LIMIT, false) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_poll(false) == KUI_SCI_STREAM_BUSY);
    assert(st->foreign - before.foreign == 1 && m.sar == 0x0c200000u);
    check_restored();
    m.sar = 0x11111111u; m.dar = 0x22222222u; m.tcr = 0x33u; m.chcr = 0;
    assert(drive(302, 4, out, true) == 0);
    check_range(302, 4, out);
    check_channel_restored();
}
static void test_unknown_and_gapless(void) {
    /* After a bus fault the card may still stream: the first fetch stops it. */
    reset_model();
    card.streaming = true; card.lba = 77; card.gap = 3;
    open_stream(true);
    assert(drive(90, 3, out, true) == 0);
    check_range(90, 3, out);
    assert(card.cmd12 == 1 && !card.idle_cmd12 && card.cmd18 == 1);
    /* An idle card answers CMD12 with "illegal command": still stopped. */
    reset_model();
    open_stream(true);
    assert(drive(91, 2, out, true) == 0);
    check_range(91, 2, out);
    assert(card.idle_cmd12 == 1 && card.cmd18 == 1);
    /* A card with no gap between blocks loses each next token to the
     * receiver's overrun byte; every block then needs a restart. */
    reset_model();
    card.nac = 0;
    open_stream(false);
    unsigned failures = drive(500, 8, out, true);
    check_range(500, 8, out);
    assert(failures == 7 && card.cmd18 == 8);
}
static void test_byte_addressed(void) {
    reset_model();
    card.high_capacity = false; sd.high_capacity = false;
    open_stream(false);
    assert(drive(1234, 3, out, true) == 0);
    check_range(1234, 3, out);
}
static void test_random_faults(void) {
    reset_model();
    open_stream(false);
    srand(12345);
    uint32_t lba = 1000;
    for(unsigned round = 0; round < 400; ++round) {
        unsigned count = 1u + (unsigned)rand() % 40u;
        int fault = rand() % 10;
        if(fault == 0) {card.corrupt_lba = lba + (unsigned)rand() % count; card.corrupt_count = 1;}
        if(fault == 1) {m.overrun_after = 1u + (unsigned)rand() % 512u; m.in_flight = rand() % 2;}
        if(fault == 2) {card.bad_token_lba = lba + (unsigned)rand() % count; card.bad_token_count = 1;}
        if(fault == 3) m.chcr = 1u;
        if(fault == 4) {m.overrun_after = 1u + (unsigned)rand() % 500u; m.ahead_on_resume = 1;}
        m.delay = (unsigned)rand() % 4u;
        card.nac = 1u + (unsigned)rand() % 3u;
        interrupt_polls = rand() % 2;
        (void)drive(lba, count, out, rand() % 4 != 0);
        check_range(lba, count, out);
        if(fault == 3) m.chcr = 0;
        m.overrun_after = 0; m.in_flight = false; m.ahead_on_resume = 0;
        check_restored();
        if(!(rand() % 5)) lba += 1000u + (unsigned)rand() % 5000u;
        else lba += count;
        if(lba > 90000u) lba = (unsigned)rand() % 1000u;
    }
    const struct kui_sci_stream_stats *st = kui_sci_stream_stats();
    printf("random: blocks %u polled %u starts %u kept %u overruns %u repaired %u deferred %u ahead %u crc %u token %u\n",
        st->blocks, st->polled, st->starts, st->kept, st->overruns, st->repaired, st->deferred,
        st->ahead, st->crc_errors, st->token_errors);
    assert(st->crc_errors && st->repaired && st->deferred && st->ahead && st->token_errors && st->polled);
    interrupt_polls = false;
    assert(kui_sci_stream_stop() == KUI_SCI_STREAM_OK);
    check_restored(); check_channel_restored();
}
static void test_bus_fault(void) {
    reset_model();
    m.sptr = 0x02u;
    assert(kui_sci_stream_open(&sd, areas[0], areas[1], false) == KUI_SCI_STREAM_RESET);
    reset_model();
    m.healthy = false;
    assert(kui_sci_stream_open(&sd, areas[0], areas[1], false) == KUI_SCI_STREAM_RESET);
    reset_model();
    open_stream(false);
    /* Beyond the card: CMD18 is rejected. */
    assert(kui_sci_stream_fetch(card.blocks, LIMIT, false) == KUI_SCI_STREAM_COMMAND);
    m.healthy = false;
    assert(kui_sci_stream_fetch(3, LIMIT, false) == KUI_SCI_STREAM_RESET);
}

static void test_polled_on_request(void) {
    /* Asked for, a block is read by programmed transfers even with the
     * channel idle: no DMA, no overrun however the bus is held. */
    reset_model();
    open_stream(false);
    const struct kui_sci_stream_stats *st = kui_sci_stream_stats();
    struct kui_sci_stream_stats before = *st;
    enum kui_sci_stream_result r;
    unsigned dma = m.dma_starts;
    m.overrun_every = 100;
    assert(kui_sci_stream_fetch(60, LIMIT, true) == KUI_SCI_STREAM_OK && !kui_sci_stream_busy());
    check_block(60, kui_sci_stream_take(60, &r));
    assert(st->polled - before.polled == 1 && m.dma_starts == dma && st->overruns == before.overruns);
    /* The stream continues by DMA from there. */
    m.overrun_every = 0;
    assert(kui_sci_stream_fetch(61, LIMIT, false) == KUI_SCI_STREAM_OK && kui_sci_stream_busy());
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
    check_block(61, kui_sci_stream_take(61, &r));
    assert(card.cmd18 == 1 && m.fences >= 1);
}
static uint8_t crc_vector[512];
static uint8_t vector_content(uint32_t lba, unsigned offset) {
    (void)lba;
    return crc_vector[offset];
}
static void test_crc_reference_vectors(void) {
    /* The card model emits CRC from its independent bit-at-a-time 0x1021
     * reference. Exercise the complete public receive/check path under both
     * native and CE builds, including bit reversal and the separate RDR tail. */
    assert(crc16((const uint8_t *)"123456789", 9) == 0x31c3u);
    for(unsigned pattern = 0; pattern < 6u; ++pattern) {
        uint32_t random = 0x7ab32ed1u;
        for(unsigned i = 0; i < sizeof(crc_vector); ++i) {
            random = random * 1664525u + 1013904223u;
            crc_vector[i] = pattern == 0 ? 0 : pattern == 1 ? 255 :
                pattern == 2 ? (uint8_t)i : pattern == 3 ? (uint8_t)(1u << (i & 7u)) :
                pattern == 4 ? (i & 1u ? 0xaau : 0x55u) : (uint8_t)(random >> 24);
        }
        uint16_t expected = crc16(crc_vector, sizeof(crc_vector));
        for(unsigned polled = 0; polled < 2u; ++polled) {
            reset_model();
            card_content = vector_content;
            open_stream(false);
            uint32_t lba = 700u;
            enum kui_sci_stream_result r;
            const struct kui_sci_stream_stats *st = kui_sci_stream_stats();
            struct kui_sci_stream_stats before = *st;
            assert(kui_sci_stream_fetch(lba, LIMIT, polled != 0) == KUI_SCI_STREAM_OK);
            assert(kui_sci_stream_busy() == !polled);
            assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
            const uint8_t *p = kui_sci_stream_take(lba, &r);
            assert(p && r == KUI_SCI_STREAM_OK && !memcmp(p, crc_vector, sizeof(crc_vector)));
            assert(crc16(p, sizeof(crc_vector)) == expected && st->crc_errors == before.crc_errors);
            /* A payload bit error is rejected, including for zero and
             * all-one blocks; a clean reread then passes independently. */
            card.corrupt_lba = ++lba; card.corrupt_count = 1;
            assert(kui_sci_stream_fetch(lba, LIMIT, polled != 0) == KUI_SCI_STREAM_OK);
            assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
            assert(!kui_sci_stream_take(lba, &r) && r == KUI_SCI_STREAM_CRC);
            assert(st->crc_errors == before.crc_errors + 1u);
            assert(kui_sci_stream_fetch(lba, LIMIT, polled != 0) == KUI_SCI_STREAM_OK);
            assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
            p = kui_sci_stream_take(lba, &r);
            assert(p && r == KUI_SCI_STREAM_OK && !memcmp(p, crc_vector, sizeof(crc_vector)));
            /* Corrupt only the stored high CRC byte of the received
             * area; neither DMA nor polled reception may accept it. */
            assert(kui_sci_stream_fetch(++lba, LIMIT, polled != 0) == KUI_SCI_STREAM_OK);
            assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
            areas[0][512] ^= 1u; areas[1][512] ^= 1u;
            assert(!kui_sci_stream_take(lba, &r) && r == KUI_SCI_STREAM_CRC);
            assert(st->crc_errors == before.crc_errors + 2u);
            assert(kui_sci_stream_stop() == KUI_SCI_STREAM_OK);
            check_restored(); check_channel_restored();
        }
    }
    /* Keep repair's CRC-syndrome reconstruction covered with the final
     * nonuniform vector too, rather than only the model's usual pattern. */
    reset_model();
    card_content = vector_content;
    open_stream(false);
    unsigned repaired = kui_sci_stream_stats()->repaired;
    m.overrun_after = 100u;
    assert(kui_sci_stream_fetch(800u, LIMIT, false) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
    enum kui_sci_stream_result r;
    const uint8_t *p = kui_sci_stream_take(800u, &r);
    assert(p && r == KUI_SCI_STREAM_OK && !memcmp(p, crc_vector, sizeof(crc_vector)));
    assert(kui_sci_stream_stats()->repaired == repaired + 1u);
    assert(kui_sci_stream_stop() == KUI_SCI_STREAM_OK);
    card_content = data_at;
}
#ifdef KUI_RETAIL_CE
static void test_ce_token_slices(void) {
    const uint32_t slice = KUI_SCI_STREAM_TOKEN_SLICE, lba = 700u;
    reset_model(); open_stream(false); card.nac_first = 3u * slice + 7u;
    struct kui_sci_stream_stats before = *kui_sci_stream_stats();
    enum kui_sci_stream_result r;
    for(unsigned i = 1; i <= 3u; ++i) {
        kui_sci_stream_token_budget(true);
        assert(kui_sci_stream_fetch(lba, 3u * slice + 8u, false) == KUI_SCI_STREAM_PENDING);
        assert(card.frame == i * slice && card.cmd18 == 1u && !card.cmd12);
        assert(kui_sci_stream_token_pending() && !kui_sci_stream_busy());
        assert(!kui_sci_stream_take(lba, &r) && r == KUI_SCI_STREAM_PENDING);
        /* A second visit in the same service entry has no fresh budget. */
        assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_PENDING);
        assert(card.frame == i * slice && card.cmd18 == 1u);
    }
    kui_sci_stream_token_budget(true);
    assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_OK);
    assert(!kui_sci_stream_token_pending() && kui_sci_stream_busy());
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
    check_block(lba, kui_sci_stream_take(lba, &r));
    assert(kui_sci_stream_stats()->token_errors == before.token_errors);
    assert(kui_sci_stream_stats()->token_bytes - before.token_bytes == 3u * slice + 8u);
    assert(kui_sci_stream_stats()->token_yields - before.token_yields == 3u);
    assert(kui_sci_stream_stop() == KUI_SCI_STREAM_OK);

    /* The original total timeout survives slice boundaries and changing
     * limit arguments on retries; pending slices never expose a payload. */
    reset_model(); open_stream(false); card.nac_first = 4u * slice;
    before = *kui_sci_stream_stats();
    const uint32_t limit = 2u * slice + 9u;
    for(unsigned i = 0; i < 3u; ++i) {
        kui_sci_stream_token_budget(true);
        assert(kui_sci_stream_fetch(lba, i ? UINT32_MAX : limit, false) ==
            (i == 2u ? KUI_SCI_STREAM_TOKEN : KUI_SCI_STREAM_PENDING));
    }
    assert(card.frame == limit && card.cmd18 == 1u && !m.dma_starts);
    assert(kui_sci_stream_stats()->token_errors - before.token_errors == 1u);
    assert(kui_sci_stream_stats()->token_bytes - before.token_bytes == limit);
    assert(!kui_sci_stream_take(lba, &r) && r == KUI_SCI_STREAM_PENDING);
    assert(kui_sci_stream_stop() == KUI_SCI_STREAM_OK && card.cmd12 == 1u);

    reset_model(); open_stream(false); card.nac_first = 4u * slice;
    kui_sci_stream_token_budget(true);
    assert(kui_sci_stream_fetch(lba, LIMIT, false) == KUI_SCI_STREAM_PENDING);
    card.nac_first = 30u;
    kui_sci_stream_token_budget(true);
    assert(kui_sci_stream_fetch(lba + 1u, LIMIT, false) == KUI_SCI_STREAM_OK);
    assert(card.cmd12 == 1u && card.cmd18 == 2u);
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
    check_block(lba + 1u, kui_sci_stream_take(lba + 1u, &r));
    assert(kui_sci_stream_stop() == KUI_SCI_STREAM_OK);
    kui_sci_stream_token_budget(false);
}
static void test_ce_counters(void) {
    /* Count the terminating token byte on both success and rejection. */
    for(unsigned bad = 0; bad < 2; ++bad) {
        reset_model(); open_stream(false);
        struct kui_sci_stream_stats before = *kui_sci_stream_stats();
        card.bad_token_lba = 700; card.bad_token_count = bad;
        card.nac_first = before.token_max + 1u;
        assert(kui_sci_stream_fetch(700, card.nac_first + 1u, true) ==
            (bad ? KUI_SCI_STREAM_TOKEN : KUI_SCI_STREAM_OK));
        const struct kui_sci_stream_stats *st = kui_sci_stream_stats();
        assert(st->token_bytes - before.token_bytes == card.nac_first + 1u);
        assert(st->token_max == card.nac_first + 1u);
        assert(kui_sci_stream_stop() == KUI_SCI_STREAM_OK);
        assert(st->stops - before.stops == 1u && card.cmd12 == 1u);
    }
    /* Synthetic owned, stopped receptions exercise every reported bucket.
     * The CH2/DMAOR values are handoff-time observations, not fault causes. */
    static const uint32_t remaining[] = {0, 128, 129, 385};
    for(unsigned i = 0; i < 4; ++i) {
        reset_model(); open_stream(false); m.stall = true;
        assert(kui_sci_stream_fetch(800, LIMIT, false) == KUI_SCI_STREAM_OK);
        struct kui_sci_stream_stats before = *kui_sci_stream_stats();
        m.dar += 513u - remaining[i]; m.tcr = remaining[i]; m.chcr &= ~3u;
        m.chcr2 = i;
        if(i & 1u) m.dmaor &= ~1u;
        assert(kui_sci_stream_poll(false) == KUI_SCI_STREAM_OVERRUN);
        const struct kui_sci_stream_stats *st = kui_sci_stream_stats();
        for(unsigned b = 0; b < 4; ++b)
            assert(st->incomplete[b] - before.incomplete[b] == (b == i));
        assert(st->incomplete_ch2_active - before.incomplete_ch2_active == (i == 1u));
        assert(st->incomplete_dmaor_bad - before.incomplete_dmaor_bad == (i & 1u));
        assert(kui_sci_stream_stop() == KUI_SCI_STREAM_OK);
        check_restored(); check_channel_restored();
    }
}
#endif
int main(void) {
    test_sequential();
    test_pending_and_kept();
    test_crc_and_token();
    test_overrun_and_stall();
    test_polled_on_request();
    test_deferred_repair();
    test_channel_busy_and_foreign();
    test_unknown_and_gapless();
    test_byte_addressed();
    test_bus_fault();
    test_random_faults();
    test_crc_reference_vectors();
#ifdef KUI_RETAIL_CE
    test_ce_counters();
    test_ce_token_slices();
#endif
    puts("sci stream: ok");
    return 0;
}
