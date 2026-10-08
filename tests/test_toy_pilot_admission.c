/* SPDX-License-Identifier: GPL-3.0-only */
#include "toy_pilot_admission.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    /* games_retail prepares this raw-GDI contract: no BOOT_CRC flag and
     * no source CRC. The stage computes the loaded CRC independently. */
    struct kui_retail_manifest raw = {0};
    raw.boot_bytes = 748444u;
    assert(kui_toy_pilot_boot_identity(&raw,748444u,0xcdc493b3u));
    assert(!kui_toy_pilot_boot_identity(&raw,748444u,0xcdc493b2u));
    assert(!kui_toy_pilot_boot_identity(&raw,748443u,0xcdc493b3u));
    raw.boot_bytes = 748443u;
    assert(!kui_toy_pilot_boot_identity(&raw,748444u,0xcdc493b3u));
    raw.boot_bytes = 748444u;

    /* An explicitly declared source CRC remains mandatory, and cannot
     * substitute for the independently computed loaded identity. */
    raw.flags = KUI_RETAIL_IMAGE_BOOT_CRC;
    assert(!kui_toy_pilot_boot_identity(&raw,748444u,0xcdc493b3u));
    raw.boot_crc32 = 0xcdc493b3u;
    assert(kui_toy_pilot_boot_identity(&raw,748444u,0xcdc493b3u));
    assert(!kui_toy_pilot_boot_identity(&raw,748444u,0xcdc493b2u));
    raw.boot_crc32 ^= 1u;
    assert(!kui_toy_pilot_boot_identity(&raw,748444u,0xcdc493b3u));
    assert(!kui_toy_pilot_boot_identity(NULL,748444u,0xcdc493b3u));
    puts("Toy raw-GDI boot admission: 9 checks passed");
    return 0;
}
