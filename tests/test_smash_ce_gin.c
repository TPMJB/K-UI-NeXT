/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_ce_gin.h"
#include "kui/retail_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static struct kui_smash_ce_gin_seed blank(unsigned profile) {
    return (struct kui_smash_ce_gin_seed){.magic0=KUI_SMASH_CE_GIN_MAGIC0,
        .magic1=KUI_SMASH_CE_GIN_MAGIC1,.version=1u,.bytes=KUI_SMASH_CE_GIN_BYTES,.profile=profile};
}
static void seal(struct kui_smash_ce_gin_seed *s) {
    s->crc32=0u;s->crc32=kui_retail_crc32(0u,s,sizeof(*s));
}
static void original_frame(uint32_t frame[21],uint32_t pattern) {
    memset(frame,0,21u*sizeof(*frame));
    frame[0]=0x28u;frame[2]=0x8c000000u;frame[3]=0x8c00f400u;frame[4]=0x700000f0u;
    for(unsigned i=13u;i<21u;i++)frame[i]=pattern^(0x01030507u*i);
}
int main(void) {
    uint8_t prefix[KUI_SMASH_CE_GIN_OFFSET+KUI_SMASH_CE_GIN_BYTES+3u];
    uint32_t frame[21];
    const uint32_t ccrs[]={0u,0x100u,0x101u,0x109u,0x909u,0x129u};
    for(unsigned profile=1u;profile<=2u;profile++) {
        struct kui_smash_ce_gin_seed seed=blank(profile),fresh=seed;
        CHECK(kui_smash_ce_gin_blank(&seed));CHECK(!kui_smash_ce_gin_valid(&seed));
        memset(prefix,0xa7,sizeof(prefix));memcpy(prefix+KUI_SMASH_CE_GIN_OFFSET,&seed,sizeof(seed));
        CHECK(kui_smash_ce_gin_capable(prefix,sizeof(prefix),profile));
        CHECK(!kui_smash_ce_gin_capable(prefix,sizeof(prefix),3u-profile));
        CHECK(!kui_smash_ce_gin_capable(prefix,KUI_SMASH_CE_GIN_OFFSET+sizeof(seed)-1u,profile));
        /* The record need not have host alignment; the reader uses memcpy. */
        uint8_t unaligned[sizeof(prefix)+1u];memcpy(unaligned+1u,prefix,sizeof(prefix));
        CHECK(kui_smash_ce_gin_capable(unaligned+1u,sizeof(prefix),profile));
        for(unsigned bit=0u;bit<sizeof(seed)*8u;bit++) {
            prefix[KUI_SMASH_CE_GIN_OFFSET+bit/8u]^=(uint8_t)(1u<<(bit%8u));
            CHECK(!kui_smash_ce_gin_capable(prefix,sizeof(prefix),profile));
            prefix[KUI_SMASH_CE_GIN_OFFSET+bit/8u]^=(uint8_t)(1u<<(bit%8u));
        }
        for(unsigned pattern=0u;pattern<4u;pattern++)for(unsigned c=0u;c<sizeof(ccrs)/sizeof(ccrs[0]);c++) {
            original_frame(frame,pattern*0x31415926u);seed=fresh;
            CHECK(kui_smash_ce_gin_capture(&seed,frame,KUI_SMASH_CE_GIN_FRAME_P1,ccrs[c]));
            CHECK(kui_smash_ce_gin_valid(&seed));CHECK(!kui_smash_ce_gin_blank(&seed));
            CHECK(!memcmp(seed.frame,frame,sizeof(frame)));CHECK(seed.ccr==ccrs[c]);
            struct kui_smash_ce_gin_seed before=seed;
            CHECK(!kui_smash_ce_gin_capture(&seed,frame,KUI_SMASH_CE_GIN_FRAME_P1,ccrs[c]));
            CHECK(!memcmp(&before,&seed,sizeof(seed)));
            for(unsigned bit=0u;bit<sizeof(seed)*8u;bit++) {
                struct kui_smash_ce_gin_seed changed=seed;
                ((uint8_t *)&changed)[bit/8u]^=(uint8_t)(1u<<(bit%8u));
                CHECK(!kui_smash_ce_gin_valid(&changed));
            }
        }
        original_frame(frame,0xdeadbeefu);seed=fresh;
        CHECK(kui_smash_ce_gin_capture(&seed,frame,KUI_SMASH_CE_GIN_FRAME_P1+0x20000000u,0x109u));
        CHECK(kui_smash_ce_gin_valid(&seed));
        for(unsigned i=0u;i<13u;i++) {
            struct kui_smash_ce_gin_seed changed=seed;changed.frame[i]^=1u;seal(&changed);
            CHECK(!kui_smash_ce_gin_valid(&changed));
            uint32_t wrong[21];memcpy(wrong,frame,sizeof(wrong));wrong[i]^=1u;
            changed=fresh;
            CHECK(!kui_smash_ce_gin_capture(&changed,wrong,KUI_SMASH_CE_GIN_FRAME_P1,0x109u));
            CHECK(!memcmp(&changed,&fresh,sizeof(changed)));
        }
        seed=fresh;CHECK(!kui_smash_ce_gin_capture(&seed,frame,KUI_SMASH_CE_GIN_FRAME_P1+4u,0x109u));
        CHECK(!memcmp(&seed,&fresh,sizeof(seed)));
        CHECK(!kui_smash_ce_gin_capture(&seed,frame,KUI_SMASH_CE_GIN_FRAME_P1,0x189u));
        CHECK(!memcmp(&seed,&fresh,sizeof(seed)));
        CHECK(!kui_smash_ce_gin_capture(&seed,NULL,KUI_SMASH_CE_GIN_FRAME_P1,0x109u));
        CHECK(!memcmp(&seed,&fresh,sizeof(seed)));
    }
    struct kui_smash_ce_gin_seed invalid=blank(0u);CHECK(!kui_smash_ce_gin_blank(&invalid));
    invalid=blank(3u);CHECK(!kui_smash_ce_gin_blank(&invalid));
    CHECK(!kui_smash_ce_gin_blank(NULL));CHECK(!kui_smash_ce_gin_valid(NULL));
    CHECK(!kui_smash_ce_gin_capable(NULL,sizeof(prefix),1u));
    original_frame(frame,0u);CHECK(!kui_smash_ce_gin_capture(NULL,frame,KUI_SMASH_CE_GIN_FRAME_P1,0u));
    printf("Smash CE GIN seed: %u checks passed; actual banked frame preserved, typed blank records and CRC mutations refused\n",checks);
    return 0;
}
