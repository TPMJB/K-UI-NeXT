/* SPDX-License-Identifier: GPL-3.0-only */
/* The background game reader end to end on the host: the resident's GD call
 * sequence and the stream's interrupt, against the card/SCI/DMA model, with
 * every destination byte compared to the ordinary image reader's output. */
#include "retail_async.h"
#include "sci_stream_model.h"
#include "kui/retail_image.h"
#include "kui/retail_loader_layout.h"

#define BEGIN 0x8c010000u
#define END 0x8c200000u
#define PARAM (BEGIN + 0x100u)
#define STATUS (BEGIN + 0x200u)
#define OUTPUT (BEGIN + 0x1000u)
#define GAME_VBR 0x8c0f0000u
#define IPRB 0xffd00008u
#define IPRC 0xffd0000cu
#define R kui_retail_async_region

static uint8_t ram[END - BEGIN];
static uint8_t image_card[2048u * 512u];
static uint8_t expected[64u * 2352u];
static struct kui_retail_manifest manifest;
static struct kui_retail_image reference;
static unsigned checks;
#define CHECK(test) do { ++checks; assert(test); } while(0)

/* ---- Resident and CPU hooks ---- */
static struct {
    uint32_t vbr;
    uint16_t iprb, iprc;
    unsigned acquires, writes, vbr_sets;
    bool frozen; /* a cancelled read must write nothing more */
    bool busy_bus; /* the SCI cannot be claimed */
} hw;
uint32_t kui_retail_async_test_vbr(void) { return hw.vbr; }
void kui_retail_async_test_set_vbr(uint32_t value) { hw.vbr = value; ++hw.vbr_sets; }
uint16_t kui_retail_async_test_read16(uint32_t address) {
    if(address == IPRB) return hw.iprb;
    assert(address == IPRC);
    return hw.iprc;
}
void kui_retail_async_test_write16(uint32_t address, uint16_t value) {
    if(address == IPRB) hw.iprb = value;
    else {assert(address == IPRC); hw.iprc = value;}
}
enum kui_loader_sd_result kui_sci_sd_acquire(void) {
    ++hw.acquires;
    return hw.busy_bus ? KUI_LOADER_SD_UNSUPPORTED : KUI_LOADER_SD_OK;
}
const uint32_t kui_retail_vector_forward[4] = {0x11111111u, 0x22222222u, 0x33333333u, 0};
const uint32_t kui_retail_vector_interrupt[13] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 0xff000028u, 0, 12, 13};
static uint32_t our_vbr(void) { return (uint32_t)(uintptr_t)&R - 0x100u; }

