/* SPDX-License-Identifier: GPL-3.0-only
 * Selectively adapted from TPMJB's original GD Ripper checksum helpers,
 * K-UI_DS 2a5309298dde8fb100da1e2e4e10517695c9780f.
 * Introductions and boundaries: docs/recovery-port.md.
 * No DreamShell runtime, filesystem or drive code is included.
 */
#include "kui/recovery_checks.h"
#include <stdbool.h>

static uint32_t matrix_times(const uint32_t *matrix, uint32_t value) {
    uint32_t result = 0;
    while (value) {
        if (value & 1) result ^= *matrix;
        value >>= 1;
        ++matrix;
    }
    return result;
}

uint32_t kui_recovery_crc_replace(uint32_t whole, uint32_t before, uint32_t after,
        uint64_t suffix_bytes) {
    /* Powers of the reflected CRC32 zero-byte operator. 8 KiB, initialized
     * once by the single drive worker; no dependency on a new core export. */
    static uint32_t powers[64][32];
    static bool ready;
    uint32_t delta = before ^ after;
    if (!ready) {
        for (unsigned i = 0; i < 32; ++i) {
            uint32_t v = (uint32_t)1 << i;
            for (unsigned b = 0; b < 8; ++b)
                v = (v >> 1) ^ ((v & 1) ? 0xedb88320U : 0);
            powers[0][i] = v;
        }
        for (unsigned p = 1; p < 64; ++p)
            for (unsigned i = 0; i < 32; ++i)
                powers[p][i] = matrix_times(powers[p-1], powers[p-1][i]);
        ready = true;
    }
    for (unsigned p = 0; suffix_bytes; ++p, suffix_bytes >>= 1)
        if (suffix_bytes & 1) delta = matrix_times(powers[p], delta);
    return whole ^ delta;
}

