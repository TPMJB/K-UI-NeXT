/* SPDX-License-Identifier: GPL-3.0-only
 * Real CHD reader -> verified GDI exporter -> host filesystem integration.
 */
#define _POSIX_C_SOURCE 200809L
#include "kui/chd_lib.h"
#include "kui/chd_cache.h"
#include "kui/hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct state {
    FILE *source, *track;
    uint64_t source_bytes;
    struct kui_chdl *reader;
    char staging[1024], final[1024];
    unsigned created;
};
static enum kui_chdl_result read_source(void *ctx, uint64_t at, void *out, size_t bytes) {
    struct state *s = ctx;
    if (at > s->source_bytes || bytes > s->source_bytes - at ||
        fseeko(s->source, (off_t)at, SEEK_SET) || fread(out, 1, bytes, s->source) != bytes)
        return KUI_CHDL_IO;
    return KUI_CHDL_OK;
}
static bool read_track(void *ctx, unsigned track, uint32_t first, uint32_t count,
                       void *out, size_t bytes) {
    struct state *s = ctx;
    return kui_chdl_read_track(s->reader, track, first, count, out, bytes) == KUI_CHDL_OK;
}
static bool path(char *out, size_t capacity, const char *dir, const char *name) {
    int n = snprintf(out, capacity, "%s/%s", dir, name);
    return n >= 0 && (size_t)n < capacity;
}
static bool track_path(struct state *s, unsigned track, char *out, size_t capacity) {
    char name[KUI_CHD_CACHE_NAME_BYTES];
    if (snprintf(name, sizeof(name), "track%02u.bin", track + 1u) != 11) return false;
    return path(out, capacity, s->staging, name);
}
static bool create_track(void *ctx, unsigned track, const char *name, uint64_t bytes) {
    (void)bytes;
    struct state *s = ctx; char filename[1100], expected[12];
    if (snprintf(expected, sizeof(expected), "track%02u.bin", track + 1u) != 11 ||
        strcmp(expected, name) || s->track || !path(filename, sizeof(filename), s->staging, name))
        return false;
    s->track = fopen(filename, "wbx");
    if (s->track) s->created = track + 1u;
    return s->track != NULL;
}
static bool write_track(void *ctx, unsigned track, uint64_t at, const void *data, size_t bytes) {
    struct state *s = ctx; (void)track;
    return s->track && !fseeko(s->track, (off_t)at, SEEK_SET) &&
           fwrite(data, 1, bytes, s->track) == bytes;
}
static bool read_output(void *ctx, unsigned track, uint64_t at, void *data, size_t bytes) {
    struct state *s = ctx; char filename[1100];
    if (!track_path(s, track, filename, sizeof(filename))) return false;
    FILE *f = fopen(filename, "rb");
    if (!f) return false;
    bool ok = !fseeko(f, (off_t)at, SEEK_SET) && fread(data, 1, bytes, f) == bytes;
    return !fclose(f) && ok;
}
static bool sync_track(void *ctx, unsigned track) {
    struct state *s = ctx; (void)track;
    return s->track && !fflush(s->track) && !fsync(fileno(s->track));
}
static bool finish_track(void *ctx, unsigned track, uint64_t expected) {
    struct state *s = ctx; (void)track;
    if (!s->track) return false;
    bool ok = !fseeko(s->track, 0, SEEK_END) && (uint64_t)ftello(s->track) == expected;
    int closed = fclose(s->track); s->track = NULL;
    return ok && !closed;
}
static bool publish(void *ctx, const char *gdi, size_t bytes,
                    const struct kui_chd_cache_report *report) {
    struct state *s = ctx; char filename[1100];
    if (!path(filename, sizeof(filename), s->staging, "disc.gdi")) return false;
    FILE *f = fopen(filename, "wbx");
    if (!f) return false;
    bool ok = fwrite(gdi, 1, bytes, f) == bytes && !fflush(f) && !fsync(fileno(f));
    if (fclose(f)) ok = false;
    if (!ok || rename(s->staging, s->final)) return false;
    uint8_t const *digest = report->descriptor_sha256; char hex[65];
    kui_hex(digest, 32, hex); printf("gdi %s\n", hex);
    for (unsigned n = 0; n < report->count; ++n) {
        kui_hex(report->track_sha256[n], 32, hex); printf("track %u %s\n", n, hex);
    }
    return true;
}
static void abort_output(void *ctx) {
    struct state *s = ctx; char filename[1100];
    if (s->track) { fclose(s->track); s->track = NULL; }
    for (unsigned n = 0; n < s->created; ++n)
        if (track_path(s, n, filename, sizeof(filename))) (void)unlink(filename);
    if (path(filename, sizeof(filename), s->staging, "disc.gdi")) (void)unlink(filename);
    (void)rmdir(s->staging);
}
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    struct state s = {0};
    if (!path(s.staging, sizeof(s.staging), argv[2], "staging") ||
        !path(s.final, sizeof(s.final), argv[2], "result")) return 2;
    s.source = fopen(argv[1], "rb");
    if (!s.source || fseeko(s.source, 0, SEEK_END) || ftello(s.source) < 0) return 2;
    s.source_bytes = (uint64_t)ftello(s.source);
    struct kui_chdl_io source_io = {&s, read_source, NULL};
    enum kui_chdl_result opened = kui_chdl_open(&source_io, s.source_bytes, &s.reader);
    if (opened != KUI_CHDL_OK) { fclose(s.source); return 3; }
    const struct kui_chdl_info *info = kui_chdl_get_info(s.reader);
    struct kui_chd_cache_plan plan = {.count = info->track_count};
    for (unsigned n = 0; n < plan.count; ++n) {
        const struct kui_chdl_track *t = &info->tracks[n];
        plan.tracks[n] = (struct kui_chd_cache_track){t->disc_lba, t->frames - t->pad,
                                                   t->sector_bytes, t->control};
    }
    if (mkdir(s.staging, 0700)) { kui_chdl_close(s.reader); fclose(s.source); return 2; }
    void *workspace = malloc(kui_chd_cache_workspace_bytes());
    if (!workspace) return 2;
    uint8_t identity[32] = {0}; struct kui_chd_cache_report report;
    struct kui_chd_cache_io io = {&s, read_track, create_track, write_track, read_output,
                                  sync_track, finish_track, publish, abort_output, NULL, NULL};
    enum kui_chd_cache_result result = kui_chd_cache_export(&plan, identity, &io,
        workspace, kui_chd_cache_workspace_bytes(), &report);
    free(workspace); kui_chdl_close(s.reader); fclose(s.source);
    if (result != KUI_CHD_CACHE_OK) {
        fprintf(stderr, "export failed: %s\n", kui_chd_cache_result_name(result)); return 4;
    }
    return 0;
}