/* ---- Image: the cursor test's track bytes, mapped onto the model card ---- */
static uint8_t source(uint32_t track, uint32_t file_byte) {
    uint32_t sector = file_byte / 2352u, inside = file_byte % 2352u;
    if(track != 1) {
        if(inside == 0 || inside == 11) return 0;
        if(inside < 11) return 255;
        if(inside == 15) return 1;
    }
    return (uint8_t)(track * 91u + sector * 53u + inside * 11u + (inside >> 8));
}
static uint8_t content(uint32_t lba, unsigned i) { return lba < 2048u ? image_card[lba * 512u + i] : 0xee; }
static int read_block(void *context, uint32_t lba, uint8_t out[512]) {
    (void)context;
    memcpy(out, image_card + lba * 512u, 512);
    return 0;
}
static void fixture(unsigned take_max, bool scattered) {
    memset(&manifest, 0, sizeof(manifest));
    memset(image_card, 0xf3, sizeof(image_card));
    manifest.card_sectors = 2048;
    manifest.partition_start = 50; manifest.partition_end = 2000;
    manifest.track_count = 4;
    manifest.session_lba = 45000; manifest.boot_lba = 45001; manifest.boot_bytes = 4567;
    strcpy(manifest.title, "Async test"); strcpy(manifest.bootfile, "1ST_READ.BIN");
    static const uint32_t starts[4] = {0, 3, 45000, 45300};
    static const uint32_t ends[4] = {3, 5, 45300, 45302};
    uint32_t used = 0;
    for(unsigned i = 0; i < 4; ++i) {
        struct kui_retail_track *t = &manifest.tracks[i];
        *t = (struct kui_retail_track){.gd={.number=i + 1, .start_lba=starts[i],
            .end_lba=ends[i], .control=i == 1 ? 0u : 4u}, .first_extent=manifest.extent_count};
        uint32_t bytes = (t->gd.end_lba - t->gd.start_lba) * 2352u;
        uint32_t blocks = (bytes + 511u) / 512u;
        for(uint32_t n = 0; n < blocks;) {
            uint32_t take = blocks - n > take_max ? take_max : blocks - n;
            uint32_t index = manifest.extent_count;
            uint32_t physical = scattered ? 100u + (index & 1u ? 900u : 0u) + (index >> 1) * (take_max + 1u) :
                100u + used;
            CHECK(index < KUI_RETAIL_ASYNC_EXTENTS && physical + take <= 1990u);
            manifest.extents[manifest.extent_count++] = (struct kui_retail_extent){n, physical, take};
            ++t->extent_count;
            for(uint32_t p = 0; p < take * 512u; ++p)
                if(n * 512u + p < bytes) image_card[physical * 512u + p] = source(i, n * 512u + p);
            n += take; used += take;
        }
    }
    CHECK(kui_retail_manifest_validate(&manifest) == KUI_GAME_OK);
    CHECK(kui_retail_image_init(&reference, &manifest, read_block, NULL) == KUI_GAME_OK);
    /* The launch map selecting this reader round-trips (SCI, <= 32 extents). */
    static uint8_t wire[KUI_RETAIL_IMAGE_WIRE_BYTES];
    static struct kui_retail_manifest decoded;
    struct kui_retail_manifest async = manifest;
    async.storage_transport = KUI_STORAGE_SCI; async.reader = KUI_RETAIL_READER_ASYNC;
    CHECK(kui_retail_manifest_encode(&async, wire) == KUI_GAME_OK && wire[264] == 1);
    CHECK(kui_retail_manifest_decode(wire, &decoded) == KUI_GAME_OK);
    CHECK(!memcmp(&decoded, &async, sizeof(async)));
}

/* ---- Guest memory and GD calls, as the resident makes them ---- */
static uint8_t *map(void *unused, uint32_t address, uint32_t bytes, int writing) {
    (void)unused;
    CHECK(address >= BEGIN && address < END && bytes <= END - address);
    if(writing == KUI_RETAIL_MAP_VALIDATE) return ram + sizeof(ram);
    if(writing == 1 && address >= OUTPUT) {CHECK(!hw.frozen); ++hw.writes;}
    return ram + (address - BEGIN);
}
static int check(void *unused, uint32_t lba, uint32_t count, uint32_t bytes) {
    (void)unused;
    return kui_retail_image_check_validated(&manifest, lba, count,
        bytes == 2352u ? KUI_GAME_SECTOR_RAW : KUI_GAME_SECTOR_MODE1) == KUI_GAME_OK ? 0 : -1;
}
static void put(uint32_t a, uint32_t n) { for(unsigned i = 0; i < 4; ++i) ram[a - BEGIN + i] = (uint8_t)(n >> (i * 8)); }
static uint32_t get(uint32_t a) {
    uint32_t n = 0;
    for(unsigned i = 0; i < 4; ++i) n |= (uint32_t)ram[a - BEGIN + i] << (i * 8);
    return n;
}
static int32_t gd(uint32_t function, uint32_t r4, uint32_t r5) {
    kui_retail_async_call(function);
    int32_t result = kui_retail_gd_dispatch(&R.shared.service, r4, r5, 0, function);
    kui_retail_async_after(function, result);
    return result;
}
static bool hooked(void) { return hw.vbr == our_vbr(); }
/* Time passing: a block in flight arrives. A reception otherwise takes
 * RECEIVE_POLLS register reads, so a GD call that only polls finds it still
 * arriving, while a waiting EXEC sees it finish. */
