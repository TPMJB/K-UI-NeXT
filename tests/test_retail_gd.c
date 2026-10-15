/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_gd.h"
#include "kui/retail_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define BEGIN 0x8c010000u
#define END 0x8d000000u
#define PARAM (BEGIN + 0x100u)
#define STATUS (BEGIN + 0x200u)
#define OUTPUT (BEGIN + 0x1000u)
static uint8_t ram[END - BEGIN];
#ifdef KUI_RETAIL_CE
#define VIRT 0x0c3b0000u /* A CE process slot's addresses, not RAM. */
static uint8_t virt[0x10000];
#endif
static struct kui_retail_gd service;
static struct {
    uint32_t reads, sectors, max_count, deny, fail_at, checks, reenter;
    uint32_t maps[3], validate_address, validate_bytes, max_checked, last_count;
} ctx;
static const union kui_retail_slot tracks[] = {
    {.track={.start_lba=0, .end_lba=8, .control=4}},
    {.track={.start_lba=16, .end_lba=24, .control=0}},
    {.track={.start_lba=45000, .end_lba=60000, .control=4}}
};
static const union kui_retail_slot full_tracks[] = {
    {.track={.start_lba=0, .end_lba=8, .control=4}},
    {.track={.start_lba=16, .end_lba=24, .control=0}},
    {.track={.start_lba=45000, .end_lba=719850, .control=4}}
};
/* The same tracks with their map's extent fields set: the service ignores them. */
static const union kui_retail_slot mapped_tracks[] = {
    {.track={.start_lba=0, .end_lba=8, .first_extent=3, .extent_count=11, .control=4}},
    {.track={.start_lba=16, .end_lba=24, .first_extent=14, .extent_count=0, .control=0}},
    {.track={.start_lba=45000, .end_lba=60000, .first_extent=14, .extent_count=13, .control=4}}
};
/* A RAM pointer through its 0x0c... alias. In the CE build such CPU
 * pointers are virtual (virtual_pointers), so the P1 address stands in. */
#ifdef KUI_RETAIL_CE
#define LOW_ALIAS(a) (a)
#else
#define LOW_ALIAS(a) ((a) & 0x1fffffffu)
#endif
static unsigned use_mapped_tracks;
static unsigned use_cooked_tracks;
static unsigned use_full_tracks;
static union kui_retail_slot cooked_tracks[3];
static unsigned assertions;
#define CHECK(x) do { ++assertions; assert(x); } while(0)
#if KUI_RETAIL_GD_REJECTION_DETAILS
#define CHECK_DIAG(x) CHECK(x)
#else
#define CHECK_DIAG(x) ((void)0)
#endif
static void put(uint32_t a, uint32_t n) {
    for(unsigned i = 0; i < 4; ++i) ram[a - BEGIN + i] = (uint8_t)(n >> (i * 8));
}
static uint32_t get(uint32_t a) {
    uint32_t n = 0;
    for(unsigned i = 0; i < 4; ++i) n |= (uint32_t)ram[a - BEGIN + i] << (i * 8);
    return n;
}
static uint8_t *map(void *unused, uint32_t a, uint32_t bytes, int writing) {
    (void)unused;
#ifdef KUI_RETAIL_CE
    /* The CE build passes other areas' addresses on as CE's virtual ones:
     * here one 64 KiB window of a process slot, separate from RAM, stands
     * for them, and the adapter refuses the rest. */
    if((a & 0xff000000u) != 0x8c000000u) {
        if(a < VIRT || bytes > sizeof(virt) || a - VIRT > sizeof(virt) - bytes) return NULL;
        ++ctx.maps[writing];
        return writing == KUI_RETAIL_MAP_VALIDATE ? virt + sizeof(virt) : virt + (a - VIRT);
    }
#endif
    CHECK(a >= BEGIN && a < END && bytes <= END - a);
    CHECK(writing >= 0 && writing <= KUI_RETAIL_MAP_VALIDATE);
    ++ctx.maps[writing];
    if(a == ctx.deny) return NULL;
    if(writing == KUI_RETAIL_MAP_VALIDATE) {
        ctx.validate_address = a; ctx.validate_bytes = bytes;
        /* Non-null validation success deliberately supplies no accessible
         * output memory: the service must obtain a write mapping at EXEC. */
        return ram + sizeof(ram);
    }
    return ram + a - BEGIN;
}
static int check(void *unused, uint32_t lba, uint32_t count, uint32_t bytes) {
    (void)unused; ++ctx.checks;
    if(count > ctx.max_checked) ctx.max_checked = count;
    CHECK(count > 0 && count <= KUI_RETAIL_GD_CHECK_SECTORS);
    const union kui_retail_slot *ranges = use_full_tracks ? full_tracks : tracks;
    for(uint32_t n = 0; n < count; ++n) {
        unsigned i;
        for(i = 0; i < 3; ++i)
            if(lba + n >= ranges[i].track.start_lba && lba + n < ranges[i].track.end_lba) break;
        if(i == 3 || (bytes == 2048 && !ranges[i].track.control)) return -1;
    }
    return 0;
}
static uint8_t pattern(uint32_t lba, uint32_t i) { return (uint8_t)(lba * 13u + i * 3u); }
static int read_sectors(void *unused, uint32_t lba, uint32_t count,
                        uint32_t bytes, void *output) {
    (void)unused; ++ctx.reads;
    if(count > ctx.max_count) ctx.max_count = count;
    ctx.last_count = count;
    uint32_t step = service.step - 1u < KUI_RETAIL_GD_STEP_MAX ?
        service.step : KUI_RETAIL_GD_STEP_SECTORS;
    CHECK(count > 0 && count <= step);
    if(ctx.reenter) {
        CHECK(kui_retail_gd_dispatch(&service, KUI_GD_NOP, 0, 0, KUI_GD_REQUEST) == 0);
        CHECK(kui_retail_gd_dispatch(&service, service.token, STATUS, 0, KUI_GD_CHECK) == 4);
        CHECK(kui_retail_gd_dispatch(&service, 0, 0, 0, KUI_GD_INIT) == -1);
    }
    if(ctx.fail_at && ctx.reads == ctx.fail_at) return -1;
    uint8_t *p = output;
    for(uint32_t n = 0; n < count; ++n)
        for(uint32_t i = 0; i < bytes; ++i) *p++ = pattern(lba + n, i);
    ctx.sectors += count;
    return 0;
}
static const struct kui_gd_ops ops = {NULL, map, check, read_sectors};
static int32_t call(uint32_t fn, uint32_t a, uint32_t b) {
    return kui_retail_gd_dispatch(&service, a, b, 0, fn);
}
static void reset(void) {
    memset(&ctx, 0, sizeof(ctx)); memset(ram, 0xa5, sizeof(ram));
    const union kui_retail_slot *slots = use_full_tracks ? full_tracks :
        use_mapped_tracks ? mapped_tracks : tracks;
    if(use_cooked_tracks) {
        memcpy(cooked_tracks, slots, sizeof(cooked_tracks));
        cooked_tracks[0].track.control |= KUI_RETAIL_TRACK_COOKED;
        cooked_tracks[2].track.control |= KUI_RETAIL_TRACK_COOKED;
        slots = cooked_tracks;
    }
    CHECK(kui_retail_gd_init(&service, slots, 3, &ops, BEGIN, END) == 0);
    CHECK(service.tracks == slots && sizeof(service) < 512);
    struct kui_retail_gd expected = service;
    memset(&service, 0xa5, sizeof(service));
    kui_retail_gd_init_validated(&service, slots, 3, &ops, BEGIN, END);
    CHECK(!memcmp(&service, &expected, sizeof(service)));
}
static void read_params(uint32_t lba, uint32_t count, uint32_t dest) {
    put(PARAM, lba + 150); put(PARAM + 4, count); put(PARAM + 8, dest); put(PARAM + 12, 0);
}
static void mode(uint32_t bytes, uint32_t type) {
    put(PARAM, 0); put(PARAM + 4, bytes == 2048 ? 0x2000 : 0x1000);
    put(PARAM + 8, type); put(PARAM + 12, bytes);
    CHECK(call(KUI_GD_DATATYPE, PARAM, 0) == 0);
}
/* The photographed Sonic request is legal through every native RAM alias.
 * CE's DMA destination is physical too, unlike its CPU parameter pointers.
 * Validate the entire request without I/O; reject flags and protected bounds
 * without changing the command/status/error that a caller would observe. */
