/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_VMU_A2_PUBLISH_H
#define KUI_ARMADA_CE_VMU_A2_PUBLISH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KUI_ARMADA_CE_VMU_A2_PUBLISH_MAGIC UINT32_C(0x31523241) /* A2R1 */
#define KUI_ARMADA_CE_VMU_A2_PUBLISH_REPLY_BYTES 524u
#ifndef KUI_ARMADA_CE_MAPLE_WRITE_STAGING
#define KUI_ARMADA_CE_MAPLE_WRITE_STAGING 0
#endif
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING != 0 && KUI_ARMADA_CE_MAPLE_WRITE_STAGING != 1
#error "KUI_ARMADA_CE_MAPLE_WRITE_STAGING must be0 or1"
#endif
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
#define KUI_ARMADA_CE_VMU_A2_REQUEST_BYTES 140u
#define KUI_ARMADA_CE_VMU_A2_WRITE_REQUEST_OFFSET 384u
#else
#define KUI_ARMADA_CE_VMU_A2_REQUEST_BYTES 12u
#endif

typedef int (*kui_armada_ce_vmu_a2_backend)(void *,const uint8_t *,size_t,
    uint8_t *,size_t,size_t *);
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
/* Private backend status, never a Maple reply. Only an admitted READ or
 * phase4 SYNC may return DEFERRED, and response_bytes must be exactlyzero. */
enum kui_armada_ce_vmu_a2_backend_result {
    KUI_ARMADA_CE_VMU_A2_BACKEND_DEFERRED=2
};
#endif
/* Repeats source-owned live-slice/mapping/header and held-idle-gate witnesses
 * after complete backend staging, immediately before the first RX store. */
typedef bool (*kui_armada_ce_vmu_a2_admit)(void *);
/* The runtime callback independently limits stores to its currently admitted
 * RX slices, uses aligned P2 stores, and confirms each write by readback.
 * False may mean the store happened but its confirmation failed. */
typedef bool (*kui_armada_ce_vmu_a2_store32)(void *,uint32_t,uint32_t);

struct kui_armada_ce_vmu_a2_parent {
    uint32_t request_header,response_header,response_function,rx_bus,capacity;
    uint32_t request_function; /* GETCOND payload; ignored for zero-word info */
};
struct kui_armada_ce_vmu_a2_target {
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    /* Keep the always-cleared fields and first12 request bytes in the same
     * 80-byte input prefix. Larger payload bytes are filled only for WRITE. */
    uint32_t request_bytes,response_header,rx_bus,capacity;
    uint8_t request[KUI_ARMADA_CE_VMU_A2_REQUEST_BYTES];
#else
    uint8_t request[12];
    uint32_t request_bytes,response_header,rx_bus,capacity;
#endif
};
struct kui_armada_ce_vmu_a2_input {
    uint32_t pool_bus,pool_bytes,tx_bus,tx_bytes,protected_bus,protected_bytes;
    struct kui_armada_ce_vmu_a2_parent parent;
    uint32_t target_present;
    struct kui_armada_ce_vmu_a2_target target;
};
/* Caller-owned private BSS, not a CE stack or source/RX allocation. */
struct kui_armada_ce_vmu_a2_publish_scratch {
    uint32_t reply[KUI_ARMADA_CE_VMU_A2_PUBLISH_REPLY_BYTES/4u];
    uint8_t request[12];
};
_Static_assert(sizeof(struct kui_armada_ce_vmu_a2_publish_scratch)==536u,
    "A2 publisher retains one bounded reply and request");
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
_Static_assert(KUI_ARMADA_CE_VMU_A2_WRITE_REQUEST_OFFSET+KUI_ARMADA_CE_VMU_A2_REQUEST_BYTES==
    KUI_ARMADA_CE_VMU_A2_PUBLISH_REPLY_BYTES,"Private WRITE request ends at reply tail");
_Static_assert(offsetof(struct kui_armada_ce_vmu_a2_input,target.request)+12u==80u,
    "WRITE staging preserves the bounded input zero prefix");
#endif

/* Six rows, all saturating counters. counts: magic, visits, completions,
 * advertisements, target replies. commands: DEVINFO, ALLINFO, MINFO, READ,
 * finalization (read, or enabled write commit). parent: request, original response, RX bus, capacity,
 * function. target: request, generated response, RX bus, capacity, reply bytes.
 * status: refusals, backend calls, confirmed stores, historical reason bits,
 * store attempts in the latest call. fault: FIRST reason, at, actual, expect,
 * detail. Historical failure blocks future publication until fresh init. */
