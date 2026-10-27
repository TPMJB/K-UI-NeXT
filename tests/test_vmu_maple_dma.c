/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_maple_dma.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fixture {
    _Alignas(32) uint8_t source[4096];
    _Alignas(32) uint8_t hardware[4096];
    _Alignas(32) uint8_t replies[32][1024];
    struct kui_vmu_maple_dma_plan plan;
    struct kui_vmu_maple_dma_region region;
    unsigned calls;
};

static struct fixture f;

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8
        | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void reset(void) {
    memset(&f, 0, sizeof(f));
    memset(f.hardware, 0xa5, sizeof(f.hardware));
    memset(f.replies, 0xff, sizeof(f.replies));
    f.region.physical_base = UINT32_C(0x0c400000);
    f.region.bytes = f.replies[0];
    f.region.size = sizeof(f.replies);
}

static size_t append(size_t offset, unsigned slot, uint8_t command,
    uint8_t destination, unsigned words, int last) {
    size_t bytes = 12u + words * 4u;
    unsigned i;
    assert(offset + bytes <= sizeof(f.source));
    put32(f.source + offset, (last ? KUI_VMU_MAPLE_DMA_LAST : 0u)
        | (uint32_t)(destination >> 6) << 16 | words);
    put32(f.source + offset + 4u, f.region.physical_base + slot * 1024u);
    f.source[offset + 8u] = command;
    f.source[offset + 9u] = destination;
    f.source[offset + 10u] = destination & 0xc0u;
    f.source[offset + 11u] = (uint8_t)words;
    for(i = 0; i < words * 4u; ++i)
        f.source[offset + 12u + i] = (uint8_t)(i + slot);
    return offset + bytes;
}

static int prepare(uint8_t address, size_t source_size) {
    return kui_vmu_maple_dma_prepare(&f.plan, address,
        f.source, source_size, f.hardware, sizeof(f.hardware), &f.region, 1u);
}

static int reply_ok(void *context, const uint8_t *request, size_t request_bytes,
    uint8_t *response, size_t capacity, size_t *response_bytes) {
    struct fixture *fixture = context;
    assert(request_bytes == 4u + 4u * request[3]);
    assert(capacity == 1024u);
    ++fixture->calls;
    response[0] = 7u;
    response[1] = request[2];
    response[2] = request[1];
    response[3] = 0u;
    *response_bytes = 4u;
    return 1;
}

static int reply_bad(void *context, const uint8_t *request, size_t request_bytes,
    uint8_t *response, size_t capacity, size_t *response_bytes) {
    int result = reply_ok(context, request, request_bytes, response,
        capacity, response_bytes);
    response[3] = 2u; /* A corrupt handler must not become a valid completion. */
    return result;
}

static int reply_timeout(void *context, const uint8_t *request, size_t request_bytes,
    uint8_t *response, size_t capacity, size_t *response_bytes) {
    (void)reply_ok(context, request, request_bytes, response, capacity, response_bytes);
    memset(response, 0xff, 4u);
    return 1;
}

static void assert_untouched(void) {
    size_t i;
    for(i = 0; i < sizeof(f.hardware); ++i)
        assert(f.hardware[i] == 0xa5u);
    for(i = 0; i < sizeof(f.replies); ++i)
        assert(f.region.bytes[i] == 0xffu);
    assert(f.calls == 0u);
}

