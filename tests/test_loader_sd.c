/* SPDX-License-Identifier: GPL-3.0-only */
#include "sd_reader.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum fault {
    FAULT_NONE, FAULT_NO_RESPONSE, FAULT_RESET, FAULT_ECHO, FAULT_VOLTAGE,
    FAULT_POWER, FAULT_CMD55, FAULT_CMD41, FAULT_INIT_TIMEOUT, FAULT_CMD16,
    FAULT_CMD59, FAULT_CSD_COMMAND, FAULT_CSD_TOKEN, FAULT_CSD_CRC,
    FAULT_CSD_VERSION, FAULT_CSD_SIZE, FAULT_READ_COMMAND, FAULT_READ_TOKEN,
    FAULT_READ_TIMEOUT, FAULT_READ_CRC, FAULT_BUSY, FAULT_STOPPED_TIMER
};

struct mock {
    bool version2, high_capacity, selected, initialized, crc_enabled;
    unsigned begin, end, packets, reads, attempts, slow_bytes, fast_bytes;
    unsigned frame_length, head, tail, fault;
    uint8_t frame[6], queue[520];
    uint32_t now, last_read_address;
    const uint8_t *payload;
};

static unsigned checks;
#define CHECK(x) do { assert(x); ++checks; } while(0)

static uint8_t reference_crc7(const uint8_t *bytes, unsigned count) {
    unsigned remainder = 0;
    for(unsigned i = 0; i < count; ++i) {
        for(int bit = 7; bit >= 0; --bit) {
            unsigned feedback = ((remainder >> 6) ^ (bytes[i] >> bit)) & 1u;
            remainder = (remainder << 1) & 127u;
            if(feedback)
                remainder ^= 9u;
        }
    }
    return (uint8_t)((remainder << 1) | 1u);
}

static uint16_t reference_crc16(const uint8_t *bytes, unsigned count) {
    uint16_t crc = 0;
    for(unsigned i = 0; i < count; ++i) {
        /* Serial polynomial division, independent of the retail byte fold. */
        for(int bit = 7; bit >= 0; --bit) {
            unsigned feedback = ((crc >> 15) ^ (bytes[i] >> bit)) & 1u;
            crc = (uint16_t)(crc << 1);
            if(feedback) crc ^= 0x1021u;
        }
    }
    return crc;
}

static uint8_t pattern(uint32_t address, unsigned i) {
    return (uint8_t)((address >> ((i & 3u) * 8u)) ^ (i * 29u + 7u));
}

static void push(struct mock *m, uint8_t byte) {
    assert(m->tail < sizeof(m->queue));
    m->queue[m->tail++] = byte;
}

static void block(struct mock *m, const uint8_t *data, unsigned count, bool corrupt) {
    push(m, 0xff);
    push(m, 0xfe);
    uint16_t crc = reference_crc16(data, count);
    for(unsigned i = 0; i < count; ++i)
        push(m, data[i]);
    if(corrupt)
        crc ^= 1u;
    push(m, (uint8_t)(crc >> 8));
    push(m, (uint8_t)crc);
}

