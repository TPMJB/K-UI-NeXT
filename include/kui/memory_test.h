/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_MEMORY_TEST_H
#define KUI_MEMORY_TEST_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define KUI_MEMORY_TEST_MAX (4u*1024u*1024u)
struct kui_memory_test_result {
    size_t buffer_bytes,tested_bytes,first_failure_offset;
    uint64_t work_done,work_total;
    unsigned passes,total_passes,errors;
    uint32_t expected,actual;
    bool complete,stopped,invalid;
};
struct kui_memory_test_ops {
    void *ctx;
    bool (*cancelled)(void *ctx);
    void (*progress)(void *ctx,const struct kui_memory_test_result *result);
    void (*yield)(void *ctx);
};
/* Destructive only within the caller-owned, word-aligned buffer. Six full
 * write/read passes cover constants and address-derived patterns. Walking-one
 * and walking-zero passes cover the first/last word of every 4 KiB block.
 * No cache maintenance, fixed physical addresses, allocation or I/O occurs.
 * Returns true only after all checks pass. A mismatch stops at the first error. */
bool kui_memory_test(volatile uint32_t *buffer,size_t bytes,
    const struct kui_memory_test_ops *ops,struct kui_memory_test_result *result);
#endif
