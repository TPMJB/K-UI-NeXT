/* SPDX-License-Identifier: BSD-3-Clause
 *
 * Freestanding K-UI read-only SD backend, adapted from KallistiOS:
 *   hardware/scif-spi.c: Copyright (C) 2012 Lawrence Sebald
 *     Copyright (C) 2023, 2025 Ruslan Rostovtsev
 *     Copyright (C) 2024 Paul Cercueil
 *   hardware/sd.c: Copyright (C) 2012, 2013 Lawrence Sebald
 *     Copyright (C) 2025 Ruslan Rostovtsev
 *   kernel/timer.c: Copyright (C) 2000, 2001, 2002 Megan Potter
 *     Copyright (C) 2023 Falco Girgis
 *     Copyright (C) 2023, 2024 Paul Cercueil
 * Pinned upstream: fcfa7d869471591ca1c777543261a7bfea7cb726.
 * K-UI adaptation Copyright (C) 2026 K-UI contributors.
 * See LICENSES/LICENSE.KOS for the retained BSD conditions and disclaimer.
 *
 * SCIF pin wiring/edge order follows upstream; protocol framing and CSD field
 * locations were checked against its SD implementation. Unlike that driver,
 * this file has no kernel, heap, scheduler, filesystem or interrupt dependency.
 * It issues only identification/configuration commands and CMD17 reads: no
 * data-write, erase, unlock, filesystem or persistent card-setting commands.
 */
#include "sd_reader.h"

#define SD_TICKS_MS 12500u
#define SD_READY_TICKS (500u * SD_TICKS_MS)
#define SD_INIT_TICKS (2000u * SD_TICKS_MS)
#define SD_READ_TICKS (2000u * SD_TICKS_MS)

static uint8_t transfer(struct kui_loader_sd *card, uint8_t data) {
    return card->bus.transfer(card->bus.ctx, data, card->slow);
}

static uint32_t ticks(struct kui_loader_sd *card) {
    return card->bus.ticks(card->bus.ctx);
}

static bool expired(struct kui_loader_sd *card, uint32_t start, uint32_t span) {
    return (uint32_t)(ticks(card) - start) >= span;
}

static void release(struct kui_loader_sd *card) {
    card->bus.select(card->bus.ctx, false);
    (void)transfer(card, 0xff);
}

/* CRC7 polynomial x^7+x^3+1; the returned byte includes the end bit. */
static uint8_t command_crc(const uint8_t *packet) {
    uint8_t crc = 0;
    for(unsigned byte = 0; byte < 5; ++byte) {
        uint8_t data = packet[byte];
        for(unsigned bit = 0; bit < 8; ++bit) {
            crc = (uint8_t)(crc << 1);
            if(((crc ^ data) & 0x80u) != 0)
                crc ^= 0x09u;
            data = (uint8_t)(data << 1);
        }
    }
    return (uint8_t)((crc << 1) | 1u);
}

static uint16_t data_crc(uint16_t crc, uint8_t data) {
    crc ^= (uint16_t)data << 8;
    for(unsigned bit = 0; bit < 8; ++bit)
        crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000u) ? 0x1021u : 0));
    return crc;
}

static enum kui_loader_sd_result command(struct kui_loader_sd *card,
                                        uint8_t cmd, uint32_t argument,
                                        uint8_t *response) {
    uint32_t start = ticks(card);
    card->last_command = cmd;
    card->last_response = 0xff;
    /* Iteration limits also bound an accidentally stopped timer. */
    unsigned attempts = 0;
    while(transfer(card, 0xff) != 0xff) {
        if(++attempts == 1000000u || expired(card, start, SD_READY_TICKS))
            return KUI_LOADER_SD_TIMEOUT;
    }
    uint8_t packet[6] = {
        (uint8_t)(0x40u | cmd), (uint8_t)(argument >> 24),
        (uint8_t)(argument >> 16), (uint8_t)(argument >> 8),
        (uint8_t)argument, 0
    };
    packet[5] = command_crc(packet);
    for(unsigned i = 0; i < sizeof(packet); ++i)
        (void)transfer(card, packet[i]);
    for(unsigned i = 0; i < 20; ++i) {
        uint8_t value = transfer(card, 0xff);
        if((value & 0x80u) == 0) {
            card->last_response = *response = value;
            return KUI_LOADER_SD_OK;
        }
    }
    return KUI_LOADER_SD_TIMEOUT;
}