static void decode(struct mock *m) {
    unsigned cmd = m->frame[0] & 63u;
    uint32_t argument = ((uint32_t)m->frame[1] << 24) |
                        ((uint32_t)m->frame[2] << 16) |
                        ((uint32_t)m->frame[3] << 8) | m->frame[4];
    CHECK(m->frame[5] == reference_crc7(m->frame, 5));
    ++m->packets;
    m->head = m->tail = 0;
    if(m->fault == FAULT_NO_RESPONSE)
        return;
    switch(cmd) {
        case 0:
            CHECK(argument == 0 && m->frame[5] == 0x95);
            push(m, m->fault == FAULT_RESET ? 4 : 1);
            break;
        case 8:
            CHECK(argument == 0x1aa && m->frame[5] == 0x87);
            push(m, m->version2 ? 1 : 5);
            if(m->version2) {
                push(m, 0); push(m, 0); push(m, 1);
                push(m, m->fault == FAULT_ECHO ? 0xab : 0xaa);
            }
            break;
        case 55:
            CHECK(argument == 0);
            push(m, m->fault == FAULT_CMD55 ? 5 : 1);
            break;
        case 41:
            CHECK(argument == (m->version2 ? 0x40000000u : 0));
            ++m->attempts;
            if(m->fault == FAULT_CMD41)
                push(m, 4);
            else if(m->fault == FAULT_INIT_TIMEOUT || m->attempts < 3)
                push(m, 1);
            else {
                push(m, 0);
                m->initialized = true;
            }
            break;
        case 58:
            CHECK(argument == 0 && m->initialized);
            push(m, 0);
            push(m, (uint8_t)((m->fault == FAULT_POWER ? 0 : 0x80) |
                             (m->high_capacity ? 0x40 : 0)));
            push(m, m->fault == FAULT_VOLTAGE ? 0 : 0xff);
            push(m, 0x80); push(m, 0);
            break;
        case 16:
            CHECK(argument == 512 && !m->high_capacity);
            push(m, m->fault == FAULT_CMD16 ? 4 : 0);
            break;
        case 59:
            CHECK(argument == 1);
            push(m, m->fault == FAULT_CMD59 ? 4 : 0);
            m->crc_enabled = m->fault != FAULT_CMD59;
            break;
        case 9: {
            CHECK(argument == 0 && m->crc_enabled);
            push(m, m->fault == FAULT_CSD_COMMAND ? 4 : 0);
            if(m->fault == FAULT_CSD_COMMAND)
                break;
            if(m->fault == FAULT_CSD_TOKEN) {
                push(m, 0x08);
                break;
            }
            uint8_t csd[16] = {0};
            if(m->high_capacity) {
                csd[0] = 0x40;
                csd[9] = 7; /* 8192 sectors. */
            } else {
                csd[5] = m->fault == FAULT_CSD_SIZE ? 15 : 9;
                csd[7] = 0xff;
                csd[8] = 0xc0; /* 4096 sectors. */
            }
            if(m->fault == FAULT_CSD_VERSION)
                csd[0] = 0x80;
            block(m, csd, sizeof(csd), m->fault == FAULT_CSD_CRC);
            break;
        }
        case 17: {
            CHECK(m->initialized && m->crc_enabled);
            ++m->reads;
            m->last_read_address = argument;
            push(m, m->fault == FAULT_READ_COMMAND ? 4 : 0);
            if(m->fault == FAULT_READ_COMMAND || m->fault == FAULT_READ_TIMEOUT ||
               m->fault == FAULT_STOPPED_TIMER)
                break;
            if(m->fault == FAULT_READ_TOKEN) {
                push(m, 0x08);
                break;
            }
            uint8_t data[512];
            for(unsigned i = 0; i < sizeof(data); ++i)
                data[i] = m->payload ? m->payload[i] : pattern(argument, i);
            block(m, data, sizeof(data), m->fault == FAULT_READ_CRC);
            break;
        }
        default:
            CHECK(!"unexpected or destructive command");
    }
}

static void begin(void *ctx) {
    struct mock *m = ctx;
    ++m->begin;
}
static void end(void *ctx) {
    struct mock *m = ctx;
    ++m->end;
}
static void select_card(void *ctx, bool selected) {
    struct mock *m = ctx;
    m->selected = selected;
    if(!selected) {
        m->head = m->tail = m->frame_length = 0;
    }
}
static uint8_t transfer(void *ctx, uint8_t data, bool slow) {
    struct mock *m = ctx;
    if(m->fault != FAULT_STOPPED_TIMER)
        m->now += slow ? 400u : 20u;
    if(slow) ++m->slow_bytes;
    else ++m->fast_bytes;
    if(!m->selected)
        return 0xff;
    if(m->head < m->tail)
        return m->queue[m->head++];
    if(m->fault == FAULT_BUSY)
        return 0;
    if(m->frame_length || ((data & 0xc0) == 0x40)) {
        m->frame[m->frame_length++] = data;
        if(m->frame_length == 6) {
            m->frame_length = 0;
            decode(m);
        }
    }
    return 0xff;
}
static uint32_t ticks(void *ctx) {
    return ((struct mock *)ctx)->now;
}
static struct kui_loader_sd_bus bus(struct mock *m) {
    return (struct kui_loader_sd_bus){m, begin, end, select_card, transfer, ticks};
}

