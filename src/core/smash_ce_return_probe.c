/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_ce_return_probe.h"
#include "kui/retail_image.h"
uint32_t kui_smash_ce_return_probe_crc(uint32_t code,const struct kui_smash_ce_return_probe_runtime *r) {
    const uint32_t zero=0;
    uint32_t crc=kui_retail_crc32(code,r,offsetof(struct kui_smash_ce_return_probe_runtime,code_crc32));
    crc=kui_retail_crc32(crc,&zero,4u);
    return kui_retail_crc32(crc,&r->raw_crc32,sizeof(*r)-offsetof(struct kui_smash_ce_return_probe_runtime,raw_crc32));
}
