/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_import_plan.h"
#include <string.h>

static int plan_overlap(const void *a, size_t an, const void *b, size_t bn) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return an && bn && (x <= y ? y - x < an : x - y < bn);
}
static int plan_range(const void *p, size_t bytes) {
    return p && bytes && (uintptr_t)p <= UINTPTR_MAX - (bytes - 1u);
}
static unsigned plan_le16(const uint8_t *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}
static const uint8_t *plan_entry(const uint8_t *source, unsigned slot) {
    return source + (253u - slot / 16u) * 512u + (slot % 16u) * 32u;
}
static int plan_read(void *context, uint16_t block, void *out) {
    if(block >= KUI_VMU_IMAGE_BLOCKS) return -1;
    memcpy(out, (const uint8_t *)context + (size_t)block * 512u, 512u);
    return 0;
}
static void plan_text(char *out, const uint8_t *in, size_t bytes) {
    size_t n = 0;
    while(n < bytes && in[n]) {
        out[n] = in[n] >= 0x20u && in[n] <= 0x7eu ? (char)in[n] : '?';
        ++n;
    }
    while(n && out[n - 1u] == ' ') --n;
    out[n] = 0;
}
static size_t plan_key_bytes(const char *key, size_t bound) {
    if(!key) return 0;
    size_t n = 0;
    while(n < bound && key[n]) ++n;
    return n < bound ? n + 1u : bound;
}
static unsigned plan_upper(unsigned c) {
    return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}
static int plan_text_equal(const uint8_t *field, size_t bytes,
                           const char *key, size_t key_bound) {
    if(!key) return 0;
    size_t n = 0, k = 0, start = 0, key_start = 0;
    while(n < bytes && field[n]) {
        if(field[n] < 0x20u || field[n] > 0x7eu) return 0;
        ++n;
    }
    while(k < key_bound && key[k]) {
        if((unsigned char)key[k] < 0x20u || (unsigned char)key[k] > 0x7eu)
            return 0;
        ++k;
    }
    if(k == key_bound) return 0;
    while(start < n && field[start] == ' ') ++start;
    while(key_start < k && key[key_start] == ' ') ++key_start;
    while(n > start && field[n - 1u] == ' ') --n;
    while(k > key_start && key[k - 1u] == ' ') --k;
    if(n - start < 4u || n - start != k - key_start) return 0;
    for(size_t i = 0; i < n - start; ++i)
        if(plan_upper(field[start + i]) !=
           plan_upper((unsigned char)key[key_start + i])) return 0;
    return 1;
}
static unsigned plan_hints(const uint8_t *field, size_t bytes,
                           const char *title, const char *product) {
    unsigned hints = 0;
    if(plan_text_equal(field, bytes, title, KUI_VMU_IMPORT_TITLE_BYTES))
        hints |= KUI_VMU_IMPORT_HINT_TITLE_TEXT;
    if(plan_text_equal(field, bytes, product, KUI_VMU_IMPORT_PRODUCT_BYTES))
        hints |= KUI_VMU_IMPORT_HINT_PRODUCT_TEXT;
    return hints;
}

