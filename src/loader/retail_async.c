/* SPDX-License-Identifier: GPL-3.0-only
 * Original K-UI background game reader (see retail_async.h). The stream
 * sequence is the one the SCI async probe proved on the console. */
#include "retail_async.h"
#if KUI_NATIVE_IRQ_REARM && (!KUI_NATIVE_REQUEST_TIMING || !KUI_RETAIL_NATIVE_HYBRID || defined(KUI_RETAIL_CE))
#error "Native IRQ rearm diagnostics require the hybrid request-timing counter profile"
#endif
#if KUI_NATIVE_IRQ_REARM && (KUI_NATIVE_PACED_CHECK || KUI_NATIVE_TIMED_PUMP)
#error "IRQ rearm uses full CHECK supply and owns its two diagnostic words"
#endif
#if KUI_NATIVE_PACED_CHECK && (!KUI_NATIVE_REQUEST_TIMING || !KUI_RETAIL_NATIVE_HYBRID || defined(KUI_RETAIL_CE))
#error "Paced CHECK requires native hybrid request-timing counter ownership"
#endif
#if KUI_NATIVE_READER_DIAGNOSTIC
#if !KUI_RETAIL_NATIVE_HYBRID
#error "Reader timing requires the unused native hybrid scheduler counters"
#endif
__asm__(".global __kui_native_reader_stall_resident\n"
        ".set __kui_native_reader_stall_resident,1\n");
#if KUI_NATIVE_COOP_PUMP
__asm__(".global __kui_native_cooperative_resident\n.set __kui_native_cooperative_resident,1\n");
#if KUI_NATIVE_IRQ_REARM
__asm__(".global __kui_native_irq_rearm_resident\n.set __kui_native_irq_rearm_resident,1\n");
#endif
#if KUI_NATIVE_PACED_CHECK
__asm__(".global __kui_native_paced_resident\n.set __kui_native_paced_resident,1\n");
#endif
#if KUI_NATIVE_TIMED_PUMP
__asm__(".global __kui_native_timed_resident\n.set __kui_native_timed_resident,1\n");
#endif
#endif
#endif
#include "sci_sd_bus.h"
#include "kui/retail_loader_layout.h"
#include <string.h>
#if KUI_ARMADA_CE_STREAM
static void wake(void);
#if !defined(KUI_RETAIL_CE) || !defined(KUI_RETAIL_ASYNC)
#error "Armada streaming SCI hooks require the CE background reader"
#endif
#include "kui/armada_ce_stream_transport.h"
#include "kui/armada_ce_excerpt_entry.h"
static uintptr_t stream_event(uint32_t event,uintptr_t argument) {
    return (uintptr_t)kui_armada_ce_bind_probe(KUI_ARMADA_CE_STREAM_BUS_EVENT,
        event,0,(uint32_t)argument);
}
#endif

#define IPRB UINT32_C(0xffd00008)
/* IPRB bits 7..4: the SCI's level. The reader's is the lowest. */
#define SCI_FIELD 0x00f0u
#define SCI_LEVEL 0x0010u
/* A fresh CMD18's token can take the card's whole access time. */
#define TOKEN_LIMIT 65536u
#if KUI_RETAIL_NATIVE_HYBRID && defined(KUI_RETAIL_CE)
#error Native DMA batch ceiling must not change the CE reader
#endif
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
#if defined(KUI_RETAIL_CE) || KUI_NATIVE_READER_DIAGNOSTIC
extern uint32_t kui_retail_async_test_read32(uint32_t);
#ifdef KUI_RETAIL_CE
extern void kui_retail_async_test_write32(uint32_t, uint32_t);
#endif
#define rd32 kui_retail_async_test_read32
#ifdef KUI_RETAIL_CE
#define wr32 kui_retail_async_test_write32
#endif
#endif
#else
#ifndef KUI_RETAIL_CE
static uint32_t vbr_get(void) {
    uint32_t value;
    __asm__ __volatile__("stc vbr,%0" : "=r"(value));
    return value;
}
static void vbr_set(uint32_t value) { __asm__ __volatile__("ldc %0,vbr" : : "r"(value) : "memory"); }
#endif
#if defined(KUI_RETAIL_CE) || KUI_NATIVE_READER_DIAGNOSTIC
static uint32_t rd32(uint32_t a) { return *(volatile uint32_t *)(uintptr_t)a; }
#endif
#ifdef KUI_RETAIL_CE
static void wr32(uint32_t a, uint32_t v) { *(volatile uint32_t *)(uintptr_t)a = v; }
#endif
static uint16_t rd16(uint32_t a) { return *(volatile uint16_t *)(uintptr_t)a; }
static void wr16(uint32_t a, uint16_t v) { *(volatile uint16_t *)(uintptr_t)a = v; }
#endif

#if KUI_VMU_NATIVE_BACKGROUND
kui_retail_async_region_type *kui_retail_async_region_storage;
#else
struct kui_retail_async_region kui_retail_async_region __attribute__((aligned(32)));
#endif
#define R kui_retail_async_region
#define e kui_retail_async_region.engine
#if KUI_VMU_NATIVE_BACKGROUND
static bool native_loan_blocked(void) {
    return e.native_loan_state!=KUI_VMU_NATIVE_ASYNC_LOAN_IDLE;
}
#endif
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
#if KUI_NATIVE_IRQ_REARM
    uint16_t b = rd16(IPRB);
    if(vbr == our_vbr()) {
        /* CE already rechecks this field while installed. A native SDK may
         * update IPRB independently of VBR: re-arm our lowest SCI level and
         * preserve its new owner field for the eventual release. */
        if((b & SCI_FIELD) != SCI_LEVEL) {
            e.release.sci = b & SCI_FIELD;
            ++e.stats.execs;
            wr16(IPRB, (uint16_t)((b & ~SCI_FIELD) | SCI_LEVEL));
        }
        return;
    }
