/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/boot_image.h"
#include "kui/boot_volume.h"
#include "kui/ext4_boot.h"
#include "kui/storage_policy.h"

const char *kui_boot_image_mode_path(enum kui_boot_mode mode, unsigned attempt) {
    if(mode == KUI_BOOT_MODE_RECOVERY) return attempt == 0 ? "0:/KUI/recovery.kui" : NULL;
    if(mode == KUI_BOOT_MODE_TOOLS) return attempt == 0 ? "0:/KUI/tools.kui" : NULL;
    if(mode != KUI_BOOT_MODE_NORMAL) return NULL;
    if(attempt == 0) return KUI_RUNTIME_PATH;
    return attempt == 1 ? "0:/KUI/recovery.kui" : NULL;
}
const char *kui_boot_image_path(bool recovery_only, unsigned attempt) {
    return kui_boot_image_mode_path(recovery_only ? KUI_BOOT_MODE_RECOVERY : KUI_BOOT_MODE_NORMAL, attempt);
}
unsigned kui_boot_transport_at(unsigned filter, unsigned attempt) {
    if(filter == KUI_STORAGE_AUTO)
        return attempt < KUI_STORAGE_AUTO ? attempt : KUI_STORAGE_AUTO;
    return filter < KUI_STORAGE_AUTO && attempt == 0 ? filter : KUI_STORAGE_AUTO;
}

/* This runs only after the on-disk header, size and original payload CRC pass.
 * SCIF keeps compatibility with runtimes predating the transport marker. */
static enum kui_runtime_result accept_image(struct kui_runtime_image *image,
    unsigned transport, const char *path, kui_log_fn log,
    kui_cancel_fn cancelled) {
    if(cancelled()) return KUI_RUNTIME_CANCELLED;
    if(!kui_storage_patch_boot(image->data, image->info.payload_bytes, transport) &&
       transport != KUI_STORAGE_SCIF) {
        log("Boot image %s lacks a valid storage handoff", path + 2);
        return KUI_RUNTIME_VERSION_ERROR;
    }
    return KUI_RUNTIME_OK;
}

static bool stop_attempts(enum kui_runtime_result result) {
    return result == KUI_RUNTIME_OK || result == KUI_RUNTIME_CANCELLED ||
        result == KUI_RUNTIME_MEMORY;
}

static enum kui_runtime_result read_fat_image(const struct kui_media_ops *raw,
    const struct kui_volume *volume, unsigned transport, const char *path,
    struct kui_runtime_image *out, kui_log_fn log, kui_cancel_fn cancelled) {
    FATFS fs;
    enum kui_runtime_result result = KUI_RUNTIME_IO;
    if(!kui_media_boot_view(raw, volume)) goto finish;
    if(cancelled()) { result = KUI_RUNTIME_CANCELLED; goto finish; }
    if(!kui_mount(&fs, log)) goto finish;
    result = kui_runtime_read(path, out, log, cancelled);
    if(result == KUI_RUNTIME_OK)
        result = accept_image(out, transport, path, log, cancelled);
finish:
    /* Even a failed FatFs mount may have registered the stack-local FATFS. */
    if(f_mount(NULL, "0:", 0) != FR_OK && result == KUI_RUNTIME_OK)
        result = KUI_RUNTIME_IO;
    kui_media_set(NULL);
    if(cancelled()) result = KUI_RUNTIME_CANCELLED;
    if(result != KUI_RUNTIME_OK) kui_runtime_free(out);
    return result;
}

static enum kui_runtime_result read_fat(const struct kui_media_ops *raw,
    const struct kui_volume *volume, unsigned transport, enum kui_boot_mode mode,
    struct kui_runtime_image *out, kui_log_fn log, kui_cancel_fn cancelled) {
    enum kui_runtime_result result = KUI_RUNTIME_IO;
    for(unsigned attempt = 0;; ++attempt) {
        const char *path = kui_boot_image_mode_path(mode, attempt);
        if(!path) break;
        if(cancelled()) return KUI_RUNTIME_CANCELLED;
        /* A read error poisons diskio's current session. Recovery is a new
         * read-only attempt, with a fresh bounded view and filesystem mount,
         * so a transient failure in the runtime file cannot hide recovery. */
        result = read_fat_image(raw, volume, transport, path, out, log, cancelled);
        if(stop_attempts(result)) break;
        log("Boot image %s rejected: %s", path + 2, kui_runtime_result_name(result));
    }
    return result;
}

