/* SPDX-License-Identifier: GPL-3.0-only */
#define _DEFAULT_SOURCE
#include "kui/ftp.h"
#include "kui/clock.h"
#include "kui/media.h"
#include "w5500_model.h"
#include <assert.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The FTP server on the W5500 model (tests/w5500_model.c) with real FatFs
 * on a card image, driven by tests/test_ftp_images.py through Python's
 * ftplib:
 *   ftp-image IMAGE seed                     K-UI's start-up files
 *   ftp-image IMAGE bad-password             an unusable password file
 *   ftp-image IMAGE serve PORT PASSIVE       serve until stdin closes
 *   ftp-image IMAGE serve PORT PASSIVE absent    no W5500 answers
 *   ftp-image IMAGE serve PORT PASSIVE sync      a W5500 without async frames
 * Otherwise uploads and downloads stream through the model's async frames.
 * On stdin while serving: s stops; n makes the next upload's rename to its
 * name fail, N also the rename that puts back the file it replaced (each
 * prints ARMED and a count); l turns the model's 1 ms network latency on or
 * off (LATENCY, a count and the latency in microseconds); p prints PAUSES, a
 * count and how many sleeping pauses the server has asked for since the
 * last p; f and x make the 20th async frame from then fail or never end
 * (FAULT, a count and the key); w makes the next card write fail (WRITE and
 * a count).
 * Every open file and folder must be closed when the card is released. */
static struct {
    FILE *image;
    uint64_t blocks;
    unsigned files, dirs, armed, fail_part, fail_old, latencies, reports, sleeps, faults, fail_write, writes;
    bool active, connected, ready, stop;
    char last_event[KUI_APP_LINE_CAP];
} test;
static FATFS fs;

static void log_line(const char *format, ...) {
    va_list args;
    va_start(args, format);
    printf("LOG ");
    vprintf(format, args);
    va_end(args);
    puts("");
}
static uint64_t blocks(void *ctx) { (void)ctx; return test.blocks; }
static int read_image(void *ctx, uint32_t block, size_t count, uint8_t *data) {
    (void)ctx;
    return fseeko(test.image, (off_t)block * 512, SEEK_SET) || fread(data, 512, count, test.image) != count ? -1 : 0;
}
static int write_image(void *ctx, uint32_t block, size_t count, const uint8_t *data) {
    (void)ctx;
    return fseeko(test.image, (off_t)block * 512, SEEK_SET) || fwrite(data, 512, count, test.image) != count ? -1 : 0;
}
static int sync_image(void *ctx) { (void)ctx; return fflush(test.image) || fsync(fileno(test.image)) ? -1 : 0; }
static const struct kui_media_ops media = {NULL, blocks, read_image, write_image, sync_image};
bool kui_sd_connect(void) {
    assert(!test.connected);
    test.connected = test.active = true;
    kui_media_set(&media);
    return true;
}
void kui_sd_disconnect(void) {
    assert(test.connected);
    assert(!test.files && !test.dirs);
    test.connected = test.active = false;
    kui_media_set(NULL);
}
FRESULT __real_f_open(FIL *file, const TCHAR *path, BYTE flags);
FRESULT __wrap_f_open(FIL *file, const TCHAR *path, BYTE flags) {
    FRESULT result = __real_f_open(file, path, flags);
    if(test.active && result == FR_OK) ++test.files;
    return result;
}
FRESULT __real_f_close(FIL *file);
FRESULT __wrap_f_close(FIL *file) {
    FRESULT result = __real_f_close(file);
    if(test.active) { assert(test.files); --test.files; }
    return result;
}
FRESULT __real_f_opendir(DIR *dir, const TCHAR *path);
FRESULT __wrap_f_opendir(DIR *dir, const TCHAR *path) {
    FRESULT result = __real_f_opendir(dir, path);
    if(test.active && result == FR_OK) ++test.dirs;
    return result;
}
FRESULT __real_f_closedir(DIR *dir);
FRESULT __wrap_f_closedir(DIR *dir) {
    FRESULT result = __real_f_closedir(dir);
    if(test.active) { assert(test.dirs); --test.dirs; }
    return result;
}
static bool ends_with(const char *text, const char *suffix) {
    size_t n = strlen(text), k = strlen(suffix);
    return n > k && !strcmp(text + n - k, suffix);
}
FRESULT __real_f_write(FIL *file, const void *data, UINT bytes, UINT *wrote);
FRESULT __wrap_f_write(FIL *file, const void *data, UINT bytes, UINT *wrote) {
    if(test.fail_write && test.active) { --test.fail_write; *wrote = 0; return FR_DISK_ERR; }
    return __real_f_write(file, data, bytes, wrote);
}
FRESULT __real_f_rename(const TCHAR *from, const TCHAR *to);
FRESULT __wrap_f_rename(const TCHAR *from, const TCHAR *to) {
    if(test.fail_part && ends_with(from, ".kui-part")) { --test.fail_part; return FR_DENIED; }
    if(test.fail_old && ends_with(from, ".kui-old")) { --test.fail_old; return FR_DENIED; }
    return __real_f_rename(from, to);
}