static void request_rejection_details(void) {
    const uint32_t lba = 0x82f22u, count = 0x69u, output = 0x8cd00000u;
    const uint32_t aliases[] = {0, 0x80000000u, 0xa0000000u};
    use_full_tracks = 1;
    for(unsigned alias = 0; alias < 3; ++alias) {
        reset();
        uint32_t destination = (output & 0x1fffffffu) | aliases[alias];
        read_params(lba, count, destination);
        uint32_t mapped_read = ctx.maps[0], mapped_write = ctx.maps[1];
        int32_t token = call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM);
        CHECK(token > 0 && service.pending && service.command == KUI_GD_DMAREAD);
        CHECK_DIAG(service.diag.reject_reason == KUI_RETAIL_GD_REJECT_NONE && !service.diag.read_flags);
        CHECK(service.diag.last_lba == lba && service.diag.last_count == count &&
              service.diag.last_destination == destination);
        CHECK(ctx.validate_address == output && ctx.validate_bytes == count * 2048u);
        CHECK(ctx.maps[0] == mapped_read + 1 && ctx.maps[1] == mapped_write &&
              ctx.maps[KUI_RETAIL_MAP_VALIDATE] == 1);
        CHECK(ctx.checks == 14 && ctx.max_checked == 8 && !ctx.reads && !ctx.sectors);
        CHECK(ram[output - BEGIN] == 0xa5 && ram[output - BEGIN + count * 2048u - 1] == 0xa5);
        CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_PROCESSING && !ctx.reads);
#ifdef KUI_RETAIL_GD_ASYNC
        CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && !ctx.reads);
        kui_retail_gd_progress(&service, count, 0);
#else
        while(service.pending) CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
        CHECK(ctx.sectors == count && ctx.max_count == KUI_RETAIL_GD_STEP_SECTORS);
        for(uint32_t n = 0; n < count; ++n)
            for(uint32_t i = 0; i < 2048u; ++i)
                CHECK(ram[output - BEGIN + n * 2048u + i] == pattern(lba + n, i));
#endif
        CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_COMPLETED);
        CHECK(get(STATUS) == 0 && get(STATUS + 4) == 0 && get(STATUS + 8) == count * 2048u);
    }
    reset();
    read_params(lba, count, output & 0x1fffffffu); put(PARAM + 12, 0x53u);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM) == 0);
    CHECK_DIAG(service.diag.reject_reason == KUI_RETAIL_GD_REJECT_READ_FLAGS && service.diag.read_flags == 0x53u);
    CHECK(!ctx.reads && !ctx.checks && !ctx.maps[KUI_RETAIL_MAP_VALIDATE]);
    CHECK(!service.command && !service.pending && !service.error && service.status == KUI_GD_NOT_FOUND);
    read_params(lba, count, 0x0c00ba5cu);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM) == 0);
    CHECK_DIAG(service.diag.reject_reason == KUI_RETAIL_GD_REJECT_DESTINATION && !service.diag.read_flags);
    CHECK(!ctx.reads && !ctx.checks && !service.error && service.status == KUI_GD_NOT_FOUND);
    read_params(lba, count, END - 2048u);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM) == 0);
    CHECK_DIAG(service.diag.reject_reason == KUI_RETAIL_GD_REJECT_DESTINATION);
    CHECK(!ctx.reads && !ctx.checks);
    ctx.deny = output;
    read_params(lba, count, output);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM) == 0);
    CHECK_DIAG(service.diag.reject_reason == KUI_RETAIL_GD_REJECT_DESTINATION);
    CHECK(!ctx.reads && !ctx.checks);
    ctx.deny = 0;
    use_full_tracks = 0;
    reset(); read_params(lba, count, output);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM) == 0);
    CHECK_DIAG(service.diag.reject_reason == KUI_RETAIL_GD_REJECT_IMAGE_RANGE);
    CHECK(ctx.checks == 1 && !ctx.reads);
    CHECK(!service.command && !service.pending && !service.error && service.status == KUI_GD_NOT_FOUND);
    read_params(45000, 1, OUTPUT); put(PARAM, 149);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM) == 0);
    CHECK_DIAG(service.diag.reject_reason == KUI_RETAIL_GD_REJECT_READ_FAD);
    read_params(45000, 0, OUTPUT);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM) == 0);
    CHECK_DIAG(service.diag.reject_reason == KUI_RETAIL_GD_REJECT_READ_COUNT);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM + 1) == 0);
    CHECK_DIAG(service.diag.reject_reason == KUI_RETAIL_GD_REJECT_PARAMETERS && !service.diag.read_flags);
    CHECK(call(KUI_GD_REQUEST, 0xffffffffu, PARAM) == 0);
    CHECK_DIAG(service.diag.reject_reason == KUI_RETAIL_GD_REJECT_UNSUPPORTED);
    int32_t token = call(KUI_GD_REQUEST, KUI_GD_NOP, 0);
    CHECK(token > 0);
    CHECK_DIAG(service.diag.reject_reason == KUI_RETAIL_GD_REJECT_NONE);
    int32_t status = service.status; uint32_t command = service.command, error = service.error;
    CHECK(call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM) == 0);
    CHECK_DIAG(service.diag.reject_reason == KUI_RETAIL_GD_REJECT_OWNED);
    CHECK(service.status == status && service.command == command && service.error == error);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_COMPLETED);
#ifdef KUI_RETAIL_CE
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_DMAREAD_STREAM, PARAM) == 0);
    CHECK_DIAG(service.diag.reject_reason == KUI_RETAIL_GD_REJECT_UNSUPPORTED);
#endif
}
static void command_acknowledgment(void) {
    reset();
    int32_t token = call(KUI_GD_REQUEST, KUI_GD_NOP, 0);
    CHECK(token > 0);
    /* An invalid handle reports illegal request, without consuming either
     * a pending command or its completion awaiting acknowledgment. */
    const uint32_t invalid[] = {0, (uint32_t)token + 1u, UINT32_MAX};
    for(unsigned i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        CHECK(call(KUI_GD_CHECK, invalid[i], STATUS) == KUI_GD_FAILED);
        CHECK(get(STATUS) == 5 && get(STATUS + 4) == 0 &&
              get(STATUS + 8) == 0 && get(STATUS + 12) == 0);
        CHECK(service.pending && service.token == (uint32_t)token);
    }
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_PROCESSING);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && !service.pending);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_NOP, 0) == 0);
    CHECK(service.token == (uint32_t)token);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token + 1u, STATUS) == KUI_GD_FAILED);
    CHECK(get(STATUS) == 5 && get(STATUS + 4) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_COMPLETED);
    CHECK(get(STATUS) == 0 && get(STATUS + 8) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_NOT_FOUND);
    CHECK(get(STATUS) == 0 && get(STATUS + 4) == 0 &&
          get(STATUS + 8) == 0 && get(STATUS + 12) == 0);

    /* A failed command also owns the slot until the valid CHECK consumes
     * its failure. A stale handle must not replace the stored error. */
    put(PARAM, OUTPUT);
    token = call(KUI_GD_REQUEST, KUI_RETAIL_GD_GET_VERS, PARAM);
    CHECK(token > 0);
    ctx.deny = OUTPUT;
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && !service.pending);
    ctx.deny = 0;
    CHECK(call(KUI_GD_REQUEST, KUI_GD_NOP, 0) == 0);
    CHECK(service.token == (uint32_t)token && service.error == KUI_GD_ERROR_MEMORY);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token - 1u, STATUS) == KUI_GD_FAILED);
    CHECK(get(STATUS) == 5 && get(STATUS + 4) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_FAILED);
    CHECK(get(STATUS) == 1 && get(STATUS + 4) == KUI_GD_ERROR_MEMORY);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_NOT_FOUND);
    CHECK(get(STATUS) == 0 && get(STATUS + 4) == 0);
    int32_t next = call(KUI_GD_REQUEST, KUI_GD_NOP, 0);
    CHECK(next > token);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_FAILED);
    CHECK(get(STATUS) == 5 && service.pending);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)next, STATUS) == KUI_GD_COMPLETED);
    CHECK(ctx.reads == 0 && ctx.checks == 0);
}
#ifndef KUI_RETAIL_GD_ASYNC
static void large_reads(void) {
    reset();
    const uint32_t aliases[] = {0,0x80000000u,0xa0000000u};
#ifdef KUI_RETAIL_CE
    /* CE's 0x0c... CPU pointers are virtual (virtual_pointers below). */
    for(unsigned a = 1; a < 3; ++a) {
#else
    for(unsigned a = 0; a < 3; ++a) {
#endif
        read_params(45000, 129, (OUTPUT & 0x1fffffffu) | aliases[a]);
        uint32_t read_maps = ctx.maps[0], write_maps = ctx.maps[1];
        uint32_t validations = ctx.maps[KUI_RETAIL_MAP_VALIDATE], checks = ctx.checks;
        int32_t token = call(KUI_GD_REQUEST, a ? KUI_GD_DMAREAD : KUI_GD_PIOREAD,
                            (PARAM & 0x1fffffffu) | aliases[a]);
        CHECK(token > 0);
        CHECK(ctx.maps[0] == read_maps + 1 && ctx.maps[1] == write_maps);
        CHECK(ctx.maps[KUI_RETAIL_MAP_VALIDATE] == validations + 1);
        CHECK(ctx.validate_address == OUTPUT && ctx.validate_bytes == 129 * 2048);
        CHECK(ctx.checks == checks + 17 && ctx.max_checked == 8);
        uint32_t before = ctx.reads;
        CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_PROCESSING);
        CHECK(ctx.reads == before && get(STATUS + 8) == 0);
        CHECK(call(KUI_GD_REQUEST, KUI_GD_NOP, 0) == 0);
        for(uint32_t done = 0; done < 129;) {
            CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
            uint32_t step = 129 - done < 2 ? 129 - done : 2;
            CHECK(ctx.last_count == step);
            done += step;
            CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) ==
                  (done == 129 ? KUI_GD_COMPLETED : KUI_GD_PROCESSING));
            CHECK(get(STATUS + 8) == done * 2048);
#ifdef KUI_RETAIL_CE
            /* CE's driver waits on the drive's interrupt each step raises. */
            CHECK(get(STATUS + 12) == (done == 129 ? 0u : 1u));
            CHECK(service.interrupts == KUI_RETAIL_GD_IRQ_DRIVE);
            service.interrupts = 0;
#else
            CHECK(get(STATUS + 12) == (done == 129 ? 0u : 4u));
#endif
        }
        CHECK(ctx.reads == before + 65 && ctx.max_count == 2 && ctx.last_count == 1);
        CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_NOT_FOUND);
        CHECK(get(STATUS + 8) == 0);
        CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && ctx.reads == before + 65);
        CHECK(ram[OUTPUT - BEGIN + 129 * 2048] == 0xa5);
        for(unsigned n = 0; n < 129; ++n)
            for(unsigned i = 0; i < 2048; i += 127)
                CHECK(ram[OUTPUT - BEGIN + n * 2048 + i] == pattern(45000 + n, i));
    }
