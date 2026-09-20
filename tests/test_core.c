/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/core.h"
#include "kui/media.h"
#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct fake_command { uint64_t now; int polls, submissions, busy, status, abort_status;
                      unsigned cancel_at; bool aborted; };
static uint64_t now(void *p) { return ((struct fake_command *)p)->now; }
static void yield(void *p) { ++((struct fake_command *)p)->now; }
static bool cancel(void *p) {
    struct fake_command *f = p;
    return f->cancel_at && f->now >= f->cancel_at;
}
static int submit(void *p, int c, void *params) {
    (void)c; (void)params;
    struct fake_command *f = p;
    return ++f->submissions <= f->busy ? 0 : 7;
}
static int poll(void *p, int handle) {
    assert(handle == 7);
    struct fake_command *f = p;
    ++f->polls;
    return f->aborted ? f->abort_status : f->status;
}
static void abort_cmd(void *p, int handle) {
    assert(handle == 7); ((struct fake_command *)p)->aborted = true;
}
static enum kui_command_result execute(struct fake_command *f) {
    struct kui_command_ops ops = {f, now, yield, cancel, submit, poll, abort_cmd};
    return kui_command(&ops, 16, NULL, 10, 3);
}
/* The split form: begin, do something else, end. kui_command is now these two composed, so the
 * cases above already prove the shared state machine; these prove the seam. */
