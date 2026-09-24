/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_metadata.h"

#include <stddef.h>
#include <string.h>

/* Format references, not imported implementations:
 * KallistiOS fcfa7d869471591ca1c777543261a7bfea7cb726:
 * utils/makeip/src/field.c (IP fixed-field offsets), and
 * kernel/arch/dreamcast/fs/fs_iso9660.c (absolute extent LBAs, session PVD).
 * ISO9660/ECMA-119 numbers 723/733 store both byte orders; validate both.
 * cdrkit genisoimage/write.c writes volume_space_size as
 * last_extent - session_start, despite absolute extent locations. Do not treat
 * that count as an absolute end LBA or add a session offset to any extent.
 */
static bool both16(const uint8_t *p, uint16_t *out) {
    uint16_t le = (uint16_t)(p[0] | (uint16_t)p[1] << 8);
    uint16_t be = (uint16_t)((uint16_t)p[2] << 8 | p[3]);
    *out = le;
    return le == be;
}
static bool both32(const uint8_t *p, uint32_t *out) {
    uint32_t le = (uint32_t)p[0] | (uint32_t)p[1] << 8 |
                  (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    uint32_t be = (uint32_t)p[4] << 24 | (uint32_t)p[5] << 16 |
                  (uint32_t)p[6] << 8 | p[7];
    *out = le;
    return le == be;
}
static void field(char *out, const uint8_t *data, size_t size) {
    size_t len = 0;
    while(len < size && data[len]) {
        out[len] = data[len] >= 32 && data[len] <= 126 ? (char)data[len] : '?';
        ++len;
    }
    while(len && out[len - 1] == ' ') --len;
    out[len] = 0;
    size_t first = 0;
    while(first < len && out[first] == ' ') ++first;
    if(first) memmove(out, out + first, len - first + 1);
}
static bool root_filename(const char *name) {
    size_t n = strlen(name);
    if(!n || !strcmp(name, ".") || !strcmp(name, "..")) return false;
    for(size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)name[i];
        if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
             (c >= '0' && c <= '9') || c == '_' || c == '.')) return false;
    }
    return true;
}
static void boot_profile(const uint8_t *ip, struct kui_game_metadata *out) {
    /* IP.BIN peripheral field: seven hexadecimal digits at 0x38; bit zero
     * selects Windows CE. Raw GD executable bytes need no MIL-CD transform. */
    if(memcmp(ip + 37, "GD-ROM", 6) || ip[63] != ' ') return;
    uint32_t flags = 0;
    for(unsigned i = 56; i < 63; ++i) {
        unsigned digit = ip[i];
        if(digit >= '0' && digit <= '9') digit -= '0';
        else if(digit >= 'A' && digit <= 'F') digit = digit - 'A' + 10u;
        else if(digit >= 'a' && digit <= 'f') digit = digit - 'a' + 10u;
        else return;
        flags = (flags << 4) | digit;
    }
    out->windows_ce = (flags & 1u) != 0;
    out->native_gd = !out->windows_ce;
}
static unsigned fold(unsigned c) {
    return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c;
}
static bool boot_name(const uint8_t *name, unsigned size, const char *wanted) {
    size_t len = strlen(wanted);
    if(size != len && size != len + 2) return false;
    if(size == len + 2 && (name[len] != ';' || name[len + 1] != '1')) return false;
    for(size_t i = 0; i < len; ++i)
        if(fold(name[i]) != fold((unsigned char)wanted[i])) return false;
    return true;
}
static enum kui_game_metadata_status sector(const struct kui_game_metadata_ops *ops,
    uint32_t lba, uint8_t *data, struct kui_game_metadata *out) {
    switch(ops->read_sector(ops->ctx, lba, data)) {
    case KUI_GAME_METADATA_IO_OK:
        ++out->sectors_read;
        return KUI_GAME_METADATA_OK;
    case KUI_GAME_METADATA_IO_CANCELLED: return KUI_GAME_METADATA_CANCELLED;
    default: return KUI_GAME_METADATA_IO;
    }
}
static bool extent_valid(const struct kui_game_metadata_ops *ops, uint32_t lba,
                         uint32_t bytes, uint32_t volume_blocks) {
    uint64_t blocks = ((uint64_t)bytes + 2047u) / 2048u;
    return bytes && blocks <= volume_blocks && (uint64_t)lba + blocks <= UINT32_MAX &&
        ops->data_range(ops->ctx, lba, (uint32_t)blocks);
}
struct record {
    uint32_t lba, bytes;
    uint8_t flags, length, name_length;
    const uint8_t *name;
    bool extended;
};
static bool record_read(const uint8_t *p, size_t available, struct record *out) {
    uint16_t sequence;
    if(available < 34 || p[0] < 34 || p[0] > available || (p[0] & 1) ||
       !p[32] || 33u + p[32] + ((p[32] & 1u) ? 0u : 1u) > p[0] ||
       !both32(p + 2, &out->lba) || !both32(p + 10, &out->bytes) ||
       !both16(p + 28, &sequence) || sequence != 1) return false;
    out->flags = p[25];
    out->length = p[0];
    out->name_length = p[32];
    out->name = p + 33;
    out->extended = p[1] || p[26] || p[27] || (p[25] & (4u | 8u | 16u | 32u | 64u | 128u));
    return true;
}
static enum kui_game_metadata_status directory(const struct kui_game_metadata_ops *ops,
    struct kui_game_metadata *out, uint8_t *data) {
    bool found = false;
    uint32_t boot_lba = 0, boot_bytes = 0;
    for(uint32_t done = 0; done < out->root_bytes;) {
        enum kui_game_metadata_status result = sector(ops, out->root_lba + done / 2048u, data, out);
        if(result != KUI_GAME_METADATA_OK) return result;
        size_t amount = out->root_bytes - done;
        if(amount > 2048) amount = 2048;
        for(size_t pos = 0; pos < amount;) {
            if(!data[pos]) {
                for(size_t pad = pos; pad < amount; ++pad)
                    if(data[pad]) return KUI_GAME_METADATA_ISO;
                break;
            }
            struct record entry;
            if(!record_read(data + pos, amount - pos, &entry)) return KUI_GAME_METADATA_ISO;
            if(boot_name(entry.name, entry.name_length, out->bootfile)) {
                if(found) return KUI_GAME_METADATA_ISO;
                if((entry.flags & 2u) || entry.extended) return KUI_GAME_METADATA_UNSUPPORTED;
                if(entry.bytes > KUI_GAME_METADATA_MAX_BOOT_BYTES) return KUI_GAME_METADATA_LIMIT;
                if(!extent_valid(ops, entry.lba, entry.bytes, out->volume_blocks))
                    return KUI_GAME_METADATA_ISO;
                found = true;
                boot_lba = entry.lba;
                boot_bytes = entry.bytes;
            }
            pos += entry.length;
        }
        done += (uint32_t)amount;
    }
    if(!found) return KUI_GAME_METADATA_BOOT_NOT_FOUND;
    out->boot_lba = boot_lba;
    out->boot_bytes = boot_bytes;
    out->boot_valid = true;
    return KUI_GAME_METADATA_OK;
}
enum kui_game_metadata_status kui_game_metadata_read(
    const struct kui_game_metadata_ops *ops, uint32_t session_lba,
    struct kui_game_metadata *out) {
    if(!out) return KUI_GAME_METADATA_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if(!ops || !ops->read_sector || !ops->data_range || session_lba > UINT32_MAX - 31u)
        return KUI_GAME_METADATA_ARGUMENT;
    out->session_lba = session_lba;
    uint8_t data[2048];
    enum kui_game_metadata_status result = sector(ops, session_lba, data, out);
    if(result != KUI_GAME_METADATA_OK) return result;
    if(memcmp(data, "SEGA SEGAKATANA ", 16)) return KUI_GAME_METADATA_IP_HEADER;
    field(out->title, data + 128, 128);
    field(out->product, data + 64, 10);
    field(out->version, data + 74, 6);
    field(out->region, data + 48, 8);
    field(out->bootfile, data + 96, 16);
    out->ip_valid = true;
    boot_profile(data, out);
    if(!root_filename(out->bootfile)) return KUI_GAME_METADATA_UNSUPPORTED;

    bool primary = false;
    unsigned descriptor_index = 0;
    for(unsigned i = 0; i < 16; ++i) {
        result = sector(ops, session_lba + 16u + i, data, out);
        if(result != KUI_GAME_METADATA_OK) return result;
        if(memcmp(data + 1, "CD001", 5) || data[6] != 1) return KUI_GAME_METADATA_ISO;
        if(data[0] == 1) {primary = true; descriptor_index = i; break;}
        if(data[0] == 255) return KUI_GAME_METADATA_ISO;
        if(data[0] > 3) return KUI_GAME_METADATA_ISO;
    }
    if(!primary) return KUI_GAME_METADATA_LIMIT;
    uint16_t block_size, set_size, sequence;
    if(data[7] || data[881] != 1 || !both32(data + 80, &out->volume_blocks) ||
       !both16(data + 120, &set_size) || !both16(data + 124, &sequence) ||
       !both16(data + 128, &block_size) || !out->volume_blocks ||
       out->volume_blocks <= 16u + descriptor_index)
        return KUI_GAME_METADATA_ISO;
    if(set_size != 1 || sequence != 1 || block_size != 2048)
        return KUI_GAME_METADATA_UNSUPPORTED;
    struct record root;
    if(data[156] != 34 || !record_read(data + 156, 34, &root) ||
       root.name_length != 1 || root.name[0] || !(root.flags & 2u))
        return KUI_GAME_METADATA_ISO;
    if(root.extended) return KUI_GAME_METADATA_UNSUPPORTED;
    if(root.bytes > KUI_GAME_METADATA_MAX_DIRECTORY_BYTES) return KUI_GAME_METADATA_LIMIT;
    if(!extent_valid(ops, root.lba, root.bytes, out->volume_blocks)) return KUI_GAME_METADATA_ISO;
    out->root_lba = root.lba;
    out->root_bytes = root.bytes;
    return directory(ops, out, data);
}
const char *kui_game_metadata_status_text(enum kui_game_metadata_status status) {
    switch(status) {
    case KUI_GAME_METADATA_OK: return "Boot metadata found";
    case KUI_GAME_METADATA_ARGUMENT: return "Invalid metadata request";
    case KUI_GAME_METADATA_IO: return "Image metadata could not be read";
    case KUI_GAME_METADATA_CANCELLED: return "Image inspection cancelled";
    case KUI_GAME_METADATA_IP_HEADER: return "Dreamcast boot header missing or invalid";
    case KUI_GAME_METADATA_ISO: return "Invalid ISO9660 metadata or file extent";
    case KUI_GAME_METADATA_BOOT_NOT_FOUND: return "Boot filename not found in the root directory";
    case KUI_GAME_METADATA_LIMIT: return "Image metadata exceeds inspection limits";
    case KUI_GAME_METADATA_UNSUPPORTED: return "Unsupported boot filename or ISO9660 layout";
    default: return "Unknown metadata result";
    }
}
