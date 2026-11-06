/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VC2_CE_VMU_PREPARE_H
#define KUI_VC2_CE_VMU_PREPARE_H
#include <stddef.h>
#include <stdint.h>

enum kui_vc2_ce_vmu_prepare_reason {
    KUI_VC2_PREPARE_OK = 0,
    KUI_VC2_PREPARE_ARGUMENT = 0x301,
    KUI_VC2_PREPARE_KERNEL = 0x302,
    KUI_VC2_PREPARE_BRIDGE = 0x303,
    KUI_VC2_PREPARE_VERIFY = 0x304
};
struct kui_vc2_ce_vmu_prepare_report {
    uint32_t magic, phase, reason, actual, expected;
    uint32_t kernel_crc, ram_end, worker_word;
};
typedef void (*kui_vc2_ce_vmu_publish)(void *);
/* Stable loaded source, exclusive CPU/DMA ownership and an admitted helper
 * are prerequisites. No source-version match alone proves those properties.
 * Publication is last: the ordinary worker cannot enter the bridge until
 * its source, virtual text bounds and allocator exclusion are installed. */
uint32_t kui_vc2_ce_vmu_install_body(uint8_t *,size_t,const uint8_t *,size_t,
    kui_vc2_ce_vmu_publish,void *,struct kui_vc2_ce_vmu_prepare_report *);
/* Mode 4 is the reserve-only texture control: keep the same admitted source,
 * helper and allocator exclusion, but leave every Maple instruction, padding
 * byte and text bound original. Other modes retain the ordinary installer. */
uint32_t kui_vc2_ce_vmu_install_body_mode(uint8_t *,size_t,const uint8_t *,size_t,
    kui_vc2_ce_vmu_publish,void *,struct kui_vc2_ce_vmu_prepare_report *,uint32_t);
#if KUI_SMASH_SWIRL_VMU
/* Swirl has no free stored text tail. The sink is independently owned main
 * RAM; this explicit parameter lets host tests exercise the production copy
 * and rollback without pretending that original kernel padding is writable.
 * Production supplies the fixed pre-NK reserved P2 source interval. */
uint32_t kui_vc2_ce_vmu_install_body_relocated(uint8_t *,size_t,
    const uint8_t *,size_t,uint8_t *,size_t,kui_vc2_ce_vmu_publish,void *,
    struct kui_vc2_ce_vmu_prepare_report *,uint32_t);
#endif
uint32_t kui_vc2_ce_vmu_prepare_run(void *,uint32_t,volatile void *);
#endif
