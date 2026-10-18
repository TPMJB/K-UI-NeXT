/* SPDX-License-Identifier: GPL-3.0-only
 * Host-only adversarial harness for the shell-side optical CHD reader.
 * The Python driver supplies MAME-created images and independent byte oracles.
 */
#define _POSIX_C_SOURCE 200809L
#include "kui/chd_lib.h"
#include "kui/hash.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct source {
    FILE *file;
    uint64_t bytes;
    unsigned reads;
    int cancel, fail;
};
static unsigned checks;
static void require(int condition, const char *what) {
    ++checks;
    if (!condition) { fprintf(stderr, "check failed: %s\n", what); exit(3); }
}
static enum kui_chdl_result read_source(void *context, uint64_t offset,
                                      void *out, size_t bytes) {
    struct source *source = context;
    ++source->reads;
    if (source->fail || offset > source->bytes || bytes > source->bytes - offset)
        return KUI_CHDL_IO;
    if (fseeko(source->file, (off_t)offset, SEEK_SET) ||
        fread(out, 1, bytes, source->file) != bytes) return KUI_CHDL_IO;
    return KUI_CHDL_OK;
}
static int cancelled(void *context) { return ((struct source *)context)->cancel; }
static void digest_line(const char *label, unsigned index,
                        const struct kui_sha256 *hash) {
    uint8_t bytes[32]; char hex[65];
    kui_sha256_digest(hash, bytes); kui_hex(bytes, sizeof(bytes), hex);
    printf("%s %u %s\n", label, index, hex);
}
static int guard_ok(const uint8_t *buffer, size_t length) {
    for (unsigned n = 0; n < 32; ++n)
        if (buffer[n] != 0xa5 || buffer[32 + length + n] != 0xa5) return 0;
    return 1;
}
int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) return 2;
    struct source source = {0};
    source.file = fopen(argv[1], "rb");
    if (!source.file || fseeko(source.file, 0, SEEK_END)) return 2;
    off_t length = ftello(source.file);
    if (length < 0) return 2;
    source.bytes = (uint64_t)length;
    struct kui_chdl_io io = {&source, read_source, cancelled};
    struct kui_chdl *reader = (struct kui_chdl *)(uintptr_t)1;
    enum kui_chdl_result result = kui_chdl_open(&io, source.bytes, &reader);
    if (argc == 3 && !strncmp(argv[2], "reject", 6)) {
        if (result == KUI_CHDL_OK) {
            const struct kui_chdl_info *info = kui_chdl_get_info(reader);
            uint8_t *hunk = malloc(info->hunk_bytes);
            if (!hunk) return 2;
            uint32_t start = !strcmp(argv[2], "reject-last") ? info->hunk_count - 1u : 0;
            for (uint32_t n = start; n < info->hunk_count && result == KUI_CHDL_OK; ++n) {
                uint32_t first = n * (info->hunk_bytes / KUI_CHDL_FRAME_BYTES);
                uint32_t frames = (uint32_t)(info->logical_bytes / KUI_CHDL_FRAME_BYTES) - first;
                if (frames > info->hunk_bytes / KUI_CHDL_FRAME_BYTES)
                    frames = info->hunk_bytes / KUI_CHDL_FRAME_BYTES;
                result = kui_chdl_read_frames(reader, first, frames, hunk, info->hunk_bytes);
            }
            free(hunk); kui_chdl_close(reader);
        } else require(reader == NULL, "failed open clears output handle");
        fclose(source.file);
        if (result == KUI_CHDL_OK) { fputs("corruption unexpectedly accepted\n", stderr); return 4; }
        printf("rejected %s\n", kui_chdl_result_name(result)); return 0;
    }
    require(result == KUI_CHDL_OK, "open valid CHD");
    require(reader != NULL, "opened handle");
    const struct kui_chdl_info *info = kui_chdl_get_info(reader);
    require(info != NULL, "opened info");
    if (argc == 3 && !strcmp(argv[2], "last")) {
        uint8_t bytes[KUI_CHDL_FRAME_BYTES]; struct kui_sha256 last;
        uint32_t number = (uint32_t)(info->logical_bytes / KUI_CHDL_FRAME_BYTES) - 1u;
        require(kui_chdl_read_frames(reader, number, 1, bytes, sizeof(bytes)) == KUI_CHDL_OK,
                "cold last-frame self-reference read");
        kui_sha256_init(&last); kui_sha256_update(&last, bytes, sizeof(bytes));
        digest_line("last", number, &last);
        kui_chdl_close(reader); fclose(source.file); return 0;
    }
    printf("info %u %u %u %u %u %u\n", info->version, info->hunk_bytes,
           info->hunk_count, info->track_count, info->gdrom,
           info->legacy_audio_little_endian);
    const size_t capacity = 13u * KUI_CHDL_FRAME_BYTES;
    uint8_t *guard = malloc(capacity + 64), *saved = malloc(capacity);
    require(guard && saved, "harness allocation");
    memset(guard, 0xa5, capacity + 64);
    uint8_t *buffer = guard + 32;
    struct kui_sha256 hash;
    kui_sha256_init(&hash);
    uint32_t total_frames = (uint32_t)(info->logical_bytes / KUI_CHDL_FRAME_BYTES);
    for (uint32_t frame = 0; frame < total_frames; ) {
        uint32_t count = total_frames - frame;
        if (count > 13) count = 13;
        require(kui_chdl_read_frames(reader, frame, count, buffer, capacity) == KUI_CHDL_OK,
                "canonical frame read crossing hunk boundary");
        kui_sha256_update(&hash, buffer, (size_t)count * KUI_CHDL_FRAME_BYTES);
        require(guard_ok(guard, capacity), "canonical output redzones");
        frame += count;
    }
    digest_line("canonical", 0, &hash);
    for (uint32_t track = 0; track < info->track_count; ++track) {
        const struct kui_chdl_track *t = &info->tracks[track];
        printf("trackinfo %u %u %u %u %u %u %u %u %u\n", track, t->disc_lba,
               t->storage_frame, t->frames, t->pad, t->stored_frames,
               t->sector_bytes, t->control, t->mode);
        kui_sha256_init(&hash);
        for (uint32_t sector = 0; sector < t->frames - t->pad; ) {
            uint32_t count = t->frames - t->pad - sector;
            if (count > 13) count = 13;
            require(kui_chdl_read_track(reader, track, sector, count, buffer, capacity) == KUI_CHDL_OK,
                    "track sector read crossing hunk boundary");
            kui_sha256_update(&hash, buffer, (size_t)count * t->sector_bytes);
            require(guard_ok(guard, capacity), "track output redzones");
            sector += count;
        }
        digest_line("track", track, &hash);
        require(kui_chdl_read_track(reader, track, t->frames - t->pad, 1,
                                   buffer, capacity) == KUI_CHDL_RANGE, "PAD not captured sector");
        require(kui_chdl_read_track(reader, track, 0, 1, buffer,
                                   t->sector_bytes - 1u) == KUI_CHDL_RANGE, "short track output");
        /* Read in reverse order after an unrelated hunk was cached. */
        for (uint32_t n = t->frames - t->pad; n; --n) {
            require(kui_chdl_read_track(reader, track, n - 1, 1, buffer, capacity) == KUI_CHDL_OK,
                    "reverse sector read");
            memcpy(saved, buffer, t->sector_bytes);
            require(kui_chdl_read_frames(reader, total_frames - 1, 1, buffer, capacity) == KUI_CHDL_OK,
                    "cache displaced");
            require(kui_chdl_read_track(reader, track, n - 1, 1, buffer, capacity) == KUI_CHDL_OK &&
                    !memcmp(saved, buffer, t->sector_bytes), "displaced cache reload agrees");
        }
    }
    require(kui_chdl_read_track(reader, info->track_count, 0, 1, buffer, capacity) == KUI_CHDL_RANGE,
            "track index range");
    require(kui_chdl_read_frames(reader, total_frames, 1, buffer, capacity) == KUI_CHDL_RANGE,
            "canonical end range");
    require(kui_chdl_read_frames(reader, UINT32_MAX, 2, buffer, capacity) == KUI_CHDL_RANGE,
            "canonical arithmetic range");
    require(kui_chdl_read_frames(reader, 0, 1, buffer, KUI_CHDL_FRAME_BYTES - 1) == KUI_CHDL_RANGE,
            "short canonical output");
    kui_chdl_close(reader);
    /* Reopen to ensure the cancellation and I/O paths cannot be masked by cache. */
    require(kui_chdl_open(&io, source.bytes, &reader) == KUI_CHDL_OK, "reopen");
    source.cancel = 1;
    require(kui_chdl_read_frames(reader, 0, 1, buffer, capacity) == KUI_CHDL_CANCELLED,
            "cancelled read");
    source.cancel = 0; source.fail = 1;
    require(kui_chdl_read_frames(reader, 0, 1, buffer, capacity) == KUI_CHDL_IO,
            "I/O failure propagation");
    source.fail = 0;
    require(kui_chdl_read_frames(reader, 0, 1, buffer, capacity) == KUI_CHDL_OK,
            "retry after cancelled/failed read");
    kui_chdl_close(reader);
    source.cancel = 1; reader = (struct kui_chdl *)(uintptr_t)1;
    require(kui_chdl_open(&io, source.bytes, &reader) == KUI_CHDL_CANCELLED && reader == NULL,
            "cancelled open clears handle");
    source.cancel = 0; source.fail = 1; reader = (struct kui_chdl *)(uintptr_t)1;
    require(kui_chdl_open(&io, source.bytes, &reader) == KUI_CHDL_IO && reader == NULL,
            "failed I/O open clears handle");
    require(guard_ok(guard, capacity), "final output redzones");
    free(saved); free(guard); fclose(source.file);
    printf("checks %u\n", checks);
    return 0;
}
