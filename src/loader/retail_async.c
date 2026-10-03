/* SPDX-License-Identifier: GPL-3.0-only
 * Original K-UI background game reader (see retail_async.h). The stream
 * sequence is the one the SCI async probe proved on the console. */
#include "retail_async.h"
#include "sci_sd_bus.h"
#include "kui/retail_loader_layout.h"
#include <string.h>

#define IPRB UINT32_C(0xffd00008)
#define IPRC UINT32_C(0xffd0000c)
/* A fresh CMD18's token can take the card's whole access time. */
#define TOKEN_LIMIT 65536u
/* Consecutive failures at one block before the read is reported failed. */
#define RETRIES 8u
/* An EXEC with no interrupt delivering: about the ordinary reader's
 * two-sector step. */
#define WAIT_BLOCKS 10u

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
/* Assembly templates: a forwarding vector whose word 3 names its target's
 * slot, and the interrupt vector whose word 11 names the game's (the
 * handler's own forwarding reads it there too). */
extern const uint32_t kui_retail_vector_forward[4];
extern const uint32_t kui_retail_vector_interrupt[13];

static uint32_t our_vbr(void) { return (uint32_t)(uintptr_t)&R - 0x100u; }

void kui_retail_async_init(const struct kui_retail_manifest *manifest) {
    e.manifest = manifest;
    memcpy(R.vector100, kui_retail_vector_forward, sizeof(kui_retail_vector_forward));
    R.vector100[3] = (uint32_t)(uintptr_t)&e.forward[0];
    memcpy(R.vector400, kui_retail_vector_forward, sizeof(kui_retail_vector_forward));
    R.vector400[3] = (uint32_t)(uintptr_t)&e.forward[1];
    memcpy(R.vector600, kui_retail_vector_interrupt, sizeof(kui_retail_vector_interrupt));
    R.vector600[11] = (uint32_t)(uintptr_t)&e.forward[2];
}

/* Put our vectors in front of the game's and give the stream's sources an
 * interrupt level: the game's DMAC level if it set one, else the lowest. */
static void hook(void) {
    uint32_t vbr = vbr_get();
    if(vbr != our_vbr()) {
        if(vbr == KUI_RETAIL_BOOT_VBR) {++e.stats.boot_vbr; return;}
        if(e.hooked) ++e.stats.vbr_changes;
        e.game_vbr = vbr;
        e.forward[0] = vbr + 0x100u; e.forward[1] = vbr + 0x400u; e.forward[2] = vbr + 0x600u;
        vbr_set(our_vbr());
    }
    if(e.hooked) return;
    uint16_t b = rd16(IPRB), c = rd16(IPRC);
    e.iprb = b; e.iprc = c;
    e.level = (c >> 8) & 15u;
    if(!e.level) {
        e.level = 1;
        wr16(IPRC, (uint16_t)((c & ~0x0f00u) | 0x0100u));
    }
    wr16(IPRB, (uint16_t)((b & ~0x00f0u) | e.level << 4));
    e.hooked = 1;
    ++e.stats.hooks;
}
/* The game's vectors and levels again, unless it changed them meanwhile. */
static void unhook(void) {
    if(!e.hooked) return;
    if(vbr_get() == our_vbr()) vbr_set(e.game_vbr);
    uint16_t b = rd16(IPRB), c = rd16(IPRC);
    if(((b >> 4) & 15u) == e.level) wr16(IPRB, (uint16_t)((b & ~0x00f0u) | (e.iprb & 0x00f0u)));
    if(((c >> 8) & 15u) == e.level && !(e.iprc & 0x0f00u)) wr16(IPRC, (uint16_t)(c & ~0x0f00u));
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
    ++e.stats.failures;
    if(++e.retries > e.stats.max_retries) e.stats.max_retries = e.retries;
    if(e.retries > RETRIES || result == KUI_SCI_STREAM_RESET) e.failed = KUI_GD_ERROR_IO;
}
static void write_out(void *unused, uint32_t offset, const uint8_t *bytes, uint32_t count) {
    (void)unused;
    struct kui_retail_gd *s = &R.shared.service;
    uint8_t *out = s->ops.map(s->ops.context, e.destination + offset, count, 1);
    if(out) memcpy(out, bytes, count);
    else e.failed = KUI_GD_ERROR_MEMORY;
}
/* Finish an arrived block, then deliver blocks in order: the next one of the
 * run is started before each block is copied. Returns while a block is in
 * flight, after up to wait blocks have been waited for. Without an active
 * read, only a finished reception is ended (its interrupt cleared). */
static void deliver(uint32_t wait) {
    uint32_t delivered = 0;
    for(;;) {
        enum kui_sci_stream_result r = KUI_SCI_STREAM_OK;
        if(kui_sci_stream_busy()) {
            r = kui_sci_stream_poll();
            if(r == KUI_SCI_STREAM_PENDING) {
                if(!e.active || e.failed || delivered >= wait) return;
                r = kui_sci_stream_wait();
            }
        }
        if(!e.active || e.failed) return;
        if(r != KUI_SCI_STREAM_OK) {failure(r); continue;}
        uint32_t lba = e.cursor.block;
        const uint8_t *block = kui_sci_stream_take(lba, &r);
        if(block) {
            e.retries = 0;
            if(e.cursor.run > 1u) {
                r = kui_sci_stream_fetch(lba + 1u, TOKEN_LIMIT, e.hooked);
                if(r != KUI_SCI_STREAM_OK) failure(r);
            }
            if(kui_retail_cursor_feed(&e.cursor, block) != KUI_GAME_OK) e.failed = KUI_GD_ERROR_IO;
            ++delivered;
            if(e.in_irq) ++e.stats.irq_blocks;
            else ++e.stats.call_blocks;
            if(e.cursor.done == e.cursor.count) e.active = 0;
            continue;
        }
        if(r == KUI_SCI_STREAM_CRC) {
            failure(r);
            if(e.failed) return;
        }
        r = kui_sci_stream_fetch(lba, TOKEN_LIMIT, e.hooked);
        if(r != KUI_SCI_STREAM_OK) failure(r);
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
        /* An EXEC waits for blocks itself, as the ordinary reader reads
         * them, when no interrupt can deliver (no hook) or none came since
         * the last EXEC although a block was in flight with its interrupt:
         * a game is then never slower than with the ordinary reader. */
        if(function == KUI_GD_EXEC &&
           (!e.hooked || (e.exec_armed && e.stats.irqs == e.exec_irqs))) {
            wait = WAIT_BLOCKS;
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
    if(function == KUI_GD_EXEC) {
        e.exec_irqs = e.stats.irqs;
        e.exec_armed = e.hooked && kui_sci_stream_busy();
    }
}
uint32_t kui_retail_async_irq(void) {
    ++e.stats.irqs;
    /* Channel 1 or the SCI used by the game itself: its event, not ours. */
    if(!kui_sci_stream_busy()) {++e.stats.forwarded; return 1;}
    e.in_irq = 1;
    deliver(0);
    e.in_irq = 0;
    if(!kui_sci_stream_busy()) unhook();
    return 0;
}
