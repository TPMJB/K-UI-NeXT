/* SPDX-License-Identifier: GPL-3.0-only */
/* Protocol fixtures come from pinned KOS's DMA buffers and Marcus Comstedt's
 * Maple tables, not another emulator. Byte order fixtures are deliberately
 * literal: the CPU's MEMCARD integer is 0x02000000, never 0x00000002. */
#include "kui/vmu_emulation.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct disk {
    uint8_t bytes[256][512];
    unsigned reads, commits;
    int read_result, commit_result;
};

static int read_block(void *context, uint16_t block, uint8_t data[512]) {
    struct disk *d = context;
    assert(block < 256);
    ++d->reads;
    if(!d->read_result) memcpy(data, d->bytes[block], 512);
    return d->read_result;
}

static int commit_block(void *context, uint16_t block, const uint8_t data[512]) {
    struct disk *d = context;
    assert(block < 256);
    ++d->commits;
    if(!d->commit_result) memcpy(d->bytes[block], data, 512);
    return d->commit_result;
}

static struct disk disk;
static kui_vmu_emulation_t state;
static uint8_t response[524];
static size_t received;
static const uint8_t info[] = {1,1,0,0};
static const uint8_t media[] = {10,1,0,2,0,0,0,2,0,0,0,0};
static const uint8_t read42[] = {11,1,0,2,0,0,0,2,0,0,0,42};
static const uint8_t sync42[] = {13,1,0,2,0,0,0,2,0,4,0,42};
static const uint8_t ack[] = {7,0,1,0};

static void start(uint8_t address) {
    const struct kui_vmu_emulation_io io = {&disk, read_block, commit_block};
    assert(kui_vmu_emulation_init(&state, address, &io) == 0);
}

static void command(const uint8_t *request, size_t size) {
    memset(response, 0xa5, sizeof(response));
    received = 999;
    assert(kui_vmu_emulation_handle(&state, request, size, response,
                                  sizeof(response), &received) == 1);
    assert(received >= 4 && received == 4u + response[3] * 4u);
    assert(response[1] == request[2] && response[2] == request[1]);
    for(size_t i = received; i < sizeof(response); ++i)
        assert(response[i] == 0xa5);
}

static void error(const uint8_t *request, size_t size, unsigned flag) {
    command(request, size);
    const uint8_t expected[] = {0xfb,0,1,1,0,0,0,(uint8_t)flag};
    assert(received == sizeof(expected) && !memcmp(response, expected, sizeof(expected)));
}

static void phase(uint8_t request[140], unsigned block, unsigned n, const uint8_t *data) {
    static const uint8_t prefix[] = {12,1,0,34,0,0,0,2,0,0,0,0};
    memcpy(request, prefix, sizeof(prefix));
    request[9] = (uint8_t)n;
    request[10] = (uint8_t)(block / 256);
    request[11] = (uint8_t)block;
    memcpy(request + 12, data, 128);
}

static void send_phase(unsigned block, unsigned n, const uint8_t *data) {
    uint8_t request[140];
    phase(request, block, n, data);
    command(request, sizeof(request));
    assert(received == 4 && !memcmp(response, ack, 4));
}

static void identity_and_media(void) {
    start(1);
    command(info, sizeof(info));
    static const uint8_t identity_prefix[] = {
        5,0,1,28, 0,0,0,2, 0,15,65,0, 0,0,0,0, 0,0,0,0, 255,0
    };
    assert(received == 116 && !memcmp(response, identity_prefix, sizeof(identity_prefix)));
    assert(!memcmp(response + 22, "K-UI Virtual Memory", 19));
    assert(response[41] == ' ' && response[111] == ' ');
    assert(response[112] == 0 && response[115] == 0);
    const uint8_t extended[] = {2,1,0,0};
    command(extended, sizeof(extended));
    assert(received == 196 && response[0] == 6 && response[3] == 48);
    assert(!memcmp(response + 116, "K-UI experimental", 17));

    /* Last physical block 255; root255; FAT254x1; directory253x13;
     * icon0; standard VMU parameters200/31; no executable capability. */
    static const uint8_t media_response[] = {
        8,0,1,7, 0,0,0,2, 255,0,0,0, 255,0,254,0,
        1,0,253,0, 13,0,0,0, 200,0,31,0, 0,0,0,0
    };
    command(media, sizeof(media));
    assert(received == sizeof(media_response));
    assert(!memcmp(response, media_response, sizeof(media_response)));
}

