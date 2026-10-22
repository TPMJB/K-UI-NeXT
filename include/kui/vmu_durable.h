/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_DURABLE_H
#define KUI_VMU_DURABLE_H
#include "kui/vmu_emulation.h"
#include "kui/vmu_store.h"
#include "kui/vmu_sd_lease.h"

#define KUI_VMU_DURABLE_MAGIC UINT32_C(0x34444d56)
#define KUI_VMU_DURABLE_VERSION 1u
#define KUI_VMU_DURABLE_CLEAN UINT32_C(0x434c4e34)
#define KUI_VMU_DURABLE_HANDOFF_BYTES 1272u
struct kui_vmu_durable_extent {uint32_t lba,blocks;};
/* Native little-endian stage/worker handoff, not an on-card profile. The
 * stage may seal it ONLY after fresh ownership admission, real-map recovery,
 * VMU validation, explicit successful checkpoint and final identity/health.
 * The worker independently requires an exactlyzero marker before binding.
 * No filesystem path, FS object, resident card pointer or lease survives. */
struct kui_vmu_durable_handoff {
    uint32_t magic,version,bytes,clean_checkpoint;
    uint64_t card_sectors;
    uint32_t high_capacity,base_count,journal_count;
    uint8_t cid[16],image_id[16];
    struct kui_vmu_durable_extent base[16],journal[2];
    uint32_t block_crc[256];
    uint8_t base_sha256[32];
    uint32_t seal;
};
_Static_assert(sizeof(struct kui_vmu_durable_handoff)==KUI_VMU_DURABLE_HANDOFF_BYTES,
    "Durable compact handoff ABI");
int kui_vmu_durable_handoff_seal(struct kui_vmu_durable_handoff *);
int kui_vmu_durable_handoff_validate(const struct kui_vmu_durable_handoff *);

enum kui_vmu_durable_result {
    KUI_VMU_DURABLE_HANDLED=1,KUI_VMU_DURABLE_REFUSED=0,
    KUI_VMU_DURABLE_ARGUMENT=-1,KUI_VMU_DURABLE_CAPACITY=-2,
    KUI_VMU_DURABLE_INTERNAL=-3
};
enum kui_vmu_durable_sd_result {
    KUI_VMU_DURABLE_SD_OK=0,KUI_VMU_DURABLE_SD_CONTEXT=1,
    KUI_VMU_DURABLE_SD_IDENTITY=2,KUI_VMU_DURABLE_SD_READ=3,
    KUI_VMU_DURABLE_SD_CRC=4,KUI_VMU_DURABLE_SD_STORE=5,
    KUI_VMU_DURABLE_SD_BUSY=6,KUI_VMU_DURABLE_SD_LEASE=7,
    KUI_VMU_DURABLE_SD_MARKER=8,KUI_VMU_DURABLE_SD_MAPPING=9
};
/* Static allocation only. Zero once, never move/copy/reset/reinitialize a
 * started card. Every new card needs a new stage checkpoint/handoff. Private
 * work fields are public only for static placement; never edit them. The
 * copied handoff's CRCs become current runtime CRCs after commits; its seal
 * is then cleared because the original stage snapshot is no longer current.
 * No loan/card pointer is retained outside a transaction callback. */
struct kui_vmu_durable {
    const struct kui_vmu_durable *self;
    struct kui_vmu_durable_handoff handoff;
    struct kui_vmu_store_extent maps[18];
    struct kui_vmu_store store;
    kui_vmu_emulation_t protocol;
    int (*lease)(kui_vmu_sd_transaction,void *);
    kui_vmu_sd_transaction transaction_entry;
    _Alignas(32) uint8_t bounce[512];
    uint32_t started,ready,faulted,bound,handling,inflight,invoked,verified,bound_candidate;
    uint32_t operation,block,pending_crc;
    uint32_t requests,write_phases,commits,reads,refused,commit_attempts,last_block;
    uint32_t sd_reads,sd_busy,sd_faults,cid_checks,last_lba,crc_expected,crc_actual;
    int sd_last_result,last_sd_result,last_store_result,last_lease_result;
};
_Static_assert(sizeof(struct kui_vmu_durable)<=4608u,"Durable worker RAM bound");
/* No I/O. Both functions and the card must remain in the admitted worker.
 * transaction_entry is the EXACT exported callback pinned by the stage, which
 * checks its context equals this card and forwards to transaction below. */
int kui_vmu_durable_init(struct kui_vmu_durable *,const struct kui_vmu_durable_handoff *,
    int (*lease)(kui_vmu_sd_transaction,void *),kui_vmu_sd_transaction transaction_entry);
/* Sole scoped callback body. Caller holds the existing initialized SCI loan
 * and shared GD lock throughout; never calls begin/end/init/releases/IRQ APIs.
 * The borrowed store context is stack-local and cleared before return. */
int kui_vmu_durable_transaction(void *context,struct kui_loader_sd *loan);
/* Strict A1 command whitelist and exact shapes, matching the sanitized chain.
 * A faulted card cannot admit another physical schedule. No I/O/state staging. */
int kui_vmu_durable_admit(struct kui_vmu_durable *,uint32_t header,uint32_t payload0,uint32_t payload1);
/* Same frame convention as emulation. C stages only in RAM; B and final D
 * each borrow ONE complete external transaction. Only release success copies
 * read bytes or publishes a new CRC/commit. Pre-acquisition BUSY gives AGAIN;
 * acquired errors latch forever and yield FILEERR. Write/SYNC need8B output
 * capacity for error responses even though successful ACKs occupy4B.
 * All caller buffers must be disjoint from the card and each other. */
int kui_vmu_durable_handle(struct kui_vmu_durable *,const uint8_t *,size_t,
    uint8_t *,size_t,size_t *);
#endif
