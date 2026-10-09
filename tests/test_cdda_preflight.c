/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_preflight.h"
#include "kui/retail_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTORS 64u
#define BASE 45000u
#define BOOT_BYTES 5003u
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL preflight line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static struct fixture {
    uint8_t data[SECTORS][2048];
    uint32_t reads,stats,fail_read,cancel_read,corrupt_header,corrupt_payload,header_kind;
    uint32_t progress_calls,last_done,progress_phase,cancel_check,cancel_at_check,cancel_progress;
    uint32_t read_sizes[128],read_lba[128];
    bool cooked;
} fixture;
static struct kui_game_image image;
static struct kui_cdda_preflight_report report,saved;
static char gdi[4096];
static uint32_t first[15],control[15],sectors[15];
static void dual16(uint8_t *p,uint16_t x){p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);p[2]=p[1];p[3]=p[0];}
static void dual32(uint8_t *p,uint32_t x){for(unsigned i=0;i<4u;i++)p[i]=p[7u-i]=(uint8_t)(x>>(i*8u));}
static unsigned entry(uint8_t *p,uint32_t lba,uint32_t bytes,uint8_t flags,const uint8_t *name,unsigned length) {
    unsigned size=33u+length+((length&1u)?0u:1u);memset(p,0,size);p[0]=(uint8_t)size;
    dual32(p+2,lba);dual32(p+10,bytes);p[25]=flags;dual16(p+28,1);p[32]=(uint8_t)length;
    memcpy(p+33,name,length);return size;
}
static int track_number(const char *name){unsigned n=0;char tail=0;return sscanf(name,"track%02u.bin%c",&n,&tail)==1 && n>=1u && n<=15u?(int)n:-1;}
static enum kui_game_result stat_file(void *context,const char *name,uint64_t *bytes) {
    CHECK(context==&fixture);int n=track_number(name);fixture.stats++;
    if(n<0)return KUI_GAME_NOT_FOUND;
    *bytes=(uint64_t)sectors[n-1]*(fixture.cooked && control[n-1]?2048u:2352u);return KUI_GAME_OK;
}
static unsigned bcd(unsigned x){return (x/10u)*16u+x%10u;}
static enum kui_game_result read_file(void *context,const char *name,uint64_t offset,void *out,size_t bytes) {
    CHECK(context==&fixture);int n=track_number(name);CHECK(n>=1 && n<=15);
    unsigned index=(unsigned)n-1u,stride=fixture.cooked && control[index]?2048u:2352u;
    CHECK(control[index]==4u && bytes==stride && offset%stride==0u && offset/stride<sectors[index]);
    CHECK(fixture.reads<128u);unsigned ordinal=fixture.reads++;uint32_t lba=first[index]+(uint32_t)(offset/stride);
    fixture.read_sizes[ordinal]=(uint32_t)bytes;fixture.read_lba[ordinal]=lba;
    if(ordinal==fixture.cancel_read)return KUI_GAME_CANCELLED;
    if(ordinal==fixture.fail_read)return KUI_GAME_IO;
    CHECK(index==2u && lba>=BASE && lba<BASE+SECTORS);
    uint8_t *destination=out;
    if(stride==2048u)memcpy(destination,fixture.data[lba-BASE],2048u);
    else {
        memset(destination,0,2352u);for(unsigned i=1;i<11u;i++)destination[i]=255u;
        uint32_t fad=lba+150u;destination[12]=(uint8_t)bcd(fad/4500u);
        destination[13]=(uint8_t)bcd(fad/75u%60u);destination[14]=(uint8_t)bcd(fad%75u);destination[15]=1;
        memcpy(destination+16u,fixture.data[lba-BASE],2048u);
        if(lba==fixture.corrupt_header) {
            if(fixture.header_kind==1u)destination[1]=0u;
            else if(fixture.header_kind==2u)destination[15]=2u;
            else destination[14]^=1u;
        }
    }
    if(lba==fixture.corrupt_payload)destination[(stride==2352u?16u:0u)+10u]^=1u;
    return KUI_GAME_OK;
}
static bool cancel(void *context){CHECK(context==&fixture);return fixture.cancel_check++==fixture.cancel_at_check;}
static bool progress(void *context,enum kui_cdda_preflight_phase phase,uint32_t done,uint32_t total) {
    CHECK(context==&fixture && phase<=KUI_CDDA_PREFLIGHT_HASH_BOOT && done<=total);
    if(fixture.progress_phase!=(uint32_t)phase){CHECK(done==0);fixture.last_done=0;fixture.progress_phase=(uint32_t)phase;}
    else CHECK(done>=fixture.last_done);
    fixture.last_done=done;return fixture.progress_calls++!=fixture.cancel_progress;
}
static struct kui_cdda_preflight_ops ops={&fixture,cancel,progress};
static void setup(bool cooked) {
    memset(&fixture,0,sizeof(fixture));fixture.cooked=cooked;
    fixture.fail_read=fixture.cancel_read=fixture.corrupt_header=fixture.corrupt_payload=UINT32_MAX;
    fixture.cancel_at_check=fixture.cancel_progress=UINT32_MAX;
    for(unsigned s=0;s<SECTORS;s++)for(unsigned i=0;i<2048u;i++)fixture.data[s][i]=(uint8_t)((s*73u+i*19u)^(i>>3));
    uint8_t *ip=fixture.data[0],*pvd=fixture.data[16];memset(ip,' ',256u);memcpy(ip,"SEGA SEGAKATANA ",16);
    memcpy(ip+37,"GD-ROM",6);memcpy(ip+48,"JUE",3);memcpy(ip+56,"0000000",7);
    memcpy(ip+64,"T-TEST0001",10);memcpy(ip+74,"V1.000",6);memcpy(ip+96,"1ST_READ.BIN",12);
    memcpy(ip+128,"K-UI COMPLETE SYNTHETIC PREFLIGHT",32);
    memset(pvd,0,2048);pvd[0]=1;memcpy(pvd+1,"CD001",5);pvd[6]=1;pvd[881]=1;
    dual32(pvd+80,SECTORS);dual16(pvd+120,1);dual16(pvd+124,1);dual16(pvd+128,2048);
    const uint8_t dot=0,parent=1;entry(pvd+156,BASE+20u,2048,2,&dot,1);
    uint8_t *directory=fixture.data[20];memset(directory,0,2048u);
    unsigned at=entry(directory,BASE+20u,2048,2,&dot,1);at+=entry(directory+at,BASE+20u,2048,2,&parent,1);
    entry(directory+at,BASE+21u,BOOT_BYTES,0,(const uint8_t*)"1ST_READ.BIN;1",14);
    size_t size=(size_t)snprintf(gdi,sizeof(gdi),"15\n");
    for(unsigned i=0;i<15u;i++) {
        first[i]=i==0u?0u:i==1u?100u:i==2u?BASE:i==13u?374201u:i==14u?377422u:50000u+(i-3u)*1000u;
        control[i]=i==0u || i==2u || i==14u?4u:0u;sectors[i]=i==2u?SECTORS:i==14u?32u:1u;
        size+=(size_t)snprintf(gdi+size,sizeof(gdi)-size,"%u %u %u %u track%02u.bin 0\n",i+1u,first[i],control[i],cooked && control[i]?2048u:2352u,i+1u);
    }
    struct kui_game_file_ops files={&fixture,stat_file,read_file};
    CHECK(kui_game_image_open(gdi,size,&files,&image)==KUI_GAME_OK && fixture.stats==15u && !fixture.reads);
    memset(&report,0xa5,sizeof(report));saved=report;
}
static uint32_t reference_crc(const uint8_t *data,size_t bytes) {
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<bytes;i++){crc^=data[i];for(unsigned b=0;b<8u;b++)crc=crc&1u?(crc>>1)^0xedb88320u:crc>>1;}
    return ~crc;
}
static bool digest_is(const uint8_t digest[32],const char *hex) {
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<32u;i++) if(hex[i*2u]!=digits[digest[i]>>4] || hex[i*2u+1u]!=digits[digest[i]&15u])return false;
    return hex[64]==0;
}
static void verify_success(bool cooked) {
    setup(cooked);CHECK(kui_cdda_preflight_read(&image,&ops,&report)==KUI_CDDA_PREFLIGHT_OK);
    CHECK(report.map.count==15u && report.map.complete && !report.scrambled && report.metadata.native_gd &&
       report.metadata.ip_valid && report.metadata.boot_valid && !report.metadata.windows_ce);
    CHECK(!strcmp(report.metadata.product,"T-TEST0001") && !strcmp(report.metadata.version,"V1.000") &&
       !strcmp(report.metadata.region,"JUE") && !strcmp(report.metadata.bootfile,"1ST_READ.BIN"));
    CHECK(report.ip_lba==BASE && report.ip_bytes==32768u && report.metadata.boot_lba==BASE+21u && report.metadata.boot_bytes==BOOT_BYTES);
    CHECK(report.metadata_reads==3u && report.metadata.sectors_read==3u && report.hash_reads==19u && fixture.reads==22u);
    CHECK(report.ip_crc32==reference_crc(fixture.data[0],32768u) && report.boot_crc32==reference_crc(fixture.data[21],BOOT_BYTES));
    /* Independent Python hashlib/zlib identities for the synthetic bytes above. */
    CHECK(report.ip_crc32==0x4e920ea2u && report.boot_crc32==0xf44bc7d4u);
    CHECK(digest_is(report.ip_sha256,"26ca8bcf3be957cc13b1db96efb20799449ea74f388408d1004af4221ee0b8ce"));
    CHECK(digest_is(report.boot_sha256,"cbee6a04f43048cc61ae6c6069f2c282cee7583a4a34c2c7bbb113491f4555ca"));
    for(unsigned i=0;i<fixture.reads;i++) CHECK(fixture.read_sizes[i]==(cooked?2048u:2352u));
    for(unsigned i=0;i<16u;i++)CHECK(fixture.read_lba[3u+i]==BASE+i);
    for(unsigned i=0;i<3u;i++)CHECK(fixture.read_lba[19u+i]==BASE+21u+i);
    CHECK(fixture.progress_calls==25u && fixture.last_done==BOOT_BYTES && fixture.progress_phase==KUI_CDDA_PREFLIGHT_HASH_BOOT);
}
static void fault_cases(void) {
    for(unsigned cooked=0;cooked<2u;cooked++)for(uint32_t ordinal=0;ordinal<22u;ordinal++) {
        setup(cooked!=0);fixture.fail_read=ordinal;CHECK(kui_cdda_preflight_read(&image,&ops,&report)==KUI_CDDA_PREFLIGHT_IO);
        CHECK(!memcmp(&report,&saved,sizeof(report)) && fixture.reads==ordinal+1u);
        setup(cooked!=0);fixture.cancel_read=ordinal;CHECK(kui_cdda_preflight_read(&image,&ops,&report)==KUI_CDDA_PREFLIGHT_CANCELLED);
        CHECK(!memcmp(&report,&saved,sizeof(report)) && fixture.reads==ordinal+1u);
    }
    for(uint32_t step=0;step<25u;step++) {
        setup(false);fixture.cancel_progress=step;
        CHECK(kui_cdda_preflight_read(&image,&ops,&report)==KUI_CDDA_PREFLIGHT_CANCELLED && !memcmp(&report,&saved,sizeof(report)));
    }
    setup(false);CHECK(kui_cdda_preflight_read(&image,&ops,&report)==KUI_CDDA_PREFLIGHT_OK);
    uint32_t cancel_boundaries=fixture.cancel_check;CHECK(cancel_boundaries && cancel_boundaries<100u);
    for(uint32_t step=0;step<cancel_boundaries;step++) {
        setup(false);fixture.cancel_at_check=step;
        CHECK(kui_cdda_preflight_read(&image,&ops,&report)==KUI_CDDA_PREFLIGHT_CANCELLED && !memcmp(&report,&saved,sizeof(report)));
    }
    setup(false);fixture.cancel_at_check=0;
    CHECK(kui_cdda_preflight_read(&image,&ops,&report)==KUI_CDDA_PREFLIGHT_CANCELLED && !fixture.reads && !memcmp(&report,&saved,sizeof(report)));
    const uint32_t bad_headers[]={BASE,BASE+16u,BASE+20u,BASE+8u,BASE+21u,BASE+22u,BASE+23u};
    for(unsigned i=0;i<sizeof(bad_headers)/sizeof(bad_headers[0]);i++) {
        setup(false);fixture.corrupt_header=bad_headers[i];
        CHECK(kui_cdda_preflight_read(&image,&ops,&report)==KUI_CDDA_PREFLIGHT_HEADER && !memcmp(&report,&saved,sizeof(report)));
    }
    for(unsigned kind=1;kind<=2u;kind++) {
        setup(false);fixture.header_kind=kind;fixture.corrupt_header=BASE+21u;
        CHECK(kui_cdda_preflight_read(&image,&ops,&report)==KUI_CDDA_PREFLIGHT_HEADER && !memcmp(&report,&saved,sizeof(report)));
    }
    setup(true);fixture.data[16][1]='X';CHECK(kui_cdda_preflight_read(&image,&ops,&report)==KUI_CDDA_PREFLIGHT_METADATA && fixture.reads==2u && !memcmp(&report,&saved,sizeof(report)));
    setup(false);dual32(fixture.data[20]+68u+2u,BASE+63u);
    CHECK(kui_cdda_preflight_read(&image,&ops,&report)==KUI_CDDA_PREFLIGHT_METADATA && fixture.reads==3u && !memcmp(&report,&saved,sizeof(report)));
    setup(false);dual32(fixture.data[20]+68u+2u,BASE+2u);
    CHECK(kui_cdda_preflight_read(&image,&ops,&report)==KUI_CDDA_PREFLIGHT_RANGE && fixture.reads==3u && !memcmp(&report,&saved,sizeof(report)));
    setup(false);image.tracks[2].end_lba=BASE+15u;
    CHECK(kui_cdda_preflight_read(&image,&ops,&report)==KUI_CDDA_PREFLIGHT_RANGE && !fixture.reads && !memcmp(&report,&saved,sizeof(report)));
    setup(false);image.tracks[13].file_bytes=0;
    CHECK(kui_cdda_preflight_read(&image,&ops,&report)==KUI_CDDA_PREFLIGHT_MAP && !fixture.reads && !memcmp(&report,&saved,sizeof(report)));
    setup(false);strcpy(image.tracks[5].name,"../BAD.raw");
    CHECK(kui_cdda_preflight_read(&image,&ops,&report)==KUI_CDDA_PREFLIGHT_MAP && !fixture.reads);
    setup(false);image.data_lba++;
    CHECK(kui_cdda_preflight_read(&image,&ops,&report)==KUI_CDDA_PREFLIGHT_MAP && !fixture.reads);
    setup(false);image.format=KUI_GAME_IMAGE_ISO;
    CHECK(kui_cdda_preflight_read(&image,&ops,&report)==KUI_CDDA_PREFLIGHT_INVALID && !fixture.reads);
    setup(false);CHECK(kui_cdda_preflight_read(NULL,&ops,&report)==KUI_CDDA_PREFLIGHT_INVALID);
    CHECK(kui_cdda_preflight_read(&image,&ops,NULL)==KUI_CDDA_PREFLIGHT_INVALID);
    CHECK(kui_cdda_preflight_read(&image,&ops,(struct kui_cdda_preflight_report*)(void*)&image)==KUI_CDDA_PREFLIGHT_INVALID && !fixture.reads);
}
int main(void) {
    verify_success(false);struct kui_cdda_preflight_report raw=report;
    verify_success(true);CHECK(report.ip_crc32==raw.ip_crc32 && report.boot_crc32==raw.boot_crc32 &&
        !memcmp(report.ip_sha256,raw.ip_sha256,32u) && !memcmp(report.boot_sha256,raw.boot_sha256,32u));
    setup(false);image.scrambled=true;CHECK(kui_cdda_preflight_read(&image,NULL,&report)==KUI_CDDA_PREFLIGHT_OK && report.scrambled);
    setup(true);fixture.data[23][BOOT_BYTES%2048u]^=1u;
    CHECK(kui_cdda_preflight_read(&image,NULL,&report)==KUI_CDDA_PREFLIGHT_OK && report.boot_crc32==raw.boot_crc32 && !memcmp(report.boot_sha256,raw.boot_sha256,32u));
    setup(true);fixture.data[21][10]^=1u;
    CHECK(kui_cdda_preflight_read(&image,NULL,&report)==KUI_CDDA_PREFLIGHT_OK && report.boot_crc32!=raw.boot_crc32 && memcmp(report.boot_sha256,raw.boot_sha256,32u));
    fault_cases();printf("PASS CDDA preflight: %u checks; complete15-track metadata, raw/cooked exact identities, header address validation, whole spans, unchanged failure and bounded cancellation\n",checks);return 0;
}
