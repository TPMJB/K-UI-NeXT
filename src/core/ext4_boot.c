/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/ext4_boot.h"
#include "kui/boot_volume.h"
#include <ext4.h>
#include <ext4_blockdev.h>
#include <ext4_errno.h>
#include <ext4_types.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

/* One bootstrap worker owns this temporary filesystem. Its device exposes no
 * write/sync forwarding even if future upstream mount cleanup tries to write. */
static struct {
    const struct kui_media_ops *raw;
    struct kui_volume volume;
    kui_cancel_fn cancelled;
    bool was_cancelled, write_attempted;
    uint8_t scratch[1024] __attribute__((aligned(32)));
    uint8_t physical[512] __attribute__((aligned(32)));
    struct ext4_blockdev_iface iface;
    struct ext4_blockdev device;
} boot;
static uint16_t le16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int open_device(struct ext4_blockdev *device) { (void)device; return EOK; }
static int close_device(struct ext4_blockdev *device) { (void)device; return EOK; }
static int read_device(struct ext4_blockdev *device, void *out, uint64_t block, uint32_t count) {
    (void)device;
    if(!out || !count || block >= boot.volume.count || count > boot.volume.count - block)
        return EIO;
    uint8_t *p = out;
    while(count) {
        if(boot.cancelled()) { boot.was_cancelled = true; return EIO; }
        uint32_t n = count > 128 ? 128 : count;
        uint64_t physical = (uint64_t)boot.volume.start + block;
        if(physical > UINT32_MAX || n > UINT64_C(0x100000000) - physical ||
           boot.raw->read(boot.raw->ctx, (uint32_t)physical, n, p)) return EIO;
        p += (size_t)n * 512; block += n; count -= n;
    }
    return EOK;
}
static int deny_write(struct ext4_blockdev *device, const void *data, uint64_t block, uint32_t count) {
    (void)device; (void)data; (void)block; (void)count;
    boot.write_attempted = true;
    return EROFS;
}
static enum kui_ext4_boot_result inspect_super(kui_log_fn log) {
    if(boot.volume.count < 4 || read_device(NULL, boot.scratch, 2, 2)) return KUI_EXT4_BOOT_IO;
    const uint8_t *s = boot.scratch;
    if(le16(s + 56) != 0xef53) return KUI_EXT4_BOOT_NOT_FOUND;
    uint32_t compat = le32(s + 92), incompat = le32(s + 96), ro = le32(s + 100);
    if(le16(s + 58) != EXT4_SUPERBLOCK_STATE_VALID_FS ||
       (incompat & EXT4_FINCOM_RECOVER) || le32(s + 232)) {
        log("ext4 boot: volume needs repair/recovery; unmount cleanly and run e2fsck on your computer");
        return KUI_EXT4_BOOT_INVALID;
    }
    /* Conservative supported set: no encryption, inline data, bigalloc,
     * metadata checksum seeds, MMP, orphan-file or external journal devices.
     * Unknown read-only-compatible flags are rejected too. */
    const uint32_t allowed_compat = EXT4_FCOM_HAS_JOURNAL | EXT4_FCOM_EXT_ATTR |
        EXT4_FCOM_RESIZE_INODE | EXT4_FCOM_DIR_INDEX;
    if((compat & ~allowed_compat) || (incompat & ~EXT4_SUPPORTED_FINCOM) ||
       (ro & ~EXT4_SUPPORTED_FRO_COM) || le32(s + 228)) {
        log("ext4 boot: unsupported features compat=%08" PRIx32 " incompat=%08" PRIx32 " ro=%08" PRIx32,
            compat, incompat, ro);
        return KUI_EXT4_BOOT_INVALID;
    }
    uint32_t shift = le32(s + 24), inode = le16(s + 88);
    if(shift > 2 || le32(s + 28) != shift) return KUI_EXT4_BOOT_INVALID;
    uint32_t bytes = 1024u << shift;
    uint64_t blocks = le32(s + 4);
    if(incompat & EXT4_FINCOM_64BIT) blocks |= (uint64_t)le32(s + 336) << 32;
    if(!blocks || blocks > (uint64_t)boot.volume.count * 512 / bytes ||
       le32(s + 20) != (shift ? 0u : 1u) || !le32(s + 32) ||
       le32(s + 32) > bytes * 8u || !le32(s + 40) || le32(s + 40) > bytes * 8u ||
       inode < 128 || inode > bytes || (inode & (inode - 1u))) return KUI_EXT4_BOOT_INVALID;
    return KUI_EXT4_BOOT_OK;
}
enum kui_ext4_boot_result kui_ext4_boot_read(const struct kui_media_ops *raw,
    struct kui_runtime_image *out, kui_log_fn log, kui_cancel_fn cancelled) {
    if(!out) return KUI_EXT4_BOOT_INVALID;
    *out = (struct kui_runtime_image){0};
    if(!raw || !raw->read || !raw->blocks || !log || !cancelled) return KUI_EXT4_BOOT_INVALID;
    if(cancelled()) return KUI_EXT4_BOOT_CANCELLED;
    memset(&boot, 0, sizeof(boot)); boot.raw = raw; boot.cancelled = cancelled;
    enum kui_boot_volume_result volume = kui_boot_volume_select(raw, &boot.volume);
    if(volume != KUI_BOOT_VOLUME_OK)
        return volume == KUI_BOOT_VOLUME_IO ? KUI_EXT4_BOOT_IO :
            volume == KUI_BOOT_VOLUME_UNSUPPORTED ? KUI_EXT4_BOOT_NOT_FOUND : KUI_EXT4_BOOT_INVALID;
    enum kui_ext4_boot_result result = inspect_super(log);
    if(result != KUI_EXT4_BOOT_OK) return boot.was_cancelled ? KUI_EXT4_BOOT_CANCELLED : result;
    boot.iface = (struct ext4_blockdev_iface){.open=open_device, .close=close_device,
        .bread=read_device, .bwrite=deny_write, .ph_bsize=512,
        .ph_bcnt=boot.volume.count, .ph_bbuf=boot.physical};
    boot.device = (struct ext4_blockdev){.bdif=&boot.iface,
        .part_size=(uint64_t)boot.volume.count * 512};
    if(ext4_device_register(&boot.device, "kui-boot")) return KUI_EXT4_BOOT_IO;
    bool mounted = false, opened = false;
    ext4_file file;
    int err = ext4_mount("kui-boot", "/", true);
    if(err) { result = err == ENOMEM ? KUI_EXT4_BOOT_MEMORY : KUI_EXT4_BOOT_INVALID; goto finish; }
    mounted = true;
    err = ext4_fopen(&file, "/KUI/runtime.kui", "r");
    if(err) { log("ext4 boot: /KUI/runtime.kui missing or unreadable (%d)", err); result=KUI_EXT4_BOOT_RUNTIME; goto finish; }
    opened = true;
    uint8_t header[KUI_RUNTIME_HEADER_BYTES]; size_t got = 0;
    enum kui_runtime_result runtime = KUI_RUNTIME_IO;
    if(ext4_fread(&file, header, sizeof(header), &got) || got != sizeof(header)) goto runtime_error;
    runtime = kui_runtime_header(header, ext4_fsize(&file), &out->info);
    if(runtime != KUI_RUNTIME_OK) goto runtime_error;
    out->data = malloc(out->info.payload_bytes);
    if(!out->data) { result=KUI_EXT4_BOOT_MEMORY; goto finish; }
    log("Loading ext4 runtime %s (%" PRIu32 " bytes)", out->info.build, out->info.payload_bytes);
    uint32_t offset = 0, crc = 0;
    while(offset < out->info.payload_bytes) {
        if(cancelled()) { result=KUI_EXT4_BOOT_CANCELLED; goto finish; }
        size_t n = out->info.payload_bytes - offset; if(n > 32768) n = 32768;
        uint8_t *p = (uint8_t *)out->data + offset;
        if(ext4_fread(&file, p, n, &got) || got != n) { runtime=KUI_RUNTIME_IO; goto runtime_error; }
        crc = kui_crc32(crc, p, n); offset += (uint32_t)n;
    }
    if(crc != out->info.crc32) { runtime=KUI_RUNTIME_CHECKSUM; goto runtime_error; }
    result = KUI_EXT4_BOOT_OK;
    goto finish;
runtime_error:
    log("ext4 boot: %s", kui_runtime_result_name(runtime));
    result = KUI_EXT4_BOOT_RUNTIME;
finish:
    if(opened && ext4_fclose(&file) && result == KUI_EXT4_BOOT_OK) result=KUI_EXT4_BOOT_IO;
    if(mounted && ext4_umount("/") && result == KUI_EXT4_BOOT_OK) result=KUI_EXT4_BOOT_IO;
    if(ext4_device_unregister("kui-boot") && result == KUI_EXT4_BOOT_OK) result=KUI_EXT4_BOOT_IO;
    if(boot.write_attempted) result=KUI_EXT4_BOOT_INVALID;
    if(boot.was_cancelled || cancelled()) result=KUI_EXT4_BOOT_CANCELLED;
    if(result != KUI_EXT4_BOOT_OK) kui_runtime_free(out);
    boot.raw = NULL;
    return result;
}
