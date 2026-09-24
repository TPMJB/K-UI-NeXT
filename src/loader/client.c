/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/loader_probe.h"

/* This file is linked into the low-RAM test executable, independently of the
 * high-RAM resident service. Never call KOS or the service's C entry points. */
static uint8_t output[4 * 2352];
static uint32_t failures;
static const struct kui_loader_probe_api *exports;

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void result(const char *name, uint32_t pass, uint32_t detail) {
    if(!pass) ++failures;
    exports->report(name, pass, detail);
}
static uint8_t expected(uint32_t lba, uint32_t offset) {
    uint32_t index = lba < 12 ? lba : lba - 45000 + 12;
    if(index < 8 || index >= 12) {
        if(offset == 0 || offset == 11) return 0;
        if(offset < 11) return 255;
        if(offset == 15) return 1;
        if(offset >= 12 && offset <= 14) {
            uint32_t f = lba + 150;
            uint32_t n = offset == 12 ? f / 4500 :
                         offset == 13 ? f / 75 % 60 : f % 75;
            return (uint8_t)(n / 10 * 16 + n % 10);
        }
    }
    return (uint8_t)((index * 37) ^ (offset * 13) ^ (offset >> 8) ^ 0xa5);
}
static uint32_t request(uint32_t opcode, uint32_t lba, uint32_t count,
                        uint32_t format, uint32_t *bytes) {
    struct kui_loader_probe_request r = {opcode, lba, count, format};
    uint32_t token = 0, before = exports->read_count();
    enum kui_loader_probe_result code = exports->submit(&r, output,
                                                       sizeof(output), &token);
    if(code != KUI_LP_OK || !token || exports->read_count() != before) return 0;
    if(exports->poll(token, bytes) != KUI_LP_OK) return 0;
    before = exports->read_count();
    uint32_t again = 0;
    return exports->poll(token, &again) == KUI_LP_OK && again == *bytes &&
           before == exports->read_count();
}
static uint32_t read_matches(uint32_t lba, uint32_t count, uint32_t format) {
    uint32_t bytes = 0, before = exports->read_count();
    uint32_t per = format == KUI_LP_RAW ? 2352 : 2048;
    uint32_t offset = format == KUI_LP_RAW ? 0 : 16;
    if(!request(KUI_LP_READ, lba, count, format, &bytes) || bytes != count * per ||
       exports->read_count() <= before) return 0;
    for(uint32_t i = 0; i < count; ++i)
        for(uint32_t p = 0; p < per; ++p)
            if(output[i * per + p] != expected(lba + i, p + offset)) return 0;
    return 1;
}
static uint32_t rejected(uint32_t lba, uint32_t count, uint32_t format,
                         enum kui_loader_probe_result wanted) {
    struct kui_loader_probe_request r = {KUI_LP_READ, lba, count, format};
    uint32_t token = 0x55aa, before = exports->read_count();
    for(uint32_t i = 0; i < sizeof(output); ++i) output[i] = 0x69;
    if(exports->submit(&r, output, sizeof(output), &token) != wanted ||
       token != 0x55aa || before != exports->read_count()) return 0;
    for(uint32_t i = 0; i < sizeof(output); ++i)
        if(output[i] != 0x69) return 0;
    return 1;
}
static void read_result(const char *name, uint32_t pass) {
    /* Evaluate the read before fetching its counter: C argument evaluation
     * order must not make the displayed diagnostic one request out of date. */
    result(name, pass, exports->read_count());
}
uint32_t kui_probe_client_main(const struct kui_loader_probe_api *api) {
    failures = 0;
    if(!api || api->version != KUI_LOADER_PROBE_VERSION ||
       api->bytes != sizeof(*api) || !api->submit || !api->poll ||
       !api->cancel || !api->report || !api->read_count) return 1;
    exports = api;
    uint32_t bytes = 0, before = api->read_count();
    uint32_t pass = request(KUI_LP_STATUS, 0, 0, 0, &bytes) && bytes == 16 &&
        get32(output) == 1 && get32(output + 4) == 1 &&
        get32(output + 8) == 3 && get32(output + 12) == 4 &&
        api->read_count() == before;
    result("Status ABI", pass, bytes);
    static const uint32_t toc[] = {1, 4, 0, 8, 2, 0, 8, 12, 3, 4, 45000, 45012};
    pass = request(KUI_LP_TOC, 0, 0, 0, &bytes) && bytes == 48;
    for(uint32_t i = 0; i < 12; ++i)
        if(get32(output + i * 4) != toc[i]) pass = 0;
    result("Synthetic TOC", pass && api->read_count() == before, bytes);
    read_result("Sequential data reads", read_matches(0, 4, KUI_LP_MODE1) &&
                read_matches(45000, 4, KUI_LP_MODE1));
    read_result("Random data reads", read_matches(45011, 1, KUI_LP_MODE1) &&
                read_matches(2, 1, KUI_LP_MODE1));
    read_result("Raw audio read", read_matches(8, 4, KUI_LP_RAW));
    read_result("Raw track boundary", read_matches(7, 2, KUI_LP_RAW));
    pass = rejected(7, 2, KUI_LP_MODE1, KUI_LP_AUDIO) &&
           rejected(11, 2, KUI_LP_RAW, KUI_LP_GAP) &&
           rejected(45011, 2, KUI_LP_RAW, KUI_LP_RANGE) &&
           rejected(0, 5, KUI_LP_RAW, KUI_LP_RANGE) &&
           rejected(0, 1, 99, KUI_LP_UNSUPPORTED);
    struct kui_loader_probe_request unsupported = {99, 0, 0, 0};
    uint32_t token = 0;
    pass = pass && api->submit(&unsupported, output, sizeof(output), &token) ==
                   KUI_LP_UNSUPPORTED;
    result("Rejected requests", pass, 6);
    struct kui_loader_probe_request r = {KUI_LP_READ, 45002, 1, KUI_LP_MODE1};
    before = api->read_count();
    pass = api->submit(&r, output, sizeof(output), &token) == KUI_LP_OK;
    uint32_t duplicate = 0;
    pass = pass && api->submit(&r, output, sizeof(output), &duplicate) == KUI_LP_BUSY;
    pass = pass && api->poll(token + 1, &bytes) == KUI_LP_INVALID;
    pass = pass && api->cancel(token) == KUI_LP_CANCELLED &&
           api->poll(token, &bytes) == KUI_LP_CANCELLED && bytes == 0 &&
           api->read_count() == before;
    result("Cancel before SD read", pass, api->read_count() - before);
    read_result("Read after cancel", read_matches(45002, 1, KUI_LP_MODE1));
    result("Physical SD reads", api->read_count() > 0, api->read_count());
    return failures;
}
