/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ATA_H
#define KUI_ATA_H
#include <stdbool.h>
#include <stdint.h>

/* G1 ATA slave only: the retained GD-ROM remains master. Callers must serialize
 * an entire operation with GD-ROM operations (the KOS G1 semaphore at runtime).
 * The resident game reader is single-threaded and uses the same protocol.
 * No reset, DMA, interrupt-handler installation, heap, or timer ownership. */
enum kui_ata_reg {
    KUI_ATA_ALTSTATUS, KUI_ATA_STATUS, KUI_ATA_ERROR, KUI_ATA_COUNT,
    KUI_ATA_LBA0, KUI_ATA_LBA1, KUI_ATA_LBA2, KUI_ATA_DEVICE, KUI_ATA_COMMAND
};
struct kui_ata_bus {
    void *ctx;
    uint8_t (*read8)(void *, enum kui_ata_reg);
    void (*write8)(void *, enum kui_ata_reg, uint8_t);
    uint16_t (*read16)(void *);
    void (*write16)(void *, uint16_t);
    bool (*dma_busy)(void *);
    /* Optional conservative PIO timing setup, called only on an idle G1 bus. */
    void (*prepare)(void *);
    /* Optional monotonic clock/pause. Every wait also has a finite poll cap,
     * so resident code needs no clock and a stopped clock cannot hang it. */
    uint64_t (*now_us)(void *);
    void (*pause)(void *);
    /* Optional platform bus-protection unlock before any taskfile access.
     * Native G1 reads the BIOS authentication range; never resets the GD-ROM. */
    bool (*activate)(void *);
};
enum kui_ata_error {
    KUI_ATA_OK, KUI_ATA_INVALID, KUI_ATA_BUSY, KUI_ATA_ABSENT,
    KUI_ATA_TIMEOUT, KUI_ATA_DEVICE_ERROR, KUI_ATA_UNSUPPORTED, KUI_ATA_RANGE,
    KUI_ATA_PROTECTION
};
struct kui_ata {
    const struct kui_ata_bus *bus;
    uint64_t sectors;
    bool ready, lba48, write_cache, flush_supported;
    enum kui_ata_error error;
};

/* IDENTIFY validates 512-byte logical sectors and LBA28/LBA48 capacity.
 * On successful operations the exact previous device-select byte is restored.
 * A device stuck BSY/DRQ is NOT forcibly deselected: ATA forbids that. Such a
 * timeout invalidates this instance; recovery requires hardware recovery/reboot.
 * When copying state to resident memory, rebind bus to its own native accessor. */
bool kui_ata_init(struct kui_ata *, const struct kui_ata_bus *);
bool kui_ata_read(struct kui_ata *, uint64_t lba, uint32_t count, void *data);
bool kui_ata_write(struct kui_ata *, uint64_t lba, uint32_t count, const void *data);
bool kui_ata_sync(struct kui_ata *);
/* Software-only; the caller must sync before shutdown. */
void kui_ata_shutdown(struct kui_ata *);
const struct kui_ata_bus *kui_ata_native_bus(void);
#endif
