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

    /* The exact reset callback is registered before gameplay and invoked
     * only after the title decides to return. Admit a linked terminal entry
     * only while both unchanged call boundaries and the native source agree. */
    const uint32_t terminal=0x8c006ce8u;
    assert(kui_toy_pilot_reset_target(0x8c027104u,0x2322u,0x430bu,terminal)==terminal);
    assert(!kui_toy_pilot_reset_target(0x8c027106u,0x2322u,0x430bu,terminal));
    assert(!kui_toy_pilot_reset_target(0x8c027104u,0x2320u,0x430bu,terminal));
    assert(!kui_toy_pilot_reset_target(0x8c027104u,0x2322u,0x4309u,terminal));
    assert(!kui_toy_pilot_reset_target(0x8c027104u,0x2322u,0x430bu,terminal+1u));
    assert(!kui_toy_pilot_reset_target(0x8c027104u,0x2322u,0x430bu,0x8c003ffeu));
    assert(!kui_toy_pilot_reset_target(0x8c027104u,0x2322u,0x430bu,0x8c007800u));
    assert(!kui_toy_pilot_reset_target(0x8c027104u,0x2322u,0x430bu,0x8cfd0000u));
    assert(!kui_toy_pilot_reset_target(0x8c027104u,0x2322u,0x430bu,0u));
    puts("Toy raw-GDI boot and early reset admission: passed");
    return 0;
}