#ifdef KUI_RETAIL_CE
    CHECK(service.diag.sectors_read == 258 && service.diag.requests == 2);
#else
    CHECK(service.diag.sectors_read == 387 && service.diag.requests == 3);
#endif
    /* Full available RAM-sized requests have no arbitrary 64-sector ceiling. */
    uint32_t count = (END - OUTPUT) / 2048;
    read_params(45000, count, OUTPUT);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM) > 0);
#ifdef KUI_RETAIL_CE
    CHECK(service.request_bytes == count * 2048 && ctx.reads == 130);
#else
    CHECK(service.request_bytes == count * 2048 && ctx.reads == 195);
#endif
    CHECK(call(KUI_GD_ABORT, service.token, 0) == 0);
}
#endif
#ifndef KUI_RETAIL_GD_ASYNC
static void cancel_failures(void) {
    reset(); read_params(45000, 20, OUTPUT);
    int32_t token = call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM);
    CHECK(token > 0); ctx.reenter = 1;
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && ctx.sectors == 2);
    CHECK(call(KUI_GD_ABORT, (uint32_t)token, 0) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_FAILED);
    CHECK(get(STATUS + 4) == KUI_GD_ERROR_CANCELLED && get(STATUS + 8) == 2 * 2048);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && ctx.sectors == 2);
    CHECK(ram[OUTPUT - BEGIN + 2 * 2048] == 0xa5);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_NOT_FOUND);
    ctx.reenter = 0; ctx.fail_at = 3;
    token = call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM);
    CHECK(token > 0); CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_FAILED);
    CHECK(get(STATUS + 4) == KUI_GD_ERROR_IO && get(STATUS + 8) == 2 * 2048);
    ctx.fail_at = 0;
    token = call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM);
    CHECK(token > 0); ctx.deny = OUTPUT;
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && ctx.reads == 3);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_FAILED);
    CHECK(get(STATUS + 4) == KUI_GD_ERROR_MEMORY && get(STATUS + 8) == 0);
    ctx.deny = 0; token = call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM);
    CHECK(token > 0); CHECK(call(KUI_GD_INIT, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_NOT_FOUND);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && ctx.reads == 3);
}
#endif
#ifndef KUI_RETAIL_GD_ASYNC
static void paced_steps(void) {
    /* The adapter may change the EXEC size between calls; each chunk stays
     * contiguous, and invalid values fall back to the two-sector default. */
    reset(); CHECK(service.step == KUI_RETAIL_GD_STEP_SECTORS);
    read_params(45000, 40, OUTPUT);
    int32_t token = call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM);
    CHECK(token > 0);
    const uint32_t steps[] = {6, 0, 1, KUI_RETAIL_GD_STEP_MAX, KUI_RETAIL_GD_STEP_MAX + 1,
                              UINT32_MAX, 3, 8, 8};
    const uint32_t expected[] = {6, 2, 1, 8, 2, 2, 3, 8, 8};
    uint32_t done = 0;
    for(unsigned i = 0; i < sizeof(steps) / sizeof(*steps); ++i) {
        service.step = steps[i];
        uint32_t n = expected[i] < 40 - done ? expected[i] : 40 - done;
        CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && ctx.last_count == n);
        done += n;
        CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) ==
              (done == 40 ? KUI_GD_COMPLETED : KUI_GD_PROCESSING));
        CHECK(get(STATUS + 8) == done * 2048);
        if(done == 40) break;
    }
    CHECK(done == 40 && ctx.sectors == 40 && service.diag.read_steps == 9);
    CHECK(ctx.max_count == KUI_RETAIL_GD_STEP_MAX);
    CHECK(ram[OUTPUT - BEGIN + 40 * 2048] == 0xa5);
    for(unsigned n = 0; n < 40; ++n)
        for(unsigned i = 0; i < 2048; i += 211)
            CHECK(ram[OUTPUT - BEGIN + n * 2048 + i] == pattern(45000 + n, i));
    /* Protocol resets restore drive state, not the adapter's pacing choice. */
    service.step = 5; CHECK(call(KUI_GD_INIT, 0, 0) == 0 && service.step == 5);
}
#endif
static void metadata(void) {
    reset();
    for(uint32_t area = 0; area < 2; ++area) {
        put(PARAM, area); put(PARAM + 4, OUTPUT);
        int32_t token = call(KUI_GD_REQUEST, KUI_GD_GETTOC2, PARAM);
        CHECK(token > 0); CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
        CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_COMPLETED);
        CHECK(get(STATUS + 8) == 408);
        CHECK(get(OUTPUT + 396) == (area ? 0x41030000u : 0x41010000u));
        CHECK(get(OUTPUT + 400) == (area ? 0x41030000u : 0x01020000u));
        CHECK(get(OUTPUT + 404) == (area ? 0x41000000u | 60150u : 0x01000000u | 174u));
        CHECK(get(OUTPUT + (area ? 8 : 0)) == (0x41000000u | (area ? 45150u : 150u)));
        CHECK(get(OUTPUT + (area ? 0 : 8)) == UINT32_MAX);
    }
    for(unsigned i = 0; i < 4; ++i) put(PARAM + i * 4u, i + 10);
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_SET_MODE, PARAM) > 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, service.token, STATUS) == KUI_GD_COMPLETED);
    CHECK(get(STATUS + 8) == 10);
    put(PARAM, OUTPUT);
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_REQ_MODE, PARAM) > 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, service.token, STATUS) == KUI_GD_COMPLETED);
    /* CHECK reports the GD wire transfer's ten bytes even though the
     * syscall expands the mode into four words in the guest buffer. */
    CHECK(get(STATUS + 8) == 10);
    for(unsigned i = 0; i < 4; ++i) CHECK(get(OUTPUT + i * 4u) == i + 10);
    put(PARAM, 45160);
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_SEEK, PARAM) > 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, service.token, STATUS) == KUI_GD_COMPLETED);
    for(unsigned i = 0; i < 4; ++i) put(PARAM + i * 4u, OUTPUT + i * 8u);
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_REQ_STAT, PARAM) > 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(get(OUTPUT) == 1 && get(OUTPUT + 8) == 3 && get(OUTPUT + 24) == 1);
    CHECK(get(OUTPUT + 16) == (0x14000000u | 45160u));
    CHECK(call(KUI_GD_CHECK, service.token, STATUS) == KUI_GD_COMPLETED);
    CHECK(ctx.reads == 0);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_STOP, 0) > 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, service.token, STATUS) == KUI_GD_COMPLETED);
    CHECK(call(KUI_GD_DRIVE, STATUS, 0) == 0 && get(STATUS) == 2 && get(STATUS + 4) == 0x80);
    CHECK(call(KUI_GD_RESET, 0, 0) == 0);
    CHECK(call(KUI_GD_DRIVE, STATUS, 0) == 0 && get(STATUS) == 1);
}
static void cd_metadata(void) {
    reset();
    static const union kui_retail_slot cd[]={
        {.track={.start_lba=0,.end_lba=8,.control=4|KUI_RETAIL_TRACK_COOKED}},
        {.track={.start_lba=16,.end_lba=24,.control=KUI_RETAIL_TRACK_2448}},
        {.track={.start_lba=44000,.end_lba=46000,.control=4|KUI_RETAIL_TRACK_MODE2|KUI_RETAIL_TRACK_OFFSET_HIGH}},
        {.track={.start_lba=48000,.end_lba=60000,.control=4|KUI_RETAIL_TRACK_MODE2|KUI_RETAIL_TRACK_2336}}
    };
    CHECK(kui_retail_gd_init(&service,cd,4,&ops,BEGIN,END)==-1); /* Public initializer remains GD. */
    kui_retail_gd_init_validated(&service,cd,4,&ops,BEGIN,END);
    kui_retail_gd_set_disc_type(&service,0x10,44000);
    CHECK(service.disc_type==0x10 && service.position_lba==44000);
    kui_retail_gd_set_disc_type(&service,0x20,0);
    CHECK(service.disc_type==0x10 && service.position_lba==44000);
    mode(2048,2048); /* Explicit Mode2 Form1 selects the same 2048-byte payload. */
    for(unsigned command=0;command<2;command++) {
        put(PARAM,0);put(PARAM+4,OUTPUT);
        int32_t token=call(KUI_GD_REQUEST,command?KUI_GD_GETTOC2:KUI_RETAIL_GD_GETTOC,PARAM);
        CHECK(token>0);
        kui_retail_gd_set_disc_type(&service,0x80,0);CHECK(service.disc_type==0x10);
        CHECK(call(KUI_GD_DRIVE,STATUS,0)==0 && get(STATUS)==0 && get(STATUS+4)==0x10);
        CHECK(call(KUI_GD_EXEC,0,0)==0);
        CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
        CHECK(get(OUTPUT)==(0x41000000u|150u));
        CHECK(get(OUTPUT+4)==(0x01000000u|166u));
        CHECK(get(OUTPUT+8)==(0x41000000u|44150u));
        CHECK(get(OUTPUT+12)==(0x41000000u|48150u));
        CHECK(get(OUTPUT+396)==0x41010000u && get(OUTPUT+400)==0x41040000u);
        CHECK(get(OUTPUT+404)==(0x41000000u|60150u));
    }
    uint8_t previous[KUI_GD_TOC_BYTES];memcpy(previous,ram+OUTPUT-BEGIN,sizeof(previous));
    /* A normal high-density probe on a CD completes without any IO error,
     * so the resident adapter does not treat its submission as fatal. */
    for(unsigned command=0;command<2;command++) {
        put(PARAM,1);put(PARAM+4,OUTPUT);
        int32_t token=call(KUI_GD_REQUEST,command?KUI_GD_GETTOC2:KUI_RETAIL_GD_GETTOC,PARAM);
        CHECK(token>0 && service.pending);
        CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_PROCESSING);
        CHECK(get(STATUS)==0 && get(STATUS+4)==0 && get(STATUS+8)==0);
        CHECK(call(KUI_GD_EXEC,0,0)==0);
        CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_FAILED);
        CHECK(get(STATUS)==1 && get(STATUS+4)==KUI_GD_ERROR_UNAVAILABLE && get(STATUS+8)==0 && get(STATUS+12)==0);
        CHECK(service.diag.last_error==KUI_GD_ERROR_UNAVAILABLE && !service.pending);
        CHECK(!memcmp(previous,ram+OUTPUT-BEGIN,sizeof(previous)));
        CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_NOT_FOUND);
    }
    put(PARAM,2);put(PARAM+4,OUTPUT);
    CHECK(call(KUI_GD_REQUEST,KUI_GD_GETTOC2,PARAM)==0 && !service.pending);
    CHECK(!memcmp(previous,ram+OUTPUT-BEGIN,sizeof(previous)));
    /* An unavailable area still validates its entire aligned destination. */
    put(PARAM,1);put(PARAM+4,OUTPUT+1);
    CHECK(call(KUI_GD_REQUEST,KUI_GD_GETTOC2,PARAM)==0 && !service.pending);
    put(PARAM+4,END-KUI_GD_TOC_BYTES+4u);
    CHECK(call(KUI_GD_REQUEST,KUI_GD_GETTOC2,PARAM)==0 && !service.pending);
    put(PARAM,48155);
    CHECK(call(KUI_GD_REQUEST,KUI_RETAIL_GD_SEEK,PARAM)>0);
    CHECK(call(KUI_GD_EXEC,0,0)==0);
    CHECK(call(KUI_GD_CHECK,service.token,STATUS)==KUI_GD_COMPLETED);
    for(unsigned i=0;i<4;i++) put(PARAM+i*4u,OUTPUT+i*8u);
    CHECK(call(KUI_GD_REQUEST,KUI_RETAIL_GD_REQ_STAT,PARAM)>0);
    CHECK(call(KUI_GD_EXEC,0,0)==0);
    CHECK(get(OUTPUT+8)==4 && get(OUTPUT+16)==(0x14000000u|48155u));
    CHECK(call(KUI_GD_CHECK,service.token,STATUS)==KUI_GD_COMPLETED);
    CHECK(call(KUI_GD_REQUEST,KUI_GD_STOP,0)>0 && call(KUI_GD_EXEC,0,0)==0);
    CHECK(call(KUI_GD_CHECK,service.token,STATUS)==KUI_GD_COMPLETED);
    CHECK(call(KUI_GD_DRIVE,STATUS,0)==0 && get(STATUS)==2 && get(STATUS+4)==0x10);
    CHECK(call(KUI_GD_INIT,0,0)==0 && call(KUI_GD_RESET,0,0)==0);
    CHECK(call(KUI_GD_DRIVE,STATUS,0)==0 && get(STATUS+4)==0x10);
    CHECK(service.sector_bytes==2048 && ctx.reads==0 && ctx.checks==0);
}
static void prepared_initialization(void) {
    static struct kui_retail_manifest m;
    m.track_count=3;memcpy(m.slots,tracks,sizeof(tracks));
    for(unsigned cd=0;cd<2;cd++) {
        m.flags=cd?(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED|KUI_RETAIL_IMAGE_BOOT_CRC):0;
        m.session_lba=cd?0:45000;
        struct kui_retail_gd expected;
        kui_retail_gd_init_validated(&expected,m.slots,m.track_count,&ops,BEGIN,END);
        kui_retail_gd_set_disc_type(&expected,cd?0x10:0x80,m.session_lba);
        memset(&service,0,sizeof(service)); /* The resident's _start contract. */
        kui_retail_gd_init_prepared(&service,&m,&ops,BEGIN,END);
        CHECK(!memcmp(&service,&expected,sizeof(service)));
        CHECK(call(KUI_GD_DRIVE,STATUS,0)==0 && get(STATUS+4)==(cd?0x10u:0x80u));
        CHECK(call(KUI_GD_INIT,0,0)==0 && call(KUI_GD_RESET,0,0)==0);
        CHECK(service.disc_type==(cd?0x10u:0x80u) && service.position_lba==m.session_lba);
    }
}
static void silent_cd_audio(void) {
    /* Disc audio commands complete at once, without reads, output or a
     * change of drive state, so a game waiting on them keeps running. */
    reset();
    const uint32_t commands[] = {KUI_RETAIL_GD_PLAY, KUI_RETAIL_GD_PLAY2,
                                 KUI_RETAIL_GD_PAUSE, KUI_RETAIL_GD_RELEASE};
    for(unsigned i = 0; i < 4; ++i) {
        put(PARAM, 3); put(PARAM + 4, 4); put(PARAM + 8, 15);
        memset(ram + OUTPUT - BEGIN, 0xa5, 64);
        int32_t token = call(KUI_GD_REQUEST, commands[i], PARAM);
        CHECK(token > 0);
        CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_PROCESSING);
        CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
        CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_COMPLETED);
        CHECK(get(STATUS) == 0 && get(STATUS + 8) == 0 && get(STATUS + 12) == 0);
        CHECK(call(KUI_GD_DRIVE, STATUS, 0) == 0 && get(STATUS) == 1);
        CHECK(ram[OUTPUT - BEGIN] == 0xa5 && get(PARAM) == 3);
    }
    /* PLAY's three parameter words must still be readable guest memory. */
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_PLAY, END - 8) == 0);
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_PLAY2, 0) == 0);
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_PAUSE, 0) > 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, service.token, STATUS) == KUI_GD_COMPLETED);
    CHECK(ctx.reads == 0 && ctx.checks == 0 && service.diag.read_steps == 0);
}
static void version_query(void) {
    static const uint8_t expected[28] = "GDC Version 1.10 1999-03-31\002";
    const uint32_t destinations[] = {OUTPUT + 1, LOW_ALIAS(OUTPUT + 1),
                                    (OUTPUT + 1) | 0x20000000u, END - 28};
    reset();
    for(unsigned i = 0; i < sizeof(destinations) / sizeof(*destinations); ++i) {
        uint32_t dst = (destinations[i] & 0x00ffffffu) | 0x8c000000u;
        memset(ram + dst - BEGIN, 0xa5, 28);
        /* A single parameter word suffices, even at the end of guest RAM. */
        uint32_t params = i == 1 ? END - 4 : PARAM;
        put(params, destinations[i]);
        int32_t token = call(KUI_GD_REQUEST, KUI_RETAIL_GD_GET_VERS, params);
        CHECK(token > 0 && ram[dst - BEGIN] == 0xa5);
        CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_PROCESSING);
        CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
        CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_COMPLETED);
        CHECK(get(STATUS) == 0 && get(STATUS + 8) == 0 && get(STATUS + 12) == 0);
        CHECK(memcmp(ram + dst - BEGIN, expected, 28) == 0);
        CHECK(ram[dst - BEGIN - 1] == 0xa5);
        if(dst + 28 < END) CHECK(ram[dst - BEGIN + 28] == 0xa5);
        CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_NOT_FOUND);
    }
    const uint32_t bad[] = {0, BEGIN - 1, END - 27, END, 0xa05f0000u};
    for(unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        put(PARAM, bad[i]);
        CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_GET_VERS, PARAM) == 0);
    }
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_GET_VERS, PARAM + 1) == 0);
    put(PARAM, OUTPUT); ctx.deny = OUTPUT;
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_GET_VERS, PARAM) == 0);
    ctx.deny = 0;
    int32_t token = call(KUI_GD_REQUEST, KUI_RETAIL_GD_GET_VERS, PARAM);
    CHECK(token > 0); ctx.deny = OUTPUT;
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_FAILED);
    CHECK(get(STATUS + 4) == KUI_GD_ERROR_MEMORY);
    CHECK(ctx.reads == 0 && ctx.checks == 0);
}
#ifndef KUI_RETAIL_GD_ASYNC
static void subcode_query(void) {
    /* Expected raw Q generated independently from the 45035-LBA position:
     * track 3, relative 00:00:35, absolute 10:02:35; CRC-CCITT complemented.
     * Formatted Q uses binary FAD 45185 = 0x00b081, not BCD MSF. */
    static const uint8_t raw[100] = { 0x0,0x15,0x0,0x64,0x0,0x40,0x0,0x0,0x0,0x0,0x0,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x40,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x40,0x40,0x0,0x40,0x0,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x40,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x0,0x40,0x0,0x0,0x0,0x40,0x40,0x0,0x40,0x0,0x40,0x40,0x40,0x40,0x0,0x0,0x40,0x0,0x0,0x0,0x40,0x40,0x40,0x40,0x0,0x0,0x40 };
    static const uint8_t formatted[14] = {0,0x15,0,14,0x41,3,1,0,0,35,0,0,0xb0,0x81};
    reset(); read_params(45035, 1, OUTPUT);
    CHECK(call(KUI_GD_REQUEST,KUI_GD_PIOREAD,PARAM)>0);
    CHECK(call(KUI_GD_EXEC,0,0)==0);
    CHECK(call(KUI_GD_CHECK,service.token,STATUS)==KUI_GD_COMPLETED);
    uint32_t reads=ctx.reads, checks=ctx.checks;
    for(unsigned format=0;format<3;format++) {
        uint32_t length=format==0?100:format==1?14:24;
        for(unsigned short_read=0;short_read<2;short_read++) {
            uint32_t bytes=short_read?5:length;
            uint32_t dest=short_read?END-bytes:OUTPUT+1;
            memset(ram+dest-BEGIN,0xa5,bytes+(short_read?0:1));
            put(PARAM,format);put(PARAM+4,bytes);put(PARAM+8,LOW_ALIAS(dest));
            int32_t token=call(KUI_GD_REQUEST,KUI_RETAIL_GD_GETSCD,PARAM);
            CHECK(token>0 && ram[dest-BEGIN]==0xa5);
            CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_PROCESSING);
            CHECK(call(KUI_GD_EXEC,0,0)==0);
            CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
            CHECK(get(STATUS+8)==bytes && get(STATUS+12)==0);
            if(format<2) CHECK(!memcmp(ram+dest-BEGIN,format?formatted:raw,bytes));
            else {
                CHECK(ram[dest-BEGIN+3]==24 && ram[dest-BEGIN+4]==2);
                if(!short_read) CHECK(ram[dest-BEGIN+8]==0 && !memcmp(ram+dest-BEGIN+9,"0000000000000",13));
            }
            CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_NOT_FOUND);
            if(!short_read) CHECK(ram[dest-BEGIN+bytes]==0xa5);
        }
    }
    put(PARAM,1);put(PARAM+4,14);put(PARAM+8,END-13);
    CHECK(call(KUI_GD_REQUEST,KUI_RETAIL_GD_GETSCD,PARAM)==0);
    put(PARAM+8,OUTPUT);put(PARAM,3);
    CHECK(call(KUI_GD_REQUEST,KUI_RETAIL_GD_GETSCD,PARAM)==0);
    CHECK(service.diag.last_lba==3 && service.diag.last_count==14 && service.diag.last_destination==OUTPUT);
    put(PARAM,1);put(PARAM+4,0);
    CHECK(call(KUI_GD_REQUEST,KUI_RETAIL_GD_GETSCD,PARAM)==0);
    put(PARAM+4,UINT32_MAX);memset(ram+OUTPUT-BEGIN,0xa5,32);
    int32_t token=call(KUI_GD_REQUEST,KUI_RETAIL_GD_GETSCD,PARAM);
    CHECK(token>0);CHECK(call(KUI_GD_EXEC,0,0)==0);
    CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED && get(STATUS+8)==14);
    CHECK(ram[OUTPUT-BEGIN+14]==0xa5);
    token=call(KUI_GD_REQUEST,KUI_RETAIL_GD_GETSCD,PARAM);CHECK(token>0);
    ctx.deny=OUTPUT;CHECK(call(KUI_GD_EXEC,0,0)==0);
    CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_FAILED);
    CHECK(get(STATUS+4)==KUI_GD_ERROR_MEMORY);ctx.deny=0;
    CHECK(ctx.reads==reads && ctx.checks==checks);
}
#endif
#ifndef KUI_RETAIL_GD_ASYNC
static void bounds_and_modes(void) {
    reset(); mode(2048, 0); mode(2048, 1024); mode(2352, 0);
    read_params(16, 2, OUTPUT);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM) > 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, service.token, STATUS) == KUI_GD_COMPLETED && get(STATUS + 8) == 4704);
    mode(2048, 0); read_params(16, 1, OUTPUT);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM) == 0);
    const uint32_t bad[] = {0,BEGIN - 4,END,END - 4,PARAM + 1,0xad000000u,0xa05f0000u,0x9c010000u};
    for(unsigned i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        CHECK(call(KUI_GD_REQUEST, KUI_GD_PIOREAD, bad[i]) == 0);
        read_params(45000, 1, bad[i]);
        CHECK(call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM) == 0);
    }
    read_params(45000, UINT32_MAX, OUTPUT);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM) == 0);
    read_params(45000, 8192, OUTPUT);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM) == 0);
    read_params(59999, 2, OUTPUT);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM) == 0);
    read_params(45000, 1, OUTPUT + 2);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM) == 0);
    read_params(45000, 1, OUTPUT); put(PARAM + 12, 1);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM) == 0);
    CHECK(ctx.reads == 1);
    CHECK(call(KUI_GD_DMA_CALLBACK, 0, 0) == 0);
    CHECK(call(KUI_GD_DMA_CALLBACK, OUTPUT, 0) == -1);
    CHECK(call(KUI_GD_DMA_TRANSFER, 1, PARAM) == -1);
