/* SPDX-License-Identifier: GPL-3.0-only
 * Original K-UI background game reader (see retail_async.h). The stream
 * sequence is the one the SCI async probe proved on the console. */
#include "retail_async.h"
#include "sci_sd_bus.h"
#include "kui/retail_loader_layout.h"
#include <string.h>

#define IPRB UINT32_C(0xffd00008)
/* IPRB bits 7..4: the SCI's level. The reader's is the lowest. */
#define SCI_FIELD 0x00f0u
#define SCI_LEVEL 0x0010u
/* A fresh CMD18's token can take the card's whole access time. */
#define TOKEN_LIMIT 65536u
/* Consecutive failures at one block before the read is reported failed;
 * from the POLLED-th on, the block is read by programmed transfers, which
 * cannot overrun however long the game holds the bus. */
#define RETRIES 8u
#define POLLED 2u
/* Blocks each EXEC or CHECK makes sure of since the previous one: X about
 * twice the ordinary reader's two-sector step (the pinned build's), Y a
 * quarter more. */
#define TARGET_X 20u
#define TARGET_Y 25u
/* The GD caller's SR, published by the resident's hook entry. */
extern volatile uint32_t kui_retail_hook_sr;

#ifdef KUI_RETAIL_CE
/* Windows CE: its MMU control register, and the entries of its interrupt
 * handler table (at kui_retail_ce_kernel[3], indexed by INTEVT / 8) for the
 * SCI's receive error (ERI, INTEVT 0x4E0) and receive (RXI, 0x500). */
#define MMUCR UINT32_C(0xff000010)
#define ERI_ENTRY 0x9cu
#define RXI_ENTRY 0xa0u
extern volatile uint32_t kui_retail_ce_kernel[KUI_RETAIL_CE_KERNEL_WORDS];
extern void kui_retail_ce_isr(void);
#endif
#ifdef KUI_RETAIL_ASYNC_TEST
extern uint32_t kui_retail_async_test_vbr(void);
extern void kui_retail_async_test_set_vbr(uint32_t);
extern uint16_t kui_retail_async_test_read16(uint32_t);
extern void kui_retail_async_test_write16(uint32_t, uint16_t);
#define vbr_get kui_retail_async_test_vbr
#define vbr_set kui_retail_async_test_set_vbr
#define rd16 kui_retail_async_test_read16
#define wr16 kui_retail_async_test_write16
#ifdef KUI_RETAIL_CE
extern uint32_t kui_retail_async_test_read32(uint32_t);
extern void kui_retail_async_test_write32(uint32_t, uint32_t);
#define rd32 kui_retail_async_test_read32
#define wr32 kui_retail_async_test_write32
#endif
#else
#ifndef KUI_RETAIL_CE
static uint32_t vbr_get(void) {
    uint32_t value;
    __asm__ __volatile__("stc vbr,%0" : "=r"(value));
    return value;
}
static void vbr_set(uint32_t value) { __asm__ __volatile__("ldc %0,vbr" : : "r"(value) : "memory"); }
#else
static uint32_t rd32(uint32_t a) { return *(volatile uint32_t *)(uintptr_t)a; }
static void wr32(uint32_t a, uint32_t v) { *(volatile uint32_t *)(uintptr_t)a = v; }
#endif
static uint16_t rd16(uint32_t a) { return *(volatile uint16_t *)(uintptr_t)a; }
static void wr16(uint32_t a, uint16_t v) { *(volatile uint16_t *)(uintptr_t)a = v; }
#endif

struct kui_retail_async_region kui_retail_async_region __attribute__((aligned(32)));
#define R kui_retail_async_region
#define e kui_retail_async_region.engine
#ifdef KUI_RETAIL_CE
void kui_retail_async_init(const struct kui_retail_manifest *manifest) {
    e.manifest = manifest;
    e.target = manifest->reader == KUI_RETAIL_READER_ASYNC_EAGER ? TARGET_Y : TARGET_X;
}