#define RECEIVE_POLLS 1000u
static void elapse(void) { if(m.rx) m.rx_delay = 0; }
/* The hardware's side: each finished block raises the stream's interrupt
 * while our vectors are installed. Every third entry comes early (still
 * receiving), which must be harmless. */
static unsigned spurious;
static unsigned interrupts(unsigned limit) {
    unsigned n = 0;
    while(n < limit && hooked() && kui_sci_stream_busy()) {
        if(++spurious % 3u) elapse();
        CHECK(kui_retail_async_irq() == 0);
        ++n;
    }
    return n;
}

static void setup(uint32_t game_vbr, uint16_t iprc, unsigned take_max, bool scattered) {
    fixture(take_max, scattered);
    memset(&card, 0, sizeof(card));
    card.nac = 1; card.nac_first = 30; card.busy = 4; card.blocks = 2048; card.high_capacity = true;
    card_content = content;
    memset(&m, 0, sizeof(m));
    m.smr = 0x80u; m.brr = 0; m.scr = 0x30u; m.ssr = 0x84u; m.sptr = 0; m.cs_high = true; m.healthy = true;
    m.sar = 0x11111111u; m.dar = 0x22222222u; m.tcr = 0x33u; m.chcr = 0; m.dmaor = 0x8201u;
    m.delay = RECEIVE_POLLS;
    memset(&R, 0, sizeof(R)); /* _start clears resident BSS */
    m.areas[0] = R.area0; m.areas[1] = R.area1;
    struct kui_loader_sd *sd = &R.shared.card.device.sd;
    R.shared.card.transport = KUI_STORAGE_SCI;
    sd->bus.select = bus_select; sd->bus.transfer = bus_transfer;
    sd->high_capacity = true; sd->ready = true; sd->blocks = 2048;
    kui_retail_async_init(&manifest);
    const struct kui_gd_ops ops = {NULL, map, check, NULL};
    kui_retail_gd_init_manifest_validated(&R.shared.service, manifest.tracks, manifest.track_count,
        &ops, BEGIN, END);
    memset(&hw, 0, sizeof(hw));
    hw.vbr = game_vbr; hw.iprb = 0x5a0f; hw.iprc = iprc;
    memset(ram, 0xa5, sizeof(ram));
    CHECK(R.vector100[3] == (uint32_t)(uintptr_t)&R.engine.forward[0]);
    CHECK(R.vector400[3] == (uint32_t)(uintptr_t)&R.engine.forward[1]);
    CHECK(R.vector600[11] == (uint32_t)(uintptr_t)&R.engine.forward[2] && R.vector600[9] == 0xff000028u);
}
static void mode(uint32_t bytes) {
    put(PARAM, 0); put(PARAM + 4, bytes == 2048 ? 0x2000 : 0x1000);
    put(PARAM + 8, bytes == 2048 ? 1024 : 0); put(PARAM + 12, bytes);
    CHECK(gd(KUI_GD_DATATYPE, PARAM, 0) == 0);
}
static int32_t request(uint32_t lba, uint32_t count, uint32_t destination) {
    put(PARAM, lba + 150); put(PARAM + 4, count); put(PARAM + 8, destination); put(PARAM + 12, 0);
    int32_t token = gd(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM);
    CHECK(token > 0);
    return token;
}
static void compare(uint32_t lba, uint32_t count, uint32_t destination) {
    enum kui_game_sector_format format = R.shared.service.sector_bytes == 2352u ?
        KUI_GAME_SECTOR_RAW : KUI_GAME_SECTOR_MODE1;
    size_t bytes = (size_t)count * R.shared.service.sector_bytes;
    CHECK(kui_retail_image_read(&reference, lba, count, format, expected, sizeof(expected)) == KUI_GAME_OK);
    CHECK(!memcmp(ram + (destination - BEGIN), expected, bytes));
    CHECK(ram[destination - BEGIN + bytes] == 0xa5);
}
/* Drive a read to completion as a game does: CHECK in a loop, an EXEC now
 * and then, and whatever interrupts the hardware delivers in between. */