static enum kui_runtime_result read_ext4(const struct kui_media_ops *raw,
    unsigned transport, enum kui_boot_mode mode, struct kui_runtime_image *out,
    kui_log_fn log, kui_cancel_fn cancelled, bool *not_found) {
    enum kui_runtime_result result = KUI_RUNTIME_IO;
    *not_found = false;
    for(unsigned attempt = 0;; ++attempt) {
        const char *path = kui_boot_image_mode_path(mode, attempt);
        if(!path) break;
        if(cancelled()) { result = KUI_RUNTIME_CANCELLED; break; }
        enum kui_ext4_boot_result ext4 =
            kui_ext4_boot_read_path(raw, path + 2, out, log, cancelled);
        switch(ext4) {
            case KUI_EXT4_BOOT_OK:
                result = accept_image(out, transport, path, log, cancelled);
                break;
            case KUI_EXT4_BOOT_CANCELLED: result = KUI_RUNTIME_CANCELLED; break;
            case KUI_EXT4_BOOT_MEMORY: result = KUI_RUNTIME_MEMORY; break;
            default: result = KUI_RUNTIME_IO; break;
        }
        if(result != KUI_RUNTIME_OK) kui_runtime_free(out);
        if(stop_attempts(result)) break;
        /* Changing filenames cannot repair an absent/unsafe filesystem. */
        if(ext4 == KUI_EXT4_BOOT_NOT_FOUND) { *not_found = true; break; }
        if(ext4 == KUI_EXT4_BOOT_INVALID) break;
        log("Boot image %s rejected: %s", path + 2, kui_runtime_result_name(result));
    }
    if(cancelled()) result = KUI_RUNTIME_CANCELLED;
    if(result != KUI_RUNTIME_OK) kui_runtime_free(out);
    return result;
}

enum kui_runtime_result kui_boot_image_read_mode(const struct kui_media_ops *raw,
    unsigned transport, enum kui_boot_mode mode, struct kui_runtime_image *out,
    kui_log_fn log, kui_cancel_fn cancelled) {
    if(!out) return KUI_RUNTIME_IO;
    *out = (struct kui_runtime_image){0};
    if(!raw || !raw->blocks || !raw->read || !log || !cancelled ||
       transport >= KUI_STORAGE_AUTO || !kui_boot_image_mode_path(mode, 0)) return KUI_RUNTIME_IO;
    if(cancelled()) return KUI_RUNTIME_CANCELLED;
    struct kui_boot_layout layout;
    enum kui_boot_volume_result scan = kui_boot_volume_scan(raw, &layout);
    if(cancelled()) return KUI_RUNTIME_CANCELLED;
    if(scan != KUI_BOOT_VOLUME_OK) {
        log("Boot volume layout rejected (%u)", (unsigned)scan);
        return KUI_RUNTIME_IO;
    }
    const struct kui_boot_candidate *fat = NULL, *linux_volume = NULL, *whole = NULL;
    if(layout.count > 2) return KUI_RUNTIME_IO;
    for(unsigned i = 0; i < layout.count; ++i) {
        const struct kui_boot_candidate *candidate = &layout.candidates[i];
        switch(candidate->kind) {
            case KUI_BOOT_VOLUME_FAT_CANDIDATE: fat = candidate; break;
            case KUI_BOOT_VOLUME_LINUX_CANDIDATE: linux_volume = candidate; break;
            case KUI_BOOT_VOLUME_RAW: whole = candidate; break;
            default: return KUI_RUNTIME_IO;
        }
    }
    /* A boot partition is authoritative. Never bypass its failed/missing
     * recovery file by loading a different payload from the ext4 data volume. */
    if(fat)
        return read_fat(raw, &fat->volume, transport, mode, out, log, cancelled);
    enum kui_runtime_result fat_result = KUI_RUNTIME_IO;
    if(whole) {
        fat_result = read_fat(raw, &whole->volume, transport, mode, out, log, cancelled);
        if(stop_attempts(fat_result)) return fat_result;
    }
    if(whole || linux_volume) {
        bool not_found;
        enum kui_runtime_result result =
            read_ext4(raw, transport, mode, out, log, cancelled, &not_found);
        /* Keep a precise FatFs image error when the raw disk simply is not
         * ext4, instead of replacing it with a filesystem-probe error. */
        return not_found && result == KUI_RUNTIME_IO ? fat_result : result;
    }
    return KUI_RUNTIME_IO;
}
enum kui_runtime_result kui_boot_image_read(const struct kui_media_ops *raw,
    unsigned transport, bool recovery_only, struct kui_runtime_image *out,
    kui_log_fn log, kui_cancel_fn cancelled) {
    return kui_boot_image_read_mode(raw, transport,
        recovery_only ? KUI_BOOT_MODE_RECOVERY : KUI_BOOT_MODE_NORMAL, out, log, cancelled);
}