static struct kui_command_ops ops_for(struct fake_command *f) {
    struct kui_command_ops o = {f, now, yield, cancel, submit, poll, abort_cmd};
    return o;
}
static void test_split_commands(void) {
    struct kui_command_async a;
    /* The happy path: begin returns at once, ready goes false then true, end succeeds. */
    struct fake_command f = {.status = 1};
    struct kui_command_ops o = ops_for(&f);
    assert(kui_command_begin(&o, 16, NULL, 10, 3, &a) == KUI_CMD_OK && a.live);
    assert(f.submissions == 1 && f.polls == 0);          /* begin must not wait */
    assert(!kui_command_ready(&o, &a));                  /* still running */
    f.status = 2;
    assert(kui_command_ready(&o, &a));
    assert(kui_command_end(&o, &a) == KUI_CMD_OK && !f.aborted && !a.live);

    /* end is mandatory, and a second one cannot abort again. A never-begun async and an
     * already-ended one are the same thing to end, and both report FAILED rather than
     * pretending something succeeded. */
    assert(kui_command_end(&o, &a) == KUI_CMD_FAILED && !f.aborted);
    memset(&a, 0, sizeof(a));
    assert(kui_command_end(&o, &a) == KUI_CMD_FAILED && !f.aborted);
    assert(kui_command_end(&o, NULL) == KUI_CMD_INVALID);
    assert(kui_command_end(NULL, &a) == KUI_CMD_INVALID);

    /* The deadline spans begin to end, not each call: time passing between them counts. */
    f = (struct fake_command){.status = 1};
    o = ops_for(&f);
    assert(kui_command_begin(&o, 16, NULL, 10, 3, &a) == KUI_CMD_OK);
    f.now = 10;                                          /* the caller was busy elsewhere */
    assert(kui_command_end(&o, &a) == KUI_CMD_TIMEOUT && f.aborted);

    /* A drive that never accepts the command fails inside begin, with nothing to end. */
    f = (struct fake_command){.busy = 100};
    o = ops_for(&f);
    assert(kui_command_begin(&o, 16, NULL, 10, 3, &a) == KUI_CMD_TIMEOUT && !a.live && !f.aborted);
    f = (struct fake_command){.status = 1, .cancel_at = 1, .now = 1};   /* already cancelled */
    o = ops_for(&f);
    assert(kui_command_begin(&o, 16, NULL, 10, 3, &a) == KUI_CMD_CANCELLED && !a.live);
    assert(f.submissions == 0 && !f.aborted);   /* nothing was ever sent to the drive */

    /* Cancellation after begin still aborts and recovers: the firmware owns the buffer. */
    f = (struct fake_command){.status = 1};
    o = ops_for(&f);
    assert(kui_command_begin(&o, 16, NULL, 10, 3, &a) == KUI_CMD_OK);
    f.cancel_at = 1; f.now = 1; f.abort_status = 0;
    assert(kui_command_end(&o, &a) == KUI_CMD_CANCELLED && f.aborted);

    /* A drive that will not even abort is the reset-required case, through this path too. */
    f = (struct fake_command){.status = 4, .abort_status = 4};
    o = ops_for(&f);
    assert(kui_command_begin(&o, 16, NULL, 10, 3, &a) == KUI_CMD_OK);
    assert(kui_command_end(&o, &a) == KUI_CMD_RECOVERY_FAILED);

    /* ready never blocks and never aborts, whatever the drive says. */
    f = (struct fake_command){.status = 4};
    o = ops_for(&f);
    assert(kui_command_begin(&o, 16, NULL, 10, 3, &a) == KUI_CMD_OK);
    unsigned before = (unsigned)f.polls;
    assert(!kui_command_ready(&o, &a) && f.polls == (int)before + 1 && !f.aborted);
    f.status = -1;
    assert(kui_command_ready(&o, &a) && !f.aborted);     /* a failure is "no longer waiting" */
    assert(kui_command_end(&o, &a) == KUI_CMD_FAILED && !f.aborted);
    /* Bad arguments are refused, not crashed on. */
    assert(kui_command_begin(&o, 16, NULL, 0, 3, &a) == KUI_CMD_INVALID);
    assert(kui_command_begin(&o, 16, NULL, 10, 3, NULL) == KUI_CMD_INVALID);
    assert(kui_command_begin(NULL, 16, NULL, 10, 3, &a) == KUI_CMD_INVALID);
}
static void test_commands(void) {
    struct fake_command f = {.status = 2};
    assert(execute(&f) == KUI_CMD_OK && !f.aborted);
    f = (struct fake_command){.status = -1};
    assert(execute(&f) == KUI_CMD_FAILED);
    f = (struct fake_command){.busy = 100};
    assert(execute(&f) == KUI_CMD_TIMEOUT && f.now == 10 && !f.aborted);
    f = (struct fake_command){.busy = 8, .status = 1};
    assert(execute(&f) == KUI_CMD_TIMEOUT && f.now == 10 && f.aborted);
    f = (struct fake_command){.status = 4, .abort_status = 4};
    assert(execute(&f) == KUI_CMD_RECOVERY_FAILED && f.now == 13);
    f = (struct fake_command){.status = 1, .cancel_at = 2};
    assert(execute(&f) == KUI_CMD_CANCELLED && f.now == 2 && f.aborted);
    f = (struct fake_command){.busy = 100, .cancel_at = 2};
    assert(execute(&f) == KUI_CMD_CANCELLED && !f.aborted);
    f = (struct fake_command){.status = 3};
    assert(execute(&f) == KUI_CMD_FAILED && f.aborted);
    f = (struct fake_command){.status = 19, .abort_status = 1};
    assert(execute(&f) == KUI_CMD_RECOVERY_FAILED);
    f = (struct fake_command){.status = 1, .now = UINT64_MAX - 4};
    assert(execute(&f) == KUI_CMD_TIMEOUT && f.now == 5);
    assert(kui_command(NULL, 0, NULL, 0, 0) == KUI_CMD_INVALID);
}
static void put32(uint8_t *p, uint32_t x) {
    for(unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(x >> (8 * i));
}
static void make_mbr(uint8_t *p) {
    memset(p, 0, 512); p[510] = 0x55; p[511] = 0xaa;
    p[450] = 0x0c; put32(p + 454, 2048); put32(p + 458, 4096);
}
static void test_volume(void) {
    struct kui_volume v;
    uint8_t mbr[512]; make_mbr(mbr);
    assert(kui_select_volume(mbr, 6144, &v));
    assert(v.start == 2048 && v.count == 4096 && v.partitioned);
    assert(!kui_select_volume(mbr, 6143, &v));
    mbr[466] = 0x0c; assert(!kui_select_volume(mbr, 6144, &v));
    make_mbr(mbr); mbr[450] = 0xee; assert(!kui_select_volume(mbr, 6144, &v));
    make_mbr(mbr); put32(mbr + 454, UINT32_MAX - 3);
    assert(!kui_select_volume(mbr, UINT32_MAX, &v));
    memset(mbr, 0, 512); mbr[510] = 0x55; mbr[511] = 0xaa;
    memcpy(mbr + 3, "EXFAT   ", 8); mbr[108] = 9;
    assert(kui_select_volume(mbr, 6144, &v) && v.start == 0 && !v.partitioned);
    mbr[108] = 12; assert(!kui_select_volume(mbr, 6144, &v));
    assert(!kui_block_range(0, 0, 10));
    assert(!kui_block_range(UINT64_MAX - 2, 4, UINT64_MAX));
    assert(kui_block_range(5, 5, 10));
}
static void test_disc_data(void) {
    uint32_t lba, entries[99]; struct kui_toc toc;
    memset(entries, 0xff, sizeof(entries));
    entries[2] = 0x41000000u | 45150; entries[3] = 0x01000000u | 50150;
    assert(kui_parse_toc(entries, 3u << 16, 4u << 16, 55150, &toc));
    assert(toc.count == 2 && toc.tracks[0].end == 50150);
    assert(kui_fad_to_lba(toc.tracks[0].start, &lba) && lba == 45000);
    assert(!kui_fad_to_lba(149, &lba));
    uint32_t points[3];
    assert(kui_sample_points(&toc.tracks[0], points) == 3);
    assert(points[0] == 45150 && points[2] == 49999);
    entries[3] = entries[2];
    assert(!kui_parse_toc(entries, 3u << 16, 4u << 16, 55150, &toc));
    assert(!kui_parse_toc(entries, 0, 100u << 16, 55150, &toc));
    uint8_t sector[2352] = {0}; memset(sector + 1, 255, 10); sector[15] = 1;
    assert(kui_data_offset(sector) == 16);
    sector[15] = 2; assert(kui_data_offset(sector) == 24);
    sector[18] = sector[22] = 0x20; assert(kui_data_offset(sector) == -1);
    sector[15] = 1; sector[0] = 1; assert(kui_data_offset(sector) == -1);
}
struct fake_media { uint8_t mbr[512]; bool fail, fail_sync; unsigned reads, writes;
                    uint32_t last; };
static uint64_t blocks(void *p) { (void)p; return 8192; }
static int read_media(void *p, uint32_t block, size_t count, uint8_t *data) {
    struct fake_media *f = p; ++f->reads; f->last = block;
    if(f->fail) return -1;
    if(block == 0 && count == 1) memcpy(data, f->mbr, 512);
    else memset(data, 0, count * 512);
    return 0;
}
static int write_media(void *p, uint32_t block, size_t count, const uint8_t *data) {
    (void)count; (void)data;
    struct fake_media *f = p; ++f->writes; f->last = block;
    return f->fail ? -1 : 0;
}
static int sync_media(void *p) { return ((struct fake_media *)p)->fail_sync ? -1 : 0; }
static void test_diskio(void) {
    struct fake_media f = {0}; make_mbr(f.mbr);
    struct kui_media_ops ops = {&f, blocks, read_media, write_media, sync_media};
    kui_media_set(&ops); assert(disk_initialize(0) == 0);
    uint8_t data[1024] = {0};
    assert(disk_read(0, data, 4095, 1) == RES_OK && f.last == 6143);
    unsigned reads = f.reads;
    assert(disk_read(0, data, 4095, 2) == RES_PARERR && reads == f.reads);
    assert(disk_write(0, data, 4096, 1) == RES_PARERR && !f.writes);
    assert(disk_write(0, data, 0, 1) == RES_OK && f.last == 2048);
    f.fail_sync = true;
    assert(disk_ioctl(0, CTRL_SYNC, NULL) == RES_ERROR);
    assert(disk_status(0) & STA_NOINIT);
    assert(kui_media_problem() != NULL);
    assert(disk_initialize(0) & STA_NOINIT);
    assert(disk_write(0, data, 0, 1) != RES_OK && f.writes == 1);
    kui_media_set(&ops); f.fail_sync = false;
    assert(disk_initialize(0) == 0); f.fail = true;
    assert(disk_read(0, data, 0, 1) == RES_ERROR);
    assert(disk_status(0) & STA_NOINIT);
    kui_media_set(&ops); f.fail = false; f.mbr[450] = 0xee;
    assert(disk_initialize(0) & STA_NOINIT);
    assert(strstr(kui_media_problem(), "layout") != NULL);
}
int main(void) {
    test_commands();
    test_split_commands(); test_volume(); test_disc_data(); test_diskio();
    assert(kui_crc32(0, "123456789", 9) == 0xcbf43926);
    assert(kui_crc32(kui_crc32(0, "1234", 4), "56789", 5) == 0xcbf43926);
    uint8_t a[200], b[200];
    kui_pattern(a, 65531, sizeof(a));
    kui_pattern(b, 65531, 17); kui_pattern(b + 17, 65548, 183);
    assert(!memcmp(a, b, sizeof(a)));
    puts("PASS: command deadlines, cancellation/recovery, TOCs, sector layouts,");
    puts("      partition bounds, failed-media latch, CRC32, chunked fixture.");
    return 0;
}