#ifdef KUI_RETAIL_CE
    CHECK(call(KUI_GD_DMA_CHECK, 1, PARAM) == 0); /* Stream reads: see stream_reads. */
#else
    CHECK(call(KUI_GD_DMA_CHECK, 1, PARAM) == -1);
#endif
    CHECK(call(KUI_GD_REQUEST, 38, PARAM) == 0);
    CHECK(kui_retail_gd_dispatch(&service, 0, 0, UINT32_MAX, 0) == -1);
    CHECK(service.diag.rejected > 0 && service.diag.last_result == -1);
    CHECK(kui_retail_gd_init(NULL, tracks, 3, &ops, BEGIN, END) == -1);
    CHECK(kui_retail_gd_init(&service, tracks, 0, &ops, BEGIN, END) == -1);
    CHECK(kui_retail_gd_init(&service, tracks, 3, &ops, 0x8c007ffcu, END) == -1);
    union kui_retail_slot invalid[3]; memcpy(invalid, tracks, sizeof(invalid));
    invalid[2].track.end_lba = 720000;
    CHECK(kui_retail_gd_init(&service, invalid, 3, &ops, BEGIN, END) == -1);
    memcpy(invalid, tracks, sizeof(invalid)); invalid[1].track.control = 1;
    CHECK(kui_retail_gd_init(&service, invalid, 3, &ops, BEGIN, END) == -1);
    memcpy(invalid, tracks, sizeof(invalid)); invalid[1].track.start_lba = 7;
    CHECK(kui_retail_gd_init(&service, invalid, 3, &ops, BEGIN, END) == -1);
    memcpy(invalid, tracks, sizeof(invalid)); invalid[2].track.start_lba = 44999;
    CHECK(kui_retail_gd_init(&service, invalid, 3, &ops, BEGIN, END) == -1);
}
#endif
#ifdef KUI_RETAIL_GD_ASYNC
/* A background reader: EXEC never reads, the adapter reports progress, and
 * CHECK shows each step until the request completes or fails. */