/* The model's clock runs ahead while the server starts, so the three
 * second address check does not hold up every case. */
static bool frame(void *ctx, const uint8_t header[3], const uint8_t *out, uint8_t *in, size_t bytes) {
    return w5500_model_bus.frame(ctx, header, out, in, bytes);
}
static uint64_t now_ms(void *ctx) { return w5500_model_bus.now_ms(ctx); }
static void pause_ms(void *ctx, unsigned ms) {
    if(!test.ready) w5500_model_advance(100);
    else if(ms) ++test.sleeps;
    w5500_model_bus.pause(ctx, ms);
}
static bool frame_async(void *ctx, const uint8_t header[3], const uint8_t *out, uint8_t *in, size_t bytes,
                        void (*done)(void *arg, bool ok), void *arg) {
    return w5500_model_bus.frame_async(ctx, header, out, in, bytes, done, arg);
}
static void poll_async(void *ctx) { w5500_model_bus.poll(ctx); }
static void cancel_async(void *ctx) { w5500_model_bus.cancel(ctx); }
static uint64_t now_us(void *ctx) { return w5500_model_bus.now_us(ctx); }
static const struct kui_w5500_bus bus = {.frame = frame, .now_ms = now_ms, .pause = pause_ms, .frame_async = frame_async,
                                         .poll = poll_async, .cancel = cancel_async, .async_max = 4096, .now_us = now_us};
static bool open_level(unsigned level) { return level == 0; }
static void close_port(void) {}
static const char *speed(unsigned level) { (void)level; return "12.5 MHz"; }
static void mac(uint8_t out[6]) { static const uint8_t m[6] = {0x02, 0x4b, 0x55, 0x49, 0x46, 0x54}; memcpy(out, m, 6); }
static const struct kui_w5500_port port = {&bus, 1, open_level, close_port, speed, mac};
const struct kui_w5500_port *kui_w5500_console_port(void) { return &port; }

static bool cancel(void) {
    struct pollfd in = {.fd = 0, .events = POLLIN};
    if(!test.stop && poll(&in, 1, 0) > 0) {
        char byte;
        if(read(0, &byte, 1) <= 0 || byte == 's') test.stop = true;
        else if(byte == 'n' || byte == 'N') {
            ++test.fail_part;
            if(byte == 'N') ++test.fail_old;
            printf("ARMED %u\n", ++test.armed);
        } else if(byte == 'l') {
            struct w5500_model_options *o = w5500_model_live();
            o->latency_us = o->latency_us ? 0 : 1000u;
            printf("LATENCY %u %u\n", ++test.latencies, o->latency_us);
        } else if(byte == 'p') {
            printf("PAUSES %u %u\n", ++test.reports, test.sleeps);
            test.sleeps = 0;
        } else if(byte == 'w') {
            ++test.fail_write;
            printf("WRITE %u\n", ++test.writes);
        } else if(byte == 'f' || byte == 'x') {
            /* The 20th async frame from now fails, or never ends. */
            struct w5500_model_options *o = w5500_model_live();
            if(byte == 'f') o->async_fail_after = 20;
            else o->async_stall_after = 20;
            printf("FAULT %u %c\n", ++test.faults, byte);
        }
    }
    return test.stop;
}
static void publish(const struct kui_ftp_status *s) {
    if(s->state == KUI_FTP_READY && !test.ready) {
        test.ready = true;
        printf("READY port=%u password=%s ip=%u.%u.%u.%u adapter=%s\n", s->port, s->password, s->ip[0], s->ip[1], s->ip[2],
            s->ip[3], s->adapter);
    }
    if(s->event_count && strcmp(s->events[0], test.last_event)) {
        snprintf(test.last_event, sizeof(test.last_event), "%s", s->events[0]);
        printf("EVENT %s\n", s->events[0]);
    }
    for(unsigned i = 0; i < KUI_FTP_SESSIONS; ++i) {
        const struct kui_ftp_client *c = &s->clients[i];
        assert(!c->name[0] || c->sending || c->receiving);
        assert(!c->total || c->done <= c->total);
    }
}