static void normal(bool version2, bool high_capacity) {
    struct mock m = {.version2 = version2, .high_capacity = high_capacity,
                     .now = UINT32_MAX - 1000u};
    struct kui_loader_sd_bus b = bus(&m);
    struct kui_loader_sd card;
    CHECK(kui_loader_sd_init_bus(&card, &b) == KUI_LOADER_SD_OK);
    CHECK(card.ready && card.high_capacity == high_capacity);
    CHECK(card.blocks == (high_capacity ? 8192 : 4096));
    CHECK(m.begin == 1 && m.end == 0 && !m.selected);
    CHECK(m.attempts == 3 && m.slow_bytes > 60 && m.fast_bytes > 16);
    uint8_t data[1536];
    CHECK(kui_loader_sd_read(&card, 37, 3, data) == KUI_LOADER_SD_OK);
    CHECK(m.reads == 3 && !m.selected);
    CHECK(m.last_read_address == (high_capacity ? 39u : 39u * 512u));
    for(unsigned sector = 0; sector < 3; ++sector)
        for(unsigned byte = 0; byte < 512; ++byte)
            CHECK(data[sector * 512u + byte] == pattern(
                high_capacity ? sector + 37u : (sector + 37u) * 512u, byte));
    unsigned packets = m.packets;
    CHECK(kui_loader_sd_read(&card, (uint32_t)card.blocks - 1u, 2, data) == KUI_LOADER_SD_RANGE);
    CHECK(kui_loader_sd_read(&card, UINT32_MAX, 2, data) == KUI_LOADER_SD_RANGE);
    CHECK(kui_loader_sd_read(&card, 0, 0, data) == KUI_LOADER_SD_ARGUMENT);
    CHECK(kui_loader_sd_read(&card, 0, 129, data) == KUI_LOADER_SD_ARGUMENT);
    CHECK(kui_loader_sd_read(&card, 0, 1, NULL) == KUI_LOADER_SD_ARGUMENT);
    CHECK(kui_loader_sd_read(NULL, 0, 1, data) == KUI_LOADER_SD_ARGUMENT);
    CHECK(m.packets == packets);
    CHECK(kui_loader_sd_read(&card, (uint32_t)card.blocks - 1u, 1, data) == KUI_LOADER_SD_OK);
    kui_loader_sd_shutdown(&card);
    CHECK(!card.ready && m.end == 1 && !m.selected);
    CHECK(kui_loader_sd_read(&card, 0, 1, data) == KUI_LOADER_SD_NOT_READY);
    kui_loader_sd_shutdown(&card);
    CHECK(m.end == 1);
}

static void init_failure(unsigned fault, enum kui_loader_sd_result expected, bool high) {
    struct mock m = {.version2 = true, .high_capacity = high, .fault = fault};
    struct kui_loader_sd_bus b = bus(&m);
    struct kui_loader_sd card;
    CHECK(kui_loader_sd_init_bus(&card, &b) == expected);
    CHECK(!card.ready && !m.selected && m.end == 1);
}

static void read_failure(unsigned fault, enum kui_loader_sd_result expected) {
    struct mock m = {.version2 = true, .high_capacity = true};
    struct kui_loader_sd_bus b = bus(&m);
    struct kui_loader_sd card;
    CHECK(kui_loader_sd_init_bus(&card, &b) == KUI_LOADER_SD_OK);
    m.fault = fault;
    uint8_t data[512];
    memset(data, 0xa5, sizeof(data));
    CHECK(kui_loader_sd_read(&card, 10, 1, data) == expected);
    CHECK(!m.selected);
    if(fault != FAULT_READ_CRC)
        for(unsigned i = 0; i < sizeof(data); ++i)
            CHECK(data[i] == 0xa5);
    kui_loader_sd_shutdown(&card);
}

