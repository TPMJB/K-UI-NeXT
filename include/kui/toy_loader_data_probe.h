/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_LOADER_DATA_PROBE_H
#define KUI_TOY_LOADER_DATA_PROBE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kui/toy_loader_trace.h"

#ifndef KUI_TOY_PILOT_DATA_PROBE
#define KUI_TOY_PILOT_DATA_PROBE 0
#endif
#if KUI_TOY_PILOT_DATA_PROBE != 0 && KUI_TOY_PILOT_DATA_PROBE != 1
#error Toy loader DATA probe must be 0 or 1
#endif
#if KUI_TOY_PILOT_DATA_PROBE && !KUI_TOY_PILOT_LOADER_TRACE
#error Toy loader DATA probe requires retained loader trace
#endif

#define KUI_TOY_LOADER_DATA_PROBE_MAGIC UINT32_C(0x4c445031) /* LDP1 */
#define KUI_TOY_LOADER_DATA_PROBE_VERSION 2u
#define KUI_TOY_LOADER_DATA_PROBE_WORDS 192u
#define KUI_TOY_LOADER_DATA_PROBE_PHASE_WORDS 88u
#define KUI_TOY_LOADER_DATA_PROBE_DMA_ATTRIBUTION UINT32_C(1)
#define KUI_TOY_LOADER_DATA_PROBE_CACHED_PAYLOAD UINT32_C(2)
#define KUI_TOY_LOADER_DATA_PROBE_PIO_PAYLOAD UINT32_C(4)
#ifndef KUI_TOY_PILOT_DATA_PAYLOAD_MODE
#define KUI_TOY_PILOT_DATA_PAYLOAD_MODE 0
#endif
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE < 0 || KUI_TOY_PILOT_DATA_PAYLOAD_MODE > 2
#error Toy loader DATA payload mode must be 0, 1 or 2
#endif
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE && !KUI_TOY_PILOT_DATA_PROBE
#error Toy loader DATA payload control requires DATA probe
#endif
typedef int (*kui_toy_loader_data_probe_read_fn)(void *,uint32_t,uint32_t,uint32_t,void *);
typedef bool (*kui_toy_loader_data_probe_block_fn)(void *,const uint8_t *,uint8_t *,size_t,bool,uint16_t *);
struct kui_retail_storage;
struct kui_sci_sd_diagnostic;
struct kui_toy_loader_data_probe_metric {
    uint32_t samples,ticks_total,ticks_max,ticks_min;
};
struct kui_toy_loader_data_probe_phase {
    uint32_t data_visits,read_calls,read_ok,read_failed;
    uint32_t payload_calls,payload_ok,payload_failed,payload_bytes;
    uint32_t no_payload_reads,invalid_intervals,reentrant_reads,reentrant_payload;
    uint32_t callback_conflicts,nonstandard_payload,dma_delta_invalid,dma_counter_wraps;
    struct kui_toy_loader_data_probe_metric read_body,first_payload_gap,payload_body;
    struct kui_toy_loader_data_probe_metric inter_payload_gap,tail_gap;
    uint32_t sr_buckets[32]; /* (saved SR.BL ? 16 : 0) + saved SR.IMASK. */
    uint32_t worst_read[8]; /* token,request LBA,chunk LBA,count,SR,caller PR,ticks,blocks */
    uint32_t dma_started,dma_payload_ok,pio_fallback,prestart_failed;
    struct kui_toy_loader_data_probe_metric dma_payload_body,pio_payload_body;
};
struct kui_toy_loader_data_probe_report {
    uint32_t magic,version,words,phase_words,tick_hz,frozen,saturated;
    uint32_t unmatched_end,nested_begin,feature_flags,payload_mode;
    uint32_t payload_attempts,payload_declines,payload_publications,payload_failed,reserved;
    struct kui_toy_loader_data_probe_phase phase[2];
};
_Static_assert(sizeof(struct kui_toy_loader_data_probe_metric)==16u,"DATA probe metric ABI");
_Static_assert(offsetof(struct kui_toy_loader_data_probe_phase,read_body)==16u*4u,"DATA probe metrics ABI");
_Static_assert(offsetof(struct kui_toy_loader_data_probe_phase,sr_buckets)==36u*4u,"DATA probe SR ABI");
_Static_assert(offsetof(struct kui_toy_loader_data_probe_phase,worst_read)==68u*4u,"DATA probe context ABI");
_Static_assert(offsetof(struct kui_toy_loader_data_probe_phase,dma_delta_invalid)==14u*4u,"DATA probe attribution status ABI");
_Static_assert(offsetof(struct kui_toy_loader_data_probe_phase,dma_started)==76u*4u,"DATA probe attribution count ABI");
_Static_assert(offsetof(struct kui_toy_loader_data_probe_phase,dma_payload_body)==80u*4u,"DATA probe DMA metric ABI");
_Static_assert(offsetof(struct kui_toy_loader_data_probe_phase,pio_payload_body)==84u*4u,"DATA probe PIO metric ABI");
_Static_assert(sizeof(struct kui_toy_loader_data_probe_phase)==88u*4u,"DATA probe phase ABI");
_Static_assert(offsetof(struct kui_toy_loader_data_probe_report,phase)==16u*4u,"DATA probe header ABI");
_Static_assert(offsetof(struct kui_toy_loader_data_probe_report,feature_flags)==9u*4u,"DATA probe feature ABI");
_Static_assert(offsetof(struct kui_toy_loader_data_probe_report,payload_attempts)==11u*4u,"DATA probe control count ABI");
_Static_assert(sizeof(struct kui_toy_loader_data_probe_report)==192u*4u,"DATA probe report ABI");

