/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_return.h"
#include "kui/storage_policy.h"
#include "kui/probe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t card[32][512],payload[1028],destination[1032],wire[1024];
static struct kui_native_return_plan plan;
static unsigned reads,fail_read,checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(n>>(8u*i));}
static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void reseal_wire(void) {put32(wire+16,0u);put32(wire+16,kui_crc32(0,wire,sizeof(wire)));}
static void header(void) {
    memset(plan.header,0,64);memcpy(plan.header,"KUIRUN1\0",8);
    put32(plan.header+8,1);put32(plan.header+12,64);put32(plan.header+16,sizeof(payload));
    put32(plan.header+20,KUI_NATIVE_RETURN_ADDRESS);put32(plan.header+24,KUI_NATIVE_RETURN_ADDRESS);
    put32(plan.header+28,UINT32_C(0x700000));put32(plan.header+32,kui_crc32(0,payload,sizeof(payload)));
    memcpy(plan.header+40,"0123456789ab",12);put32(plan.header+60,kui_crc32(0,plan.header,60));
}
static void publish(void) {
    memset(card,0xa5,sizeof(card));
    uint8_t file[64+sizeof(payload)];memcpy(file,plan.header,64);memcpy(file+64,payload,sizeof(payload));
    for(unsigned block=0;block<(sizeof(file)+511)/512;block++) {
        unsigned lba=block?20u+block-1u:10u,offset=block*512u;
        unsigned bytes=sizeof(file)-offset;if(bytes>512)bytes=512;
        memcpy(card[lba],file+offset,bytes);
    }
    memset(destination,0xcc,sizeof(destination));reads=fail_read=0;
}
static void fixture(void) {
    memset(&plan,0,sizeof(plan));
    plan.version=1;plan.bytes=sizeof(plan);plan.file_bytes=64+sizeof(payload);plan.extent_count=2;
    plan.card_sectors=32;plan.partition_first=4;plan.partition_end=31;
    plan.extents[0]=(struct kui_native_return_extent){0,10,1};
    plan.extents[1]=(struct kui_native_return_extent){1,20,2};
    for(unsigned i=0;i<sizeof(payload);i++)payload[i]=(uint8_t)(i*17u+13u);
    struct kui_storage_boot_marker marker=KUI_STORAGE_BOOT_INITIALIZER;
    memcpy(payload+300,&marker,sizeof(marker));header();publish();
}
static bool read_sector(void *context,uint32_t lba,uint8_t sector[512]) {
    CHECK(context==&plan);CHECK(lba==10 || lba==20 || lba==21);++reads;
    if(reads==fail_read)return false;
    memcpy(sector,card[lba],512);return true;
}
static int load(void) {return kui_native_return_load(&plan,UINT32_C(0x8c500000),UINT32_C(0x51000),
    destination,sizeof(destination),read_sector,&plan);}
static void codec(void) {
    fixture();CHECK(kui_native_return_plan_valid(&plan));
    CHECK(kui_native_return_encode(wire,&plan));struct kui_native_return_plan decoded={0};
    CHECK(kui_native_return_decode(&decoded,wire,sizeof(wire)));CHECK(!memcmp(&decoded,&plan,sizeof(plan)));
    for(unsigned i=0;i<sizeof(wire);i++) {
        wire[i]^=1u;CHECK(!kui_native_return_decode(&decoded,wire,sizeof(wire)));wire[i]^=1u;
    }
    CHECK(!kui_native_return_decode(&decoded,wire,sizeof(wire)-1u));
    wire[900]=1;reseal_wire();CHECK(!kui_native_return_decode(&decoded,wire,sizeof(wire)));
    CHECK(kui_native_return_encode(wire,&plan));put32(wire+20,1);reseal_wire();
    CHECK(!kui_native_return_decode(&decoded,wire,sizeof(wire)));
    fixture();plan.extents[1].card_lba=10;CHECK(!kui_native_return_plan_valid(&plan));
    fixture();plan.extents[1].file_block=2;CHECK(!kui_native_return_plan_valid(&plan));
    fixture();plan.extents[1].blocks=3;CHECK(!kui_native_return_plan_valid(&plan));
    fixture();plan.extents[1].card_lba=30;CHECK(!kui_native_return_plan_valid(&plan));
    fixture();plan.extents[3].blocks=1;CHECK(!kui_native_return_plan_valid(&plan));
    fixture();plan.header[40]='g';put32(plan.header+60,kui_crc32(0,plan.header,60));CHECK(!kui_native_return_plan_valid(&plan));
}
static void leases(void) {
    fixture();
    CHECK(kui_native_return_lease_safe(&plan,UINT32_C(0x8c500000),UINT32_C(0x51000)));
    CHECK(kui_native_return_lease_safe(&plan,UINT32_C(0xac500000),UINT32_C(0x51000)));
    CHECK(kui_native_return_lease_safe(&plan,UINT32_C(0x0c500000),UINT32_C(0x51000)));
    CHECK(!kui_native_return_lease_safe(&plan,UINT32_C(0x8c010020),UINT32_C(0x51000)));
    CHECK(!kui_native_return_lease_safe(&plan,UINT32_C(0x8c500001),UINT32_C(0x51000)));
    CHECK(!kui_native_return_lease_safe(&plan,UINT32_C(0xcc500000),UINT32_C(0x51000)));
    CHECK(!kui_native_return_lease_safe(&plan,UINT32_C(0x8cff0000),UINT32_C(0x51000)));
}
static void loading(void) {
    fixture();CHECK(load()==KUI_NATIVE_RETURN_OK);CHECK(reads==3);
    CHECK(get32(destination+312)==KUI_STORAGE_SCI);CHECK(get32(destination+316)==~(uint32_t)KUI_STORAGE_SCI);
    CHECK(kui_storage_patch_boot(payload,sizeof(payload),KUI_STORAGE_SCI));
    CHECK(!memcmp(destination,payload,sizeof(payload)));
    for(unsigned i=sizeof(payload);i<sizeof(destination);i++)CHECK(destination[i]==0xcc);
    fixture();card[10][0]^=1u;CHECK(load()==KUI_NATIVE_RETURN_CHANGED);CHECK(reads==1);
    for(unsigned i=0;i<sizeof(destination);i++)CHECK(destination[i]==0xcc);
    fixture();fail_read=2;CHECK(load()==KUI_NATIVE_RETURN_IO);CHECK(reads==2);
    fixture();card[20][14]^=1;CHECK(load()==KUI_NATIVE_RETURN_CRC);
    CHECK(get32(destination+312)==KUI_STORAGE_AUTO);
    fixture();memset(payload+300,0,20);header();publish();CHECK(load()==KUI_NATIVE_RETURN_HEADER);
    fixture();memcpy(payload+700,payload+300,20);header();publish();CHECK(load()==KUI_NATIVE_RETURN_HEADER);
    fixture();CHECK(kui_native_return_load(&plan,UINT32_C(0x8c010020),UINT32_C(0x51000),destination,
        sizeof(destination),read_sector,&plan)==KUI_NATIVE_RETURN_LEASE);CHECK(!reads);
    CHECK(kui_native_return_load(&plan,UINT32_C(0x8c500000),UINT32_C(0x51000),destination,
        12,read_sector,&plan)==KUI_NATIVE_RETURN_ARGUMENT);CHECK(!reads);
}
int main(void) {codec();leases();loading();printf("PASS native return: %u checks; pinned header, bounded fragmented reads, payload CRC, SCI marker and live-lease exclusion\n",checks);return 0;}
