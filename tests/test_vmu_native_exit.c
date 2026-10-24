/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_exit.h"
#include "kui/vmu_native_program.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"exit guard failed at %u\n",__LINE__);exit(1);}} while(0)
static unsigned char image[2048];
static const uint32_t load=UINT32_C(0x8c010000);
static const unsigned pair=0x200u,exec=0x400u;
static const uint16_t pair_words[]={
    0xd310,0x6030,0x600c,0x8801,0x8b0e,0xd20f,0x6020,0x600c,
    0x8801,0x8b02,0xd10d,0x412b,0x0009,0xd00e,0xd10c,0x2012,
    0xd10d,0x6312,0x432b,0xe403,0xd10b,0x6212,0x422b,0xe401,
    0x4f22,0xd30a,0x430b,0x0009,0xd307,0xe402,0x6032,0x402b,0x4f26,0x0000};
static const uint16_t exec_words[]={
    0x2fe6,0x2fd6,0x4f22,0x7ff4,0x1f42,0xd41a,0xd31a,0x1f51,
    0x430b,0xed00,0xd319,0x65d3,0x430b,0x54f2,0x6e03,0x2ee8,
    0x8911,0xd317,0x65f3,0x430b,0x64e3,0xd316,0x65f2,0x56f1,
    0x430b,0x64e3,0x2008,0x8d01,0x64e3,0xedff,0xd312,0x430b,
    0x0009,0xa001,0x0009,0xedff,0xd310,0x430b,0x0009,0x2dd8,
    0x8902,0xd30f,0x430b,0x0009,0x52f1,0x420b,0x0009,0xe001,
    0x7f0c,0x4f26,0x6df6,0x000b,0x6ef6,0xd40b,0xd509,0xafc7,0xe601,0x0000};
static void halfword(unsigned char *p,uint16_t value) {p[0]=(unsigned char)value;p[1]=(unsigned char)(value>>8);}
static void word(unsigned char *p,uint32_t value) {for(unsigned i=0;i<4u;i++) p[i]=(unsigned char)(value>>(8u*i));}
static void fixture(void) {
    memset(image,0,sizeof(image));
    for(unsigned i=0;i<sizeof(pair_words)/sizeof(pair_words[0]);i++) halfword(image+pair+i*2u,pair_words[i]);
    for(unsigned i=0;i<sizeof(exec_words)/sizeof(exec_words[0]);i++) halfword(image+exec+i*2u,exec_words[i]);
    const uint32_t pool[]={UINT32_C(0x8c0080fc),UINT32_C(0x8c0080fe),UINT32_C(0x8c008300),
        UINT32_C(0x8c008000),UINT32_C(0x8c000064),UINT32_C(0x8c0000e0),load+0x760u};
    for(unsigned i=0;i<sizeof(pool)/sizeof(pool[0]);i++) word(image+pair+0x44u+4u*i,pool[i]);
    word(image+exec+0x74u,load+0x740u);
    for(unsigned i=0;i<6u;i++) word(image+exec+0x78u+i*4u,load+0x720u+2u*i);
    word(image+exec+0x90u,load+pair);word(image+exec+0x94u,UINT32_C(0x8c008300));
    word(image+exec+0x98u,load+0x744u);
}
static void long_load(unsigned instruction,unsigned reg,unsigned slot,uint32_t value) {
    unsigned pc=(instruction+4u)&~3u;
    CHECK(slot>=pc && !(slot&3u) && slot-pc<=1020u);
    halfword(image+instruction,(uint16_t)(0xd000u|(reg<<8)|((slot-pc)/4u)));word(image+slot,value);
}
static void short_load(unsigned instruction,unsigned reg,unsigned slot,uint16_t value) {
    CHECK(slot>=instruction+4u && !((slot-instruction)&1u) && slot-instruction-4u<=510u);
    halfword(image+instruction,(uint16_t)(0x9000u|(reg<<8)|((slot-instruction-4u)/2u)));
    halfword(image+slot,value);
}
/* Independent scalar construction of the platform operations. Immutable
 * literals and PC-relative encodings are generated for relocated synthetic
 * functions; owner executable bodies are never copied into these fixtures. */