/* Only the SCI's level changes, to the lowest, and only while CE's table
 * leads its events to the resident. Raised again if found dropped (by
 * kui_retail_ce_isr, should an interrupt reach it inside a GD call). */
static void hook(void) {
    if(!e.isr) return;
    uint16_t b = rd16(IPRB);
    if(!e.hooked) {e.release.sci = b & SCI_FIELD; e.hooked = 1; ++e.stats.hooks;}
    else if((b & SCI_FIELD) == SCI_LEVEL) return;
    wr16(IPRB, (uint16_t)((b & ~SCI_FIELD) | SCI_LEVEL));
}
static void unhook(void) {
    if(!e.hooked) return;
    uint16_t b = rd16(IPRB);
    if((b & SCI_FIELD) == SCI_LEVEL) wr16(IPRB, (uint16_t)((b & ~SCI_FIELD) | e.release.sci));
    e.hooked = 0; ++e.stats.releases;
}
/* CE dispatches every interrupt through its handler table. Once CE has set
 * it up (its MMU on, both entries handlers in P1: CE's own or already the
 * resident's), the SCI's two go to the resident (kui_retail_ce_isr); checked
 * at every GD call. Before then (the bootstrap loading CE) GD calls do all
 * the reading. */
static bool handler(uint32_t entry, uint32_t isr) {
    return entry == isr || (entry & 0xff000000u) == 0x8c000000u;
}
static void install(void) {
    uint32_t table = kui_retail_ce_kernel[3], isr = (uint32_t)(uintptr_t)kui_retail_ce_isr;
    e.isr = 0;
    if(table && (rd32(MMUCR) & 1u)) {
        uint32_t eri = rd32(table + ERI_ENTRY), rxi = rd32(table + RXI_ENTRY);
        if(handler(eri, isr) && handler(rxi, isr)) {
            if(eri != isr) wr32(table + ERI_ENTRY, isr);
            if(rxi != isr) wr32(table + RXI_ENTRY, isr);
            e.isr = 1;
        }
    }
    if(!e.isr) unhook();
}
#else
/* Assembly templates: a forwarding vector whose word 2 names its releasing
 * entry, and the interrupt vector (its targets are fixed). */
extern const uint32_t kui_retail_vector_forward[3];
extern const uint32_t kui_retail_vector_interrupt[11];

static uint32_t our_vbr(void) { return (uint32_t)(uintptr_t)&R - 0x100u; }

/* Word by word: a constant-size copy here would pull in libgcc's block
 * moves, which the resident has no room for. */
static void copy_words(volatile uint32_t *to, const uint32_t *from, unsigned words) {
    while(words--) *to++ = *from++;
}
void kui_retail_async_init(const struct kui_retail_manifest *manifest) {
    e.manifest = manifest;
    e.target = manifest->reader == KUI_RETAIL_READER_ASYNC_EAGER ? TARGET_Y : TARGET_X;
    copy_words(R.vector100, kui_retail_vector_forward, 3);
    R.vector100[2] = (uint32_t)(uintptr_t)kui_retail_release_100;
    copy_words(R.vector400, kui_retail_vector_forward, 3);
    R.vector400[2] = (uint32_t)(uintptr_t)kui_retail_release_400;
    copy_words(R.vector600, kui_retail_vector_interrupt, 11);
}

/* Put our vectors in front of the game's and give the SCI the lowest level.
 * Nothing else changes: the game's DMAC and other levels stay as they are.
 * Games may keep the bootstrap's VBR for good (DOA2 does), so it is hooked
 * like any other. Found released (the game's VBR again while hooked), the
 * vectors are simply installed again. */
