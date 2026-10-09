/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_pilot_bus.h"
#include <stddef.h>

/* Exact installed Toy driver contract. This owner uses short PIO transfers;
 * it never calls the game's unbounded G2 helpers or programs either DMA engine. */
#define SOUND_BASE UINT32_C(0xa0800000)
#define SOUND_END UINT32_C(0xa09f4000)
#define FIFO UINT32_C(0xa05f688c)
#define FIFO_MASK 0x31u
#define G2_DMA UINT32_C(0xa05f7800)
#define TCNT0 UINT32_C(0xffd8000c)
#define SR_BL UINT32_C(0x10000000)
#define QUEUE UINT32_C(0xa080b200)
#define NOTIFY UINT32_C(0xa080b400)
#define INSTALLED UINT32_C(0x8c0af74c)
#define HOST_QUEUE UINT32_C(0x8c112b08)
#define HOST_PRODUCER UINT32_C(0x8c112b0c)
#define HOST_NOTIFY UINT32_C(0x8c112b10)
#define HOST_COMMANDS UINT32_C(0x8c0a8924)
#define POLL_LIMIT 10000u
/* Retail TMU0 runs at nominal 781250Hz (PCLK/64). The active worker admits
 * that clock profile. A shared poll cap also bounds a stopped/reset counter. */
#define TICK_LIMIT 1563u

#ifdef KUI_TOY_PILOT_BUS_TEST
extern uint32_t kui_toy_pilot_bus_test_read(uint32_t address, unsigned width);
extern void kui_toy_pilot_bus_test_write(uint32_t address, uint32_t value,
                                        unsigned width);
extern uint32_t kui_toy_pilot_bus_test_sr_read(void);
extern void kui_toy_pilot_bus_test_sr_write(uint32_t value);
#define read32(a) kui_toy_pilot_bus_test_read((a), 4u)
#define read16(a) kui_toy_pilot_bus_test_read((a), 2u)
#define write32(a,v) kui_toy_pilot_bus_test_write((a), (v), 4u)
#define write16(a,v) kui_toy_pilot_bus_test_write((a), (v), 2u)
#define sr_read() kui_toy_pilot_bus_test_sr_read()
#define sr_write(v) kui_toy_pilot_bus_test_sr_write(v)
#else
#define read32(a) (*(volatile const uint32_t *)(uintptr_t)(a))
#define read16(a) (*(volatile const uint16_t *)(uintptr_t)(a))
#define write32(a,v) (*(volatile uint32_t *)(uintptr_t)(a) = (uint32_t)(v))
#define write16(a,v) (*(volatile uint16_t *)(uintptr_t)(a) = (uint16_t)(v))
static uint32_t sr_read(void) {
    uint32_t value;
    __asm__ __volatile__("stc sr,%0" : "=r"(value) : : "memory");
    return value;
}
static void sr_write(uint32_t value) {
    __asm__ __volatile__("ldc %0,sr" : : "r"(value) : "memory","t");
}
#endif

struct transaction { uint32_t sr, start, polls; };
static uint32_t ticks(void) { return ~read32(TCNT0); }
static enum kui_toy_pilot_bus_result begin(struct transaction *t) {
    t->sr = sr_read();
    sr_write(t->sr | 0xf0u);
    t->start = ticks();
    t->polls = 0;
    if(t->sr & SR_BL) return KUI_TOY_PILOT_BUS_BUSY;
    /* Enabled timer-triggered DMA can begin autonomously while IRQs are
     * masked. Admit only disabled AND stopped channels; never suspend them. */
    for(unsigned channel = 0; channel < 4u; ++channel) {
        uint32_t base = G2_DMA + channel * 0x20u;
        if((read32(base + 0x14u) | read32(base + 0x18u)) & 1u)
            return KUI_TOY_PILOT_BUS_BUSY;
    }
    return KUI_TOY_PILOT_BUS_OK;
}
static enum kui_toy_pilot_bus_result end(
    const struct transaction *t, enum kui_toy_pilot_bus_result result) {
    sr_write(t->sr);
    return result;
}
static int address_valid(uint32_t address, uint32_t bytes) {
    return !(address & 3u) && bytes && !(bytes & 3u) &&
        address >= SOUND_BASE && address < SOUND_END && bytes <= SOUND_END-address;
}
static int budget(struct transaction *t) {
    return t->polls < POLL_LIMIT && ticks()-t->start < TICK_LIMIT;
}
static int drain(struct transaction *t) {
    while(budget(t)) {
        ++t->polls;
        if(!(read32(FIFO) & FIFO_MASK)) return 1;
    }
    return 0;
}
static int stable(struct transaction *t, uint32_t address, uint32_t *value) {
    while(budget(t)) {
        uint32_t first, second;
        if(!drain(t)) return 0;
        first = read32(address);
        if(!drain(t)) return 0;
        second = read32(address);
        if(first == second) {
            if(!drain(t)) return 0;
            *value = first;
            return 1;
        }
    }
    return 0;
}

