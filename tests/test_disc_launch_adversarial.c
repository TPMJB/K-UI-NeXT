/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent packet attacks and full-RAM copy oracle. The copy model uses
 * the actual freestanding minic routines under private names, against an
 * oracle which assigns final destinations from an immutable source image. */
#include "kui/disc_launch.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define memset disc_stage_memset
#define memcpy disc_stage_memcpy
#define memmove disc_stage_memmove
#define memcmp disc_stage_memcmp
#define strlen disc_stage_strlen
#define KUI_RETAIL_FAST_IO 1
#include "../src/loader/minic.c"
#undef memset
#undef memcpy
#undef memmove
#undef memcmp
#undef strlen

static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static void word(uint8_t *p,uint32_t v) {
    for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(v>>(i*8u));
}
static void hash(const void *p,size_t n,uint8_t out[32]) {
    struct kui_sha256 s;kui_sha256_init(&s);kui_sha256_update(&s,p,n);kui_sha256_digest(&s,out);
}
static void reseal(uint8_t *packet) {
    word(packet+112u,0u);word(packet+112u,kui_crc32(0u,packet,512u));
}
static void make_template(uint8_t *p,uint32_t bytes,uint32_t flags) {
    memset(p,0,bytes);
    /* A synthetic stage identifies bytes moved by the real entry routine;
     * it is data for this host model, never a runnable console image. */
    for(uint32_t i=0x2000u;i<bytes;i++)p[i]=(uint8_t)(i*17u+43u);
    memcpy(p+0x100u,"KUIDISC1",8u);
    const uint32_t fields[]={1u,64u,0x1000u,512u,0x8ce00000u,bytes-0x2000u,
        0x8ce00000u,0x8c010000u,KUI_DISC_LAUNCH_EXEC_MAX_BYTES,0x8cff0000u,0x2000u,flags,0u,0u};
    for(unsigned i=0u;i<14u;i++)word(p+0x108u+i*4u,fields[i]);
}
static uint8_t *fixture_mode(uint32_t module,uint32_t executable,uint32_t flags,
    struct kui_disc_launch_plan *plan) {
    uint8_t *p=malloc((size_t)module+32768u+executable+4096u+64u);CHECK(p);
    make_template(p,module,flags);CHECK(kui_disc_launch_layout(p,module,executable,plan));
    memset(p+module,0,plan->payload_bytes-module);
    uint8_t *ip=p+plan->ip_offset,*boot=p+plan->exec_offset;
    memset(ip,0x31,32768u);memcpy(ip,"SEGA SEGAKATANA  ",16u);
    memcpy(ip+37u,"GD-ROM",6u);memcpy(ip+48u,"JUE     ",8u);memcpy(ip+56u,"0000000 ",8u);
    for(uint32_t i=0u;i<executable;i++)boot[i]=(uint8_t)(i*31u+(i>>8u)+11u);
    plan->ip_crc32=kui_crc32(0u,ip,32768u);plan->exec_crc32=kui_crc32(0u,boot,executable);
    hash(ip,32768u,plan->ip_sha256);hash(boot,executable,plan->exec_sha256);
    if(plan->vmu_profile_bytes) {
        uint8_t *profile=p+plan->vmu_profile_offset;
        for(unsigned i=0u;i<plan->vmu_profile_bytes;i++)profile[i]=(uint8_t)(i*47u+(i>>5u)+29u);
        hash(profile,plan->vmu_profile_bytes,plan->vmu_profile_sha256);
    }
    CHECK(kui_disc_launch_encode(p+0x1000u,plan));
    return p;
}
static uint8_t *fixture(uint32_t module,uint32_t executable,struct kui_disc_launch_plan *plan) {
    return fixture_mode(module,executable,KUI_DISC_LAUNCH_FLAGS,plan);
}
static void packet_attacks(void) {
    struct kui_disc_launch_plan plan,out;
    uint8_t *p=fixture(0x2104u,513u,&plan),packet[512],before[512];
    memcpy(packet,p+0x1000u,sizeof(packet));
    for(unsigned at=116u;at<512u;at++) {
        packet[at]=0x80u;reseal(packet);memset(&out,0x5a,sizeof(out));
        CHECK(!kui_disc_launch_decode(packet,&out));
        const struct kui_disc_launch_plan empty={0};CHECK(!memcmp(&out,&empty,sizeof(out)));
        memcpy(packet,p+0x1000u,sizeof(packet));
    }
    const uint32_t hostile[]={0u,1u,127u,KUI_DISC_LAUNCH_EXEC_MAX_BYTES+1u,
        0x7fffffffu,0x80000000u,0xfffffffcu,0xffffffffu};
    const unsigned offsets[]={16u,20u,24u,28u,32u};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++)
        for(unsigned j=0;j<sizeof(hostile)/sizeof(hostile[0]);j++) {
            memcpy(packet,p+0x1000u,sizeof(packet));word(packet+offsets[i],hostile[j]);reseal(packet);
            CHECK(!kui_disc_launch_decode(packet,&out));
        }
    /* Neither exact adjacency nor an alias into the second half of a packet
     * may cause the decoder to erase a still-needed source before refusing. */
    memcpy(before,packet,sizeof(packet));
    CHECK(!kui_disc_launch_decode(packet,(void *)(packet+1u)));
    CHECK(!memcmp(before,packet,sizeof(packet)));
    CHECK(!kui_disc_launch_encode(packet,(const void *)(packet+3u)));
    CHECK(!memcmp(before,packet,sizeof(packet)));
    /* The caller's real bounded length precedes every alleged packet length.
     * A tiny payload may contain any truncated prefix, without an OOB read. */
    for(size_t n=0u;n<0x2004u;n+=13u)
        CHECK(!kui_disc_launch_validate(p,n,&out));
    free(p);
}
static void copy_oracle(void) {
    const struct {uint32_t bytes,flags;} modules[]={
        {0x2004u,KUI_DISC_LAUNCH_FLAGS},{0x2180u,KUI_DISC_LAUNCH_FLAGS},
        {KUI_DISC_LAUNCH_STAGE_OFFSET+KUI_DISC_LAUNCH_STAGE_MAX_BYTES,KUI_DISC_LAUNCH_FLAGS},
        {KUI_DISC_LAUNCH_STAGE_OFFSET+KUI_DISC_LAUNCH_VMU_STAGE_MAX_BYTES,KUI_DISC_LAUNCH_VMU_FLAGS}};
    const uint32_t sizes[]={128u,129u,32767u,32768u,32769u,0x3fffffu,0x400000u,
        0x400001u,5460080u,6u*1024u*1024u,KUI_DISC_LAUNCH_EXEC_MAX_BYTES};
    uint8_t *ram=malloc(0x1000000u),*expected=malloc(0x1000000u);CHECK(ram && expected);
    for(unsigned m=0;m<sizeof(modules)/sizeof(modules[0]);m++)
        for(unsigned n=0;n<sizeof(sizes)/sizeof(sizes[0]);n++) {
            struct kui_disc_launch_plan plan,decoded;
            uint8_t *p=fixture_mode(modules[m].bytes,sizes[n],modules[m].flags,&plan);
            memset(ram,0xa5,0x1000000u);
            for(unsigned i=0u;i<0x8000u;i++)ram[i]=(uint8_t)(i*7u+(i>>9u));
            word(ram+0xbcu,0x8c001000u);
            memcpy(ram+0x10000u,p,plan.payload_bytes);
            memcpy(expected,ram,0x1000000u);
            CHECK(kui_disc_launch_validate(ram+0x10000u,plan.payload_bytes,&decoded));
            CHECK(!memcmp(&plan,&decoded,sizeof(plan)));
            /* Model the console entry's word copy and the high C stage's
             * moves using the actual freestanding byte-copy implementation. */
            CHECK(disc_stage_memcpy(ram+0xe00000u,ram+0x12000u,modules[m].bytes-0x2000u)==ram+0xe00000u);
            /* The VMU stage admits and snapshots its profile before moving
             * overlapping game sources. Keep guards around that owned copy. */
            if(plan.vmu_profile_bytes)
                CHECK(disc_stage_memcpy(ram+0xfe0000u,ram+0x10000u+plan.vmu_profile_offset,
                    plan.vmu_profile_bytes)==ram+0xfe0000u);
            CHECK(disc_stage_memmove(ram+0x8000u,ram+0x10000u+plan.ip_offset,32768u)==ram+0x8000u);
            CHECK(disc_stage_memmove(ram+0x10000u,ram+0x10000u+plan.exec_offset,sizes[n])==ram+0x10000u);
            ram[0x80fcu]&=(uint8_t)~0x20u;
            /* Independent final-state oracle: sources come from immutable p,
             * not from any overlapping RAM location that a move could clobber. */
            memcpy(expected+0xe00000u,p+0x2000u,modules[m].bytes-0x2000u);
            if(plan.vmu_profile_bytes)
                memcpy(expected+0xfe0000u,p+plan.vmu_profile_offset,plan.vmu_profile_bytes);
            memcpy(expected+0x8000u,p+plan.ip_offset,32768u);
            memcpy(expected+0x10000u,p+plan.exec_offset,sizes[n]);
            expected[0x80fcu]&=(uint8_t)~0x20u;
            CHECK(!memcmp(ram,expected,0x1000000u));
            CHECK(!memcmp(ram,expected,0x8000u));
            CHECK(ram[0xfdffffu]==0xa5u && ram[0xfe1000u]==0xa5u);
            free(p);
        }
    free(ram);free(expected);
}
int main(void) {
    packet_attacks();copy_oracle();
    printf("Disc launch independent adversarial: %u checks; resealed size attacks and full-RAM overlap/canary oracle through 12 MiB with maximum VMU module\n",checks);
    return 0;
}
