/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_LOADER_PAYLOAD_CONTROL_H
#define KUI_TOY_LOADER_PAYLOAD_CONTROL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef KUI_TOY_PILOT_DATA_PAYLOAD_MODE
#define KUI_TOY_PILOT_DATA_PAYLOAD_MODE 0
#endif
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE < 0 || KUI_TOY_PILOT_DATA_PAYLOAD_MODE > 2
#error DATA payload mode must be zero, cached image, or programmed control
#endif
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE && (!KUI_TOY_PILOT_DATA_PROBE || \
    !KUI_TOY_PILOT_LOADER_TRACE || !KUI_TOY_PILOT_PRIVATE_P2)
#error DATA payload controls require the retained private P2 DATA probe
#endif

typedef bool (*kui_toy_loader_payload_fn)(void *,const uint8_t *,uint8_t *,size_t,bool,uint16_t *);
struct kui_toy_loader_payload_control_counts {
    uint32_t attempts,declines,publications,failed;
};

/* Called only by the serialized, already-masked cooked DATA observer. It
 * calls original exactly once and never retries a started or failed read.
 * Mode 1 substitutes the cached alias of the exact low image block, with
 * pre/post OCBP over its sixteen complete lines, including failed returns.
 * Mode 2 supplies an unaligned private P2 scratch destination so the retained
 * bus takes its existing bounded PIO path; only successful logical bytes are
 * copied to the original image block. Neither mode changes raw/CDDA routing.
 * Counters include wrapper costs and saturate independently at UINT32_MAX.
 */
bool kui_toy_loader_payload_call(kui_toy_loader_payload_fn original,void *context,
    const uint8_t *tx,uint8_t *rx,size_t count,bool slow,uint16_t *crc);
const struct kui_toy_loader_payload_control_counts *kui_toy_loader_payload_control_counts(void);

#ifdef KUI_TOY_LOADER_PAYLOAD_CONTROL_HOST_TEST
void kui_toy_loader_payload_control_host_bind(uint8_t *image_block);
void kui_toy_loader_payload_control_host_reset(void);
uint32_t kui_toy_loader_payload_control_host_ccr(void);
uint8_t *kui_toy_loader_payload_control_host_cached(uint8_t *);
void kui_toy_loader_payload_control_host_purge(uint8_t *,uint32_t);
#endif
#endif