static void hook(void) {
    uint32_t vbr = vbr_get();
    if(vbr == our_vbr()) return;
    uint16_t b = rd16(IPRB);
    if(e.hooked) {
        if(vbr == e.release.vbr) ++e.stats.releases;
        else ++e.release.vbr_changes;
    }
    /* The game's own field, unless ours is still there (a game that set its
     * VBR again itself). */
    if(!e.hooked || (b & SCI_FIELD) != SCI_LEVEL) e.release.sci = b & SCI_FIELD;
    e.release.vbr = vbr;
    vbr_set(our_vbr());
    wr16(IPRB, (uint16_t)((b & ~SCI_FIELD) | SCI_LEVEL));
    e.release.armed = 1;
    if(!e.hooked) {e.hooked = 1; ++e.stats.hooks;}
}
/* The game's vectors and SCI level again, unless it changed them meanwhile
 * (or a releasing entry already gave them back). Returns still pending
 * complete normally, without installing anything. */
static void unhook(void) {
    e.release.armed = 0;
    if(!e.hooked) return;
    if(vbr_get() == our_vbr()) vbr_set(e.release.vbr);
    uint16_t b = rd16(IPRB);
    if((b & SCI_FIELD) == SCI_LEVEL) wr16(IPRB, (uint16_t)((b & ~SCI_FIELD) | e.release.sci));
    e.hooked = 0;
}
#endif

/* The bus is claimed at the first read and kept: the card waits mid-stream
 * between requests, deselected. */
static bool open_bus(void) {
    e.opened = kui_sci_sd_acquire() == KUI_LOADER_SD_OK &&
        kui_sci_stream_open(&R.shared.card.device.sd, R.area0, R.area1, false) == KUI_SCI_STREAM_OK;
    return e.opened;
}
/* CRC errors, overruns, token waits and a busy channel are retried at the
 * same block. A latched bus fault or an SCI that did not come back (RESET)
 * ends the read, as it does for the ordinary reader. */
static void failure(enum kui_sci_stream_result result) {
    if(++e.retries > RETRIES || result == KUI_SCI_STREAM_RESET) e.failed = KUI_GD_ERROR_IO;
}
#ifdef KUI_RETAIL_CE
/* count more bytes of the current piece. P1 destinations are written
 * through P2 (the resident's map); a virtual one as it is, only in CE's own
 * GD calls (writable). */
static void place(const uint8_t *bytes, uint32_t count) {
    struct kui_retail_gd *s = &R.shared.service;
    uint8_t *out = e.piece_direct ? e.piece_direct + e.piece_filled :
        s->ops.map(s->ops.context, e.piece_destination + e.piece_filled, count, 1);
    if(out) memcpy(out, bytes, count);
    else e.failed = KUI_GD_ERROR_MEMORY;
    e.piece_filled += count;
}
/* The cursor's output arrives in order: into the piece while it has room
 * and nothing waits in the spill, the rest into the spill, which a block is
 * taken into only when empty (writable) and so always holds it. */
static void write_out(void *unused, uint32_t offset, const uint8_t *bytes, uint32_t count) {
    (void)unused;
    if(offset != e.piece_begin + e.piece_filled + e.spill_bytes) {e.failed = KUI_GD_ERROR_IO; return;}
    if(e.piece_set && !e.spill_bytes) {
        uint32_t n = e.piece_bytes - e.piece_filled;
        if(n > count) n = count;
        if(n) place(bytes, n);
        bytes += n; count -= n;
    }
    if(!count) return;
    if(count > KUI_RETAIL_ASYNC_SPILL_BYTES - e.spill_from - e.spill_bytes) {e.failed = KUI_GD_ERROR_IO; return;}
    memcpy(R.spill + e.spill_from + e.spill_bytes, bytes, count);
    e.spill_bytes += count;
}
/* The next piece: output bytes [begin, begin + bytes), right after the
 * previous piece's (closed or full), to destination (P1 or CE's virtual
 * address) or direct. What the spill holds goes in first. */
