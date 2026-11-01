/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_boot.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define FIRST 45150u
#define SECTORS 128u
#define ROOT 32u
#define BOOT 40u
#define BOOT_BYTES 3001u
static unsigned checks;
#define CHECK(x) do { ++checks;assert(x); } while(0)
static struct fixture {
    uint8_t data[SECTORS][2048];
    uint32_t fads[SECTORS],reads,fail_at,cancel_at,unknown_at;
    bool stop,stop_after_read,change_ip;
    struct kui_disc_boot_source source;
} fixture;
static void dual16(uint8_t *p,uint16_t n) {
    p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=p[1];p[3]=p[0];
}
static void dual32(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4u;i++)p[i]=p[7u-i]=(uint8_t)(n>>(8u*i));
}
static unsigned record(uint8_t *p,uint32_t lba,uint32_t bytes,uint8_t flags,
                       const uint8_t *name,unsigned length) {
    unsigned size=33u+length+((length&1u)?0u:1u);memset(p,0,size);
    p[0]=(uint8_t)size;dual32(p+2u,lba);dual32(p+10u,bytes);p[25]=flags;
    dual16(p+28u,1u);p[32]=(uint8_t)length;memcpy(p+33u,name,length);return size;
}
static enum kui_disc_boot_io_result read_sector(void *context,uint32_t fad,uint8_t data[2048]) {
    struct fixture *f=context;
    CHECK(fad>=f->source.first_fad && fad<f->source.end_fad);
    CHECK(f->reads<SECTORS);f->fads[f->reads++]=fad;
    if(f->reads==f->fail_at)return KUI_DISC_BOOT_IO_ERROR;
    if(f->reads==f->cancel_at)return KUI_DISC_BOOT_IO_CANCELLED;
    if(f->reads==f->unknown_at)return (enum kui_disc_boot_io_result)77;
    CHECK(fad>=FIRST && fad-FIRST<SECTORS);memcpy(data,f->data[fad-FIRST],2048u);
    if(f->change_ip && fad==FIRST && f->reads>1u)data[128]^=1u;
    if(f->stop_after_read)f->stop=true;
    return KUI_DISC_BOOT_IO_OK;
}
static bool cancelled(void *context) {return ((struct fixture *)context)->stop;}
static void setup(void) {
    memset(&fixture,0,sizeof(fixture));
    fixture.source=(struct kui_disc_boot_source){&fixture,read_sector,cancelled,
        KUI_DISC_BOOT_GD_TYPE,FIRST,FIRST+SECTORS};
    for(unsigned sector=0;sector<16u;sector++)
        for(unsigned at=0;at<2048u;at++)fixture.data[sector][at]=(uint8_t)(sector*17u+at);
    uint8_t *ip=fixture.data[0],*pvd=fixture.data[16];
    memset(ip,' ',256u);memcpy(ip,"SEGA SEGAKATANA ",16u);
    memcpy(ip+37u,"GD-ROM",6u);memcpy(ip+48u,"JUE",3u);memcpy(ip+56u,"0000000",7u);
    memcpy(ip+64u,"T-12345",7u);memcpy(ip+74u,"V1.000",6u);
    memcpy(ip+96u,"1ST_READ.BIN",12u);memcpy(ip+128u,"SYNTHETIC GD BOOT",17u);
    pvd[0]=1u;memcpy(pvd+1u,"CD001",5u);pvd[6]=1u;pvd[881]=1u;
    dual32(pvd+80u,SECTORS);dual16(pvd+120u,1u);dual16(pvd+124u,1u);dual16(pvd+128u,2048u);
    const uint8_t dot=0,parent=1u;
    record(pvd+156u,FIRST-150u+ROOT,2048u,2u,&dot,1u);
    uint8_t *directory=fixture.data[ROOT];
    unsigned at=record(directory,FIRST-150u+ROOT,2048u,2u,&dot,1u);
    at+=record(directory+at,FIRST-150u+ROOT,2048u,2u,&parent,1u);
    record(directory+at,FIRST-150u+BOOT,BOOT_BYTES,0u,(const uint8_t *)"1ST_READ.BIN;1",14u);
    for(unsigned i=0;i<4096u;i++)fixture.data[BOOT+i/2048u][i%2048u]=(uint8_t)(i*29u+11u);
}
static uint8_t *boot_record(void) {return fixture.data[ROOT]+68u;}
static void clear_plan(const struct kui_disc_boot_plan *plan) {
    const uint8_t *p=(const uint8_t *)plan;
    for(unsigned i=0;i<sizeof(*plan);i++)CHECK(!p[i]);
}
static void refused(enum kui_disc_boot_status expected) {
    struct kui_disc_boot_plan plan;memset(&plan,0xa5,sizeof(plan));
    CHECK(kui_disc_boot_prepare(&fixture.source,&plan)==expected);clear_plan(&plan);
}
static void digest(const void *data,size_t bytes,uint8_t out[32]) {
    struct kui_sha256 sha;kui_sha256_init(&sha);kui_sha256_update(&sha,data,bytes);
    kui_sha256_digest(&sha,out);
}
static void success_and_regions(void) {
    static const char *const regions[]={"J","U","E","JUE"};
    struct kui_disc_boot_plan baseline={0};
    for(unsigned r=0;r<sizeof(regions)/sizeof(regions[0]);r++) {
        setup();memset(fixture.data[0]+48u,' ',8u);memcpy(fixture.data[0]+48u,regions[r],strlen(regions[r]));
        struct kui_disc_boot_plan plan;CHECK(kui_disc_boot_prepare(&fixture.source,&plan)==KUI_DISC_BOOT_OK);
        CHECK(plan.first_fad==FIRST && plan.end_fad==FIRST+SECTORS && plan.boot_fad==FIRST+BOOT);
        CHECK(plan.metadata.session_lba==FIRST-150u && plan.metadata.boot_lba==FIRST-150u+BOOT);
        CHECK(plan.metadata.boot_bytes==BOOT_BYTES && plan.boot_sectors==2u);
        CHECK(!strcmp(plan.metadata.product,"T-12345") && !strcmp(plan.metadata.bootfile,"1ST_READ.BIN"));
        CHECK(!strcmp(plan.metadata.region,regions[r]));
        CHECK(plan.sectors_read==21u && fixture.reads==21u && plan.metadata.sectors_read==3u);
        CHECK(fixture.fads[0]==FIRST && fixture.fads[1]==FIRST+16u && fixture.fads[2]==FIRST+ROOT);
        for(unsigned i=0;i<16u;i++)CHECK(fixture.fads[3u+i]==FIRST+i);
        CHECK(fixture.fads[19]==FIRST+BOOT && fixture.fads[20]==FIRST+BOOT+1u);
        uint8_t sha[32];digest(fixture.data,KUI_DISC_BOOT_IP_BYTES,sha);
        CHECK(!memcmp(sha,plan.ip_sha256,32u));
        CHECK(plan.ip_crc32==kui_crc32(0u,fixture.data,KUI_DISC_BOOT_IP_BYTES));
        digest(fixture.data[BOOT],BOOT_BYTES,sha);CHECK(!memcmp(sha,plan.boot_sha256,32u));
        CHECK(plan.boot_crc32==kui_crc32(0u,fixture.data[BOOT],BOOT_BYTES));
        if(r==0u)baseline=plan;
    }
    /* The final sector's allocation padding is not part of the executable. */
    setup();struct kui_disc_boot_plan before,after;
    CHECK(kui_disc_boot_prepare(&fixture.source,&before)==KUI_DISC_BOOT_OK);
    memset(fixture.data[BOOT+1u]+(BOOT_BYTES-2048u),0x73,4096u-BOOT_BYTES);fixture.reads=0u;
    CHECK(kui_disc_boot_prepare(&fixture.source,&after)==KUI_DISC_BOOT_OK);
    CHECK(before.boot_crc32==after.boot_crc32 && !memcmp(before.boot_sha256,after.boot_sha256,32u));
    fixture.data[BOOT][0]^=1u;fixture.reads=0u;
    CHECK(kui_disc_boot_prepare(&fixture.source,&after)==KUI_DISC_BOOT_OK);
    CHECK(before.boot_crc32!=after.boot_crc32 && memcmp(before.boot_sha256,after.boot_sha256,32u));
    CHECK(baseline.boot_crc32==before.boot_crc32);
    /* The primary descriptor need not be the first descriptor. Its actual
     * address still fences the root and boot extents; do not assume +16. */
    for(unsigned primary=1u;primary<16u;primary++) {
        setup();memcpy(fixture.data[16u+primary],fixture.data[16u],2048u);
        for(unsigned at=16u;at<16u+primary;at++) {
            memset(fixture.data[at],0,2048u);memcpy(fixture.data[at]+1u,"CD001",5u);
            fixture.data[at][6]=1u;
        }
        CHECK(kui_disc_boot_prepare(&fixture.source,&after)==KUI_DISC_BOOT_OK);
        CHECK(after.sectors_read==21u+primary && after.boot_crc32==before.boot_crc32);
    }
    /* A rounded-up final sector may end exactly at the observed data limit. */
    setup();dual32(boot_record()+2u,FIRST-150u+SECTORS-2u);
    CHECK(kui_disc_boot_prepare(&fixture.source,&after)==KUI_DISC_BOOT_OK);
    CHECK(after.boot_fad==FIRST+SECTORS-2u && after.boot_fad+after.boot_sectors==after.end_fad);
}
static void geometry_and_metadata(void) {
    setup();fixture.source.disc_type=0x10u;refused(KUI_DISC_BOOT_MEDIA);CHECK(!fixture.reads);
    setup();fixture.source.disc_type=0x10u;fixture.source.first_fad=150u;
    refused(KUI_DISC_BOOT_MEDIA);CHECK(!fixture.reads);
    setup();fixture.source.first_fad=45149u;refused(KUI_DISC_BOOT_ARGUMENT);CHECK(!fixture.reads);
    setup();fixture.source.end_fad=FIRST;refused(KUI_DISC_BOOT_ARGUMENT);CHECK(!fixture.reads);
    setup();fixture.source.end_fad=FIRST+17u;refused(KUI_DISC_BOOT_ARGUMENT);CHECK(!fixture.reads);
    setup();fixture.source.end_fad=UINT32_C(0x01000001);refused(KUI_DISC_BOOT_ARGUMENT);CHECK(!fixture.reads);
    setup();fixture.source.read_sector=NULL;refused(KUI_DISC_BOOT_ARGUMENT);CHECK(!fixture.reads);
    setup();fixture.source.cancelled=NULL;refused(KUI_DISC_BOOT_ARGUMENT);CHECK(!fixture.reads);
    setup();fixture.data[0][0]^=1u;refused(KUI_DISC_BOOT_IP);
    setup();memcpy(fixture.data[0]+37u,"CD-ROM",6u);refused(KUI_DISC_BOOT_MEDIA);
    setup();fixture.data[0][62]='1';refused(KUI_DISC_BOOT_WINDOWS_CE);
    setup();fixture.data[0][56]='Z';refused(KUI_DISC_BOOT_MEDIA);
    setup();memcpy(fixture.data[0]+96u,"../READ.BIN",11u);refused(KUI_DISC_BOOT_UNSUPPORTED);
    setup();fixture.data[16][81]^=1u;refused(KUI_DISC_BOOT_ISO);
    setup();fixture.data[16][129]^=1u;refused(KUI_DISC_BOOT_ISO);
    setup();dual16(fixture.data[16]+128u,512u);refused(KUI_DISC_BOOT_UNSUPPORTED);
    setup();fixture.data[16][156+2u]^=1u;refused(KUI_DISC_BOOT_ISO);
    setup();boot_record()[6]^=1u;refused(KUI_DISC_BOOT_ISO);
    setup();boot_record()[25]=128u;refused(KUI_DISC_BOOT_UNSUPPORTED);
    setup();boot_record()[33]='Z';refused(KUI_DISC_BOOT_NOT_FOUND);
    setup();dual32(boot_record()+2u,FIRST-150u+SECTORS-1u);refused(KUI_DISC_BOOT_ISO);
    setup();dual32(boot_record()+2u,UINT32_MAX-1u);refused(KUI_DISC_BOOT_ISO);
    setup();dual32(boot_record()+2u,FIRST-150u+ROOT);refused(KUI_DISC_BOOT_ISO);
    setup();dual32(boot_record()+2u,FIRST-150u);refused(KUI_DISC_BOOT_ISO);
    setup();fixture.source.end_fad=FIRST+9000u;
    dual32(fixture.data[16]+80u,9000u);dual32(boot_record()+10u,KUI_DISC_BOOT_MAX_EXEC_BYTES+1u);
    refused(KUI_DISC_BOOT_LIMIT);CHECK(fixture.reads==3u);
    setup();fixture.change_ip=true;refused(KUI_DISC_BOOT_CHANGED);CHECK(fixture.reads==4u);
    /* Byte order and flags fail before executable reads, with no partial plan. */
    setup();unsigned original=boot_record()[0];memcpy(boot_record()+original,boot_record(),original);
    refused(KUI_DISC_BOOT_ISO);
}
static void fault_and_cancel(void) {
    for(unsigned read=1u;read<=21u;read++) {
        setup();fixture.fail_at=read;refused(KUI_DISC_BOOT_IO);CHECK(fixture.reads==read);
        setup();fixture.cancel_at=read;refused(KUI_DISC_BOOT_CANCELLED);CHECK(fixture.reads==read);
        setup();fixture.unknown_at=read;refused(KUI_DISC_BOOT_IO);CHECK(fixture.reads==read);
    }
    setup();fixture.stop=true;refused(KUI_DISC_BOOT_CANCELLED);CHECK(!fixture.reads);
    setup();fixture.stop_after_read=true;refused(KUI_DISC_BOOT_CANCELLED);CHECK(fixture.reads==1u);
    setup();struct kui_disc_boot_plan plan;memset(&plan,0xa5,sizeof(plan));
    CHECK(kui_disc_boot_prepare(NULL,&plan)==KUI_DISC_BOOT_ARGUMENT);
    const uint8_t *bytes=(const uint8_t *)&plan;
    for(unsigned i=0;i<sizeof(plan);i++)CHECK(bytes[i]==0xa5u);
    CHECK(kui_disc_boot_prepare(&fixture.source,NULL)==KUI_DISC_BOOT_ARGUMENT);CHECK(!fixture.reads);
    union {struct kui_disc_boot_source source;struct kui_disc_boot_plan plan;} overlap;
    memset(&overlap,0,sizeof(overlap));overlap.source=fixture.source;
    uint8_t saved[sizeof(overlap)];memcpy(saved,&overlap,sizeof(saved));
    CHECK(kui_disc_boot_prepare(&overlap.source,&overlap.plan)==KUI_DISC_BOOT_ARGUMENT);
    CHECK(!memcmp(saved,&overlap,sizeof(saved)) && !fixture.reads);
    CHECK(strstr(kui_disc_boot_status_text(KUI_DISC_BOOT_WINDOWS_CE),"Windows CE"));
    CHECK(strstr(kui_disc_boot_status_text(KUI_DISC_BOOT_CHANGED),"changed"));
}
int main(void) {
    success_and_regions();geometry_and_metadata();fault_and_cancel();
    printf("Physical GD boot preflight: %u checks; FAD bounds, exact IP/executable hashes, J/U/E regions, malformed ISO, all read cuts and cancellation\n",checks);
    return 0;
}
