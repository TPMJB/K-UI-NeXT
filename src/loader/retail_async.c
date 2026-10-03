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

#ifdef KUI_RETAIL_ASYNC_TEST
extern uint32_t kui_retail_async_test_vbr(void);
extern void kui_retail_async_test_set_vbr(uint32_t);
extern uint16_t kui_retail_async_test_read16(uint32_t);
extern void kui_retail_async_test_write16(uint32_t, uint16_t);
#define vbr_get kui_retail_async_test_vbr
#define vbr_set kui_retail_async_test_set_vbr
#define rd16 kui_retail_async_test_read16
#define wr16 kui_retail_async_test_write16
#else
static uint32_t vbr_get(void) {
    uint32_t value;
    __asm__ __volatile__("stc vbr,%0" : "=r"(value));
    return value;
}
static void vbr_set(uint32_t value) { __asm__ __volatile__("ldc %0,vbr" : : "r"(value) : "memory"); }
static uint16_t rd16(uint32_t a) { return *(volatile uint16_t *)(uintptr_t)a; }
static void wr16(uint32_t a, uint16_t v) { *(volatile uint16_t *)(uintptr_t)a = v; }
#endif

struct kui_retail_async_region kui_retail_async_region __attribute__((aligned(32)));
#define R kui_retail_async_region
#define e kui_retail_async_region.engine
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
static void write_out(void *unused, uint32_t offset, const uint8_t *bytes, uint32_t count) {
    (void)unused;
    struct kui_retail_gd *s = &R.shared.service;
    uint8_t *out = s->ops.map(s->ops.context, e.destination + offset, count, 1);
    if(out) memcpy(out, bytes, count);
    else e.failed = KUI_GD_ERROR_MEMORY;
}
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
    if(s->pending && e.token == s->token)
        kui_retail_gd_progress(s, e.cursor.done, e.failed);
}
static void start(void) {
    struct kui_retail_gd *s = &R.shared.service;
    e.token = s->token;
    e.failed = 0; e.retries = 0; e.active = 0;
    e.cursor.done = 0; /* a read that fails here delivered nothing */
    e.destination = (s->destination & 0x00ffffffu) | 0x8c000000u;
    if((!e.opened && !open_bus()) ||
       kui_retail_cursor_begin(&e.cursor, e.manifest, s->lba, s->count,
           s->sector_bytes == KUI_GAME_RAW_BYTES ? KUI_GAME_SECTOR_RAW : KUI_GAME_SECTOR_MODE1,
           write_out, NULL) != KUI_GAME_OK) {
        e.failed = KUI_GD_ERROR_IO;
        return;
    }
    e.active = 1;
}
void kui_retail_async_call(uint32_t function) {
    uint32_t wait = 0;
    if(e.active) {
        hook();
        if(function == KUI_GD_EXEC) {
            ++e.stats.execs;
            if(kui_retail_hook_sr & 0xf0u) ++e.stats.exec_int; /* IMASK: a handler */
        }
        /* An EXEC or CHECK waits for the blocks nothing delivered since the
         * previous one, up to the target: the data a game waits for keeps
         * coming while the interrupt is held off. */
        if((function == KUI_GD_EXEC || function == KUI_GD_CHECK) && e.since < e.target) {
            wait = e.target - e.since;
            ++e.stats.waits;
        }
    }
    deliver(wait);
    report();
}
void kui_retail_async_after(uint32_t function, int32_t result) {
    struct kui_retail_gd *s = &R.shared.service;
    if(function == KUI_GD_REQUEST && result > 0 && s->pending &&
       (s->command == KUI_GD_PIOREAD || s->command == KUI_GD_DMAREAD)) {
        start();
        if(e.active) {hook(); deliver(0);}
        report();
    } else if(e.active && (!s->pending || e.token != s->token)) {
        e.active = 0; /* aborted or reset: nothing more is written */
    }
    if(!kui_sci_stream_busy()) unhook();
    if(function == KUI_GD_EXEC || function == KUI_GD_CHECK) e.since = 0;
}
uint32_t kui_retail_async_irq(void) {
    /* An SCI event without a reception: not ours, passed on as any other. */
    if(!kui_sci_stream_busy()) return 1;
    e.in_irq = 1;
    deliver(0);
    e.in_irq = 0;
    if(!kui_sci_stream_busy()) unhook();
    return 0;
}
