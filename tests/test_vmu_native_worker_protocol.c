/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_worker.h"
#include "kui/vmu_image.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    uint8_t block[512];uint32_t expected=0;
    for(unsigned i=0;i<256u;i++) {
        assert(!kui_vmu_image_blank_block((uint16_t)i,block));
        expected=kui_crc32(expected,block,sizeof(block));
    }
    assert(kui_vmu_native_worker_protocol_probe(NULL)==-1);
    assert(expected==KUI_VMU_NATIVE_WORKER_BLANK_CRC);
    for(unsigned i=0;i<2u;i++) {
        struct kui_vmu_native_worker_result r;memset(&r,0,sizeof(r));
        assert(!kui_vmu_native_worker_protocol_probe(&r));
        assert(r.requests==259u && r.reads==256u && r.refused==1u && r.image_crc==expected);
        assert(!r.lease_valid && !r.self_address && !r.entry_sp); /* CPU-only proof. */
    }
    printf("Native worker protocol: 259 requests, 256 blocks, CRC %08x, repeat init and refusal passed\n",expected);
    return 0;
}
