/* SPDX-License-Identifier: GPL-3.0-only */
/* Software model of the pilot's bounded bus contract, not console timing. */
#include "kui/toy_pilot_bus.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define SOUND UINT32_C(0xa0800000)
#define SOUND_END UINT32_C(0xa09f4000)
#define FIFO UINT32_C(0xa05f688c)
#define G2_DMA UINT32_C(0xa05f7800)
#define TCNT0 UINT32_C(0xffd8000c)
#define QUEUE (SOUND+0xb200u)
#define NOTIFY (SOUND+0xb400u)
#define HOST_QUEUE UINT32_C(0x8c112b08)
#define HOST_PRODUCER UINT32_C(0x8c112b0c)
#define HOST_NOTIFY UINT32_C(0x8c112b10)
#define INSTALLED UINT32_C(0x8c0af74c)
#define HOST_COMMANDS UINT32_C(0x8c0a8924)
#define INITIAL_SR UINT32_C(0x60000353)
static struct {
    uint8_t sound[SOUND_END-SOUND];
    uint32_t dma[32], sr, ticks, step, fifo_stuck, unstable_address;
    uint32_t installed, queue, producer, notify, commands;
    unsigned fifo_reads, fifo_bytes, max_fifo, sound_reads, sound_writes;
    unsigned dma_reads, sr_writes, expire_write, stuck_write;
    struct { uint32_t address, value; unsigned width; } log[128];
    unsigned log_count;
} hw;
static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while(0)
static void reset(void) {
    memset(&hw, 0, sizeof(hw));
    hw.sr = INITIAL_SR;
    hw.installed = 1u;
    hw.queue = QUEUE;
    hw.producer = 0xffffu;
    hw.notify = NOTIFY;
    hw.commands = 77u;
}
static uint32_t sound_get(uint32_t address) {
    assert(address >= SOUND && address <= SOUND_END-4u);
    uint32_t value;
    memcpy(&value, hw.sound+address-SOUND, sizeof(value));
    return value;
}
static void sound_set(uint32_t address, uint32_t value) {
    assert(address >= SOUND && address <= SOUND_END-4u);
    memcpy(hw.sound+address-SOUND, &value, sizeof(value));
}
uint32_t kui_toy_pilot_bus_test_sr_read(void) { return hw.sr; }
void kui_toy_pilot_bus_test_sr_write(uint32_t value) {
    hw.sr = value;
    ++hw.sr_writes;
}
uint32_t kui_toy_pilot_bus_test_read(uint32_t address, unsigned width) {
    assert((hw.sr & 0xf0u) == 0xf0u);
    if(address == HOST_PRODUCER) { assert(width == 2u); return hw.producer; }
    assert(width == 4u);
    if(address == TCNT0) { hw.ticks += hw.step; return ~hw.ticks; }
    if(address == FIFO) {
        ++hw.fifo_reads;
        if(hw.fifo_stuck) return hw.fifo_stuck;
        hw.fifo_bytes = 0;
        return 0;
    }
    if(address >= G2_DMA && address < G2_DMA+0x80u) {
        ++hw.dma_reads;
        return hw.dma[(address-G2_DMA)/4u];
    }
    if(address == INSTALLED) return hw.installed;
    if(address == HOST_QUEUE) return hw.queue;
    if(address == HOST_NOTIFY) return hw.notify;
    if(address == HOST_COMMANDS) return hw.commands;
    ++hw.sound_reads;
    if(address == hw.unstable_address) return hw.sound_reads;
    return sound_get(address);
}
void kui_toy_pilot_bus_test_write(uint32_t address, uint32_t value, unsigned width) {
    assert((hw.sr & 0xf0u) == 0xf0u);
    /* No production path may write a DMA or timer register. */
    assert(!(address >= G2_DMA && address < G2_DMA+0x80u));
    assert(address != TCNT0);
    assert(hw.log_count < sizeof(hw.log)/sizeof(hw.log[0]));
    hw.log[hw.log_count].address = address;
    hw.log[hw.log_count].value = value;
    hw.log[hw.log_count].width = width;
    ++hw.log_count;
    if(address == HOST_PRODUCER) {
        assert(width == 2u); hw.producer = value; return;
    }
    assert(width == 4u);
    if(address == HOST_COMMANDS) { hw.commands = value; return; }
    sound_set(address, value);
    ++hw.sound_writes;
    hw.fifo_bytes += 4u;
    assert(hw.fifo_bytes <= 32u);
    if(hw.fifo_bytes > hw.max_fifo) hw.max_fifo = hw.fifo_bytes;
    if(hw.sound_writes == hw.expire_write) hw.ticks += 1563u;
    if(hw.sound_writes == hw.stuck_write) hw.fifo_stuck = 0x31u;
}
static void restored(void) { CHECK(hw.sr == INITIAL_SR); }
static void observations(void) {
    reset(); sound_set(SOUND+0x14a4u, 0x1234u);
    uint32_t value = UINT32_MAX;
    CHECK(kui_toy_pilot_bus_read(SOUND+0x14a4u, &value) == KUI_TOY_PILOT_BUS_OK);
    CHECK(value == 0x1234u && hw.sound_reads == 2u && hw.fifo_reads == 3u);
    restored();
    CHECK(kui_toy_pilot_bus_read(SOUND+1u, &value) == KUI_TOY_PILOT_BUS_ARGUMENT);
    CHECK(kui_toy_pilot_bus_read(SOUND_END, &value) == KUI_TOY_PILOT_BUS_ARGUMENT);
    CHECK(kui_toy_pilot_bus_read(SOUND, NULL) == KUI_TOY_PILOT_BUS_ARGUMENT);
    _Alignas(4) uint8_t output[8] = {0};
    CHECK(kui_toy_pilot_bus_read(SOUND, (uint32_t *)(void *)(output+1u)) ==
          KUI_TOY_PILOT_BUS_ARGUMENT);
    restored();
    for(unsigned bit = 0; bit < 3u; ++bit) {
        reset(); hw.fifo_stuck = bit == 0u ? 1u : bit == 1u ? 0x10u : 0x20u;
        value = 0xfeedu;
        CHECK(kui_toy_pilot_bus_read(SOUND, &value) == KUI_TOY_PILOT_BUS_TIMEOUT);
        CHECK(value == 0xfeedu && hw.fifo_reads == 10000u && !hw.sound_reads);
        restored();
    }
    reset(); hw.unstable_address = SOUND;
    CHECK(kui_toy_pilot_bus_read(SOUND, &value) == KUI_TOY_PILOT_BUS_TIMEOUT);
    CHECK(hw.sound_reads <= 10000u && hw.fifo_reads == 10000u);
    restored();
    reset(); hw.fifo_stuck = 1u; hw.step = 100u;
    CHECK(kui_toy_pilot_bus_read(SOUND, &value) == KUI_TOY_PILOT_BUS_TIMEOUT);
    CHECK(hw.fifo_reads < 20u);
    restored();
    reset(); hw.ticks = UINT32_MAX-500u; hw.step = 100u;
    CHECK(kui_toy_pilot_bus_read(SOUND, &value) == KUI_TOY_PILOT_BUS_OK);
    restored();
}
static void admission(void) {
    uint32_t value = 0;
    for(unsigned c = 0; c < 4u; ++c) for(unsigned reg = 0; reg < 2u; ++reg) {
        reset(); hw.dma[c*8u+5u+reg] = 1u;
        CHECK(kui_toy_pilot_bus_read(SOUND, &value) == KUI_TOY_PILOT_BUS_BUSY);
        CHECK(!hw.sound_reads && !hw.sound_writes && !hw.fifo_reads && !hw.log_count);
        restored();
    }
    reset(); hw.sr |= UINT32_C(0x10000000);
    CHECK(kui_toy_pilot_bus_write(SOUND, 0) == KUI_TOY_PILOT_BUS_BUSY);
    CHECK(hw.sr == (INITIAL_SR | UINT32_C(0x10000000)) && !hw.dma_reads && !hw.sound_writes);
    reset(); hw.sr |= 0xf0u;
    CHECK(kui_toy_pilot_bus_write(SOUND, 99u) == KUI_TOY_PILOT_BUS_OK);
    CHECK(hw.sr == (INITIAL_SR | 0xf0u) && sound_get(SOUND) == 99u);
}
static void copies(void) {
    uint32_t source[64];
    for(unsigned i = 0; i < 64u; ++i) source[i] = i*0x1020304u;
    reset();
    CHECK(kui_toy_pilot_bus_copy(SOUND+0x30040u, source, sizeof(source)) == KUI_TOY_PILOT_BUS_OK);
    CHECK(hw.sound_writes == 64u && hw.max_fifo == 32u && hw.fifo_reads == 9u);
    for(unsigned i = 0; i < 64u; ++i) CHECK(sound_get(SOUND+0x30040u+i*4u) == source[i]);
    restored();
    CHECK(kui_toy_pilot_bus_copy(SOUND, source, 260u) == KUI_TOY_PILOT_BUS_ARGUMENT);
    CHECK(kui_toy_pilot_bus_copy(SOUND, source, 0u) == KUI_TOY_PILOT_BUS_ARGUMENT);
    CHECK(kui_toy_pilot_bus_copy(SOUND_END-4u, source, 8u) == KUI_TOY_PILOT_BUS_ARGUMENT);
    CHECK(kui_toy_pilot_bus_copy(SOUND, (uint8_t *)source+1u, 4u) == KUI_TOY_PILOT_BUS_ARGUMENT);
    restored();
    reset(); hw.stuck_write = 8u;
    CHECK(kui_toy_pilot_bus_copy(SOUND, source, sizeof(source)) == KUI_TOY_PILOT_BUS_TIMEOUT);
    CHECK(hw.sound_writes == 8u && hw.max_fifo == 32u && hw.fifo_reads == 10000u);
    restored();
    reset(); hw.step = 200u;
    CHECK(kui_toy_pilot_bus_copy(SOUND, source, sizeof(source)) == KUI_TOY_PILOT_BUS_TIMEOUT);
    CHECK(hw.sound_writes < 64u); /* All bursts share one deadline. */
    restored();
    reset(); hw.stuck_write = 1u;
    CHECK(kui_toy_pilot_bus_write(SOUND_END-4u, 0xdeadbeefu) == KUI_TOY_PILOT_BUS_TIMEOUT);
    CHECK(sound_get(SOUND_END-4u) == 0xdeadbeefu && hw.sound_writes == 1u);
    restored();
    reset();
    _Alignas(4) int16_t samples[4] = {0x1234, -2, 0x5678, -3};
    CHECK(kui_toy_pilot_bus_copy(SOUND, samples, sizeof(samples)) == KUI_TOY_PILOT_BUS_OK);
    CHECK(sound_get(SOUND) == UINT32_C(0xfffe1234) &&
          sound_get(SOUND+4u) == UINT32_C(0xfffd5678));
    restored();
}
static void publication(void) {
    const uint32_t packet[4] = {0x003eff90u, 0x87654321u, 0x11223344u, 0xaabbccddu};
    uint32_t slot = UINT32_MAX;
    reset();
    CHECK(kui_toy_pilot_bus_publish(packet, &slot) == KUI_TOY_PILOT_BUS_OK);
    CHECK(slot == QUEUE && hw.producer == 0u && hw.commands == 78u);
    CHECK(hw.log_count == 7u && hw.max_fifo == 20u);
    CHECK(hw.log[0].address == QUEUE+12u && hw.log[1].address == QUEUE+8u &&
          hw.log[2].address == QUEUE+4u && hw.log[3].address == HOST_PRODUCER &&
          hw.log[4].address == QUEUE && hw.log[5].address == NOTIFY &&
          hw.log[6].address == HOST_COMMANDS);
    for(unsigned i = 0; i < 4u; ++i) CHECK(sound_get(QUEUE+i*4u) == packet[i]);
    CHECK(sound_get(NOTIFY) == UINT32_MAX);
    restored();
    reset(); hw.producer = 31u;
    CHECK(kui_toy_pilot_bus_publish(packet, &slot) == KUI_TOY_PILOT_BUS_OK);
    CHECK(hw.producer == 0u && slot == QUEUE);
    reset(); hw.producer = 7u;
    CHECK(kui_toy_pilot_bus_publish(packet, &slot) == KUI_TOY_PILOT_BUS_OK);
    CHECK(hw.producer == 8u && slot == QUEUE+128u);
    restored();
    reset(); hw.producer = 31u; sound_set(QUEUE, 0x1234u); slot = UINT32_MAX;
    CHECK(kui_toy_pilot_bus_publish(packet, &slot) == KUI_TOY_PILOT_BUS_BUSY);
    CHECK(!hw.sound_writes && hw.producer == 31u && hw.commands == 77u && slot == UINT32_MAX);
    restored();
    for(unsigned field = 0; field < 4u; ++field) {
        reset();
        if(field == 0u) hw.installed = 0;
        if(field == 1u) hw.queue += 4u;
        if(field == 2u) hw.notify += 4u;
        if(field == 3u) hw.producer = 32u;
        CHECK(kui_toy_pilot_bus_publish(packet, &slot) == KUI_TOY_PILOT_BUS_STATE);
        CHECK(!hw.sound_reads && !hw.sound_writes && !hw.log_count);
        restored();
    }
    reset(); hw.expire_write = 3u; slot = UINT32_MAX;
    CHECK(kui_toy_pilot_bus_publish(packet, &slot) == KUI_TOY_PILOT_BUS_TIMEOUT);
    CHECK(hw.sound_writes == 3u && !sound_get(QUEUE) && !sound_get(NOTIFY));
    CHECK(hw.producer == 0xffffu && hw.commands == 77u && slot == UINT32_MAX);
    restored();
    reset(); hw.stuck_write = 4u; slot = UINT32_MAX;
    CHECK(kui_toy_pilot_bus_publish(packet, &slot) == KUI_TOY_PILOT_BUS_PUBLISHED_STALLED);
    CHECK(slot == QUEUE && hw.producer == 0u && hw.commands == 78u);
    CHECK(hw.sound_writes == 5u && sound_get(QUEUE) == packet[0] && sound_get(NOTIFY) == UINT32_MAX);
    restored();
    reset();
    const uint32_t empty[4] = {0,0,0,0};
    CHECK(kui_toy_pilot_bus_publish(empty, &slot) == KUI_TOY_PILOT_BUS_ARGUMENT);
    CHECK(kui_toy_pilot_bus_publish(NULL, &slot) == KUI_TOY_PILOT_BUS_ARGUMENT);
    CHECK(kui_toy_pilot_bus_publish(packet, NULL) == KUI_TOY_PILOT_BUS_ARGUMENT);
    _Alignas(4) uint8_t output[8] = {0};
    CHECK(kui_toy_pilot_bus_publish(packet, (uint32_t *)(void *)(output+1u)) ==
          KUI_TOY_PILOT_BUS_ARGUMENT);
    restored();
}
int main(void) {
    observations(); admission(); copies(); publication();
    printf("Toy pilot bus: %u checks passed\n", checks);
    return 0;
}
