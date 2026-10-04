/* SPDX-License-Identifier: GPL-3.0-only */
/* Windows CE's background reader on the host: CE's GD driver calls and its
 * interrupt dispatch reaching the reader through CE's handler table, against
 * the card/SCI/DMA model. Ordinary reads (P1 and CE virtual destinations),
 * DMA streams in transfers of any size, PIO streams through read_part, the
 * interrupts CE's driver waits for, and every output byte compared to the
 * ordinary image reader's. */
#include "retail_async.h"
#include "sci_stream_model.h"
#include "kui/retail_image.h"
#include "kui/retail_loader_layout.h"

#define BEGIN 0x8c010000u
#define END 0x8c200000u
#define PARAM (BEGIN + 0x100u)
#define STATUS (BEGIN + 0x200u)
#define PIECE (BEGIN + 0x300u)
#define OUTPUT (BEGIN + 0x1000u)
/* A CE process's virtual window (its MMU on), mapped by the resident as is. */
#define VBASE 0x00100000u
#define VBYTES 0x40000u
#define IPRB 0xffd00008u
#define MMUCR 0xff000010u
/* CE's handler table (KData + 0x404) and its own default handler. */
#define TABLE 0x8c145c04u
#define CE_DEFAULT 0x8c0130a0u
#define R kui_retail_async_region

static uint8_t ram[END - BEGIN], vram[VBYTES];
static uint8_t image_card[2048u * 512u];
static uint8_t expected[64u * 2352u];
static struct kui_retail_manifest manifest;
static struct kui_retail_image reference;
static unsigned checks;
#define CHECK(test) do { ++checks; assert(test); } while(0)

/* ---- Resident, CE and CPU hooks ---- */
static struct {
    uint16_t iprb;
    uint32_t mmucr, table[2]; /* the SCI's ERI and RXI entries */
    unsigned acquires, writes, virtual_writes;
    bool frozen, in_irq;
} hw;
static struct { unsigned drive, dma_end; } ev;
volatile uint32_t kui_retail_ce_kernel[KUI_RETAIL_CE_KERNEL_WORDS] = {0x8c145b40u, 0x8c145b44u, 0x8c145b68u, TABLE};
void kui_retail_ce_isr(void) {}
static uint32_t isr(void) { return (uint32_t)(uintptr_t)kui_retail_ce_isr; }
uint16_t kui_retail_async_test_read16(uint32_t address) { assert(address == IPRB); return hw.iprb; }
void kui_retail_async_test_write16(uint32_t address, uint16_t value) { assert(address == IPRB); hw.iprb = value; }
uint32_t kui_retail_async_test_read32(uint32_t address) {
    if(address == MMUCR) return hw.mmucr;
    assert(address == TABLE + 0x9cu || address == TABLE + 0xa0u);
    return hw.table[(address - TABLE - 0x9cu) / 4u];
}
void kui_retail_async_test_write32(uint32_t address, uint32_t value) {
    assert(!hw.in_irq && (address == TABLE + 0x9cu || address == TABLE + 0xa0u));
    hw.table[(address - TABLE - 0x9cu) / 4u] = value;
}
volatile uint32_t kui_retail_hook_sr;
enum kui_loader_sd_result kui_sci_sd_acquire(void) { ++hw.acquires; return KUI_LOADER_SD_OK; }

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
static void fixture(unsigned take_max) {
    memset(&manifest, 0, sizeof(manifest));
    memset(image_card, 0xf3, sizeof(image_card));
    manifest.card_sectors = 2048;
    manifest.partition_start = 50; manifest.partition_end = 2000;
    manifest.track_count = 4;
    manifest.session_lba = 45000; manifest.boot_lba = 45001; manifest.boot_bytes = 4567;
    strcpy(manifest.title, "CE async test"); strcpy(manifest.bootfile, "0WINCEOS.BIN");
    static const uint32_t starts[4] = {0, 3, 45000, 45300};
    static const uint32_t ends[4] = {3, 5, 45300, 45302};
    /* Small extents scattered across the card (so runs end and the stream
     * restarts); whole tracks in one extent each, back to back. */
    const bool scattered = take_max < 100u;
    uint32_t used = 0;
    for(unsigned i = 0; i < 4; ++i) {
        struct kui_retail_track *t = &manifest.slots[i].track;
        *t = (struct kui_retail_track){.start_lba=starts[i], .end_lba=ends[i],
            .control=i == 1 ? 0u : 4u, .first_extent=(uint16_t)(4u + manifest.extent_count)};
        uint32_t bytes = (t->end_lba - t->start_lba) * 2352u;
        uint32_t blocks = (bytes + 511u) / 512u;
        for(uint32_t n = 0; n < blocks;) {
            uint32_t take = blocks - n > take_max ? take_max : blocks - n;
            uint32_t index = manifest.extent_count;
            uint32_t physical = scattered ? 100u + (index & 1u ? 900u : 0u) + (index >> 1) * (take_max + 1u) :
                100u + used;
            CHECK(4u + index < KUI_RETAIL_ASYNC_SLOTS && physical + take <= 1990u);
            manifest.slots[4u + manifest.extent_count++].extent = (struct kui_retail_extent){n, physical, take};
            ++t->extent_count;
            for(uint32_t p = 0; p < take * 512u; ++p)
                if(n * 512u + p < bytes) image_card[physical * 512u + p] = source(i, n * 512u + p);
            n += take; used += take;
        }
    }
    CHECK(kui_retail_manifest_validate(&manifest) == KUI_GAME_OK);
    CHECK(kui_retail_image_init(&reference, &manifest, read_block, NULL) == KUI_GAME_OK);
}

