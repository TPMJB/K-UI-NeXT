/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthetic instructions are assembled from scalar SDK operations. Optional
 * original inputs are read privately; no retail payload is copied here. */
#include "../src/core/vmu_native_exit.c"
#include "kui/vmu_native_discovery.h"
#ifdef KUI_VMU_NATIVE_FIRMWARE_EXIT_INCLUDED
#define FIRMWARE_EXIT_INTEGRATED 1
#else
#include "../src/core/vmu_native_firmware_exit.inc"
#endif
#define main existing_exit_main
#include "test_vmu_native_exit.c"
#undef main

static const unsigned fw=0x602u;
static void firmware(unsigned at) {
    static const uint16_t instructions[]={0xd30a,0x6430,0x654e,0x6053,
        0x8820,0x8901,0x2558,0x8b03,0xd208,0x6322,0x432b,0xe401,
        0xd006,0xd307,0xd206,0x2322,0x6102,0x412b,0xe403};
    static const uint32_t constants[]={0x8c0080f0u,0x8c0080fcu,0x8c0080fdu,
        0x8c0000e0u,0x8c008000u,0x8c000064u};
    halfword(image+at-4u,0x000bu);halfword(image+at-2u,0x0009u);
    for(unsigned i=0;i<19u;i++) halfword(image+at+2u*i,instructions[i]);
    for(unsigned i=0;i<6u;i++) word(image+at+38u+4u*i,constants[i]);
}
static void firmware_fixture(unsigned kind) {
    if(kind==2u) early_fixture(0u);
    else {if(kind) pvr_fixture(false);else fixture();memset(image+exec,0,0x200u);}
    firmware(fw);
    long_load(0x510u,3u,fw+38u,0x8c0080f0u);
    long_load(0x514u,3u,fw+42u,0x8c0080fcu);
    long_load(0x518u,3u,fw+46u,0x8c0080fdu);
}
static int result_at(const uint8_t *p,size_t n,uint32_t address,unsigned kind,size_t *out) {
    struct exit_shape shape=kind?(struct exit_shape){54u,0x64u,26u,{38u,46u,62u}}:
        (struct exit_shape){48u,0x58u,20u,{32u,40u,56u}};
    return firmware_exit_extra(p,n,address,kind==2u?early:pair,kind==2u,kind==1u,&shape,out);
}
static int result(unsigned kind) {
    size_t out=SIZE_MAX-1u;int status=result_at(image,sizeof(image),load,kind,&out);
    CHECK(status==1?out==fw:out==SIZE_MAX-1u);
#ifdef FIRMWARE_EXIT_INTEGRATED
    struct kui_vmu_native_exit e;
    if(status==1) {
        CHECK(discover(image,sizeof(image),load,&e));
        CHECK(e.family==(kind==2u?10u:9u) && e.count==3u && e.exec_thunk==0u);
        CHECK(e.entries[2]==load+fw && kui_vmu_native_exit_shape_valid(&e));
        CHECK(kui_vmu_native_exit_window(&e,0u)==(kind==2u?8u:12u));
        CHECK(kui_vmu_native_exit_window(&e,1u)==12u && kui_vmu_native_exit_window(&e,2u)==12u);
    } else if(status<0) CHECK(!discover(image,sizeof(image),load,&e));
#endif
    return status;
}
static void synthetic(void) {
    const uint32_t aliases[]={0x0c010000u,0x8c010000u,0xac010000u};
    for(unsigned kind=0;kind<3u;kind++) {
        for(unsigned alias=0;alias<3u;alias++) {
            firmware_fixture(kind);size_t out=SIZE_MAX;
            CHECK(result_at(image,sizeof(image),aliases[alias],kind,&out)==1 && out==fw);
        }
        for(unsigned byte=0;byte<62u;byte++) for(unsigned bit=0;bit<8u;bit++) {
            firmware_fixture(kind);image[fw+byte]^=(unsigned char)(1u<<bit);CHECK(result(kind)==-1);
        }
        for(unsigned byte=0;byte<4u;byte++) for(unsigned bit=0;bit<8u;bit++) {
            firmware_fixture(kind);image[fw-4u+byte]^=(unsigned char)(1u<<bit);CHECK(result(kind)==-1);
        }
        for(unsigned i=0;i<2u;i++) {
            firmware_fixture(kind);memset(image+(i?0x518u:0x510u),0,2u);CHECK(result(kind)==-1);
        }
        firmware_fixture(kind);firmware(0x702u);CHECK(result(kind)==-1);
        firmware_fixture(kind);long_load(0x40u,0u,0x100u,0x8c0000e0u);CHECK(result(kind)==-1);
        firmware_fixture(kind);long_load(0x40u,0u,0x100u,0xac008300u);CHECK(result(kind)==-1);
        for(unsigned alias=0;alias<3u;alias++) for(unsigned offset=0;offset<62u;offset++) {
            firmware_fixture(kind);word(image+0x100u,aliases[alias]+fw+offset);
            CHECK(result(kind)==(offset==0u?1:-1));
        }
        static const uint16_t branches[]={0xa000u,0xb000u,0x8900u,0x8b00u,0x8d00u,0x8f00u};
        for(unsigned b=0;b<6u;b++) for(unsigned offset=0;offset<62u;offset+=2u) {
            unsigned source=0x5a0u;int displacement=((int)fw+(int)offset-(int)source-4)/2;
            firmware_fixture(kind);halfword(image+source,(uint16_t)(branches[b]|((unsigned)displacement&(b<2u?4095u:255u))));
            CHECK(result(kind)==(offset==0u?1:-1));
        }
        /* Full/partial reads and address-of are not control-flow evidence. */
        for(unsigned offset=0;offset<62u;offset+=2u) {
            firmware_fixture(kind);unsigned source=0x580u;
            halfword(image+source,(uint16_t)(0x9000u|((fw+offset-source-4u)/2u)));CHECK(result(kind)==-1);
        }
        for(unsigned offset=38u;offset<62u;offset+=4u) {
            firmware_fixture(kind);unsigned source=0x580u,pc=(source+4u)&~3u;
            halfword(image+source,(uint16_t)(0xc700u|((fw+offset-pc)/4u)));CHECK(result(kind)==-1);
            firmware_fixture(kind);halfword(image+source,(uint16_t)(0xd000u|((fw+offset-pc)/4u)));
            CHECK(result(kind)==(offset<50u?1:-1));
        }
        firmware_fixture(kind);memset(image+fw-4u,0,66u);memset(image+0x510u,0,12u);CHECK(result(kind)==0);
        firmware_fixture(kind);size_t out=123u;
        for(size_t n=fw;n<fw+62u;n++) {
            CHECK(result_at(image,n,load,kind,&out)!=1);CHECK(out==123u);
        }
    }
    firmware_fixture(0u);size_t out=123u;
    CHECK(firmware_exit_extra(NULL,sizeof(image),load,pair,false,false,NULL,&out)==-1);
    CHECK(firmware_exit_extra(image,sizeof(image),load,pair,false,false,NULL,&out)==-1);
    CHECK(firmware_exit_extra(image,sizeof(image),load,pair,true,true,NULL,&out)==-1);
    CHECK(out==123u);
    /* The peripheral-pointer source role closes its entire source pool,
     * including its start, rather than trusting a value or a code offset. */
    for(unsigned alias=0;alias<3u;alias++) for(unsigned offset=0;offset<36u;offset++) {
        firmware_fixture(1u);word(image+0x100u,aliases[alias]+pair+0x48u+offset);CHECK(result(1u)==-1);
    }
    static const uint16_t branches[]={0xa000u,0xb000u,0x8900u,0x8b00u,0x8d00u,0x8f00u};
    for(unsigned b=0;b<6u;b++) for(unsigned offset=0;offset<36u;offset+=2u) {
        firmware_fixture(1u);unsigned source=0x180u,target=pair+0x48u+offset;
        halfword(image+source,(uint16_t)(branches[b]|((target-source-4u)/2u)));CHECK(result(1u)==-1);
    }
    for(unsigned offset=0;offset<36u;offset+=2u) {
        firmware_fixture(1u);halfword(image+0x180u,(uint16_t)(0x9000u|((pair+0x48u+offset-0x184u)/2u)));
        CHECK(result(1u)==-1);
    }
    for(unsigned offset=0;offset<36u;offset+=4u) for(unsigned op=0;op<2u;op++) {
        firmware_fixture(1u);halfword(image+0x180u,(uint16_t)((op?0xd000u:0xc700u)|((pair+0x48u+offset-0x184u)/4u)));
        CHECK(result(1u)==-1);
    }
    for(unsigned byte=0;byte<4u;byte++) for(unsigned bit=0;bit<8u;bit++) {
        firmware_fixture(1u);image[pair+0x4cu+byte]^=(unsigned char)(1u<<bit);CHECK(result(1u)==-1);
    }
    for(unsigned field=0;field<6u;field++) for(unsigned bit=0;bit<32u;bit++) {
        firmware_fixture(1u);struct exit_shape shape={54u,0x64u,26u,{38u,46u,62u}};
        uint32_t fields[6];memcpy(fields,&shape,sizeof(fields));fields[field]^=UINT32_C(1)<<bit;
        memcpy(&shape,fields,sizeof(shape));out=123u;
        CHECK(firmware_exit_extra(image,sizeof(image),load,pair,false,true,&shape,&out)==-1);CHECK(out==123u);
    }
#ifdef FIRMWARE_EXIT_INTEGRATED
    struct kui_vmu_native_exit e;
    for(unsigned kind=0;kind<3u;kind++) {
        uint8_t restart[0x4cu];crt_fixture();memcpy(restart,image+0x600u,sizeof(restart));
        firmware_fixture(kind);memcpy(image+0x680u,restart,sizeof(restart));
        CHECK(!discover(image,sizeof(image),load,&e));
        /* Even a damaged bootstrap literal cannot hide a recognizable CRT
         * restart path whose stack/FPSCR/BSS operations remain present. */
        word(image+0x680u+0x34u,UINT32_C(0x8c008304));
        CHECK(!discover(image,sizeof(image),load,&e));
    }
    fixture();firmware(fw);long_load(0x510u,3u,fw+38u,0x8c0080f0u);
    long_load(0x518u,3u,fw+46u,0x8c0080fdu);CHECK(!discover(image,sizeof(image),load,&e));
    word(image+exec+0x94u,0u);CHECK(!discover(image,sizeof(image),load,&e));
#endif
}
static void original(const char *path,int expected) {
    FILE *f=fopen(path,"rb");CHECK(f!=NULL);CHECK(!fseek(f,0,SEEK_END));long length=ftell(f);
    CHECK(length>0 && length<=0xc00000);CHECK(!fseek(f,0,SEEK_SET));
    unsigned char *p=malloc((size_t)length);CHECK(p!=NULL);CHECK(fread(p,1,(size_t)length,f)==(size_t)length);CHECK(!fclose(f));
    size_t found_pair=SIZE_MAX;bool is_early=false,is_pvr=false;struct exit_shape shape={0};
    for(size_t at=0;at+108u<=(size_t)length;at+=4u) {
        uint32_t cleanup;
        if(exit_early_pair(p,(size_t)length,load,at,&cleanup)) {CHECK(found_pair==SIZE_MAX);found_pair=at;is_early=true;}
        else if(exit_pair(p,(size_t)length,load,at,&cleanup)) {
            CHECK(found_pair==SIZE_MAX);found_pair=at;shape=(struct exit_shape){48u,0x58u,20u,{32u,40u,56u}};
        } else if(exit_pvr_pair(p,(size_t)length,load,at,&cleanup)) {
            CHECK(found_pair==SIZE_MAX);found_pair=at;is_pvr=true;shape=(struct exit_shape){54u,0x64u,26u,{38u,46u,62u}};
        }
    }
    CHECK(found_pair!=SIZE_MAX);size_t out=123u;
    int actual=firmware_exit_extra(p,(size_t)length,load,found_pair,is_early,is_pvr,&shape,&out);
    if(actual!=expected) for(size_t at=0;at+8u<=(size_t)length;at+=2u)
        if(firmware_exit_prefix(p,(size_t)length,at))
            fprintf(stderr,"firmware mismatch: result=%d expected=%d pair=%zx helper=%zx body=%d closed=%d\n",
                actual,expected,found_pair,at,firmware_exit_body(p,(size_t)length,at),
                firmware_exit_closed(p,(size_t)length,load,at,found_pair,is_pvr));
    CHECK(actual==expected);if(expected<1) CHECK(out==123u);
#ifdef FIRMWARE_EXIT_INTEGRATED
    struct kui_vmu_native_exit e,g;
    CHECK(discover(p,(size_t)length,load,&e)==(expected==1));
    if(expected==1) {
        struct kui_vmu_native_discovery driver;struct kui_vmu_native_heap heap;
        CHECK(kui_vmu_native_discover(p,(size_t)length,load,&driver)==KUI_VMU_NATIVE_RESOLVED);
        CHECK(kui_vmu_native_heap_discover(p,(size_t)length,load,&heap)==KUI_VMU_NATIVE_HEAP_FOUND);
        struct kui_vmu_native_patch_window extra[3]={{driver.submit,12u},{heap.initializer,12u},{heap.release,20u}};
        CHECK(guarded(p,(size_t)length,load,extra,3u,&g));
        CHECK(!memcmp(&e,&g,sizeof(e)) && e.family==(is_early?10u:9u) && e.count==3u);
        CHECK(e.exec_thunk==0u && e.entries[2]==load+out && !e.entries[3]);
        CHECK(kui_vmu_native_exit_shape_valid(&e));
        extra[2]=(struct kui_vmu_native_patch_window){e.entries[2]+2u,12u};
        CHECK(!guarded(p,(size_t)length,load,extra,3u,&g));
    }
#endif
    if(expected==1) {
        CHECK(firmware_exit_body(p,(size_t)length,out));
        for(unsigned byte=0;byte<62u;byte++) for(unsigned bit=0;bit<8u;bit++) {
            p[out+byte]^=(unsigned char)(1u<<bit);size_t unchanged=123u;
            CHECK(firmware_exit_extra(p,(size_t)length,load,found_pair,is_early,is_pvr,&shape,&unchanged)==-1);
            CHECK(unchanged==123u);p[out+byte]^=(unsigned char)(1u<<bit);
        }
    }
    printf("original firmware result %d: %s\n",actual,path);free(p);
}
int main(int argc,char **argv) {
    synthetic();for(int i=1;i+1<argc;i+=2) original(argv[i],atoi(argv[i+1]));
    printf("firmware exit: %u checks passed\n",checks);return 0;
}
