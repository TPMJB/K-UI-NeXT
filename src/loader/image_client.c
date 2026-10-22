/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/image_client.h"
#include "kui/gd_service.h"

/* This executable is linked independently at the guest address. Its only
 * resident exports are a display function and a physical-I/O counter. All
 * disc operations go through the console's real GD BIOS vector. */
static uint8_t output[KUI_IMAGE_CLIENT_MAX_SECTORS * 2352]
    __attribute__((aligned(32)));
static uint32_t toc[102] __attribute__((aligned(32)));
static uint32_t status[4], parameters[4], mode[4], drive[2];
static const struct kui_image_client_api *exports;
static uint32_t failures;

static uint32_t address(const void *pointer) {
#ifdef KUI_ON_CONSOLE
    return (uint32_t)(uintptr_t)pointer;
#else
    return kui_image_client_host_address(pointer);
#endif
}
static int32_t call(uint32_t function, uintptr_t arg0, uintptr_t arg1) {
    return kui_image_client_gd_call(function, arg0, arg1);
}
static void report(const char *label, uint32_t pass, uint32_t detail) {
    if(!pass) ++failures;
    exports->report(label, pass, detail);
}
/* Independent, small reference implementation; not the resident's CRC code. */
static uint32_t crc32(const uint8_t *data, uint32_t bytes) {
    uint32_t value = UINT32_MAX;
    while(bytes--) {
        value ^= *data++;
        for(uint32_t bit = 0; bit < 8; ++bit)
            value = value >> 1 ^ (0xedb88320u & (0u - (value & 1u)));
    }
    return ~value;
}
static uint32_t valid_api(const struct kui_image_client_api *api) {
    if(!api || api->version != KUI_IMAGE_CLIENT_VERSION ||
       api->bytes != sizeof(*api) || !api->report || !api->read_count ||
       !api->tracks || !api->samples || !api->track_count ||
       api->track_count > KUI_IMAGE_CLIENT_TRACKS || !api->sample_count ||
       api->sample_count > KUI_IMAGE_CLIENT_SAMPLES) return 0;
    for(uint32_t i = 0; i < api->track_count; ++i) {
        const struct kui_image_client_track *t = api->tracks + i;
        if(t->number != i + 1 || (t->control != 0 && t->control != 4) ||
           t->end_lba <= t->start_lba || t->end_lba > 719850u ||
           (i && t->start_lba < api->tracks[i - 1].end_lba)) return 0;
    }
    for(uint32_t i = 0; i < api->sample_count; ++i) {
        const struct kui_image_client_sample *s = api->samples + i;
        if(!s->count || s->count > KUI_IMAGE_CLIENT_MAX_SECTORS ||
           (s->sector_bytes != 2048 && s->sector_bytes != 2352) ||
           s->lba >= 719850u || s->count > 719850u - s->lba) return 0;
    }
    return 1;
}
static uint32_t query_mode(uint32_t bytes) {
    mode[0] = 1; mode[1] = mode[2] = mode[3] = 0;
    return call(KUI_GD_DATATYPE, (uintptr_t)mode, 0) == 0 &&
        mode[1] == (bytes == 2352 ? 0x1000u : 0x2000u) &&
        mode[2] == (bytes == 2352 ? 0u : 1024u) && mode[3] == bytes;
}
static uint32_t set_mode(uint32_t bytes) {
    uint32_t before = exports->read_count();
    mode[0] = 0;
    mode[1] = bytes == 2352 ? 0x1000u : 0x2000u;
    mode[2] = bytes == 2352 ? 0u : 1024u;
    mode[3] = bytes;
    return call(KUI_GD_DATATYPE, (uintptr_t)mode, 0) == 0 &&
        query_mode(bytes) && exports->read_count() == before;
}
static uint32_t completed(int32_t token, uint32_t bytes) {
    if(token <= 0) return 0;
    uint32_t before = exports->read_count();
    if(call(KUI_GD_CHECK, (uintptr_t)token, (uintptr_t)status) !=
       KUI_GD_PROCESSING || exports->read_count() != before) return 0;
    for(uint32_t step = 0; step < KUI_IMAGE_CLIENT_SERVER_STEPS; ++step) {
        if(call(KUI_GD_EXEC, 0, 0) != 0) return 0;
        before = exports->read_count();
        int32_t value = call(KUI_GD_CHECK, (uintptr_t)token, (uintptr_t)status);
        if(exports->read_count() != before) return 0;
        if(value == KUI_GD_PROCESSING) continue;
        if(value != KUI_GD_COMPLETED || status[0] || status[1] ||
           status[2] != bytes || status[3]) return 0;
        /* Polling a completed command must be stable and must not reread SD. */
        return call(KUI_GD_CHECK, (uintptr_t)token, (uintptr_t)status) ==
            KUI_GD_COMPLETED && status[0] == 0 && status[1] == 0 &&
            status[2] == bytes && status[3] == 0 &&
            exports->read_count() == before;
    }
    /* A stuck server must produce a failed probe, not an endless screen. */
    (void)call(KUI_GD_ABORT, (uintptr_t)token, 0);
    return 0;
}
static uint32_t run(uint32_t command, uint32_t bytes) {
    uint32_t before = exports->read_count();
    int32_t token = call(KUI_GD_REQUEST, command, (uintptr_t)parameters);
    return exports->read_count() == before && completed(token, bytes);
}
static uint32_t read_sample(uint32_t index, uint32_t command) {
    const struct kui_image_client_sample *s = exports->samples + index;
    if(!set_mode(s->sector_bytes)) return 0;
    for(uint32_t i = 0; i < sizeof(output); ++i) output[i] = 0xa5;
    parameters[0] = s->lba + KUI_GD_FAD_OFFSET;
    parameters[1] = s->count;
    parameters[2] = address(output);
    parameters[3] = 0;
    uint32_t before = exports->read_count();
    uint32_t bytes = s->count * s->sector_bytes;
    if(!run(command, bytes) || exports->read_count() <= before ||
       crc32(output, bytes) != s->crc32) return 0;
    for(uint32_t i = bytes; i < sizeof(output); ++i)
        if(output[i] != 0xa5) return 0;
    return 1;
}
static uint32_t toc_matches(uint32_t area) {
    uint32_t first = UINT32_MAX, last = 0;
    for(uint32_t i = 0; i < exports->track_count; ++i) {
        const struct kui_image_client_track *t = exports->tracks + i;
        if((t->start_lba >= 45000u) == (area != 0)) {
            if(first == UINT32_MAX) first = i;
            last = i;
        }
    }
    if(first == UINT32_MAX) return 1; /* No such session in this image. */
    parameters[0] = area; parameters[1] = address(toc);
    parameters[2] = parameters[3] = 0;
    uint32_t before = exports->read_count();
    if(!run(KUI_GD_GETTOC2, sizeof(toc)) ||
       exports->read_count() != before) return 0;
    for(uint32_t i = 0; i < 99; ++i) {
        uint32_t wanted = UINT32_MAX;
        if(i >= first && i <= last) {
            const struct kui_image_client_track *t = exports->tracks + i;
            wanted = t->control << 28 | 1u << 24 |
                     (t->start_lba + KUI_GD_FAD_OFFSET);
        }
        if(toc[i] != wanted) return 0;
    }
    const struct kui_image_client_track *a = exports->tracks + first;
    const struct kui_image_client_track *b = exports->tracks + last;
    return toc[99] == (a->control << 28 | 1u << 24 | a->number << 16) &&
        toc[100] == (b->control << 28 | 1u << 24 | b->number << 16) &&
        toc[101] == (b->control << 28 | 1u << 24 |
                     (b->end_lba + KUI_GD_FAD_OFFSET));
}
static uint32_t rejected_read(uint32_t fad, uint32_t count, uint32_t target) {
    uint32_t before = exports->read_count();
    parameters[0] = fad; parameters[1] = count;
    parameters[2] = target; parameters[3] = 0;
    return call(KUI_GD_REQUEST, KUI_GD_PIOREAD,
                (uintptr_t)parameters) == 0 && exports->read_count() == before;
}
static uint32_t rejected_requests(void) {
    uint32_t before = exports->read_count();
    const struct kui_image_client_sample *s = exports->samples;
    if(!set_mode(s->sector_bytes)) return 0;
    for(uint32_t i = 0; i < sizeof(output); ++i) output[i] = 0x69;
    uint32_t end = exports->tracks[exports->track_count - 1].end_lba + 150u;
    if(call(KUI_GD_REQUEST, UINT32_MAX, (uintptr_t)parameters) != 0 ||
       !rejected_read(149, 1, address(output)) ||
       !rejected_read(s->lba + 150u, 0, address(output)) ||
       !rejected_read(s->lba + 150u, KUI_GD_MAX_READ_SECTORS + 1,
                      address(output)) ||
       !rejected_read(end, 1, address(output)) ||
       !rejected_read(s->lba + 150u, 1, 0) ||
       !rejected_read(s->lba + 150u, 1, address(output) + 1u)) return 0;
    mode[0] = 0; mode[1] = 0x1000; mode[2] = 0; mode[3] = 999;
    if(call(KUI_GD_DATATYPE, (uintptr_t)mode, 0) != -1 ||
       !query_mode(s->sector_bytes) ||
       call(KUI_GD_DMA_CALLBACK, 0, 0) != -1 ||
       call(KUI_GD_PIO_TRANSFER, 0, 0) != -1 ||
       call(99, 0, 0) != -1 || exports->read_count() != before) return 0;
    for(uint32_t i = 0; i < sizeof(output); ++i)
        if(output[i] != 0x69) return 0;
    return 1;
}
static uint32_t cancel_pending(void) {
    const struct kui_image_client_sample *s = exports->samples;
    if(!set_mode(s->sector_bytes)) return 0;
    parameters[0] = s->lba + 150u; parameters[1] = s->count;
    parameters[2] = address(output); parameters[3] = 0;
    uint32_t before = exports->read_count();
    int32_t token = call(KUI_GD_REQUEST, KUI_GD_PIOREAD, (uintptr_t)parameters);
    if(token <= 0 || call(KUI_GD_CHECK, (uintptr_t)token,
        (uintptr_t)status) != KUI_GD_PROCESSING ||
       call(KUI_GD_REQUEST, KUI_GD_PIOREAD, (uintptr_t)parameters) != 0 ||
       call(KUI_GD_ABORT, (uintptr_t)token, 0) != 0 ||
       call(KUI_GD_CHECK, (uintptr_t)token, (uintptr_t)status) != KUI_GD_FAILED ||
       status[0] != 1 || status[1] != KUI_GD_ERROR_CANCELLED || status[2] ||
       status[3] || exports->read_count() != before) return 0;
    return 1;
}
uint32_t kui_image_client_main(const struct kui_image_client_api *api) {
    failures = 0;
    if(!valid_api(api)) return 1;
    exports = api;
    uint32_t initial_reads = api->read_count();
    uint32_t pass = call(KUI_GD_INIT, 0, 0) == 0 &&
        call(KUI_GD_DRIVE, (uintptr_t)drive, 0) == 0 &&
        drive[0] == 1 && drive[1] == 0x80 &&
        api->read_count() == initial_reads;
    report("GD vector init/status", pass, drive[1]);
    pass = query_mode(2048) && set_mode(2352) && set_mode(2048);
    report("Sector mode query/set", pass, 2);
    pass = toc_matches(0) && toc_matches(1);
    report("Selected image TOCs", pass, api->track_count);
    for(uint32_t method = 0; method < 2; ++method) {
        pass = 1;
        uint32_t detail = api->sample_count;
        for(uint32_t i = 0; i < api->sample_count; ++i) {
            if(!read_sample(i, method ? KUI_GD_DMAREAD : KUI_GD_PIOREAD)) {
                pass = 0; detail = i + 1; break;
            }
        }
        report(method ? "DMA-command reference CRCs" : "PIO reference CRCs",
               pass, detail);
    }
    pass = read_sample(api->sample_count - 1, KUI_GD_PIOREAD) &&
        read_sample(0, KUI_GD_DMAREAD) &&
        read_sample(api->sample_count / 2, KUI_GD_PIOREAD);
    report("Repeated random CRCs", pass, 3);
    pass = rejected_requests();
    report("Rejected requests", pass, 11);
    pass = cancel_pending();
    report("Cancel before SD read", pass, 1);
    pass = read_sample(0, KUI_GD_PIOREAD);
    report("Read after cancel CRC", pass, 1);
    uint32_t reads = api->read_count();
    report("Physical SD reads", reads > initial_reads, reads - initial_reads);
    return failures;
}
