/* SPDX-License-Identifier: BSD-3-Clause
 * Adapted ONLY from the bus-reactivation section of upstream KallistiOS
 * kernel/arch/dreamcast/hardware/cdrom.c at
 * fcfa7d869471591ca1c777543261a7bfea7cb726.
 * Copyright (C) 2000 Megan Potter
 * Copyright (C) 2014 Lawrence Sebald
 * Copyright (C) 2014 Donald Haase
 * Copyright (C) 2023, 2024, 2025 Ruslan Rostovtsev
 * Copyright (C) 2024 Andy Barajas
 * See LICENSES/LICENSE.KOS for the retained terms and disclaimer.
 * K-UI adaptation: one exclusive PIO owner; no DMA, VBlank driver or reinit.
 */
#include <stdint.h>
#include <dc/g1ata.h>
#include <dc/memory.h>
#include <dc/syscalls.h>

void kui_drive_init_bus(void) {
    volatile uint32_t *react = (uint32_t *)(G1_ATA_BUS_PROTECTION | MEM_AREA_P2_BASE);
    volatile uint32_t *state = (uint32_t *)(G1_ATA_BUS_PROTECTION_STATUS | MEM_AREA_P2_BASE);
    volatile uint32_t *bios = (uint32_t *)MEM_AREA_P2_BASE;
    if(*state != G1_ATA_BUS_PROTECTION_STATUS_PASSED) {
        uint32_t size = *(volatile uint16_t *)MEM_AREA_P2_BASE == 0xe6ff ?
            0x400 : 0x200000;
        *react = size - 1;
        for(uint32_t p = 0; p < size / sizeof(*bios); ++p) (void)bios[p];
    }
    syscall_gdrom_init();
}
