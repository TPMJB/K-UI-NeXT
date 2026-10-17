/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_maple_observe.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t ram[1024], source;
static unsigned reads, stops;

uint32_t kui_vmu_maple_observer_test_read(uint32_t address) {
    ++reads;
    if(address == UINT32_C(0xa05f6c04))
        return source;
    if(address == UINT32_C(0xa05f6c10) || address == UINT32_C(0xa05f6c18))
        return 0u;
    if(address == UINT32_C(0xa05f6c14))
        return 1u;
    if(address == UINT32_C(0xac1223a4))
        return UINT32_C(0x8c100000);
    if(address == UINT32_C(0xac1010f4))
        return UINT32_C(0xa05f6c00);
    assert(address >= UINT32_C(0xac100000));
    assert(!(address & 3u));
    assert(address < UINT32_C(0xac100000) + sizeof(ram));
    return ram[(address - UINT32_C(0xac100000)) / 4u];
}

void kui_vmu_maple_observer_test_stop(void) {
    ++stops;
}

static void initialize(unsigned stop_after) {
    struct kui_vmu_maple_observe_config config = {
        KUI_VMU_MAPLE_OBSERVE_MAGIC, KUI_VMU_MAPLE_OBSERVE_VERSION,
        sizeof(config), stop_after, {0}
    };
    source = UINT32_C(0x0c100000);
    reads = stops = 0;
    memset(ram, 0, sizeof(ram));
    assert(kui_vmu_maple_observer_init(&config) == 0);
}

static void mixed_owned_observation(void) {
    const struct kui_vmu_maple_observe_snapshot *s = &kui_vmu_maple_observer_snapshot;
    initialize(2u);
    ram[0] = 0;
    ram[1] = UINT32_C(0x0c200000);
    ram[2] = UINT32_C(0x00002001); /* A controller DEVINFO. */
    ram[3] = UINT32_C(0x80000002);
    ram[4] = UINT32_C(0x0c200400);
    ram[5] = UINT32_C(0x0200020b); /* A2 card block read. */
    ram[6] = UINT32_C(0x02000000);
    ram[7] = UINT32_C(0x05000000);
    kui_vmu_maple_observer_visit(0x400000f1u);
    assert(s->starts == 1u && s->descriptors == 2u);
    assert(s->ports[0] == 2u && s->units[0] == 1u && s->units[2] == 1u);
    assert(s->commands[1] == 1u && s->commands[11] == 1u);
    assert(s->controller_seen == 1u && s->rx_bad == 0u);
    assert(s->source_min == source && s->source_max == source);
    assert(s->source_samples[0] == source && s->descriptors_max == 2u);
    assert(reads == 12u && stops == 0u); /* Headers and MMIO, no payload/RX reads. */
    assert(s->entry_sr_last == 0x400000f1u && s->entry_masks == (1u << 15));
    assert(s->manager_last == 0xa05f6c00u && !s->manager_other);
    kui_vmu_maple_observer_visit(0x40000000u);
    assert(s->starts == 2u && stops == 1u);
    assert(s->initialized == 2u);
    kui_vmu_maple_observer_visit(0u);
    assert(s->starts == 2u && stops == 1u && reads == 24u);
}

static void invalid_sources_and_patterns(void) {
    const struct kui_vmu_maple_observe_snapshot *s = &kui_vmu_maple_observer_snapshot;
    initialize(10u);
    source = UINT32_C(0x0d000000); /* Never dereferenced. */
    kui_vmu_maple_observer_visit(0u);
    assert(s->malformed == 1u && reads == 6u);
    source = UINT32_C(0x0c100001); /* Never dereferenced. */
    kui_vmu_maple_observer_visit(0u);
    assert(s->malformed == 2u && reads == 12u);
    source = UINT32_C(0x0c100000);
    ram[0] = UINT32_C(0x80000200); /* Unsupported gun descriptor. */
    kui_vmu_maple_observer_visit(0u);
    assert(s->unsupported == 1u && s->incomplete == 1u && reads == 19u);
    ram[0] = UINT32_C(0x80000000);
    ram[1] = UINT32_C(0xac200000); /* CPU alias is not a physical RX address. */
    ram[2] = UINT32_C(0x00002001);
    kui_vmu_maple_observer_visit(0u);
    assert(s->descriptors == 1u && s->rx_bad == 1u);
    ram[2] = UINT32_C(0x01002001); /* Inconsistent packet length. */
    kui_vmu_maple_observer_visit(0u);
    assert(s->malformed == 3u && s->descriptors == 1u);
}

static void maximum_bounded_walk(void) {
    const struct kui_vmu_maple_observe_snapshot *s = &kui_vmu_maple_observer_snapshot;
    unsigned i;
    initialize(10u);
    for(i = 0; i < 33u; ++i) {
        ram[i * 3u] = i == 32u ? UINT32_C(0x80000000) : 0u;
        ram[i * 3u + 1u] = UINT32_C(0x0c200000) + i * 1024u;
        ram[i * 3u + 2u] = UINT32_C(0x00002001);
    }
    kui_vmu_maple_observer_visit(0u);
    assert(s->descriptors == 32u && s->descriptors_max == 32u);
    assert(s->incomplete == 1u && reads == 102u);
}

static void rejects_invalid_configuration(void) {
    struct kui_vmu_maple_observe_config config = {0};
    assert(kui_vmu_maple_observer_init(NULL) == -1);
    assert(kui_vmu_maple_observer_init(&config) == -1);
    config.magic = KUI_VMU_MAPLE_OBSERVE_MAGIC;
    config.version = KUI_VMU_MAPLE_OBSERVE_VERSION;
    config.bytes = sizeof(config);
    assert(kui_vmu_maple_observer_init(&config) == -1);
    config.stop_after = UINT32_C(1000001);
    assert(kui_vmu_maple_observer_init(&config) == -1);
}

int main(void) {
    mixed_owned_observation();
    invalid_sources_and_patterns();
    maximum_bounded_walk();
    rejects_invalid_configuration();
    puts("VMU read-only Maple observer worker tests passed");
    return 0;
}