enum kui_toy_pilot_bus_result kui_toy_pilot_bus_read(
    uint32_t address, uint32_t *value) {
    struct transaction t;
    enum kui_toy_pilot_bus_result result = begin(&t);
    if(!value || ((uintptr_t)value & 3u) || !address_valid(address, 4u))
        result = KUI_TOY_PILOT_BUS_ARGUMENT;
    if(result == KUI_TOY_PILOT_BUS_OK && !stable(&t, address, value))
        result = KUI_TOY_PILOT_BUS_TIMEOUT;
    return end(&t, result);
}
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_write(
    uint32_t address, uint32_t value) {
    struct transaction t;
    enum kui_toy_pilot_bus_result result = begin(&t);
    if(!address_valid(address, 4u)) result = KUI_TOY_PILOT_BUS_ARGUMENT;
    if(result == KUI_TOY_PILOT_BUS_OK) {
        if(!drain(&t)) result = KUI_TOY_PILOT_BUS_TIMEOUT;
        else {
            write32(address, value);
            if(!drain(&t)) result = KUI_TOY_PILOT_BUS_TIMEOUT;
        }
    }
    return end(&t, result);
}
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_copy(
    uint32_t destination, const void *source, uint32_t bytes) {
    struct transaction t;
    enum kui_toy_pilot_bus_result result = begin(&t);
    if(!source || ((uintptr_t)source & 3u) || bytes > 256u ||
       !address_valid(destination, bytes)) result = KUI_TOY_PILOT_BUS_ARGUMENT;
    if(result == KUI_TOY_PILOT_BUS_OK) {
        /* The checked source is four-byte aligned, but can hold int16_t
         * sample planes. A fixed-size builtin copy permits one aligned word
         * load without creating an incompatible uint32_t source alias. */
        const uint8_t *input = __builtin_assume_aligned(source, 4u);
        for(uint32_t done = 0; done < bytes;) {
            if(!drain(&t)) { result = KUI_TOY_PILOT_BUS_TIMEOUT; break; }
            uint32_t count = bytes-done > 32u ? 8u : (bytes-done)/4u;
            for(uint32_t i = 0; i < count; ++i) {
                uint32_t offset = done+i*4u;
                uint32_t value;
                __builtin_memcpy(&value, input+offset, sizeof(value));
                write32(destination+offset, value);
            }
            done += count*4u;
        }
        if(result == KUI_TOY_PILOT_BUS_OK && !drain(&t))
            result = KUI_TOY_PILOT_BUS_TIMEOUT;
    }
    return end(&t, result);
}
enum kui_toy_pilot_bus_result kui_toy_pilot_bus_publish(
    const uint32_t packet[4], uint32_t *slot) {
    struct transaction t;
    enum kui_toy_pilot_bus_result result = begin(&t);
    if(!packet || ((uintptr_t)packet & 3u) || !slot || ((uintptr_t)slot & 3u) ||
       !(packet[0] & 0xffffu))
        result = KUI_TOY_PILOT_BUS_ARGUMENT;
    if(result == KUI_TOY_PILOT_BUS_OK) {
        uint32_t producer = read16(HOST_PRODUCER);
        if(read32(INSTALLED) != 1u || read32(HOST_QUEUE) != QUEUE ||
           read32(HOST_NOTIFY) != NOTIFY ||
           (producer != 0xffffu && producer >= 32u)) result = KUI_TOY_PILOT_BUS_STATE;
        else {
            uint32_t next = (producer+1u) & 31u;
            uint32_t candidate = QUEUE+16u*next, header;
            if(!stable(&t, candidate, &header)) result = KUI_TOY_PILOT_BUS_TIMEOUT;
            else if(header & 0xffffu) result = KUI_TOY_PILOT_BUS_BUSY;
            else {
                /* One <=20-byte FIFO burst: payload first, command last.
                 * A failure before the header leaves a reusable empty slot. */
                write32(candidate+12u, packet[3]);
                write32(candidate+8u, packet[2]);
                write32(candidate+4u, packet[1]);
                if(!budget(&t)) result = KUI_TOY_PILOT_BUS_TIMEOUT;
                else {
                    write16(HOST_PRODUCER, next);
                    write32(candidate, packet[0]);
                    write32(NOTIFY, UINT32_MAX);
                    write32(HOST_COMMANDS, read32(HOST_COMMANDS)+1u);
                    *slot = candidate;
                    if(!drain(&t)) result = KUI_TOY_PILOT_BUS_PUBLISHED_STALLED;
                }
            }
        }
    }
    return end(&t, result);
}