#else
    if(vbr == our_vbr()) return;
    uint16_t b = rd16(IPRB);
#endif
#if !KUI_NATIVE_REQUEST_TIMING
    if(e.hooked) {
        if(vbr == e.release.vbr) ++e.stats.releases;
        else ++e.release.vbr_changes;
    }
#endif
    /* The game's own field, unless ours is still there (a game that set its
     * VBR again itself). */
    if(!e.hooked || (b & SCI_FIELD) != SCI_LEVEL) e.release.sci = b & SCI_FIELD;
    e.release.vbr = vbr;
    vbr_set(our_vbr());
    wr16(IPRB, (uint16_t)((b & ~SCI_FIELD) | SCI_LEVEL));
    e.release.armed = 1;
    if(!e.hooked) {e.hooked = 1;
#if !KUI_NATIVE_REQUEST_TIMING
        ++e.stats.hooks;
#endif
    }
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
#if KUI_VMU_NATIVE_BACKGROUND
    if(native_loan_blocked()) return false;
#endif
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    if(e.vmu_loan_state!=KUI_ARMADA_CE_VMU_LOAN_IDLE) return false;
#endif
    e.opened = kui_sci_sd_acquire() == KUI_LOADER_SD_OK &&
        kui_sci_stream_open(&R.shared.card.device.sd, R.area0, R.area1, false) == KUI_SCI_STREAM_OK;
    return e.opened;
}
#if KUI_ARMADA_CE_STREAM
extern void kui_retail_ce_stream_wake(void);
static void stream_wake(void) { wake();kui_retail_ce_stream_wake(); }
/* Published only through the immutable STREAM entry word. The stage sends
 * ATTACH after proving the high controller and its zero state survived boot. */
const struct kui_armada_ce_stream_bus kui_retail_ce_stream_bus={
    KUI_ARMADA_CE_STREAM_BUS_VERSION,sizeof(struct kui_armada_ce_stream_bus),&R,
    open_bus,hook,unhook,stream_wake,kui_sci_stream_busy,kui_sci_stream_ready,
    kui_sci_stream_poll,kui_sci_stream_wait,kui_sci_stream_fetch,kui_sci_stream_take,kui_sci_stream_discard,
    kui_sci_stream_token_budget
};
#endif
/* CRC errors, overruns, token waits and a busy channel are retried at the
 * same block. A latched bus fault or an SCI that did not come back (RESET)
 * ends the read, as it does for the ordinary reader. */
static void failure(enum kui_sci_stream_result result) {
#ifdef KUI_RETAIL_CE
    /* An unrecovered overrun already failed this DMA block. Avoid
     * repeating that attempt before the existing polled
     * fallback. Repaired overruns return PENDING and do not reach here. */
    if(result == KUI_SCI_STREAM_OVERRUN) e.early_polled = 1;
#endif
    if(++e.retries > RETRIES || result == KUI_SCI_STREAM_RESET) e.failed = KUI_GD_ERROR_IO;
}
#ifdef KUI_RETAIL_CE
static bool retry_polled(void) { return e.early_polled || e.retries >= POLLED; }
/* count more bytes of the current piece. P1 destinations are written
 * through P2 (the resident's map); a virtual one as it is, only in CE's own
 * GD calls (writable). */
static void place(const uint8_t *bytes, uint32_t count) {
    struct kui_retail_gd *s = &R.shared.service;
    uint8_t *out = e.piece_direct ? e.piece_direct + e.piece_filled :
        s->ops.map(s->ops.context, e.piece_destination + e.piece_filled, count, 1);
    if(!out) {e.failed = KUI_GD_ERROR_MEMORY; return;}
    memcpy(out, bytes, count);
    e.piece_filled += count;
}
/* Drain oldest bytes first, and touch virtual destinations only while CE's
 * own call supplies their address space. A finished cursor may still have
 * a queued tail, so draining does not depend on e.active. */
static void drain(void) {
    if(!e.piece_set || (e.in_irq && !e.piece_physical)) return;
    while(e.spill_bytes && e.piece_filled < e.piece_bytes && !e.failed) {
        uint32_t n = e.piece_bytes - e.piece_filled;
        if(n > e.spill_bytes) n = e.spill_bytes;
        uint32_t contiguous = KUI_RETAIL_ASYNC_SPILL_BYTES - e.spill_from;
        if(n > contiguous) n = contiguous;
        place(R.spill + e.spill_from, n);
        if(e.failed) return;
        e.spill_from = (e.spill_from + n) & (KUI_RETAIL_ASYNC_SPILL_BYTES - 1u);
        e.spill_bytes -= n;
    }
}
/* The cursor's output arrives in order: into the piece while it has room
 * and can be touched, otherwise into the bounded physical ring. Admission
 * reserves a whole card block, including when its output wraps the ring. */
