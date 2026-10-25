/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_READAHEAD_H
#define KUI_NATIVE_READAHEAD_H
#include <stdint.h>
#include <stdbool.h>
#include "kui/retail_cursor.h"
#include "sci_stream.h"
#include "retail_async.h"
#include "kui/native_readahead_layout.h"
enum kui_native_ra_event {
    KUI_NATIVE_RA_DELIVER=1u,
    KUI_NATIVE_RA_TICK=2u,
    KUI_NATIVE_RA_QUIESCE=3u,
    KUI_NATIVE_RA_RESUME=4u,
    KUI_NATIVE_RA_RESET=5u,
    KUI_NATIVE_RA_CAPTURE_START=6u
};
struct kui_retail_cursor;
/* Every pointer is proved against the immutable low resident by the stage.
 * The auxiliary code invokes these standard C entries on its own stack. */
struct kui_native_ra_low_ops {
    uint32_t magic,version,bytes;
    kui_retail_async_region_type **region_storage;
    void (*deliver)(uint32_t wait);
    void (*hook)(void);
    void (*unhook)(void);
    enum kui_sci_stream_result (*fetch)(uint32_t lba,uint32_t token_limit,bool polled);
    enum kui_sci_stream_result (*poll)(bool interrupt);
    const uint8_t *(*take)(uint32_t lba,enum kui_sci_stream_result *result);
    enum kui_sci_stream_result (*stop)(void);
    bool (*busy)(void);
    enum kui_game_result (*feed)(struct kui_retail_cursor *,const uint8_t block[512]);
};
typedef uint32_t (*kui_native_ra_visit)(uint32_t event,uint32_t arg,
    kui_retail_async_region_type *region,uint32_t reserved);
struct kui_native_ra_config {
    uint32_t version,bytes;
    kui_retail_async_region_type *region;
    const struct kui_native_ra_low_ops *ops;
    uint8_t *data;
    uint32_t data_bytes,lease,lease_bytes;
};
/* The relocation domain includes exports, context, code and private stack;
 * the separately owned data arena is passed by the cold stage, never relocated. */
struct kui_native_ra_exports {
    uint32_t magic,version,bytes;
    kui_native_ra_visit visit;
    int (*init)(const struct kui_native_ra_config *);
    void *context;
    uint32_t image_end,stack_bottom,stack_top,memory_end;
    uint32_t reserved[6];
};
struct kui_native_ra_stats {
    uint32_t hits,misses,hit_requests,producer_blocks,producer_visits;
    uint32_t crc_errors,token_errors,abandoned,seeks,quiesces,resumes;
    uint32_t full,extent_ends,heartbeat_visits,irq_visits,guard_faults;
    uint32_t max_visit_ticks,clock_bad;
};
struct kui_native_ra_public {
    uint32_t busy;
    struct kui_native_ra_stats stats;
};
#if UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(struct kui_native_ra_low_ops)==52u,"read-ahead low operations ABI");
_Static_assert(sizeof(struct kui_native_ra_exports)==64u,"read-ahead auxiliary exports ABI");
_Static_assert(sizeof(struct kui_native_ra_config)==32u,"read-ahead configuration ABI");
#endif
#endif
