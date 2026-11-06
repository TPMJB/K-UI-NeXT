/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_genesis_save_header.h"
uint32_t kui_smash_genesis_sram_bytes(const void *header,size_t bytes) {
    if(!header || bytes!=KUI_SMASH_GENESIS_HEADER_BYTES)return 0u;
    const uint8_t *p=header;
    if(p[0x1b0]!='R' || p[0x1b1]!='A' || p[0x1b3]!=0x20u)return 0u;
    uint32_t size=((uint32_t)p[0x1ba]+1u)*256u;
    if(p[0x1b2]==0xf0u || p[0x1b2]==0xf8u)return size/2u;
    return p[0x1b2]==0xe0u?size:0u;
}
