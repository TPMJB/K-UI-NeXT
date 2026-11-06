/* SPDX-License-Identifier: GPL-3.0-only */
/* Independently assembled early exits + compact loader; no owner bytes are
 * embedded. The optional original input supplies a separate regression. */
#include "kui/vmu_native_exit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t load=UINT32_C(0x8c010000);
static unsigned char image[4096];
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"early compact check failed at %u\n",__LINE__);exit(1);}}while(0)
static void half(unsigned at,uint16_t value) {image[at]=(unsigned char)value;image[at+1]=(unsigned char)(value>>8);}
static void word(unsigned at,uint32_t value) {for(unsigned i=0;i<4;i++)image[at+i]=(unsigned char)(value>>(8u*i));}
static void literal(unsigned at,unsigned reg,unsigned slot,uint32_t value) {
    unsigned pc=(at+4u)&~3u;CHECK(slot>=pc && !(slot&3u) && slot-pc<=1020u);
    half(at,(uint16_t)(0xd000u|(reg<<8)|((slot-pc)/4u)));word(slot,value);
}
static void loader(unsigned at) {
    static const struct {unsigned at;uint16_t op;} code[]={
        {0,0x2fe6},{2,0x4f22},{4,0x7ff4},{6,0x1f42},{12,0x430b},{14,0x1f51},
        {18,0xe500},{20,0x430b},{22,0x54f2},{24,0x6e03},{26,0x2ee8},{28,0x890b},
        {32,0x65f3},{34,0x430b},{36,0x64e3},{40,0x65f2},{42,0x56f1},{44,0x430b},{46,0x64e3},
        {50,0x420b},{52,0x64e3},{56,0x430b},{58,0x0009},{60,0x52f1},{62,0x420b},{64,0x0009},
        {66,0xe001},{68,0x7f0c},{70,0x4f26},{72,0x000b},{74,0x6ef6},
        {80,0xafd6},{82,0xe601}
    };
    for(unsigned i=0;i<sizeof(code)/sizeof(code[0]);i++)half(at+code[i].at,code[i].op);
    literal(at+10u,4u,at+0x54u,load+0xb00u);
    static const unsigned calls[]={8u,16u,30u,38u,48u,54u};
    for(unsigned i=0;i<6u;i++)literal(at+calls[i],i==4u?2u:3u,at+0x58u+4u*i,load+0xc00u+0x20u*i);
    literal(at+76u,4u,at+0x74u,load+0xb04u);
    literal(at+78u,5u,at+0x70u,UINT32_C(0x8c008300));
}
static void fixture(void) {
    memset(image,0,sizeof(image));
    static const uint16_t pair[]={0xd206,0x6322,0x432b,0xe401,0x4f22,0xd305,
        0x430b,0x0009,0xd302,0xe402,0x6032,0x402b,0x4f26,0x0000};
    for(unsigned i=0;i<sizeof(pair)/sizeof(pair[0]);i++)half(0x200u+2u*i,pair[i]);
    word(0x21cu,UINT32_C(0x8c0000e0));word(0x220u,load+0xd00u);
    loader(0x600u);word(0x100u,load+0x64cu);
}
static bool discover(struct kui_vmu_native_exit *out) {
    memset(out,0xa5,sizeof(*out));bool ok=kui_vmu_native_exit_discover(image,sizeof(image),load,out);
    if(!ok){struct kui_vmu_native_exit zero={0};CHECK(!memcmp(out,&zero,sizeof(*out)));}
    return ok;
}
static void positive(void) {
    struct kui_vmu_native_exit e;CHECK(discover(&e));
    CHECK(e.family==KUI_VMU_NATIVE_EXIT_EARLY_COMPACT && e.count==3u && kui_vmu_native_exit_shape_valid(&e));
    CHECK(e.entries[0]==load+0x200u && e.entries[1]==load+0x208u && e.entries[2]==load+0x600u && !e.entries[3]);
    CHECK(e.exec_thunk==load+0x64cu && e.bios_vector_literal==load+0x21cu && e.cleanup==load+0xd00u);
    CHECK(kui_vmu_native_exit_window(&e,0)==8 && kui_vmu_native_exit_window(&e,1)==12 && kui_vmu_native_exit_window(&e,2)==12);
    struct kui_vmu_native_exit bad=e;bad.count=2;CHECK(!kui_vmu_native_exit_shape_valid(&bad));
    bad=e;bad.entries[1]+=2;CHECK(!kui_vmu_native_exit_shape_valid(&bad));
    bad=e;bad.entries[2]=0;CHECK(!kui_vmu_native_exit_shape_valid(&bad));
    bad=e;bad.exec_thunk+=2;CHECK(!kui_vmu_native_exit_shape_valid(&bad));
    bad=e;bad.entries[3]=load;CHECK(!kui_vmu_native_exit_shape_valid(&bad));
    bad=e;bad.family=KUI_VMU_NATIVE_EXIT_EARLY_PAIR;CHECK(!kui_vmu_native_exit_shape_valid(&bad));
}
static void opcode_and_role_mutants(void) {
    struct kui_vmu_native_exit e;
    for(unsigned at=0;at<84u;at++)for(unsigned bit=0;bit<8;bit++) {
        fixture();image[0x600u+at]^=(unsigned char)(1u<<bit);CHECK(!discover(&e));
    }
    /* Context/thunk binding, bootstrap and both early vector transports. */
    static const unsigned words[]={0x654u,0x670u,0x674u,0x21cu};
    for(unsigned i=0;i<sizeof(words)/sizeof(words[0]);i++)for(unsigned bit=0;bit<32;bit++) {
        fixture();image[words[i]+bit/8u]^=(unsigned char)(1u<<(bit%8u));CHECK(!discover(&e));
    }
    for(unsigned at=0;at<28u;at++)for(unsigned bit=0;bit<8;bit++) {
        fixture();image[0x200u+at]^=(unsigned char)(1u<<bit);CHECK(!discover(&e));
    }
    for(unsigned i=0;i<6u;i++) {
        fixture();word(0x658u+4u*i,load+0x1001u);CHECK(!discover(&e));
        fixture();word(0x658u+4u*i,UINT32_C(0xacffffff));CHECK(!discover(&e));
    }
    fixture();loader(0x900u);CHECK(!discover(&e));
    fixture();memcpy(image+0x900u,image+0x600u,24u);CHECK(!discover(&e));
    /* Hiding the boot consumer cannot downgrade a malformed loader. */
    fixture();word(0x670u,0);CHECK(!discover(&e));
}
static void entries_and_pool(void) {
    struct kui_vmu_native_exit e;
    static const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    for(unsigned alias=0;alias<3;alias++)for(unsigned offset=0;offset<120u;offset++) {
        fixture();word(0x100u,aliases[alias]|((load+0x600u+offset)&UINT32_C(0x1fffffff)));
        CHECK(discover(&e)==(offset==0u || offset==76u));
    }
    static const uint16_t branches[]={0xa000u,0xb000u,0x8900u,0x8b00u,0x8d00u,0x8f00u};
    for(unsigned kind=0;kind<6;kind++)for(unsigned offset=0;offset<120u;offset+=2u) {
        fixture();int32_t displacement=(0x600+(int32_t)offset-0x584)/2;
        unsigned mask=kind<2?4095u:255u;
        half(0x580u,(uint16_t)(branches[kind]|((uint32_t)displacement&mask)));
        CHECK(discover(&e)==(offset==0u || offset==76u));
    }
    /* No outside PC-relative operation may borrow instructions or pool data. */
    for(unsigned offset=0;offset<120u;offset+=2u) {
        fixture();half(0x5f8u,(uint16_t)(0x9300u|((0x600u+offset-0x5fcu)/2u)));CHECK(!discover(&e));
        if(!(offset&3u)) {
            fixture();half(0x5f8u,(uint16_t)(0xd300u|((0x600u+offset-0x5fcu)/4u)));CHECK(!discover(&e));
            fixture();half(0x5f8u,(uint16_t)(0xc700u|((0x600u+offset-0x5fcu)/4u)));CHECK(!discover(&e));
        }
    }
    fixture();literal(0x300u,2u,0x340u,UINT32_C(0xac0000e0));CHECK(!discover(&e));
    fixture();literal(0x300u,2u,0x340u,UINT32_C(0x0c008300));CHECK(!discover(&e));
}
int main(void) {
    fixture();positive();opcode_and_role_mutants();entries_and_pool();fixture();positive();
    printf("Early pair/compact loader: %u checks passed\n",checks);return 0;
}
