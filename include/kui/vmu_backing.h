/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_BACKING_H
#define KUI_VMU_BACKING_H
#include <stdbool.h>
#include "kui/vmu_store.h"
#if KUI_VMU_EXPERIMENTAL && (KUI_RETAIL_ASYNC || KUI_RETAIL_CE || KUI_RETAIL_TOY_PILOT)
#error "Experimental VMU backing supports only the synchronous native reader"
#endif

struct kui_loader_sd;
struct kui_loader_sd_stream;

struct kui_vmu_sd_identity {
    uint8_t cid[16];
    uint64_t sectors;
    bool high_capacity;
};
/* Caller already owns the bus, card is initialized, and no read stream is
 * active. Read-only CMD10 with CRC checking. Never initializes/rebinds a card.
 * Capturing this before unmount and comparing it after handoff prevents a
 * different card with the same capacity from receiving the old file map. */
int kui_vmu_sd_identity_read(struct kui_loader_sd *,struct kui_vmu_sd_identity *);

/* Every callback and object must live in the freestanding resident. enter
 * atomically reserves the SAME lock as the game reader and masks IRQs; false
 * means busy without I/O. leave restores the exact previous interrupt state.
 * No callback may use KOS, FatFs, allocation, or the departed shell.
 * acquire/release borrow only the existing initialized card's SCI bus.
 * A successful acquire is always paired with release.
 *
 * If a standard CMD18 reader lease can survive until enter, supply its stream
 * and lease flag. The backing explicitly stops the stream and returns that
 * lease BEFORE acquiring its own. A stop failure poisons the session. Async,
 * CDDA and WinCE ownership are deliberately unsupported by this interface. */
struct kui_vmu_backing_bus {
    void *context;
    struct kui_loader_sd *card;
    bool (*enter)(void *);
    void (*leave)(void *);
    int (*acquire)(void *); /* KUI_LOADER_SD_OK (zero) on success. */
    void (*release)(void *);
    bool (*healthy)(void *); /* Optional bus fault latch, sampled under lock. */
    struct kui_loader_sd_stream *reader_stream;
    bool *reader_lease;
};
struct kui_vmu_backing_config {
    struct kui_vmu_sd_identity identity;
    const struct kui_vmu_store_config *store;
    uint64_t partition_first,partition_sectors;
    /* Optional game/other protected allocations, checked before any I/O. */
    const struct kui_vmu_store_extent *forbidden;
    size_t forbidden_count;
    bool allow_writes;
};
enum kui_vmu_backing_result {
    KUI_VMU_BACKING_OK=0,
    KUI_VMU_BACKING_ARGUMENT=-1,
    KUI_VMU_BACKING_MAPPING=-2,
    KUI_VMU_BACKING_IDENTITY=-3,
    KUI_VMU_BACKING_BUSY=-4,
    KUI_VMU_BACKING_IO=-5,
    KUI_VMU_BACKING_READ_ONLY=-6,
    KUI_VMU_BACKING_POISONED=-7
};
/* Private work state exposed for static allocation only. Mapping and expected
 * card identity are copied; no path or filesystem object survives handoff.
 * Treat all fields as immutable after successful init except by these APIs. */
struct kui_vmu_backing {
    struct kui_vmu_backing_bus bus;
    struct kui_vmu_sd_identity identity;
    struct kui_vmu_store_extent allowed[KUI_VMU_STORE_MAX_EXTENTS*2u];
    size_t allowed_count;
    int last_result,last_sd_result;
    bool initialized,allow_writes,poisoned,inflight,reserved,callback_active;
};
/* No I/O. Validates exact 128 KiB image + two-sector journal, card/partition
 * bounds, all pairwise overlaps, and protected ranges before copying maps.
 * A poisoned instance has no reset/retry API; rebuild it after controlled card
 * reinitialization and journal recovery in the shell, never resume the game. */
int kui_vmu_backing_init(struct kui_vmu_backing *,
    const struct kui_vmu_backing_bus *,const struct kui_vmu_backing_config *);
struct kui_vmu_store_io kui_vmu_backing_io(struct kui_vmu_backing *);
int kui_vmu_backing_read(void *,uint64_t absolute_lba,void *sector);
int kui_vmu_backing_write(void *,uint64_t absolute_lba,const void *sector);
int kui_vmu_backing_sync(void *);
/* Optional synchronous transaction ownership. Reserve BEFORE entering the
 * journal store, so initial BUSY can become a Maple retry without touching the
 * store. Successful reserve keeps the shared lock, IRQ mask and SCI lease
 * until unreserve; all sector callbacks reuse that ownership. Each callback
 * still validates CID/card state/bounds. No unrelated reader may run during
 * this interval. Do not hot-swap media or call from another execution context.
 *
 * Always unreserve after success OR failure. It releases exactly once, never
 * clears poison, and checks final health. A game bridge may acknowledge a
 * save only after BOTH store_write and unreserve succeed. A nested reserve,
 * callback, or unreserve during an active callback returns BUSY without I/O.
 * Holding IRQs across SD programming can stall the game for the bounded card
 * timeout; this API is an experimental building block, not a latency claim. */
int kui_vmu_backing_reserve(struct kui_vmu_backing *);
int kui_vmu_backing_unreserve(struct kui_vmu_backing *);
/* Writes require BOTH KUI_VMU_EXPERIMENTAL=1 at compile time and allow_writes
 * at init. Default builds cannot issue CMD24/CMD25 through this backend.
 * sync uses the existing CMD13/busy completion implementation. A successful
 * sector write finishes programming/status before returning; failures poison
 * this instance and card.ready, without retries or card reinitialization. */
#endif