static void piece(uint32_t begin, uint32_t bytes, uint32_t destination, uint8_t *direct, bool physical) {
    if(begin != e.piece_begin + e.piece_bytes || e.piece_filled != e.piece_bytes) {
        e.failed = KUI_GD_ERROR_IO;
        return;
    }
    e.piece_begin = begin; e.piece_bytes = bytes; e.piece_filled = 0;
    e.piece_destination = destination; e.piece_direct = direct;
    e.piece_physical = physical; e.piece_set = 1;
    uint32_t n = e.spill_bytes < bytes ? e.spill_bytes : bytes;
    if(n) place(R.spill + e.spill_from, n);
    e.spill_from += n; e.spill_bytes -= n;
    if(!e.spill_bytes) e.spill_from = 0;
}
/* No more of the piece is written here (read_part's buffer is the caller's
 * only during its call). */
static void close_piece(void) {
    e.piece_set = 0; e.piece_bytes = e.piece_filled; e.piece_direct = NULL;
}
/* Whether a block may be taken now: its output (at most a card block's
 * bytes) goes into the piece, and what the piece cannot hold into the
 * spill, which must be empty. The interrupt writes no virtual piece. */
static bool writable(void) {
    if(e.spill_bytes) return false;
    if(!e.piece_set || e.piece_filled == e.piece_bytes) return true;
    return !e.in_irq || e.piece_physical;
}
/* Whether the interrupt fills the current read: its handlers installed and
 * its piece physical. */
static bool irq_fills(void) { return e.isr && e.piece_set && e.piece_physical; }
/* GD calls top up what the interrupt does not fill (EXEC, CHECK, and a DMA
 * stream's DMA_CHECK). */
static bool topping(uint32_t function) {
    return function == KUI_GD_EXEC || function == KUI_GD_CHECK || function == KUI_GD_DMA_CHECK;
}
static bool tops_up(uint32_t function) { return topping(function) && !irq_fills(); }
/* CE's driver waits for an interrupt: for an ordinary read the drive's, for
 * a DMA stream's transfer its DMA end. Wake it to call for what no
 * interrupt will deliver, as the standard reader does after each step: the
 * drive's interrupt, and for an unfinished transfer both. */
static void wake(void) {
    struct kui_retail_gd *s = &R.shared.service;
    if(!s->pending || e.token != s->token || e.failed) return;
    if(s->command == KUI_GD_PIOREAD || s->command == KUI_GD_DMAREAD)
        s->interrupts |= KUI_RETAIL_GD_IRQ_DRIVE;
    else if(s->command == KUI_RETAIL_GD_DMAREAD_STREAM && s->xfer_left)
        s->interrupts |= KUI_RETAIL_GD_IRQ_DMA_END | KUI_RETAIL_GD_IRQ_DRIVE;
}
#else
static void write_out(void *unused, uint32_t offset, const uint8_t *bytes, uint32_t count) {
    (void)unused;
    struct kui_retail_gd *s = &R.shared.service;
    uint8_t *out = s->ops.map(s->ops.context, e.destination + offset, count, 1);
    if(out) memcpy(out, bytes, count);
    else e.failed = KUI_GD_ERROR_MEMORY;
}
static bool topping(uint32_t function) { return function == KUI_GD_EXEC || function == KUI_GD_CHECK; }
static bool tops_up(uint32_t function) { return topping(function); }
#endif
/* Finish an arrived block, then deliver blocks in order: the next one of the
 * run is started before each block is checked and copied. Returns while a block is in
 * flight, after up to wait blocks have been waited for. Without an active
 * read, only a finished reception is ended (its interrupt cleared). */
