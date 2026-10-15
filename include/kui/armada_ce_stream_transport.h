/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_STREAM_TRANSPORT_H
#define KUI_ARMADA_CE_STREAM_TRANSPORT_H
#include "kui/retail_image.h"
#include "sci_stream.h"
#include <stdbool.h>
#include <stdint.h>

#define KUI_ARMADA_CE_STREAM_BUS_EVENT UINT32_C(0xfffffffe)
#define KUI_ARMADA_CE_STREAM_BUS_VERSION 1u
/* These visits run masked, without CE APIs. A DATA fence waits for at most
 * the singleton's one in-flight card block, never an audio request. */
enum kui_armada_ce_stream_bus_event {
    KUI_ARMADA_CE_STREAM_BUS_ATTACH,
    KUI_ARMADA_CE_STREAM_BUS_DATA_ENTER,
    KUI_ARMADA_CE_STREAM_BUS_DATA_BLOCK,
    KUI_ARMADA_CE_STREAM_BUS_DATA_TAKE,
    KUI_ARMADA_CE_STREAM_BUS_IRQ,
    KUI_ARMADA_CE_STREAM_BUS_IDLE,
    KUI_ARMADA_CE_STREAM_BUS_DATA_PREFETCH
};
struct kui_retail_async_region;
struct kui_armada_ce_stream_bus {
    uint32_t version, bytes;
    struct kui_retail_async_region *region;
    bool (*open)(void);
    void (*hook)(void), (*unhook)(void), (*wake)(void);
    bool (*busy)(void), (*ready)(uint32_t);
    enum kui_sci_stream_result (*poll)(bool), (*wait)(void);
    enum kui_sci_stream_result (*fetch)(uint32_t,uint32_t,bool);
    const uint8_t *(*take)(uint32_t,enum kui_sci_stream_result *);
    void (*discard)(void);
    void (*token_budget)(bool);
};
/* DATA_TAKE supplies a saved checked block to this short-lived low frame.
 * result is a SCI result; copied==0 leaves the ordinary take path intact.
 * The block remains valid until the caller feeds it and visits DATA_BLOCK. */
struct kui_armada_ce_stream_data_take {
    uint32_t lba, result, copied;
    const uint8_t *block;
};
enum kui_armada_ce_stream_transport_status {
    KUI_ARMADA_CE_STREAM_TRANSPORT_IDLE,
    KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING,
    KUI_ARMADA_CE_STREAM_TRANSPORT_DONE,
    KUI_ARMADA_CE_STREAM_TRANSPORT_IO,
    KUI_ARMADA_CE_STREAM_TRANSPORT_CANCELLED,
    KUI_ARMADA_CE_STREAM_TRANSPORT_ARGUMENT,
    KUI_ARMADA_CE_STREAM_TRANSPORT_BUSY,
    KUI_ARMADA_CE_STREAM_TRANSPORT_STALE
};
struct kui_armada_ce_stream_transport_snapshot {
    uint32_t epoch,status,bytes,total,blocks,retries,data_blocks,audio_blocks;
    uint32_t holds,preemptions,owner,attached;
};
/* Configure once using the fully validated immutable complete map. Tests
 * may supply an ordinary host span; the console accepts only its private
 * source bank or reserved work bank. No storage acquisition occurs here. */
bool kui_armada_ce_stream_transport_init(const struct kui_retail_manifest *,
    uintptr_t source_begin,uintptr_t source_end);
uint32_t kui_armada_ce_stream_transport_event(uint32_t event,uintptr_t argument);
/* Read-only bootstrap gate. Attachment is authorized by the stage only
 * after the controller's code, zero state and source pool are verified. */
bool kui_armada_ce_stream_transport_attached(void);
/* Masked authored-state update only. Enter urgency at one second of fresh
 * PCM, leave at1.5 seconds, and clear when inactive. A changed priority
 * applies at the next AUDIO grant; DATA's16-block allowance and its
 * synchronous one-reception preemption remain unchanged. */
void kui_armada_ce_stream_transport_priority(uint32_t fresh_bytes,bool active);
bool kui_armada_ce_stream_transport_urgent(void);
/* Masked state query for bounded worker visits: a pending AUDIO lease has
 * no reception in flight. The next pump can consume a ready block or
 * continue a sliced token search. Does not poll, fetch, copy or wait. */
bool kui_armada_ce_stream_transport_worker_can_pump(void);
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_submit(
    uint32_t epoch,uint32_t raw_lba,uint32_t sectors,uintptr_t destination,uint32_t capacity);
/* At most one received block is copied and one reception started per pump.
 * The caller supplies its short IRQ-masked integer-only bridge. */
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_pump(void);
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_snapshot(
    uint32_t epoch,struct kui_armada_ce_stream_transport_snapshot *);
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_cancel(uint32_t epoch);
#ifdef KUI_ARMADA_CE_STREAM_TRANSPORT_TEST
void kui_armada_ce_stream_transport_test_reset(void);
#endif
#endif