static void reads_and_errors(void) {
    uint8_t request[140];
    start(1);
    for(unsigned block = 0; block < 256; ++block) {
        for(unsigned i = 0; i < 512; ++i)
            disk.bytes[block][i] = (uint8_t)(block + i * 7);
        memcpy(request, read42, sizeof(read42));
        request[11] = (uint8_t)block;
        command(request, 12);
        assert(received == 524 && response[0] == 8 && response[3] == 130);
        assert(!memcmp(response + 4, request + 4, 8));
        assert(!memcmp(response + 12, disk.bytes[block], 512));
    }
    memcpy(request, read42, 12);
    request[8] = 1; error(request, 12, 1);
    request[8] = 0; request[9] = 1; error(request, 12, 2);
    request[9] = 0; request[10] = 1; error(request, 12, 4);
    request[10] = 255; request[11] = 255; error(request, 12, 4);
    memcpy(request, read42, 12); request[4] = 2; request[7] = 0;
    command(request, 12);
    assert(response[0] == 0xfe && received == 4); /* wrong CPU endianness */
    memcpy(request, read42, 12); request[7] = 4;
    command(request, 12); assert(response[0] == 0xfe); /* LCD unsupported */
    memcpy(request, media, 12); request[9] = 1; error(request, 12, 2);
    memcpy(request, media, 12); request[11] = 1; error(request, 12, 4);
    for(int status = -2; status <= 2; ++status) {
        disk.read_result = status;
        command(read42, sizeof(read42));
        assert(response[0] == (status == 0 ? 8 : status == 1 ? 0xfc : 0xfb));
    }
    disk.read_result = 0;
    command(read42, sizeof(read42));
    const uint8_t read_status[] = {13,1,0,2,0,0,0,2,0,1,0,42};
    command(read_status, sizeof(read_status)); assert(!memcmp(response, ack, 4));
    const uint8_t unsupported[] = {9,1,0,1,0,0,0,8};
    command(unsupported, sizeof(unsupported)); assert(response[0] == 0xfe);
    const uint8_t storage_condition[] = {9,1,0,1,0,0,0,2};
    command(storage_condition, sizeof(storage_condition)); assert(response[0] == 0xfd);
    const uint8_t unknown[] = {0x21,1,0,0};
    command(unknown, sizeof(unknown)); assert(response[0] == 0xfd);
}

