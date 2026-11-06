/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CE_RETURN_CRC32_H
#define KUI_CE_RETURN_CRC32_H
/* CE return controller only: every byte still contributes to full IEEE CRC32.
 * The small bitwise leaf keeps the accepted startup code/data footprint;
 * ordinary readers, native workers and immutable CE helpers retain their
 * existing implementations. This leaf runs only on the terminal return path. */
__asm__(".global __kui_smash_ce_return_crc32_bitwise\n.set __kui_smash_ce_return_crc32_bitwise,1\n");
static uint32_t kui_ce_return_crc32(uint32_t previous,const void *data,size_t bytes) {
    uint32_t crc=previous^UINT32_MAX;
    const uint8_t *p=data;
    while(bytes--) {
        crc^=*p++;
        for(unsigned bit=0u;bit<8u;++bit)
            crc=(crc>>1)^((0u-(crc&1u))&UINT32_C(0xedb88320));
    }
    return crc^UINT32_MAX;
}
#endif