static enum kui_loader_sd_result read_data(struct kui_loader_sd *card,
                                          uint8_t *data, size_t count) {
    uint32_t start = ticks(card);
    uint8_t token;
    unsigned attempts = 0;
    do {
        token = transfer(card, 0xff);
        if(token != 0xff)
            break;
        if(++attempts == 1000000u || expired(card, start, SD_READY_TICKS))
            return KUI_LOADER_SD_TIMEOUT;
    } while(true);
    if(token != 0xfe)
        return KUI_LOADER_SD_TOKEN;
    uint16_t crc = 0;
    for(size_t i = 0; i < count; ++i) {
        data[i] = transfer(card, 0xff);
        crc = data_crc(crc, data[i]);
    }
    uint16_t expected = (uint16_t)transfer(card, 0xff) << 8;
    expected |= transfer(card, 0xff);
    return crc == expected ? KUI_LOADER_SD_OK : KUI_LOADER_SD_CRC;
}

static enum kui_loader_sd_result capacity(struct kui_loader_sd *card,
                                         const uint8_t csd[16]) {
    unsigned version = csd[0] >> 6;
    if(version == 1 && card->high_capacity) {
        uint32_t size = ((uint32_t)(csd[7] & 0x3fu) << 16) |
                        ((uint32_t)csd[8] << 8) | csd[9];
        card->blocks = ((uint64_t)size + 1u) << 10;
    } else if(version == 0 && !card->high_capacity) {
        unsigned read_bits = csd[5] & 15u;
        unsigned multiplier = ((csd[9] & 3u) << 1) | (csd[10] >> 7);
        uint32_t size = ((uint32_t)(csd[6] & 3u) << 10) |
                        ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
        if(read_bits < 9 || read_bits > 11)
            return KUI_LOADER_SD_CAPACITY;
        card->blocks = ((uint64_t)size + 1u) << (multiplier + 2u + read_bits - 9u);
        /* CMD17's 32-bit byte address must fit for the whole SDSC card. */
        if(card->blocks > (UINT64_C(1) << 23))
            return KUI_LOADER_SD_CAPACITY;
    } else {
        return KUI_LOADER_SD_CAPACITY;
    }
    if(!card->blocks || card->blocks > (UINT64_C(1) << 32))
        return KUI_LOADER_SD_CAPACITY;
    return KUI_LOADER_SD_OK;
}