static void write_out(void *unused, uint32_t offset, const uint8_t *bytes, uint32_t count) {
    (void)unused;
    if(offset != e.piece_begin + e.piece_filled + e.spill_bytes) {e.failed = KUI_GD_ERROR_IO; return;}
    if(e.piece_set && !e.spill_bytes && (!e.in_irq || e.piece_physical)) {
        uint32_t n = e.piece_bytes - e.piece_filled;
        if(n > count) n = count;
        if(n) place(bytes, n);
        if(e.failed) return;
        bytes += n; count -= n;
    }
    if(!count) return;
    if(count > KUI_RETAIL_ASYNC_SPILL_BYTES - e.spill_bytes) {e.failed = KUI_GD_ERROR_IO; return;}
    e.prefetched_bytes += count;
    while(count) {
        uint32_t at = (e.spill_from + e.spill_bytes) & (KUI_RETAIL_ASYNC_SPILL_BYTES - 1u);
        uint32_t n = KUI_RETAIL_ASYNC_SPILL_BYTES - at;
        if(n > count) n = count;
        memcpy(R.spill + at, bytes, n);
        e.spill_bytes += n; bytes += n; count -= n;
    }
    if(e.spill_bytes > e.queue_highwater) e.queue_highwater = e.spill_bytes;
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
    drain();
}
/* No more of the piece is written here (read_part's buffer is the caller's
 * only during its call). */
static void close_piece(void) {
    e.piece_set = 0; e.piece_bytes = e.piece_filled; e.piece_direct = NULL;
}
/* A card block produces at most 512 output bytes. Reserve that much even
 * when the piece may take some directly; an interrupt can then prefetch
 * without knowing or touching a virtual destination. */
static bool writable(void) {
    if(e.spill_bytes <= KUI_RETAIL_ASYNC_SPILL_BYTES - 512u) return true;
    ++e.queue_blocked;
    return false;
}
/* Whether the interrupt fills the current read: its handlers installed and
 * its piece physical. */
static bool interrupt_fills(void) { return e.isr && e.piece_set && e.piece_physical; }
/* GD calls top up what the interrupt does not fill (EXEC, CHECK, and a DMA
 * stream's DMA_CHECK). */
static bool topping(uint32_t function) {
    return function == KUI_GD_EXEC || function == KUI_GD_CHECK || function == KUI_GD_DMA_CHECK;
}
static bool tops_up(uint32_t function) { return topping(function) && !interrupt_fills(); }
/* CE's driver waits for an interrupt: for an ordinary read the drive's, for
 * a DMA stream's transfer its DMA end. Wake it to call for what no
 * interrupt will deliver, as the standard reader does after each step: the
 * drive's interrupt, and for an unfinished transfer both. */
static void wake(void) {
    struct kui_retail_gd *s = &R.shared.service;
    if(!s->pending || e.token != s->token || e.failed) return;
    if(s->command == KUI_GD_PIOREAD || s->command == KUI_GD_DMAREAD ||
       s->command == KUI_RETAIL_GD_PIOREAD_STREAM)
        s->interrupts |= KUI_RETAIL_GD_IRQ_DRIVE;
    else if(s->command == KUI_RETAIL_GD_DMAREAD_STREAM && s->xfer_left)
        s->interrupts |= KUI_RETAIL_GD_IRQ_DMA_END | KUI_RETAIL_GD_IRQ_DRIVE;
}
#else
#define retry_polled() (e.retries >= POLLED)
static void write_out(void *unused, uint32_t offset, const uint8_t *bytes, uint32_t count) {
    (void)unused;
    struct kui_retail_gd *s = &R.shared.service;
    uint8_t *out = s->ops.map(s->ops.context, e.destination + offset, count, 1);
    if(out) memcpy(out, bytes, count);
    else e.failed = KUI_GD_ERROR_MEMORY;
}
static bool topping(uint32_t function) { return function == KUI_GD_EXEC || function == KUI_GD_CHECK; }
static bool tops_up(uint32_t function) {
#if KUI_NATIVE_PACED_CHECK
    if(function==KUI_GD_EXEC) {e.stats.exec_int=0u;return true;}
    if(function!=KUI_GD_CHECK)return false;
    /* The Standard reader distinguishes one status poll from a game that
     * is spinning on CHECK. Keep the same sixteen-close-poll criterion,
     * using the two unused diagnostic words for scanline/count state. */
    uint32_t line=rd32(UINT32_C(0xa05f810c))&0x3ffu;
    uint32_t count=(line-e.stats.execs<=1u?e.stats.exec_int+1u:1u)&15u;
    e.stats.execs=line;e.stats.exec_int=count;
    return count==0u;
#else
    return topping(function);
#endif
}
#endif
/* Finish an arrived block, then deliver blocks in order: the next one of the
 * run is started before each block is checked and copied. Returns while a block is in
 * flight, after up to wait blocks have been waited for. Without an active
 * read, only a finished reception is ended (its interrupt cleared). */