struct kui_armada_ce_vmu_a2_publish_snapshot {
    uint32_t counts[5],commands[5],parent[5],target[5],status[5],fault[5];
};
_Static_assert(sizeof(struct kui_armada_ce_vmu_a2_publish_snapshot)==120u,
    "A2 publisher report is six rows");
enum kui_armada_ce_vmu_a2_publish_result {
    KUI_ARMADA_CE_VMU_A2_PUBLISH_OK=0,
    KUI_ARMADA_CE_VMU_A2_PUBLISH_ARGUMENT=1,
    KUI_ARMADA_CE_VMU_A2_PUBLISH_STATE=2,
    KUI_ARMADA_CE_VMU_A2_PUBLISH_SPAN=3,
    KUI_ARMADA_CE_VMU_A2_PUBLISH_OVERLAP=4,
    KUI_ARMADA_CE_VMU_A2_PUBLISH_PARENT=5,
    KUI_ARMADA_CE_VMU_A2_PUBLISH_CONFLICT=6,
    KUI_ARMADA_CE_VMU_A2_PUBLISH_REQUEST=7,
    KUI_ARMADA_CE_VMU_A2_PUBLISH_BACKEND=8,
    KUI_ARMADA_CE_VMU_A2_PUBLISH_REPLY=9,
    KUI_ARMADA_CE_VMU_A2_PUBLISH_STORE=10,
    KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION=11,
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    KUI_ARMADA_CE_VMU_A2_PUBLISH_PENDING=12
#endif
};

/* Pure publication policy for an already-held, source-specific CE gate. The
 * caller must prove and repeat the live unit/function/request/RX ownership,
 * requested capacities, uncached mappings and stable original bytes while
 * interrupts are masked and automatic DMA is disabled/idle. This function
 * cannot establish those facts from an RX bus address. In particular, a
 * pool-contained address alone is never sufficient admission.
 *
 * The mandatory final admit callback runs after complete reply staging and
 * before the first guest store, including parent-only advertisement. False
 * latches ADMISSION and publishes no byte. It may not block or alter source.
 * Only parent A-controller bytes and one optional hardware-timeout A2 reply
 * are writable. Parent accepts DEVINFO, ALLINFO or GETCOND; target admits
 * metadata/read requests and exact read-finalization shapes, with the backend
 * proving matching prior-read state for finalization. WRITE/RESET/KILL/LCD
 * never reach it. All validation and complete reply staging precede stores.
 * Target payload publishes before its header; the parent's A2 bit is ORed
 * last, preserving physical A1 and every other original header bit.
 *
 * A store error can leave confirmed earlier payload words in guest memory;
 * there is no rollback claim. Its first fault is latched and no later call
 * can write. Before the target header is attempted, the target remains the
 * original hardware timeout. Backend state may advance before a later fault.
 * No TX, MMIO, payload reads from source RAM, storage or CE API occurs here. */
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
/* Explicit integration prototype only; this flag selects no storage provider
 * and changes no production build/reservation. Exact140-byte WRITE phases and
 * phase4SYNC additionally pass the bounded backend contract. Their immutable
 * request and reply prefix are disjoint; ACK/AGAIN are4 bytes. A canonical
 * private FILEERR8 may publish its unchanged header only for exact CMD12 with
 * requested capacity4, matching the audited native truncation; all other
 * responses must fit the proven guest slice. Both pre-backend and final admissions are
 * mandatory for writes. A backend may only stage/enqueue private work while
 * this Maple fence is held; actual provider I/O belongs outside the fence.
 *
 * Private backend DEFERRED with zero produced bytes is accepted only for READ
 * or phase4 SYNC after final admission. PENDING publishes no guest byte, sets
 * no fault and records no completed reply/advertisement. Caller MUST withhold
 * the original consumer, release the fence, service the worker unmasked, then
 * reacquire a fresh complete owner/query proof before calling again. This is
 * not a native IRP hold implementation. Ordinary backend AGAIN remains a
 * visible Maple response/error; it is never globally translated to PENDING. */
#endif
enum kui_armada_ce_vmu_a2_publish_result kui_armada_ce_vmu_a2_publish(
    const struct kui_armada_ce_vmu_a2_input *,kui_armada_ce_vmu_a2_backend,void *,
    kui_armada_ce_vmu_a2_admit,void *,kui_armada_ce_vmu_a2_store32,void *,
    struct kui_armada_ce_vmu_a2_publish_scratch *,
    struct kui_armada_ce_vmu_a2_publish_snapshot *);
#endif
