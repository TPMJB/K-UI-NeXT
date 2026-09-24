/* SPDX-License-Identifier: GPL-3.0-only
 * Selectively adapted from TPMJB's original GD Ripper checksum helpers,
 * K-UI_DS 2a5309298dde8fb100da1e2e4e10517695c9780f.
 * Introductions and boundaries: docs/recovery-port.md.
 * No DreamShell runtime, filesystem or drive code is included.
 */
#include "kui/recovery_checks.h"
#include <stdbool.h>
#include <string.h>

static uint32_t edc_table[256];
static uint8_t ecc_f[256],ecc_b[256];
static bool tables_ready;

static void make_tables(void) {
    if (tables_ready) return;
    for (unsigned i = 0; i < 256; ++i) {
        uint32_t v = i;
        unsigned j = i << 1;
        for (unsigned bit = 0; bit < 8; ++bit)
            v = (v >> 1) ^ ((v & 1) ? 0xd8018001U : 0);
        edc_table[i] = v;
        if (j & 0x100) j ^= 0x11d;
        ecc_f[i] = j;
        ecc_b[i ^ j] = i;
    }
    tables_ready = true;
}

static bool parity_ok(const uint8_t *src, unsigned major_count,
        unsigned minor_count, unsigned major_mult, unsigned minor_inc,
        const uint8_t *parity) {
    unsigned size = major_count * minor_count;
    for (unsigned major = 0; major < major_count; ++major) {
        unsigned index = (major >> 1) * major_mult + (major & 1);
        uint8_t a = 0, b = 0;
        for (unsigned minor = 0; minor < minor_count; ++minor) {
            uint8_t v = src[index];
            index = (index + minor_inc) % size;
            a ^= v;
            b ^= v;
            a = ecc_f[a];
        }
        a = ecc_b[ecc_f[a] ^ b];
        if (parity[major] != a || parity[major + major_count] != (a ^ b))
            return false;
    }
    return true;
}

static uint8_t bcd(unsigned v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static unsigned check_edc(const uint8_t *s, uint32_t fad) {
    static const uint8_t sync[12] = {0,255,255,255,255,255,255,255,255,255,255,0};
    uint32_t edc = 0, stored;
    unsigned result = 0;
    make_tables();
    if (memcmp(s, sync, sizeof(sync))) return KUI_RECOVERY_SECTOR_SYNC;
    /* Mode 2 and audio require a different validation scheme. */
    if (s[15] != 1) return KUI_RECOVERY_SECTOR_UNSUPPORTED;
    if (s[12] != bcd(fad / 4500) || s[13] != bcd((fad / 75) % 60) ||
            s[14] != bcd(fad % 75)) result |= KUI_RECOVERY_SECTOR_ADDRESS;
    for (unsigned i = 0; i < 2064; ++i)
        edc = (edc >> 8) ^ edc_table[(edc ^ s[i]) & 255];
    stored = (uint32_t)s[2064] | ((uint32_t)s[2065] << 8) |
        ((uint32_t)s[2066] << 16) | ((uint32_t)s[2067] << 24);
    if (edc != stored) result |= KUI_RECOVERY_SECTOR_EDC;
    return result;
}

unsigned kui_recovery_sector_check(const void *raw,size_t size,uint32_t fad) {
    if(!raw || size!=KUI_RECOVERY_RAW_BYTES || fad<150u || fad>KUI_RECOVERY_FAD_MAX)
        return KUI_RECOVERY_SECTOR_INPUT;
    const uint8_t *s=raw;
    unsigned result = check_edc(s, fad);
    if (result & (KUI_RECOVERY_SECTOR_SYNC | KUI_RECOVERY_SECTOR_UNSUPPORTED)) return result;
    /* ECMA-130 section 14.4: Mode 1 intermediate bytes must be zero, even if
     * somebody regenerated valid parity around an invalid reserved field. */
    for(unsigned i=2068;i<2076;++i)
        if(s[i]) result|=KUI_RECOVERY_SECTOR_RESERVED;
    if (!parity_ok(s + 12, 86, 24, 2, 86, s + 2076) ||
        !parity_ok(s + 12, 52, 43, 86, 88, s + 2248)) result |= KUI_RECOVERY_SECTOR_ECC;
    return result;
}