/* Serialized, already-masked high adapter only. DATA-owned EXEC/CHECK visits
 * sample original caller SR and PR; only the one cooked (2048-byte) read per
 * EXEC temporarily replaces a known callback. Raw audio never enters it.
 * The block callback is restored immediately after original read returns;
 * ops.read is restored at wrapper entry, or by end/freeze if never called.
 * Callback conflicts preserve the unexpected callback and decline probing.
 * No guest mapping, extra storage I/O, timer writes, IRQ changes or SDK calls.
 *
 * Read-body and its first/payload/inter/tail partition include observer cost.
 * Payload spans the selected high wrapper and original transfer_block, including setup, transfer, bit
 * reversal and computed CRC, but not on-wire CRC tail bytes. First/inter/tail
 * gaps also include command/token waits, copy and cleanup; they do not isolate
 * pure wire latency or physical setup/cleanup. All counts/totals saturate.
 * Version 2 preserves all version-1 timing/count offsets. Header feature bits
 * are DMA attribution (1), cached payload mode (2), and PIO payload mode (4).
 * Header word 10 is the selected mode (0/1/2); words 11..14 are the selected
 * wrapper's attempts, declines, publications and failed calls, captured only
 * at freeze (zero beforehand); word 15 is zero. Any helper counter at its
 * UINT32_MAX ceiling conservatively marks the frozen report saturated.
 * DMA attribution reads the retained low diagnostic counters without writing
 * them. Modular deltas classify only standard cooked payloads: (1,1,0) and
 * true is DMA success; (1,0,0) and false is DMA failure; (0,0,1) is programmed
 * fallback; (0,0,0) and false is pre-start failure. Other triples, reentrancy
 * and invalid intervals are rejected. DMA success is computed payload CRC,
 * not validation against the on-wire CRC. DMA/PIO spans include failed calls;
 * accepted wrapped low counters add one wrap per counter, not per payload.
 * Phase matches the separate trace's first accepted PLAY-mailbox boundary.
 * Selected payload modes stop permanently at that boundary: the next
 * dispatch's begin sees phase 1 and freezes/restores the observer before
 * any operation runs. The accepting PLAY call has no cooked payload.
 * Mode zero continues collecting both phases. A rejected PLAY does not stop
 * the selected mode; hardware mailbox acceptance is not audible-start proof.
 */
void kui_toy_loader_data_probe_begin(struct kui_retail_gd *,uint32_t function,uint32_t phase);
void kui_toy_loader_data_probe_end(struct kui_retail_gd *);
const uint32_t *kui_toy_loader_data_probe_words(void);
uint32_t kui_toy_loader_data_probe_word_count(void);
void kui_toy_loader_data_probe_freeze(void);
#ifdef KUI_TOY_LOADER_DATA_PROBE_HOST_TEST
void kui_toy_loader_data_probe_host_bind(struct kui_retail_storage *,
    const volatile uint32_t *sr,const volatile uint32_t *caller,
    kui_toy_loader_data_probe_read_fn,kui_toy_loader_data_probe_block_fn);
void kui_toy_loader_data_probe_host_bind_diagnostic(const volatile struct kui_sci_sd_diagnostic *);
uint32_t kui_toy_loader_data_probe_host_read(uint32_t address,unsigned bytes);
void kui_toy_loader_data_probe_host_reset(void);
#endif
#endif
