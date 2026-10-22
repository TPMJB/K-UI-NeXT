/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/clock_platform.h"
#include <dc/flashrom.h>
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define FLASH_SIZE 131072
#define PART_START 114688
#define PART_SIZE 16384
#define RECORD_SIZE 64
#define USER_SLOTS 254
#define BITMAP_OFFSET (PART_START + PART_SIZE - RECORD_SIZE)
#define UNIX_DELTA UINT32_C(631152000)

static unsigned char flash[FLASH_SIZE], before_flash[FLASH_SIZE];
static time_t cached, rtc;
static unsigned rtc_writes, flash_writes, erases, logs;
static int rtc_failure, info_failure, info_start, info_size;
static int read_fault_start, read_fault_end;
static unsigned read_fault_after;
static bool read_returns_zero;
static unsigned write_fault_call;
static int write_fault_bytes, write_fault_result, normal_write_result;
static struct { int offset, size; } write_log[32];

time_t time(time_t *out) { if(out) *out = cached; return cached; }
time_t rtc_unix_secs(void) { return rtc; }
int rtc_set_unix_secs(time_t value) {
    ++rtc_writes;
    if(rtc_failure == 1) return -1;
    rtc = value + (rtc_failure == 2 ? 2 : 0);
    cached = value + (rtc_failure == 3 ? 2 : 0);
    return 0;
}
int flashrom_info(int part, int *start, int *size) {
    assert(part == FLASHROM_PT_BLOCK_1);
    if(info_failure) return -1;
    *start = info_start; *size = info_size;
    return 0;
}
int flashrom_read(int offset, void *out, int bytes) {
    assert(offset >= 0 && bytes >= 0 && offset <= FLASH_SIZE - bytes);
    if(read_fault_start >= 0 && flash_writes >= read_fault_after &&
       offset < read_fault_end && offset + bytes > read_fault_start) {
        if(bytes > 1) memcpy(out, flash + offset, (size_t)bytes - 1);
        return bytes - 1;
    }
    memcpy(out, flash + offset, (size_t)bytes);
    return read_returns_zero ? 0 : bytes;
}
int flashrom_write(int offset, void *data, int bytes) {
    assert(offset >= PART_START && bytes > 0 && offset <= FLASH_SIZE - bytes);
    assert(flash_writes < sizeof(write_log) / sizeof(write_log[0]));
    write_log[flash_writes].offset = offset;
    write_log[flash_writes].size = bytes;
    ++flash_writes;
    int programmed = bytes;
    int result = normal_write_result < 0 ? bytes : normal_write_result;
    if(flash_writes == write_fault_call) {
        programmed = write_fault_bytes < bytes ? write_fault_bytes : bytes;
        result = write_fault_result;
    }
    const unsigned char *input = data;
    /* Real flash programming cannot turn a cleared bit back into a one. */
    for(int i = 0; i < programmed; ++i) {
        assert((flash[offset + i] & input[i]) == input[i]);
        flash[offset + i] &= input[i];
    }
    return result;
}
int flashrom_delete(int offset) {
    (void)offset;
    ++erases;
    assert(!"Clock changes must never erase a flash partition");
    return -1;
}
static void log_line(const char *format, ...) { (void)format; ++logs; }

/* Nibble-table CRC is independent of the production bit-at-a-time algorithm.
 * CRC-16/GENIBUS has check value D64E for the ASCII string "123456789". */