static void async_reads(void) {
    reset();
    read_params(45000, 20, OUTPUT);
    int32_t token = call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM);
    CHECK(token > 0 && service.pending);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && !ctx.reads && service.pending);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_PROCESSING);
    CHECK(get(STATUS + 8) == 0 && get(STATUS + 12) == 4);
    kui_retail_gd_progress(&service, 3, 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_PROCESSING);
    CHECK(get(STATUS + 8) == 3 * 2048 && service.position_lba == 45002);
    CHECK(service.diag.sectors_read == 3 && service.diag.read_steps == 1);
    kui_retail_gd_progress(&service, 2, 0); /* never backwards */
    kui_retail_gd_progress(&service, 3, 0); /* no new sectors, no new step */
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_PROCESSING);
    CHECK(get(STATUS + 8) == 3 * 2048 && service.diag.read_steps == 1);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_NOP, 0) == 0);
    kui_retail_gd_progress(&service, 99, 0); /* clamped to the request */
    CHECK(!service.pending && service.diag.sectors_read == 20);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_COMPLETED);
    CHECK(get(STATUS + 8) == 20 * 2048 && get(STATUS + 12) == 0 && get(STATUS + 4) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_NOT_FOUND);
    kui_retail_gd_progress(&service, 5, KUI_GD_ERROR_IO); /* nothing pending */
    CHECK(!service.error && service.diag.sectors_read == 20);
    /* A failure keeps the sectors already delivered. */
    mode(2352, 0);
    read_params(45100, 6, OUTPUT);
    token = call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM);
    CHECK(token > 0);
    kui_retail_gd_progress(&service, 4, KUI_GD_ERROR_IO);
    CHECK(!service.pending && service.diag.last_error == KUI_GD_ERROR_IO);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_FAILED);
    CHECK(get(STATUS + 4) == KUI_GD_ERROR_IO && get(STATUS + 8) == 4 * 2352);
    /* Abort ends the request; later progress is ignored. */
    token = call(KUI_GD_REQUEST, KUI_GD_PIOREAD, PARAM);
    CHECK(token > 0 && call(KUI_GD_ABORT, (uint32_t)token, 0) == 0);
    kui_retail_gd_progress(&service, 6, 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_FAILED);
    CHECK(get(STATUS + 4) == KUI_GD_ERROR_CANCELLED && get(STATUS + 8) == 0);
    /* Other commands still complete on EXEC and ignore read progress. */
    put(PARAM, 0); put(PARAM + 4, OUTPUT);
    token = call(KUI_GD_REQUEST, KUI_GD_GETTOC2, PARAM);
    CHECK(token > 0);
    kui_retail_gd_progress(&service, 1, KUI_GD_ERROR_IO);
    CHECK(service.pending && !service.error);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && !service.pending);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_COMPLETED);
    CHECK(!ctx.reads);
}
#endif
#ifdef KUI_RETAIL_CE
/* Windows CE's DMA stream reads (DMAREAD_STREAM_EX and DMA_TRANSFER). */
static struct { uint32_t calls, lba, sector_bytes, skip, bytes, fail; } part;
static int read_part(void *unused, uint32_t lba, uint32_t sector_bytes,
                     uint32_t skip, uint32_t bytes, void *output) {
    (void)unused; ++part.calls;
    part.lba = lba; part.sector_bytes = sector_bytes; part.skip = skip; part.bytes = bytes;
    if(part.fail && part.calls == part.fail) return -1;
    uint8_t *p = output;
    for(uint32_t i = 0; i < bytes; ++i)
        *p++ = pattern(lba + (skip + i) / sector_bytes, (skip + i) % sector_bytes);
    return 0;
}
static void stream_params(uint32_t lba, uint32_t count) {
    put(PARAM, lba + 150); put(PARAM + 4, count); put(PARAM + 8, 0);
}
#define PIECE (PARAM + 0x40u)
static int32_t piece(int32_t token, uint32_t destination, uint32_t bytes) {
    put(PIECE, destination); put(PIECE + 4, bytes);
    return call(KUI_GD_DMA_TRANSFER, (uint32_t)token, PIECE);
}
static void stream_bytes(uint32_t destination, uint32_t lba, uint32_t skip,
                         uint32_t bytes, uint32_t sector_bytes) {
    for(uint32_t i = 0; i < bytes; ++i)
        CHECK(ram[destination - BEGIN + i] ==
              pattern(lba + (skip + i) / sector_bytes, (skip + i) % sector_bytes));
}
/* With CE's MMU on, a 0x0c... pointer the CPU uses (parameters, status) is
 * virtual, while a DMA destination there is physical RAM. */