static void deliver(uint32_t wait) {
    uint32_t delivered = 0;
    for(;;) {
        enum kui_sci_stream_result r = KUI_SCI_STREAM_OK;
        if(kui_sci_stream_busy()) {
            r = kui_sci_stream_poll(e.in_irq != 0);
            if(r == KUI_SCI_STREAM_PENDING) {
                if(!e.active || e.failed || delivered >= wait) return;
                r = kui_sci_stream_wait();
            }
        }
        if(!e.active || e.failed) return;
        if(r != KUI_SCI_STREAM_OK) {failure(r); continue;}
        uint32_t lba = e.cursor.block;
#ifdef KUI_RETAIL_CE
        if(!writable()) {
            /* Until a piece can take it, only have the block ready. */
            if(!kui_sci_stream_ready(lba)) {
                r = kui_sci_stream_fetch(lba, TOKEN_LIMIT, e.retries >= POLLED);
                if(r > KUI_SCI_STREAM_BUSY) failure(r);
            }
            return;
        }
#endif
        /* The run's next block starts before this one is checked and
         * copied: the card streams while the CPU works. If it cannot start,
         * its own fetch later retries it and counts the failure. */
        if(e.cursor.run > 1u && kui_sci_stream_ready(lba))
            (void)kui_sci_stream_fetch(lba + 1u, TOKEN_LIMIT, false);
        const uint8_t *block = kui_sci_stream_take(lba, &r);
        if(block) {
            e.retries = 0;
            if(kui_retail_cursor_feed(&e.cursor, block) != KUI_GAME_OK) e.failed = KUI_GD_ERROR_IO;
            ++delivered;
            ++e.since;
            if(e.in_irq) ++e.stats.irq_blocks;
            else ++e.stats.call_blocks;
            if(e.cursor.done == e.cursor.count) e.active = 0;
            continue;
        }
        if(r == KUI_SCI_STREAM_CRC) {
            failure(r);
            if(e.failed) return;
        }
        /* BUSY: the next block, started early, still arrives; this one is
         * fetched again after it (not a failure of its own). */
        r = kui_sci_stream_fetch(lba, TOKEN_LIMIT, e.retries >= POLLED);
        if(r > KUI_SCI_STREAM_BUSY) failure(r);
    }
}
static void report(void) {
    struct kui_retail_gd *s = &R.shared.service;
    if(!s->pending || e.token != s->token) return;
#ifdef KUI_RETAIL_CE
    /* A DMA stream: the bytes written of its transfers. A PIO stream's
     * pieces are reported by the service (read_part). */
    if(s->command == KUI_RETAIL_GD_DMAREAD_STREAM) {
        if(e.piece_set || e.failed)
            kui_retail_gd_stream_progress(s, e.piece_begin + e.piece_filled, e.failed);
        return;
    }
#endif
    kui_retail_gd_progress(s, e.cursor.done, e.failed);
}
static bool reads(uint32_t command) {
#ifdef KUI_RETAIL_CE
    if(command == KUI_RETAIL_GD_DMAREAD_STREAM || command == KUI_RETAIL_GD_PIOREAD_STREAM) return true;
#endif
    return command == KUI_GD_PIOREAD || command == KUI_GD_DMAREAD;
}
static void start(void) {
    struct kui_retail_gd *s = &R.shared.service;
    e.token = s->token;
    e.failed = 0; e.retries = 0; e.active = 0;
    e.cursor.done = 0; /* a read that fails here delivered nothing */
#ifdef KUI_RETAIL_CE
    e.piece_set = 0; e.piece_begin = e.piece_bytes = e.piece_filled = 0;
    e.spill_from = e.spill_bytes = 0;
#else
    e.destination = (s->destination & 0x00ffffffu) | 0x8c000000u;
#endif
    if((!e.opened && !open_bus()) ||
       kui_retail_cursor_begin(&e.cursor, e.manifest, s->lba, s->count,
           s->sector_bytes == KUI_GAME_RAW_BYTES ? KUI_GAME_SECTOR_RAW : KUI_GAME_SECTOR_MODE1,
           write_out, NULL) != KUI_GAME_OK) {
        e.failed = KUI_GD_ERROR_IO;
        return;
    }
    e.active = 1;
#ifdef KUI_RETAIL_CE
    /* An ordinary read's piece is its whole destination: physical when P1
     * or P2 (or, for a DMA, a physical address), else CE's virtual one.
     * Streams' pieces come with their transfers. */
    if(s->command == KUI_GD_PIOREAD || s->command == KUI_GD_DMAREAD) {
        uint32_t d = s->destination, area = d & 0xff000000u;
        bool physical = area == 0x8c000000u || area == 0xac000000u ||
            (area == 0x0c000000u && s->command == KUI_GD_DMAREAD);
        piece(0, s->request_bytes, physical ? (d & 0x1fffffffu) | 0x80000000u : d, NULL, physical);
    }
#endif
}
void kui_retail_async_call(uint32_t function) {
    uint32_t wait = 0;
#ifdef KUI_RETAIL_CE
    install();
#endif
    if(e.active) {
        hook();
        if(function == KUI_GD_EXEC) {
            ++e.stats.execs;
            if(kui_retail_hook_sr & 0xf0u) ++e.stats.exec_int; /* IMASK: a handler */
        }
        /* An EXEC or CHECK waits for the blocks nothing delivered since the
         * previous one, up to the target: the data a game waits for keeps
         * coming while the interrupt is held off. */
        if(tops_up(function) && e.since < e.target) {
            wait = e.target - e.since;
            ++e.stats.waits;
        }
    }
    deliver(wait);
    report();
}
void kui_retail_async_after(uint32_t function, int32_t result) {
    struct kui_retail_gd *s = &R.shared.service;
    if(function == KUI_GD_REQUEST && result > 0 && s->pending && reads(s->command)) {
        start();
        if(e.active) {hook(); deliver(0);}
        report();
#ifdef KUI_RETAIL_CE
    } else if(function == KUI_GD_DMA_TRANSFER && !result && s->pending && e.token == s->token &&
              s->xfer_left) {
        /* A DMA stream's transfer: its next piece, at a physical address. */
        piece(s->completed_bytes, s->xfer_left, (s->xfer_destination & 0x1fffffffu) | 0x80000000u,
              NULL, true);
        if(e.active) {hook(); deliver(e.isr ? 0 : e.target);}
        report();
#endif
    } else if(!s->pending || e.token != s->token) {
        e.active = 0; /* aborted or reset: nothing more is written */
#ifdef KUI_RETAIL_CE
        e.piece_set = 0; e.spill_bytes = 0;
#endif
    }
#ifdef KUI_RETAIL_CE
    if(kui_sci_stream_busy()) hook(); /* a block in flight ends by interrupt */
    else unhook();
#else
    if(!kui_sci_stream_busy()) unhook();
#endif
    if(topping(function)) e.since = 0;
#ifdef KUI_RETAIL_CE
    /* What the interrupt does not fill, CE's driver calls for again at once. */
    if(!irq_fills() && (topping(function) || function == KUI_GD_DMA_TRANSFER)) wake();
#endif
}
uint32_t kui_retail_async_irq(void) {
    /* An SCI event without a reception: not ours, passed on as any other
     * (under CE: its level is dropped). */
    if(!kui_sci_stream_busy()) {
#ifdef KUI_RETAIL_CE
        unhook();
#endif
        return 1;
    }
    e.in_irq = 1;
    deliver(0);
    e.in_irq = 0;
#ifdef KUI_RETAIL_CE
    report();
    /* The stream stopped short of the read (a block for a virtual piece, or
     * one to resume later): CE's driver calls for the rest. */
    if(e.active && !kui_sci_stream_busy()) wake();
#endif
    if(!kui_sci_stream_busy()) unhook();
    return 0;
}
#ifdef KUI_RETAIL_CE
int kui_retail_async_read_part(void *unused, uint32_t lba, uint32_t sector_bytes,
                               uint32_t skip, uint32_t bytes, void *output) {
    (void)unused; (void)lba; (void)sector_bytes;
    struct kui_retail_gd *s = &R.shared.service;
    if(e.token != s->token) return -1;
    piece(skip, bytes, 0, output, false);
    if(!e.failed && e.piece_filled < bytes) deliver(UINT32_MAX);
    bool done = !e.failed && e.piece_filled == bytes;
    close_piece();
    return done ? 0 : -1;
}
#endif