int kui_vmu_import_plan_inspect(const void *source, size_t source_bytes,
    const char *title, const char *product,
    struct kui_vmu_image_workspace *work, struct kui_vmu_import_plan *plan) {
    if(source_bytes != KUI_VMU_IMAGE_BYTES ||
       !plan_range(source, source_bytes) || !plan_range(work, sizeof(*work)) ||
       !plan_range(plan, sizeof(*plan))) return KUI_VMU_IMAGE_ARGUMENT;
    const void *read_only[] = {source, title, product};
    const size_t read_sizes[] = {source_bytes,
        plan_key_bytes(title, KUI_VMU_IMPORT_TITLE_BYTES),
        plan_key_bytes(product, KUI_VMU_IMPORT_PRODUCT_BYTES)};
    for(unsigned i = 0; i < 3u; ++i) {
        if(read_sizes[i] && !plan_range(read_only[i], read_sizes[i]))
            return KUI_VMU_IMAGE_ARGUMENT;
        if(plan_overlap(work, sizeof(*work), read_only[i], read_sizes[i]) ||
           plan_overlap(plan, sizeof(*plan), read_only[i], read_sizes[i]))
            return KUI_VMU_IMAGE_ARGUMENT;
    }
    if(plan_overlap(work, sizeof(*work), plan, sizeof(*plan)))
        return KUI_VMU_IMAGE_ARGUMENT;
    memset(plan, 0, sizeof(*plan));
    const struct kui_vmu_image_io io = {(void *)source, plan_read, NULL, NULL};
    int result = kui_vmu_image_validate(&io, source_bytes, work, &plan->summary);
    if(result) return result;
    const uint8_t *input = source;
    for(unsigned slot = 0; slot < KUI_VMU_IMAGE_DIRECTORY_ENTRIES; ++slot) {
        const uint8_t *entry = plan_entry(input, slot);
        if(!entry[0]) continue;
        struct kui_vmu_import_entry *out = &plan->entries[plan->entry_count++];
        out->slot = (uint16_t)slot;
        out->blocks = (uint16_t)plan_le16(entry + 24u);
        out->header_offset = (uint16_t)plan_le16(entry + 26u);
        out->type = entry[0];
        unsigned at = plan_le16(entry + 2u);
        /* Full admission proves the header lies on this bounded, complete
         * file chain. Follow links, never assume consecutive data blocks. */
        for(unsigned n = 0; n < out->header_offset; ++n)
            at = plan_le16(work->fat + at * 2u);
        const uint8_t *header = input + at * 512u;
        plan_text(out->name, entry + 4u, 12u);
        plan_text(out->short_description, header, 16u);
        plan_text(out->long_description, header + 16u, 32u);
        plan_text(out->application, header + 48u, 16u);
        out->hints = (uint8_t)(plan_hints(entry + 4u, 12u, title, product) |
            plan_hints(header, 16u, title, product) |
            plan_hints(header + 16u, 32u, title, product) |
            plan_hints(header + 48u, 16u, title, product));
    }
    return KUI_VMU_IMAGE_OK;
}

int kui_vmu_import_plan_build(const void *source, size_t source_bytes,
    void *destination, size_t destination_bytes,
    const uint16_t *slots, size_t slot_count,
    struct kui_vmu_image_workspace *work,
    struct kui_vmu_image_summary *summary) {
    if(slot_count) return kui_vmu_image_subset(source, source_bytes,
        destination, destination_bytes, slots, slot_count, work, summary);
    if(source_bytes != KUI_VMU_IMAGE_BYTES ||
       destination_bytes != KUI_VMU_IMAGE_BYTES ||
       !plan_range(source, source_bytes) ||
       !plan_range(destination, destination_bytes) ||
       !plan_range(work, sizeof(*work)) || !plan_range(summary, sizeof(*summary)))
        return KUI_VMU_IMAGE_ARGUMENT;
    const void *mutable[] = {destination, work, summary};
    const size_t sizes[] = {destination_bytes, sizeof(*work), sizeof(*summary)};
    for(unsigned i = 0; i < 3u; ++i) {
        if(plan_overlap(mutable[i], sizes[i], source, source_bytes))
            return KUI_VMU_IMAGE_ARGUMENT;
        for(unsigned j = 0; j < i; ++j)
            if(plan_overlap(mutable[i], sizes[i], mutable[j], sizes[j]))
                return KUI_VMU_IMAGE_ARGUMENT;
    }
    memset(summary, 0, sizeof(*summary));
    struct kui_vmu_image_io io = {(void *)source, plan_read, NULL, NULL};
    struct kui_vmu_image_summary admitted;
    int result = kui_vmu_image_validate(&io, source_bytes, work, &admitted);
    if(result) return result;
    for(unsigned block = 0; block < KUI_VMU_IMAGE_BLOCKS; ++block)
        if(kui_vmu_image_blank_block((uint16_t)block,
                                    (uint8_t *)destination + block * 512u))
            return KUI_VMU_IMAGE_VERIFY;
    io.context = destination;
    struct kui_vmu_image_summary verified;
    result = kui_vmu_image_validate(&io, destination_bytes, work, &verified);
    if(result || verified.files || verified.game_files || verified.used_blocks ||
       verified.free_blocks != KUI_VMU_IMAGE_USER_BLOCKS)
        return KUI_VMU_IMAGE_VERIFY;
    *summary = verified;
    return KUI_VMU_IMAGE_OK;
}