static void transactions(void) {
    uint8_t data[512], old[512], request[140];
    for(unsigned i = 0; i < 512; ++i) data[i] = (uint8_t)(i * 13 + i / 128);
    memcpy(old, disk.bytes[42], 512);
    start(1);
    unsigned initial = disk.commits;
    error(sync42, sizeof(sync42), 2);
    phase(request, 42, 1, data + 128); error(request, 140, 2);
    phase(request, 42, 4, data); error(request, 140, 2);
    send_phase(42, 0, data);
    phase(request, 42, 2, data + 256); error(request, 140, 2);
    phase(request, 43, 1, data + 128); error(request, 140, 2);
    error(sync42, sizeof(sync42), 2);
    send_phase(42, 1, data + 128);
    send_phase(42, 0, data); /* identical old phase0 must preserve phase1 */
    phase(request, 42, 1, data + 128); request[12] ^= 1; error(request, 140, 2);
    send_phase(42, 2, data + 256);
    send_phase(42, 2, data + 256); /* exact duplicate safe */
    send_phase(42, 3, data + 384);
    assert(disk.commits == initial && !memcmp(disk.bytes[42], old, 512));
    command(read42, sizeof(read42)); assert(!memcmp(response + 12, old, 512));

    /* Both I/O failures and a pending durable write forbid a successful
     * final ACK, preserve the immutable transaction, and permit retry. */
    disk.commit_result = -1;
    error(sync42, sizeof(sync42), 8);
    assert(disk.commits == initial + 1 && !memcmp(disk.bytes[42], old, 512));
    disk.commit_result = 1;
    command(sync42, sizeof(sync42)); assert(response[0] == 0xfc && received == 4);
    assert(disk.commits == initial + 2 && !memcmp(disk.bytes[42], old, 512));
    phase(request, 42, 0, data); request[12] ^= 0xff;
    command(request, sizeof(request)); assert(response[0] == 0xfc);
    for(unsigned p = 0; p < 4; ++p)
        send_phase(42, p, data + p * 128); /* whole-block retry still works */
    uint8_t wrong_sync[12]; memcpy(wrong_sync, sync42, 12);
    wrong_sync[11] = 43; error(wrong_sync, 12, 2);
    wrong_sync[11] = 42; wrong_sync[9] = 0; error(wrong_sync, 12, 2);
    disk.commit_result = 0;
    command(sync42, sizeof(sync42)); assert(!memcmp(response, ack, 4));
    assert(disk.commits == initial + 3 && !memcmp(disk.bytes[42], data, 512));
    command(sync42, sizeof(sync42)); assert(!memcmp(response, ack, 4));
    assert(disk.commits == initial + 3); /* lost ACK doesn't reprogram storage */
    command(read42, sizeof(read42)); assert(!memcmp(response + 12, data, 512));
    kui_vmu_emulation_reset(&state);
    error(sync42, sizeof(sync42), 2);
    command(read42, sizeof(read42)); assert(!memcmp(response + 12, data, 512));

    /* New phase zero cancels a stale transaction instead of mixing blocks. */
    send_phase(41, 0, data);
    send_phase(41, 1, data + 128);
    send_phase(42, 0, data);
    phase(request, 42, 2, data + 256); error(request, 140, 2);
    send_phase(42, 1, data + 128);
    data[0] ^= 1; send_phase(42, 0, data); /* same block, changed phase0 */
    phase(request, 42, 2, data + 256); error(request, 140, 2);
    send_phase(42, 1, data + 128);
    const uint8_t reset[] = {3,1,0,0};
    command(reset, sizeof(reset)); assert(!memcmp(response, ack, 4));
    error(sync42, sizeof(sync42), 2);
    phase(request, 42, 2, data + 256); error(request, 140, 2);
    command(read42, sizeof(read42)); assert(memcmp(response + 12, data, 512));

    for(unsigned block = 0; block < 256; ++block) {
        for(unsigned i = 0; i < 512; ++i) data[i] = (uint8_t)(block ^ i ^ (i >> 8));
        for(unsigned p = 0; p < 4; ++p) send_phase(block, p, data + p * 128);
        memcpy(wrong_sync, sync42, 12); wrong_sync[11] = (uint8_t)block;
        command(wrong_sync, 12); assert(!memcmp(response, ack, 4));
        assert(!memcmp(disk.bytes[block], data, 512));
    }
    /* KILL acknowledges, then hardware timeout words continue until a
     * hardware/API reset. A Maple device-reset command is also ignored. */
    const uint8_t kill[] = {4,1,0,0};
    command(kill, sizeof(kill)); assert(!memcmp(response, ack, 4));
    const uint8_t *ignored[] = {info, read42, sync42, reset};
    const size_t ignored_length[] = {4,12,12,4};
    for(unsigned i = 0; i < 4; ++i) {
        received = 0;
        assert(kui_vmu_emulation_handle(&state, ignored[i], ignored_length[i],
                                      response, sizeof(response), &received) == 1);
        assert(received == 4 && !memcmp(response, "\xff\xff\xff\xff", 4));
    }
    kui_vmu_emulation_reset(&state);
    command(info, sizeof(info)); assert(response[0] == 5);
}