/* ---- Guest memory and GD calls, as the resident makes them ---- */
static uint8_t *map(void *unused, uint32_t address, uint32_t bytes, int writing) {
    (void)unused;
    if(address < VBASE + VBYTES && address >= VBASE) {
        /* CE's virtual memory: only in CE's own calls, never the interrupt. */
        CHECK(!hw.in_irq && bytes <= VBASE + VBYTES - address);
        if(writing == KUI_RETAIL_MAP_VALIDATE) return vram + sizeof(vram);
        if(writing == 1) {CHECK(!hw.frozen); ++hw.virtual_writes;}
        return vram + (address - VBASE);
    }
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
/* The interrupts raised, as the resident hands them to CE (ce_events). */
static void take_events(void) {
    uint32_t raised = R.shared.service.interrupts;
    R.shared.service.interrupts = 0;
    if(raised & KUI_RETAIL_GD_IRQ_DRIVE) ++ev.drive;
    if(raised & KUI_RETAIL_GD_IRQ_DMA_END) ++ev.dma_end;
}
static int32_t gd(uint32_t function, uint32_t r4, uint32_t r5) {
    kui_retail_async_call(function);
    int32_t result = kui_retail_gd_dispatch(&R.shared.service, r4, r5, 0, function);
    kui_retail_async_after(function, result);
    take_events();
    return result;
}
static bool level_up(void) { return (hw.iprb & 0x00f0u) == 0x0010u; }
static bool installed(void) { return hw.table[0] == isr() && hw.table[1] == isr(); }
/* Time passing: a block in flight arrives (RECEIVE_POLLS register reads
 * otherwise). */
#define RECEIVE_POLLS 1000u
static void elapse(void) { if(m.rx) m.rx_delay = 0; }
/* CE's dispatch: each finished block raises the SCI's ERI, which reaches the
 * reader through CE's table while the SCI's level is up. Every third comes
 * early (still receiving), which must be harmless. */
static unsigned spurious;
static unsigned interrupts(unsigned limit) {
    unsigned n = 0;
    while(n < limit && level_up() && installed() && kui_sci_stream_busy()) {
        if(++spurious % 3u) elapse();
        hw.in_irq = true;
        CHECK(kui_retail_async_irq() == 0);
        hw.in_irq = false;
        take_events();
        ++n;
    }
    return n;
}

static void setup(unsigned take_max, bool mmu) {
    fixture(take_max);
    manifest.reader = KUI_RETAIL_READER_ASYNC;
    memset(&card, 0, sizeof(card));
    card.nac = 1; card.nac_first = 30; card.busy = 4; card.blocks = 2048; card.high_capacity = true;
    card_content = content;
    memset(&m, 0, sizeof(m));
    m.smr = 0x80u; m.brr = 0; m.scr = 0x30u; m.ssr = 0x84u; m.sptr = 0; m.cs_high = true; m.healthy = true;
    m.sar = 0x11111111u; m.dar = 0x22222222u; m.tcr = 0x33u; m.chcr = 0; m.dmaor = 0x8201u;
    m.delay = RECEIVE_POLLS; m.late_take = true;
    memset(&R, 0, sizeof(R)); /* _start clears resident BSS */
    m.areas[0] = R.area0; m.areas[1] = R.area1;
    struct kui_loader_sd *sd = &R.shared.card.device.sd;
    R.shared.card.transport = KUI_STORAGE_SCI;
    sd->bus.select = bus_select; sd->bus.transfer = bus_transfer;
    sd->high_capacity = true; sd->ready = true; sd->blocks = 2048;
    kui_retail_async_init(&manifest);
    const struct kui_gd_ops ops = {NULL, map, check, NULL};
    kui_retail_gd_init_validated(&R.shared.service, manifest.slots, manifest.track_count,
        &ops, BEGIN, END);
    R.shared.service.read_part = kui_retail_async_read_part;
    memset(&hw, 0, sizeof(hw));
    memset(&ev, 0, sizeof(ev));
    hw.iprb = 0x5a0f; hw.mmucr = mmu ? 0x105u : 0; hw.table[0] = hw.table[1] = CE_DEFAULT;
    memset(ram, 0xa5, sizeof(ram));
    memset(vram, 0xa5, sizeof(vram));
    kui_retail_hook_sr = 0;
}
static void reference_read(uint32_t lba, uint32_t count) {
    enum kui_game_sector_format format = R.shared.service.sector_bytes == 2352u ?
        KUI_GAME_SECTOR_RAW : KUI_GAME_SECTOR_MODE1;
    CHECK(kui_retail_image_read(&reference, lba, count, format, expected, sizeof(expected)) == KUI_GAME_OK);
}
static void mode(uint32_t bytes) {
    put(PARAM, 0); put(PARAM + 4, bytes == 2048 ? 0x2000 : 0x1000);
    put(PARAM + 8, bytes == 2048 ? 1024 : 0); put(PARAM + 12, bytes);
    CHECK(gd(KUI_GD_DATATYPE, PARAM, 0) == 0);
}
static int32_t request(uint32_t command, uint32_t lba, uint32_t count, uint32_t destination) {
    put(PARAM, lba + 150); put(PARAM + 4, count); put(PARAM + 8, destination); put(PARAM + 12, 0);
    int32_t token = gd(KUI_GD_REQUEST, command, PARAM);
    CHECK(token > 0);
    return token;
}
static int32_t status(int32_t token) { return gd(KUI_GD_CHECK, (uint32_t)token, STATUS); }

/* ---- Ordinary reads ---- */
/* CE's driver: REQUEST, then CHECK while it waits for the drive's
 * interrupt; whatever interrupts the hardware delivers in between. */
static int32_t finish(int32_t token, unsigned irqs) {
    for(unsigned round = 0; round < 100000u; ++round) {
        if(irqs) (void)interrupts(irqs + round % 3u);
        else elapse();
        int32_t s = status(token);
        if(s != KUI_GD_PROCESSING) return s;
    }
    CHECK(!"read never finished");
    return -1;
}
static void test_dma_reads_by_interrupt(void) {
    setup(48, true);
    const struct kui_retail_async_stats *st = &R.engine.stats;
    mode(2048);
    for(uint32_t lba = 45000, n = 0; lba < 45060; lba += 6, ++n) {
        /* Physical (as the G1 DMA takes it), P1 and P2 destinations. */
        uint32_t dest = OUTPUT + n * 0x4000u;
        uint32_t given = n % 3u == 0 ? (dest & 0x1fffffffu) : n % 3u == 1 ? dest : dest | 0x20000000u;
        unsigned drive = ev.drive;
        int32_t token = request(KUI_GD_DMAREAD, lba, 6, given);
        /* CE's table now leads the SCI's events to the reader. */
        CHECK(installed() && R.engine.isr && level_up());
        CHECK(finish(token, 12) == KUI_GD_COMPLETED);
        /* One drive interrupt, at completion: CE's driver waits for it. */
        CHECK(ev.drive == drive + 1u && !ev.dma_end);
        reference_read(lba, 6);
        CHECK(!memcmp(ram + (dest - BEGIN), expected, 6u * 2048u));
        CHECK(ram[dest - BEGIN + 6u * 2048u] == 0xa5);
    }
    /* The interrupt delivered nearly every block; calls never waited. */
    CHECK(st->irq_blocks > 200u && st->call_blocks < st->irq_blocks / 4u && !st->waits);
    CHECK(!kui_sci_stream_stats()->crc_errors && !hw.virtual_writes);
    /* Idle between requests: the SCI's level is CE's again. */
    CHECK(hw.iprb == 0x5a0f && st->hooks == st->releases);
    /* An SCI event while idle: not the stream's, its level dropped. */
    CHECK(kui_retail_async_irq() == 1 && hw.iprb == 0x5a0f);
    /* Raw sectors across into the last track. */
    mode(2352);
    int32_t token = request(KUI_GD_DMAREAD, 45298, 4, OUTPUT & 0x1fffffffu);
    CHECK(finish(token, 5) == KUI_GD_COMPLETED);
    reference_read(45298, 4);
    CHECK(!memcmp(ram + (OUTPUT - BEGIN), expected, 4u * 2352u));
}
static void test_before_ce_runs(void) {
    /* The bootstrap loading CE: its MMU off, CE's table not yet in use.
     * Nothing is installed and the SCI's level never changes; calls read
     * (topping up as the native reader does). */
    setup(2000, false);
    hw.table[0] = hw.table[1] = 0;
    mode(2048);
    const struct kui_retail_async_stats *st = &R.engine.stats;
    int32_t token = request(KUI_GD_PIOREAD, 45001, 40, OUTPUT);
    CHECK(!R.engine.isr && hw.iprb == 0x5a0f && !installed());
    CHECK(finish(token, 0) == KUI_GD_COMPLETED);
    CHECK(st->waits && !st->irq_blocks && !st->hooks && hw.iprb == 0x5a0f);
    CHECK(!hw.table[0] && !hw.table[1]);
    reference_read(45001, 40);
    CHECK(!memcmp(ram + (OUTPUT - BEGIN), expected, 40u * 2048u));
    /* MMU on but the table not CE's handlers yet (not in P1): still none. */
    hw.mmucr = 1; hw.table[0] = 0x00001234u; hw.table[1] = CE_DEFAULT;
    token = request(KUI_GD_DMAREAD, 45041, 8, OUTPUT);
    CHECK(!R.engine.isr && hw.table[0] == 0x00001234u && hw.iprb == 0x5a0f);
    CHECK(finish(token, 0) == KUI_GD_COMPLETED);
    /* Then CE's: installed at the next call. */
    hw.table[0] = CE_DEFAULT;
    token = request(KUI_GD_DMAREAD, 45049, 8, OUTPUT);
    CHECK(R.engine.isr && installed() && level_up());
    CHECK(finish(token, 4) == KUI_GD_COMPLETED);
    reference_read(45049, 8);
    CHECK(!memcmp(ram + (OUTPUT - BEGIN), expected, 8u * 2048u));
}
static void test_virtual_pio_read(void) {
    /* A PIOREAD into CE's virtual buffer: the interrupt never writes it
     * (map asserts); a block it cannot place raises the drive's interrupt,
     * so CE's driver calls, and the call writes. */
    setup(48, true);
    mode(2048);
    const struct kui_retail_async_stats *st = &R.engine.stats;
    uint32_t dest = VBASE + 0x800u;
    int32_t token = request(KUI_GD_PIOREAD, 45010, 30, dest);
    CHECK(installed() && level_up());
    unsigned drive = ev.drive, woken = 0;
    for(unsigned round = 0; round < 100000u; ++round) {
        (void)interrupts(3);
        if(ev.drive != drive) {++woken; drive = ev.drive;}
        if(status(token) != KUI_GD_PROCESSING) break;
    }
    CHECK(get(STATUS) == (uint32_t)KUI_GD_COMPLETED || !R.shared.service.pending);
    CHECK(!st->irq_blocks && st->call_blocks >= 30u * 2048u / 512u && woken);
    reference_read(45010, 30);
    CHECK(!memcmp(vram + (dest - VBASE), expected, 30u * 2048u));
    CHECK(vram[dest - VBASE + 30u * 2048u] == 0xa5 && !hw.writes);
}

/* ---- DMA streams: transfers of any size ---- */
/* DMAREAD_STREAM, then transfers: each piece of the given sizes goes to
 * its own physical destination, 64 bytes apart. As CE's driver, its DMA
 * thread waits for the DMA end interrupt and then asks DMA_CHECK whether the
 * transfer is done (waiting again if not), and its interrupt thread calls
 * EXEC on the drive's. Nothing else calls: a stall fails the test. */
static void dma_stream(uint32_t lba, uint32_t count, const uint32_t *sizes, unsigned irqs) {
    uint32_t total = count * R.shared.service.sector_bytes, done = 0, dest = OUTPUT;
    memset(ram + (OUTPUT - BEGIN), 0xa5, 0x40000u);
    int32_t token = request(KUI_RETAIL_GD_DMAREAD_STREAM, lba, count, 0);
    CHECK(status(token) == KUI_RETAIL_GD_STREAMING);
    reference_read(lba, count);
    unsigned drive = ev.drive;
    for(unsigned i = 0; done < total; ++i) {
        uint32_t bytes = sizes[i % 7u];
        if(bytes > total - done) bytes = total - done;
        put(PIECE, dest & 0x1fffffffu); put(PIECE + 4, bytes);
        unsigned end = ev.dma_end, woken = ev.drive;
        CHECK(gd(KUI_GD_DMA_TRANSFER, (uint32_t)token, PIECE) == 0);
        for(unsigned round = 0;; ++round) {
            CHECK(round < 100000u);
            if(ev.drive != woken) {woken = ev.drive; (void)gd(KUI_GD_EXEC, 0, 0);}
            if(ev.dma_end != end) {
                end = ev.dma_end;
                if(gd(KUI_GD_DMA_CHECK, (uint32_t)token, STATUS) == 0) break;
                continue;
            }
            if(!irqs || !interrupts(irqs)) elapse();
        }
        CHECK(!R.shared.service.xfer_left && get(STATUS) == 0);
        CHECK(!memcmp(ram + (dest - BEGIN), expected + done, bytes));
        CHECK(ram[dest - BEGIN + bytes] == 0xa5);
        done += bytes; dest += bytes + 64u;
        if(done < total) CHECK(status(token) == KUI_RETAIL_GD_STREAMING);
    }
    /* The last bytes complete the request, with the drive's interrupt. */
    CHECK(status(token) == KUI_GD_COMPLETED && get(STATUS + 8) == total);
    CHECK(ev.drive > drive && !R.engine.spill_bytes);
}
static void test_dma_stream_pieces(void) {
    /* Pieces smaller than a block, ending mid-block or mid-sector, and
     * larger than many blocks: the spill carries what a piece cannot hold. */
    static const uint32_t sizes[7] = {32, 4096, 2080, 96, 8192, 512, 1600};
    setup(48, true);
    mode(2048);
    const struct kui_retail_async_stats *st = &R.engine.stats;
    dma_stream(45000, 40, sizes, 4);
    CHECK(st->irq_blocks > st->call_blocks && !hw.virtual_writes);
    /* Raw sectors: 2352 bytes each, pieces across sector boundaries (an
     * even count: the G1 DMA moves 32-byte multiples). */
    mode(2352);
    dma_stream(45100, 20, sizes, 2);
    /* Without interrupts (masked by CE): DMA_CHECK reads, and CE's driver
     * is woken after each call while its transfer lasts. */
    mode(2048);
    hw.table[0] = 0; /* not CE's: nothing installed, the level stays down */
    dma_stream(45200, 12, sizes, 0);
    CHECK(!R.engine.isr && hw.iprb == 0x5a0f);
    /* One whole-request transfer, as ARMADA's DMA thread gives. */
    hw.table[0] = CE_DEFAULT;
    static const uint32_t whole[7] = {0x10000u, 0, 0, 0, 0, 0, 0};
    dma_stream(45010, 32, whole, 8);
}

/* ---- PIO streams: CE's driver's own (virtual) buffer, through read_part ---- */
static void test_pio_stream(void) {
    static const uint32_t sizes[7] = {2, 4096, 1000, 512, 2048, 66, 4094};
    setup(48, true);
    mode(2048);
    const struct kui_retail_async_stats *st = &R.engine.stats;
    uint32_t lba = 45020, count = 30, total = count * 2048u, done = 0, dest = VBASE;
    int32_t token = request(KUI_RETAIL_GD_PIOREAD_STREAM, lba, count, 0);
    reference_read(lba, count);
    CHECK(gd(KUI_GD_PIO_CALLBACK, 0x8c0f0000u, 0x1234u) == 0);
    for(unsigned i = 0; done < total; ++i) {
        /* The interrupt only takes a block ahead into the spill. */
        (void)interrupts(1 + i % 3u);
        CHECK(gd(KUI_GD_PIO_CHECK, (uint32_t)token, STATUS) == 0);
        uint32_t offered = get(STATUS), bytes = sizes[i % 7u];
        CHECK(offered == (total - done < 4096u ? total - done : 4096u));
        if(bytes > offered) bytes = offered;
        put(PIECE, dest); put(PIECE + 4, bytes);
        CHECK(gd(KUI_GD_PIO_TRANSFER, (uint32_t)token, PIECE) == 0);
        CHECK(!memcmp(vram + (dest - VBASE), expected + done, bytes));
        CHECK(R.shared.service.callback_due);
        (void)gd(KUI_GD_EXEC, 0, 0);
        R.shared.service.callback_due = 0; /* the resident made the callback */
        done += bytes; dest += bytes;
    }
    CHECK(status(token) == KUI_GD_COMPLETED && vram[dest - VBASE] == 0xa5);
    CHECK(!hw.writes && st->call_blocks && !R.engine.spill_bytes && !R.engine.piece_set);
}

/* ---- Cancelling, failing, CE changing its table ---- */
static void test_abort_writes_nothing_more(void) {
    static const uint32_t sizes[7] = {4096, 4096, 4096, 4096, 4096, 4096, 4096};
    for(unsigned how = 0; how < 3; ++how) {
        setup(2000, true);
        mode(2048);
        int32_t token = request(KUI_RETAIL_GD_DMAREAD_STREAM, 45000, 30, 0);
        put(PIECE, OUTPUT & 0x1fffffffu); put(PIECE + 4, 0x8000u);
        CHECK(gd(KUI_GD_DMA_TRANSFER, (uint32_t)token, PIECE) == 0);
        (void)interrupts(5);
        if(how == 0) CHECK(gd(KUI_GD_ABORT, (uint32_t)token, 0) == 0);
        else if(how == 1) CHECK(gd(KUI_GD_INIT, 0, 0) == 0);
        else CHECK(gd(KUI_GD_RESET, 0, 0) == 0);
        hw.frozen = true;
        (void)interrupts(50);
        for(unsigned i = 0; i < 10; ++i) {(void)gd(KUI_GD_EXEC, 0, 0); (void)status(token);}
        CHECK(!kui_sci_stream_busy() && hw.iprb == 0x5a0f);
        CHECK(!R.engine.spill_bytes && !R.engine.piece_set);
        hw.frozen = false;
        dma_stream(45040, 9, sizes, 3);
    }
}
static void test_failures_complete_the_request(void) {
    /* A block that never passes its CRC fails the read: the drive's
     * interrupt tells CE's driver, and CHECK reports the error. */
    setup(2000, true);
    mode(2048);
    card.corrupt_lba = manifest.slots[4 + 2].extent.card_lba + 3; card.corrupt_count = 1000;
    unsigned drive = ev.drive;
    int32_t token = request(KUI_GD_DMAREAD, 45000, 20, OUTPUT);
    CHECK(finish(token, 3) == KUI_GD_FAILED && get(STATUS + 4) == KUI_GD_ERROR_IO);
    CHECK(ev.drive > drive);
    card.corrupt_count = 0;
    /* A stream's too: its transfer ends with the error. */
    setup(2000, true);
    mode(2048);
    card.corrupt_lba = manifest.slots[4 + 2].extent.card_lba + 3; card.corrupt_count = 1000;
    token = request(KUI_RETAIL_GD_DMAREAD_STREAM, 45000, 20, 0);
    put(PIECE, OUTPUT & 0x1fffffffu); put(PIECE + 4, 20u * 2048u);
    CHECK(gd(KUI_GD_DMA_TRANSFER, (uint32_t)token, PIECE) == 0);
    for(unsigned round = 0; round < 1000u && R.shared.service.pending; ++round) {
        (void)interrupts(2);
        (void)gd(KUI_GD_DMA_CHECK, (uint32_t)token, STATUS);
    }
    CHECK(!R.shared.service.pending && status(token) == KUI_GD_FAILED);
    card.corrupt_count = 0;
    /* A PIO stream piece that does not continue the stream is refused. */
    setup(2000, true);
    mode(2048);
    token = request(KUI_RETAIL_GD_PIOREAD_STREAM, 45000, 4, 0);
    CHECK(kui_retail_async_read_part(NULL, 45000, 2048, 512, 512, vram) == -1);
}
static void test_table_replaced(void) {
    /* CE puts its own handlers back mid-read: the next call drops the
     * SCI's level, and calls finish the read. */
    setup(2000, true);
    mode(2048);
    int32_t token = request(KUI_GD_DMAREAD, 45000, 40, OUTPUT);
    CHECK(level_up() && interrupts(5) == 5);
    hw.table[0] = hw.table[1] = CE_DEFAULT + 0x40u;
    hw.mmucr = 0; /* and reports its MMU off: nothing is installed again */
    CHECK(status(token) == KUI_GD_PROCESSING);
    CHECK(!R.engine.isr && hw.iprb == 0x5a0f && hw.table[0] == CE_DEFAULT + 0x40u);
    CHECK(finish(token, 0) == KUI_GD_COMPLETED);
    reference_read(45000, 40);
    CHECK(!memcmp(ram + (OUTPUT - BEGIN), expected, 40u * 2048u));
}
static void test_level_dropped_in_a_call(void) {
    /* kui_retail_ce_isr reached inside a GD call (CE unmasked in an
     * exception path) only drops the SCI's level; the call raises it again
     * and the read goes on by interrupt. */
    setup(2000, true);
    mode(2048);
    int32_t token = request(KUI_GD_DMAREAD, 45000, 30, OUTPUT);
    CHECK(level_up() && interrupts(3) == 3);
    hw.iprb &= (uint16_t)~0x00f0u;
    CHECK(interrupts(3) == 0);
    CHECK(status(token) == KUI_GD_PROCESSING && level_up() && R.engine.stats.hooks == 1);
    CHECK(finish(token, 4) == KUI_GD_COMPLETED);
    CHECK(hw.iprb == 0x5a0f);
    reference_read(45000, 30);
    CHECK(!memcmp(ram + (OUTPUT - BEGIN), expected, 30u * 2048u));
}
static void test_stress(void) {
    setup(48, true);
    srand(9191);
    const struct kui_retail_async_stats *st = &R.engine.stats;
    for(unsigned round = 0; round < 200; ++round) {
        uint32_t count = 1u + (uint32_t)rand() % 24u;
        uint32_t lba = 45000u + (uint32_t)rand() % (300u - count);
        int fault = rand() % 6;
        if(fault == 0) {card.corrupt_lba = 100u + (uint32_t)rand() % 1800u; card.corrupt_count = 1;}
        if(fault == 1) {m.overrun_after = 1u + (unsigned)rand() % 512u; m.in_flight = rand() % 2;}
        if(fault == 2) m.chcr = 1;
        card.nac = 1u + (unsigned)rand() % 3u;
        uint32_t sizes[7];
        for(unsigned i = 0; i < 7; ++i) sizes[i] = 32u * (1u + (uint32_t)rand() % 300u);
        int kind = rand() % 3;
        /* CE's DMA thread only waits: its interrupts must arrive. */
        if(kind == 0) dma_stream(lba, count, sizes, 1u + (unsigned)rand() % 5u);
        else {
            memset(ram + (OUTPUT - BEGIN), 0xa5, 64u * 2352u);
            int32_t token = request(kind == 1 ? KUI_GD_DMAREAD : KUI_GD_PIOREAD, lba, count, OUTPUT);
            CHECK(finish(token, (unsigned)rand() % 6u) == KUI_GD_COMPLETED);
            reference_read(lba, count);
            CHECK(!memcmp(ram + (OUTPUT - BEGIN), expected, count * 2048u));
        }
        m.chcr = 0; m.overrun_after = 0; card.corrupt_count = 0; m.in_flight = false;
    }
    const struct kui_sci_stream_stats *ss = kui_sci_stream_stats();
    printf("stress CE: irq %u call %u waits %u hooks %u | dma %u polled %u repaired %u deferred %u\n",
        st->irq_blocks, st->call_blocks, st->waits, st->hooks, ss->blocks, ss->polled,
        ss->repaired, ss->deferred);
    CHECK(st->irq_blocks > st->call_blocks && ss->repaired);
}

int main(void) {
    test_dma_reads_by_interrupt();
    test_before_ce_runs();
    test_virtual_pio_read();
    test_dma_stream_pieces();
    test_pio_stream();
    test_abort_writes_nothing_more();
    test_failures_complete_the_request();
    test_table_replaced();
    test_level_dropped_in_a_call();
    test_stress();
    printf("retail async reader (Windows CE): %u checks passed\n", checks);
    return 0;
}
