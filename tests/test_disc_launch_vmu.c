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
static bool zero(const void *p,size_t bytes) {
    const uint8_t *at=p;for(size_t i=0;i<bytes;i++)if(at[i])return false;return true;
}
static void module_header(uint8_t *p,uint32_t module,uint32_t cap) {
    uint8_t *h=p+0x100u;memcpy(h,"KUIDISC1",8u);
    const uint32_t fields[]={1u,64u,0x1000u,512u,0x8ce00000u,module-0x2000u,
        0x8ce00000u,0x8c010000u,cap,0x8cff0000u,0x2000u,3u,0u,0u};
    for(unsigned i=0;i<14u;i++)put(h+8u+i*4u,fields[i]);
}
static uint8_t *payload(uint32_t module,uint32_t bytes,uint32_t cap,struct kui_disc_launch_plan *plan) {
    size_t capacity=(size_t)module+31u+32768u+bytes+31u+4096u;
    uint8_t *p=calloc(1u,capacity);CHECK(p);module_header(p,module,cap);
    CHECK(kui_disc_launch_layout(p,module,bytes,plan));
    CHECK(plan->flags==3u && plan->module_bytes==module && plan->exec_bytes==bytes);
    CHECK(plan->vmu_profile_bytes==4096u && plan->vmu_transport==1u);
    CHECK(!(plan->vmu_profile_offset&31u));
    CHECK(plan->ip_offset>=module && plan->exec_offset==plan->ip_offset+32768u);
    CHECK(plan->vmu_profile_offset>=plan->exec_offset+bytes);
    CHECK(plan->payload_bytes==plan->vmu_profile_offset+4096u && plan->payload_bytes<=capacity);
    CHECK(KUI_DISC_LAUNCH_LOAD_ADDRESS+plan->payload_bytes<KUI_DISC_LAUNCH_STAGE_ADDRESS);
    CHECK(KUI_DISC_LAUNCH_EXEC_ADDRESS+bytes<KUI_DISC_LAUNCH_STAGE_ADDRESS);
    uint8_t *ip=p+plan->ip_offset,*exec=p+plan->exec_offset,*profile=p+plan->vmu_profile_offset;
    memset(ip,' ',32768u);memcpy(ip,"SEGA SEGAKATANA ",16u);
    memcpy(ip+37u,"GD-ROM",6u);memcpy(ip+56u,"0000000 ",8u);
    for(uint32_t i=0;i<bytes;i++) exec[i]=(uint8_t)(i*37u+(i>>16u)+17u);
    for(unsigned i=0;i<4096u;i++) profile[i]=(uint8_t)(i*53u+23u);
    plan->ip_crc32=kui_crc32(0u,ip,32768u);plan->exec_crc32=kui_crc32(0u,exec,bytes);
    digest(ip,32768u,plan->ip_sha256);digest(exec,bytes,plan->exec_sha256);
    digest(profile,4096u,plan->vmu_profile_sha256);
    CHECK(kui_disc_launch_encode(p+0x1000u,plan));return p;
}
static void small_packet(void) {
    const uint32_t module=0x22004u;
    struct kui_disc_launch_plan plan,out;
    uint8_t *p=payload(module,10003u,KUI_DISC_LAUNCH_EXEC_MAX_BYTES,&plan);
    uint8_t *h=p+0x100u,*profile=p+plan.vmu_profile_offset;
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
    free(p);
}
static void large_packets(void) {
    const uint32_t sizes[]={0x400001u,5460080u,6u*1024u*1024u,KUI_DISC_LAUNCH_EXEC_MAX_BYTES};
    const uint32_t module=KUI_DISC_LAUNCH_STAGE_OFFSET+KUI_DISC_LAUNCH_VMU_STAGE_MAX_BYTES;
    CHECK(KUI_DISC_LAUNCH_EXEC_MAX_BYTES==12u*1024u*1024u);
    for(unsigned test=0;test<sizeof(sizes)/sizeof(sizes[0]);test++) {
        struct kui_disc_launch_plan plan,out;
        uint8_t *p=payload(module,sizes[test],KUI_DISC_LAUNCH_EXEC_MAX_BYTES,&plan);
        CHECK(kui_disc_launch_decode(p+0x1000u,&out) && !memcmp(&out,&plan,sizeof(out)));
        CHECK(kui_disc_launch_validate(p,plan.payload_bytes,&out) && !memcmp(&out,&plan,sizeof(out)));
        /* The complete executable beyond the former 4 MiB cap, its final
         * byte and the independent profile must each retain their seals. */
        const uint32_t changed[]={plan.exec_offset+0x400000u,
            plan.exec_offset+sizes[test]-1u,plan.vmu_profile_offset+4095u};
        for(unsigned i=0;i<sizeof(changed)/sizeof(changed[0]);i++) {
            p[changed[i]]^=1u;
            CHECK(!kui_disc_launch_validate(p,plan.payload_bytes,&out) && zero(&out,sizeof(out)));
            p[changed[i]]^=1u;
        }
        if(plan.vmu_profile_offset>plan.exec_offset+sizes[test]) {
            uint32_t padding=plan.exec_offset+sizes[test];p[padding]=1u;
            CHECK(!kui_disc_launch_validate(p,plan.payload_bytes,&out) && zero(&out,sizeof(out)));
            p[padding]=0u;
        }
        CHECK(!kui_disc_launch_validate(p,plan.payload_bytes-1u,&out) && zero(&out,sizeof(out)));
        /* A valid modern large packet cannot enlarge an older module's
         * declared capability, even when all packet and payload seals match. */
        put(p+0x100u+40u,0x400000u);
        CHECK(!kui_disc_launch_validate(p,plan.payload_bytes,&out) && zero(&out,sizeof(out)));
        free(p);
    }
}
static void size_limits(void) {
    const uint32_t module=KUI_DISC_LAUNCH_STAGE_OFFSET+KUI_DISC_LAUNCH_VMU_STAGE_MAX_BYTES;
    uint8_t *p=calloc(1u,module);CHECK(p);module_header(p,module,KUI_DISC_LAUNCH_EXEC_MAX_BYTES);
    struct kui_disc_launch_plan out;
    const uint32_t invalid[]={KUI_DISC_LAUNCH_EXEC_MAX_BYTES+1u,UINT32_MAX-31u,
        UINT32_MAX-3u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        memset(&out,0xa5,sizeof(out));
        CHECK(!kui_disc_launch_layout(p,module,invalid[i],&out) && zero(&out,sizeof(out)));
    }
    free(p);
    /* Old modules remain usable within the capability actually advertised.
     * Their zero config must refuse an executable one byte beyond that cap. */
    const uint32_t legacy_sizes[]={128u,0x400000u};
    for(unsigned i=0;i<sizeof(legacy_sizes)/sizeof(legacy_sizes[0]);i++) {
        struct kui_disc_launch_plan plan;
        p=payload(module,legacy_sizes[i],0x400000u,&plan);
        CHECK(kui_disc_launch_validate(p,plan.payload_bytes,&out) && !memcmp(&out,&plan,sizeof(out)));
        memset(p+0x1000u,0,512u);memset(&out,0xa5,sizeof(out));
        CHECK(!kui_disc_launch_layout(p,module,0x400001u,&out) && zero(&out,sizeof(out)));
        free(p);
    }
    /* Recomputed packet CRCs cannot authorize cap or rounding overflows. */
    struct kui_disc_launch_plan plan;
    p=payload(module,128u,KUI_DISC_LAUNCH_EXEC_MAX_BYTES,&plan);
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        put(p+0x1000u+32u,invalid[i]);packet_crc(p+0x1000u);
        CHECK(!kui_disc_launch_decode(p+0x1000u,&out) && zero(&out,sizeof(out)));
        CHECK(!kui_disc_launch_validate(p,plan.payload_bytes,&out) && zero(&out,sizeof(out)));
        CHECK(kui_disc_launch_encode(p+0x1000u,&plan));
    }
    free(p);
}
int main(void) {
    small_packet();large_packets();size_limits();
    puts("PASS physical-disc VMU packet: 4-12 MiB synthetic executables, maximum module, legacy capability, sealed profile, geometry and padding checks");
    printf("%u checks\n",checks);return 0;
}