static void pvr_fixture(bool compact) {
    fixture();memset(image+pair,0,0x6cu);
    static const struct {unsigned at;uint16_t op;} ops[]={
        {6,0x2232},{8,0x6010},{10,0x600c},{12,0x8801},{14,0x8b0e},
        {18,0x6030},{20,0x600c},{22,0x8801},{24,0x8b02},{28,0x422b},{30,0x0009},
        {36,0x2022},{40,0x6122},{42,0x412b},{44,0xe403},{48,0x6032},{50,0x402b},{52,0xe401},
        {54,0x4f22},{58,0x430b},{60,0x0009},{64,0xe402},{66,0x6032},{68,0x402b},{70,0x4f26}
    };
    for(unsigned i=0;i<sizeof(ops)/sizeof(ops[0]);i++) halfword(image+pair+ops[i].at,ops[i].op);
    const unsigned instructions[]={0,4,16,26,32,34,38,46,56,62};
    const unsigned registers[]={1,2,3,2,0,2,2,3,3,3};
    const unsigned slots[]={0x50,0x4c,0x54,0x58,0x60,0x5c,0x64,0x64,0x68,0x64};
    const uint32_t values[]={0x8c0080fc,0xa05f811c,0x8c0080fe,0x8c008300,0x8c000064,
        0x8c008000,0x8c0000e0,0x8c0000e0,load+0x760u,0x8c0000e0};
    for(unsigned i=0;i<10u;i++) long_load(pair+instructions[i],registers[i],pair+slots[i],values[i]);
    short_load(pair+2u,3u,pair+0x48u,0xffu);
    if(!compact) {
        memset(image+exec+88u,0,0xa8u-88u);
        static const struct {unsigned at;uint16_t op;} tail[]={
            {92,0x2322},{94,0x51f1},{96,0x410b},{98,0x0009},{100,0xe001},
            {102,0x7f0c},{104,0x4f26},{106,0x6df6},{108,0x000b},{110,0x6ef6},{116,0xafc4},{118,0xe601}
        };
        for(unsigned i=0;i<sizeof(tail)/sizeof(tail[0]);i++) halfword(image+exec+tail[i].at,tail[i].op);
        long_load(exec+10u,4u,exec+0x7cu,load+0x740u);
        static const unsigned calls[]={12u,20u,34u,42u,60u,72u,82u};
        for(unsigned i=0;i<7u;i++) long_load(exec+calls[i],3u,exec+0x80u+4u*i,
            i==6u?load+pair:load+0x720u+2u*i);
        long_load(exec+88u,3u,exec+0x9cu,0xa05f811c);short_load(exec+90u,2u,exec+0x78u,0xffu);
        long_load(exec+112u,4u,exec+0xa4u,load+0x744u);long_load(exec+114u,5u,exec+0xa0u,0x8c008300);
    } else {
        memset(image+exec,0,0xa8u);
        static const struct {unsigned at;uint16_t op;} compact_ops[]={
            {0,0x2fe6},{2,0x4f22},{4,0x7ff4},{6,0x1f42},{12,0x430b},{14,0x1f51},
            {18,0xe500},{20,0x430b},{22,0x54f2},{24,0x6e03},{26,0x2ee8},{28,0x890b},
            {32,0x65f3},{34,0x430b},{36,0x64e3},{40,0x65f2},{42,0x56f1},{44,0x430b},{46,0x64e3},
            {50,0x420b},{52,0x64e3},{56,0x430b},{58,0x0009},{60,0x52f1},{62,0x420b},{64,0x0009},
            {66,0xe001},{68,0x7f0c},{70,0x4f26},{72,0x000b},{74,0x6ef6},{80,0xafd6},{82,0xe601}
        };
        for(unsigned i=0;i<sizeof(compact_ops)/sizeof(compact_ops[0]);i++)
            halfword(image+exec+compact_ops[i].at,compact_ops[i].op);
        long_load(exec+10u,4u,exec+0x54u,load+0x740u);
        static const unsigned calls[]={8u,16u,30u,38u,48u,54u};
        for(unsigned i=0;i<6u;i++) long_load(exec+calls[i],i==4u?2u:3u,exec+0x58u+4u*i,load+0x720u+2u*i);
        long_load(exec+76u,4u,exec+0x74u,load+0x744u);long_load(exec+78u,5u,exec+0x70u,0x8c008300);
    }
}
static bool discover(const void *data,size_t bytes,uint32_t address,struct kui_vmu_native_exit *out) {
    memset(out,0xa5,sizeof(*out));bool result=kui_vmu_native_exit_discover(data,bytes,address,out);
    if(!result) {struct kui_vmu_native_exit zero={0};CHECK(!memcmp(out,&zero,sizeof(*out)));}
    return result;
}
static bool guarded(const void *data,size_t bytes,uint32_t address,
    const struct kui_vmu_native_patch_window *additional,size_t count,struct kui_vmu_native_exit *out) {
    memset(out,0xa5,sizeof(*out));
    bool result=kui_vmu_native_exit_discover_guarded(data,bytes,address,additional,count,out);
    if(!result) {struct kui_vmu_native_exit zero={0};CHECK(!memcmp(out,&zero,sizeof(*out)));}
    return result;
}
static void pvr_tests(void) {
    struct kui_vmu_native_exit out;
    const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    for(unsigned compact=0;compact<2u;compact++) {
        unsigned op_bytes=compact?0x54u:0x78u,exe_bytes=compact?0x78u:0xa8u;
        pvr_fixture(compact!=0u);CHECK(discover(image,sizeof(image),load,&out));
        CHECK(out.family==(compact?KUI_VMU_NATIVE_EXIT_PVR_COMPACT:KUI_VMU_NATIVE_EXIT_PVR_CLEAR));
        CHECK(out.entries[0]==load+pair && out.entries[1]==load+pair+54u && out.entries[2]==load+exec);
        CHECK(out.bios_vector_literal==load+pair+0x64u && out.cleanup==load+0x760u);
        CHECK(out.exec_thunk==load+exec+(compact?76u:112u));
        for(unsigned alias=0;alias<3u;alias++) {
            pvr_fixture(compact!=0u);uint32_t base=(load&0x1fffffffu)|aliases[alias];
            CHECK(discover(image,sizeof(image),base,&out));CHECK(out.entries[1]==base+pair+54u);
        }
        /* Exercise every executable halfword, including literal-load
         * encodings, conditionals and delay slots, without relying on an
         * owner image or on the recognizer's internal witness tables. */
        for(unsigned at=0;at<0x48u;at+=2u) {
            pvr_fixture(compact!=0u);image[pair+at]^=1u;CHECK(!discover(image,sizeof(image),load,&out));
        }
        for(unsigned at=0;at<op_bytes;at+=2u) {
            pvr_fixture(compact!=0u);image[exec+at]^=1u;CHECK(!discover(image,sizeof(image),load,&out));
        }
        const unsigned pool[]={0x48u,0x4cu,0x50u,0x54u,0x58u,0x5cu,0x60u,0x64u};
        for(unsigned i=0;i<sizeof(pool)/sizeof(pool[0]);i++) {
            pvr_fixture(compact!=0u);image[pair+pool[i]]^=1u;CHECK(!discover(image,sizeof(image),load,&out));
        }
        pvr_fixture(compact!=0u);word(image+pair+0x68u,load+0x761u);CHECK(!discover(image,sizeof(image),load,&out));
        pvr_fixture(compact!=0u);word(image+exec+(compact?0x74u:0xa4u),load+0x748u);
        CHECK(!discover(image,sizeof(image),load,&out));
        pvr_fixture(compact!=0u);word(image+exec+(compact?0x70u:0xa0u),0x8c008304u);
        CHECK(!discover(image,sizeof(image),load,&out));
        const unsigned entries[]={pair,pair+54u,exec};
        for(unsigned a=0;a<3u;a++) for(unsigned entry=0;entry<3u;entry++) {
            pvr_fixture(compact!=0u);word(image,aliases[a]|((load&0x1fffffffu)+entries[entry]+2u));
            CHECK(!discover(image,sizeof(image),load,&out));
        }
        for(unsigned a=0;a<3u;a++) for(unsigned target=0;target<2u;target++) {
            pvr_fixture(compact!=0u);long_load(0x80u,3u,0x100u,
                aliases[a]|(target?UINT32_C(0x0c008300):UINT32_C(0x0c0000e0)));
            CHECK(!discover(image,sizeof(image),load,&out));
        }
        pvr_fixture(compact!=0u);memcpy(image+0x600u,image+pair,0x6cu);
        CHECK(!discover(image,sizeof(image),load,&out));
        pvr_fixture(compact!=0u);memcpy(image+0x600u,image+exec,exe_bytes);
        CHECK(!discover(image,sizeof(image),load,&out));
        pvr_fixture(compact!=0u);
        const struct kui_vmu_native_patch_window extra[]={{load+0x680u,12u}};
        CHECK(guarded(image,sizeof(image),load,extra,1u,&out));
        word(image,load+0x682u);CHECK(!guarded(image,sizeof(image),load,extra,1u,&out));
    }
}
static void pair_only_tests(void) {
    struct kui_vmu_native_exit out;
    const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    for(unsigned a=0;a<3u;a++) {
        fixture();memset(image+exec,0,0x9cu);uint32_t base=(load&0x1fffffffu)|aliases[a];
        CHECK(discover(image,sizeof(image),base,&out));
        CHECK(out.family==KUI_VMU_NATIVE_EXIT_PAIR_ONLY && out.count==2u && !out.entries[2] && !out.exec_thunk);
        CHECK(out.entries[0]==base+pair && out.entries[1]==base+pair+48u &&
            out.bios_vector_literal==base+pair+0x58u && out.cleanup==load+0x760u);
    }
    for(unsigned a=0;a<3u;a++) for(unsigned target=0;target<2u;target++) {
        fixture();memset(image+exec,0,0x9cu);
        long_load(0x80u,3u,0x100u,aliases[a]|(target?UINT32_C(0x0c008300):UINT32_C(0x0c0000e0)));
        CHECK(!discover(image,sizeof(image),load,&out));
    }
    /* Losing/corrupting a loader thunk cannot turn the old executable
     * family into a permitted pair-only image. Its entry remains an explicit
     * conservative exclusion, even if the bootstrap literal stops matching. */
    fixture();word(image+exec+0x94u,0x8c008304u);CHECK(!discover(image,sizeof(image),load,&out));
    fixture();memset(image+exec+106u,0,0x9cu-106u);CHECK(!discover(image,sizeof(image),load,&out));
    fixture();memcpy(image+sizeof(image)-24u,image+exec,24u);memset(image+exec,0,0x9cu);
    CHECK(!discover(image,sizeof(image),load,&out));
    /* Active opcode and pool mutations must still fail with the loader
     * completely absent; the outer family choice grants no weaker pair. */
    for(unsigned at=0;at<sizeof(pair_words);at+=2u) {
        fixture();memset(image+exec,0,0x9cu);image[pair+at]^=1u;
        CHECK(!discover(image,sizeof(image),load,&out));
    }
    const struct kui_vmu_native_patch_window extra[]={{load+0x688u,12u}};
    fixture();memset(image+exec,0,0x9cu);CHECK(guarded(image,sizeof(image),load,extra,1u,&out));
    CHECK(out.count==2u && !out.entries[2] && !out.exec_thunk);
    word(image,load+0x68au);CHECK(!guarded(image,sizeof(image),load,extra,1u,&out));
    fixture();memset(image+exec,0,0x9cu);word(image,load+pair+50u);
    CHECK(!discover(image,sizeof(image),load,&out));
    fixture();memset(image+exec,0,0x9cu);memcpy(image+0x600u,image+pair,0x60u);
    CHECK(!discover(image,sizeof(image),load,&out));
}
static void pair_fill_tests(void) {
    struct kui_vmu_native_exit out;
    const uint16_t fills[]={0u,UINT16_C(0xffff)};
    for(unsigned no_loader=0u;no_loader<2u;no_loader++) for(unsigned f=0u;f<2u;f++) {
        fixture();if(no_loader) memset(image+exec,0,0x9cu);
        halfword(image+pair+66u,fills[f]);
        CHECK(discover(image,sizeof(image),load,&out));
        CHECK(out.family==(no_loader?KUI_VMU_NATIVE_EXIT_PAIR_ONLY:KUI_VMU_NATIVE_EXIT_FAMILY));
        for(unsigned bit=0u;bit<16u;bit++) {
            halfword(image+pair+66u,(uint16_t)(fills[f]^(1u<<bit)));
            CHECK(!discover(image,sizeof(image),load,&out));
        }
        halfword(image+pair+66u,fills[f]);
        /* The preceding JMP and its executed delay-slot restore remain
         * mandatory under either canonical linker-fill convention. */
        image[pair+64u]^=1u;CHECK(!discover(image,sizeof(image),load,&out));
    }
}
int main(int argc,char **argv) {
    struct kui_vmu_native_exit out;
    pvr_tests();pair_only_tests();pair_fill_tests();
    fixture();CHECK(discover(image,sizeof(image),load,&out));
    CHECK(out.version==1u && out.family==1u && out.bytes==sizeof(out) && out.count==3u);
    CHECK(out.load==load && out.image_bytes==sizeof(image));
    CHECK(out.entries[0]==load+pair && out.entries[1]==load+pair+48u && out.entries[2]==load+exec);
    CHECK(out.bios_vector_literal==load+pair+0x58u && out.exec_thunk==load+exec+0x6au && out.cleanup==load+0x760u);
    const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    for(unsigned i=0;i<3u;i++) {
        fixture();uint32_t base=(load&UINT32_C(0x1fffffff))|aliases[i];
        CHECK(discover(image,sizeof(image),base,&out));
        CHECK(out.entries[0]==base+pair && out.entries[1]==base+pair+48u && out.entries[2]==base+exec);
    }
    /* Every opcode halfword, including each encoded literal load and delay
     * slot, is constrained by this exact family. */
    for(unsigned at=0;at<sizeof(pair_words);at+=2u) {
        fixture();image[pair+at]^=1u;CHECK(!discover(image,sizeof(image),load,&out));
    }
    for(unsigned at=0;at<sizeof(exec_words);at+=2u) {
        fixture();image[exec+at]^=1u;CHECK(!discover(image,sizeof(image),load,&out));
    }
    for(unsigned at=0x44u;at<0x5cu;at+=4u) {
        fixture();image[pair+at]^=1u;CHECK(!discover(image,sizeof(image),load,&out));
    }
    fixture();word(image+pair+0x5cu,UINT32_C(0x8c0000e0));CHECK(!discover(image,sizeof(image),load,&out));
    fixture();word(image+pair+0x5cu,load+0x761u);CHECK(!discover(image,sizeof(image),load,&out));
    fixture();word(image+exec+0x74u,load+0x741u);CHECK(!discover(image,sizeof(image),load,&out));
    fixture();word(image+exec+0x98u,load+0x748u);CHECK(!discover(image,sizeof(image),load,&out));
    fixture();word(image+exec+0x90u,load+pair+4u);CHECK(!discover(image,sizeof(image),load,&out));
    fixture();word(image+exec+0x94u,UINT32_C(0x8c008304));CHECK(!discover(image,sizeof(image),load,&out));
    for(unsigned i=0;i<6u;i++) {
        fixture();word(image+exec+0x78u+i*4u,UINT32_C(0xff000000));
        CHECK(!discover(image,sizeof(image),load,&out));
    }
    /* Unknown encoded consumers of either firmware target must not be
     * silently left live, including aliases of the vector. */
    for(unsigned a=0;a<3u;a++) for(unsigned target=0;target<2u;target++) {
        fixture();halfword(image+0x80u,UINT16_C(0xd31f));
        word(image+0x100u,aliases[a]|(target?UINT32_C(0x0c008300):UINT32_C(0x0c0000e0)));
        CHECK(!discover(image,sizeof(image),load,&out));
    }
    fixture();word(image+0x100u,UINT32_C(0x0c0000e0));CHECK(discover(image,sizeof(image),load,&out));
    fixture();memcpy(image+0x600u,image+pair,0x60u);CHECK(!discover(image,sizeof(image),load,&out));
    fixture();memcpy(image+0x600u,image+exec,0x9cu);CHECK(!discover(image,sizeof(image),load,&out));
    fixture();memset(image+exec,0,0x9cu);CHECK(discover(image,sizeof(image),load,&out));
    CHECK(out.family==KUI_VMU_NATIVE_EXIT_PAIR_ONLY && out.count==2u && !out.entries[2] && !out.exec_thunk);
    fixture();memset(image+pair,0,0x60u);CHECK(!discover(image,sizeof(image),load,&out));
    const unsigned windows[]={pair,pair+48u,exec};
    for(unsigned i=0;i<3u;i++) for(unsigned a=0;a<3u;a++) {
        fixture();word(image,(load&UINT32_C(0x1fffffff))+windows[i]+2u+aliases[a]);
        CHECK(!discover(image,sizeof(image),load,&out));
    }
    for(unsigned i=0;i<3u;i++) {
        fixture();int delta=((int)windows[i]+2-0x100-4)/2;
        halfword(image+0x100u,(uint16_t)(UINT16_C(0xb000)|((uint16_t)delta&4095u)));
        CHECK(!discover(image,sizeof(image),load,&out));
    }
    fixture();CHECK(!discover(NULL,sizeof(image),load,&out));
    CHECK(!discover(image,0x9bu,load,&out));CHECK(!discover(image,0xc00001u,load,&out));
    CHECK(!discover(image,sizeof(image),load+2u,&out));CHECK(!discover(image,sizeof(image),UINT32_C(0x8cfffffc),&out));
    CHECK(!discover(image,sizeof(image),UINT32_C(0xcc010000),&out));
    CHECK(!kui_vmu_native_exit_discover(image,sizeof(image),load,NULL));
    fixture();unsigned char before[sizeof(image)];memcpy(before,image,sizeof(image));
    CHECK(!kui_vmu_native_exit_discover(image,sizeof(image),load,(struct kui_vmu_native_exit *)(void *)(image+0x100u)));
    CHECK(!memcmp(before,image,sizeof(image)));
    CHECK(!kui_vmu_native_exit_discover((const void *)(uintptr_t)UINT32_C(0x8c010000),sizeof(image),load,
        (struct kui_vmu_native_exit *)(uintptr_t)UINT32_C(0xac010100)));
    /* Combined discovery must be equivalent to the old two strict passes,
     * including cross-window overlaps, aliases, boundary/gap pointers and
     * caller-only interior targets. Both the image and output stay intact
     * when a caller aliases its metadata with the result. */
    for(unsigned a=0;a<3u;a++) for(unsigned target=0x680u;target<=0x6c0u;target++) {
        fixture();word(image,(load&UINT32_C(0x1fffffff))+target+aliases[a]);
        struct kui_vmu_native_patch_window extra[]={
            {load+0x688u,12u},{load+0x6b0u,12u}};
        bool baseline=discover(image,sizeof(image),load,&out);
        struct kui_vmu_native_patch_window combined[]={
            {out.entries[0],12u},{out.entries[1],12u},{out.entries[2],12u},extra[0],extra[1]};
        baseline=baseline && kui_vmu_native_patch_no_interiors(image,sizeof(image),load,combined,5u);
        CHECK(guarded(image,sizeof(image),load,extra,2u,&out)==baseline);
    }
    fixture();struct kui_vmu_native_patch_window extra[6];
    for(unsigned i=0;i<6u;i++) extra[i]=(struct kui_vmu_native_patch_window){load+0x600u+i*16u,12u};
    CHECK(guarded(image,sizeof(image),load,extra,5u,&out));
    CHECK(!guarded(image,sizeof(image),load,extra,6u,&out));
    CHECK(!guarded(image,sizeof(image),load,NULL,1u,&out));
    extra[0]=(struct kui_vmu_native_patch_window){load+pair+10u,12u};
    CHECK(!guarded(image,sizeof(image),load,extra,1u,&out));
    extra[0]=(struct kui_vmu_native_patch_window){load+0x600u,11u};
    CHECK(!guarded(image,sizeof(image),load,extra,1u,&out));
    fixture();memcpy(before,image,sizeof(image));
    CHECK(!kui_vmu_native_exit_discover_guarded(image,sizeof(image),load,extra,1u,
        (struct kui_vmu_native_exit *)(void *)(image+0x100u)));
    CHECK(!memcmp(before,image,sizeof(image)));
    memset(&out,0xa5,sizeof(out));struct kui_vmu_native_exit original=out;
    CHECK(!kui_vmu_native_exit_discover_guarded(image,sizeof(image),load,
        (const struct kui_vmu_native_patch_window *)(const void *)&out,1u,&out));
    CHECK(!memcmp(&out,&original,sizeof(out)));
    CHECK(!kui_vmu_native_exit_discover_guarded(image,sizeof(image),load,
        (const struct kui_vmu_native_patch_window *)(const void *)&out,6u,&out));
    CHECK(!memcmp(&out,&original,sizeof(out)));
    CHECK(!kui_vmu_native_exit_discover_guarded(image,sizeof(image),load,
        (const struct kui_vmu_native_patch_window *)(uintptr_t)UINT32_C(0x8c020000),1u,
        (struct kui_vmu_native_exit *)(uintptr_t)UINT32_C(0xac020004)));
    if(argc>=2) {
        FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);
        unsigned char *actual=malloc((size_t)n);CHECK(actual);CHECK(fread(actual,1,(size_t)n,f)==(size_t)n);fclose(f);
        CHECK(discover(actual,(size_t)n,load,&out));
        CHECK(out.entries[0]==UINT32_C(0x8c2b3a98) && out.entries[1]==UINT32_C(0x8c2b3ac8) &&
            out.entries[2]==UINT32_C(0x8c2aeaa0) && out.exec_thunk==UINT32_C(0x8c2aeb0a));
        CHECK(out.bios_vector_literal==UINT32_C(0x8c2b3af0) && out.cleanup==UINT32_C(0x0c2bb00c));
        const unsigned sites[]={0x2a3a98u,0x2a3ac8u,0x29eaa0u,0x29eb0eu};
        for(unsigned i=0;i<sizeof(sites)/sizeof(sites[0]);i++) {
            actual[sites[i]]^=1u;CHECK(!discover(actual,(size_t)n,load,&out));actual[sites[i]]^=1u;
        }
        CHECK(discover(actual,(size_t)n,load,&out));
        const struct kui_vmu_native_patch_window actual_extra[]={
            {UINT32_C(0x8c2d9b68),12u},{UINT32_C(0x8c2b15c8),12u},{UINT32_C(0x8c2b1778),20u}};
        struct kui_vmu_native_exit ordinary=out;
        CHECK(guarded(actual,(size_t)n,load,actual_extra,3u,&out));
        CHECK(!memcmp(&out,&ordinary,sizeof(out)));
        unsigned char saved[4];memcpy(saved,actual,sizeof(saved));
        for(unsigned i=0;i<3u;i++) {
            word(actual,actual_extra[i].begin+2u);
            CHECK(!guarded(actual,(size_t)n,load,actual_extra,3u,&out));
            memcpy(actual,saved,sizeof(saved));
        }
        CHECK(guarded(actual,(size_t)n,load,actual_extra,3u,&out));free(actual);
    }
    for(int arg=2;arg<argc;arg++) {
        FILE *f=fopen(argv[arg],"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);
        unsigned char *actual=malloc((size_t)n);CHECK(actual);CHECK(fread(actual,1,(size_t)n,f)==(size_t)n);fclose(f);
        size_t program_bytes;CHECK(kui_vmu_native_program_extent(actual,(size_t)n,load,&program_bytes));
        CHECK(program_bytes<=(size_t)n);n=(long)program_bytes;
        CHECK(discover(actual,(size_t)n,load,&out));
        CHECK(out.family==KUI_VMU_NATIVE_EXIT_PVR_CLEAR || out.family==KUI_VMU_NATIVE_EXIT_PVR_COMPACT ||
            out.family==KUI_VMU_NATIVE_EXIT_PAIR_ONLY);
        struct kui_vmu_native_exit original=out;
        for(unsigned entry=0;entry<original.count;entry++) {
            size_t at=out.entries[entry]-load;unsigned char saved=actual[at];actual[at]^=1u;
            CHECK(!discover(actual,(size_t)n,load,&out));actual[at]=saved;
            CHECK(discover(actual,(size_t)n,load,&out));CHECK(!memcmp(&out,&original,sizeof(out)));
        }
        /* Mutation of an otherwise-unrelated image word into a new consumer
         * or an interior entry alias must still defeat the complete scan. */
        unsigned char saved[4];memcpy(saved,actual,4u);word(actual,out.entries[1]+2u);
        CHECK(!discover(actual,(size_t)n,load,&out));memcpy(actual,saved,4u);
        CHECK(discover(actual,(size_t)n,load,&out));free(actual);
    }
    printf("Native exit/exec family: %u checks passed\n",checks);return 0;
}
