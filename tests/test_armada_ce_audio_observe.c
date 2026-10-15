/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_audio_observe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define VIRTUAL UINT32_C(0x023b0100)
#define STATUS (VIRTUAL + 32u)
static unsigned checks;
#define CHECK(condition) do { ++checks; assert(condition); } while(0)
static uint8_t virtual_ram[64];
static struct { unsigned maps, reads, checks; } access;
static struct kui_retail_gd service;
static struct kui_armada_ce_audio_observation observation;
static const union kui_retail_slot tracks[] = {
    {.track = {.start_lba = 0, .end_lba = 8, .control = 4}},
    {.track = {.start_lba = 16, .end_lba = 24, .control = 0}},
    {.track = {.start_lba = 45000, .end_lba = 60000, .control = 4}}
};
static uint8_t *map(void *unused, uint32_t address, uint32_t bytes,
                    int writing) {
    (void)unused;
    CHECK(writing >= 0 && writing <= KUI_RETAIL_MAP_VALIDATE);
    ++access.maps;
    if(address < VIRTUAL || bytes > sizeof(virtual_ram) ||
       address - VIRTUAL > sizeof(virtual_ram) - bytes) return NULL;
    return virtual_ram + address - VIRTUAL;
}
static int image_check(void *unused, uint32_t lba, uint32_t count,
                       uint32_t bytes) {
    (void)unused; (void)lba; (void)count; (void)bytes;
    ++access.checks;
    return -1;
}
static int image_read(void *unused, uint32_t lba, uint32_t count,
                      uint32_t bytes, void *out) {
    (void)unused; (void)lba; (void)count; (void)bytes; (void)out;
    ++access.reads;
    return -1;
}
static const struct kui_gd_ops ops = {NULL, map, image_check, image_read};

static void parameters(uint32_t p0, uint32_t p1, uint32_t p2) {
    const uint32_t values[3] = {p0, p1, p2};
    for(unsigned word = 0; word < 3u; ++word)
        for(unsigned byte = 0; byte < 4u; ++byte)
            virtual_ram[word * 4u + byte] = (uint8_t)(values[word] >> (byte * 8u));
}
static void observe_unchanged(uint32_t function, uint32_t command,
                              int32_t result) {
    const struct kui_retail_gd before = service;
    const unsigned maps = access.maps, reads = access.reads;
    const unsigned image_checks = access.checks;
    kui_armada_ce_audio_observe(&observation, function, command, result, &service);
    CHECK(!memcmp(&before, &service, sizeof(service)));
    CHECK(access.maps == maps && access.reads == reads && access.checks == image_checks);
}
static int32_t call(uint32_t function, uint32_t r4, uint32_t r5) {
    const int32_t result = kui_retail_gd_dispatch(&service, r4, r5, 0, function);
    observe_unchanged(function, r4, result);
    return result;
}
static void complete(int32_t token) {
    CHECK(token > 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_PROCESSING);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_COMPLETED);
    CHECK(!service.pending && !service.command);
}

