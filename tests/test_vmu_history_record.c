/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_history_record.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static void seal(uint8_t wire[512]) {
    uint32_t crc=kui_crc32(0,wire,508u);
    for(unsigned i=0;i<4u;i++)wire[508u+i]=(uint8_t)(crc>>(i*8u));
}
int main(void) {
    struct kui_vmu_history_record r={.sequence=17u,.timestamp_seconds=1793700000u,
        .source_pair=9999u,.reason=KUI_VMU_HISTORY_AFTER_RETURN,.image_crc32=UINT32_C(0x12345678)};
    CHECK(kui_vmu_game_card_key("MK-51000",r.product));
    memset(r.source_identity,0x71,16u);memset(r.sha256,0xa5,32u);
    uint8_t wire[512],saved[512];struct kui_vmu_history_record decoded={0};
    CHECK(kui_vmu_history_record_encode(&r,wire));memcpy(saved,wire,sizeof(saved));
    CHECK(kui_vmu_history_record_decode(wire,sizeof(wire),&decoded));
    CHECK(decoded.sequence==r.sequence && decoded.timestamp_seconds==r.timestamp_seconds &&
        decoded.source_pair==r.source_pair && decoded.reason==r.reason &&
        decoded.image_crc32==r.image_crc32 && !memcmp(decoded.product,r.product,12u) &&
        !memcmp(decoded.source_identity,r.source_identity,16u) && !memcmp(decoded.sha256,r.sha256,32u));
    for(unsigned i=0;i<512u;i++) {
        wire[i]^=1u;CHECK(!kui_vmu_history_record_decode(wire,sizeof(wire),&decoded));wire[i]^=1u;
    }
    CHECK(!kui_vmu_history_record_decode(wire,511u,&decoded));
    CHECK(!kui_vmu_history_record_decode(wire,513u,&decoded));
    for(unsigned i=104u;i<508u;i++) {
        wire[i]=1u;seal(wire);CHECK(!kui_vmu_history_record_decode(wire,sizeof(wire),&decoded));memcpy(wire,saved,512u);
    }
    for(unsigned field=0;field<7u;field++) {
        struct kui_vmu_history_record bad=r;
        if(field==0u)bad.sequence=0u;
        if(field==1u)bad.source_pair=0u;
        if(field==2u)bad.source_pair=10000u;
        if(field==3u)bad.reason=0u;
        if(field==4u)memset(bad.source_identity,0,16u);
        if(field==5u)memset(bad.sha256,0,32u);
        if(field==6u)bad.product[0]='m';
        CHECK(!kui_vmu_history_record_encode(&bad,wire));
    }
    union {struct kui_vmu_history_record record;uint8_t wire[512];} alias;
    alias.record=r;struct kui_vmu_history_record before=alias.record;
    CHECK(!kui_vmu_history_record_encode(&alias.record,alias.wire));
    CHECK(!memcmp(&alias.record,&before,sizeof(before)));
    memcpy(alias.wire,saved,512u);CHECK(kui_vmu_history_record_decode(alias.wire,512u,&alias.record));
    CHECK(alias.record.sequence==r.sequence && !memcmp(alias.record.sha256,r.sha256,32u));
    printf("VMU history record: %u checks; canonical provenance, every-byte corruption, padding and alias boundaries passed\n",checks);
    return 0;
}