static void mixed_chain(void) {
    size_t n = 0;
    uint8_t original[4096];
    reset();
    n = append(n, 0, 1, 0x20, 0, 0); /* Actual controller enumeration. */
    n = append(n, 1, 1, 0x02, 0, 0); /* Virtual A2. */
    n = append(n, 2, 9, 0x20, 1, 0); /* Actual controller input. */
    n = append(n, 3, 9, 0xe0, 1, 0); /* Actual port D controller. */
    n = append(n, 4, 11, 0x02, 2, 1); /* Final descriptor is virtual. */
    memcpy(original, f.source, sizeof(original));
    assert(prepare(0x02, n) == KUI_VMU_MAPLE_DMA_OK);
    assert(f.plan.frame_count == 5u && f.plan.virtual_count == 2u);
    assert(f.plan.hardware_count == 3u && f.plan.hardware_bytes == 44u);
    assert(f.plan.source_bytes == n);
    assert(le32(f.hardware) == 0u);
    assert(le32(f.hardware + 12u) == 1u);
    assert(le32(f.hardware + 28u) == UINT32_C(0x80030001));
    assert(memcmp(f.hardware + 4u, original + 4u, 8u) == 0);
    assert(memcmp(f.hardware + 16u, original + 28u, 12u) == 0);
    assert(memcmp(f.hardware + 32u, original + 44u, 12u) == 0);
    assert(memcmp(original, f.source, sizeof(original)) == 0);
    /* Simulate physical DMA response, including a real VMU in A1. */
    f.replies[0][0] = 5u; f.replies[0][1] = 0u; f.replies[0][2] = 0x21u;
    f.replies[0][3] = 28u;
    f.replies[2][0] = 8u; f.replies[2][1] = 0u; f.replies[2][2] = 0x21u;
    f.replies[3][0] = 8u; f.replies[3][1] = 0xc0u; f.replies[3][2] = 0xe0u;
    assert(kui_vmu_maple_dma_complete(&f.plan, reply_ok, &f) == 0);
    assert(f.calls == 2u);
    assert(f.replies[0][2] == 0x23u && f.replies[2][2] == 0x23u);
    assert(f.replies[3][2] == 0xe0u);
    assert(f.replies[1][0] == 7u && f.replies[4][0] == 7u);
    assert(kui_vmu_maple_dma_complete(&f.plan, reply_ok, &f)
        == KUI_VMU_MAPLE_DMA_STATE);
    assert(f.calls == 2u);
}

static void virtual_only_and_terminal_failure(void) {
    size_t n;
    reset();
    n = append(0, 0, 3, 0x81, 0, 1);
    assert(kui_vmu_maple_dma_prepare(&f.plan, 0x81, f.source, n,
        NULL, 0, &f.region, 1) == 0);
    assert(f.plan.hardware_bytes == 0u && f.plan.hardware_count == 0u);
    assert(kui_vmu_maple_dma_complete(&f.plan, NULL, &f)
        == KUI_VMU_MAPLE_DMA_INVALID);
    assert(kui_vmu_maple_dma_complete(&f.plan, reply_bad, &f)
        == KUI_VMU_MAPLE_DMA_HANDLER);
    assert(f.calls == 1u);
    assert(kui_vmu_maple_dma_complete(&f.plan, reply_ok, &f)
        == KUI_VMU_MAPLE_DMA_STATE);
    assert(f.calls == 1u);
    assert(prepare(0x81, n) == 0);
    assert(kui_vmu_maple_dma_complete(&f.plan, reply_timeout, &f) == 0);
    assert(le32(f.replies[0]) == UINT32_MAX);
}

static void controller_errors_are_preserved(void) {
    size_t n = 0;
    reset();
    n = append(n, 0, 1, 0x60, 0, 0);
    n = append(n, 1, 1, 0x60, 0, 0);
    n = append(n, 2, 1, 0x60, 0, 1);
    assert(prepare(0x50, n) == 0);
    /* Timeout, error response, malformed controller source respectively. */
    f.replies[1][0] = 0xfcu; f.replies[1][1] = 0x40u; f.replies[1][2] = 0x60u;
    f.replies[2][0] = 5u; f.replies[2][1] = 0x40u; f.replies[2][2] = 0x20u;
    assert(kui_vmu_maple_dma_complete(&f.plan, NULL, &f) == 0);
    assert(le32(f.replies[0]) == UINT32_MAX);
    assert(f.replies[1][2] == 0x60u && f.replies[2][2] == 0x20u);
}

static void rejects_bad_chains_without_side_effects(void) {
    size_t n;
    unsigned test;
    for(test = 0; test < 14u; ++test) {
        reset();
        n = append(0, 0, 1, 0x20, 0, 0);
        n = append(n, 1, 1, 0x02, 0, 1);
        switch(test) {
        case 0: n -= 4u; break; /* Truncated last descriptor. */
        case 1: f.source[11] = 1u; break; /* Header vs descriptor length. */
        case 2: f.source[0] = 255u; break; /* Truncated payload. */
        case 3: f.source[2] = 1u; break; /* Port vs recipient mismatch. */
        case 4: f.source[10] = 0x20u; break; /* Invalid host source. */
        case 5: f.source[9] = 3u; break; /* Multicast. */
        case 6: f.source[1] = 2u; break; /* Lightgun pattern. */
        case 7: f.source[3] = 0x40u; break; /* Reserved control bit. */
        case 8: f.source[4] = 1u; break; /* Unaligned RX. */
        case 9: put32(f.source + 4, 0x0c3fffe0u); break; /* Before owned region. */
        case 10: put32(f.source + 4, 0x0c407c20u); break; /* RX end outside region. */
        case 11: put32(f.source + 16, 0x0c400000u); break; /* Duplicate RX. */
        case 12: f.source[15] = 0u; break; /* Missing LAST. */
        case 13: put32(f.source + 4, 0xac400000u); break; /* CPU alias, not physical. */
        }
        assert(prepare(0x02, n) != 0);
        assert_untouched();
        assert(kui_vmu_maple_dma_complete(&f.plan, reply_ok, &f)
            == KUI_VMU_MAPLE_DMA_STATE);
    }
}

