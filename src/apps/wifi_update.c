/* SPDX-License-Identifier: GPL-3.0-only */
/* SD image -> existing SCI link -> ESP's inactive firmware slot. */
#include "kui/wifi_update.h"
#include "kui/hash.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK 2048u
#define OTA_REPLY_MS 10000u
#define RESTART_MS 20000u

struct update {
    struct kui_wifi_session *session;
    struct kui_wifi_view *out;
    kui_log_fn log;
    kui_cancel_fn cancel;
    kui_wifi_publish_fn publish;
    FATFS fs;
    FIL file;
    bool connected, mounted, opened, begun, committing;
};
static void quiet_log(const char *format, ...) { (void)format; }
static void tell(struct update *u, bool failed, const char *text) {
    snprintf(u->out->message, sizeof(u->out->message), "%s", text);
    u->out->failed = failed;
    if(u->log) u->log("Wi-Fi firmware: %s", text);
    if(u->publish) u->publish(u->out);
}
static bool fail(struct update *u, const char *text) { tell(u, true, text); return false; }
static bool stopped(struct update *u) {
    if(!u->committing && u->cancel && u->cancel()) {
        fail(u, "Firmware update stopped before activation");
        return true;
    }
    return false;
}
static uint64_t now(struct update *u) {
    const struct kui_wifi_bus *b = u->session->port->bus;
    return b->now_ms(b->ctx);
}
static void pause_ms(struct update *u, unsigned ms) {
    const struct kui_wifi_bus *b = u->session->port->bus;
    b->pause(b->ctx, ms);
}
static void progress(struct update *u, uint32_t done) {
    u->out->firmware_done = done;
    if(u->publish) u->publish(u->out);
}