#if !defined(KUI_RETAIL_CE) && (!KUI_NATIVE_READER_DIAGNOSTIC || KUI_NATIVE_REQUEST_TIMING)
#define deliver_work deliver
#endif
static void deliver_work(uint32_t wait) {
    uint32_t delivered = 0;
#ifndef KUI_RETAIL_CE
    /* A native GD entry has at most one active delivery visit: REQUEST
     * rejects an owned command even if before-work just completed it;
     * after-work delivers only a newly accepted REQUEST. */
    uint32_t polled_entry=R.shared.stream.stats.polled;
#endif
#ifdef KUI_RETAIL_CE
    uint32_t irq_steps = 0;
#endif
    for(;;) {
#if KUI_RETAIL_NATIVE_HYBRID
        /* Retain the proved 20/25-block catch-up and full DMA wait. Only
         * stop the formerly unlimited run of already-completed blocks. */
        if(delivered>=(
#if KUI_NATIVE_COOP_PUMP && !KUI_NATIVE_PACED_CHECK && !KUI_NATIVE_IRQ_REARM
            e.in_irq==2u?KUI_RETAIL_NATIVE_PUMP_BATCH_LIMIT:
#endif
            e.in_irq?KUI_RETAIL_NATIVE_IRQ_BATCH_LIMIT:e.target)) return;
#endif
#ifndef KUI_RETAIL_CE
        /* PIO fallback must not drain a whole request with interrupts
         * masked. Its ready block stays buffered for the next GD entry;
         * DMA-only work keeps the existing target and receive-ahead path. */
        if(R.shared.stream.stats.polled!=polled_entry) return;
#endif
#if KUI_ARMADA_CE_STREAM
        if(!stream_event(KUI_ARMADA_CE_STREAM_BUS_DATA_ENTER,delivered<wait)) return;
#endif
#ifdef KUI_RETAIL_CE
        if(kui_sci_stream_token_pending() && R.shared.stream.token_bounded &&
           !R.shared.stream.token_budget) return;
        /* A larger queue must not turn a polled fallback into a long ISR.
         * A paused active read is woken below; an in-flight block supplies
         * the next SCI interrupt. Count failed/retry iterations too. */
        if(e.in_irq && irq_steps++ >= 2u) return;
        drain();
        if(wait == UINT32_MAX && e.piece_set && e.piece_filled == e.piece_bytes) return;
#endif
        enum kui_sci_stream_result r = KUI_SCI_STREAM_OK;
        if(kui_sci_stream_busy()) {
            r = kui_sci_stream_poll(e.in_irq != 0);
            if(r == KUI_SCI_STREAM_PENDING) {
                if(!e.active || e.failed || delivered >= wait) return;
#ifdef KUI_RETAIL_CE
                /* read_part waits for its requested bytes, never for a
                 * speculative refill after its destination is full. */
                if(e.piece_set && e.piece_filled == e.piece_bytes) return;
#endif
                r = kui_sci_stream_wait();
            }
        }
        if(!e.active || e.failed) return;
        if(r != KUI_SCI_STREAM_OK) {failure(r); continue;}
        uint32_t lba = e.cursor.block;
#ifdef KUI_RETAIL_CE
        if(!writable()) {
            /* The queue is full: keep at most one more card block ready. */
            if(!kui_sci_stream_ready(lba)) {
                r = kui_sci_stream_fetch(lba, TOKEN_LIMIT, retry_polled());
                if(r > KUI_SCI_STREAM_BUSY) failure(r);
            }
            return;
        }
#endif
        /* The run's next block starts before this one is checked and
         * copied: the card streams while the CPU works. If it cannot start,
         * its own fetch later retries it and counts the failure. */
        if(e.cursor.run > 1u && kui_sci_stream_ready(lba)
#if KUI_ARMADA_CE_STREAM
           && stream_event(KUI_ARMADA_CE_STREAM_BUS_DATA_PREFETCH,0)
#endif
          )
            (void)kui_sci_stream_fetch(lba + 1u, TOKEN_LIMIT, false);
#if KUI_ARMADA_CE_STREAM
        struct kui_armada_ce_stream_data_take held={lba,KUI_SCI_STREAM_PENDING,0,NULL};
        (void)stream_event(KUI_ARMADA_CE_STREAM_BUS_DATA_TAKE,(uintptr_t)&held);
        const uint8_t *block;
        if(held.copied) {block=held.block;r=(enum kui_sci_stream_result)held.result;}
        else block=kui_sci_stream_take(lba,&r);
#else
        const uint8_t *block = kui_sci_stream_take(lba, &r);
#endif
        if(block) {
            e.retries = 0;
#ifdef KUI_RETAIL_CE
            e.early_polled = 0;
#endif
            if(kui_retail_cursor_feed(&e.cursor, block) != KUI_GAME_OK) e.failed = KUI_GD_ERROR_IO;
            ++delivered;
            ++e.since;
            if(e.in_irq) ++e.stats.irq_blocks;
            else ++e.stats.call_blocks;
            if(e.cursor.done == e.cursor.count) {
                e.active = 0;
#if KUI_NATIVE_REQUEST_TIMING
                uint32_t elapsed=e.stats.hooks-rd32(UINT32_C(0xffd8000c));
                if(elapsed>e.stats.releases) {
                    e.stats.releases=elapsed;
                    e.native_max_lba=R.shared.service.lba;
                    e.native_max_count=R.shared.service.count;
                }
#endif
            }
#if KUI_ARMADA_CE_STREAM
            if(stream_event(KUI_ARMADA_CE_STREAM_BUS_DATA_BLOCK,0)) return;
#endif
            continue;
        }
        if(r == KUI_SCI_STREAM_CRC) {
            failure(r);
#ifdef KUI_RETAIL_CE
            if(e.failed) return;
#else
            /* Prefetch may already have spent this visit's PIO allowance.
             * Check it at the loop head before attempting the reread. */
            continue;
#endif
        }
        /* BUSY: the next block, started early, still arrives; this one is
         * fetched again after it (not a failure of its own). */
        r = kui_sci_stream_fetch(lba, TOKEN_LIMIT, retry_polled());
        if(r > KUI_SCI_STREAM_BUSY) failure(r);
    }
}
#ifdef KUI_RETAIL_CE
static void deliver(uint32_t wait) {
    uint32_t before = R.shared.stream.stats.polled;
    deliver_work(wait);
    uint32_t blocks = R.shared.stream.stats.polled - before;
    uint32_t *maximum = e.in_irq ? &e.polled_irq_max : &e.polled_call_max;
    if(blocks > *maximum) *maximum = blocks;
}
#elif KUI_NATIVE_READER_DIAGNOSTIC && !KUI_NATIVE_REQUEST_TIMING
/* Diagnostic build only. These two counters are unused by the hybrid
 * scheduler. The worker samples/reset them under IMASK15; clock admission
 * happens at both capture endpoints. Never configure a game timer here. */