static void capacity_and_aliases(void) {
    size_t n = 0;
    struct kui_vmu_maple_dma_region regions[2];
    reset();
    n = append(n, 0, 1, 0x20, 0, 1);
    assert(kui_vmu_maple_dma_prepare(&f.plan, 2, f.source, n,
        f.hardware, 8, &f.region, 1) == KUI_VMU_MAPLE_DMA_CAPACITY);
    assert_untouched();
    assert(kui_vmu_maple_dma_prepare(&f.plan, 2, f.source, n,
        f.source, sizeof(f.source), &f.region, 1) == KUI_VMU_MAPLE_DMA_INVALID);
    assert(kui_vmu_maple_dma_prepare(&f.plan, 2, f.source, n,
        f.hardware + 1, sizeof(f.hardware) - 1, &f.region, 1)
        == KUI_VMU_MAPLE_DMA_INVALID);
    regions[0] = f.region; regions[1] = f.region;
    regions[1].physical_base += (uint32_t)sizeof(f.replies);
    assert(kui_vmu_maple_dma_prepare(&f.plan, 2, f.source, n,
        f.hardware, sizeof(f.hardware), regions, 2) == KUI_VMU_MAPLE_DMA_INVALID);
    assert(prepare(0, n) == KUI_VMU_MAPLE_DMA_INVALID);
    assert(prepare(0x20, n) == KUI_VMU_MAPLE_DMA_INVALID);
    assert(prepare(3, n) == KUI_VMU_MAPLE_DMA_INVALID);
    assert(prepare(2, n) == 0);
    assert(prepare(3, n) != 0); /* A rejected re-prepare revokes old plan. */
    assert(kui_vmu_maple_dma_complete(&f.plan, NULL, &f)
        == KUI_VMU_MAPLE_DMA_STATE);
}

static void largest_payload_and_frame_bound(void) {
    size_t n;
    unsigned i;
    reset();
    n = append(0, 0, 12, 0x01, 255, 1);
    assert(prepare(2, n) == 0 && f.plan.hardware_bytes == 1032u);
    assert(memcmp(f.source, f.hardware, n) == 0);
    reset();
    n = 0;
    for(i = 0; i < 32; ++i)
        n = append(n, i, 1, 2, 0, i == 31);
    assert(prepare(2, n) == 0 && f.plan.virtual_count == 32u);
    assert(kui_vmu_maple_dma_complete(&f.plan, reply_ok, &f) == 0);
    assert(f.calls == 32u);
    reset();
    n = 0;
    for(i = 0; i < 33; ++i)
        n = append(n, i % 32u, 1, 2, 0, i == 32);
    assert(prepare(2, n) == KUI_VMU_MAPLE_DMA_CAPACITY);
    assert_untouched();
}

static void bounded_mutation_sweep(void) {
    unsigned i;
    uint32_t random = 0x574d5532u;
    for(i = 0; i < 12000u; ++i) {
        size_t n, byte;
        reset();
        n = append(0, 0, 1, 0x20, 0, 0);
        n = append(n, 1, 11, 2, 2, 1);
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        byte = random % n;
        f.source[byte] ^= (uint8_t)(random >> 24);
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        (void)prepare(2, (random % (n / 4u + 1u)) * 4u);
        assert(f.calls == 0u);
        for(byte = 0; byte < sizeof(f.replies); ++byte)
            assert(f.region.bytes[byte] == 0xffu);
    }
}

int main(void) {
    mixed_chain();
    virtual_only_and_terminal_failure();
    controller_errors_are_preserved();
    rejects_bad_chains_without_side_effects();
    capacity_and_aliases();
    largest_payload_and_frame_bound();
    bounded_mutation_sweep();
    puts("VMU Maple DMA adapter tests passed");
    return 0;
}