static void bounds_and_addressing(void) {
    uint8_t request[1028], out[526], data[128] = {0};
    kui_vmu_emulation_t snapshot;
    start(1);
    for(unsigned address = 0; address < 256; ++address) {
        memcpy(request, info, 4); request[1] = (uint8_t)address;
        size_t n = 10;
        int r = kui_vmu_emulation_handle(&state, request, 4, out, sizeof(out), &n);
        assert(r == (address == 1 ? 1 : 0));
        if(!r) assert(n == 0);
    }
    for(unsigned port = 0; port < 4; ++port) {
        for(unsigned unit = 1; unit <= 16; unit *= 2) {
            start((uint8_t)(port * 64 + unit));
            uint8_t probe[] = {1,(uint8_t)(port * 64 + unit),(uint8_t)(port * 64),0};
            command(probe, sizeof(probe)); assert(response[0] == 5);
            probe[2] ^= 1; size_t n = 10;
            assert(!kui_vmu_emulation_handle(&state, probe, 4, out, sizeof(out), &n));
            assert(n == 0);
        }
    }
    start(1);
    const struct kui_vmu_emulation_io io = {&disk, read_block, commit_block};
    for(unsigned address = 0; address < 256; ++address) {
        unsigned low = address % 64;
        int valid = low && low <= 16 && !(low & (low - 1));
        assert((kui_vmu_emulation_init(&snapshot, (uint8_t)address, &io) == 0) == valid);
    }
    /* Every output-capacity failure leaves both state and I/O untouched. */
    const uint8_t *requests[] = {info, media, read42, sync42};
    const size_t lengths[] = {4,12,12,12}, capacities[] = {116,32,524,8};
    for(unsigned k = 0; k < 4; ++k) {
        for(size_t cap = 0; cap < capacities[k]; ++cap) {
            snapshot = state; unsigned reads = disk.reads, commits = disk.commits;
            size_t n = 11; memset(out, 0xcd, sizeof(out));
            assert(kui_vmu_emulation_handle(&state, requests[k], lengths[k], out + 1, cap, &n) == -2);
            assert(n == 0 && !memcmp(&snapshot, &state, sizeof(state)));
            assert(disk.reads == reads && disk.commits == commits);
            for(unsigned i = 0; i < sizeof(out); ++i) assert(out[i] == 0xcd);
        }
    }
    phase(request, 42, 0, data); snapshot = state;
    received = 11;
    assert(kui_vmu_emulation_handle(&state, request, 140, out, 7, &received) == -2);
    assert(received == 0 && !memcmp(&snapshot, &state, sizeof(state)));

    /* Exact allocations expose any accidental read beyond a truncated frame
     * to ASan. Complete-header malformed lengths produce FILEERR/length. */
    for(unsigned n = 0; n < 1028; ++n) {
        uint8_t *short_request = malloc(n ? n : 1);
        assert(short_request);
        memset(short_request, 0, n);
        if(n > 0) short_request[0] = 12;
        if(n > 1) short_request[1] = 1;
        if(n > 3) short_request[3] = 34;
        size_t size = 55; unsigned commits = disk.commits;
        int r = kui_vmu_emulation_handle(&state, short_request, n, out, sizeof(out), &size);
        if(n < 4) assert(r == -1 && size == 0);
        else if(n == 140) assert(r == 1 && out[0] == 0xfe);
        else assert(r == 1 && out[0] == 0xfb && size == 8 && out[7] == 16);
        assert(disk.commits == commits);
        free(short_request);
    }
    const uint8_t malformed_info[] = {1,1,0,1,0,0,0,0};
    error(malformed_info, sizeof(malformed_info), 16);
    assert(kui_vmu_emulation_handle(NULL, info, 4, out, sizeof(out), &received) == -1);
    assert(received == 0);
    assert(kui_vmu_emulation_handle(&state, NULL, 4, out, sizeof(out), &received) == -1);
    assert(kui_vmu_emulation_handle(&state, info, 4, NULL, sizeof(out), &received) == -1);
    assert(kui_vmu_emulation_handle(&state, info, 4, out, sizeof(out), NULL) == -1);
}

int main(void) {
    identity_and_media();
    reads_and_errors();
    transactions();
    bounds_and_addressing();
    puts("PASS VMU Maple protocol: byte fixtures, durable commits, retries, bounds, addressing");
    return 0;
}