static void virtual_pointers(void) {
    reset(); memset(virt, 0x5a, sizeof(virt));
    uint32_t params = VIRT + 0xf4ec, status = VIRT + 0xf500;
    uint32_t physical = (OUTPUT & 0x1fffffffu);
    uint8_t *v = virt + (params - VIRT);
    const uint32_t words[4] = {45000 + 150, 3, physical, 0};
    for(unsigned i = 0; i < 4; ++i)
        for(unsigned b = 0; b < 4; ++b) v[i * 4 + b] = (uint8_t)(words[i] >> (b * 8));
    int32_t token = call(KUI_GD_REQUEST, KUI_GD_DMAREAD, params);
    CHECK(token > 0 && ctx.validate_address == OUTPUT);
    while(service.pending) CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, status) == KUI_GD_COMPLETED);
    CHECK(virt[status - VIRT + 8] == 0x00 && virt[status - VIRT + 9] == 0x18); /* 3 * 2048 */
    for(uint32_t i = 0; i < 3 * 2048; ++i)
        CHECK(ram[OUTPUT - BEGIN + i] == pattern(45000 + i / 2048, i % 2048));
    CHECK(virt[0] == 0x5a && virt[physical & 0xffffu] == 0x5a);
    /* A PIO destination is the CPU's, so virtual too; a DMA one never is. */
    v = virt + (params - VIRT);
    const uint32_t pio[4] = {45000 + 150, 1, VIRT, 0};
    for(unsigned i = 0; i < 4; ++i)
        for(unsigned b = 0; b < 4; ++b) v[i * 4 + b] = (uint8_t)(pio[i] >> (b * 8));
    token = call(KUI_GD_REQUEST, KUI_GD_PIOREAD, params);
    CHECK(token > 0);
    while(service.pending) CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    for(uint32_t i = 0; i < 2048; ++i) CHECK(virt[i] == pattern(45000, i));
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, status) == KUI_GD_COMPLETED);
    /* A DMA destination outside RAM's physical and P1/P2 areas (another
     * process slot) is refused; in 0x0c... it is RAM, never virtual. */
    v[8] = 0x00; v[9] = 0xf0; v[10] = 0x04; v[11] = 0x02;
    CHECK(call(KUI_GD_REQUEST, KUI_GD_DMAREAD, params) == 0);
}
static void stream_reads(void) {
    reset(); memset(&part, 0, sizeof(part));
    /* Without the adapter's part reader a stream is refused. */
    stream_params(45000, 3);
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_DMAREAD_STREAM, PARAM) == 0);
    service.read_part = read_part;
    stream_params(149, 3);
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_DMAREAD_STREAM, PARAM) == 0);
    stream_params(45000, 0);
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_DMAREAD_STREAM, PARAM) == 0);
    stream_params(16, 2); /* Audio sectors are not Mode 1 data. */
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_DMAREAD_STREAM, PARAM) == 0);
    /* The request checks its sectors but maps no destination; EXEC reads
     * nothing; CHECK reports STREAMING until every byte has moved. */
    stream_params(45000, 3);
    uint32_t validations = ctx.maps[KUI_RETAIL_MAP_VALIDATE];
    int32_t token = call(KUI_GD_REQUEST, KUI_RETAIL_GD_DMAREAD_STREAM, PARAM);
    CHECK(token > 0 && ctx.maps[KUI_RETAIL_MAP_VALIDATE] == validations);
    CHECK(service.diag.last_command == KUI_RETAIL_GD_DMAREAD_STREAM && service.diag.last_lba == 45000);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_RETAIL_GD_STREAMING);
    CHECK(get(STATUS + 8) == 0 && get(STATUS + 12) == 4);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && !part.calls && !ctx.reads && service.pending);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_NOP, 0) == 0);
    /* Rejected pieces change nothing: wrong token, unaligned or empty
     * sizes, more than remains, unaligned or unmapped destinations. */
    CHECK(piece(token + 1, OUTPUT, 0x800) == -1);
    CHECK(piece(token, OUTPUT, 0x810 - 8) == -1);
    CHECK(piece(token, OUTPUT, 0) == -1);
    CHECK(piece(token, OUTPUT, 3 * 2048 + 32) == -1);
    CHECK(piece(token, OUTPUT + 16, 0x800) == -1);
    ctx.deny = OUTPUT;
    CHECK(piece(token, OUTPUT, 0x800) == -1);
    ctx.deny = 0;
    CHECK(!part.calls && !service.interrupts && service.pending);
    /* Page pieces split sectors: 0xee0, then 0x800, then the last 0x120. */
    CHECK(piece(token, OUTPUT, 0xee0) == 0);
    CHECK(part.calls == 1 && part.lba == 45000 && part.skip == 0 && part.bytes == 0xee0);
    CHECK(part.sector_bytes == 2048 && service.interrupts == KUI_RETAIL_GD_IRQ_DMA_END);
    CHECK(service.diag.sectors_read == 1 && service.position_lba == 45000);
    service.interrupts = 0;
    put(STATUS, 0xffffffffu);
    CHECK(call(KUI_GD_DMA_CHECK, (uint32_t)token, STATUS) == 0 && get(STATUS) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_RETAIL_GD_STREAMING);
    CHECK(get(STATUS + 8) == 0xee0);
    CHECK(piece(token, OUTPUT + 0x1000, 0x800) == 0);
    CHECK(part.skip == 0xee0 && service.interrupts == KUI_RETAIL_GD_IRQ_DMA_END);
    CHECK(service.diag.sectors_read == 2 && service.pending);
    service.interrupts = 0;
    CHECK(piece(token, OUTPUT + 0x2000, 0x120) == 0);
    CHECK(part.skip == 0x16e0 && !service.pending && service.diag.sectors_read == 3);
    CHECK(service.interrupts == (KUI_RETAIL_GD_IRQ_DMA_END | KUI_RETAIL_GD_IRQ_DRIVE));
    CHECK(service.position_lba == 45002 && service.diag.last_destination == OUTPUT + 0x2000);
    stream_bytes(OUTPUT, 45000, 0, 0xee0, 2048);
    stream_bytes(OUTPUT + 0x1000, 45000, 0xee0, 0x800, 2048);
    stream_bytes(OUTPUT + 0x2000, 45000, 0x16e0, 0x120, 2048);
    CHECK(ram[OUTPUT - BEGIN + 0xee0] == 0xa5 && ram[OUTPUT - BEGIN + 0x2120] == 0xa5);
    CHECK(piece(token, OUTPUT, 32) == -1);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_COMPLETED);
    CHECK(get(STATUS) == 0 && get(STATUS + 8) == 3 * 2048 && get(STATUS + 12) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_NOT_FOUND);
    /* A failed piece ends the stream with an IO error and both interrupts. */
    service.interrupts = 0; part.fail = part.calls + 1;
    token = call(KUI_GD_REQUEST, KUI_RETAIL_GD_DMAREAD_STREAM, PARAM);
    CHECK(token > 0 && piece(token, OUTPUT, 0x1000) == 0);
    CHECK(!service.pending && service.error == KUI_GD_ERROR_IO);
    CHECK(service.interrupts == (KUI_RETAIL_GD_IRQ_DMA_END | KUI_RETAIL_GD_IRQ_DRIVE));
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_FAILED);
    CHECK(get(STATUS + 4) == KUI_GD_ERROR_IO && get(STATUS + 8) == 0);
    /* ABORT ends a stream; INIT forgets one. */
    service.interrupts = 0; part.fail = 0;
    token = call(KUI_GD_REQUEST, KUI_RETAIL_GD_DMAREAD_STREAM, PARAM);
    CHECK(token > 0 && piece(token, OUTPUT, 0x800) == 0);
    CHECK(call(KUI_GD_ABORT, (uint32_t)token, 0) == 0 && piece(token, OUTPUT, 0x800) == -1);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_FAILED);
    CHECK(get(STATUS + 4) == KUI_GD_ERROR_CANCELLED && get(STATUS + 8) == 0x800);
    token = call(KUI_GD_REQUEST, KUI_RETAIL_GD_DMAREAD_STREAM, PARAM);
    CHECK(token > 0 && call(KUI_GD_INIT, 0, 0) == 0 && piece(token, OUTPUT, 0x800) == -1);
    /* A large transfer moves 4 KiB per driver call: DMA_TRANSFER, then
     * DMA_CHECK or EXEC. Until it is done both interrupts are raised; CHECK
     * moves nothing. Another transfer must wait for it. */
    service.interrupts = 0; part.fail = 0;
    stream_params(45000, 15);
    token = call(KUI_GD_REQUEST, KUI_RETAIL_GD_DMAREAD_STREAM, PARAM);
    CHECK(token > 0 && piece(token, OUTPUT + 0x40, 0x5000) == 0);
    CHECK(part.skip == 0 && part.bytes == 0x1000 && service.xfer_left == 0x4000);
    CHECK(service.interrupts == (KUI_RETAIL_GD_IRQ_DMA_END | KUI_RETAIL_GD_IRQ_DRIVE));
    CHECK(piece(token, OUTPUT + 0x8000, 0x800) == -1);
    service.interrupts = 0;
    CHECK(call(KUI_GD_DMA_CHECK, (uint32_t)token, STATUS) == 1 && get(STATUS) == 0x3000);
    CHECK(part.skip == 0x1000 && part.bytes == 0x1000);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && service.xfer_left == 0x2000 && part.skip == 0x2000);
    uint32_t calls = part.calls;
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_RETAIL_GD_STREAMING);
    CHECK(part.calls == calls && get(STATUS + 8) == 0x3000);
    CHECK(call(KUI_GD_DMA_CHECK, (uint32_t)token, STATUS) == 1 && get(STATUS) == 0x1000);
    service.interrupts = 0;
    CHECK(call(KUI_GD_DMA_CHECK, (uint32_t)token, STATUS) == 0 && get(STATUS) == 0);
    CHECK(service.interrupts == KUI_RETAIL_GD_IRQ_DMA_END && service.pending);
    CHECK(call(KUI_GD_DMA_CHECK, (uint32_t)token, STATUS) == 0 && part.calls == calls + 2);
    stream_bytes(OUTPUT + 0x40, 45000, 0, 0x5000, 2048);
    /* The last transfer, with a partial final step, completes the command. */
    CHECK(piece(token, OUTPUT + 0x8000, 0x1000) == 0 && call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(service.xfer_left == 0 && service.pending);
    service.interrupts = 0;
    CHECK(piece(token, OUTPUT + 0xa000, 0x1800) == 0);
    CHECK(service.xfer_left == 0x800 && service.pending);
    service.interrupts = 0;
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && part.bytes == 0x800);
    CHECK(service.xfer_left == 0 && !service.pending);
    CHECK(service.interrupts == (KUI_RETAIL_GD_IRQ_DMA_END | KUI_RETAIL_GD_IRQ_DRIVE));
    stream_bytes(OUTPUT + 0xa000, 45000, 0x6000, 0x1800, 2048);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_COMPLETED);
    CHECK(get(STATUS + 8) == 15 * 2048);
    /* ABORT and INIT end a transfer in progress. */
    token = call(KUI_GD_REQUEST, KUI_RETAIL_GD_DMAREAD_STREAM, PARAM);
    CHECK(token > 0 && piece(token, OUTPUT, 0x3000) == 0 && service.xfer_left == 0x2000);
    CHECK(call(KUI_GD_ABORT, (uint32_t)token, 0) == 0 && !service.xfer_left);
    calls = part.calls;
    CHECK(call(KUI_GD_DMA_CHECK, (uint32_t)token, STATUS) == 0 && part.calls == calls);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_FAILED);
    token = call(KUI_GD_REQUEST, KUI_RETAIL_GD_DMAREAD_STREAM, PARAM);
    CHECK(token > 0 && piece(token, OUTPUT, 0x3000) == 0 && service.xfer_left == 0x2000);
    CHECK(call(KUI_GD_INIT, 0, 0) == 0 && !service.xfer_left);
    /* A destination refused part-way fails the command. */
    stream_params(45000, 3);
    token = call(KUI_GD_REQUEST, KUI_RETAIL_GD_DMAREAD_STREAM, PARAM);
    CHECK(token > 0 && piece(token, OUTPUT, 0x1800) == 0 && service.xfer_left == 0x800);
    ctx.deny = OUTPUT + 0x1000; service.interrupts = 0;
    CHECK(call(KUI_GD_DMA_CHECK, (uint32_t)token, STATUS) == 0 && !service.pending);
    CHECK(service.error == KUI_GD_ERROR_MEMORY);
    CHECK(service.interrupts == (KUI_RETAIL_GD_IRQ_DMA_END | KUI_RETAIL_GD_IRQ_DRIVE));
    ctx.deny = 0;
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_FAILED);
    /* Raw sectors stream too, from audio or data tracks. */
    mode(2352, 0); stream_params(16, 2);
    uint32_t sectors = service.diag.sectors_read;
    token = call(KUI_GD_REQUEST, KUI_RETAIL_GD_DMAREAD_STREAM, PARAM);
    CHECK(token > 0 && piece(token, OUTPUT, 2 * 2352) == 0);
    CHECK(part.sector_bytes == 2352 && part.lba == 16 && !service.pending);
    CHECK(service.diag.sectors_read == sectors + 2);
    stream_bytes(OUTPUT, 16, 0, 2 * 2352, 2352);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_COMPLETED);
    /* DMA_CHECK needs a writable result word. */
    ctx.deny = STATUS;
    CHECK(call(KUI_GD_DMA_CHECK, 0, STATUS) == -1);
    ctx.deny = 0;
}
/* Windows CE's PIO stream: PIO_CHECK offers up to 4 KiB, PIO_TRANSFER
 * copies it into the CPU's (here virtual) buffer and makes the registered
 * callback due, with the drive's interrupt. */
