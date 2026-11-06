/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_ce_maple_profile.h"
#include "kui/ce_maple_owner_profile.h"
#include "kui/armada_ce_maple_owner_pointer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc,char **argv) {
    static const uint8_t check[]="123456789";
    assert(kui_vc2_ce_maple_crc32(check,9u)==UINT32_C(0xcbf43926));
    assert(!kui_vc2_ce_maple_source_valid(NULL,KUI_VC2_CE_KERNEL_BODY_BYTES));
    assert(!kui_vc2_ce_maple_source_valid(check,0));
    assert(!kui_vc2_ce_maple_source_valid(check,KUI_VC2_CE_KERNEL_BODY_BYTES-1u));
    assert(!kui_vc2_ce_maple_source_valid(check,KUI_VC2_CE_KERNEL_BODY_BYTES+1u));
    /* The authored BSR displacement must reach the bridge while preserving
     * the original call's third halfword and return-to-worker address. */
    uint32_t displacement=(KUI_VC2_CE_MAPLE_WORKER_PATCH_WORD>>16)&0xfffu;
    assert(KUI_VC2_CE_MAPLE_WORKER_PATCH_RVA+2u+4u+2u*displacement==
        KUI_VC2_CE_MAPLE_BRIDGE_RVA);
    assert(KUI_VC2_CE_MAPLE_WORKER_PATCH_RVA+6u==KUI_VC2_CE_MAPLE_WORKER_RESUME_RVA);
    assert(KUI_VC2_CE_MAPLE_RESERVE_END_P1-KUI_VC2_CE_MAPLE_RESERVE_P1==48u*1024u);
#if KUI_VC2_CE_VMU
    uint32_t pointer;
    assert(KUI_CE_MAPLE_EXTENSION_BYTES==0x1a0u && KUI_CE_MAPLE_FUNCTION_BYTES==0x150u);
    assert(kui_armada_ce_maple_owner_pointer(0x01fffe60u,0x08000000u,KUI_CE_MAPLE_EXTENSION_BYTES,&pointer));
    assert(!kui_armada_ce_maple_owner_pointer(0x01fffe68u,0x08000000u,KUI_CE_MAPLE_EXTENSION_BYTES,&pointer));
    assert(kui_armada_ce_maple_owner_pointer(0x01fffeb0u,0x08000000u,KUI_CE_MAPLE_FUNCTION_BYTES,&pointer));
    assert(!kui_armada_ce_maple_owner_pointer(0x01ffff0cu,0x08000000u,KUI_CE_MAPLE_FUNCTION_BYTES,&pointer));
#endif
    if(argc==1) {puts("VC2 source profile public checks passed; owner checks require a kernel path");return 0;}
    assert(argc==2);
    FILE *f=fopen(argv[1],"rb");assert(f);
    uint8_t *file=malloc(KUI_VC2_CE_KERNEL_FILE_BYTES+1u);assert(file);
    assert(fread(file,1u,KUI_VC2_CE_KERNEL_FILE_BYTES+1u,f)==KUI_VC2_CE_KERNEL_FILE_BYTES);
    assert(!ferror(f));fclose(f);
    uint8_t *body=file+KUI_VC2_CE_KERNEL_PREFIX_BYTES;
    assert(kui_vc2_ce_maple_source_valid(body,KUI_VC2_CE_KERNEL_BODY_BYTES));
    const size_t offsets[]={0,64,68,
        KUI_VC2_CE_ROMHDR_P1-KUI_VC2_CE_KERNEL_BODY_P1,
        KUI_VC2_CE_RAMEND_WORD_P1-KUI_VC2_CE_KERNEL_BODY_P1,
        KUI_VC2_CE_MAPLE_TEXT_RECORD_P1-KUI_VC2_CE_KERNEL_BODY_P1,
        KUI_VC2_CE_MAPLE_WORKER_PATCH_P1-KUI_VC2_CE_KERNEL_BODY_P1,
        KUI_VC2_CE_MAPLE_WORKER_PATCH_P1-KUI_VC2_CE_KERNEL_BODY_P1+4u,
        KUI_VC2_CE_MAPLE_PADDING_P1-KUI_VC2_CE_KERNEL_BODY_P1,
        KUI_VC2_CE_MAPLE_PADDING_P1-KUI_VC2_CE_KERNEL_BODY_P1+KUI_VC2_CE_MAPLE_PADDING_BYTES-1u,
        KUI_VC2_CE_KERNEL_BODY_BYTES-1u};
    for(size_t i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        body[offsets[i]]^=1u;
        assert(!kui_vc2_ce_maple_source_valid(body,KUI_VC2_CE_KERNEL_BODY_BYTES));
        body[offsets[i]]^=1u;
    }
    assert(kui_vc2_ce_maple_source_valid(body,KUI_VC2_CE_KERNEL_BODY_BYTES));
    memset(body,0,KUI_VC2_CE_KERNEL_BODY_BYTES);
    assert(!kui_vc2_ce_maple_source_valid(body,KUI_VC2_CE_KERNEL_BODY_BYTES));
    free(file);
    puts("VC2 source profile original body and eleven corruption gates passed");
    return 0;
}