static void deliver(uint32_t wait) {
    uint32_t before=rd32(UINT32_C(0xffd8000c));
    deliver_work(wait);
    uint32_t elapsed=before-rd32(UINT32_C(0xffd8000c));
    uint32_t *maximum=e.in_irq?&e.stats.exec_int:&e.stats.execs;
    if(elapsed>*maximum) *maximum=elapsed;
}
#endif
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
    if(s->command == KUI_RETAIL_GD_PIOREAD_STREAM) {
        /* Prefetch can fail before the next PIO_TRANSFER is submitted.
         * Wake the driver's request/error path now; do not invent a
         * transfer callback. Successful progress still belongs to its
         * synchronous read_part caller. */
        if(e.failed) {
            s->error = e.failed; s->status = KUI_GD_FAILED; s->pending = 0;
            s->diag.last_error = e.failed;
            s->interrupts |= KUI_RETAIL_GD_IRQ_DRIVE;
        }
        return;
    }
    /* Prefetch can finish the cursor before its final bytes reach CE.
     * Ordinary reads complete only at the destination, never in the queue.
     * PIO stream progress remains owned by read_part's service caller. */
    kui_retail_gd_progress(s, e.piece_filled / s->sector_bytes, e.failed);
#else
    kui_retail_gd_progress(s, e.cursor.done, e.failed);
#endif
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
#if KUI_NATIVE_REQUEST_TIMING
    e.stats.hooks=rd32(UINT32_C(0xffd8000c));
#endif
    e.failed = 0; e.retries = 0; e.active = 0;
#if KUI_NATIVE_PACED_CHECK
    e.stats.exec_int=0u;
#endif
    e.cursor.done = 0; /* a read that fails here delivered nothing */
