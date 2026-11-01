/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_launch.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static void put(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(v>>(i*8u));}
static void digest(const void *p,size_t bytes,uint8_t out[32]) {
    struct kui_sha256 h;kui_sha256_init(&h);kui_sha256_update(&h,p,bytes);kui_sha256_digest(&h,out);
}
static void packet_crc(uint8_t *p) {put(p+112u,0u);put(p+112u,kui_crc32(0u,p,512u));}
int main(void) {
    uint8_t *p=calloc(1u,0x600000u);CHECK(p);
    const uint32_t module=0x22004u;
    uint8_t *h=p+0x100u;memcpy(h,"KUIDISC1",8u);
    const uint32_t fields[]={1u,64u,0x1000u,512u,0x8ce00000u,module-0x2000u,
        0x8ce00000u,0x8c010000u,0x400000u,0x8cff0000u,0x2000u,3u,0u,0u};
    for(unsigned i=0;i<14u;i++)put(h+8u+i*4u,fields[i]);
    struct kui_disc_launch_plan plan,out;
    CHECK(kui_disc_launch_layout(p,module,10003u,&plan));
    CHECK(plan.flags==3u && plan.vmu_profile_bytes==4096u && plan.vmu_transport==1u);
    CHECK(!(plan.vmu_profile_offset&31u));
    CHECK(plan.vmu_profile_offset>=plan.exec_offset+plan.exec_bytes);
    CHECK(plan.payload_bytes==plan.vmu_profile_offset+4096u);
    uint8_t *ip=p+plan.ip_offset,*exec=p+plan.exec_offset,*profile=p+plan.vmu_profile_offset;
    memset(ip,' ',32768u);memcpy(ip,"SEGA SEGAKATANA ",16u);
    memcpy(ip+37u,"GD-ROM",6u);memcpy(ip+56u,"0000000 ",8u);
    for(unsigned i=0;i<plan.exec_bytes;i++) exec[i]=(uint8_t)(i*37u+17u);
    for(unsigned i=0;i<4096u;i++) profile[i]=(uint8_t)(i*53u+23u);
    plan.ip_crc32=kui_crc32(0u,ip,32768u);plan.exec_crc32=kui_crc32(0u,exec,plan.exec_bytes);
    digest(ip,32768u,plan.ip_sha256);digest(exec,plan.exec_bytes,plan.exec_sha256);
    digest(profile,4096u,plan.vmu_profile_sha256);
    CHECK(kui_disc_launch_encode(p+0x1000u,&plan));
    CHECK(kui_disc_launch_validate(p,plan.payload_bytes,&out) && !memcmp(&out,&plan,sizeof(plan)));
    for(unsigned i=0;i<4096u;i+=31u) {
        profile[i]^=1u;CHECK(!kui_disc_launch_validate(p,plan.payload_bytes,&out));profile[i]^=1u;
    }
    for(unsigned i=plan.exec_offset+plan.exec_bytes;i<plan.vmu_profile_offset;i++) {
        p[i]=1u;CHECK(!kui_disc_launch_validate(p,plan.payload_bytes,&out));p[i]=0u;
    }
    const uint32_t offsets[]={36u,116u,120u,124u,128u,156u,160u,508u};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        p[0x1000u+offsets[i]]^=4u;packet_crc(p+0x1000u);
        CHECK(!kui_disc_launch_validate(p,plan.payload_bytes,&out));
        CHECK(kui_disc_launch_encode(p+0x1000u,&plan));
    }
    struct kui_disc_launch_plan bad=plan;
    bad.vmu_profile_offset=UINT32_MAX;CHECK(!kui_disc_launch_encode(p+0x1000u,&bad));
    bad=plan;bad.vmu_profile_bytes=4092u;CHECK(!kui_disc_launch_encode(p+0x1000u,&bad));
    bad=plan;bad.vmu_transport=0u;CHECK(!kui_disc_launch_encode(p+0x1000u,&bad));
    bad=plan;bad.flags=1u;CHECK(!kui_disc_launch_encode(p+0x1000u,&bad));
    CHECK(kui_disc_launch_encode(p+0x1000u,&plan));
    /* A baseline module cannot accidentally consume a VMU configuration. */
    put(h+52u,1u);CHECK(!kui_disc_launch_validate(p,plan.payload_bytes,&out));
    CHECK(!kui_disc_launch_layout(p,module,10003u,&out));
    puts("PASS physical-disc VMU packet: sealed profile, bounded disjoint geometry, SCI transport, mode capability and padding checks");
    printf("%u checks\n",checks);free(p);return 0;
}
