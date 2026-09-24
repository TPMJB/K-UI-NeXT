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
    FAULT_READ_TIMEOUT, FAULT_READ_CRC, FAULT_BUSY, FAULT_STOPPED_TIMER,
    FAULT_MULTI_NO_RESPONSE, FAULT_STOP_RESPONSE, FAULT_STOP_COMMAND,
    FAULT_STOP_BUSY
};

struct mock {
    bool version2, high_capacity, selected, initialized, crc_enabled;
    unsigned begin, end, packets, reads, attempts, slow_bytes, fast_bytes;
    unsigned frame_length, head, tail, fault;
    uint8_t frame[6], queue[520];
    uint32_t now, last_read_address;
    const uint8_t *payload;
    bool streaming, stopping, stop_busy, frozen_timer;
    unsigned multi_commands, stream_blocks, stops, active_deselects, undrained_stops;
    unsigned stop_fault, read_fault_block, tick_step, token_delay, token_delay_reload;
    uint32_t next_address;
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

static void stream_block(struct mock *m) {
    m->head = m->tail = 0;
    bool fault_here = m->stream_blocks == m->read_fault_block;
    if(fault_here && (m->fault == FAULT_READ_TIMEOUT || m->fault == FAULT_STOPPED_TIMER))
        return;
    if(m->token_delay) {
        --m->token_delay;
        return;
    }
    ++m->stream_blocks;
    if(fault_here && m->fault == FAULT_READ_TOKEN) {
        push(m, 0x08);
        return;
    }
    uint8_t data[512];
    for(unsigned i = 0; i < sizeof(data); ++i)
        data[i] = m->payload ? m->payload[i] : pattern(m->next_address, i);
    block(m, data, sizeof(data), fault_here && m->fault == FAULT_READ_CRC);
    m->next_address += m->high_capacity ? 1u : 512u;
    m->token_delay = m->token_delay_reload;
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
        case 18:
            CHECK(m->initialized && m->crc_enabled && !m->streaming);
            ++m->multi_commands;
            m->stream_blocks = 0;
            m->next_address = m->last_read_address = argument;
            m->streaming = true;
            m->stopping = m->stop_busy = false;
            if(m->fault != FAULT_MULTI_NO_RESPONSE)
                push(m, m->fault == FAULT_READ_COMMAND ? 4 : 0);
            break;
        case 12:
            CHECK(argument == 0 && m->streaming);
            ++m->stops;
            m->stopping = true;
            push(m, 0x04); /* Arbitrary stuff byte is not an R1 response. */
            if(m->stop_fault == FAULT_STOP_RESPONSE)
                break;
            push(m, m->stop_fault == FAULT_STOP_COMMAND ? 4 : 0);
            push(m, 0); push(m, 0); /* R1b busy must be drained. */
            m->stop_busy = m->stop_fault == FAULT_STOP_BUSY;
            if(!m->stop_busy) push(m, 0xff);
            if(m->stop_fault != FAULT_STOP_COMMAND) m->streaming = false;
            break;
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
        if(m->streaming) ++m->active_deselects;
        if(m->stopping && m->head < m->tail) ++m->undrained_stops;
        m->head = m->tail = m->frame_length = 0;
    }
}
static uint8_t transfer(void *ctx, uint8_t data, bool slow) {
    struct mock *m = ctx;
    if(m->fault != FAULT_STOPPED_TIMER && !m->frozen_timer)
        m->now += m->tick_step ? m->tick_step : (slow ? 400u : 20u);
    if(slow) ++m->slow_bytes;
    else ++m->fast_bytes;
    if(!m->selected)
        return 0xff;
    if(m->fault == FAULT_BUSY)
        return 0;
    /* A CMD12 packet can arrive while MISO is still streaming. Parse MOSI
     * independently, and do not manufacture the next block during a command. */
    if(m->head == m->tail && m->streaming && !m->stopping &&
       m->fault != FAULT_MULTI_NO_RESPONSE && !m->frame_length && data == 0xff)
        stream_block(m);
    uint8_t received = m->head < m->tail ? m->queue[m->head++] :
                       (m->stop_busy ? 0 : 0xff);
    if(m->frame_length || ((data & 0xc0) == 0x40)) {
        m->frame[m->frame_length++] = data;
        if(m->frame_length == 6) {
            m->frame_length = 0;
            decode(m);
        }
    }
    return received;
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

static void multi_normal(bool version2, bool high_capacity) {
    struct mock m = {.version2 = version2, .high_capacity = high_capacity,
                     .now = UINT32_MAX - 1000u};
    struct kui_loader_sd_bus b = bus(&m);
    struct kui_loader_sd card;
    CHECK(kui_loader_sd_init_bus(&card, &b) == KUI_LOADER_SD_OK);
    uint8_t single[8 * 512], multiple[8 * 512];
    const unsigned counts[] = {2, 8};
    for(unsigned i = 0; i < 2; ++i) {
        unsigned count = counts[i], commands = m.packets, stops = m.stops;
        CHECK(kui_loader_sd_read(&card, 37, count, single) == KUI_LOADER_SD_OK);
        CHECK(m.packets == commands + count);
        CHECK(kui_loader_sd_read_multi(&card, 37, count, multiple) == KUI_LOADER_SD_OK);
        CHECK(m.packets == commands + count + 2 && m.stops == stops + 1);
        CHECK(m.stream_blocks == count && m.multi_commands == i + 1);
        CHECK(m.last_read_address == (high_capacity ? 37u : 37u * 512u));
        CHECK(!memcmp(single, multiple, count * 512u));
        CHECK(card.ready && !m.selected && !m.streaming && !m.stop_busy);
        CHECK(!m.active_deselects && !m.undrained_stops);
    }
    unsigned packets = m.packets;
    CHECK(kui_loader_sd_read_multi(&card, (uint32_t)card.blocks - 1u, 2, multiple) == KUI_LOADER_SD_RANGE);
    CHECK(kui_loader_sd_read_multi(&card, UINT32_MAX, 2, multiple) == KUI_LOADER_SD_RANGE);
    CHECK(kui_loader_sd_read_multi(&card, 0, 0, multiple) == KUI_LOADER_SD_ARGUMENT);
    CHECK(kui_loader_sd_read_multi(&card, 0, 129, multiple) == KUI_LOADER_SD_ARGUMENT);
    CHECK(kui_loader_sd_read_multi(&card, 0, 1, NULL) == KUI_LOADER_SD_ARGUMENT);
    CHECK(kui_loader_sd_read_multi(NULL, 0, 1, multiple) == KUI_LOADER_SD_ARGUMENT);
    CHECK(m.packets == packets);
    CHECK(kui_loader_sd_read_multi(&card, (uint32_t)card.blocks - 1u, 1, multiple) == KUI_LOADER_SD_OK);
    kui_loader_sd_shutdown(&card);
    CHECK(kui_loader_sd_read_multi(&card, 0, 1, multiple) == KUI_LOADER_SD_NOT_READY);
    CHECK(!m.selected && m.end == 1);
}

static void multi_failure(unsigned fault, unsigned stop_fault,
                           enum kui_loader_sd_result expected, bool ready) {
    struct mock m = {.version2 = true, .high_capacity = true};
    struct kui_loader_sd_bus b = bus(&m);
    struct kui_loader_sd card;
    CHECK(kui_loader_sd_init_bus(&card, &b) == KUI_LOADER_SD_OK);
    m.fault = fault;
    m.stop_fault = stop_fault;
    m.read_fault_block = 1; /* Exercise cleanup after a complete first block. */
    uint8_t data[3 * 512];
    memset(data, 0xa5, sizeof(data));
    CHECK(kui_loader_sd_read_multi(&card, 10, 3, data) == expected);
    CHECK(card.ready == ready && !m.selected);
    CHECK(m.stops == (fault == FAULT_BUSY ? 0u : 1u));
    CHECK(!m.undrained_stops);
    if(fault != FAULT_NONE) {
        CHECK(card.last_command == 18); /* Cleanup must not hide read failure. */
        for(unsigned i = 2 * 512; i < sizeof(data); ++i)
            CHECK(data[i] == 0xa5);
    } else {
        CHECK(card.last_command == 12);
    }
    if(fault == FAULT_READ_CRC || fault == FAULT_READ_TOKEN ||
       fault == FAULT_READ_TIMEOUT || fault == FAULT_STOPPED_TIMER) {
        for(unsigned i = 0; i < 512; ++i) CHECK(data[i] == pattern(10, i));
    }
    if(ready) {
        CHECK(!m.streaming && !m.active_deselects && !m.stop_busy);
        m.fault = FAULT_NONE;
        CHECK(kui_loader_sd_read_multi(&card, 20, 2, data) == KUI_LOADER_SD_OK);
        CHECK(kui_loader_sd_read(&card, 20, 2, data) == KUI_LOADER_SD_OK);
        kui_loader_sd_shutdown(&card);
    } else {
        unsigned packets = m.packets;
        CHECK(kui_loader_sd_read_multi(&card, 0, 1, data) == KUI_LOADER_SD_NOT_READY);
        CHECK(kui_loader_sd_read(&card, 0, 1, data) == KUI_LOADER_SD_NOT_READY);
        CHECK(m.packets == packets);
    }
}

static void multi_limits(void) {
    struct mock m = {.version2 = true, .high_capacity = true};
    struct kui_loader_sd_bus b = bus(&m);
    struct kui_loader_sd card;
    CHECK(kui_loader_sd_init_bus(&card, &b) == KUI_LOADER_SD_OK);
    static uint8_t data[128 * 512];
    CHECK(kui_loader_sd_read_multi(&card, 8192 - 128, 128, data) == KUI_LOADER_SD_OK);
    CHECK(m.stream_blocks == 128 && card.ready);
    for(unsigned i = 0; i < sizeof(data); ++i)
        CHECK(data[i] == pattern(8192 - 128 + i / 512, i % 512));
    card.blocks = UINT64_C(1) << 32;
    CHECK(kui_loader_sd_read_multi(&card, UINT32_MAX, 1, data) == KUI_LOADER_SD_OK);
    CHECK(m.last_read_address == UINT32_MAX);
    card.high_capacity = m.high_capacity = false;
    CHECK(kui_loader_sd_read_multi(&card, (1u << 23) - 1, 1, data) == KUI_LOADER_SD_OK);
    CHECK(m.last_read_address == 0xfffffe00u);
    unsigned packets = m.packets;
    CHECK(kui_loader_sd_read_multi(&card, (1u << 23) - 1, 2, data) == KUI_LOADER_SD_RANGE);
    CHECK(m.packets == packets);
    kui_loader_sd_shutdown(&card);
}

static void multi_total_budget(bool frozen) {
    struct mock m = {.version2 = true, .high_capacity = true};
    struct kui_loader_sd_bus b = bus(&m);
    struct kui_loader_sd card;
    CHECK(kui_loader_sd_init_bus(&card, &b) == KUI_LOADER_SD_OK);
    if(frozen) {
        m.frozen_timer = true;
        m.token_delay = m.token_delay_reload = 500000;
    } else {
        m.tick_step = 5000; /* Individual blocks fit; the whole read does not. */
    }
    uint8_t data[16 * 512];
    unsigned bytes = m.fast_bytes;
    CHECK(kui_loader_sd_read_multi(&card, 10, 16, data) == KUI_LOADER_SD_TIMEOUT);
    CHECK(card.ready && !m.selected && !m.streaming && m.stops == 1);
    CHECK(!m.active_deselects && !m.undrained_stops && card.last_command == 18);
    if(frozen) {
        CHECK(m.stream_blocks == 1);
        CHECK(m.fast_bytes - bytes >= 1000000 && m.fast_bytes - bytes < 1000040);
    } else {
        CHECK(m.stream_blocks > 1 && m.stream_blocks < 16);
    }
    kui_loader_sd_shutdown(&card);
}

static void stream_lifecycle(bool high_capacity) {
    struct mock m = {.version2 = true, .high_capacity = high_capacity};
    struct kui_loader_sd_bus b = bus(&m);
    struct kui_loader_sd card;
    struct kui_loader_sd_stream stream = {0};
    uint8_t data[512];
    CHECK(kui_loader_sd_init_bus(&card, &b) == KUI_LOADER_SD_OK);
    CHECK(kui_loader_sd_stream_start(&card, &stream, 37, 10) == KUI_LOADER_SD_OK);
    CHECK(stream.active && stream.remaining == 10 && stream.next_lba == 37);
    CHECK(m.selected && m.streaming && !m.stream_blocks && !m.stops);
    unsigned packets = m.packets, bytes = m.fast_bytes;
    CHECK(kui_loader_sd_stream_start(&card, &stream, 50, 8) == KUI_LOADER_SD_ARGUMENT);
    CHECK(m.packets == packets && m.fast_bytes == bytes && stream.remaining == 10);
    for(unsigned i = 0; i < 10; ++i) {
        CHECK(kui_loader_sd_stream_next(&card, &stream, data) == KUI_LOADER_SD_OK);
        CHECK(stream.next_lba == 38 + i && stream.remaining == 9 - i);
        CHECK(stream.active == (i != 9) && m.selected == stream.active);
        CHECK(m.stops == (i == 9 ? 1u : 0u));
        for(unsigned j = 0; j < sizeof(data); ++j)
            CHECK(data[j] == pattern(high_capacity ? 37 + i : (37 + i) * 512, j));
    }
    packets = m.packets; bytes = m.fast_bytes;
    CHECK(kui_loader_sd_stream_next(&card, &stream, data) == KUI_LOADER_SD_ARGUMENT);
    CHECK(kui_loader_sd_stream_stop(&card, &stream) == KUI_LOADER_SD_OK);
    CHECK(m.packets == packets && m.fast_bytes == bytes);
    /* Early stop before any data and after two blocks, then a different LBA. */
    for(unsigned consumed = 0; consumed <= 2; consumed += 2) {
        CHECK(kui_loader_sd_stream_start(&card, &stream, 80, 8) == KUI_LOADER_SD_OK);
        for(unsigned i = 0; i < consumed; ++i)
            CHECK(kui_loader_sd_stream_next(&card, &stream, data) == KUI_LOADER_SD_OK);
        CHECK(stream.remaining == 8 - consumed && stream.next_lba == 80 + consumed);
        CHECK(kui_loader_sd_stream_stop(&card, &stream) == KUI_LOADER_SD_OK);
        CHECK(!stream.active && !stream.remaining && stream.next_lba == 80 + consumed);
        CHECK(card.ready && !m.selected && !m.streaming && !m.stop_busy);
    }
    CHECK(kui_loader_sd_stream_start(&card, &stream, 100, 1) == KUI_LOADER_SD_OK);
    CHECK(kui_loader_sd_stream_next(&card, &stream, data) == KUI_LOADER_SD_OK);
    CHECK(!stream.active && stream.next_lba == 101 && !stream.remaining);
    CHECK(!m.active_deselects && !m.undrained_stops);
    kui_loader_sd_shutdown(&card);
}

static void stream_arguments(void) {
    struct mock m = {.version2 = true, .high_capacity = true};
    struct kui_loader_sd_bus b = bus(&m);
    struct kui_loader_sd card;
    struct kui_loader_sd_stream stream = {0};
    uint8_t data[512];
    CHECK(kui_loader_sd_init_bus(&card, &b) == KUI_LOADER_SD_OK);
    unsigned packets = m.packets, bytes = m.fast_bytes;
    CHECK(kui_loader_sd_stream_start(NULL, &stream, 0, 1) == KUI_LOADER_SD_ARGUMENT);
    CHECK(kui_loader_sd_stream_start(&card, NULL, 0, 1) == KUI_LOADER_SD_ARGUMENT);
    CHECK(kui_loader_sd_stream_start(&card, &stream, 0, 0) == KUI_LOADER_SD_ARGUMENT);
    CHECK(kui_loader_sd_stream_start(&card, &stream, 0, 129) == KUI_LOADER_SD_ARGUMENT);
    CHECK(kui_loader_sd_stream_start(&card, &stream, 8191, 2) == KUI_LOADER_SD_RANGE);
    CHECK(kui_loader_sd_stream_start(&card, &stream, UINT32_MAX, 2) == KUI_LOADER_SD_RANGE);
    CHECK(kui_loader_sd_stream_next(&card, &stream, data) == KUI_LOADER_SD_ARGUMENT);
    CHECK(kui_loader_sd_stream_next(NULL, &stream, data) == KUI_LOADER_SD_ARGUMENT);
    CHECK(kui_loader_sd_stream_next(&card, NULL, data) == KUI_LOADER_SD_ARGUMENT);
    CHECK(kui_loader_sd_stream_stop(&card, &stream) == KUI_LOADER_SD_OK);
    CHECK(kui_loader_sd_stream_stop(NULL, &stream) == KUI_LOADER_SD_ARGUMENT);
    CHECK(kui_loader_sd_stream_stop(&card, NULL) == KUI_LOADER_SD_ARGUMENT);
    card.ready = false;
    CHECK(kui_loader_sd_stream_start(&card, &stream, 0, 1) == KUI_LOADER_SD_NOT_READY);
    card.ready = true;
    CHECK(!stream.active && !m.selected && m.packets == packets && m.fast_bytes == bytes);
    CHECK(kui_loader_sd_stream_start(&card, &stream, 10, 8) == KUI_LOADER_SD_OK);
    CHECK(kui_loader_sd_stream_next(&card, &stream, NULL) == KUI_LOADER_SD_ARGUMENT);
    CHECK(!stream.active && !stream.remaining && !m.selected && m.stops == 1);
    CHECK(card.ready && card.last_command == 18 && !m.active_deselects);
    kui_loader_sd_shutdown(&card);
}

static void stream_errors(void) {
    struct mock m = {.version2 = true, .high_capacity = true};
    struct kui_loader_sd_bus b = bus(&m);
    struct kui_loader_sd card;
    struct kui_loader_sd_stream stream = {0};
    uint8_t data[512];
    CHECK(kui_loader_sd_init_bus(&card, &b) == KUI_LOADER_SD_OK);
    m.fault = FAULT_READ_CRC; m.read_fault_block = 1;
    CHECK(kui_loader_sd_stream_start(&card, &stream, 20, 8) == KUI_LOADER_SD_OK);
    CHECK(kui_loader_sd_stream_next(&card, &stream, data) == KUI_LOADER_SD_OK);
    CHECK(kui_loader_sd_stream_next(&card, &stream, data) == KUI_LOADER_SD_CRC);
    CHECK(!stream.active && !stream.remaining && stream.next_lba == 21);
    CHECK(card.ready && card.last_command == 18 && !m.selected && m.stops == 1);
    m.fault = FAULT_MULTI_NO_RESPONSE;
    CHECK(kui_loader_sd_stream_start(&card, &stream, 30, 8) == KUI_LOADER_SD_TIMEOUT);
    CHECK(!stream.active && !stream.remaining && !m.selected && m.stops == 2);
    CHECK(card.ready && card.last_command == 18 && card.last_response == 0xff);
    m.fault = FAULT_NONE;
    CHECK(kui_loader_sd_stream_start(&card, &stream, 40, 8) == KUI_LOADER_SD_OK);
    CHECK(kui_loader_sd_stream_next(&card, &stream, data) == KUI_LOADER_SD_OK);
    m.now += 25000000u; /* Whole-stream timeout also spans separate next calls. */
    CHECK(kui_loader_sd_stream_next(&card, &stream, data) == KUI_LOADER_SD_TIMEOUT);
    CHECK(!stream.active && !stream.remaining && stream.next_lba == 41);
    CHECK(card.ready && !m.selected && m.stops == 3 && m.stream_blocks == 1);
    CHECK(!m.active_deselects && !m.undrained_stops);
    m.stop_fault = FAULT_STOP_COMMAND;
    CHECK(kui_loader_sd_stream_start(&card, &stream, 50, 8) == KUI_LOADER_SD_OK);
    CHECK(kui_loader_sd_stream_next(&card, &stream, data) == KUI_LOADER_SD_OK);
    CHECK(kui_loader_sd_stream_stop(&card, &stream) == KUI_LOADER_SD_COMMAND);
    CHECK(!stream.active && !stream.remaining && !card.ready && !m.selected);
    CHECK(card.last_command == 12 && m.stops == 4);
    unsigned packets = m.packets;
    CHECK(kui_loader_sd_stream_stop(&card, &stream) == KUI_LOADER_SD_OK);
    CHECK(kui_loader_sd_stream_start(&card, &stream, 0, 1) == KUI_LOADER_SD_NOT_READY);
    CHECK(m.packets == packets);
}

int main(void) {
    static const uint8_t text[] = "123456789";
    CHECK(reference_crc16(text, 9) == 0x31c3);
    normal(true, true);
    normal(true, false);
    normal(false, false);
    data_crc_vectors();
    multi_normal(true, true);
    multi_normal(true, false);
    multi_normal(false, false);
    multi_limits();
    multi_total_budget(false);
    multi_total_budget(true);
    stream_lifecycle(true);
    stream_lifecycle(false);
    stream_arguments();
    stream_errors();
    multi_failure(FAULT_READ_COMMAND, FAULT_NONE, KUI_LOADER_SD_COMMAND, true);
    multi_failure(FAULT_MULTI_NO_RESPONSE, FAULT_NONE, KUI_LOADER_SD_TIMEOUT, true);
    multi_failure(FAULT_READ_TOKEN, FAULT_NONE, KUI_LOADER_SD_TOKEN, true);
    multi_failure(FAULT_READ_CRC, FAULT_NONE, KUI_LOADER_SD_CRC, true);
    multi_failure(FAULT_READ_TIMEOUT, FAULT_NONE, KUI_LOADER_SD_TIMEOUT, true);
    multi_failure(FAULT_STOPPED_TIMER, FAULT_NONE, KUI_LOADER_SD_TIMEOUT, true);
    multi_failure(FAULT_BUSY, FAULT_NONE, KUI_LOADER_SD_TIMEOUT, false);
    multi_failure(FAULT_NONE, FAULT_STOP_RESPONSE, KUI_LOADER_SD_TIMEOUT, false);
    multi_failure(FAULT_NONE, FAULT_STOP_COMMAND, KUI_LOADER_SD_COMMAND, false);
    multi_failure(FAULT_NONE, FAULT_STOP_BUSY, KUI_LOADER_SD_TIMEOUT, false);
    multi_failure(FAULT_READ_CRC, FAULT_STOP_COMMAND, KUI_LOADER_SD_CRC, false);
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