static void data_crc_vectors(void) {
    struct mock m = {.version2 = true, .high_capacity = true};
    struct kui_loader_sd_bus b = bus(&m);
    struct kui_loader_sd card;
    CHECK(kui_loader_sd_init_bus(&card, &b) == KUI_LOADER_SD_OK);
    uint8_t expected[512], actual[512];
    static const uint16_t crc[] = {0x0000, 0x7fa1, 0x31c3};
    m.payload = expected;
    for(unsigned i = 0; i < 3; ++i) {
        memset(expected, i == 1 ? 0xff : 0, sizeof(expected));
        /* Leading zero bytes preserve the zero seed, so this final vector
         * retains the standard CRC-16/XMODEM check value for "123456789". */
        if(i == 2) memcpy(expected + 503, "123456789", 9);
        CHECK(reference_crc16(expected, sizeof(expected)) == crc[i]);
        m.fault = FAULT_NONE;
        CHECK(kui_loader_sd_read(&card, 10, 1, actual) == KUI_LOADER_SD_OK);
        CHECK(!memcmp(actual, expected, sizeof(actual)) && !m.selected);
        m.fault = FAULT_READ_CRC;
        CHECK(kui_loader_sd_read(&card, 10, 1, actual) == KUI_LOADER_SD_CRC);
        CHECK(!m.selected);
    }
    kui_loader_sd_shutdown(&card);
}

int main(void) {
    static const uint8_t text[] = "123456789";
    CHECK(reference_crc16(text, 9) == 0x31c3);
    normal(true, true);
    normal(true, false);
    normal(false, false);
    data_crc_vectors();
    init_failure(FAULT_NO_RESPONSE, KUI_LOADER_SD_TIMEOUT, true);
    init_failure(FAULT_RESET, KUI_LOADER_SD_COMMAND, true);
    init_failure(FAULT_ECHO, KUI_LOADER_SD_UNSUPPORTED, true);
    init_failure(FAULT_VOLTAGE, KUI_LOADER_SD_UNSUPPORTED, true);
    init_failure(FAULT_POWER, KUI_LOADER_SD_UNSUPPORTED, true);
    init_failure(FAULT_CMD55, KUI_LOADER_SD_UNSUPPORTED, true);
    init_failure(FAULT_CMD41, KUI_LOADER_SD_COMMAND, true);
    init_failure(FAULT_INIT_TIMEOUT, KUI_LOADER_SD_TIMEOUT, true);
    init_failure(FAULT_CMD16, KUI_LOADER_SD_COMMAND, false);
    init_failure(FAULT_CMD59, KUI_LOADER_SD_COMMAND, true);
    init_failure(FAULT_CSD_COMMAND, KUI_LOADER_SD_COMMAND, true);
    init_failure(FAULT_CSD_TOKEN, KUI_LOADER_SD_TOKEN, true);
    init_failure(FAULT_CSD_CRC, KUI_LOADER_SD_CRC, true);
    init_failure(FAULT_CSD_VERSION, KUI_LOADER_SD_CAPACITY, true);
    init_failure(FAULT_CSD_SIZE, KUI_LOADER_SD_CAPACITY, false);
    init_failure(FAULT_BUSY, KUI_LOADER_SD_TIMEOUT, true);
    read_failure(FAULT_READ_COMMAND, KUI_LOADER_SD_COMMAND);
    read_failure(FAULT_READ_TOKEN, KUI_LOADER_SD_TOKEN);
    read_failure(FAULT_READ_CRC, KUI_LOADER_SD_CRC);
    read_failure(FAULT_READ_TIMEOUT, KUI_LOADER_SD_TIMEOUT);
    read_failure(FAULT_BUSY, KUI_LOADER_SD_TIMEOUT);
    read_failure(FAULT_STOPPED_TIMER, KUI_LOADER_SD_TIMEOUT);
    struct kui_loader_sd card;
    struct mock m = {0};
    struct kui_loader_sd_bus b = bus(&m);
    CHECK(kui_loader_sd_init_bus(NULL, &b) == KUI_LOADER_SD_ARGUMENT);
    CHECK(kui_loader_sd_init_bus(&card, NULL) == KUI_LOADER_SD_ARGUMENT);
    b.transfer = NULL;
    CHECK(kui_loader_sd_init_bus(&card, &b) == KUI_LOADER_SD_ARGUMENT);
    CHECK(kui_loader_sd_init(&card) == KUI_LOADER_SD_UNSUPPORTED);
    CHECK(strcmp(kui_loader_sd_result_name(KUI_LOADER_SD_CRC), "SD CRC16 mismatch") == 0);
    CHECK(strcmp(kui_loader_sd_result_name((enum kui_loader_sd_result)-1), "unknown SD error") == 0);
    kui_loader_sd_shutdown(NULL);
    printf("loader SD protocol: %u assertions passed\n", checks);
    return 0;
}
