/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/ata.h"
#include <stddef.h>

/* Register addresses and conservative PIO timing are from the pinned KOS
 * kernel/arch/dreamcast/hardware/g1ata.c at
 * fcfa7d869471591ca1c777543261a7bfea7cb726 (dependencies.json).
 * This transport deliberately does not call g1_ata_init: that routine installs
 * DMA handlers and changes the shared GD-ROM DMA configuration. */
static uintptr_t address(enum kui_ata_reg reg) {
    static const uintptr_t addresses[] = {
        0xa05f7018u, 0xa05f709cu, 0xa05f7084u, 0xa05f7088u,
        0xa05f708cu, 0xa05f7090u, 0xa05f7094u, 0xa05f7098u, 0xa05f709cu
    };
    return addresses[reg];
}
static uint8_t read8(void *ctx, enum kui_ata_reg reg) {
    (void)ctx;
    return *(volatile uint8_t *)address(reg);
}
static void write8(void *ctx, enum kui_ata_reg reg, uint8_t value) {
    (void)ctx;
    *(volatile uint8_t *)address(reg) = value;
}
static uint16_t read16(void *ctx) {
    (void)ctx;
    return *(volatile uint16_t *)(uintptr_t)0xa05f7080u;
}
static void write16(void *ctx, uint16_t value) {
    (void)ctx;
    *(volatile uint16_t *)(uintptr_t)0xa05f7080u = value;
}
/* A sector callback keeps the volatile 16-bit ATA accesses in one loop rather
 * than making an indirect call for every word. Buffers need not be aligned;
 * the aligned path uses GCC's alias-safe word type for byte-array storage.
 * This remains PIO: no DMA engine, IRQ handler or transfer timing is changed. */
typedef uint16_t ata_word __attribute__((__may_alias__));
static void read_sector(void *ctx, void *out) {
    (void)ctx;
    volatile uint16_t *data = (volatile uint16_t *)(uintptr_t)0xa05f7080u;
    if(!((uintptr_t)out & 1u)) {
        ata_word *p = out;
        for(unsigned i = 0; i < 256; ++i) p[i] = *data;
    } else {
        uint8_t *p = out;
        for(unsigned i = 0; i < 256; ++i) {
            uint16_t value = *data;
            p[2u*i] = (uint8_t)value;
            p[2u*i+1u] = (uint8_t)(value >> 8);
        }
    }
}
static void write_sector(void *ctx, const void *in) {
    (void)ctx;
    volatile uint16_t *data = (volatile uint16_t *)(uintptr_t)0xa05f7080u;
    if(!((uintptr_t)in & 1u)) {
        const ata_word *p = in;
        for(unsigned i = 0; i < 256; ++i) *data = p[i];
    } else {
        const uint8_t *p = in;
        for(unsigned i = 0; i < 256; ++i)
            *data = (uint16_t)(p[2u*i] | ((uint16_t)p[2u*i+1u] << 8));
    }
}
static bool dma_busy(void *ctx) {
    (void)ctx;
    return *(volatile uint32_t *)(uintptr_t)0xa05f7418u != 0;
}
/* Bus-reactivation sequence adapted from pinned KOS hardware/cdrom.c.
 * Copyright (C) 2000 Megan Potter
 * Copyright (C) 2014 Lawrence Sebald
 * Copyright (C) 2014 Donald Haase
 * Copyright (C) 2023, 2024, 2025 Ruslan Rostovtsev
 * Copyright (C) 2024 Andy Barajas
 * BSD-3-Clause; retained terms/disclaimer: LICENSES/LICENSE.KOS.
 * K-UI adaptation omits syscall_gdrom_init and all DMA/IRQ setup. */
static bool activate(void *ctx) {
    (void)ctx;
    volatile uint32_t *state = (volatile uint32_t *)(uintptr_t)0xa05f74ecu;
    if(*state == 3u) return true;
    volatile uint32_t *react = (volatile uint32_t *)(uintptr_t)0xa05f74e4u;
    volatile uint32_t *bios = (volatile uint32_t *)(uintptr_t)0xa0000000u;
    uint32_t size = *(volatile uint16_t *)(uintptr_t)0xa0000000u == 0xe6ffu ?
                    0x400u : 0x200000u;
    *react = size - 1u;
    for(uint32_t p = 0; p < size / sizeof(*bios); ++p) (void)bios[p];
    return *state == 3u;
}
static void prepare(void *ctx) {
    (void)ctx;
    /* PIO default timing is safe for both devices. These G1 timing registers
     * are write-only; do not try to save/restore an unreadable value. */
    *(volatile uint32_t *)(uintptr_t)0xa05f7490u = 0x00000222u;
    *(volatile uint32_t *)(uintptr_t)0xa05f7494u = 0x00000222u;
}
const struct kui_ata_bus *kui_ata_native_bus(void) {
    static const struct kui_ata_bus bus = {
        NULL, read8, write8, read16, write16, dma_busy, prepare, NULL, NULL, activate,
        read_sector, write_sector
    };
    return &bus;
}