static int32_t finish(int32_t token, bool irqs, unsigned exec_every) {
    for(unsigned round = 0; round < 100000u; ++round) {
        if(irqs) (void)interrupts(1u + round % 3u);
        else if(round % 2u) elapse(); /* the game's next call comes later */
        if(exec_every && round % exec_every == 0) CHECK(gd(KUI_GD_EXEC, 0, 0) == 0);
        int32_t status = gd(KUI_GD_CHECK, (uint32_t)token, STATUS);
        if(status != KUI_GD_PROCESSING) return status;
    }
    CHECK(!"read never finished");
    return -1;
}
static void read_and_compare(uint32_t lba, uint32_t count, bool irqs, unsigned exec_every) {
    memset(ram + (OUTPUT - BEGIN), 0xa5, 64u * 2352u + 16u);
    int32_t token = request(lba, count, OUTPUT);
    CHECK(finish(token, irqs, exec_every) == KUI_GD_COMPLETED);
    CHECK(get(STATUS + 8) == count * R.shared.service.sector_bytes && get(STATUS + 4) == 0);
    compare(lba, count, OUTPUT);
}

static void test_interrupt_reads(void) {
    setup(GAME_VBR, 0x0000, 48, true);
    const struct kui_retail_async_stats *st = &R.engine.stats;
    /* Sequential requests: the stream continues across them (a block two
     * requests share is kept, not read again) and restarts only where an
     * extent ends: 60 Mode1 sectors span 276 blocks, six 48-block extents. */
    for(uint32_t lba = 45000; lba < 45060; lba += 6) read_and_compare(lba, 6, true, 0);
    CHECK(st->irq_blocks > 250u && st->call_blocks < 10u && st->hooks == 10 && !st->waits && !st->failures);
    CHECK(kui_sci_stream_stats()->kept >= 3 && kui_sci_stream_stats()->blocks == 276);
    CHECK(card.cmd18 == 6 && card.cmd12 == 5);
    /* Idle between requests: the game's vectors and levels are back. */
    CHECK(hw.vbr == GAME_VBR && hw.iprb == 0x5a0f && hw.iprc == 0 && hw.acquires == 1);
    /* Extent changes inside a request restart the stream there. */
    unsigned before = card.cmd18;
    read_and_compare(45100, 40, true, 5);
    CHECK(card.cmd18 > before);
    /* A request in the first track, then raw sectors. */
    read_and_compare(0, 3, true, 0);
    mode(2352);
    read_and_compare(45200, 17, true, 2);
    read_and_compare(45298, 4, true, 0); /* across into the last track */
    mode(2048);
    read_and_compare(45001, 64, true, 0);
    CHECK(R.engine.stats.forwarded == 0);
    /* Not ours while idle: the event goes to the game's vector. */
    CHECK(kui_retail_async_irq() == 1 && R.engine.stats.forwarded == 1);
}
static void test_levels_while_streaming(void) {
    /* The game's own DMAC level is kept and used for the SCI too. */
    setup(GAME_VBR, 0x0500, 2000, false);
    int32_t token = request(45010, 20, OUTPUT);
    CHECK(hooked() && R.engine.forward[0] == GAME_VBR + 0x100u &&
          R.engine.forward[1] == GAME_VBR + 0x400u && R.engine.forward[2] == GAME_VBR + 0x600u);
    CHECK(hw.iprc == 0x0500 && hw.iprb == 0x5a5f && m.armed_chcr == 0x4915u);
    CHECK(finish(token, true, 0) == KUI_GD_COMPLETED);
    compare(45010, 20, OUTPUT);
    CHECK(hw.vbr == GAME_VBR && hw.iprc == 0x0500 && hw.iprb == 0x5a0f);
    /* No level of its own: the lowest one, removed again afterwards. */
    setup(GAME_VBR, 0x1034, 2000, false);
    token = request(45010, 3, OUTPUT);
    CHECK(hw.iprc == 0x1134 && hw.iprb == 0x5a1f);
    CHECK(finish(token, true, 0) == KUI_GD_COMPLETED);
    CHECK(hw.iprc == 0x1034 && hw.iprb == 0x5a0f);
}
static void test_boot_vbr_hooked(void) {
    /* DOA2 keeps the bootstrap's VBR for good: it is hooked like any other,
     * and events still pass to the bootstrap's vectors. */
    setup(KUI_RETAIL_BOOT_VBR, 0, 49, true);
    int32_t token = request(45000, 30, OUTPUT);
    CHECK(hooked() && R.engine.forward[2] == KUI_RETAIL_BOOT_VBR + 0x600u);
    CHECK(finish(token, true, 1) == KUI_GD_COMPLETED);
    compare(45000, 30, OUTPUT);
    const struct kui_retail_async_stats *st = &R.engine.stats;
    CHECK(st->hooks && st->boot_vbr == st->hooks && st->irq_blocks > st->call_blocks);
    CHECK(hw.vbr == KUI_RETAIL_BOOT_VBR && hw.iprc == 0 && m.armed_chcr == 0x4915u);
    /* Without EXEC calls, CHECK alone still completes a read. */
    read_and_compare(45030, 9, false, 0);
}
static void test_dead_interrupt(void) {
    /* Hooked, but the interrupt never arrives (masked by the game): the
     * second EXEC waits, so the read still completes at the ordinary pace. */
    setup(GAME_VBR, 0, 2000, false);
    int32_t token = request(45000, 24, OUTPUT);
    unsigned waits = R.engine.stats.waits;
    CHECK(gd(KUI_GD_EXEC, 0, 0) == 0 && R.engine.stats.waits == waits);
    CHECK(gd(KUI_GD_EXEC, 0, 0) == 0 && R.engine.stats.waits == waits + 1);
    CHECK(finish(token, false, 1) == KUI_GD_COMPLETED);
    compare(45000, 24, OUTPUT);
    CHECK(!R.engine.stats.irq_blocks);
}
static void test_faults_retried(void) {
    setup(GAME_VBR, 0, 50, true);
    card.corrupt_lba = manifest.extents[3].card_lba + 1; card.corrupt_count = 2;
    read_and_compare(45000, 40, true, 3);
    CHECK(kui_sci_stream_stats()->crc_errors == 2);
    /* Held off the bus mid-block: resumed in place, the byte rebuilt. */
    m.overrun_after = 200;
    read_and_compare(45040, 10, true, 0);
    CHECK(kui_sci_stream_stats()->repaired == 1 && !kui_sci_stream_stats()->overruns);
    card.bad_token_lba = 0; card.bad_token_count = 0;
    /* A busy channel: programmed reception for those blocks. */
    m.chcr = 1;
    read_and_compare(45050, 8, true, 0);
    CHECK(kui_sci_stream_stats()->polled);
    m.chcr = 0;
    CHECK(R.engine.stats.failures >= 2 && R.engine.stats.max_retries >= 1);
}
static void test_cancel_writes_nothing_more(void) {
    for(unsigned how = 0; how < 3; ++how) {
        setup(GAME_VBR, 0, 2000, false);
        int32_t token = request(45000, 50, OUTPUT);
        (void)interrupts(12);
        CHECK(!R.shared.service.pending || R.shared.service.completed_bytes < 50u * 2048u);
        if(how == 0) CHECK(gd(KUI_GD_ABORT, (uint32_t)token, 0) == 0);
        else if(how == 1) CHECK(gd(KUI_GD_INIT, 0, 0) == 0);
        else CHECK(gd(KUI_GD_RESET, 0, 0) == 0);
        hw.frozen = true;
        (void)interrupts(50);
        for(unsigned i = 0; i < 20; ++i) {(void)gd(KUI_GD_EXEC, 0, 0); (void)gd(KUI_GD_CHECK, (uint32_t)token, STATUS);}
        CHECK(!kui_sci_stream_busy() && hw.vbr == GAME_VBR);
        hw.frozen = false;
        read_and_compare(45020, 10, true, 0);
    }
}
static void test_game_moves_vbr(void) {
    setup(GAME_VBR, 0, 2000, false);
    int32_t token = request(45000, 40, OUTPUT);
    (void)interrupts(5);
    hw.vbr = 0x8c0e0000u; /* the game installs other vectors mid-read */
    CHECK(interrupts(5) == 0);
    CHECK(gd(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_PROCESSING);
    CHECK(hooked() && R.engine.stats.vbr_changes == 1 && R.engine.forward[2] == 0x8c0e0600u);
    CHECK(finish(token, true, 0) == KUI_GD_COMPLETED);
    compare(45000, 40, OUTPUT);
    CHECK(hw.vbr == 0x8c0e0000u);
}
static void test_read_fails_after_retries(void) {
    setup(GAME_VBR, 0, 2000, false);
    card.corrupt_lba = manifest.extents[2].card_lba + 3; card.corrupt_count = 1000;
    int32_t token = request(45000, 20, OUTPUT);
    CHECK(finish(token, true, 1) == KUI_GD_FAILED);
    CHECK(get(STATUS + 4) == KUI_GD_ERROR_IO && R.shared.service.error == KUI_GD_ERROR_IO);
    CHECK(R.engine.stats.max_retries == 9 && hw.vbr == GAME_VBR);
    /* A bus that cannot be claimed fails the read with nothing delivered,
     * even after an earlier read completed. */
    setup(GAME_VBR, 0, 2000, false);
    hw.busy_bus = true;
    token = request(45000, 5, OUTPUT);
    CHECK(gd(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_FAILED);
    CHECK(get(STATUS + 4) == KUI_GD_ERROR_IO && get(STATUS + 8) == 0 && hw.vbr == GAME_VBR);
    setup(GAME_VBR, 0, 2000, false);
    read_and_compare(45000, 7, true, 0);
    R.engine.opened = 0; hw.busy_bus = true;
    token = request(45010, 5, OUTPUT);
    CHECK(gd(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_FAILED && get(STATUS + 8) == 0);
    /* A latched bus fault ends the read at once. */
    setup(GAME_VBR, 0, 2000, false);
    token = request(45000, 20, OUTPUT);
    (void)interrupts(3);
    m.healthy = false;
    CHECK(finish(token, true, 1) == KUI_GD_FAILED && R.shared.service.error == KUI_GD_ERROR_IO);
}
static void test_stress(void) {
    setup(GAME_VBR, 0, 48, true);
    srand(4242);
    for(unsigned round = 0; round < 300; ++round) {
        uint32_t count = 1u + (uint32_t)rand() % 30u;
        uint32_t lba = 45000u + (uint32_t)rand() % (300u - count);
        int fault = rand() % 8;
        if(fault == 0) {card.corrupt_lba = 100u + (uint32_t)rand() % 1800u; card.corrupt_count = 1;}
        if(fault == 1) m.overrun_after = 1u + (unsigned)rand() % 512u;
        if(fault == 2) m.chcr = 1;
        card.nac = 1u + (unsigned)rand() % 3u;
        bool irqs = rand() % 4 != 0;
        unsigned exec_every = (unsigned)rand() % 4u;
        if(!irqs && !exec_every) exec_every = 1;
        read_and_compare(lba, count, irqs, exec_every);
        m.chcr = 0; m.overrun_after = 0; card.corrupt_count = 0;
    }
    const struct kui_retail_async_stats *st = &R.engine.stats;
    const struct kui_sci_stream_stats *ss = kui_sci_stream_stats();
    printf("stress: irq %u call %u waits %u irqs %u failures %u | dma %u polled %u starts %u kept %u repaired %u\n",
        st->irq_blocks, st->call_blocks, st->waits, st->irqs, st->failures,
        ss->blocks, ss->polled, ss->starts, ss->kept, ss->repaired);
}

int main(void) {
    test_interrupt_reads();
    test_levels_while_streaming();
    test_boot_vbr_hooked();
    test_dead_interrupt();
    test_faults_retried();
    test_cancel_writes_nothing_more();
    test_game_moves_vbr();
    test_read_fails_after_retries();
    test_stress();
    printf("retail async reader: %u checks passed\n", checks);
    return 0;
}
