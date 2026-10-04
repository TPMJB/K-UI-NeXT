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
static struct kui_retail_gd service;
static struct {
    uint32_t reads, sectors, max_count, deny, fail_at, checks, reenter;
    uint32_t maps[3], validate_address, validate_bytes, max_checked, last_count;
} ctx;
static const struct kui_gd_track tracks[] = {
    {1,4,0,8}, {2,0,16,24}, {3,4,45000,60000}
};
/* Deliberately non-GD trailing fields expose incorrect plain-array strides. */
static const struct kui_retail_track image_tracks[] = {
    {.gd={1,4,0,8}, .first_extent=91, .extent_count=11},
    {.gd={2,0,16,24}, .first_extent=92, .extent_count=12},
    {.gd={3,4,45000,60000}, .first_extent=93, .extent_count=13}
};
static unsigned use_manifest_tracks;
static unsigned assertions;
#define CHECK(x) do { ++assertions; assert(x); } while(0)
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
    /* The CE build passes other areas' (virtual) addresses on; with no MMU
     * here, the adapter refuses them as the CE reader would. */
    if((a & 0xff000000u) != 0x8c000000u) return NULL;
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
    for(uint32_t n = 0; n < count; ++n) {
        unsigned i;
        for(i = 0; i < 3; ++i)
            if(lba + n >= tracks[i].start_lba && lba + n < tracks[i].end_lba) break;
        if(i == 3 || (bytes == 2048 && !tracks[i].control)) return -1;
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
    CHECK(kui_retail_gd_init(&service, tracks, 3, &ops, BEGIN, END) == 0);
    CHECK(service.tracks == tracks && sizeof(service) < 512);
    struct kui_retail_gd expected = service;
    memset(&service, 0xa5, sizeof(service));
    kui_retail_gd_init_validated(&service, tracks, 3, &ops, BEGIN, END);
    CHECK(!memcmp(&service, &expected, sizeof(service)));
    if(use_manifest_tracks) {
        kui_retail_gd_init_manifest_validated(&service, image_tracks, 3, &ops, BEGIN, END);
        expected.image_tracks = image_tracks;
        expected.image_track_layout = 1;
        CHECK(!memcmp(&service, &expected, sizeof(service)));
    }
}
static void read_params(uint32_t lba, uint32_t count, uint32_t dest) {
    put(PARAM, lba + 150); put(PARAM + 4, count); put(PARAM + 8, dest); put(PARAM + 12, 0);
}
static void mode(uint32_t bytes, uint32_t type) {
    put(PARAM, 0); put(PARAM + 4, bytes == 2048 ? 0x2000 : 0x1000);
    put(PARAM + 8, type); put(PARAM + 12, bytes);
    CHECK(call(KUI_GD_DATATYPE, PARAM, 0) == 0);
}
#ifndef KUI_RETAIL_GD_ASYNC
static void large_reads(void) {
    reset();
    const uint32_t aliases[] = {0,0x80000000u,0xa0000000u};
    for(unsigned a = 0; a < 3; ++a) {
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
            CHECK(get(STATUS + 12) == (done == 129 ? 0u : 4u));
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
    CHECK(service.diag.sectors_read == 387 && service.diag.requests == 3);
    /* Full available RAM-sized requests have no arbitrary 64-sector ceiling. */
    uint32_t count = (END - OUTPUT) / 2048;
    read_params(45000, count, OUTPUT);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_DMAREAD, PARAM) > 0);
    CHECK(service.request_bytes == count * 2048 && ctx.reads == 195);
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
    put(PARAM, OUTPUT);
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_REQ_MODE, PARAM) > 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    for(unsigned i = 0; i < 4; ++i) CHECK(get(OUTPUT + i * 4u) == i + 10);
    put(PARAM, 45160);
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_SEEK, PARAM) > 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    for(unsigned i = 0; i < 4; ++i) put(PARAM + i * 4u, OUTPUT + i * 8u);
    CHECK(call(KUI_GD_REQUEST, KUI_RETAIL_GD_REQ_STAT, PARAM) > 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(get(OUTPUT) == 1 && get(OUTPUT + 8) == 3 && get(OUTPUT + 24) == 1);
    CHECK(get(OUTPUT + 16) == (0x14000000u | 45160u));
    CHECK(ctx.reads == 0);
    CHECK(call(KUI_GD_REQUEST, KUI_GD_STOP, 0) > 0);
    CHECK(call(KUI_GD_EXEC, 0, 0) == 0);
    CHECK(call(KUI_GD_DRIVE, STATUS, 0) == 0 && get(STATUS) == 2 && get(STATUS + 4) == 0x80);
    CHECK(call(KUI_GD_RESET, 0, 0) == 0);
    CHECK(call(KUI_GD_DRIVE, STATUS, 0) == 0 && get(STATUS) == 1);
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
    CHECK(ctx.reads == 0 && ctx.checks == 0 && service.diag.read_steps == 0);
}
static void version_query(void) {
    static const uint8_t expected[28] = "GDC Version 1.10 1999-03-31\002";
    const uint32_t destinations[] = {OUTPUT + 1, (OUTPUT + 1) & 0x1fffffffu,
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
            put(PARAM,format);put(PARAM+4,bytes);put(PARAM+8,dest&0x1fffffffu);
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
    struct kui_gd_track invalid[3]; memcpy(invalid, tracks, sizeof(invalid));
    invalid[2].end_lba = 720000;
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
#endif
int main(void) {
    for(use_manifest_tracks=0;use_manifest_tracks<2;use_manifest_tracks++) {
#ifdef KUI_RETAIL_GD_ASYNC
        async_reads(); metadata(); silent_cd_audio(); version_query();
#else
        large_reads(); paced_steps(); cancel_failures(); metadata(); silent_cd_audio(); version_query(); subcode_query(); bounds_and_modes();
#endif
#ifdef KUI_RETAIL_CE
        stream_reads();
#endif
    }
    printf("retail GD service: %u checks passed\n", assertions);
    return 0;
}
