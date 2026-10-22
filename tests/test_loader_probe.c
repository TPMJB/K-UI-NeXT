/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/loader_probe.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t card[512 * 512], fixture[KUI_LOADER_PROBE_FILE_BYTES];
static struct kui_loader_probe_service service;
static uint32_t calls, fail_call, reports, report_failures;
static int read_block(void *context, uint32_t lba, uint8_t out[512]) {
    assert(context == card && lba < 512);
    ++calls;
    if(fail_call && calls == fail_call) return -1;
    memcpy(out, card + lba * 512, 512);
    return 0;
}
static enum kui_loader_probe_result submit(
    const struct kui_loader_probe_request *r, void *out, uint32_t cap, uint32_t *t) {
    return kui_loader_probe_submit(&service, r, out, cap, t);
}
static enum kui_loader_probe_result poll(uint32_t t, uint32_t *bytes) {
    return kui_loader_probe_poll(&service, t, bytes);
}
static enum kui_loader_probe_result cancel(uint32_t t) {
    return kui_loader_probe_cancel(&service, t);
}
static uint32_t read_count(void) { return service.blocks_read; }
static void report(const char *label, uint32_t pass, uint32_t detail) {
    assert(label && *label); (void)detail;
    ++reports; if(!pass) ++report_failures;
}
static const struct kui_loader_probe_api api = {
    KUI_LOADER_PROBE_VERSION, sizeof(struct kui_loader_probe_api),
    submit, poll, cancel, report, read_count
};
static struct kui_loader_probe_manifest make_manifest(unsigned fragmented) {
    struct kui_loader_probe_manifest m = {0};
    m.card_sectors = 512;
    if(fragmented) {
        m.extent_count = 111;
        for(uint32_t i = 0; i < 111; ++i)
            m.extents[i] = (struct kui_loader_probe_extent){i, 500 - i * 3, 1};
    } else {
        m.extent_count = 1;
        m.extents[0] = (struct kui_loader_probe_extent){0, 100, 111};
    }
    memset(card, 0x31, sizeof(card));
    for(uint32_t i = 0; i < m.extent_count; ++i)
        memcpy(card + m.extents[i].card_lba * 512,
               fixture + m.extents[i].file_block * 512, m.extents[i].blocks * 512);
    return m;
}
static void manifest_tests(void) {
    struct kui_loader_probe_manifest m = make_manifest(0), decoded, before;
    uint8_t wire[1600], clean[1600];
    assert(kui_loader_probe_manifest_encode(&m, wire) == KUI_LP_OK);
    assert(!memcmp(wire, "KUIG3P01", 8));
    assert(wire[8] == 1 && wire[12] == 0x40 && wire[13] == 6);
    assert(wire[24] == 0 && wire[25] == 2 && wire[32] == 1);
    assert(wire[68] == 100 && wire[72] == 111);
    memset(&decoded, 0xa7, sizeof(decoded));
    assert(kui_loader_probe_manifest_decode(wire, &decoded) == KUI_LP_OK);
    assert(!memcmp(&m, &decoded, sizeof(m)));
    memcpy(clean, wire, sizeof(wire));
    before = decoded;
    for(unsigned i = 0; i < sizeof(wire); ++i) {
        wire[i] ^= 1;
        assert(kui_loader_probe_manifest_decode(wire, &decoded) != KUI_LP_OK);
        assert(!memcmp(&before, &decoded, sizeof(decoded)));
        wire[i] ^= 1;
    }
    assert(!memcmp(clean, wire, sizeof(wire)));
    m.extents[0].blocks = 110;
    assert(kui_loader_probe_manifest_encode(&m, wire) == KUI_LP_RANGE);
    assert(!memcmp(clean, wire, sizeof(wire)));
    m = make_manifest(1);
    assert(kui_loader_probe_manifest_encode(&m, wire) == KUI_LP_OK);
    assert(kui_loader_probe_manifest_decode(wire, &decoded) == KUI_LP_OK);
    assert(!memcmp(&m, &decoded, sizeof(m)));
    m.extents[110].card_lba = m.extents[0].card_lba;
    assert(kui_loader_probe_manifest_encode(&m, wire) == KUI_LP_RANGE);
    m = make_manifest(1); m.extents[2].file_block = 1;
    assert(kui_loader_probe_manifest_encode(&m, wire) == KUI_LP_RANGE);
    m = make_manifest(0); m.extents[0].card_lba = 502;
    assert(kui_loader_probe_manifest_encode(&m, wire) == KUI_LP_RANGE);
    m = make_manifest(0); m.extents[0].blocks = UINT32_MAX;
    assert(kui_loader_probe_manifest_encode(&m, wire) == KUI_LP_RANGE);
    m = make_manifest(0); m.extent_count = 129;
    assert(kui_loader_probe_manifest_encode(&m, wire) == KUI_LP_INVALID);
    m = make_manifest(0); m.card_sectors = 0;
    assert(kui_loader_probe_manifest_encode(&m, wire) == KUI_LP_INVALID);
}
static void interface_tests(unsigned fragmented) {
    struct kui_loader_probe_manifest m = make_manifest(fragmented);
    calls = fail_call = reports = report_failures = 0;
    assert(kui_loader_probe_init(&service, &m, read_block, card) == KUI_LP_OK);
    assert(kui_probe_client_main(&api) == 0);
    assert(reports == 10 && report_failures == 0 && calls > 0);
    /* Integrity checks must actually detect altered card bytes, even though
     * the transport returned success and the Mode 1 header remains valid. */
    uint32_t altered = m.extents[0].card_lba * 512 + 47;
    card[altered] ^= 0x80;
    assert(kui_probe_client_main(&api) != 0 && report_failures != 0);
    card[altered] ^= 0x80;
    assert(kui_probe_client_main(&api) == 0);
    uint8_t output[9408]; memset(output, 0xa9, sizeof(output));
    struct kui_loader_probe_request r = {KUI_LP_READ, 0, 1, KUI_LP_MODE1};
    uint32_t t = 0, bytes = 99, before = calls;
    assert(submit(&r, output, 2047, &t) == KUI_LP_RANGE);
    assert(t == 0 && calls == before && output[0] == 0xa9);
    r.count = 0;
    assert(submit(&r, output, sizeof(output), &t) == KUI_LP_RANGE);
    r.count = 4; r.lba = UINT32_MAX;
    assert(submit(&r, output, sizeof(output), &t) == KUI_LP_RANGE);
    r.count = 1; r.lba = 0;
    assert(submit(&r, output, sizeof(output), &t) == KUI_LP_OK);
    fail_call = calls + 1;
    assert(poll(t, &bytes) == KUI_LP_IO && bytes == 0 && calls == fail_call);
    assert(poll(t, &bytes) == KUI_LP_IO && calls == fail_call);
    fail_call = 0;
    assert(submit(&r, output, sizeof(output), &t) == KUI_LP_OK);
    assert(poll(t, &bytes) == KUI_LP_OK && bytes == 2048);
    assert(!memcmp(output, fixture + 16, 2048));
    uint32_t first = m.extents[0].card_lba * 512;
    card[first + 15] = 2;
    assert(submit(&r, output, sizeof(output), &t) == KUI_LP_OK);
    assert(poll(t, &bytes) == KUI_LP_FORMAT && bytes == 0);
    r.format = KUI_LP_RAW;
    assert(submit(&r, output, sizeof(output), &t) == KUI_LP_OK);
    assert(poll(t, &bytes) == KUI_LP_OK && bytes == 2352 && output[15] == 2);
    card[first + 15] = 1;
    /* Later block failure is not accepted as a successful short read. */
    r.lba = 45000; r.count = 4; r.format = KUI_LP_MODE1;
    assert(submit(&r, output, sizeof(output), &t) == KUI_LP_OK);
    fail_call = calls + 6;
    assert(poll(t, &bytes) == KUI_LP_IO && bytes == 0);
    fail_call = 0;
    service.token = UINT32_MAX;
    assert(submit(&r, output, sizeof(output), &t) == KUI_LP_OK && t == 1);
    before = calls;
    assert(cancel(t) == KUI_LP_CANCELLED && calls == before);
    assert(cancel(t) == KUI_LP_CANCELLED && poll(t, &bytes) == KUI_LP_CANCELLED);
    assert(bytes == 0 && calls == before);
    r.opcode = KUI_LP_STATUS; r.lba = 1; r.count = 0; r.format = 0;
    assert(submit(&r, output, sizeof(output), &t) == KUI_LP_INVALID);
}
int main(int argc, char **argv) {
    for(uint32_t i = 0; i < sizeof(fixture); ++i)
        fixture[i] = kui_loader_probe_fixture_byte(i);
    if(argc == 2) {
        FILE *f = fopen(argv[1], "rb"); assert(f);
        for(uint32_t i = 0; i < sizeof(fixture); ++i) assert(fgetc(f) == fixture[i]);
        assert(fgetc(f) == EOF && !ferror(f)); assert(fclose(f) == 0);
    }
    assert(kui_loader_probe_fixture_byte(UINT32_MAX) == 0);
    manifest_tests(); interface_tests(0); interface_tests(1);
    struct kui_loader_probe_api wrong = api; wrong.version = 99;
    assert(kui_probe_client_main(&wrong) != 0);
    assert(kui_probe_client_main(NULL) != 0);
    puts("loader probe: manifest integrity, fragmented maps, ABI lifecycle, original fixture and client checks pass");
    return 0;
}