static void pio_stream_reads(void) {
    reset(); memset(&part, 0, sizeof(part)); memset(virt, 0x5a, sizeof(virt));
    service.read_part = read_part;
    stream_params(45000, 4);
    int32_t token = call(KUI_GD_REQUEST, KUI_RETAIL_GD_PIOREAD_STREAM, PARAM);
    CHECK(token > 0 && service.status == KUI_RETAIL_GD_STREAMING);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_RETAIL_GD_STREAMING);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0 && !part.calls);
    CHECK(call(KUI_GD_PIO_CALLBACK, 0x01de5724u, 0x0c3b1000u) == 0);
    CHECK(service.pio_callback == 0x01de5724u && service.pio_argument == 0x0c3b1000u);
    put(STATUS, 0xffffffffu);
    CHECK(call(KUI_GD_PIO_CHECK, (uint32_t)token, STATUS) == 0 && get(STATUS) == 0x1000);
    /* Refused: wrong token, more than offered, an unmapped destination. */
    put(PIECE, VIRT + 0x100); put(PIECE + 4, 0x1000);
    CHECK(call(KUI_GD_PIO_TRANSFER, (uint32_t)token + 1, PIECE) == -1);
    put(PIECE + 4, 0x1002);
    CHECK(call(KUI_GD_PIO_TRANSFER, (uint32_t)token, PIECE) == -1);
    put(PIECE, 0x02000000u); put(PIECE + 4, 0x1000);
    CHECK(call(KUI_GD_PIO_TRANSFER, (uint32_t)token, PIECE) == -1);
    CHECK(!part.calls && !service.callback_due);
    /* Pieces need not be sector- or 32-byte-sized. */
    put(PIECE, VIRT + 0x102); put(PIECE + 4, 0x7fe);
    service.interrupts = 0;
    CHECK(call(KUI_GD_PIO_TRANSFER, (uint32_t)token, PIECE) == 0);
    CHECK(service.callback_due && service.interrupts == KUI_RETAIL_GD_IRQ_DRIVE);
    CHECK(part.skip == 0 && part.bytes == 0x7fe && service.pending);
    for(uint32_t i = 0; i < 0x7fe; ++i) CHECK(virt[0x102 + i] == pattern(45000, i));
    CHECK(virt[0x101] == 0x5a && virt[0x102 + 0x7fe] == 0x5a);
    service.callback_due = 0;
    CHECK(call(KUI_GD_PIO_CHECK, (uint32_t)token, STATUS) == 0 && get(STATUS) == 0x1000);
    put(PIECE, VIRT + 0x2000); put(PIECE + 4, 0x1000);
    CHECK(call(KUI_GD_PIO_TRANSFER, (uint32_t)token, PIECE) == 0 && part.skip == 0x7fe);
    CHECK(call(KUI_GD_PIO_CHECK, (uint32_t)token, STATUS) == 0 && get(STATUS) == 0x802);
    put(PIECE, VIRT + 0x4000); put(PIECE + 4, 0x802);
    CHECK(call(KUI_GD_PIO_TRANSFER, (uint32_t)token, PIECE) == 0 && !service.pending);
    for(uint32_t i = 0; i < 0x802; ++i)
        CHECK(virt[0x4000 + i] == pattern(45000 + (0x17fe + i) / 2048, (0x17fe + i) % 2048));
    CHECK(service.diag.sectors_read == 4 && service.position_lba == 45003);
    /* The driver's callback then finds nothing left and removes itself. */
    CHECK(call(KUI_GD_PIO_CHECK, (uint32_t)token, STATUS) == 0 && get(STATUS) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_COMPLETED);
    CHECK(get(STATUS + 8) == 4 * 2048);
    CHECK(call(KUI_GD_PIO_CALLBACK, 0, 0) == 0 && !service.pio_callback);
    /* A failed piece ends the command; INIT forgets the callback. */
    part.fail = part.calls + 1;
    token = call(KUI_GD_REQUEST, KUI_RETAIL_GD_PIOREAD_STREAM, PARAM);
    CHECK(token > 0 && call(KUI_GD_PIO_CALLBACK, 0x01de5724u, 1) == 0);
    put(PIECE, VIRT); put(PIECE + 4, 0x800);
    CHECK(call(KUI_GD_PIO_TRANSFER, (uint32_t)token, PIECE) == 0);
    CHECK(!service.pending && service.error == KUI_GD_ERROR_IO && service.callback_due);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_FAILED);
    CHECK(call(KUI_GD_INIT, 0, 0) == 0 && !service.pio_callback && !service.callback_due);
    part.fail = 0;
}
#endif
/* GD-ROM's 99 tracks: the TOC lists each in its numbered entry, and the
 * position reports name the track holding the current sector. */
