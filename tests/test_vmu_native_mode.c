/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_mode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
int main(void) {
    static const uint8_t canonical[32]={'K','U','I','N','V','M','0','1',1,0,0,0,32};
    uint8_t wire[33];
    for(unsigned mode=0;mode<=4u;mode++) {
        memcpy(wire+1u,canonical,sizeof(canonical));wire[17u]=(uint8_t)mode;
        CHECK(kui_vmu_native_mode_decode(wire+1u,32u)==(enum kui_vmu_native_mode)mode);
        for(unsigned byte=0;byte<32u;byte++) for(unsigned bit=0;bit<8u;bit++) {
            wire[byte+1u]^=(uint8_t)(1u<<bit);
            int decoded=kui_vmu_native_mode_decode(wire+1u,32u);
            if(byte==16u && ((mode^(1u<<bit))<=4u))
                CHECK(decoded==(int)(mode^(1u<<bit)));
            else CHECK(decoded==KUI_VMU_NATIVE_MODE_INVALID);
            wire[byte+1u]^=(uint8_t)(1u<<bit);
        }
        for(unsigned length=0;length<34u;length++) if(length!=32u)
            CHECK(kui_vmu_native_mode_decode(wire+1u,length)==KUI_VMU_NATIVE_MODE_INVALID);
    }
    for(unsigned mode=5u;mode<256u;mode++) {
        memcpy(wire+1u,canonical,sizeof(canonical));wire[17u]=(uint8_t)mode;
        CHECK(kui_vmu_native_mode_decode(wire+1u,32u)==KUI_VMU_NATIVE_MODE_INVALID);
    }
    CHECK(kui_vmu_native_mode_decode(NULL,32u)==KUI_VMU_NATIVE_MODE_INVALID);
    puts("Native runtime mode codec: canonical modes, reserved bytes, unaligned inputs and mutations passed");
    printf("Native runtime mode: %u checks passed\n",checks);
    return 0;
}
