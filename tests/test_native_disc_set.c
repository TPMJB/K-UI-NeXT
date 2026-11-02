/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_disc_set.h"
#include "kui/retail_gd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t wire[KUI_NATIVE_SESSION_WIRE_BYTES],copy[KUI_NATIVE_SESSION_WIRE_BYTES];
static struct kui_retail_manifest maps[2];
static struct kui_native_disc_set set;
static unsigned checks,opened,closed,busy;static bool observed,fail;
#define CHECK(x) do {++checks;assert(x);} while(0)
static void put(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8u*i));}
static void checksum(void) {put(wire+KUI_NATIVE_SESSION_CRC_OFFSET,kui_native_session_crc(wire));}
static void fixture(void) {
    memset(wire,0,sizeof(wire));memcpy(wire,KUI_NATIVE_SESSION_MAGIC,8u);
    put(wire+8u,KUI_NATIVE_SESSION_VERSION);put(wire+12u,sizeof(wire));
    memset(maps,0,sizeof(maps));
    for(unsigned i=0;i<2;i++) {
        struct kui_retail_manifest *m=&maps[i];
        m->card_sectors=4096;m->partition_start=50;m->partition_end=4000;
        m->track_count=3;m->extent_count=2;m->session_lba=45000;m->boot_lba=45001;m->boot_bytes=2048;
        m->storage_transport=KUI_STORAGE_SCI;m->reader=KUI_RETAIL_READER_ASYNC;
        m->flags=KUI_RETAIL_IMAGE_BOOT_CRC;m->boot_crc32=1u+i;m->ip_crc32=5u+i;m->gdi_crc32=9u+i;
        strcpy(m->title,"Synthetic disc set");strcpy(m->product,"KUITEST");strcpy(m->region,"JUE");strcpy(m->bootfile,"1ST_READ.BIN");
        m->slots[0].track=(struct kui_retail_track){0,4,3,1,KUI_RETAIL_TRACK_COOKED|4u};
        m->slots[1].track=(struct kui_retail_track){4,8,4,0,0};
        m->slots[2].track=(struct kui_retail_track){45000,45016,4,1,KUI_RETAIL_TRACK_COOKED|4u};
        m->slots[3].extent=(struct kui_retail_extent){0,100u+i*512u,16};
        m->slots[4].extent=(struct kui_retail_extent){0,200u+i*512u,64};
        CHECK(kui_retail_manifest_validate(m)==KUI_GAME_OK);
    }
}
static void configured(void) {
    fixture();put(wire+20u,KUI_NATIVE_SESSION_FLAG_DISCS);put(wire+24u,2u);
    for(unsigned i=0;i<2;i++)CHECK(kui_retail_manifest_encode(&maps[i],wire+2048u+i*4096u)==KUI_GAME_OK);
    checksum();
}
static int control(uint32_t operation,const struct kui_retail_manifest *next,struct kui_loader_sd *card) {
    CHECK(!card);
    if(fail)return -1;
    if(busy) {--busy;return 0;}
    if(operation==KUI_NATIVE_SESSION_DISC_OPEN) {CHECK(!next);++opened;return 1;}
    if(operation==KUI_NATIVE_SESSION_STATUS) {CHECK(!next);return KUI_NATIVE_SESSION_OPEN|(observed?KUI_NATIVE_SESSION_OBSERVED:0u);}
    CHECK(operation==KUI_NATIVE_SESSION_DISC_CLOSE && next);
    CHECK(next==((const void *)set.maps[(set.selected+1u)%set.count]));++closed;return 1;
}
static void wire_checks(void) {
    fixture();size_t at=99u;
    CHECK(kui_native_session_wire_valid(wire,sizeof(wire),false));
    CHECK(!kui_native_session_wire_valid(wire,sizeof(wire),true));
    CHECK(kui_native_session_descriptor_find(wire,sizeof(wire),&at)==1 && at==0u);
    memcpy(copy,wire,sizeof(copy));
    static uint8_t doubled[KUI_NATIVE_SESSION_WIRE_BYTES*2u];
    memcpy(doubled,wire,sizeof(wire));memcpy(doubled+sizeof(wire),wire,sizeof(wire));
    CHECK(kui_native_session_descriptor_find(doubled,sizeof(doubled),NULL)==-1);
    CHECK(kui_native_session_descriptor_find(wire,sizeof(wire)-1u,NULL)==-1);
    configured();CHECK(kui_native_session_wire_valid(wire,sizeof(wire),true));
    memcpy(copy,wire,sizeof(copy));memset(copy+KUI_NATIVE_SESSION_CRC_OFFSET,0,4u);
    uint32_t independent=kui_retail_crc32(0,copy,sizeof(copy));
    CHECK(kui_native_session_crc(wire)==independent);
    CHECK(kui_native_disc_set_prepare_wire(&set,wire,sizeof(wire),&maps[0]));
    CHECK(set.count==2u && !set.selected && set.phase==KUI_NATIVE_DISC_IDLE);
    CHECK(!memcmp(kui_native_disc_set_current(&set),&maps[0],sizeof(maps[0])));
    CHECK(!kui_native_disc_set_prepare_wire(&set,wire,sizeof(wire),NULL));
    CHECK(!kui_native_disc_set_prepare_wire(&set,wire,sizeof(wire),&maps[1]));
    static const unsigned offsets[]={0u,8u,12u,16u,20u,24u,28u,32u,127u,1152u,2047u,2048u,6144u,10240u,18431u};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);++i) {
        configured();wire[offsets[i]]^=0x80u;
        CHECK(!kui_native_disc_set_prepare_wire(&set,wire,sizeof(wire),&maps[0]));
    }
    configured();wire[32u]=1u;checksum();CHECK(!kui_native_session_wire_valid(wire,sizeof(wire),true));
    configured();put(wire+24u,5u);checksum();CHECK(!kui_native_session_wire_valid(wire,sizeof(wire),true));
    configured();maps[1].card_sectors++;
    CHECK(kui_retail_manifest_encode(&maps[1],wire+6144u)==KUI_GAME_OK);checksum();
    CHECK(!kui_native_disc_set_prepare_wire(&set,wire,sizeof(wire),&maps[0]));
    configured();maps[1].flags|=KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED;
    CHECK(kui_retail_manifest_encode(&maps[1],wire+6144u)==KUI_GAME_OK);checksum();
    CHECK(!kui_native_disc_set_prepare_wire(&set,wire,sizeof(wire),&maps[0]));
    configured();
    /* A genuine high-density audio track is legal in the general map codec,
     * but this first session-control profile does not carry a CDDA engine. */
    maps[1].slots[5]=maps[1].slots[4];maps[1].slots[4]=maps[1].slots[3];
    maps[1].track_count=4u;
    maps[1].slots[0].track.first_extent=4u;maps[1].slots[1].track.first_extent=5u;
    maps[1].slots[2].track.first_extent=5u;
    maps[1].slots[3].track=(struct kui_retail_track){45016u,45032u,6u,0u,0u};
    CHECK(kui_retail_manifest_encode(&maps[1],wire+6144u)==KUI_GAME_OK);checksum();
    CHECK(!kui_native_disc_set_prepare_wire(&set,wire,sizeof(wire),&maps[0]));
    configured();memcpy(wire+6144u,wire+2048u,4096u);checksum();
    CHECK(!kui_native_disc_set_prepare_wire(&set,wire,sizeof(wire),&maps[0]));
    configured();put(wire+6144u+20u,65u);memset(wire+6144u+16u,0,4u);
    put(wire+6144u+16u,kui_retail_crc32(0,wire+6144u,4096u));checksum();
    CHECK(!kui_native_disc_set_prepare_wire(&set,wire,sizeof(wire),&maps[0]));
    fixture();put(wire+20u,KUI_NATIVE_SESSION_FLAG_RETURN);checksum();
    CHECK(kui_native_disc_set_prepare_wire(&set,wire,sizeof(wire),NULL) && !set.count);
}
static void lifecycle(void) {
    configured();CHECK(kui_native_disc_set_prepare_wire(&set,wire,sizeof(wire),&maps[0]));
    opened=closed=0;observed=fail=false;busy=2u;
    CHECK(kui_native_disc_set_request_next(&set));CHECK(!kui_native_disc_set_request_next(&set));
    CHECK(!kui_native_disc_set_visit(&set,control) && !opened);
    CHECK(!kui_native_disc_set_visit(&set,control) && !opened);
    CHECK(!kui_native_disc_set_visit(&set,control) && opened==1u && !closed);
    for(unsigned i=0;i<60u;++i)CHECK(!kui_native_disc_set_visit(&set,control) && !closed && !set.selected);
    observed=true;CHECK(kui_native_disc_set_visit(&set,control)==1 && closed==1u && set.selected==1u);
    CHECK(kui_native_disc_set_current(&set)->slots[4].extent.card_lba==712u);
    CHECK(!kui_native_disc_set_visit(&set,control) && closed==1u);
    CHECK(kui_native_disc_set_request_next(&set));CHECK(!kui_native_disc_set_visit(&set,control));
    for(unsigned i=0;i<29u;++i)CHECK(!kui_native_disc_set_visit(&set,control) && closed==1u);
    CHECK(kui_native_disc_set_visit(&set,control)==1 && !set.selected && closed==2u);
    CHECK(kui_native_disc_set_request_next(&set));fail=true;
    CHECK(kui_native_disc_set_visit(&set,control)==-1 && set.phase==KUI_NATIVE_DISC_FAULT);
    CHECK(!kui_native_disc_set_request_next(&set) && kui_native_disc_set_visit(&set,control)==-1);
}
int main(void) {wire_checks();lifecycle();printf("PASS native session disc set: %u checks; bounded descriptor/maps, busy retry, observed lid, delayed close, wrap and fault\n",checks);return 0;}
