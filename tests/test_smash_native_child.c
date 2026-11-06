/* SPDX-License-Identifier: GPL-3.0-only */
/* Original images are optional read-only inputs, never copied into this tree. */
#include "kui/smash_native_child.h"
#include "kui/retail_image.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"Smash child check failed at line %u\n",__LINE__);exit(1);}} while(0)
static void digest(const void *data,size_t bytes,uint8_t out[32]) {
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,data,bytes);
    kui_sha256_digest(&hash,out);
}
static uint8_t *read_owner(const char *directory,const char *name,size_t expected) {
    char path[1024];CHECK(snprintf(path,sizeof(path),"%s/%s",directory,name)>0);
    FILE *file=fopen(path,"rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));
    CHECK(ftell(file)==(long)expected);CHECK(!fseek(file,0,SEEK_SET));
    uint8_t *data=malloc(expected);CHECK(data);CHECK(fread(data,1u,expected,file)==expected);
    CHECK(!fclose(file));return data;
}
static struct kui_game_metadata candidate(void) {
    struct kui_game_metadata m={.ip_valid=true,.boot_valid=true,.native_gd=true,
        .boot_bytes=KUI_SMASH_NATIVE_MENU_BYTES};
    strcpy(m.product,"MK-51146");strcpy(m.version,"V1.002");strcpy(m.region,"U");
    strcpy(m.bootfile,"1ST_READ.BIN");return m;
}
static void candidates(void) {
    struct kui_game_metadata m=candidate();CHECK(kui_smash_native_child_candidate(&m));
    CHECK(!kui_smash_native_child_candidate(NULL));
    for(unsigned field=0;field<9;field++) {
        m=candidate();
        switch(field) {
        case 0:m.ip_valid=false;break;case 1:m.boot_valid=false;break;
        case 2:m.native_gd=false;break;case 3:m.windows_ce=true;break;
        case 4:m.product[0]^=1;break;case 5:m.version[0]^=1;break;
        case 6:m.region[0]^=1;break;case 7:m.bootfile[0]^=1;break;
        default:--m.boot_bytes;break;
        }
        CHECK(!kui_smash_native_child_candidate(&m));
    }
}
static void seeds(void) {
    CHECK(!kui_smash_native_child_seed_seal(NULL));CHECK(!kui_smash_native_child_seed_valid(NULL));
    for(unsigned child=1;child<KUI_SMASH_NATIVE_CHILD_COUNT;child++) {
        struct kui_smash_native_child_seed s;memset(&s,0xa7,sizeof(s));s.child=child;
        CHECK(kui_smash_native_child_seed_seal(&s));CHECK(kui_smash_native_child_seed_valid(&s));
        CHECK(s.child==child && !s.reserved[0] && !s.reserved[1]);
        for(size_t i=0;i<sizeof(s);i++)for(unsigned bit=0;bit<8;bit++) {
            struct kui_smash_native_child_seed changed=s;
            ((uint8_t *)&changed)[i]^=(uint8_t)(1u<<bit);
            CHECK(!kui_smash_native_child_seed_valid(&changed));
        }
    }
    for(unsigned child=0;child<=KUI_SMASH_NATIVE_CHILD_COUNT+1u;child++)
        if(!child || child>=KUI_SMASH_NATIVE_CHILD_COUNT) {
            struct kui_smash_native_child_seed s;memset(&s,0xa7,sizeof(s));s.child=child;
            struct kui_smash_native_child_seed before=s;
            CHECK(!kui_smash_native_child_seed_seal(&s));CHECK(!memcmp(&s,&before,sizeof(s)));
        }
    CHECK(!kui_smash_native_child_name(0));CHECK(!kui_smash_native_child_folder(99));
    CHECK(!kui_smash_native_child_rom(KUI_SMASH_NATIVE_CHILD_COUNT));
}
static void frames(void) {
    uint32_t original[21]={0};original[2]=UINT32_C(0x8c000000);
    original[3]=UINT32_C(0x8c00f400);original[4]=UINT32_C(0x700000f0);
    original[5]=UINT32_C(0xac00e0b2);
    uint32_t expected[21];memcpy(expected,original,sizeof(expected));
    expected[0]=0x28u;expected[5]=0;expected[18]=expected[19]=0x800u;
    expected[20]=UINT32_C(0x8c0100a0);
    CHECK(!kui_smash_native_child_frame(NULL,UINT32_C(0x8c00f3ac)));
    const uint32_t aliases[]={UINT32_C(0x8c00f3ac),UINT32_C(0xac00f3ac)};
    for(unsigned alias=0;alias<2;alias++) {
        uint32_t frame[21];memcpy(frame,original,sizeof(frame));
        CHECK(kui_smash_native_child_frame(frame,aliases[alias]));
        CHECK(!memcmp(frame,expected,sizeof(frame)));
        /* No second normalization can reinterpret a monitor-patched or
         * already normalized frame as a fresh owner bootstrap frame. */
        CHECK(!kui_smash_native_child_frame(frame,aliases[alias]));
        CHECK(!memcmp(frame,expected,sizeof(frame)));
        for(unsigned word=0;word<21;word++)for(unsigned bit=0;bit<32;bit++) {
            uint32_t before[21];memcpy(frame,original,sizeof(frame));
            frame[word]^=UINT32_C(1)<<bit;memcpy(before,frame,sizeof(before));
            CHECK(!kui_smash_native_child_frame(frame,aliases[alias]));
            CHECK(!memcmp(frame,before,sizeof(frame)));
        }
    }
    for(unsigned bit=0;bit<32;bit++)if(bit!=29u) {
        uint32_t frame[21];memcpy(frame,original,sizeof(frame));
        CHECK(!kui_smash_native_child_frame(frame,UINT32_C(0x8c00f3ac)^(UINT32_C(1)<<bit)));
        CHECK(!memcmp(frame,original,sizeof(frame)));
    }
}
static void owners(const char *directory,uint8_t **engine_out) {
    uint8_t hash[32];
    uint8_t *menu=read_owner(directory,"1ST_READ.BIN",KUI_SMASH_NATIVE_MENU_BYTES);
    uint8_t *ip=read_owner(directory,"IP.BIN",32768u);
    uint8_t *engine=read_owner(directory,"MDE_US.BIN",KUI_SMASH_NATIVE_ENGINE_BYTES);
    uint32_t crc=kui_retail_crc32(0,ip,32768u);CHECK(crc==KUI_SMASH_NATIVE_IP_CRC32);
    digest(menu,KUI_SMASH_NATIVE_MENU_BYTES,hash);
    CHECK(kui_retail_crc32(0,menu,KUI_SMASH_NATIVE_MENU_BYTES)==KUI_SMASH_NATIVE_MENU_CRC32);
    CHECK(kui_smash_native_child_menu_owner(crc,KUI_SMASH_NATIVE_MENU_BYTES,hash));
    CHECK(!kui_smash_native_child_menu_owner(crc+1,KUI_SMASH_NATIVE_MENU_BYTES,hash));
    CHECK(!kui_smash_native_child_menu_owner(crc,KUI_SMASH_NATIVE_MENU_BYTES-1,hash));
    CHECK(!kui_smash_native_child_menu_owner(crc,KUI_SMASH_NATIVE_MENU_BYTES,NULL));
    for(unsigned i=0;i<32;i++) {hash[i]^=1;CHECK(!kui_smash_native_child_menu_owner(crc,KUI_SMASH_NATIVE_MENU_BYTES,hash));hash[i]^=1;}
    digest(engine,KUI_SMASH_NATIVE_ENGINE_BYTES,hash);
    CHECK(kui_retail_crc32(0,engine,KUI_SMASH_NATIVE_ENGINE_BYTES)==KUI_SMASH_NATIVE_ENGINE_CRC32);
    CHECK(kui_smash_native_child_engine_owner(KUI_SMASH_NATIVE_ENGINE_BYTES,hash));
    CHECK(!kui_smash_native_child_engine_owner(KUI_SMASH_NATIVE_ENGINE_BYTES-1,hash));
    CHECK(!kui_smash_native_child_engine_owner(KUI_SMASH_NATIVE_ENGINE_BYTES,NULL));
    for(unsigned i=0;i<32;i++) {hash[i]^=1;CHECK(!kui_smash_native_child_engine_owner(KUI_SMASH_NATIVE_ENGINE_BYTES,hash));hash[i]^=1;}
    free(menu);free(ip);*engine_out=engine;
}
static void arguments(const uint8_t *engine) {
    enum { N=KUI_SMASH_NATIVE_CONTEXT_BYTES };
    const uint8_t *original=engine+KUI_SMASH_NATIVE_CONTEXT_OFFSET;
    CHECK(kui_retail_crc32(0,original,N)==KUI_SMASH_NATIVE_CONTEXT_CRC32);
    uint8_t *guarded=malloc(N+32u),*before=malloc(N);CHECK(guarded && before);
    uint8_t *context=guarded+16;uint8_t ip[48],ip_before[48];
    static const char psii[]="\\MDE_US.BIN\0PHSTAR2.SGA\0MODE0\0SKIP0\0SOUND0\0";
    static const char shining[]="\\MDE_US.BIN\0SHFORCE.SGA\0MODE0\0SKIP0\0SOUND1\0";
    for(unsigned child=1;child<KUI_SMASH_NATIVE_CHILD_COUNT;child++) {
        memset(guarded,0x5c,N+32u);memcpy(context,original,N);memset(ip,0xa7,sizeof(ip));
        const char *tokens=child==KUI_SMASH_NATIVE_CHILD_PHANTASY_STAR_II?psii:shining;
        size_t bytes=child==KUI_SMASH_NATIVE_CHILD_PHANTASY_STAR_II?sizeof(psii):sizeof(shining);
        CHECK(kui_smash_native_child_arguments(context,N,ip+16,child));
        CHECK(!memcmp(context,original,256u));CHECK(!memcmp(context+512,original+512,N-512));
        CHECK(!memcmp(context+256,tokens,bytes));
        for(size_t i=256+bytes;i<512;i++)CHECK(!context[i]);
        const char *folder=child==KUI_SMASH_NATIVE_CHILD_PHANTASY_STAR_II?"PHSTAR2":"SHFORCE";
        CHECK(!strcmp(kui_smash_native_child_folder(child),folder));
        CHECK(!strcmp(kui_smash_native_child_rom(child),tokens+strlen(tokens)+1));
        CHECK(kui_smash_native_child_name(child));CHECK(!memcmp(ip+16,folder,strlen(folder)));
        for(size_t i=strlen(folder);i<12;i++)CHECK(ip[16+i]==' ');
        CHECK(ip[28]==1 && ip[29]==4 && ip[30]==0xa7 && ip[31]==0xa7);
        for(size_t i=0;i<16;i++)CHECK(guarded[i]==0x5c && guarded[N+16+i]==0x5c && ip[i]==0xa7 && ip[32+i]==0xa7);
    }
    for(unsigned fault=0;fault<10;fault++) {
        memcpy(context,original,N);memset(ip,0xa7,sizeof(ip));
        void *at=context;uint8_t *tail=ip+16;size_t bytes=N;unsigned child=1;
        switch(fault) {
        case 0:at=NULL;break;case 1:tail=NULL;break;case 2:bytes=N-1;break;
        case 3:bytes=N+1;break;case 4:child=0;break;case 5:child=KUI_SMASH_NATIVE_CHILD_COUNT;break;
        case 6:tail=context+N-1;break;case 7:tail=context;break;
        case 8:at=(void *)(uintptr_t)(UINTPTR_MAX-8u);break;
        default:tail=(uint8_t *)(uintptr_t)(UINTPTR_MAX-8u);break;
        }
        memcpy(before,context,N);memcpy(ip_before,ip,sizeof(ip));
        CHECK(!kui_smash_native_child_arguments(at,bytes,tail,child));
        CHECK(!memcmp(before,context,N) && !memcmp(ip_before,ip,sizeof(ip)));
    }
    /* Every byte, including unused context padding, belongs to the bound
     * source. Corruption must be rejected before either output changes. */
    for(size_t i=0;i<N;i++) {
        memcpy(context,original,N);context[i]^=1;memcpy(before,context,N);
        memset(ip,0xa7,sizeof(ip));memcpy(ip_before,ip,sizeof(ip));
        CHECK(!kui_smash_native_child_arguments(context,N,ip+16,1));
        CHECK(!memcmp(before,context,N) && !memcmp(ip_before,ip,sizeof(ip)));
    }
    free(before);free(guarded);
}
int main(int argc,char **argv) {
    candidates();seeds();frames();
    if(argc==2) {uint8_t *engine;owners(argv[1],&engine);arguments(engine);free(engine);}
    else CHECK(argc==1);
    printf("PASS Smash native child: %u checks%s\n",checks,argc==2?" including actual-source arguments/IP/identity":" (original-source checks not requested)");
    return 0;
}