int main(void) {
    CHECK(sizeof(observation) == 48u);
    CHECK(kui_retail_gd_init(&service, tracks, 3u, &ops,
                            0x8c010000u, 0x8d000000u) == 0);

    /* Real CE GD requests map virtual parameter addresses only once. The
     * observer takes the accepted copy even if the caller buffer changes. */
    parameters(4, 4, 15);
    int32_t token = kui_retail_gd_dispatch(&service, KUI_RETAIL_GD_PLAY,
                                          VIRTUAL, 0, KUI_GD_REQUEST);
    CHECK(token > 0 && access.maps == 1u);
    memset(virtual_ram, 0xa5, 12u);
    observe_unchanged(KUI_GD_REQUEST, KUI_RETAIL_GD_PLAY, token);
    CHECK(observation.accepted[0] == 1u && observation.last_play_command == KUI_RETAIL_GD_PLAY);
    CHECK(observation.play_parameters[0] == 4u && observation.play_parameters[1] == 4u &&
          observation.play_parameters[2] == 15u);

    /* Rejection while another request owns the service leaves that request's
     * outputs intact. Invalid rejected addresses must never be dereferenced. */
    struct kui_armada_ce_audio_observation saved = observation;
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_PLAY2, UINT32_MAX) == 0);
    saved.rejected = 1u;
    CHECK(!memcmp(&saved, &observation, sizeof(saved)) && access.maps == 1u);
    complete(token);

    /* Unmapped and unaligned PLAY buffers reject without publishing stale
     * output fields. Parameterless audio commands accept an arbitrary R5. */
    saved = observation;
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_PLAY2, VIRTUAL + 60u) == 0);
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_PLAY, VIRTUAL + 1u) == 0);
    saved.rejected += 2u;
    CHECK(!memcmp(&saved, &observation, sizeof(saved)));
    parameters(45150u, 46000u, 0);
    complete(call(KUI_GD_REQUEST, KUI_RETAIL_GD_PLAY2, VIRTUAL));
    CHECK(observation.accepted[1] == 1u && observation.play_parameters[0] == 45150u &&
          observation.play_parameters[1] == 46000u && observation.play_parameters[2] == 0u);
    const uint32_t parameterless[] = {KUI_RETAIL_GD_PAUSE, KUI_RETAIL_GD_RELEASE, KUI_GD_STOP};
    for(unsigned i = 0; i < 3u; ++i) {
        complete(call(KUI_GD_REQUEST, parameterless[i], UINT32_MAX));
        CHECK(observation.accepted[i + 2u] == 1u && observation.last_play_command == KUI_RETAIL_GD_PLAY2);
        CHECK(observation.play_parameters[0] == 45150u && observation.play_parameters[1] == 46000u &&
              observation.play_parameters[2] == 0u);
    }

    /* EXEC/CHECK never double-count an accepted request. GD protocol reset
     * clears its own command but retains these boot-lifetime observations. */
    saved = observation;
    CHECK(call(KUI_GD_INIT, 0, 0) == 0);
    CHECK(call(KUI_GD_RESET, 0, 0) == 0);
    complete(call(KUI_GD_REQUEST, KUI_GD_NOP, 0));
    CHECK(call(KUI_GD_REQUEST, 0xfffffffcu, UINT32_MAX) == 0);
    CHECK(!memcmp(&saved, &observation, sizeof(saved)));
    CHECK(!access.reads && !access.checks && !service.diag.sectors_read);

    /* Count successful status/drive queries without changing the original
     * answers, and distinguish their rejected/invalid destination paths. */
    parameters(STATUS, STATUS + 4u, STATUS + 8u);
    const uint32_t final_status = STATUS + 12u;
    for(unsigned i = 0; i < 4u; ++i)
        virtual_ram[12u + i] = (uint8_t)(final_status >> (i * 8u));
    complete(call(KUI_GD_REQUEST, KUI_RETAIL_GD_REQ_STAT, VIRTUAL));
    CHECK(observation.status_requests == 1u && !observation.drive_calls);
    CHECK(call(KUI_GD_DRIVE, STATUS, 0) == 0);
    CHECK(observation.drive_calls == 1u);
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_REQ_STAT, UINT32_MAX) == 0);
    CHECK(call(KUI_GD_DRIVE, UINT32_MAX, 0) == -1);
    CHECK(observation.status_requests == 1u && observation.drive_calls == 1u &&
          observation.rejected == saved.rejected);
    CHECK(observation.last_play_command == saved.last_play_command &&
          !memcmp(observation.play_parameters, saved.play_parameters,
                  sizeof(saved.play_parameters)));

    /* Disabled/reentrant service results cannot dereference caller buffers or
     * be mistaken for successful audio requests. Saturation stays monotonic. */
    service.executing = 1u;
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_PAUSE, UINT32_MAX) == 0);
    service.executing = 0u;
    CHECK(observation.rejected == 4u && observation.accepted[2] == 1u);
    service.initialized = 0u;
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_PLAY, UINT32_MAX) == -1);
    service.initialized = 1u;
    CHECK(observation.rejected == 5u && observation.accepted[0] == 1u);
    observation.accepted[0] = UINT32_MAX;
    observation.rejected = UINT32_MAX;
    parameters(4, 4, 0);
    token = call(KUI_GD_REQUEST, KUI_RETAIL_GD_PLAY, VIRTUAL);
    CHECK(observation.accepted[0] == UINT32_MAX && observation.play_parameters[2] == 0u);
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_PLAY2, UINT32_MAX) == 0);
    CHECK(observation.rejected == UINT32_MAX);
    complete(token);
    observation.status_requests = UINT32_MAX;
    observation.drive_calls = UINT32_MAX;
    CHECK(call(KUI_GD_DRIVE, STATUS, 0) == 0);
    CHECK(observation.drive_calls == UINT32_MAX);
    parameters(STATUS, STATUS + 4u, STATUS + 8u);
    for(unsigned i = 0; i < 4u; ++i)
        virtual_ram[12u + i] = (uint8_t)(final_status >> (i * 8u));
    complete(call(KUI_GD_REQUEST, KUI_RETAIL_GD_REQ_STAT, VIRTUAL));
    CHECK(observation.status_requests == UINT32_MAX);
    CHECK(!access.reads && !access.checks && !service.diag.sectors_read);
    printf("Armada CE audio observation: %u checks passed\n", checks);
    return 0;
}