#ifdef KUI_RETAIL_CE
    e.piece_set = 0; e.piece_begin = e.piece_bytes = e.piece_filled = 0;
    e.spill_from = e.spill_bytes = 0;
    e.early_polled = 0;
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
#if KUI_VMU_NATIVE_BACKGROUND
extern volatile uint32_t kui_retail_hook_active,kui_retail_hook_fault;
#ifdef KUI_RETAIL_ASYNC_TEST
extern uint32_t kui_retail_async_vmu_test_sr(void);
#define native_loan_sr kui_retail_async_vmu_test_sr
#else
static uint32_t native_loan_sr(void) {
    uint32_t sr;
    __asm__ __volatile__("stc sr,%0":"=r"(sr)::"memory");
    return sr;
}
#endif
static void native_loan_fault(void) {
    e.native_loan_state=KUI_VMU_NATIVE_ASYNC_LOAN_QUARANTINED;
    R.shared.card.device.sd.ready=false;
    e.failed=KUI_GD_ERROR_IO;
    kui_retail_hook_fault=1u;
}
enum kui_vmu_native_async_loan_result kui_retail_native_async_loan_begin(
    struct kui_loader_sd *copied_card) {
    if((native_loan_sr()&UINT32_C(0xf0))!=UINT32_C(0xf0))
        return KUI_VMU_NATIVE_ASYNC_LOAN_CONTEXT;
    if(!copied_card || e.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_QUARANTINED)
        return KUI_VMU_NATIVE_ASYNC_LOAN_FAULT;
    if(native_loan_blocked() || kui_retail_hook_active)
        return KUI_VMU_NATIVE_ASYNC_LOAN_BUSY;
    kui_retail_hook_active=1u;
    const struct kui_retail_gd *s=&R.shared.service;
    /* A completed command may still await CHECK. Its terminal result does
     * not own SCI; a pending command, receiver or released handler does. */
    if(!s->initialized || s->pending || s->executing || e.active || e.in_irq ||
       e.release.depth || kui_sci_stream_busy()) {
        kui_retail_hook_active=0u;return KUI_VMU_NATIVE_ASYNC_LOAN_BUSY;
    }
    if(R.shared.card.transport!=KUI_STORAGE_SCI || !R.shared.card.device.sd.ready ||
       R.shared.card.device.sd.slow || !R.shared.card.device.sd.blocks ||
       R.shared.card.stream.active || R.shared.port.fault ||
       e.opened!=(uint32_t)R.shared.port.acquired) goto fault;
    e.native_loan_state=KUI_VMU_NATIVE_ASYNC_LOAN_STOPPING;
    unhook();
    if((e.opened && kui_sci_stream_stop()!=KUI_SCI_STREAM_OK) ||
       kui_sci_stream_busy() || R.shared.stream.state || R.shared.port.fault ||
       e.opened!=(uint32_t)R.shared.port.acquired || e.release.armed ||
       e.release.depth || e.hooked || vbr_get()==our_vbr()) goto fault;
    kui_sci_stream_discard();
    if(e.opened) kui_sci_sd_release();
    if(R.shared.port.acquired || R.shared.port.fault) goto fault;
    e.opened=0u;
    *copied_card=R.shared.card.device.sd;
    e.native_loan_state=KUI_VMU_NATIVE_ASYNC_LOAN_WRITER;
    return KUI_VMU_NATIVE_ASYNC_LOAN_OK;
fault:
    native_loan_fault();kui_retail_hook_active=0u;
    return KUI_VMU_NATIVE_ASYNC_LOAN_FAULT;
}
enum kui_vmu_native_async_loan_result kui_retail_native_async_loan_end(bool healthy) {
    if((native_loan_sr()&UINT32_C(0xf0))!=UINT32_C(0xf0))
        return KUI_VMU_NATIVE_ASYNC_LOAN_CONTEXT;
    const struct kui_retail_gd *s=&R.shared.service;
    if(e.native_loan_state!=KUI_VMU_NATIVE_ASYNC_LOAN_WRITER ||
       kui_retail_hook_active!=1u || !healthy || R.shared.port.acquired ||
       R.shared.port.fault || e.opened || kui_sci_stream_busy() || R.shared.stream.state ||
       !R.shared.card.device.sd.ready || e.active || e.in_irq || e.release.depth ||
       e.release.armed || e.hooked || vbr_get()==our_vbr() ||
       !s->initialized || s->pending || s->executing) {
        native_loan_fault();
        if(kui_retail_hook_active==1u) kui_retail_hook_active=0u;
        return KUI_VMU_NATIVE_ASYNC_LOAN_FAULT;
    }
    /* Preserve the completed handle and cursor; a future read starts its
     * own cursor and reacquires the bus through open_bus, never here. */
    e.native_loan_state=KUI_VMU_NATIVE_ASYNC_LOAN_IDLE;
    kui_retail_hook_active=0u;
    return KUI_VMU_NATIVE_ASYNC_LOAN_OK;
}
#endif
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
#if !defined(KUI_RETAIL_CE) || !defined(KUI_RETAIL_ASYNC)
#error "CE VMU storage loans require the CE asynchronous reader"
#endif
extern volatile uint32_t kui_retail_hook_active,kui_retail_hook_fault;
extern volatile uint32_t kui_retail_ce_deferred[2];
extern void kui_retail_ce_vmu_loan_wake(void);
#ifdef KUI_RETAIL_ASYNC_TEST
extern uint32_t kui_retail_async_vmu_test_sr(void);
extern void kui_retail_async_vmu_test_set_sr(uint32_t);
#define loan_sr kui_retail_async_vmu_test_sr
#define loan_set_sr kui_retail_async_vmu_test_set_sr
#else
static uint32_t loan_sr(void) {
    uint32_t sr;
    __asm__ __volatile__("stc sr,%0":"=r"(sr)::"memory");
    return sr;
}
static void loan_set_sr(uint32_t sr) {
    __asm__ __volatile__("ldc %0,sr"::"r"(sr):"memory");
}
#endif
static bool loan_blocked(void) {
    return e.vmu_loan_state!=KUI_ARMADA_CE_VMU_LOAN_IDLE;
}
static void loan_fault(void) {
    struct kui_retail_gd *s=&R.shared.service;
    e.vmu_loan_state=KUI_ARMADA_CE_VMU_LOAN_QUARANTINED;
    s->storage_deferred=1u;
    R.shared.card.device.sd.ready=false;
    e.failed=KUI_GD_ERROR_IO;kui_retail_hook_fault=1u;
    if(s->pending && reads(s->command)) {
        s->error=KUI_GD_ERROR_IO;s->diag.last_error=s->error;
        s->status=KUI_GD_FAILED;s->pending=0u;s->xfer_left=0u;
        s->interrupts|=KUI_RETAIL_GD_IRQ_DRIVE|KUI_RETAIL_GD_IRQ_DMA_END;
    }
}
enum kui_armada_ce_vmu_loan_result kui_retail_async_vmu_loan_begin(
    struct kui_loader_sd *copied_card) {
    uint32_t sr=loan_sr();
    if(sr&UINT32_C(0x100000f0)) return KUI_ARMADA_CE_VMU_LOAN_CONTEXT;
    if(!copied_card) return KUI_ARMADA_CE_VMU_LOAN_FAULT;
    loan_set_sr(sr|0xf0u);
    struct kui_retail_gd *s=&R.shared.service;
    if(e.vmu_loan_state==KUI_ARMADA_CE_VMU_LOAN_QUARANTINED) {
        loan_set_sr(sr);return KUI_ARMADA_CE_VMU_LOAN_FAULT;
    }
    if(loan_blocked() || kui_retail_hook_active) {
        loan_set_sr(sr);return KUI_ARMADA_CE_VMU_LOAN_BUSY;
    }
    kui_retail_hook_active=1u;
    /* A terminal GD handle remains owned until CHECK consumes it. Its token,
     * result and completed output do not require SCI ownership. In particular
     * the CE caller may be waiting for this VMU reply before that CHECK.
     * Keep every pending command and any live/unfilled output excluded. */
    if(!s->initialized || s->pending || s->executing ||
       s->callback_due || kui_retail_ce_deferred[0] || e.active || e.spill_bytes ||
       (e.piece_set && (!e.piece_bytes || e.piece_filled!=e.piece_bytes)) ||
       e.piece_direct || e.in_irq || kui_sci_stream_busy()) {
        kui_retail_hook_active=0u;loan_set_sr(sr);return KUI_ARMADA_CE_VMU_LOAN_BUSY;
    }
    if(R.shared.card.transport!=KUI_STORAGE_SCI || !R.shared.card.device.sd.ready ||
       R.shared.port.fault || e.opened!=(uint32_t)R.shared.port.acquired ||
       e.vmu_loan_generation==UINT32_MAX) {
        loan_fault();kui_retail_hook_active=0u;loan_set_sr(sr);
        return KUI_ARMADA_CE_VMU_LOAN_FAULT;
    }
#if KUI_ARMADA_CE_STREAM
    if(!stream_event(KUI_ARMADA_CE_STREAM_BUS_STORAGE_CLAIM,0)) {
        kui_retail_hook_active=0u;loan_set_sr(sr);return KUI_ARMADA_CE_VMU_LOAN_BUSY;
    }
#endif
    ++e.vmu_loan_generation;
    e.vmu_loan_state=KUI_ARMADA_CE_VMU_LOAN_STOPPING;s->storage_deferred=1u;
    unhook();
    uint32_t opened=e.opened;
    kui_retail_hook_active=0u;
    loan_set_sr(sr);
    /* No receiver was in flight at claim. CMD12's bounded card busy wait
     * runs with interrupts restored. GD calls/SCI IRQ/audio workers observe
     * the loan independently of the now-free GD entry lock. */
    enum kui_sci_stream_result stopped=opened?kui_sci_stream_stop():KUI_SCI_STREAM_OK;
    loan_set_sr(sr|0xf0u);
    if(kui_retail_hook_active) {
        loan_fault();loan_set_sr(sr);return KUI_ARMADA_CE_VMU_LOAN_FAULT;
    }
    kui_retail_hook_active=1u;
    if(e.vmu_loan_state!=KUI_ARMADA_CE_VMU_LOAN_STOPPING ||
       stopped!=KUI_SCI_STREAM_OK || kui_sci_stream_busy() || R.shared.stream.state ||
       R.shared.port.fault || e.opened!=opened ||
       (opened && !R.shared.port.acquired)) {
        loan_fault();kui_retail_hook_active=0u;loan_set_sr(sr);
        return KUI_ARMADA_CE_VMU_LOAN_FAULT;
    }
    kui_sci_stream_discard();
    if(opened) kui_sci_sd_release();
    if(R.shared.port.acquired || R.shared.port.fault) {
        loan_fault();kui_retail_hook_active=0u;loan_set_sr(sr);
        return KUI_ARMADA_CE_VMU_LOAN_FAULT;
    }
    e.opened=0u;
    *copied_card=R.shared.card.device.sd;
    e.vmu_loan_state=KUI_ARMADA_CE_VMU_LOAN_WRITER;
    kui_retail_hook_active=0u;loan_set_sr(sr);
    return KUI_ARMADA_CE_VMU_LOAN_OK;
}
enum kui_armada_ce_vmu_loan_result kui_retail_async_vmu_loan_end(bool healthy) {
    uint32_t sr=loan_sr();
    if(sr&UINT32_C(0x100000f0)) return KUI_ARMADA_CE_VMU_LOAN_CONTEXT;
    loan_set_sr(sr|0xf0u);
    if(e.vmu_loan_state!=KUI_ARMADA_CE_VMU_LOAN_WRITER ||
       kui_retail_hook_active || !healthy || R.shared.port.acquired ||
       R.shared.port.fault || e.opened || kui_sci_stream_busy() || R.shared.stream.state ||
       !R.shared.service.storage_deferred || !e.vmu_loan_generation) {
        loan_fault();kui_retail_ce_vmu_loan_wake();loan_set_sr(sr);
        return KUI_ARMADA_CE_VMU_LOAN_FAULT;
    }
    kui_retail_hook_active=1u;
#if KUI_ARMADA_CE_STREAM
    if(!stream_event(KUI_ARMADA_CE_STREAM_BUS_STORAGE_RELEASE,0)) {
        loan_fault();kui_retail_hook_active=0u;kui_retail_ce_vmu_loan_wake();loan_set_sr(sr);
        return KUI_ARMADA_CE_VMU_LOAN_FAULT;
    }
#endif
    e.vmu_loan_state=KUI_ARMADA_CE_VMU_LOAN_IDLE;
    struct kui_retail_gd *s=&R.shared.service;
    s->storage_deferred=0u;
    /* A request accepted during the loan already owns its real token and
     * copied parameters. Wake its driver; the next GD call starts its cursor
     * in CE's own address space, never on this storage worker's stack. */
    if(s->pending && reads(s->command)) {
        s->interrupts|=KUI_RETAIL_GD_IRQ_DRIVE;
        if(s->command==KUI_RETAIL_GD_DMAREAD_STREAM && s->xfer_left)
            s->interrupts|=KUI_RETAIL_GD_IRQ_DMA_END;
    }
    kui_retail_hook_active=0u;kui_retail_ce_vmu_loan_wake();loan_set_sr(sr);
    return KUI_ARMADA_CE_VMU_LOAN_OK;
}
#endif
void kui_retail_async_call(uint32_t function) {
#if KUI_VMU_NATIVE_BACKGROUND
    if(native_loan_blocked()) return;
#endif
    uint32_t wait = 0;
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    if(loan_blocked()) return;
    struct kui_retail_gd *s=&R.shared.service;
    if(s->pending && reads(s->command) && e.token!=s->token) {
        start();
        if(s->command==KUI_RETAIL_GD_DMAREAD_STREAM && s->xfer_left)
            piece(s->completed_bytes,s->xfer_left,
                (s->xfer_destination&0x1fffffffu)|0x80000000u,NULL,true);
    }
#endif
#ifdef KUI_RETAIL_CE
    install();
    kui_sci_stream_token_budget(e.isr && function != KUI_GD_PIO_TRANSFER);
#endif
    if(e.active) {
        hook();
#if !KUI_RETAIL_NATIVE_HYBRID
        if(function == KUI_GD_EXEC) {
            ++e.stats.execs;
            if(kui_retail_hook_sr & 0xf0u) ++e.stats.exec_int; /* IMASK: a handler */
        }
#endif
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
#if KUI_VMU_NATIVE_BACKGROUND
    if(native_loan_blocked()) return;
#endif
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    if(loan_blocked()) {
        if(e.vmu_loan_state==KUI_ARMADA_CE_VMU_LOAN_QUARANTINED) loan_fault();
        return;
    }
#endif
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
        e.piece_set = 0; e.spill_from = e.spill_bytes = 0; e.piece_direct = NULL;
        e.early_polled = 0;
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
    if(kui_sci_stream_token_pending() ||
       (!interrupt_fills() && (topping(function) || function == KUI_GD_DMA_TRANSFER))) wake();
#if KUI_ARMADA_CE_STREAM
    (void)stream_event(KUI_ARMADA_CE_STREAM_BUS_IDLE,0);
#endif
#endif
}
uint32_t kui_retail_async_irq(void) {
#if KUI_NATIVE_IRQ_REARM
    ++e.stats.exec_int;
#endif
#if KUI_VMU_NATIVE_BACKGROUND
    if(native_loan_blocked()) return 1u;
#endif
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    if(loan_blocked()) return 1u;
#endif
#if KUI_ARMADA_CE_STREAM
    if(stream_event(KUI_ARMADA_CE_STREAM_BUS_IRQ,0)) return 0;
#endif
    /* An SCI event without a reception: not ours, passed on as any other
     * (under CE: its level is dropped). */
    if(!kui_sci_stream_busy()
#if KUI_NATIVE_COOP_PUMP && !KUI_NATIVE_PACED_CHECK && !KUI_NATIVE_IRQ_REARM
       && e.in_irq!=2u /* A cooperative visit also restarts an idle run. */
#endif
      ) {
#ifdef KUI_RETAIL_CE
        unhook();
#endif
        return 1;
    }
#ifdef KUI_RETAIL_CE
    kui_sci_stream_token_budget(e.isr != 0);
#endif
#if KUI_NATIVE_COOP_PUMP && !KUI_NATIVE_PACED_CHECK && !KUI_NATIVE_IRQ_REARM
    if(e.in_irq==2u) hook();
#endif
#if KUI_NATIVE_COOP_PUMP && !KUI_NATIVE_PACED_CHECK && !KUI_NATIVE_IRQ_REARM
    if(!e.in_irq) e.in_irq = 1;
#else
    e.in_irq = 1;
#endif
    deliver(0);
    e.in_irq = 0;
#ifdef KUI_RETAIL_CE
    report();
    /* The stream stopped short of the read (a block for a virtual piece, or
     * one to resume later): CE's driver calls for the rest. */
    if((e.active || e.spill_bytes) && !kui_sci_stream_busy()) wake();
#endif
    if(!kui_sci_stream_busy()) unhook();
    return 0;
}
#ifdef KUI_RETAIL_CE
int kui_retail_async_read_part(void *unused, uint32_t lba, uint32_t sector_bytes,
                               uint32_t skip, uint32_t bytes, void *output) {
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    if(loan_blocked()) return -1;
#endif
    (void)unused; (void)lba; (void)sector_bytes;
    struct kui_retail_gd *s = &R.shared.service;
    if(e.token != s->token) return -1;
    ++e.pio_calls;
    e.pio_bytes += bytes;
    uint8_t bounded = R.shared.stream.token_bounded;
    R.shared.stream.token_bounded = 0; /* synchronous PIO must fill its piece */
    piece(skip, bytes, 0, output, false);
    if(!e.failed && e.piece_filled < bytes) deliver(UINT32_MAX);
    R.shared.stream.token_bounded = bounded;
    bool done = !e.failed && e.piece_filled == bytes;
    close_piece();
    return done ? 0 : -1;
}
#endif
