/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/storage_test.h"
#include <string.h>

void kui_storage_test_defaults(struct kui_storage_test_request *request) {
    if(!request) return;
    memset(request, 0, sizeof(*request));
    request->preset = KUI_STORAGE_TEST_QUICK;
    request->repeats = 1;
    request->soak_minutes = 15;
}

bool kui_storage_test_request_valid(const struct kui_storage_test_request *request) {
    if(!request || request->preset > KUI_STORAGE_TEST_SOAK ||
       (request->repeats != 1 && request->repeats != 3 && request->repeats != 5) ||
       (request->soak_minutes != 5 && request->soak_minutes != 15 &&
        request->soak_minutes != 30 && request->soak_minutes != 60)) return false;
    for(size_t i = 0; i < sizeof(request->card_label); ++i) {
        unsigned char c = (unsigned char)request->card_label[i];
        if(!c) return true;
        if(c < 32 || c == 127) return false;
    }
    return false;
}

const char *kui_storage_test_preset_name(unsigned preset) {
    switch(preset) {
        case KUI_STORAGE_TEST_QUICK: return "Quick";
        case KUI_STORAGE_TEST_COMPARE: return "Compare";
        case KUI_STORAGE_TEST_SOAK: return "Soak";
        default: return "Unknown";
    }
}

const char *kui_storage_test_outcome_name(unsigned outcome) {
    switch(outcome) {
        case KUI_STORAGE_TEST_NONE: return "Not run";
        case KUI_STORAGE_TEST_RUNNING: return "Running";
        case KUI_STORAGE_TEST_PASSED: return "Passed";
        case KUI_STORAGE_TEST_STOPPED: return "Stopped";
        case KUI_STORAGE_TEST_FAILED: return "Failed";
        default: return "Unknown";
    }
}

bool kui_storage_test_comparable(const struct kui_storage_test_result *a,
                                  const struct kui_storage_test_result *b) {
    return a && b && a->outcome == KUI_STORAGE_TEST_PASSED &&
        b->outcome == KUI_STORAGE_TEST_PASSED &&
        a->request.preset == b->request.preset &&
        (a->request.preset == KUI_STORAGE_TEST_SOAK ?
            a->request.soak_minutes == b->request.soak_minutes :
            a->request.repeats == b->request.repeats) &&
        !strncmp(a->metadata.filesystem, b->metadata.filesystem,
                 sizeof(a->metadata.filesystem)) &&
        a->metadata.cluster_bytes == b->metadata.cluster_bytes &&
        a->metadata.music_playing == b->metadata.music_playing &&
        a->metadata.ui_hz == b->metadata.ui_hz;
}

uint32_t kui_storage_test_rate(const struct kui_storage_test_result *result,
                               uint32_t chunk_bytes, bool write) {
    if(!result) return 0;
    uint32_t rates[KUI_STORAGE_TEST_SAMPLES];
    unsigned count = 0;
    for(unsigned i = 0; i < result->sample_count && i < KUI_STORAGE_TEST_SAMPLES; ++i) {
        const struct kui_storage_test_sample *sample = &result->samples[i];
        uint64_t us = write ? sample->write_us : sample->read_us;
        if(!sample->verified || sample->chunk_bytes != chunk_bytes || !us ||
           sample->bytes > UINT64_MAX / UINT64_C(1000000)) continue;
        /* At most an hour on the supported transports: bytes * 1e6 fits u64. */
        uint64_t rate = sample->bytes * UINT64_C(1000000) / us / 1024;
        uint32_t value = rate > UINT32_MAX ? UINT32_MAX : (uint32_t)rate;
        unsigned j = count;
        while(j && rates[j - 1] > value) { rates[j] = rates[j - 1]; --j; }
        rates[j] = value;
        ++count;
    }
    if(!count) return 0;
    if(count & 1u) return rates[count / 2];
    return (uint32_t)(((uint64_t)rates[count / 2 - 1] + rates[count / 2]) / 2);
}