static void many_tracks(void) {
    static union kui_retail_slot many[KUI_RETAIL_IMAGE_TRACKS];
    many[0].track = (struct kui_retail_track){.start_lba=0, .end_lba=8, .control=4};
    many[1].track = (struct kui_retail_track){.start_lba=16, .end_lba=24, .control=0};
    for(uint32_t i = 2; i < KUI_RETAIL_IMAGE_TRACKS; ++i)
        many[i].track = (struct kui_retail_track){.start_lba=45000 + (i - 2) * 100,
            .end_lba=45000 + (i - 2) * 100 + 90, .control=i == 2 || i == 98 ? 4u : 0u};
    reset();
    CHECK(kui_retail_gd_init(&service, many, KUI_RETAIL_IMAGE_TRACKS + 1, &ops, BEGIN, END) == -1);
    CHECK(kui_retail_gd_init(&service, many, KUI_RETAIL_IMAGE_TRACKS, &ops, BEGIN, END) == 0);
    put(PARAM, 1); put(PARAM + 4, OUTPUT);
    int32_t token = call(KUI_GD_REQUEST, KUI_GD_GETTOC2, PARAM);
    CHECK(token > 0); CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_COMPLETED);
    CHECK(get(OUTPUT) == UINT32_MAX && get(OUTPUT + 4) == UINT32_MAX);
    for(uint32_t i = 2; i < KUI_RETAIL_IMAGE_TRACKS; ++i)
        CHECK(get(OUTPUT + i * 4u) == ((i == 2 || i == 98 ? 0x41000000u : 0x01000000u) |
                                       (45150u + (i - 2) * 100u)));
    CHECK(get(OUTPUT + 396) == 0x41030000u && get(OUTPUT + 400) == 0x41630000u);
    CHECK(get(OUTPUT + 404) == (0x41000000u | (45000u + 96u * 100u + 90u + 150u)));
    put(PARAM, 45000 + 50 * 100 + 95 + 150); /* Between two tracks. */
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_SEEK, PARAM) == 0);
    put(PARAM, 45000 + 50 * 100 + 5 + 150);
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_SEEK, PARAM) > 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, service.token, STATUS) == KUI_GD_COMPLETED);
    for(unsigned i = 0; i < 4; ++i) put(PARAM + i * 4u, OUTPUT + i * 8u);
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_REQ_STAT, PARAM) > 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(get(OUTPUT + 8) == 53 && get(OUTPUT + 16) == (0x10000000u | 50155u));
    CHECK(call(KUI_GD_CHECK, service.token, STATUS) == KUI_GD_COMPLETED);
    put(PARAM, 1); put(PARAM + 4, 14); put(PARAM + 8, OUTPUT);
    token = call(KUI_GD_REQUEST, KUI_RETAIL_GD_GETSCD, PARAM);
    CHECK(token > 0); CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_CHECK, (uint32_t)token, STATUS) == KUI_GD_COMPLETED);
    CHECK(ram[OUTPUT - BEGIN + 4] == 0x01 && ram[OUTPUT - BEGIN + 5] == 53);
    CHECK(ctx.reads == 0);
}
int main(void) {
    request_rejection_details();
    command_acknowledgment();
    prepared_initialization();
    cd_metadata();
    many_tracks();
    /* The physical cooked flag must never leak into a game's disc metadata.
     * Exercise TOC, drive position and Q subcode through normal GD calls. */
    use_cooked_tracks = 1;
    metadata();
#ifndef KUI_RETAIL_GD_ASYNC
    subcode_query();
#endif
    reset(); cooked_tracks[1].track.control |= KUI_RETAIL_TRACK_COOKED;
    CHECK(kui_retail_gd_init(&service, cooked_tracks, 3, &ops, BEGIN, END) == -1);
    use_cooked_tracks = 0;
    for(use_mapped_tracks=0;use_mapped_tracks<2;use_mapped_tracks++) {
#ifdef KUI_RETAIL_GD_ASYNC
        async_reads(); metadata(); silent_cd_audio(); version_query();
#else
        large_reads(); paced_steps(); cancel_failures(); metadata(); silent_cd_audio(); version_query(); subcode_query(); bounds_and_modes();
#endif
#ifdef KUI_RETAIL_CE
        virtual_pointers(); stream_reads(); pio_stream_reads();
#endif
    }
    printf("retail GD service: %u checks passed\n", assertions);
    return 0;
}