enum kui_loader_sd_result kui_loader_sd_init_bus(
    struct kui_loader_sd *card, const struct kui_loader_sd_bus *bus) {
    if(!card || !bus || !bus->begin || !bus->end || !bus->select ||
       !bus->transfer || !bus->ticks)
        return KUI_LOADER_SD_ARGUMENT;
    card->bus = *bus;
    card->ready = false;
    card->high_capacity = false;
    card->blocks = 0;
    card->slow = true;
    card->last_command = card->last_response = 0xff;
    card->bus.begin(card->bus.ctx);
    card->bus.select(card->bus.ctx, false);
    for(unsigned i = 0; i < 10; ++i)
        (void)transfer(card, 0xff);
    card->bus.select(card->bus.ctx, true);

    uint8_t response = 0xff;
    enum kui_loader_sd_result result = command(card, 0, 0, &response);
    if(result != KUI_LOADER_SD_OK)
        goto fail;
    if(response != 1) {
        result = KUI_LOADER_SD_COMMAND;
        goto fail;
    }
    result = command(card, 8, 0x1aa, &response);
    if(result != KUI_LOADER_SD_OK)
        goto fail;
    bool version2 = response == 1;
    if(version2) {
        uint8_t echo[4];
        for(unsigned i = 0; i < 4; ++i)
            echo[i] = transfer(card, 0xff);
        if(echo[0] || echo[1] || echo[2] != 1 || echo[3] != 0xaa) {
            result = KUI_LOADER_SD_UNSUPPORTED;
            goto fail;
        }
    } else if(response != 5) {
        result = KUI_LOADER_SD_COMMAND;
        goto fail;
    }

    uint32_t start = ticks(card);
    unsigned attempts = 0;
    do {
        result = command(card, 55, 0, &response);
        if(result != KUI_LOADER_SD_OK)
            goto fail;
        if(response > 1) {
            result = KUI_LOADER_SD_UNSUPPORTED; /* No MMC fallback. */
            goto fail;
        }
        result = command(card, 41, version2 ? 0x40000000u : 0, &response);
        if(result != KUI_LOADER_SD_OK)
            goto fail;
        if(response == 0)
            break;
        if(response != 1) {
            result = KUI_LOADER_SD_COMMAND;
            goto fail;
        }
        if(++attempts == 10000u || expired(card, start, SD_INIT_TICKS)) {
            result = KUI_LOADER_SD_TIMEOUT;
            goto fail;
        }
    } while(true);

    result = command(card, 58, 0, &response);
    if(result != KUI_LOADER_SD_OK)
        goto fail;
    if(response) {
        result = KUI_LOADER_SD_COMMAND;
        goto fail;
    }
    uint8_t ocr[4];
    for(unsigned i = 0; i < 4; ++i)
        ocr[i] = transfer(card, 0xff);
    /* Power-up complete, 3.2-3.4 V supported, coherent CCS/version. */
    if(!(ocr[0] & 0x80u) || !(ocr[1] & 0x30u) ||
       (!version2 && (ocr[0] & 0x40u))) {
        result = KUI_LOADER_SD_UNSUPPORTED;
        goto fail;
    }
    card->high_capacity = (ocr[0] & 0x40u) != 0;
    if(!card->high_capacity) {
        result = command(card, 16, 512, &response);
        if(result != KUI_LOADER_SD_OK)
            goto fail;
        if(response) {
            result = KUI_LOADER_SD_COMMAND;
            goto fail;
        }
    }
    result = command(card, 59, 1, &response);
    if(result != KUI_LOADER_SD_OK)
        goto fail;
    if(response) {
        result = KUI_LOADER_SD_COMMAND;
        goto fail;
    }
    release(card);
    card->slow = false;
    card->bus.select(card->bus.ctx, true);
    result = command(card, 9, 0, &response);
    if(result != KUI_LOADER_SD_OK)
        goto fail;
    if(response) {
        result = KUI_LOADER_SD_COMMAND;
        goto fail;
    }
    uint8_t csd[16];
    result = read_data(card, csd, sizeof(csd));
    if(result != KUI_LOADER_SD_OK)
        goto fail;
    result = capacity(card, csd);
    if(result != KUI_LOADER_SD_OK)
        goto fail;
    release(card);
    card->ready = true;
    return KUI_LOADER_SD_OK;

fail:
    release(card);
    card->bus.end(card->bus.ctx);
    return result;
}

enum kui_loader_sd_result kui_loader_sd_read(
    struct kui_loader_sd *card, uint32_t lba, uint32_t count, void *out) {
    if(!card || !out || !count || count > KUI_LOADER_SD_MAX_READ_BLOCKS)
        return KUI_LOADER_SD_ARGUMENT;
    if(!card->ready)
        return KUI_LOADER_SD_NOT_READY;
    if((uint64_t)lba + count > card->blocks ||
       (!card->high_capacity && (uint64_t)lba + count > (UINT64_C(1) << 23)))
        return KUI_LOADER_SD_RANGE;
    uint8_t *dest = out;
    uint32_t start = ticks(card);
    for(uint32_t i = 0; i < count; ++i) {
        if(expired(card, start, SD_READ_TICKS))
            return KUI_LOADER_SD_TIMEOUT;
        card->bus.select(card->bus.ctx, true);
        uint32_t address = lba + i;
        if(!card->high_capacity)
            address <<= 9;
        uint8_t response;
        enum kui_loader_sd_result result = command(card, 17, address, &response);
        if(result == KUI_LOADER_SD_OK)
            result = response ? KUI_LOADER_SD_COMMAND : read_data(card, dest, 512);
        release(card);
        if(result != KUI_LOADER_SD_OK)
            return result;
        dest += 512;
    }
    return KUI_LOADER_SD_OK;
}

void kui_loader_sd_shutdown(struct kui_loader_sd *card) {
    if(!card || !card->ready)
        return;
    release(card);
    card->bus.end(card->bus.ctx);
    card->ready = false;
}

