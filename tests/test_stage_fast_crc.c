/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_image.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "stage_fast_crc32.h"
static uint8_t image[8197];
int main(void) {
    assert(kui_stage_crc32(0,"123456789",9)==0xcbf43926u);
    assert(kui_stage_crc32(0,NULL,0)==0u);
    uint32_t seed=0x45424e44u;
    for(unsigned i=0;i<sizeof(image);++i){seed=seed*1664525u+1013904223u;image[i]=(uint8_t)(seed>>24);}
    for(unsigned offset=0;offset<8u;++offset)for(unsigned n=0;n<4097u;++n) {
        uint32_t expected=kui_retail_crc32(0,image+offset,n);
        assert(kui_stage_crc32(0,image+offset,n)==expected);
        unsigned split=n/3u;
        uint32_t prefix=kui_stage_crc32(0,image+offset,split);
        assert(kui_stage_crc32(prefix,image+offset+split,n-split)==expected);
    }
    uint32_t original=kui_stage_crc32(0,image,sizeof(image));
    for(unsigned i=0;i<sizeof(image);++i){image[i]^=1u;assert(kui_stage_crc32(0,image,sizeof(image))!=original);image[i]^=1u;}
    puts("stage CRC: 32776 aligned/unaligned sizes, incremental splits and every-byte corruption passed");
}
