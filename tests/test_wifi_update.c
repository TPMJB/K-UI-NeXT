/* SPDX-License-Identifier: GPL-3.0-only */
#define _DEFAULT_SOURCE
#include "kui/wifi_update.h"
#include "kui/hash.h"
#include "kui/media.h"
#include "wifi_model.h"
#include "ff.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Real FatFs reads on an exFAT/FAT32 card, real firmware bridge/link, and
 * an inactive application slot with independently computed SHA-256. */
#define IMAGE_BYTES 12288u
#define IMAGE_PATH "0:/KUI/firmware/kui-wifi-esp32c5-update.bin"
static FILE *card;
static uint64_t card_blocks;
static bool connected, active, cancel_before, cancel_writing, cancel_committing;
static bool corrupt_sidecar, keep_sidecar, change_stream, changed_stream;
static unsigned writes, connects, disconnects, publications;
static uint8_t firmware[IMAGE_BYTES];
static struct kui_wifi_view last;
static void log_line(const char *format, ...) { (void)format; }
static uint64_t blocks(void *ctx) { (void)ctx; return card_blocks; }
static int read_card(void *ctx, uint32_t block, size_t count, uint8_t *data) {
    (void)ctx;
    if(fseeko(card, (off_t)block * 512, SEEK_SET) || fread(data, 512, count, card) != count) return -1;
    if(active && change_stream && !changed_stream && wifi_model_ota_result()->begins) {
        /* Change a later file block only after its preview/hash pass. */
        for(size_t i = 0; i + 64u <= count * 512u; ++i) {
            if(memcmp(data + i, firmware + 4096, 64)) continue;
            data[i] ^= 1;
            changed_stream = true;
            break;
        }
    }
    return 0;
}
static int write_card(void *ctx, uint32_t block, size_t count, const uint8_t *data) {
    (void)ctx;
    if(active) ++writes;
    return fseeko(card, (off_t)block * 512, SEEK_SET) || fwrite(data, 512, count, card) != count ? -1 : 0;
}
static int sync_card(void *ctx) { (void)ctx; return fflush(card) || fsync(fileno(card)) ? -1 : 0; }
static const struct kui_media_ops media = {NULL, blocks, read_card, write_card, sync_card};
bool kui_sd_connect(void) {
    assert(!connected);
    connected = true;
    ++connects;
    kui_media_set(&media);
    return true;
}
void kui_sd_disconnect(void) {
    assert(connected);
    connected = false;
    ++disconnects;
    kui_media_set(NULL);
}
bool kui_storage_sci_reserved(void) { return false; }
const struct kui_wifi_port *kui_wifi_console_port(void) { return &wifi_model_port; }
static bool cancel(void) {
    return cancel_before || (cancel_writing && wifi_model_ota_result()->writes && last.firmware_done >= 2048u) ||
        (cancel_committing && last.firmware_committing);
}
static void publish(const struct kui_wifi_view *view) {
    last = *view;
    ++publications;
}
static void put32(uint8_t *out, uint32_t value) { kwl_put32(out, value); }
static void image_fixture(void) {
    for(unsigned i = 0; i < sizeof(firmware); ++i) firmware[i] = (uint8_t)(i * 97u + (i >> 8));
    memset(firmware, 0, KUI_WIFI_FIRMWARE_HEADER);
    firmware[0] = 0xe9;
    firmware[1] = 1;
    firmware[12] = 23;
    firmware[23] = 1;
    put32(firmware + 24, 0x42000020u);
    put32(firmware + 28, IMAGE_BYTES - 80u);
    put32(firmware + 32, 0xabcd5432u);
    memcpy(firmware + 48, "0.1.2", 6);
    memcpy(firmware + 80, "kui-wifi", 9);
    for(unsigned i = 0; i < 32; ++i) firmware[176u + i] = (uint8_t)(i * 13u + 1u);
}
static void write_file(const char *path, const void *data, size_t bytes) {
    FIL file;
    UINT written;
    assert(f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
    assert(f_write(&file, data, (UINT)bytes, &written) == FR_OK && written == bytes);
    assert(f_close(&file) == FR_OK);
}
static void seed(bool sidecar) {
    FATFS fs;
    assert(!active && !connected);
    kui_media_set(&media);
    assert(kui_mount(&fs, log_line));
    FRESULT result = f_mkdir("0:/KUI");
    assert(result == FR_OK || result == FR_EXIST);
    result = f_mkdir("0:/KUI/firmware");
    assert(result == FR_OK || result == FR_EXIST);
    write_file(IMAGE_PATH, firmware, sizeof(firmware));
    if(sidecar && !keep_sidecar) {
        struct kui_sha256 hash;
        uint8_t digest[32];
        char text[67];
        kui_sha256_init(&hash);
        kui_sha256_update(&hash, firmware, sizeof(firmware));
        kui_sha256_digest(&hash, digest);
        if(corrupt_sidecar) digest[0] ^= 1;
        kui_hex(digest, sizeof(digest), text);
        text[64] = '\r'; text[65] = '\n'; text[66] = 0;
        write_file(IMAGE_PATH ".sha256", text, 66);
    } else if(!keep_sidecar) {
        result = f_unlink(IMAGE_PATH ".sha256");
        assert(result == FR_OK || result == FR_NO_FILE);
    }
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    kui_media_set(NULL);
}
static void model(const struct wifi_model_ota_faults *faults) {
    struct wifi_model_options options = {false, 0, 0, true, KWM_WIFI_IDLE};
    wifi_model_start(&options);
    wifi_model_firmware(5, "0.1.1-9faf7156");
    wifi_model_ota_configure(faults);
    memset(&last, 0, sizeof(last));
    cancel_before = cancel_writing = cancel_committing = false;
    change_stream = changed_stream = false;
    writes = connects = disconnects = publications = 0;
}
static void run(struct kui_wifi_request *request, struct kui_wifi_view *view) {
    active = true;
    kui_wifi_update_run(&wifi_model_port, request, view, log_line, cancel, publish);
    active = false;
    assert(!connected && connects == disconnects);
    assert(!writes);
}
static struct kui_wifi_request preview(struct kui_wifi_view *view) {
    struct kui_wifi_request request = {.action = KUI_WIFI_UPDATE_CHECK};
    run(&request, view);
    assert(view->found && view->firmware_ready && !view->failed);
    assert(view->firmware.bytes == IMAGE_BYTES && view->firmware.chip == 5);
    assert(!strcmp(view->firmware.version, "0.1.2-010e1b28"));
    assert(!strcmp(view->firmware.path, IMAGE_PATH + 2));
    assert(!wifi_model_ota_result()->begins && !wifi_model_ota_result()->writes && !wifi_model_ota_result()->reboots);
    request.action = KUI_WIFI_UPDATE;
    request.firmware = view->firmware;
    return request;
}
static void header_validation(void) {
    struct kui_wifi_firmware out = {.bytes = 77, .path = "kept"};
    assert(kui_wifi_firmware_header(firmware, KUI_WIFI_FIRMWARE_HEADER, 5, &out));
    assert(out.chip == 5 && out.bytes == 77 && !strcmp(out.path, "kept"));
    assert(!strcmp(out.version, "0.1.2-010e1b28"));
    assert(!kui_wifi_firmware_header(firmware, KUI_WIFI_FIRMWARE_HEADER - 1u, 5, &out));
    assert(!kui_wifi_firmware_header(firmware, KUI_WIFI_FIRMWARE_HEADER, 6, &out));
    uint8_t copy[KUI_WIFI_FIRMWARE_HEADER];
    const unsigned corrupt[] = {0, 1, 12, 23, 32, 48, 80};
    for(unsigned i = 0; i < sizeof(corrupt) / sizeof(corrupt[0]); ++i) {
        memcpy(copy, firmware, sizeof(copy));
        copy[corrupt[i]] = corrupt[i] == 48 ? 0 : 0xff;
        assert(!kui_wifi_firmware_header(copy, sizeof(copy), 5, &out));
    }
    memcpy(copy, firmware, sizeof(copy));
    memset(copy + 48, 'x', 32);
    assert(!kui_wifi_firmware_header(copy, sizeof(copy), 5, &out));
    memcpy(copy, firmware, sizeof(copy));
    copy[12] = 13;
    assert(kui_wifi_firmware_header(copy, sizeof(copy), 6, &out) && out.chip == 6);
    puts("PASS Wi-Fi firmware header: chip, app identity, descriptor bounds/version and target HELLO");
}
static void successful_update(bool noisy, bool cancel_after_commit, unsigned startup_ms) {
    struct wifi_model_ota_faults faults = {.corrupt_frame_once = noisy, .reboot_silent_ms = startup_ms};
    model(&faults);
    struct kui_wifi_view view;
    struct kui_wifi_request request = preview(&view);
    cancel_committing = cancel_after_commit;
    run(&request, &view);
    const struct wifi_model_ota_result *result = wifi_model_ota_result();
    if(view.failed) fprintf(stderr, "Update failed: %s\n", view.message);
    assert(!view.failed && !view.working && !view.firmware_updating && !view.firmware_committing);
    assert(result->begins == 1 && result->writes > 1 && result->ends == 1 && result->reboots == 1);
    assert(result->committed && result->checksum_ok && result->written == IMAGE_BYTES);
    assert(!memcmp(result->expected_sha, request.firmware.sha256, 32));
    assert(!memcmp(result->actual_sha, request.firmware.sha256, 32));
    assert(!memcmp(wifi_model_ota_bytes(), firmware, IMAGE_BYTES));
    assert(strstr(view.board, "0.1.2-010e1b28") || strstr(view.message, "0.1.2-010e1b28"));
    assert(publications > 3);
    wifi_model_stop();
}
static void refused_images(void) {
    struct kui_wifi_view view;
    struct kui_wifi_request request = {.action = KUI_WIFI_UPDATE_CHECK};
    model(NULL);
    firmware[80] = 'X'; seed(true);
    run(&request, &view);
    assert(view.failed && !view.firmware_ready && !wifi_model_ota_result()->begins);
    wifi_model_stop(); image_fixture(); seed(true);

    model(NULL);
    corrupt_sidecar = true; seed(true);
    request.action = KUI_WIFI_UPDATE_CHECK;
    run(&request, &view);
    assert(view.failed && !view.firmware_ready && !wifi_model_ota_result()->begins);
    wifi_model_stop(); corrupt_sidecar = false; seed(true);

    model(NULL);
    request = preview(&view);
    firmware[1000] ^= 1; keep_sidecar = true; seed(true);
    run(&request, &view);
    assert(view.failed && !wifi_model_ota_result()->begins && strstr(view.message, "SHA-256"));
    wifi_model_stop(); keep_sidecar = false; image_fixture(); seed(true);

    model(NULL);
    request = preview(&view);
    firmware[1000] ^= 1; seed(false); /* Missing sidecar is refused. */
    run(&request, &view);
    assert(view.failed && !wifi_model_ota_result()->begins);
    wifi_model_stop(); image_fixture(); seed(true);

    model(NULL);
    request = preview(&view);
    firmware[1000] ^= 1; seed(true); /* A newly valid file is still a different preview. */
    run(&request, &view);
    assert(view.failed && !wifi_model_ota_result()->begins);
    wifi_model_stop(); image_fixture(); seed(true);

    model(NULL);
    request = preview(&view);
    request.firmware.mac[0] ^= 1;
    run(&request, &view);
    assert(view.failed && !wifi_model_ota_result()->begins);
    wifi_model_stop();
    puts("PASS Wi-Fi updater: wrong app, missing/mismatched digest, changed image and changed adapter refused before flash");
}
static void changed_during_transfer(void) {
    model(NULL);
    struct kui_wifi_view view;
    struct kui_wifi_request request = preview(&view);
    change_stream = true;
    run(&request, &view);
    const struct wifi_model_ota_result *result = wifi_model_ota_result();
    assert(changed_stream && view.failed && result->begins == 1 && result->writes);
    assert(!result->ends && !result->reboots && !result->committed && !result->open && result->aborts == 1);
    wifi_model_stop();
    puts("PASS Wi-Fi updater: SD bytes changing after preview/hash are rejected before activation");
}
static void failures(void) {
    const struct wifi_model_ota_faults cases[] = {
        {.begin_status = 2}, {.data_status = 2}, {.wrong_phase = true}, {.wrong_written = true},
        {.corrupt_image = true}, {.end_status = 4}, {.reset_after = 2048},
        {.rollback = true}, {.wrong_version = true},
    };
    for(unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        model(&cases[i]);
        struct kui_wifi_view view;
        struct kui_wifi_request request = preview(&view);
        run(&request, &view);
        const struct wifi_model_ota_result *result = wifi_model_ota_result();
        assert(view.failed && !view.working && !view.firmware_updating);
        assert(result->begins == 1);
        if(i < 7) assert(!result->reboots && !result->committed);
        else assert(result->committed && result->reboots == 1);
        wifi_model_stop();
    }
    puts("PASS Wi-Fi updater: flash errors, reply phase/offset, checksum/image errors, reset, rollback and wrong target HELLO");
}
static void cancellation(void) {
    for(unsigned i = 0; i < 2; ++i) {
        model(NULL);
        struct kui_wifi_view view;
        struct kui_wifi_request request = preview(&view);
        cancel_before = i == 0;
        cancel_writing = i == 1;
        run(&request, &view);
        const struct wifi_model_ota_result *result = wifi_model_ota_result();
        assert(!result->committed && !result->reboots && !result->open);
        if(i == 0) assert(!result->begins && !result->writes);
        else assert(result->begins == 1 && result->written > 0 && result->written < IMAGE_BYTES && result->aborts == 1);
        wifi_model_stop();
    }
    puts("PASS Wi-Fi updater: cancel before write and during transfer abort inactive slot without reboot");
}
static void uncertain_commit_and_restart_timeout(void) {
    const struct wifi_model_ota_faults cases[] = {{.lose_end_reply = true}, {.never_returns = true}};
    for(unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        model(&cases[i]);
        struct kui_wifi_view view;
        struct kui_wifi_request request = preview(&view);
        run(&request, &view);
        const struct wifi_model_ota_result *result = wifi_model_ota_result();
        assert(view.failed && result->committed && result->checksum_ok && result->ends == 1);
        assert(!result->aborts && !result->open);
        if(!i) assert(!result->reboots && strstr(view.message, "not confirmed"));
        else assert(result->reboots == 1 && strstr(view.message, "did not return"));
        wifi_model_stop();
    }
    puts("PASS Wi-Fi updater: lost activation answer and actual SPI silence after reboot report uncertain state/timeouts");
}
int main(int argc, char **argv) {
    image_fixture();
    if(argc == 2 && !strcmp(argv[1], "--header")) { header_validation(); return 0; }
    if(argc != 2) return 2;
    struct stat st;
    assert(!stat(argv[1], &st) && st.st_size > 0 && st.st_size % 512 == 0);
    card_blocks = (uint64_t)st.st_size / 512;
    card = fopen(argv[1], "r+b"); assert(card);
    header_validation();
    seed(true);
    successful_update(false, false, 50);
    successful_update(true, false, 50);
    successful_update(false, true, 50);
    successful_update(false, false, 3300);
    refused_images();
    changed_during_transfer();
    failures();
    cancellation();
    uncertain_commit_and_restart_timeout();
    assert(!fclose(card));
    puts("PASS Wi-Fi updater: real card reads, radio offline, no READY, SHA/byte integrity and slot/reboot lifecycle");
    return 0;
}
