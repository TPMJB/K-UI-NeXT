/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include <dc/syscalls.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <inttypes.h>
#include <string.h>

/* All firmware-visible objects survive a failed abort. Once poisoned, this
 * adapter will not touch them again, and requires a console reset. */
static bool initialized, poisoned, media_changed;
static cd_read_params_t read_params;
static cd_cmd_toc_params_t toc_params;
static cd_cmd_chk_status_t detail;
static struct { uint8_t before[32]; cd_toc_t data; uint8_t after[32]; } toc;
static _Alignas(32) struct { uint8_t before[32], data[KUI_RAW_BYTES], after[32]; } raw[2];
static _Alignas(32) struct { uint8_t before[32], data[KUI_DATA_BYTES], after[32]; } cooked[2];
static _Alignas(32) struct {
    uint8_t before[32], data[KUI_CAPTURE_CHUNK*KUI_RAW_BYTES], after[32];
} capture_raw[2];

static uint64_t now(void *ctx) { (void)ctx; return timer_ms_gettime64(); }
static void pause_worker(void *ctx) { (void)ctx; thd_sleep(1); }
static bool cancelled(void *ctx) { (void)ctx; return kui_cancelled(); }
static int submit(void *ctx, int command, void *params) {
    (void)ctx;
    int handle = syscall_gdrom_send_command((cd_cmd_code_t)command, params);
    syscall_gdrom_exec_server();
    return handle;
}
static int poll(void *ctx, int handle) {
    (void)ctx;
    syscall_gdrom_exec_server();
    return syscall_gdrom_check_command(handle, &detail);
}
static void abort_command(void *ctx, int handle) {
    (void)ctx; syscall_gdrom_abort_command(handle);
}
static bool command(int code, void *params, uint32_t timeout) {
    if(poisoned || kui_cancelled()) return false;
    memset(&detail, 0, sizeof(detail));
    struct kui_command_ops ops = {NULL, now, pause_worker, cancelled, submit, poll, abort_command};
    enum kui_command_result result = kui_command(&ops, code, params, timeout, 1000);
    if(result == KUI_CMD_OK) return true;
    kui_log("CMD %d %s", code, kui_command_name(result));
    kui_log("  sense=%" PRId32 "/%" PRId32 " size=%lu ATA=%d",
        detail.err1, detail.err2, (unsigned long)detail.size, detail.ata);
    if(result == KUI_CMD_RECOVERY_FAILED) poisoned = true;
    if(detail.err1 == 6 || detail.err1 == 2) media_changed = true;
    return false;
}

static bool set_mode(int size, int track_type) {
    if(poisoned || kui_cancelled()) return false;
    cd_sec_mode_params_t mode = {0,
        size == 2352 ? CDROM_READ_WHOLE_SECTOR : CDROM_READ_DATA_AREA,
        track_type, size};
    int r = syscall_gdrom_sector_mode(&mode);
    if(r != 0) kui_log("Set sector mode failed: %d", r);
    return r == 0;
}
static bool read_sector(void *data, uint32_t fad) {
    read_params = (cd_read_params_t){.start_sec = fad, .num_sec = 1,
                                   .buffer = data, .is_test = 0};
    return command(CD_CMD_PIOREAD, &read_params, 5000);
}

static bool sample(const struct kui_track *track, uint32_t fad) {
    uint32_t lba;
    if(!kui_fad_to_lba(fad, &lba)) return false;
    kui_log("Track %02u FAD=%" PRIu32 " LBA=%" PRIu32, track->number, fad, lba);
    if(!set_mode(2352, 0)) return false;
    /* Two different initial fills reveal bytes left untouched by a short
     * transfer. Guards detect nearby overwrite. Repeat equality is evidence
     * of coverage/stability, not a full CD integrity or reference check. */
    for(unsigned i = 0; i < 2; ++i) {
        memset(&raw[i], i ? 0x5a : 0xa5, sizeof(raw[i]));
        if(!read_sector(raw[i].data, fad)) return false;
        uint8_t fill = i ? 0x5a : 0xa5;
        if(!kui_guard_is(raw[i].before, 32, fill) ||
           !kui_guard_is(raw[i].after, 32, fill)) {
            kui_log("RAW GUARD CORRUPTION: RESET REQUIRED"); poisoned = true;
            return false;
        }
    }
    if(memcmp(raw[0].data, raw[1].data, KUI_RAW_BYTES)) {
        kui_log("Raw reads differ: underfill, jitter or unstable data"); return false;
    }
    kui_log("  raw repeat/guards OK CRC32=%08" PRIx32,
        kui_crc32(0, raw[0].data, KUI_RAW_BYTES));
    if(!(track->control & 4)) {
        kui_log("  audio sample (no offset/subchannel validation)"); return true;
    }
    int offset = kui_data_offset(raw[0].data);
    if(offset < 0) { kui_log("  unsupported/malformed data-sector layout"); return false; }
    kui_log("  raw mode=%u header=%02x:%02x:%02x", raw[0].data[15],
        raw[0].data[12], raw[0].data[13], raw[0].data[14]);
    if(!set_mode(2048, offset == 16 ? 1024 : 2048)) return false;
    for(unsigned i = 0; i < 2; ++i) {
        memset(&cooked[i], i ? 0x5a : 0xa5, sizeof(cooked[i]));
        if(!read_sector(cooked[i].data, fad)) return false;
        uint8_t fill = i ? 0x5a : 0xa5;
        if(!kui_guard_is(cooked[i].before, 32, fill) ||
           !kui_guard_is(cooked[i].after, 32, fill)) {
            kui_log("COOKED GUARD CORRUPTION: RESET REQUIRED"); poisoned = true;
            return false;
        }
    }
    if(memcmp(cooked[0].data, cooked[1].data, KUI_DATA_BYTES) ||
       memcmp(raw[0].data + offset, cooked[0].data, KUI_DATA_BYTES)) {
        kui_log("  raw/cooked payload mismatch"); return false;
    }
    kui_log("  cooked repeat/guards and raw payload MATCH");
    return true;
}

