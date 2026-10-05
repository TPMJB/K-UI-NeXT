/* SPDX-License-Identifier: GPL-3.0-only */
#include "retail_boot.h"
#include <assert.h>
#include <stdio.h>

#define COUNT 3u
static uint8_t raw[COUNT * KUI_GAME_RAW_BYTES];
static uint8_t cooked[COUNT * KUI_GAME_DATA_BYTES];
static uint8_t output[COUNT * KUI_GAME_DATA_BYTES + 32u];

static uint8_t bcd(uint32_t value) { return (uint8_t)((value / 10u) * 16u + value % 10u); }
static void prepare(void) {
    for(uint32_t i = 0; i < COUNT; ++i) {
        uint8_t *sector = raw + i * KUI_GAME_RAW_BYTES;
        uint32_t fad = 45000u + i + 150u;
        memset(sector, 0xa5, KUI_GAME_RAW_BYTES);
        sector[0] = sector[11] = 0;
        memset(sector + 1, 0xff, 10);
        sector[12] = bcd(fad / (60u * 75u));
        sector[13] = bcd(fad / 75u % 60u);
        sector[14] = bcd(fad % 75u); sector[15] = 1;
        for(uint32_t j = 0; j < KUI_GAME_DATA_BYTES; ++j)
            sector[16 + j] = cooked[i * KUI_GAME_DATA_BYTES + j] = (uint8_t)(i * 37u + j);
    }
    memset(output, 0x5a, sizeof(output));
}
int main(void) {
    uint32_t failed = 0;
    prepare();
    assert(kui_retail_boot_copy(raw, output, 45000, COUNT, KUI_GAME_RAW_BYTES, &failed) ==
        KUI_RETAIL_HEADER_OK);
    assert(!memcmp(output, cooked, sizeof(cooked)));
    for(unsigned i = sizeof(cooked); i < sizeof(output); ++i) assert(output[i] == 0x5a);
    uint32_t reference = kui_retail_crc32(0, output, sizeof(cooked) - 7u);
    prepare();
    assert(kui_retail_boot_copy(cooked, output, 45000, COUNT, KUI_GAME_DATA_BYTES, &failed) ==
        KUI_RETAIL_HEADER_OK);
    assert(!memcmp(output, cooked, sizeof(cooked)));
    /* Exact executable bytes, excluding final-sector padding. CE prefix/body
     * CRC chaining must equal the fingerprint of the original full file. */
    uint32_t prefix = kui_retail_crc32(0, output, KUI_GAME_DATA_BYTES);
    uint32_t combined = kui_retail_crc32(prefix, output + KUI_GAME_DATA_BYTES,
        sizeof(cooked) - KUI_GAME_DATA_BYTES - 7u);
    assert(combined == reference);
    output[100] ^= 1;
    assert(kui_retail_crc32(0, output, sizeof(cooked) - 7u) != reference);
    prepare(); raw[KUI_GAME_RAW_BYTES + 14u] ^= 1;
    assert(kui_retail_boot_copy(raw, output, 45000, COUNT, KUI_GAME_RAW_BYTES, &failed) ==
        KUI_RETAIL_HEADER_ADDRESS && failed == 45001u);
    assert(!memcmp(output, cooked, KUI_GAME_DATA_BYTES));
    assert(output[KUI_GAME_DATA_BYTES] == 0x5a); /* Refuse mismatched sector. */
    prepare(); raw[15] = 2;
    assert(kui_retail_boot_copy(raw, output, 45000, COUNT, KUI_GAME_RAW_BYTES, &failed) ==
        KUI_RETAIL_HEADER_MODE && failed == 45000u && output[0] == 0x5a);
    prepare(); raw[8] = 0;
    assert(kui_retail_boot_copy(raw, output, 45000, COUNT, KUI_GAME_RAW_BYTES, &failed) ==
        KUI_RETAIL_HEADER_SYNC && failed == 45000u && output[0] == 0x5a);
    puts("retail boot: raw header/address checks, cooked copy and exact-file CRC passed");
    return 0;
}