/* New files are dated by K-UI's clock, as on the console: here a fixed
 * 2026-09-26 12:34:56, so the test knows every date to expect. */
static bool fixed_clock(void *ctx, int64_t *seconds) {
    (void)ctx;
    static const struct kui_datetime now = {2026, 9, 26, 12, 34, 56};
    return kui_clock_to_seconds(&now, seconds);
}
static void put(const char *path, const char *text) {
    FIL file;
    UINT wrote;
    assert(f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    assert(f_write(&file, text, (UINT)strlen(text), &wrote) == FR_OK && wrote == strlen(text));
    assert(f_close(&file) == FR_OK);
}
static void seed(bool bad_password) {
    assert(kui_sd_connect() && kui_mount(&fs, log_line));
    assert(f_mkdir("0:/KUI") == FR_OK && f_mkdir("0:/KUI/apps") == FR_OK && f_mkdir("0:/KUI/apps/games") == FR_OK);
    put("0:/KUI/runtime.kui", "K-UI runtime stand-in\n");
    put("0:/KUI/apps/games/retail-boot.kui", "retail boot stand-in\n");
    assert(f_mkdir("0:/Read only") == FR_OK);
    put("0:/Read only/locked.txt", "locked\n");
    assert(f_chmod("0:/Read only/locked.txt", AM_RDO, AM_RDO) == FR_OK);
    if(bad_password) put(KUI_FTP_PASSWORD_PATH, "this password is far too long to be accepted by K-UI\n");
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    kui_sd_disconnect();
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    if(argc < 3) { fprintf(stderr, "usage: ftp-image IMAGE seed|bad-password|serve PORT PASSIVE [absent|sync]\n"); return 2; }
    test.image = fopen(argv[1], "r+b");
    assert(test.image);
    struct stat st;
    assert(!fstat(fileno(test.image), &st));
    test.blocks = (uint64_t)st.st_size / 512u;
    kui_clock_configure(fixed_clock, NULL, log_line);
    int code = 0;
    if(!strcmp(argv[2], "seed") || !strcmp(argv[2], "bad-password")) {
        seed(!strcmp(argv[2], "bad-password"));
        puts("SEEDED");
    } else if(!strcmp(argv[2], "serve") && (argc == 5 || (argc == 6 && (!strcmp(argv[5], "absent") ||
                                                                          !strcmp(argv[5], "sync"))))) {
        struct kui_ftp_options options = {(uint16_t)atoi(argv[3]), (uint16_t)atoi(argv[4]), 40, 1234};
        struct kui_ftp_status status;
        struct w5500_model_options model = {.absent = argc == 6 && !strcmp(argv[5], "absent"),
                                            .no_async = argc == 6 && !strcmp(argv[5], "sync")};
        w5500_model_start(&model);
        kui_ftp_run(&port, &options, &status, log_line, cancel, publish);
        printf("STOPPED state=%d in=%u out=%u failures=%u connections=%u message=%s\n", status.state, status.files_in,
            status.files_out, status.failures, status.connections, status.message);
        w5500_model_stop();
        code = status.state == KUI_FTP_STOPPED ? 0 : 1;
    } else { fprintf(stderr, "unknown mode %s\n", argv[2]); return 2; }
    assert(!test.connected && !fclose(test.image));
    return code;
}