bool kui_disc_prepare(struct kui_toc sessions[2]) {
    if(poisoned) { kui_log("Drive unavailable after failed recovery; reset console"); return false; }
    if(!initialized) { kui_drive_init_bus(); initialized = true; }
    bool ready = false;
    /* Two additional INIT attempts for disc-change sense only. */
    for(unsigned attempt = 0; attempt < 3 && !kui_cancelled(); ++attempt) {
        if(command(CD_CMD_INIT, NULL, 10000)) { ready = true; break; }
        if(poisoned || detail.err1 != 6) break;
    }
    if(!ready) { kui_log("Disc initialization did not complete"); return false; }
    media_changed = false;
    for(unsigned area = 0; area < 2; ++area) {
        memset(&toc, 0xa5, sizeof(toc));
        toc_params = (cd_cmd_toc_params_t){(cd_area_t)area, &toc.data};
        if(!command(CD_CMD_GETTOC2, &toc_params, 5000)) return false;
        if(!kui_guard_is(toc.before, 32, 0xa5) || !kui_guard_is(toc.after, 32, 0xa5)) {
            kui_log("TOC GUARD CORRUPTION: RESET REQUIRED"); poisoned = true; return false;
        }
        kui_log("%s TOC first=%08" PRIx32 " last=%08" PRIx32 " end=%08" PRIx32,
            area ? "HIGH" : "LOW", toc.data.first, toc.data.last, toc.data.leadout_sector);
        if(!kui_parse_toc(toc.data.entry, toc.data.first, toc.data.last,
                           toc.data.leadout_sector, &sessions[area])) {
            kui_log("TOC invalid or unsupported; no sample reads issued"); return false;
        }
        for(unsigned j = 0; j < sessions[area].count; ++j) {
            const struct kui_track *t = &sessions[area].tracks[j];
            kui_log("  T%02u ctrl=%x start=%" PRIu32 " next=%" PRIu32,
                t->number, t->control, t->start, t->end);
        }
    }
    return true;
}

enum kui_read_result kui_disc_read_raw(void *ctx,uint32_t fad,unsigned sectors,uint8_t *out) {
    (void)ctx;
    if(!out || !sectors || sectors>KUI_CAPTURE_CHUNK || fad<150 || fad>0xffffff ||
       sectors>0x1000000u-fad || poisoned || media_changed || kui_cancelled()) return KUI_READ_FATAL;
    if(!set_mode(2352,0)) return KUI_READ_FATAL;
    size_t bytes=(size_t)sectors*KUI_RAW_BYTES;
    for(unsigned i=0;i<2;i++) {
        uint8_t fill=i?0x5a:0xa5;
        memset(&capture_raw[i],fill,sizeof(capture_raw[i]));
        read_params=(cd_read_params_t){.start_sec=fad,.num_sec=sectors,.buffer=capture_raw[i].data,.is_test=0};
        if(!command(CD_CMD_PIOREAD,&read_params,5000))
            return poisoned || media_changed || kui_cancelled()?KUI_READ_FATAL:KUI_READ_RETRY;
        if(!kui_guard_is(capture_raw[i].before,32,fill) ||
           !kui_guard_is(capture_raw[i].after,32,fill) ||
           !kui_guard_is(capture_raw[i].data+bytes,sizeof(capture_raw[i].data)-bytes,fill)) {
            poisoned=true;kui_log("CAPTURE GUARD CORRUPTION: RESET REQUIRED");return KUI_READ_FATAL;
        }
    }
    if(memcmp(capture_raw[0].data,capture_raw[1].data,bytes)) {
        kui_log("Raw repeat mismatch at FAD=%" PRIu32,fad);return KUI_READ_RETRY;
    }
    memcpy(out,capture_raw[0].data,bytes);return KUI_READ_OK;
}

void kui_disc_probe(void) {
    kui_log("DISC PROBE: insert a known-good retail GD-ROM first");
    struct kui_toc sessions[2];
    if(!kui_disc_prepare(sessions)) return;
    unsigned passed = 0;
    for(unsigned area = 0; area < 2; ++area) {
        for(unsigned i = 0; i < sessions[area].count; ++i) {
            const struct kui_track *track = &sessions[area].tracks[i];
            uint32_t points[3];
            unsigned count = kui_sample_points(track, points);
            for(unsigned j = 0; j < count; ++j) {
                if(kui_cancelled() || !sample(track, points[j])) {
                    kui_log("DISC PROBE INCOMPLETE: %u samples passed", passed); return;
                }
                ++passed;
            }
        }
    }
    kui_log("DISC PROBE PASS: both TOCs, %u samples", passed);
    kui_log("Samples only; full-disc accuracy and compatibility unproven");
}
