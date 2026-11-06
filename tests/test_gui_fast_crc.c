/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/core.h"
#include "kui/retail_image.h"
#include <assert.h>
#include <stdio.h>

static uint32_t oracle(uint32_t previous,const uint8_t *data,size_t n) {
    uint32_t value=~previous;
    while(n--) {
        value^=*data++;
        for(unsigned i=0;i<8u;i++)value=(value>>1)^((value&1u)?UINT32_C(0xedb88320):0u);
    }
    return ~value;
}
int main(void) {
    static uint8_t bytes[32768u+32u];
    for(size_t i=0;i<sizeof(bytes);i++)bytes[i]=(uint8_t)(i*37u+(i>>8));
    assert(kui_crc32(0u,"123456789",9u)==UINT32_C(0xcbf43926));
    assert(kui_retail_crc32(0u,NULL,0u)==0u);
    for(size_t offset=0u;offset<32u;offset++)for(size_t n=0u;n<=32768u;n=n<130u?n+1u:n*2u) {
        uint32_t previous=(uint32_t)(offset*UINT32_C(0x89abcdef)),want=oracle(previous,bytes+offset,n);
        assert(kui_crc32(previous,bytes+offset,n)==want);
        assert(kui_retail_crc32(previous,bytes+offset,n)==want);
        size_t split=n/3u;
        assert(kui_crc32(kui_crc32(previous,bytes+offset,split),bytes+offset+split,n-split)==want);
        assert(kui_retail_crc32(kui_retail_crc32(previous,bytes+offset,split),bytes+offset+split,n-split)==want);
    }
    puts("GUI CRC: byte-exact independent polynomial oracle, all alignments/tails/incremental states PASS");
    return 0;
}
