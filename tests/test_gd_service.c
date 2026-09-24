/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/gd_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define BEGIN 0x8c010000u
#define END 0x8ce00000u
#define PARAM (BEGIN + 0x100u)
#define STATUS (BEGIN + 0x200u)
#define OUTPUT (BEGIN + 0x1000u)
static uint8_t memory[0x40000];
static struct kui_gd_service service;
static struct context {
    unsigned reads, checks, maps, failures, reenter;
    uint32_t deny, last_lba, last_count, last_bytes;
} context;
static unsigned assertions;
#define CHECK(x) do { ++assertions; assert(x); } while(0)
static void put(uint32_t address, uint32_t n) {
    unsigned p = address - BEGIN;
    for(unsigned i = 0; i < 4; ++i) memory[p + i] = (uint8_t)(n >> (i * 8));
}
static uint32_t get(uint32_t address) {
    unsigned p = address - BEGIN;
    return (uint32_t)memory[p] | (uint32_t)memory[p + 1] << 8 |
        (uint32_t)memory[p + 2] << 16 | (uint32_t)memory[p + 3] << 24;
}
static const struct kui_gd_track tracks[] = {
    {1,4,0,8}, {2,0,16,24}, {3,4,45000,45064}, {4,4,45064,45072}
};
static uint8_t *map(void *opaque, uint32_t address, uint32_t bytes, int writing) {
    struct context *c = opaque;
    CHECK(c == &context); ++c->maps;
    CHECK(address >= BEGIN && address < END && bytes <= END - address);
    CHECK(writing == 0 || writing == 1);
    if(address == c->deny || address - BEGIN >= sizeof(memory) ||
       bytes > sizeof(memory) - (address - BEGIN)) return NULL;
    return memory + address - BEGIN;
}
static int check(void *opaque, uint32_t lba, uint32_t count, uint32_t bytes) {
    struct context *c = opaque; ++c->checks;
    CHECK(bytes == 2048 || bytes == 2352);
    CHECK(count && count <= KUI_GD_MAX_READ_SECTORS);
    for(uint32_t n = 0; n < count; ++n) {
        unsigned i;
        for(i = 0; i < sizeof(tracks) / sizeof(tracks[0]); ++i)
            if(lba + n >= tracks[i].start_lba && lba + n < tracks[i].end_lba) break;
        if(i == 4 || (bytes == 2048 && tracks[i].control == 0)) return -1;
    }
    return 0;
}
static uint8_t pattern(uint32_t lba, uint32_t byte) {
    return (uint8_t)((lba * 17u) ^ byte ^ (byte >> 8));
}
static int read_sectors(void *opaque, uint32_t lba, uint32_t count,
                        uint32_t bytes, void *output) {
    struct context *c = opaque; ++c->reads;
    c->last_lba = lba; c->last_count = count; c->last_bytes = bytes;
    if(c->reenter) {
        CHECK(kui_gd_service_dispatch(&service, KUI_GD_NOP, 0, 0, KUI_GD_REQUEST) == 0);
        CHECK(kui_gd_service_dispatch(&service, service.token, STATUS, 0, KUI_GD_CHECK) == 4);
        CHECK(kui_gd_service_dispatch(&service, service.token, 0, 0, KUI_GD_ABORT) == -1);
        CHECK(kui_gd_service_dispatch(&service, 0, 0, 0, KUI_GD_INIT) == -1);
    }
    uint8_t *out = output;
    if(c->failures) { out[0] = 0x23; return -1; }
    for(uint32_t n = 0; n < count; ++n)
        for(uint32_t i = 0; i < bytes; ++i) out[n * bytes + i] = pattern(lba + n, i);
    return 0;
}
static const struct kui_gd_ops ops = {&context, map, check, read_sectors};
static int32_t call(uint32_t fn, uint32_t a, uint32_t b) {
    return kui_gd_service_dispatch(&service, a, b, 0, fn);
}
static void reset(void) {
    memset(&context, 0, sizeof(context)); memset(memory, 0xa5, sizeof(memory));
    CHECK(kui_gd_service_init(&service, tracks, 4, &ops, BEGIN, END) == 0);
}
static void read_params(uint32_t lba, uint32_t count, uint32_t output) {
    put(PARAM, lba + 150); put(PARAM + 4, count);
    put(PARAM + 8, output); put(PARAM + 12, 0);
}
static void mode(uint32_t bytes) {
    put(PARAM, 0); put(PARAM + 4, bytes == 2048 ? 0x2000 : 0x1000);
    put(PARAM + 8, bytes == 2048 ? 1024 : 0); put(PARAM + 12, bytes);
    CHECK(call(KUI_GD_DATATYPE, PARAM, 0) == 0);
}
static void lifecycle(void) {
    reset(); CHECK(call(KUI_GD_INIT, 0, 0) == 0);
    CHECK(call(KUI_GD_DRIVE, STATUS, 0) == 0);
    CHECK(get(STATUS) == 1 && get(STATUS + 4) == 0x80);
    CHECK(call(KUI_GD_CHECK, 1, STATUS) == 0 && get(STATUS + 8) == 0);
    read_params(45003, 3, OUTPUT);
    int32_t token = call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM);
    CHECK(token > 0 && context.reads == 0 && context.checks == 1);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == 1);
    CHECK(get(STATUS) == 0 && get(STATUS + 4) == 0 && get(STATUS + 8) == 0 && get(STATUS + 12) == 4);
    CHECK(call(KUI_GD_DRIVE, STATUS, 0) == 0 && get(STATUS) == 0);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_NOP, 0) == 0);
    CHECK(call(KUI_GD_DATATYPE, PARAM, 0) == -1);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == 1 && context.reads == 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && context.reads == 1);
    CHECK(context.last_lba == 45003 && context.last_count == 3 && context.last_bytes == 2048);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == 2);
    CHECK(get(STATUS + 8) == 6144 && get(STATUS + 12) == 0);
    for(unsigned n = 0; n < 3; ++n)
        for(unsigned i = 0; i < 2048; ++i)
            CHECK(memory[OUTPUT - BEGIN + n * 2048 + i] == pattern(45003 + n, i));
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == 2 && context.reads == 1);
    CHECK(call(KUI_GD_ABORT, (uint32_t)token, 0) == -1);
    int32_t next = call(KUI_GD_REQUEST, KUI_GD_NOP, 0);
    CHECK(next > token && call(KUI_GD_CHECK, (uint32_t)token, STATUS) == 0);
    CHECK(call(KUI_GD_ABORT, (uint32_t)next + 1, 0) == -1);
    CHECK(call(KUI_GD_ABORT, (uint32_t)next, 0) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)next, STATUS) == -1);
    CHECK(get(STATUS) == 1 && get(STATUS + 4) == KUI_GD_ERROR_CANCELLED && get(STATUS + 8) == 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && context.reads == 1);
    CHECK(call(KUI_GD_ABORT, (uint32_t)next, 0) == -1);
    read_params(45000, 1, OUTPUT);
    token = call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM);
    CHECK(token > next); context.reenter = 1;
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == 2 && context.reads == 2);
    context.reenter = 0;
    CHECK(call(KUI_GD_RESET, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == 0);
    token = call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM);
    CHECK(token > 0); CHECK(call(KUI_GD_INIT, 0, 0) == 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && context.reads == 2);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == 0);
    service.token = 0x7fffffffu;
    CHECK(call(KUI_GD_REQUEST, KUI_GD_NOP, 0) == 1);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
}
static void toc_tests(void) {
    reset();
    for(unsigned area = 0; area < 2; ++area) {
        put(PARAM, area); put(PARAM + 4, OUTPUT);
        int32_t token = call(KUI_GD_REQUEST, KUI_GD_GETTOC2, PARAM);
        CHECK(token > 0); CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
        CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == 2 && get(STATUS + 8) == 408);
        for(unsigned n = 0; n < 99; ++n) {
            uint32_t want = 0xffffffffu;
            if(n < 4 && ((n >= 2) == (area != 0)))
                want = tracks[n].control << 28 | 0x01000000u | (tracks[n].start_lba + 150);
            CHECK(get(OUTPUT + n * 4) == want);
        }
        CHECK(get(OUTPUT + 396) == (area ? 0x41030000u : 0x41010000u));
        CHECK(get(OUTPUT + 400) == (area ? 0x41040000u : 0x01020000u));
        CHECK(get(OUTPUT + 404) == (area ? 0x41000000u | (45072 + 150) : 0x01000000u | (24 + 150)));
    }
    CHECK(context.reads == 0 && context.checks == 0);
    put(PARAM, 2); CHECK(call(KUI_GD_REQUEST, KUI_GD_GETTOC2, PARAM) == 0);
    CHECK(kui_gd_service_init(&service, tracks, 2, &ops, BEGIN, END) == 0);
    put(PARAM, 1); CHECK(call(KUI_GD_REQUEST, KUI_GD_GETTOC2, PARAM) == 0);
}
static void format_tests(void) {
    reset(); put(PARAM, 1);
    CHECK(call(KUI_GD_DATATYPE, PARAM, 0) == 0);
    CHECK(get(PARAM + 4) == 0x2000 && get(PARAM + 8) == 1024 && get(PARAM + 12) == 2048);
    read_params(16, 1, OUTPUT);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM) == 0); /* audio not user data */
    mode(2352); put(PARAM, 1); CHECK(call(KUI_GD_DATATYPE, PARAM, 0) == 0);
    CHECK(get(PARAM + 4) == 0x1000 && get(PARAM + 8) == 0 && get(PARAM + 12) == 2352);
    read_params(16, 1, OUTPUT);
    int32_t token = call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM);
    CHECK(token > 0); CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == 2 && get(STATUS + 8) == 2352);
    mode(2048); read_params(45063, 2, OUTPUT);
    token = call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM);
    CHECK(token > 0); CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(context.last_lba == 45063 && context.last_count == 2);
    for(unsigned i = 0; i < 10; ++i) {
        put(PARAM, 0); put(PARAM + 4, 0x2000); put(PARAM + 8, 1024); put(PARAM + 12, 2048);
        if(i < 4) put(PARAM + i * 4, 0xffffffffu);
        else if(i == 4) put(PARAM + 8, 2048); /* Mode 2 not implemented */
        else if(i == 5) put(PARAM + 12, 2340);
        else if(i == 6) put(PARAM + 12, 0);
        else if(i == 7) put(PARAM + 4, 0x1000);
        else if(i == 8) put(PARAM + 8, 0);
        else put(PARAM, 2);
        CHECK(call(KUI_GD_DATATYPE, PARAM, 0) == -1 && service.sector_bytes == 2048);
    }
    mode(2352); token = call(KUI_GD_REQUEST, KUI_GD_COMMAND_INIT, 0);
    CHECK(token > 0); CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && service.sector_bytes == 2048);
}
static void boundaries(void) {
    reset();
    const uint32_t bad[] = {0,0xffffffffu,0x8c000000u,BEGIN - 1,END,END - 4,
        0x8d000000u,0xad000000u,0x0d000000u,0xa05f0000u,0x9c010000u,0x4c010000u,PARAM + 1};
    for(unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        CHECK(call(KUI_GD_REQUEST, KUI_GD_PIOREAD, bad[i]) == 0);
        CHECK(call(KUI_GD_DATATYPE, bad[i], 0) == -1);
        CHECK(call(KUI_GD_DRIVE, bad[i], 0) == -1);
        CHECK(call(KUI_GD_CHECK, 1, bad[i]) == -1);
    }
    for(unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        read_params(45000, 1, bad[i]);
        CHECK(call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM) == 0);
    }
    read_params(45000, 1, OUTPUT + 2);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM) == 0);
    for(unsigned i = 0; i < 7; ++i) {
        read_params(45000, 1, OUTPUT);
        if(i == 0) put(PARAM, 149);
        if(i == 1) put(PARAM, 720000);
        if(i == 2) put(PARAM, 719999), put(PARAM + 4, 2);
        if(i == 3) put(PARAM + 4, 0);
        if(i == 4) put(PARAM + 4, 65);
        if(i == 5) put(PARAM + 4, 0xffffffffu);
        if(i == 6) put(PARAM + 12, 1);
        CHECK(call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM) == 0);
    }
    read_params(7, 2, OUTPUT); CHECK(call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM) == 0);
    read_params(45071, 2, OUTPUT); CHECK(call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM) == 0);
    CHECK(context.reads == 0);
    /* All accepted SH main-RAM aliases resolve to exactly the same host RAM. */
    const uint32_t aliases[] = {0,0x80000000u,0xa0000000u};
    for(unsigned i = 0; i < 3; ++i) {
        read_params(45000, 64, (OUTPUT & 0x1fffffffu) | aliases[i]);
        int32_t token = call(KUI_GD_REQUEST, KUI_GD_DMAREAD, (PARAM & 0x1fffffffu) | aliases[i]);
        CHECK(token > 0); CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
        CHECK(call(KUI_GD_CHECK, (uint32_t)token, (STATUS & 0x1fffffffu) | aliases[i]) == 2);
        CHECK(get(STATUS + 8) == 64 * 2048);
    }
    CHECK(context.reads == 3);
}
static void failures(void) {
    reset(); read_params(45000, 1, OUTPUT);
    int32_t token = call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM);
    context.failures = 1;
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == -1);
    CHECK(get(STATUS) == 1 && get(STATUS + 4) == KUI_GD_ERROR_IO && get(STATUS + 8) == 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && context.reads == 1);
    context.failures = 0;
    token = call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM);
    CHECK(token > 0); context.deny = OUTPUT;
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && context.reads == 1);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == -1);
    CHECK(get(STATUS + 4) == KUI_GD_ERROR_MEMORY && get(STATUS + 8) == 0);
    context.deny = 0; token = call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM);
    CHECK(token > 0); CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == 2 && context.reads == 2);
    for(uint32_t fn = 0; fn < 20; ++fn) {
        if(fn < 5 || fn == 8 || fn == 9 || fn == 10) continue;
        CHECK(call(fn, PARAM, STATUS) == -1);
    }
    for(uint32_t cmd = 0; cmd < 48; ++cmd) {
        if(cmd == 16 || cmd == 17 || cmd == 19 || cmd == 24 || cmd == 29 || cmd == 33) continue;
        CHECK(call(KUI_GD_REQUEST, cmd, PARAM) == 0);
    }
    CHECK(kui_gd_service_dispatch(&service, 0, 0, 0xffffffffu, 0) == -1);
    CHECK(kui_gd_service_dispatch(&service, 0, 0, 1, 3) == -1);
    CHECK(context.reads == 2);
}
static void init_rejections(void) {
    reset();
    CHECK(kui_gd_service_init(NULL, tracks, 4, &ops, BEGIN, END) == -1);
    CHECK(kui_gd_service_init(&service, NULL, 4, &ops, BEGIN, END) == -1);
    CHECK(kui_gd_service_init(&service, tracks, 0, &ops, BEGIN, END) == -1);
    CHECK(kui_gd_service_init(&service, tracks, 100, &ops, BEGIN, END) == -1);
    CHECK(kui_gd_service_init(&service, tracks, 4, NULL, BEGIN, END) == -1);
    CHECK(kui_gd_service_init(&service, tracks, 4, &ops, BEGIN - 4, END) == -1);
    CHECK(kui_gd_service_init(&service, tracks, 4, &ops, BEGIN, 0x8d000004u) == -1);
    CHECK(kui_gd_service_init(&service, tracks, 4, &ops, END, BEGIN) == -1);
    CHECK(kui_gd_service_init(&service, tracks, 4, &ops, BEGIN + 1, END) == -1);
    struct kui_gd_ops missing = ops; missing.map = NULL;
    CHECK(kui_gd_service_init(&service, tracks, 4, &missing, BEGIN, END) == -1);
    missing = ops; missing.read = NULL;
    CHECK(kui_gd_service_init(&service, tracks, 4, &missing, BEGIN, END) == -1);
    missing = ops; missing.check = NULL;
    CHECK(kui_gd_service_init(&service, tracks, 4, &missing, BEGIN, END) == -1);
    for(unsigned i = 0; i < 6; ++i) {
        struct kui_gd_track bad[4]; memcpy(bad, tracks, sizeof(bad));
        if(i == 0) bad[0].number = 0;
        if(i == 1) bad[1].control = 7;
        if(i == 2) bad[1].start_lba = 7;
        if(i == 3) bad[3].end_lba = 720000;
        if(i == 4) bad[0].end_lba = 45001;
        if(i == 5) bad[3].end_lba = bad[3].start_lba;
        CHECK(kui_gd_service_init(&service, bad, 4, &ops, BEGIN, END) == -1);
    }
    CHECK(kui_gd_service_dispatch(NULL, 0, 0, 0, 0) == -1);
    struct kui_gd_service empty = {0};
    CHECK(kui_gd_service_dispatch(&empty, 0, 0, 0, 3) == -1);
}
int main(void) {
    lifecycle(); toc_tests(); format_tests(); boundaries(); failures(); init_rejections();
    printf("GD BIOS service: %u assertions passed (portable ABI/lifecycle only)\n", assertions);
    return 0;
}
