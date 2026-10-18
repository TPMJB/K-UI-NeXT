/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/chd_cache.h"
#include "kui/hash.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define CACHE_LBA_LIMIT (719999u - 150u + 1u)
struct cache_workspace {
    struct kui_chd_cache_report report;
    struct kui_sha256 hash;
    uint8_t buffer[KUI_CHD_CACHE_CHUNK_SECTORS * 2352u];
    char gdi[KUI_CHD_CACHE_GDI_BYTES];
};

size_t kui_chd_cache_workspace_bytes(void) { return sizeof(struct cache_workspace); }
size_t kui_chd_cache_workspace_alignment(void) { return _Alignof(struct cache_workspace); }

static enum kui_chd_cache_result descriptor(const struct kui_chd_cache_plan *plan,
    char *text, size_t capacity, size_t *length, uint64_t *payload) {
    if(!plan || !length || !payload || !plan->count || plan->count > KUI_CHD_CACHE_TRACK_MAX)
        return KUI_CHD_CACHE_INVALID;
    size_t used = 0;
    uint64_t total = 0;
    char row[96];
    int n = snprintf(row, sizeof(row), "%u\n", plan->count);
    if(n <= 0 || (size_t)n >= sizeof(row)) return KUI_CHD_CACHE_RANGE;
    if(text) {
        if((size_t)n >= capacity) return KUI_CHD_CACHE_RANGE;
        memcpy(text, row, (size_t)n);
    }
    used = (size_t)n;
    uint32_t previous_end = 0;
    for(unsigned i = 0; i < plan->count; ++i) {
        const struct kui_chd_cache_track *track = &plan->tracks[i];
        if(!track->frames || (track->control != 0u && track->control != 4u) ||
           (track->sector_bytes != 2048u && track->sector_bytes != 2352u) ||
           (track->control == 0u && track->sector_bytes != 2352u))
            return KUI_CHD_CACHE_INVALID;
        if(track->disc_lba >= CACHE_LBA_LIMIT ||
           track->frames > CACHE_LBA_LIMIT - track->disc_lba ||
           (i && track->disc_lba < previous_end)) return KUI_CHD_CACHE_RANGE;
        previous_end = track->disc_lba + track->frames;
        uint64_t bytes = (uint64_t)track->frames * track->sector_bytes;
        if(total > UINT64_MAX - bytes) return KUI_CHD_CACHE_RANGE;
        total += bytes;
        n = snprintf(row, sizeof(row), "%u %" PRIu32 " %u %u track%02u.bin 0\n",
            i + 1u, track->disc_lba, (unsigned)track->control,
            (unsigned)track->sector_bytes, i + 1u);
        if(n <= 0 || (size_t)n >= sizeof(row) || used > SIZE_MAX - (size_t)n)
            return KUI_CHD_CACHE_RANGE;
        if(text) {
            if(used + (size_t)n >= capacity) return KUI_CHD_CACHE_RANGE;
            memcpy(text + used, row, (size_t)n);
        }
        used += (size_t)n;
    }
    if(used >= KUI_CHD_CACHE_GDI_BYTES) return KUI_CHD_CACHE_RANGE;
    if(text) text[used] = '\0';
    *length = used;
    *payload = total;
    return KUI_CHD_CACHE_OK;
}

enum kui_chd_cache_result kui_chd_cache_bound(const struct kui_chd_cache_plan *plan,
    uint64_t *bytes) {
    if(!bytes) return KUI_CHD_CACHE_INVALID;
    size_t length = 0;
    uint64_t payload = 0;
    enum kui_chd_cache_result result = descriptor(plan, NULL, 0, &length, &payload);
    if(result != KUI_CHD_CACHE_OK) return result;
    if(payload > UINT64_MAX - length) return KUI_CHD_CACHE_RANGE;
    *bytes = payload + length;
    return KUI_CHD_CACHE_OK;
}

static bool cancelled(const struct kui_chd_cache_io *io) {
    return io->cancelled && io->cancelled(io->ctx);
}
static enum kui_chd_cache_result failed(const struct kui_chd_cache_io *io) {
    return cancelled(io) ? KUI_CHD_CACHE_CANCELLED : KUI_CHD_CACHE_IO;
}
static void progress(const struct kui_chd_cache_io *io, uint64_t done,
    uint64_t total, bool verify) {
    if(io->progress) io->progress(io->ctx, done, total, verify);
}