const char *kui_loader_sd_result_name(enum kui_loader_sd_result result) {
    static const char *const names[] = {
        "OK", "invalid arguments", "card not initialized", "card timeout",
        "SD command rejected", "bad SD data token", "SD CRC16 mismatch",
        "unsupported capacity", "SD LBA out of range", "unsupported SD card"
    };
    return (unsigned)result < sizeof(names) / sizeof(names[0]) ? names[result] : "unknown SD error";
}

#ifdef KUI_ON_CONSOLE
#define SD_REG8(a) (*(volatile uint8_t *)(uintptr_t)(a))
#define SD_REG16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define SD_REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define SD_SCSPTR2 SD_REG16(0xffe80020u)
#define SD_TSTR SD_REG8(0xffd80004u)
#define SD_TCOR1 SD_REG32(0xffd80014u)
#define SD_TCNT1 SD_REG32(0xffd80018u)
#define SD_TCR1 SD_REG16(0xffd8001cu)
#define SD_RTSIO 0x80u
#define SD_RTSDT 0x40u
#define SD_CTSIO 0x20u
#define SD_CTSDT 0x10u
#define SD_SPB2IO 0x02u
#define SD_SPB2DT 0x01u

/* This state is resident payload BSS, never a pointer into the old kernel. */
static uint16_t native_pins;

static void native_begin(void *ctx) {
    (void)ctx;
    SD_TSTR &= (uint8_t)~2u;
    SD_TCOR1 = UINT32_MAX;
    SD_TCNT1 = UINT32_MAX;
    SD_TCR1 = 0; /* peripheral clock/4, no interrupt */
    SD_TSTR |= 2u;
    SD_REG16(0xffe80008u) = 0; /* disable SCIF transmitter/receiver/IRQs */
    SD_REG16(0xffe80018u) = 0x06; /* FIFO reset */
    SD_REG16(0xffe80018u) = 0;
    SD_REG16(0xffe80000u) = 0;
    SD_REG16(0xffe80010u) = 0;
    SD_REG16(0xffe80024u) = 0;
    native_pins = SD_RTSIO | SD_RTSDT | SD_CTSIO | SD_SPB2IO;
    SD_SCSPTR2 = native_pins;
}

static void native_end(void *ctx) {
    (void)ctx;
    native_pins |= SD_RTSDT;
    SD_SCSPTR2 = native_pins;
    SD_TSTR &= (uint8_t)~2u;
}

static uint32_t native_ticks(void *ctx) {
    (void)ctx;
    return UINT32_MAX - SD_TCNT1;
}

static void native_delay(void) {
    uint32_t start = SD_TCNT1;
    /* 25 ticks is >=2 microseconds, keeping initialization below 400 kHz.
     * The finite fallback also avoids hanging if the timer is damaged. */
    for(unsigned n = 0; n < 10000u; ++n)
        if((uint32_t)(start - SD_TCNT1) >= 25u)
            break;
}

static void native_select(void *ctx, bool selected) {
    (void)ctx;
    if(selected)
        native_pins &= (uint16_t)~SD_RTSDT;
    else
        native_pins |= SD_RTSDT;
    SD_SCSPTR2 = native_pins;
}

static uint8_t native_transfer(void *ctx, uint8_t data, bool slow) {
    (void)ctx;
    uint16_t pins = native_pins & (uint16_t)~(SD_CTSDT | SD_SPB2DT);
    uint8_t received = 0;
    for(unsigned i = 0; i < 8; ++i) {
        uint16_t bit = (data >> (7u - i)) & 1u;
        /* Tx must be established before the CTS rising clock edge. */
        SD_SCSPTR2 = pins | bit;
        if(slow)
            native_delay();
        SD_SCSPTR2 = pins | bit | SD_CTSDT;
        received = (uint8_t)((received << 1) | (SD_SCSPTR2 & SD_SPB2DT));
        if(slow)
            native_delay();
    }
    return received;
}

enum kui_loader_sd_result kui_loader_sd_init(struct kui_loader_sd *card) {
    const struct kui_loader_sd_bus bus = {
        NULL, native_begin, native_end, native_select, native_transfer, native_ticks
    };
    return kui_loader_sd_init_bus(card, &bus);
}
#else
enum kui_loader_sd_result kui_loader_sd_init(struct kui_loader_sd *card) {
    (void)card;
    return KUI_LOADER_SD_UNSUPPORTED;
}
#endif
