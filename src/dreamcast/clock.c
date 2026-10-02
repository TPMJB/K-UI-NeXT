/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/clock_platform.h"
#include <arch/rtc.h>
#include <dc/flashrom.h>
#include <string.h>
#include <time.h>

/* The BIOS also remembers when the clock was set in BLOCK_1/SYSCFG. Changing
 * only the RTC can make the next boot request the date again. Append a copy of
 * that record, preserving every other console/network setting. Never erase or
 * compact flash here; an invalid/full partition must be handled by the BIOS.
 * Format and source references: docs/clock-and-file-dates.md. */
enum { FLASH_BYTES = 128 * 1024, CONFIG_BYTES = 16 * 1024,
       RECORD_BYTES = 64, BITMAP_BYTES = 64, CONFIG_SLOTS = 254 };
struct clock_record {
    uint8_t data[RECORD_BYTES];
    int offset, bitmap_offset;
    uint8_t old_bitmap, new_bitmap;
    bool append;
};
/* KOS documents byte counts; some BIOS replacements return zero on success. */
static bool read_flash(int offset, void *data, int bytes) {
    memset(data, 0xa5, (size_t)bytes);
    int result = flashrom_read(offset, data, bytes);
    return result == 0 || result == bytes;
}
static bool write_flash(int offset, void *data, int bytes) {
    int result = flashrom_write(offset, data, bytes);
    return result == 0 || result == bytes;
}
static uint16_t record_crc(const uint8_t *data) {
    uint16_t crc = UINT16_C(0xffff);
    for(unsigned i = 0; i < 62; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for(unsigned bit = 0; bit < 8; ++bit)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return (uint16_t)~crc;
}
static bool prepare_record(int64_t seconds, struct clock_record *record) {
    int start, size;
    uint8_t header[18], bitmap[BITMAP_BYTES], block[RECORD_BYTES];
    if(flashrom_info(FLASHROM_PT_BLOCK_1, &start, &size) != 0 ||
       size != CONFIG_BYTES || start < 0 || start > FLASH_BYTES - CONFIG_BYTES ||
       start % CONFIG_BYTES != 0 ||
       !read_flash(start, header, sizeof(header)) ||
       memcmp(header, "KATANA_FLASH____", 16) != 0 ||
       header[16] != FLASHROM_PT_BLOCK_1 || header[17] > 1) return false;
    int bitmap_offset = start + size - BITMAP_BYTES;
    if(!read_flash(bitmap_offset, bitmap, sizeof(bitmap)))
        return false;

    /* Only 254 bits describe user records. The header and trailing bitmap
     * occupy the other two physical blocks; unused bitmap bits are not slots.
     * Reject holes rather than guessing how a nonstandard layout is ordered. */
    unsigned used = 0;
    while(used < CONFIG_SLOTS && !(bitmap[used / 8] & (0x80 >> (used % 8)))) ++used;
    for(unsigned i = used; i < CONFIG_SLOTS; ++i)
        if(!(bitmap[i / 8] & (0x80 >> (i % 8)))) return false;
    bool found = false;
    for(unsigned i = used; i > 0; --i) {
        if(!read_flash(start + (int)i * RECORD_BYTES, block, sizeof(block))) return false;
        if(block[0] != FLASHROM_B1_SYSCFG || block[1] != 0) continue;
        uint16_t crc = record_crc(block);
        if(block[62] != (uint8_t)crc || block[63] != (uint8_t)(crc >> 8)) continue;
        memcpy(record->data, block, sizeof(block));
        found = true;
        break;
    }
    if(!found) return false;
    uint32_t dc_seconds = (uint32_t)(seconds + INT64_C(631152000));
    record->append = false;
    for(unsigned i = 0; i < 4; ++i) {
        uint8_t byte = (uint8_t)(dc_seconds >> (8 * i));
        if(record->data[2 + i] != byte) record->append = true;
        record->data[2 + i] = byte;
    }
    if(!record->append) return true;
    if(used == CONFIG_SLOTS) return false;
    record->offset = start + (int)(used + 1) * RECORD_BYTES;
    if(!read_flash(record->offset, block, sizeof(block)))
        return false;
    for(unsigned i = 0; i < sizeof(block); ++i)
        if(block[i] != 0xff) return false;
    uint16_t crc = record_crc(record->data);
    record->data[62] = (uint8_t)crc;
    record->data[63] = (uint8_t)(crc >> 8);
    record->bitmap_offset = bitmap_offset + (int)(used / 8);
    record->old_bitmap = bitmap[used / 8];
    record->new_bitmap = record->old_bitmap & ~(0x80 >> (used % 8));
    return true;
}
static bool save_record(struct clock_record *record) {
    if(!record->append) return true;
    uint8_t bitmap, block[RECORD_BYTES];
    if(!read_flash(record->bitmap_offset, &bitmap, 1) ||
       bitmap != record->old_bitmap) return false;
    /* Reserve first. An interrupted/failed record write consumes a slot, but
     * leaves the previous CRC-valid record intact and cannot be reused later.
     * Readback is authoritative even if a BIOS reports a non-byte-count write. */
    if(!write_flash(record->bitmap_offset, &record->new_bitmap, 1) ||
       !read_flash(record->bitmap_offset, &bitmap, 1) ||
       bitmap != record->new_bitmap) return false;
    if(!write_flash(record->offset, record->data, sizeof(record->data)) ||
       !read_flash(record->offset, block, sizeof(block)) ||
       memcmp(block, record->data, sizeof(block)) != 0) return false;
    return true;
}

/* Pinned KOS include/kos/rtc.h specifies local wall time. time() uses its
 * boot-time RTC sample plus the elapsed timer, avoiding G2 reads per f_sync. */
static bool read_local(void *ctx, int64_t *seconds) {
    (void)ctx;
    time_t now = time(NULL);
    if(now == (time_t)-1) return false;
    *seconds = (int64_t)now;
    /* The Dreamcast's 32-bit counter begins in 1950. Values beyond the last
     * representable instant are not a valid console RTC, even if FAT fits. */
    return *seconds >= INT64_C(315532800) && *seconds <= INT64_C(3663815295);
}
void kui_clock_start(kui_clock_log_fn log) {
    kui_clock_configure(read_local, NULL, log);
    struct kui_datetime now;
    if(log && kui_clock_now(&now))
        log("Clock: %04u-%02u-%02u %02u:%02u:%02u local (RTC); new file dates follow it",
            (unsigned)now.year, (unsigned)now.month, (unsigned)now.day,
            (unsigned)now.hour, (unsigned)now.minute, (unsigned)now.second);
    else if(log)
        log("CLOCK WARNING: RTC invalid; check Settings clock. New file dates use 1980-01-01");
}
bool kui_clock_set_local(const struct kui_datetime *value) {
    int64_t seconds;
    if(!kui_clock_to_seconds(value, &seconds) || seconds > INT64_C(3663815295)) return false;
    time_t requested = (time_t)seconds;
    struct clock_record record;
    /* Refuse an unsafe/full flash layout before changing the hardware clock. */
    if((int64_t)requested != seconds || !prepare_record(seconds, &record) ||
       rtc_set_unix_secs(requested) != 0) return false;
    /* KOS also updates its cached boot time. Check that both the RTC and the
     * timestamp source agree, allowing one second for a rollover while setting. */
    int64_t actual = (int64_t)rtc_unix_secs(), cached;
    if(actual < seconds || actual > seconds + 1 || !read_local(NULL, &cached) ||
       cached < seconds || cached > seconds + 1) return false;
    return save_record(&record);
}