static uint16_t record_crc(const unsigned char *data, size_t size) {
    static const uint16_t table[16] = {
        0x0000,0x1021,0x2042,0x3063,0x4084,0x50a5,0x60c6,0x70e7,
        0x8108,0x9129,0xa14a,0xb16b,0xc18c,0xd1ad,0xe1ce,0xf1ef
    };
    uint16_t crc = 0xffff;
    for(size_t i = 0; i < size; ++i) {
        crc = (uint16_t)((crc << 4) ^ table[(crc >> 12) ^ (data[i] >> 4)]);
        crc = (uint16_t)((crc << 4) ^ table[(crc >> 12) ^ (data[i] & 15)]);
    }
    return (uint16_t)(crc ^ 0xffff);
}
static void put16(unsigned char *out, uint16_t value) {
    out[0] = (unsigned char)value; out[1] = (unsigned char)(value >> 8);
}
static void put32(unsigned char *out, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i) out[i] = (unsigned char)(value >> (i * 8));
}
static int slot_offset(unsigned slot) {
    assert(slot < USER_SLOTS);
    return PART_START + (int)(slot + 1) * RECORD_SIZE;
}
static unsigned char slot_mask(unsigned slot) { return (unsigned char)(0x80u >> (slot % 8)); }
static void reserve(unsigned slot) { flash[BITMAP_OFFSET + slot / 8] &= (unsigned char)~slot_mask(slot); }
static void install_record(unsigned slot, unsigned id, time_t timestamp, unsigned salt) {
    unsigned char *record = flash + slot_offset(slot);
    for(unsigned i = 0; i < RECORD_SIZE; ++i) record[i] = (unsigned char)(salt + i * 13);
    put16(record, (uint16_t)id);
    put32(record + 2, (uint32_t)timestamp + UNIX_DELTA);
    put16(record + 62, record_crc(record, 62));
    reserve(slot);
}
static struct kui_datetime requested_date(unsigned day) {
    return (struct kui_datetime){2026,9,(uint8_t)day,17,42,0};
}
static time_t requested_seconds(const struct kui_datetime *value) {
    int64_t seconds;
    assert(kui_clock_to_seconds(value, &seconds));
    return (time_t)seconds;
}
static void reset_fixture(void) {
    for(unsigned i = 0; i < FLASH_SIZE; ++i) flash[i] = (unsigned char)(i * 7);
    memset(flash + PART_START, 0xff, PART_SIZE);
    memcpy(flash + PART_START, "KATANA_FLASH____", 16);
    flash[PART_START + 16] = FLASHROM_PT_BLOCK_1;
    flash[PART_START + 17] = 0;
    cached = rtc = 1790208000;
    rtc_writes = flash_writes = erases = logs = 0;
    rtc_failure = info_failure = 0;
    info_start = PART_START; info_size = PART_SIZE;
    read_fault_start = read_fault_end = -1; read_fault_after = 0;
    read_returns_zero = false;
    write_fault_call = 0; write_fault_bytes = write_fault_result = 0;
    normal_write_result = -1;
    memset(write_log, 0, sizeof(write_log));
    /* User slot zero is physical flash block one: it must be discoverable. */
    install_record(0, FLASHROM_B1_SYSCFG, rtc, 19);
}
static void expect_no_mutation(const struct kui_datetime *value) {
    memcpy(before_flash, flash, sizeof(flash));
    unsigned before_rtc = rtc_writes, before_writes = flash_writes;
    assert(!kui_clock_set_local(value));
    assert(rtc_writes == before_rtc && flash_writes == before_writes && erases == 0);
    assert(!memcmp(before_flash, flash, sizeof(flash)));
}
static void expect_append(const struct kui_datetime *value, unsigned source, unsigned target) {
    unsigned char expected_record[RECORD_SIZE];
    memcpy(before_flash, flash, sizeof(flash));
    memcpy(expected_record, flash + slot_offset(source), sizeof(expected_record));
    put32(expected_record + 2, (uint32_t)requested_seconds(value) + UNIX_DELTA);
    put16(expected_record + 62, record_crc(expected_record, 62));
    memcpy(before_flash + slot_offset(target), expected_record, sizeof(expected_record));
    before_flash[BITMAP_OFFSET + target / 8] &= (unsigned char)~slot_mask(target);
    unsigned before_writes = flash_writes, before_rtc = rtc_writes;
    assert(kui_clock_set_local(value));
    assert(rtc_writes == before_rtc + 1 && flash_writes == before_writes + 2 && erases == 0);
    assert(write_log[before_writes].offset == BITMAP_OFFSET + (int)(target / 8));
    assert(write_log[before_writes].size == 1);
    assert(write_log[before_writes + 1].offset == slot_offset(target));
    assert(write_log[before_writes + 1].size == RECORD_SIZE);
    /* Includes unrelated partitions, old records, network settings, all other
     * system fields, bitmap padding and every byte of the appended record. */
    assert(!memcmp(before_flash, flash, sizeof(flash)));
    assert(rtc == requested_seconds(value) && cached == rtc);
}
static void test_read_only_startup_and_bounds(void) {
    reset_fixture();
    memcpy(before_flash, flash, sizeof(flash));
    kui_clock_start(log_line);
    struct kui_datetime value;
    assert(kui_clock_now(&value) && rtc_writes == 0 && flash_writes == 0 && logs == 1);
    assert(value.year == 2026 && value.month == 9 && value.day == 24);
    assert(!memcmp(before_flash, flash, sizeof(flash)));
    expect_no_mutation(NULL);
    value = (struct kui_datetime){2086,2,6,6,28,16}; expect_no_mutation(&value);
    value = (struct kui_datetime){1970,1,1,0,0,0}; expect_no_mutation(&value);
    value = (struct kui_datetime){2026,2,29,0,0,0}; expect_no_mutation(&value);
    value = (struct kui_datetime){2086,2,6,6,28,15}; expect_append(&value,0,1);
    cached = (time_t)-1; assert(!kui_clock_now(&value));
    kui_clock_start(log_line); assert(logs == 2);
    cached = 3663815296LL; assert(!kui_clock_now(&value));
}
static void test_forward_backward_and_preservation(void) {
    reset_fixture();
    struct kui_datetime value = requested_date(23);
    expect_append(&value,0,1); /* Backwards relative to the BIOS checkpoint. */
    value = requested_date(25); expect_append(&value,1,2);

    reset_fixture();
    install_record(1, FLASHROM_B1_IP_SETTINGS, rtc, 91);
    install_record(2, FLASHROM_B1_SYSCFG, rtc, 137);
    install_record(3, FLASHROM_B1_PW_SETTINGS_1, rtc, 221);
    /* An interrupted newer system record must not replace the valid source. */
    install_record(4, FLASHROM_B1_SYSCFG, rtc, 252);
    flash[slot_offset(4) + 62] ^= 1;
    value = requested_date(23); expect_append(&value,2,5);

    reset_fixture();
    flash[PART_START + 17] = 1; /* Both known partition versions are accepted. */
    normal_write_result = 0; /* BIOS may return success rather than a count. */
    read_returns_zero = true;
    expect_append(&value,0,1);
}
static void test_last_slot_full_and_unchanged_timestamp(void) {
    reset_fixture();
    for(unsigned i = 1; i < USER_SLOTS - 1; ++i)
        install_record(i, FLASHROM_B1_IP_SETTINGS, rtc, i);
    struct kui_datetime value = requested_date(23);
    expect_append(&value,0,USER_SLOTS - 1);
    value = requested_date(25); expect_no_mutation(&value);
    /* Full flash needs no append if its latest checkpoint already matches. */
    value = requested_date(23);
    memcpy(before_flash, flash, sizeof(flash));
    unsigned writes = flash_writes;
    assert(kui_clock_set_local(&value) && flash_writes == writes && erases == 0);
    assert(!memcmp(before_flash, flash, sizeof(flash)));

    reset_fixture();
    assert(kui_clock_from_seconds(rtc, &value));
    memcpy(before_flash, flash, sizeof(flash));
    assert(kui_clock_set_local(&value) && flash_writes == 0);
    assert(!memcmp(before_flash, flash, sizeof(flash)));
}
static void test_invalid_partition_and_records(void) {
    struct kui_datetime value = requested_date(23);
    reset_fixture(); info_failure = 1; expect_no_mutation(&value);
    const int starts[] = {-1, FLASH_SIZE, INT_MAX, FLASH_SIZE - PART_SIZE + 1};
    for(unsigned i = 0; i < sizeof(starts)/sizeof(starts[0]); ++i) {
        reset_fixture(); info_start = starts[i]; expect_no_mutation(&value);
    }
    const int sizes[] = {-1, 0, PART_SIZE - 1, PART_SIZE + 1, INT_MAX};
    for(unsigned i = 0; i < sizeof(sizes)/sizeof(sizes[0]); ++i) {
        reset_fixture(); info_size = sizes[i]; expect_no_mutation(&value);
    }
    for(unsigned i = 0; i < 16; ++i) {
        reset_fixture(); flash[PART_START + i] ^= 1; expect_no_mutation(&value);
    }
    reset_fixture(); flash[PART_START + 16] = 3; expect_no_mutation(&value);
    reset_fixture(); flash[PART_START + 17] = 2; expect_no_mutation(&value);
    reset_fixture(); flash[slot_offset(0) + 63] ^= 1; expect_no_mutation(&value);
    reset_fixture(); install_record(0,FLASHROM_B1_IP_SETTINGS,rtc,19); expect_no_mutation(&value);
    reset_fixture(); memset(flash + BITMAP_OFFSET,0xff,RECORD_SIZE); expect_no_mutation(&value);
    reset_fixture(); install_record(2,FLASHROM_B1_IP_SETTINGS,rtc,11); expect_no_mutation(&value);
    reset_fixture(); flash[slot_offset(1) + 37] = 0; expect_no_mutation(&value);
    /* A short read at any preflight component must stop before setting RTC. */
    const int offsets[] = {PART_START, BITMAP_OFFSET, PART_START + RECORD_SIZE,
                           PART_START + 2 * RECORD_SIZE};
    for(unsigned i = 0; i < sizeof(offsets)/sizeof(offsets[0]); ++i) {
        reset_fixture(); read_fault_start = offsets[i]; read_fault_end = offsets[i] + 1;
        expect_no_mutation(&value);
    }
}
static void test_rtc_failures_do_not_write_flash(void) {
    struct kui_datetime value = requested_date(23);
    for(int failure = 1; failure <= 3; ++failure) {
        reset_fixture(); rtc_failure = failure;
        memcpy(before_flash, flash, sizeof(flash));
        assert(!kui_clock_set_local(&value) && rtc_writes == 1 && flash_writes == 0);
        assert(!memcmp(before_flash, flash, sizeof(flash)) && erases == 0);
    }
}
static void test_flash_write_failures_and_retry(void) {
    struct kui_datetime value = requested_date(23);
    /* A failed or falsely successful reservation cannot reach record writing. */
    for(int result = -1; result <= 1; ++result) {
        reset_fixture(); write_fault_call = 1; write_fault_bytes = 0; write_fault_result = result;
        memcpy(before_flash, flash, sizeof(flash));
        assert(!kui_clock_set_local(&value) && rtc_writes == 1 && flash_writes == 1);
        assert(!memcmp(before_flash, flash, sizeof(flash)));
    }
    reset_fixture(); write_fault_call = 1; write_fault_bytes = 1; write_fault_result = -1;
    assert(!kui_clock_set_local(&value) && flash_writes == 1);
    assert(!(flash[BITMAP_OFFSET] & slot_mask(1)));
    write_fault_call = 0;
    expect_append(&value,0,2); /* Failed reservation consumed an otherwise blank slot. */

    for(int result = -1; result <= 1; ++result) {
        reset_fixture(); write_fault_call = 2; write_fault_bytes = 0; write_fault_result = result;
        assert(!kui_clock_set_local(&value) && flash_writes == 2 && rtc_writes == 1);
        assert(!(flash[BITMAP_OFFSET] & slot_mask(1)) && erases == 0);
        write_fault_call = 0;
        expect_append(&value,0,2);
    }
    reset_fixture(); write_fault_call = 2; write_fault_bytes = 13; write_fault_result = 13;
    assert(!kui_clock_set_local(&value) && flash_writes == 2);
    write_fault_call = 0;
    expect_append(&value,0,2); /* Partial record is CRC-invalid and never reused. */

    reset_fixture(); write_fault_call = 2; write_fault_bytes = RECORD_SIZE; write_fault_result = -1;
    assert(!kui_clock_set_local(&value) && flash_writes == 2);
    write_fault_call = 0;
    assert(kui_clock_set_local(&value) && flash_writes == 2); /* Negative result stays failure. */

    reset_fixture(); write_fault_call = 2; write_fault_bytes = RECORD_SIZE; write_fault_result = 13;
    assert(!kui_clock_set_local(&value) && flash_writes == 2); /* A positive partial count is failure. */

    reset_fixture();
    read_fault_start = BITMAP_OFFSET; read_fault_end = BITMAP_OFFSET + 1; read_fault_after = 1;
    assert(!kui_clock_set_local(&value) && flash_writes == 1);
    read_fault_start = -1;
    expect_append(&value,0,2);

    reset_fixture();
    read_fault_start = slot_offset(1); read_fault_end = slot_offset(1) + RECORD_SIZE; read_fault_after = 2;
    assert(!kui_clock_set_local(&value) && flash_writes == 2);
    read_fault_start = -1;
    assert(kui_clock_set_local(&value) && flash_writes == 2);
    assert(erases == 0);
}
int main(void) {
    assert(record_crc((const unsigned char *)"123456789",9) == 0xd64e);
    test_read_only_startup_and_bounds();
    test_forward_backward_and_preservation();
    test_last_slot_full_and_unchanged_timestamp();
    test_invalid_partition_and_records();
    test_rtc_failures_do_not_write_flash();
    test_flash_write_failures_and_retry();
    puts("PASS console clock: RTC bounds, BIOS checkpoint append, exact preservation, full flash, fault recovery; no erases");
    return 0;
}
