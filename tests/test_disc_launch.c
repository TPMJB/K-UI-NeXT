/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_launch.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static void put(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(v>>(8u*i));}
static uint32_t get(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void digest(const void *data,size_t n,uint8_t out[32]) {
    struct kui_sha256 s;kui_sha256_init(&s);kui_sha256_update(&s,data,n);kui_sha256_digest(&s,out);
}
static void template(uint8_t *p,size_t n) {
    memset(p,0,n);uint8_t *h=p+KUI_DISC_LAUNCH_HEADER_OFFSET;
    memcpy(h,"KUIDISC1",8u);
    const uint32_t fields[]={1u,64u,0x1000u,512u,0x8ce00000u,(uint32_t)n-0x2000u,
        0x8ce00000u,0x8c010000u,0x400000u,0x8cff0000u,0x2000u,1u,0u,0u};
    for(unsigned i=0;i<14u;i++) put(h+8u+4u*i,fields[i]);
}
static bool zero(const void *p,size_t n) {const uint8_t *q=p;while(n--) if(*q++)return false;return true;}
int main(void) {
    const size_t module_bytes=0x2024u;uint8_t *p=malloc(0x500000u),*saved=malloc(0x500000u);
    CHECK(p && saved);template(p,module_bytes);struct kui_disc_launch_plan plan,out;
    CHECK(kui_disc_launch_layout(p,module_bytes,128u,&plan));
    CHECK(plan.ip_offset==0x2040u && plan.exec_offset==0xa040u && plan.payload_bytes==0xa0c0u);
    CHECK(!kui_disc_launch_layout(p,module_bytes,127u,&out) && zero(&out,sizeof(out)));
    CHECK(!kui_disc_launch_layout(p,module_bytes,UINT32_MAX,&out) && zero(&out,sizeof(out)));
    CHECK(!kui_disc_launch_layout(p,module_bytes,0x400001u,&out));
    for(unsigned i=0;i<64u;i++) {
        p[0x100u+i]^=1u;CHECK(!kui_disc_launch_layout(p,module_bytes,128u,&out));p[0x100u+i]^=1u;
    }
    for(unsigned i=0;i<512u;i++) {
        p[0x1000u+i]=1u;CHECK(!kui_disc_launch_layout(p,module_bytes,128u,&out));p[0x1000u+i]=0u;
    }
    CHECK(kui_disc_launch_layout(p,module_bytes,0x400000u,&out));
    CHECK(out.payload_bytes<0x500000u && KUI_DISC_LAUNCH_LOAD_ADDRESS+out.payload_bytes<KUI_DISC_LAUNCH_STAGE_ADDRESS);
    CHECK(kui_disc_launch_layout(p,module_bytes,10003u,&plan));
    memset(p+module_bytes,0,plan.payload_bytes-module_bytes);
    uint8_t *ip=p+plan.ip_offset,*exec=p+plan.exec_offset;
    memset(ip,' ',KUI_DISC_LAUNCH_IP_BYTES);memcpy(ip,"SEGA SEGAKATANA ",16u);
    memcpy(ip+37u,"GD-ROM",6u);memcpy(ip+56u,"0000000 ",8u);memcpy(ip+48u,"JUE     ",8u);
    for(uint32_t i=0;i<plan.exec_bytes;i++) exec[i]=(uint8_t)(i*53u+7u);
    plan.ip_crc32=kui_crc32(0u,ip,KUI_DISC_LAUNCH_IP_BYTES);plan.exec_crc32=kui_crc32(0u,exec,plan.exec_bytes);
    digest(ip,KUI_DISC_LAUNCH_IP_BYTES,plan.ip_sha256);digest(exec,plan.exec_bytes,plan.exec_sha256);
    CHECK(kui_disc_launch_encode(p+0x1000u,&plan));
    CHECK(kui_disc_launch_decode(p+0x1000u,&out) && !memcmp(&out,&plan,sizeof(plan)));
    CHECK(kui_disc_launch_validate(p,plan.payload_bytes,&out) && !memcmp(&out,&plan,sizeof(plan)));
    memcpy(saved,p,plan.payload_bytes);
    for(unsigned i=0;i<512u;i++) {
        p[0x1000u+i]^=1u;CHECK(!kui_disc_launch_validate(p,plan.payload_bytes,&out) && zero(&out,sizeof(out)));
        p[0x1000u+i]^=1u;
    }
    const unsigned geometry_fields[]={16u,20u,24u,28u,32u,36u};
    for(unsigned i=0;i<sizeof(geometry_fields)/sizeof(geometry_fields[0]);i++) {
        uint8_t *packet=p+0x1000u;unsigned at=geometry_fields[i];put(packet+at,get(packet+at)^4u);
        put(packet+112u,0u);put(packet+112u,kui_crc32(0u,packet,512u));
        CHECK(!kui_disc_launch_validate(p,plan.payload_bytes,&out));memcpy(p,saved,plan.payload_bytes);
    }
    for(uint32_t i=0;i<KUI_DISC_LAUNCH_IP_BYTES;i+=31u) {
        ip[i]^=1u;CHECK(!kui_disc_launch_validate(p,plan.payload_bytes,&out));ip[i]^=1u;
    }
    for(uint32_t i=0;i<plan.exec_bytes;i+=23u) {
        exec[i]^=1u;CHECK(!kui_disc_launch_validate(p,plan.payload_bytes,&out));exec[i]^=1u;
    }
    for(unsigned i=0;i<64u;i++) {
        p[0x100u+i]^=1u;CHECK(!kui_disc_launch_validate(p,plan.payload_bytes,&out));p[0x100u+i]^=1u;
    }
    CHECK(!kui_disc_launch_validate(p,plan.payload_bytes-1u,&out));
    CHECK(!kui_disc_launch_validate(p,plan.payload_bytes+4u,&out));
    for(unsigned i=0;i<3u;i++) {
        memset(ip+48u,' ',8u);ip[48u]="JUE"[i];
        plan.ip_crc32=kui_crc32(0u,ip,KUI_DISC_LAUNCH_IP_BYTES);digest(ip,KUI_DISC_LAUNCH_IP_BYTES,plan.ip_sha256);
        CHECK(kui_disc_launch_encode(p+0x1000u,&plan));CHECK(kui_disc_launch_validate(p,plan.payload_bytes,&out));
    }
    /* A fresh matching fingerprint does not excuse a malformed header tail. */
    for(unsigned i=14u;i<16u;i++) {
        uint8_t original=ip[i];
        ip[i]='!';plan.ip_crc32=kui_crc32(0u,ip,KUI_DISC_LAUNCH_IP_BYTES);
        digest(ip,KUI_DISC_LAUNCH_IP_BYTES,plan.ip_sha256);
        CHECK(kui_disc_launch_encode(p+0x1000u,&plan));CHECK(!kui_disc_launch_validate(p,plan.payload_bytes,&out));
        ip[i]=original;
    }
    /* Valid hashes cannot authorize CE or a non-GD medium. */
    ip[62u]='1';plan.ip_crc32=kui_crc32(0u,ip,KUI_DISC_LAUNCH_IP_BYTES);digest(ip,KUI_DISC_LAUNCH_IP_BYTES,plan.ip_sha256);
    CHECK(kui_disc_launch_encode(p+0x1000u,&plan));CHECK(!kui_disc_launch_validate(p,plan.payload_bytes,&out));
    ip[62u]='0';ip[37u]='C';plan.ip_crc32=kui_crc32(0u,ip,KUI_DISC_LAUNCH_IP_BYTES);digest(ip,KUI_DISC_LAUNCH_IP_BYTES,plan.ip_sha256);
    CHECK(kui_disc_launch_encode(p+0x1000u,&plan));CHECK(!kui_disc_launch_validate(p,plan.payload_bytes,&out));
    /* Aliased outputs refuse without corrupting the input image. */
    memcpy(p,saved,plan.payload_bytes);CHECK(!kui_disc_launch_validate(p,plan.payload_bytes,(void *)(p+0x1000u)));
    CHECK(!memcmp(p,saved,plan.payload_bytes));
    CHECK(!kui_disc_launch_encode(p+0x1000u,(const void *)(p+0x1000u)));
    CHECK(!memcmp(p,saved,plan.payload_bytes));
    printf("Physical-disc launch packet: %u checks; full fingerprints, geometry, regions and no partial admission\n",checks);
    free(saved);free(p);return 0;
}
