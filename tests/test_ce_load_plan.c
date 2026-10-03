/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/ce_load_plan.h"
#include "kui/retail_loader_layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned assertions;
#define CHECK(x) do { ++assertions; assert(x); } while(0)
static uint8_t header[KUI_CE_LOAD_PREFIX_BYTES];
static struct kui_ce_load_plan plan;
/* The CE placement probe stage (retail_stage.c with KUI_RETAIL_CE), not a
 * verified CE boot layout. The complete low system/IP area and high stack
 * allocation remain live. */
static const struct kui_ce_live_range candidate[] = {
    {0x8c000000u, 0x10000u},
    {KUI_RETAIL_CE_STAGE_ADDRESS, KUI_RETAIL_STAGE_MEMORY_END - KUI_RETAIL_CE_STAGE_ADDRESS},
    {KUI_RETAIL_STAGE_MEMORY_END, KUI_RETAIL_RAM_END - KUI_RETAIL_STAGE_MEMORY_END}
};
static void put32(size_t offset, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i) header[offset + i] = (uint8_t)(value >> (8u * i));
}
static void fixture(uint32_t bytes) {
    /* Synthetic declarations only: no original boot prefix or game code. */
    memset(header, 0, sizeof(header));
    put32(0x10, 1u); put32(0x14, 0x0c010000u);
    put32(0x18, 0x800u); put32(0x1c, bytes); put32(0x20, 0x0c010000u);
}
static enum kui_ce_load_result build(uint64_t bytes) {
    return kui_ce_load_plan_build(true, header, sizeof(header), bytes,
        candidate, sizeof(candidate) / sizeof(candidate[0]), &plan);
}
static void empty(void) {
    const struct kui_ce_load_plan zero = {0};
    CHECK(!memcmp(&plan, &zero, sizeof(plan)));
}
static void reported_layouts(void) {
    /* Sizes from the owner's analysis, not locally inspected binaries. */
    const uint32_t files[] = {1253376u, 1830912u, 1110016u};
    const uint32_t ends[] = {0x8c141800u, 0x8c1ce800u, 0x8c11e800u};
    for(unsigned i = 0; i < 3; ++i) {
        fixture(files[i] - 0x800u);
        CHECK(build(files[i]) == KUI_CE_LOAD_OK);
        CHECK(plan.prefix.address == 0x8ce01000u && plan.prefix.bytes == 0x800u);
        CHECK(plan.prefix.file_offset == 0 && plan.prefix.sector_count == 1);
        CHECK(plan.body.address == 0x8c010000u && plan.body.file_offset == 0x800u);
        CHECK(plan.body.address + plan.body.bytes == ends[i]);
        CHECK(plan.body.sector_count * 2048u == plan.body.bytes);
        CHECK(plan.body.rounded_bytes == plan.body.bytes);
        CHECK(plan.entry_address == 0x8c010000u);
    }
    fixture(0x131800u);
    struct kui_ce_live_range live[3]; memcpy(live, candidate, sizeof(live));
    live[1].address = KUI_RETAIL_STAGE_ADDRESS;
    live[1].bytes = KUI_RETAIL_STAGE_MEMORY_END - KUI_RETAIL_STAGE_ADDRESS;
    CHECK(kui_ce_load_plan_build(true, header, sizeof(header), 1253376,
        live, 3, &plan) == KUI_CE_LOAD_OVERLAP); empty();
}
static void aliases_and_entry(void) {
    const uint32_t aliases[] = {0x0c010000u, 0x8c010000u, 0xac010000u};
    for(unsigned a = 0; a < 3; ++a) for(unsigned b = 0; b < 3; ++b) {
        fixture(4096u); put32(0x14, aliases[a]); put32(0x20, aliases[b] + 4094u);
        CHECK(build(6144u) == KUI_CE_LOAD_OK);
        CHECK(plan.body.address == 0x8c010000u && plan.entry_address == 0x8c010ffeu);
    }
    const uint32_t bad[] = {0u, 0x0bffffffu, 0x0d010000u, 0x1c010000u,
        0x01de1000u, 0x8b010000u, 0x8d010000u, 0xad010000u, 0xff010000u,
        0x8c010001u, 0x8c020000u, UINT32_MAX};
    for(unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        fixture(4096u); put32(0x14, bad[i]);
        CHECK(build(6144u) == KUI_CE_LOAD_ADDRESS); empty();
    }
    const uint32_t entry[] = {0x0c00fffeu, 0x0c010001u, 0x0c011000u,
        0x0d000000u, 0x01de1000u, UINT32_MAX};
    for(unsigned i = 0; i < sizeof(entry) / sizeof(entry[0]); ++i) {
        fixture(4096u); put32(0x20, entry[i]);
        CHECK(build(6144u) == KUI_CE_LOAD_ENTRY); empty();
    }
    fixture(3u); put32(0x20, 0x0c010002u);
    CHECK(build(2051u) == KUI_CE_LOAD_ENTRY); empty();
    fixture(3u); CHECK(build(2051u) == KUI_CE_LOAD_OK);
}
static void file_and_rounding(void) {
    fixture(2049u); CHECK(build(4097u) == KUI_CE_LOAD_OK);
    CHECK(plan.body.bytes == 2049u && plan.body.rounded_bytes == 4096u);
    CHECK(plan.body.sector_count == 2);
    /* Padding is reserved, even when a live range starts after the last byte. */
    struct kui_ce_live_range guard = {0xac010801u, 1u};
    CHECK(kui_ce_load_plan_build(true, header, sizeof(header), 4097u,
        &guard, 1, &plan) == KUI_CE_LOAD_OVERLAP); empty();
    guard.address = 0xac011000u;
    CHECK(kui_ce_load_plan_build(true, header, sizeof(header), 4097u,
        &guard, 1, &plan) == KUI_CE_LOAD_OK);
    fixture(2u); CHECK(build(2050u) == KUI_CE_LOAD_OK);
    const uint64_t bad_files[] = {0, 2047, 2048, 2049, 2051, UINT32_MAX, UINT64_MAX};
    for(unsigned i = 0; i < sizeof(bad_files) / sizeof(bad_files[0]); ++i) {
        CHECK(build(bad_files[i]) == KUI_CE_LOAD_FILE_RANGE); empty();
    }
    for(unsigned i = 0; i < 2; ++i) {
        fixture(i); CHECK(build(2048u + i) == KUI_CE_LOAD_FILE_RANGE); empty();
    }
    const uint32_t huge[] = {0x00ff0001u, 0x01000000u, 0xfffff801u, UINT32_MAX};
    for(unsigned i = 0; i < sizeof(huge) / sizeof(huge[0]); ++i) {
        fixture(huge[i]);
        CHECK(build((uint64_t)huge[i] + 2048u) == KUI_CE_LOAD_ADDRESS); empty();
    }
    /* Exactly reaches, then crosses, the separate prefix destination. */
    fixture(0x00df1000u); CHECK(build(0x00df1800u) == KUI_CE_LOAD_OK);
    fixture(0x00df1001u); CHECK(build(0x00df1801u) == KUI_CE_LOAD_OVERLAP); empty();
}
static void live_ranges(void) {
    fixture(4096u);
    const struct kui_ce_live_range bad[] = {
        {0x0c000000u, 0}, {0x8c000000u, UINT32_MAX},
        {0xacffffffu, 2}, {0xad000000u, 1}, {0x01de0000u, 4096},
        {UINT32_MAX, UINT32_MAX}
    };
    for(unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        CHECK(kui_ce_load_plan_build(true, header, sizeof(header), 6144,
            &bad[i], 1, &plan) == KUI_CE_LOAD_LAYOUT); empty();
    }
    /* Any named live allocation can collide: source, resident, stage, stack.
     * Alias mixing cannot hide a collision at either end of either region. */
    const uint32_t points[] = {0x0c010000u, 0x8c010fffu, 0xac010001u,
        0x0ce01000u, 0x8ce017ffu, 0xace01001u};
    for(unsigned i = 0; i < sizeof(points) / sizeof(points[0]); ++i) {
        struct kui_ce_live_range live[4]; memcpy(live, candidate, sizeof(candidate));
        live[3] = (struct kui_ce_live_range){points[i], 1u};
        CHECK(kui_ce_load_plan_build(true, header, sizeof(header), 6144,
            live, 4, &plan) == KUI_CE_LOAD_OVERLAP); empty();
    }
    const struct kui_ce_live_range adjacent[] = {
        {0x0c000000u, 0x10000u}, {0xac011000u, 0xdf0000u},
        {0x0ce01800u, 0x1fe800u}
    };
    CHECK(kui_ce_load_plan_build(true, header, sizeof(header), 6144,
        adjacent, 3, &plan) == KUI_CE_LOAD_OK);
    struct kui_ce_live_range cover = {0x0c000000u, 0x1000000u};
    CHECK(kui_ce_load_plan_build(true, header, sizeof(header), 6144,
        &cover, 1, &plan) == KUI_CE_LOAD_OVERLAP); empty();
}
static void bad_profile_and_arguments(void) {
    fixture(4096u);
    CHECK(kui_ce_load_plan_build(false, header, sizeof(header), 6144,
        candidate, 3, &plan) == KUI_CE_LOAD_PROFILE); empty();
    for(size_t n = 0; n < sizeof(header); ++n) {
        CHECK(kui_ce_load_plan_build(true, header, n, 6144,
            candidate, 3, &plan) == KUI_CE_LOAD_HEADER); empty();
    }
    const uint32_t unsupported[] = {0, 2, 0x100, UINT32_MAX};
    for(unsigned i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i) {
        fixture(4096u); put32(0x10, unsupported[i]);
        CHECK(build(6144u) == KUI_CE_LOAD_UNSUPPORTED); empty();
        fixture(4096u); put32(0x18, unsupported[i]);
        CHECK(build(6144u) == KUI_CE_LOAD_UNSUPPORTED); empty();
    }
    fixture(4096u);
    CHECK(kui_ce_load_plan_build(true, NULL, sizeof(header), 6144,
        candidate, 3, &plan) == KUI_CE_LOAD_ARGUMENT); empty();
    CHECK(kui_ce_load_plan_build(true, header, sizeof(header), 6144,
        NULL, 3, &plan) == KUI_CE_LOAD_ARGUMENT); empty();
    CHECK(kui_ce_load_plan_build(true, header, sizeof(header), 6144,
        candidate, 0, &plan) == KUI_CE_LOAD_ARGUMENT); empty();
    CHECK(kui_ce_load_plan_build(true, header, sizeof(header), 6144,
        candidate, KUI_CE_LOAD_MAX_LIVE_RANGES + 1, &plan) == KUI_CE_LOAD_ARGUMENT); empty();
    CHECK(kui_ce_load_plan_build(true, header, sizeof(header), 6144,
        candidate, 3, NULL) == KUI_CE_LOAD_ARGUMENT);
}
int main(void) {
    reported_layouts(); aliases_and_entry(); file_and_rounding();
    live_ranges(); bad_profile_and_arguments();
    printf("CE read-only load planner: %u checks passed\n", assertions);
    return 0;
}