bool kui_wifi_firmware_header(const uint8_t *d, size_t bytes, uint8_t chip,
                              struct kui_wifi_firmware *out) {
    if(!d || !out || bytes < KUI_WIFI_FIRMWARE_HEADER || bytes > KUI_WIFI_FIRMWARE_MAX ||
       d[0] != 0xe9 || !d[1] || d[1] > 16 || d[23] != 1) return false;
    unsigned id = chip == 5 ? 23u : chip == 6 ? 13u : 0u;
    if(!id || kwl_get16(d + 12) != id || kwl_get32(d + 32) != 0xabcd5432u ||
       memcmp(d + 80, "kui-wifi\0", 9)) return false;
    const uint8_t *end = memchr(d + 48, 0, 32);
    if(!end || end == d + 48) return false;
    for(const uint8_t *p = d + 48; p < end; ++p) if(*p < 32 || *p > 126) return false;
    out->chip = chip;
    /* Match platform_version(), including the ELF identity rather than
     * accepting a different binary carrying the same version label. */
    snprintf(out->version, sizeof(out->version), "%.20s-%02x%02x%02x%02x", (const char *)d + 48,
        (unsigned)d[176], (unsigned)d[177], (unsigned)d[178], (unsigned)d[179]);
    return true;
}
static int hex(unsigned char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static bool expected_digest(struct update *u, const char *path, uint8_t digest[32]) {
    char sidecar[96];
    snprintf(sidecar, sizeof(sidecar), "0:%s.sha256", path);
    FIL f;
    if(f_open(&f, sidecar, FA_READ) != FR_OK)
        return fail(u, "Missing firmware .bin.sha256 file in /KUI/firmware");
    uint8_t text[66]; UINT got = 0;
    FSIZE_t size = f_size(&f);
    bool ok = size >= 64 && size <= sizeof(text) && f_read(&f, text, (UINT)size, &got) == FR_OK && got == size;
    if(f_close(&f) != FR_OK) ok = false;
    if(ok && size == 65) ok = text[64] == '\n';
    if(ok && size == 66) ok = text[64] == '\r' && text[65] == '\n';
    for(unsigned i = 0; ok && i < 32; ++i) {
        int hi = hex(text[i * 2]), lo = hex(text[i * 2 + 1]);
        if(hi < 0 || lo < 0) ok = false;
        else digest[i] = (uint8_t)(hi * 16 + lo);
    }
    return ok || fail(u, "Cannot read a valid firmware SHA-256 record");
}
static bool close_storage(struct update *u) {
    bool ok = true;
    if(u->opened) { if(f_close(&u->file) != FR_OK) ok = false; u->opened = false; }
    if(u->mounted) { if(f_mount(NULL, "0:", 0) != FR_OK) ok = false; u->mounted = false; }
    if(u->connected) { kui_sd_disconnect(); u->connected = false; }
    return ok;
}
static bool read_image(struct update *u, struct kui_wifi_firmware *image) {
    uint8_t header[KUI_WIFI_FIRMWARE_HEADER], data[CHUNK], expected[32]; UINT got;
    uint8_t chip = u->session->host.hello.chip;
    if(chip != 5 && chip != 6) return fail(u, "This adapter has no supported firmware image");
    memset(image, 0, sizeof(*image));
    snprintf(image->path, sizeof(image->path), "/KUI/firmware/kui-wifi-esp32c%u-update.bin", (unsigned)chip);
    memcpy(image->mac, u->session->host.hello.mac, sizeof(image->mac));
    char path[84]; snprintf(path, sizeof(path), "0:%s", image->path);
    if(f_open(&u->file, path, FA_READ) != FR_OK)
        return fail(u, "Put the adapter's update .bin and .bin.sha256 in /KUI/firmware");
    u->opened = true;
    FSIZE_t bytes = f_size(&u->file);
    if(bytes < KUI_WIFI_FIRMWARE_HEADER || bytes > KUI_WIFI_FIRMWARE_MAX)
        return fail(u, "Firmware image is empty, truncated or too large for the update slot");
    image->bytes = (uint32_t)bytes;
    if(f_read(&u->file, header, sizeof(header), &got) != FR_OK || got != sizeof(header))
        return fail(u, "Cannot read the firmware header from SD");
    if(!kui_wifi_firmware_header(header, sizeof(header), chip, image))
        return fail(u, "Use a K-UI app-only update image for this ESP32 chip");
    if(!expected_digest(u, image->path, expected)) return false;
    if(f_lseek(&u->file, 0) != FR_OK) return fail(u, "Cannot rewind the firmware image");
    u->out->firmware_total = image->bytes;
    tell(u, false, "Checking the firmware image and SHA-256 from SD");
    struct kui_sha256 sha; kui_sha256_init(&sha);
    for(uint32_t off = 0; off < image->bytes;) {
        if(stopped(u)) return false;
        UINT amount = image->bytes - off < CHUNK ? image->bytes - off : CHUNK;
        if(f_read(&u->file, data, amount, &got) != FR_OK || got != amount)
            return fail(u, "Firmware SD read failed; adapter firmware was not activated");
        kui_sha256_update(&sha, data, amount);
        off += amount; progress(u, off);
    }
    kui_sha256_digest(&sha, image->sha256);
    if(memcmp(image->sha256, expected, 32)) return fail(u, "Firmware SHA-256 mismatch; replace the SD update files");
    if(f_lseek(&u->file, 0) != FR_OK) return fail(u, "Cannot rewind the checked firmware image");
    return true;
}
static bool same_image(const struct kui_wifi_firmware *a, const struct kui_wifi_firmware *b) {
    return a->bytes == b->bytes && a->chip == b->chip && !memcmp(a->mac, b->mac, 6) &&
        !memcmp(a->sha256, b->sha256, 32) && !memcmp(a->version, b->version, sizeof(a->version)) &&
        !memcmp(a->path, b->path, sizeof(a->path));
}
static const char *ota_error(uint8_t status) {
    switch(status) {
    case 1: return "Adapter refused firmware size or write offset";
    case 2: return "Adapter firmware flash write failed";
    case 3: return "Adapter rejected the firmware SHA-256";
    case 4: return "Adapter rejected the firmware image";
    case 5: return "Adapter could not select the new firmware";
    default: return "Adapter refused the firmware update";
    }
}
static bool reply(struct update *u, uint32_t before, uint8_t phase, uint32_t written, uint32_t lost) {
    struct kui_wifi_session *s = u->session;
    uint64_t start = now(u);
    for(;;) {
        if(stopped(u)) return false;
        if(!kui_wifi_session_step(s)) return fail(u, s->problem);
        if(s->host.counts.lost != lost) return fail(u, "Adapter restarted during the firmware update");
        if(s->host.counts.ota != before) {
            if(s->host.ota_phase != phase) return fail(u, "Unexpected firmware-update acknowledgment");
            if(s->host.ota_status) return fail(u, ota_error(s->host.ota_status));
            if(s->host.ota_written != written) return fail(u, "Adapter reported an unexpected firmware write offset");
            return true;
        }
        if(now(u) - start > OTA_REPLY_MS) return fail(u, "Adapter firmware-update acknowledgment timed out");
        pause_ms(u, 1);
    }
}
static void abort_update(struct update *u) {
    if(!u->begun || u->committing || !u->session->open) return;
    /* Protocol v1 has no ABORT. A zero-length BEGIN closes the open OTA
     * handle, then rejects the new request without selecting a boot slot. */
    struct kwh *h = &u->session->host;
    uint8_t zero[32] = {0}; uint32_t seen = h->counts.ota;
    if(!kwh_ota_begin(h, 0, zero)) return;
    uint64_t start = now(u);
    while(now(u) - start < 3000u) {
        if(!kui_wifi_session_step(u->session)) break;
        if(h->counts.ota != seen && h->ota_phase == KWM_OTA_BEGIN_PHASE && h->ota_status == 1) break;
        pause_ms(u, 2);
    }
}
static bool restart_adapter(struct update *u, const struct kui_wifi_firmware *image) {
    struct kui_wifi_session *s = u->session;
    struct kwh *h = &s->host;
    uint32_t hello = h->counts.hello, lost = h->counts.lost, observed_lost = lost;
    if(!kwh_reboot(h)) return fail(u, "Firmware selected; adapter restart could not be queued. Power-cycle the console");
    tell(u, false, "Restarting the adapter and checking its new firmware");
    uint64_t start = now(u), verified = 0;
    bool changed = false;
    for(;;) {
        /* Startup may exceed the session's normal three-second silence
         * limit. Keep polling through it; the link negotiates after reset. */
        (void)kui_wifi_session_step(s);
        if(h->counts.lost != observed_lost) {
            changed = true;
            observed_lost = h->counts.lost;
            verified = 0;
        }
        if(changed && h->counts.hello != hello && h->ready) {
            if(h->hello.protocol != KWL_PROTOCOL)
                return fail(u, "Updated adapter uses an incompatible link protocol");
            if(h->hello.chip != image->chip || memcmp(h->hello.mac, image->mac, 6))
                return fail(u, "A different adapter answered after the firmware restart");
            if(strcmp(h->hello.version, image->version))
                return fail(u, "Adapter returned with different firmware; update was not confirmed");
            if(!verified) verified = now(u);
            /* Trial firmware marks the image valid once this SPI session
             * is seen, from its 100 ms startup loop. Keep the link alive. */
            if(now(u) - verified >= 500u) { s->problem[0] = 0; return true; }
        }
        if(now(u) - start >= RESTART_MS)
            return fail(u, "Firmware selected but adapter did not return. Power-cycle the console, then inspect Wi-Fi");
        pause_ms(u, 2);
    }
}
static bool install(struct update *u, const struct kui_wifi_firmware *image) {
    struct kwh *h = &u->session->host;
    uint32_t before = h->counts.ota, lost = h->counts.lost;
    if(stopped(u)) return false;
    if(!kwh_ota_begin(h, image->bytes, image->sha256)) return fail(u, "Cannot queue the firmware update");
    u->begun = true;
    tell(u, false, "Preparing the adapter's second firmware slot");
    if(!reply(u, before, KWM_OTA_BEGIN_PHASE, 0, lost)) return false;
    uint8_t data[CHUNK]; struct kui_sha256 sha; kui_sha256_init(&sha);
    progress(u, 0); tell(u, false, "Sending checked firmware to the adapter; B can stop before activation");
    for(uint32_t off = 0; off < image->bytes;) {
        if(stopped(u)) return false;
        UINT amount = image->bytes - off < CHUNK ? image->bytes - off : CHUNK, got;
        if(f_read(&u->file, data, amount, &got) != FR_OK || got != amount)
            return fail(u, "SD read failed during update; new firmware was not activated");
        kui_sha256_update(&sha, data, amount);
        before = h->counts.ota;
        if(!kwh_ota_data(h, off, data, amount)) return fail(u, "Cannot queue the next firmware block");
        off += amount;
        if(!reply(u, before, KWM_OTA_DATA_PHASE, off, lost)) return false;
        progress(u, off);
    }
    uint8_t digest[32]; kui_sha256_digest(&sha, digest);
    if(memcmp(digest, image->sha256, 32) || f_size(&u->file) != image->bytes)
        return fail(u, "SD firmware changed during transfer; new firmware was not activated");
    if(!close_storage(u)) return fail(u, "Cannot close the firmware SD file; new firmware was not activated");
    if(stopped(u)) return false;
    /* END is the activation boundary. A reply may be lost after selecting
     * the next boot partition; cancellation cannot undo that selection. */
    u->committing = u->out->firmware_committing = true;
    tell(u, false, "Validating and activating firmware; keep the console powered on");
    before = h->counts.ota;
    if(!kwh_ota_end(h)) {
        u->committing = u->out->firmware_committing = false;
        return fail(u, "Cannot queue firmware activation");
    }
    if(!reply(u, before, KWM_OTA_END_PHASE, 0, lost)) {
        if(!h->ota_status || h->counts.ota == before)
            tell(u, true, "Firmware activation was not confirmed. Power-cycle the console, then inspect Wi-Fi");
        return false;
    }
    return restart_adapter(u, image);
}

void kui_wifi_update_run(const struct kui_wifi_port *port, const struct kui_wifi_request *request,
    struct kui_wifi_view *out, kui_log_fn log, kui_cancel_fn cancel, kui_wifi_publish_fn publish) {
    if(!out || !request) return;
    memset(out, 0, sizeof(*out));
    bool commit = request->action == KUI_WIFI_UPDATE;
    out->working = true; out->firmware_updating = commit;
    struct update u = {.out=out, .log=log, .cancel=cancel, .publish=publish};
    struct kui_wifi_firmware image;
    bool ok = false;
    tell(&u, false, "Opening the SD firmware update");
    if(request->action != KUI_WIFI_UPDATE_CHECK && !commit) { fail(&u, "Invalid firmware-update request"); goto done; }
    if(stopped(&u)) goto done;
    if(!kui_sd_connect()) { fail(&u, "SD card unavailable for the firmware update"); goto done; }
    u.connected = true;
    if(!kui_mount(&u.fs, log ? log : quiet_log)) { fail(&u, "Cannot mount the SD firmware update"); goto done; }
    u.mounted = true;
    u.session = calloc(1, sizeof(*u.session));
    if(!u.session) { fail(&u, "Insufficient memory for the firmware updater"); goto done; }
    if(!kui_wifi_session_find(u.session, port, log)) { fail(&u, u.session->problem); goto done; }
    out->found = true;
    snprintf(out->board, sizeof(out->board), "XIAO %s, firmware %.32s",
        kui_wifi_chip_text(u.session->host.hello.chip), u.session->host.hello.version);
    (void)kui_wifi_session_status(u.session, cancel);
    out->wifi = u.session->host.wifi;
    if(stopped(&u) || !read_image(&u, &image)) goto done;
    out->firmware = image;
    if(commit) {
        if(!same_image(&image, &request->firmware)) { fail(&u, "Firmware file or adapter changed; check the update again"); goto done; }
        ok = install(&u, &image);
        if(ok) {
            snprintf(out->board, sizeof(out->board), "XIAO %s, firmware %.32s",
                kui_wifi_chip_text(u.session->host.hello.chip), u.session->host.hello.version);
            (void)kui_wifi_session_status(u.session, NULL);
            out->wifi = u.session->host.wifi;
            tell(&u, false, "Firmware updated and adapter checked; saved Wi-Fi settings are kept");
        }
    } else {
        ok = true;
        tell(&u, false, "Firmware image checked; confirm to install it on the adapter");
    }
done:
    if(!ok) abort_update(&u);
    if(!close_storage(&u) && ok) { ok = false; fail(&u, "Cannot release the SD firmware image"); }
    if(u.session) {
        kui_wifi_session_end(u.session);
        memset(u.session, 0, sizeof(*u.session)); free(u.session);
    }
    out->firmware_ready = ok && !commit;
    out->working = out->firmware_updating = out->firmware_committing = false;
    if(publish) publish(out);
}
