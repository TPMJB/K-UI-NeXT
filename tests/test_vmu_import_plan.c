/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthetic cards only. Metadata is deliberately independent of filenames. */
#include "kui/vmu_import_plan.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t source[KUI_VMU_IMAGE_BYTES], before[KUI_VMU_IMAGE_BYTES];
static uint8_t destination[KUI_VMU_IMAGE_BYTES], expected[KUI_VMU_IMAGE_BYTES];
static struct kui_vmu_image_workspace work;
static struct kui_vmu_import_plan plan, previous_plan;
static struct kui_vmu_image_summary summary;

static void put16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static uint8_t *entry(unsigned slot) {
    assert(slot < 208u);
    return source + (253u - slot / 16u) * 512u + slot % 16u * 32u;
}
static void blank(void) {
    for(unsigned b = 0; b < 256u; ++b)
        assert(!kui_vmu_image_blank_block((uint16_t)b, source + b * 512u));
    memset(destination, 0xa5, sizeof(destination));
}
static void add(unsigned slot, const unsigned *chain, unsigned count,
                unsigned header, const char *name, int game) {
    uint8_t *e = entry(slot);
    assert(strlen(name) <= 12u && header < count);
    e[0] = game ? 0xcc : 0x33; e[1] = 0x10;
    put16(e + 2u, chain[0]); memcpy(e + 4u, name, strlen(name));
    put16(e + 24u, count); put16(e + 26u, header);
    for(unsigned i = 0; i < count; ++i) {
        assert(chain[i] < 200u);
        put16(source + 254u * 512u + chain[i] * 2u,
              i + 1u == count ? 0xfffa : chain[i + 1u]);
        for(unsigned p = 0; p < 512u; ++p)
            source[chain[i] * 512u + p] = (uint8_t)(p * 7u + i * 31u + slot);
    }
    /* Nondefault opaque entry bytes and root must survive a nonempty subset. */
    for(unsigned i = 16; i < 24u; ++i) e[i] = (uint8_t)(i + slot);
    for(unsigned i = 28; i < 32u; ++i) e[i] = (uint8_t)(i * 5u + slot);
    source[255u * 512u + 0x20u] = 0x97;
}
static void metadata(unsigned block, const char *short_text,
                     const char *long_text, const char *app) {
    uint8_t *h = source + block * 512u;
    assert(strlen(short_text) <= 16u && strlen(long_text) <= 32u && strlen(app) <= 16u);
    memset(h, ' ', 64u);
    memcpy(h, short_text, strlen(short_text));
    memcpy(h + 16u, long_text, strlen(long_text));
    memcpy(h + 48u, app, strlen(app));
}
static void inspect(const char *title, const char *product) {
    memcpy(before, source, sizeof(source));
    assert(!kui_vmu_import_plan_inspect(source, sizeof(source), title, product, &work, &plan));
    assert(!memcmp(source, before, sizeof(source)));
    assert(plan.entry_count == plan.summary.files);
}
static void fat_header_metadata_and_review_hints(void) {
    blank();
    const unsigned a[] = {199, 51, 17}, b[] = {122}, c[] = {0, 1, 2, 3};
    const unsigned d[] = {88, 104};
    add(0, a, 3, 2, "SAVE.001", 0);
    add(15, b, 1, 0, "CONFIG", 0);
    add(16, c, 4, 3, "MINI-GAME", 1);
    add(207, d, 2, 1, "OTHER", 0);
    metadata(17, "Skies of Arcadia", "Main save", "T-1234N");
    metadata(122, "Settings", "Related file chosen manually", "SYSTEM");
    metadata(3, "Mini Game", "No automatic group membership", "T-1234N-EXTRA");
    metadata(104, "Elsewhere", "Different title", "OTHERAPP");
    inspect("  skies OF arcadia  ", "t-1234n");
    assert(plan.entry_count == 4u && plan.summary.used_blocks == 10u);
    const unsigned slots[] = {0, 15, 16, 207}, blocks[] = {3, 1, 4, 2};
    for(unsigned i = 0; i < 4u; ++i) {
        assert(plan.entries[i].slot == slots[i]);
        assert(plan.entries[i].blocks == blocks[i]);
    }
    assert(plan.entries[0].header_offset == 2u);
    assert(!strcmp(plan.entries[0].short_description, "Skies of Arcadia"));
    assert(!strcmp(plan.entries[0].long_description, "Main save"));
    assert(!strcmp(plan.entries[0].application, "T-1234N"));
    assert(plan.entries[0].hints == (KUI_VMU_IMPORT_HINT_TITLE_TEXT | KUI_VMU_IMPORT_HINT_PRODUCT_TEXT));
    assert(!plan.entries[1].hints && !plan.entries[2].hints && !plan.entries[3].hints);
    assert(plan.entries[2].type == 0xcc && !strcmp(plan.entries[2].short_description, "Mini Game"));
    /* Hints are only evidence to display: build retains exactly the slots
     * explicitly reviewed, including unrelated-looking configuration data. */
    const uint16_t chosen[] = {15, 0};
    struct kui_vmu_image_workspace other_work;
    struct kui_vmu_image_summary other_summary;
    assert(!kui_vmu_image_subset(source, sizeof(source), expected, sizeof(expected),
                                 chosen, 2u, &other_work, &other_summary));
    assert(!kui_vmu_import_plan_build(source, sizeof(source), destination, sizeof(destination),
                                      chosen, 2u, &work, &summary));
    assert(!memcmp(destination, expected, sizeof(destination)));
    assert(!memcmp(source, before, sizeof(source)));
    assert(summary.files == 2u && summary.used_blocks == 4u && summary.free_blocks == 196u);
    assert(!memcmp(destination + 255u * 512u, source + 255u * 512u, 512u));
    assert(!memcmp(destination + 199u * 512u, source + 199u * 512u, 512u));
    assert(!memcmp(destination + 17u * 512u, source + 17u * 512u, 512u));
    assert(!memcmp(destination + (entry(15) - source), entry(15), 32u));
}
static void bounded_display_text(void) {
    blank(); const unsigned chain[] = {199};
    add(207, chain, 1, 0, "ABCDEFGHIJKL", 0);
    uint8_t *h = source + 199u * 512u;
    memset(h, 'X', 64u);
    memcpy(h, "A\nB\177C\377D", 7u); h[7] = 0;
    memcpy(h + 16u, "FULL LONG DESCRIPTION 32 BYTES!!!", 32u);
    memset(h + 48u, 'Z', 16u);
    entry(207)[4] = 1; entry(207)[15] = 0xff;
    inspect("A?B?C?D", NULL);
    assert(!strcmp(plan.entries[0].name, "?BCDEFGHIJK?"));
    assert(!strcmp(plan.entries[0].short_description, "A?B?C?D"));
    assert(strlen(plan.entries[0].long_description) == 32u);
    assert(strlen(plan.entries[0].application) == 16u);
    /* Sanitizing opaque bytes must not create a coincident-text hint. */
    assert(!plan.entries[0].hints);
    char unterminated[KUI_VMU_IMPORT_TITLE_BYTES]; memset(unterminated, 'X', sizeof(unterminated));
    inspect(unterminated, "ZZZZ"); assert(!plan.entries[0].hints);
    inspect("FULL LONG DESCRIPTION", "ZZZZ"); assert(!plan.entries[0].hints);
    inspect(" ", "ZZZ"); assert(!plan.entries[0].hints);
}
static void last_header_and_all_user_blocks(void) {
    blank(); unsigned chain[200];
    for(unsigned i = 0; i < 200u; ++i) chain[i] = 199u - i;
    add(207, chain, 200u, 199u, "FULL-CARD", 0);
    metadata(0, "Last block", "Header is at FAT chain index 199", "APPLICATION");
    inspect(NULL, NULL);
    assert(plan.entry_count == 1u && plan.summary.used_blocks == 200u && !plan.summary.free_blocks);
    assert(plan.entries[0].header_offset == 199u);
    assert(!strcmp(plan.entries[0].short_description, "Last block"));
    assert(!strcmp(plan.entries[0].application, "APPLICATION"));
}
static void empty_selection_is_fully_admitted_blank(void) {
    blank(); const unsigned chain[] = {80, 12}; add(16, chain, 2, 1, "SAVE", 0);
    memcpy(before, source, sizeof(source));
    assert(!kui_vmu_import_plan_build(source, sizeof(source), destination, sizeof(destination),
                                      NULL, 0u, &work, &summary));
    for(unsigned b = 0; b < 256u; ++b) {
        uint8_t block[512]; assert(!kui_vmu_image_blank_block((uint16_t)b, block));
        assert(!memcmp(destination + b * 512u, block, 512u));
    }
    assert(!memcmp(source, before, sizeof(source)));
    assert(!summary.files && !summary.game_files && !summary.used_blocks && summary.free_blocks == 200u);
    /* A request for a blank destination cannot bypass malformed-source
     * admission. Same guarantee applies to a valid but unselected file. */
    put16(source + 254u * 512u + 12u * 2u, 80u);
    memset(destination, 0xa5, sizeof(destination));
    assert(kui_vmu_import_plan_build(source, sizeof(source), destination, sizeof(destination),
                                     NULL, 0u, &work, &summary) == KUI_VMU_IMAGE_FILESYSTEM);
    for(unsigned i = 0; i < sizeof(destination); ++i) assert(destination[i] == 0xa5);
    assert(!summary.files && !summary.free_blocks);
}
static void fail_closed_admission(void) {
    for(unsigned mutation = 0; mutation < 6u; ++mutation) {
        blank(); const unsigned a[] = {40}, b[] = {70, 18};
        add(0, a, 1, 0, "KEEP", 0); add(207, b, 2, 1, "BAD-OTHER", 0);
        switch(mutation) {
        case 0: put16(entry(207) + 26u, 2u); break;
        case 1: put16(source + 254u * 512u + 18u * 2u, 70u); break;
        case 2: put16(entry(207) + 2u, 200u); break;
        case 3: put16(entry(207) + 2u, 40u); break;
        case 4: put16(source + 254u * 512u + 99u * 2u, 0xfffa); break;
        case 5: source[255u * 512u] = 0; break;
        }
        memset(&plan, 0xa5, sizeof(plan)); memcpy(before, source, sizeof(source));
        assert(kui_vmu_import_plan_inspect(source, sizeof(source), "KEEP", NULL, &work, &plan) < 0);
        assert(!memcmp(source, before, sizeof(source)));
        const uint8_t *p = (const uint8_t *)&plan;
        for(unsigned i = 0; i < sizeof(plan); ++i) assert(!p[i]);
        const uint16_t chosen[] = {0};
        assert(kui_vmu_import_plan_build(source, sizeof(source), destination, sizeof(destination),
                                         chosen, 1u, &work, &summary) < 0);
        for(unsigned i = 0; i < sizeof(destination); ++i) assert(destination[i] == 0xa5);
    }
}
static void argument_and_overlap_guards(void) {
    blank(); const unsigned chain[] = {30}; add(0, chain, 1, 0, "SAVE", 0);
    memset(&plan, 0xa5, sizeof(plan)); previous_plan = plan;
    assert(kui_vmu_import_plan_inspect(source, sizeof(source) - 1u, NULL, NULL, &work, &plan) == KUI_VMU_IMAGE_ARGUMENT);
    assert(!memcmp(&plan, &previous_plan, sizeof(plan)));
    memcpy(before, source, sizeof(source));
    assert(kui_vmu_import_plan_inspect(source, sizeof(source), NULL, NULL,
        &work, (struct kui_vmu_import_plan *)(void *)source) == KUI_VMU_IMAGE_ARGUMENT);
    assert(!memcmp(source, before, sizeof(source)));
    assert(kui_vmu_import_plan_inspect(source, sizeof(source), (const char *)&plan, NULL,
        &work, &plan) == KUI_VMU_IMAGE_ARGUMENT);
    assert(!memcmp(&plan, &previous_plan, sizeof(plan)));
    assert(kui_vmu_import_plan_inspect(source, sizeof(source), NULL, NULL,
        (struct kui_vmu_image_workspace *)(void *)&plan, &plan) == KUI_VMU_IMAGE_ARGUMENT);
    assert(!memcmp(&plan, &previous_plan, sizeof(plan)));
    summary = (struct kui_vmu_image_summary){9, 8, 7, 6};
    assert(kui_vmu_import_plan_build(source, sizeof(source), source, sizeof(source),
        NULL, 0u, &work, &summary) == KUI_VMU_IMAGE_ARGUMENT);
    assert(summary.files == 9u && !memcmp(source, before, sizeof(source)));
    assert(kui_vmu_import_plan_build(source, sizeof(source), destination, sizeof(destination),
        NULL, 0u, &work, (struct kui_vmu_image_summary *)(void *)source) == KUI_VMU_IMAGE_ARGUMENT);
    assert(!memcmp(source, before, sizeof(source)));
    const uint16_t duplicates[] = {0, 0}, inactive[] = {1};
    assert(kui_vmu_import_plan_build(source, sizeof(source), destination, sizeof(destination),
        duplicates, 2u, &work, &summary) == KUI_VMU_IMAGE_SUBSET_SELECTION);
    assert(kui_vmu_import_plan_build(source, sizeof(source), destination, sizeof(destination),
        inactive, 1u, &work, &summary) == KUI_VMU_IMAGE_SUBSET_SELECTION);
    for(unsigned i = 0; i < sizeof(destination); ++i) assert(destination[i] == 0xa5);
}
int main(void) {
    fat_header_metadata_and_review_hints();
    bounded_display_text();
    last_header_and_all_user_blocks();
    empty_selection_is_fully_admitted_blank();
    fail_closed_admission();
    argument_and_overlap_guards();
    puts("VMU import plan: metadata, explicit subsets, blank, admission and alias guards passed");
    return 0;
}
