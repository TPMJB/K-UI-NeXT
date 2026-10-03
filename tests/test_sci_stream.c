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
 * after it before copying, refetch after any failure. */
static unsigned drive(uint32_t first, uint32_t count, uint8_t *out, bool overlap) {
    unsigned failures = 0;
    uint32_t want = first;
    while(want < first + count) {
        enum kui_sci_stream_result r;
        assert(failures < 20000u);
        if(kui_sci_stream_busy()) {
            r = kui_sci_stream_wait();
            if(r != KUI_SCI_STREAM_OK) {++failures; continue;}
        }
        const uint8_t *p = kui_sci_stream_take(want, &r);
        if(r == KUI_SCI_STREAM_CRC) {
            ++failures;
            if(kui_sci_stream_fetch(want, LIMIT) != KUI_SCI_STREAM_OK) ++failures;
            continue;
        }
        if(p) {
            if(overlap && want + 1u < first + count &&
               kui_sci_stream_fetch(want + 1u, LIMIT) != KUI_SCI_STREAM_OK) ++failures;
            memcpy(out + (size_t)(want - first) * 512u, p, 512);
            ++want;
            continue;
        }
        assert(r == KUI_SCI_STREAM_PENDING);
        if(kui_sci_stream_fetch(want, LIMIT) != KUI_SCI_STREAM_OK) ++failures;
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
    assert(st->starts - before.starts == 1 && st->continued - before.continued == 63);
    assert(st->blocks - before.blocks == 64 && st->stops == before.stops && !st->polled);
    assert(card.cmd18 == 1 && !card.cmd12 && m.dma_starts == 64 && m.module_resets == 64);
    assert(m.purges == 64 && !m.irq_starts && m.armed_chcr == 0x4911u);
    assert(st->max_token_bytes == 40);
    check_restored(); check_channel_restored();
    /* The stream continues where it paused, without another CMD18. */
    assert(drive(164, 10, out, false) == 0);
    check_range(164, 10, out);
    assert(card.cmd18 == 1 && !m.irq_starts && m.armed_chcr == 0x4911u);
    /* Elsewhere: CMD12 with its busy interval, then a new CMD18. */
    assert(drive(5000, 3, out, true) == 0);
    check_range(5000, 3, out);
    assert(card.cmd18 == 2 && card.cmd12 == 1 && !card.idle_cmd12 && st->stops == 1);
    assert(kui_sci_stream_stop() == KUI_SCI_STREAM_OK);
    assert(card.cmd12 == 2 && !card.streaming && m.cs_high);
    check_restored(); check_channel_restored();
}
static void test_pending_and_kept(void) {
    reset_model();
    open_stream(false);
    m.delay = 3;
    enum kui_sci_stream_result r;
    assert(kui_sci_stream_fetch(7, LIMIT) == KUI_SCI_STREAM_OK && kui_sci_stream_busy());
    assert(!kui_sci_stream_take(7, &r) && r == KUI_SCI_STREAM_PENDING);
    assert(kui_sci_stream_poll() == KUI_SCI_STREAM_PENDING);
    assert(kui_sci_stream_fetch(8, LIMIT) == KUI_SCI_STREAM_BUSY);
    unsigned polls = 1;
    while((r = kui_sci_stream_poll()) == KUI_SCI_STREAM_PENDING) ++polls;
    assert(r == KUI_SCI_STREAM_OK && !kui_sci_stream_busy() && polls <= 4);
    const uint8_t *p = kui_sci_stream_take(7, &r);
    check_block(7, p);
    /* The next block arrives in the other area while block 7 stays usable;
     * a request that starts in block 7 again gets it without a second read. */
    assert(kui_sci_stream_fetch(8, LIMIT) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
    const uint8_t *again = kui_sci_stream_take(7, &r);
    assert(again == p && r == KUI_SCI_STREAM_OK);
    check_block(7, again);
    assert(kui_sci_stream_stats()->kept);
    /* Re-fetching the block after the kept one never overwrites the kept
     * area, even when the other area still holds an untaken block. */
    assert(kui_sci_stream_fetch(9, LIMIT) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
    check_block(7, again);
    check_block(9, kui_sci_stream_take(9, &r));
    check_block(9, kui_sci_stream_take(9, &r));
    assert(!kui_sci_stream_take(7, &r) && r == KUI_SCI_STREAM_PENDING);
    kui_sci_stream_discard();
    assert(!kui_sci_stream_take(9, &r) && r == KUI_SCI_STREAM_PENDING);
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
            assert(kui_sci_stream_fetch(lba, LIMIT) == KUI_SCI_STREAM_OK);
            assert(kui_sci_stream_poll() == KUI_SCI_STREAM_PENDING && kui_sci_stream_busy());
            assert(m.chcr & 1u && m.dar == AREA_BASE + (m.dar & 0x1000u) + positions[i] + 2u);
            assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK && m.module_resets == resets + 2u);
            check_block(lba, kui_sci_stream_take(lba, &r));
            check_restored(); check_channel_restored();
        }
    }
    m.late_take = true;
    assert(st->repaired - before.repaired == 2u * count && st->overruns == before.overruns);
    assert(card.cmd18 == 1 && !card.cmd12 && st->crc_errors == before.crc_errors);
    /* Too late to rebuild (a CRC byte lost), or a second loss in the same
     * block: the stream restarts at that block. */
    static const unsigned late[] = {511, 512};
    for(unsigned i = 0; i < 2; ++i, ++lba) {
        m.overrun_after = late[i];
        assert(kui_sci_stream_fetch(lba, LIMIT) == KUI_SCI_STREAM_OK);
        assert(kui_sci_stream_poll() == KUI_SCI_STREAM_OVERRUN && !kui_sci_stream_busy());
        check_restored(); check_channel_restored();
        assert(kui_sci_stream_fetch(lba, LIMIT) == KUI_SCI_STREAM_OK);
        assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
        check_block(lba, kui_sci_stream_take(lba, &r));
    }
    m.overrun_after = 100; m.overrun_again = 200;
    assert(kui_sci_stream_fetch(lba, LIMIT) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_poll() == KUI_SCI_STREAM_PENDING);
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OVERRUN);
    assert(st->overruns - before.overruns == 3 && card.cmd12 == 2);
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
        assert(kui_sci_stream_fetch(lba, LIMIT) == KUI_SCI_STREAM_OK);
        assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
        assert(!kui_sci_stream_take(lba, &r) && r == KUI_SCI_STREAM_CRC);
        assert(drive(lba, 3, out, true) == 0);
        check_range(lba, 3, out);
        lba += 3;
        if(fault) break;
        unsigned still = st->repaired;
        m.overrun_after = 40;
        assert(kui_sci_stream_fetch(lba, LIMIT) == KUI_SCI_STREAM_OK);
        assert(kui_sci_stream_poll() == KUI_SCI_STREAM_PENDING && st->repaired == still + 1u);
        assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
        check_block(lba, kui_sci_stream_take(lba, &r));
        lba += 1;
    }
    unsigned repaired = st->repaired, overruns = st->overruns;
    m.overrun_after = 40;
    assert(kui_sci_stream_fetch(lba, LIMIT) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_poll() == KUI_SCI_STREAM_OVERRUN);
    assert(st->repaired == repaired && st->overruns == overruns + 1u);
    assert(drive(lba, 2, out, true) == 0);
    check_range(lba, 2, out);
    lba += 2;
    open_stream(true); /* the card may still be streaming: CMD12 first */
    m.overrun_after = 40;
    assert(kui_sci_stream_fetch(lba, LIMIT) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_poll() == KUI_SCI_STREAM_PENDING && st->repaired == repaired + 1u);
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OK);
    check_block(lba, kui_sci_stream_take(lba, &r));
    lba += 1;
    /* A receiver that never finishes ends as an overrun after the bound. */
    m.stall = true;
    assert(kui_sci_stream_fetch(lba, LIMIT) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_wait() == KUI_SCI_STREAM_OVERRUN);
    check_restored(); check_channel_restored();
    m.stall = false;
    assert(drive(lba, 5, out, true) == 0);
    check_range(lba, 5, out);
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
    assert(kui_sci_stream_fetch(300, LIMIT) == KUI_SCI_STREAM_OK && !kui_sci_stream_busy());
    assert(st->polled - before.polled == 1 && m.chcr == 1u && !m.dma_starts);
    assert(bus_bytes - bytes_before >= 514u + 40u);
    check_block(300, kui_sci_stream_take(300, &r));
    assert(kui_sci_stream_fetch(301, LIMIT) == KUI_SCI_STREAM_OK);
    check_block(301, kui_sci_stream_take(301, &r));
    assert(card.cmd18 == 1 && st->continued - before.continued == 1);
    m.chcr = 0;
    /* The channel taken over mid-block: the SCI is still handed back. */
    m.foreign_during_rx = true;
    assert(kui_sci_stream_fetch(302, LIMIT) == KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_poll() == KUI_SCI_STREAM_BUSY);
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
        if(fault == 1) m.overrun_after = 1u + (unsigned)rand() % 512u;
        if(fault == 2) {card.bad_token_lba = lba + (unsigned)rand() % count; card.bad_token_count = 1;}
        if(fault == 3) m.chcr = 1u;
        m.delay = (unsigned)rand() % 4u;
        card.nac = 1u + (unsigned)rand() % 3u;
        (void)drive(lba, count, out, rand() % 4 != 0);
        check_range(lba, count, out);
        if(fault == 3) m.chcr = 0;
        m.overrun_after = 0;
        check_restored();
        if(!(rand() % 5)) lba += 1000u + (unsigned)rand() % 5000u;
        else lba += count;
        if(lba > 90000u) lba = (unsigned)rand() % 1000u;
    }
    const struct kui_sci_stream_stats *st = kui_sci_stream_stats();
    printf("random: blocks %u polled %u starts %u continued %u kept %u overruns %u repaired %u crc %u token %u\n",
        st->blocks, st->polled, st->starts, st->continued, st->kept, st->overruns, st->repaired,
        st->crc_errors, st->token_errors);
    assert(st->crc_errors && st->repaired && st->token_errors && st->polled);
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
    assert(kui_sci_stream_fetch(card.blocks, LIMIT) == KUI_SCI_STREAM_COMMAND);
    m.healthy = false;
    assert(kui_sci_stream_fetch(3, LIMIT) == KUI_SCI_STREAM_RESET);
}

int main(void) {
    test_sequential();
    test_pending_and_kept();
    test_crc_and_token();
    test_overrun_and_stall();
    test_channel_busy_and_foreign();
    test_unknown_and_gapless();
    test_byte_addressed();
    test_bus_fault();
    test_random_faults();
    puts("sci stream: ok");
    return 0;
}