enum kui_chd_cache_result kui_chd_cache_export(const struct kui_chd_cache_plan *plan,
    const uint8_t source_id[KUI_CHD_CACHE_ID_BYTES], const struct kui_chd_cache_io *io,
    void *workspace, size_t workspace_bytes, struct kui_chd_cache_report *report) {
    if(!plan || !source_id || !io || !workspace || !report ||
       workspace_bytes < sizeof(struct cache_workspace) ||
       (uintptr_t)workspace % _Alignof(struct cache_workspace) ||
       !io->read_track || !io->create_track || !io->write_track || !io->read_output ||
       !io->sync_track || !io->finish_track || !io->publish || !io->abort)
        return KUI_CHD_CACHE_INVALID;
    struct cache_workspace *work = workspace;
    /* source_id may be the previous output report's identity. */
    uint8_t identity[KUI_CHD_CACHE_ID_BYTES];
    memcpy(identity, source_id, sizeof(identity));
    memset(report, 0, sizeof(*report));
    memset(&work->report, 0, sizeof(work->report));
    size_t gdi_bytes = 0;
    uint64_t payload = 0;
    enum kui_chd_cache_result result = descriptor(plan, work->gdi, sizeof(work->gdi),
        &gdi_bytes, &payload);
    if(result != KUI_CHD_CACHE_OK) return result;
    work->report.count = plan->count;
    work->report.payload_bytes = payload;
    work->report.descriptor_bytes = gdi_bytes;
    memcpy(work->report.source_id, identity, sizeof(identity));
    bool active = false;
    unsigned current = 0;
    uint64_t done = 0;
    for(unsigned track = 0; track < plan->count; ++track) {
        current = track;
        const struct kui_chd_cache_track *entry = &plan->tracks[track];
        uint64_t bytes = (uint64_t)entry->frames * entry->sector_bytes;
        char name[KUI_CHD_CACHE_NAME_BYTES];
        int n = snprintf(name, sizeof(name), "track%02u.bin", track + 1u);
        if(n <= 0 || (size_t)n >= sizeof(name)) {result = KUI_CHD_CACHE_RANGE; goto fail;}
        if(cancelled(io)) {result = KUI_CHD_CACHE_CANCELLED; goto fail;}
        if(!io->create_track(io->ctx, track, name, bytes)) {result = failed(io); goto fail;}
        active = true;
        kui_sha256_init(&work->hash);
        for(uint32_t first = 0; first < entry->frames;) {
            if(cancelled(io)) {result = KUI_CHD_CACHE_CANCELLED; goto fail;}
            uint32_t count = entry->frames - first;
            if(count > KUI_CHD_CACHE_CHUNK_SECTORS) count = KUI_CHD_CACHE_CHUNK_SECTORS;
            size_t take = (size_t)count * entry->sector_bytes;
            if(!io->read_track(io->ctx, track, first, count, work->buffer, take) ||
               !io->write_track(io->ctx, track, (uint64_t)first * entry->sector_bytes,
                                work->buffer, take)) {result = failed(io); goto fail;}
            kui_sha256_update(&work->hash, work->buffer, take);
            first += count;
            progress(io, done + (uint64_t)first * entry->sector_bytes, payload, false);
        }
        kui_sha256_digest(&work->hash, work->report.track_sha256[track]);
        if(cancelled(io)) {result = KUI_CHD_CACHE_CANCELLED; goto fail;}
        if(!io->sync_track(io->ctx, track)) {result = failed(io); goto fail;}
        kui_sha256_init(&work->hash);
        for(uint64_t offset = 0; offset < bytes;) {
            if(cancelled(io)) {result = KUI_CHD_CACHE_CANCELLED; goto fail;}
            size_t take = bytes - offset > sizeof(work->buffer) ?
                sizeof(work->buffer) : (size_t)(bytes - offset);
            if(!io->read_output(io->ctx, track, offset, work->buffer, take)) {
                result = failed(io); goto fail;
            }
            kui_sha256_update(&work->hash, work->buffer, take);
            offset += take;
            progress(io, done + offset, payload, true);
        }
        uint8_t actual[32];
        kui_sha256_digest(&work->hash, actual);
        if(memcmp(actual, work->report.track_sha256[track], sizeof(actual))) {
            result = KUI_CHD_CACHE_VERIFY; goto fail;
        }
        /* finish owns closure even when it reports an exact-length failure. */
        active = false;
        if(!io->finish_track(io->ctx, track, bytes)) {result = failed(io); goto fail;}
        done += bytes;
    }
    kui_sha256_init(&work->hash);
    kui_sha256_update(&work->hash, work->gdi, gdi_bytes);
    kui_sha256_digest(&work->hash, work->report.descriptor_sha256);
    if(cancelled(io)) {result = KUI_CHD_CACHE_CANCELLED; goto fail;}
    if(!io->publish(io->ctx, work->gdi, gdi_bytes, &work->report)) {result = failed(io); goto fail;}
    *report = work->report;
    return KUI_CHD_CACHE_OK;
fail:
    if(active) {
        uint64_t expected = (uint64_t)plan->tracks[current].frames * plan->tracks[current].sector_bytes;
        (void)io->finish_track(io->ctx, current, expected);
    }
    io->abort(io->ctx);
    return result;
}

const char *kui_chd_cache_result_name(enum kui_chd_cache_result result) {
    switch(result) {
    case KUI_CHD_CACHE_OK: return "CHD cache prepared and verified";
    case KUI_CHD_CACHE_INVALID: return "Invalid CHD cache plan or callback";
    case KUI_CHD_CACHE_RANGE: return "CHD cache geometry exceeds limits";
    case KUI_CHD_CACHE_IO: return "CHD cache read or write failed";
    case KUI_CHD_CACHE_CANCELLED: return "CHD cache preparation stopped";
    case KUI_CHD_CACHE_VERIFY: return "CHD cache readback differs";
    default: return "Unknown CHD cache error";
    }
}
