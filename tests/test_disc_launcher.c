/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual app, optical boot planner and packet/hash admission. Only physical
 * drive, bootstrap file loading and mount lifecycle are supplied by this bus. */
#include "kui/disc_launcher.h"
#include "kui/disc_launch.h"
#include "kui/hash.h"
#include "platform.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIRST 45150u
#define SECTORS 128u
#define ROOT 32u
#define BOOT 40u
#define BOOT_BYTES 3001u
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"%s:%u: %s\n",__FILE__,__LINE__,#x);abort();}} while(0)
static struct fixture {
    uint8_t data[SECTORS][2048];
    struct kui_toc toc[2];
    unsigned connections,disconnects,mounts,unmounts,runtime_reads,runtime_frees;
    unsigned preparations,raw_reads,progress,logs,reallocations;
    unsigned read_fail,edc_fail,address_fail,cancel_at,change_at;
    bool connected,mounted,stop,connect_fail,mount_fail,unmount_fail,prepare_fail,is_cd,handoff_fail;
    bool runtime_fail,template_invalid,realloc_fail,ce;
    struct kui_app_status last;
} fixture;
static void dual16(uint8_t *p,uint16_t n) {
    p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);p[2]=p[1];p[3]=p[0];
}
static void dual32(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4u;i++)p[i]=p[7u-i]=(uint8_t)(n>>(8u*i));
}
static void put(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(n>>(8u*i));
}
static unsigned record(uint8_t *p,uint32_t lba,uint32_t bytes,uint8_t flags,
    const uint8_t *name,unsigned length) {
    unsigned size=33u+length+((length&1u)?0u:1u);memset(p,0,size);
    p[0]=(uint8_t)size;dual32(p+2u,lba);dual32(p+10u,bytes);p[25]=flags;
    dual16(p+28u,1u);p[32]=(uint8_t)length;memcpy(p+33u,name,length);return size;
}
static void setup(void) {
    memset(&fixture,0,sizeof(fixture));
    fixture.toc[0].count=1u;fixture.toc[0].tracks[0]=(struct kui_track){1u,4u,150u,1000u};
    fixture.toc[1].count=1u;fixture.toc[1].tracks[0]=(struct kui_track){3u,4u,FIRST,FIRST+SECTORS};
    for(unsigned sector=0;sector<16u;sector++)
        for(unsigned at=0;at<2048u;at++)fixture.data[sector][at]=(uint8_t)(sector*17u+at);
    uint8_t *ip=fixture.data[0],*pvd=fixture.data[16];
    memset(ip,' ',256u);memcpy(ip,"SEGA SEGAKATANA ",16u);
    memcpy(ip+37u,"GD-ROM",6u);memcpy(ip+48u,"JUE",3u);memcpy(ip+56u,"0000000",7u);
    memcpy(ip+64u,"T-12345",7u);memcpy(ip+74u,"V1.000",6u);
    memcpy(ip+96u,"1ST_READ.BIN",12u);memcpy(ip+128u,"SYNTHETIC PHYSICAL GD",21u);
    pvd[0]=1u;memcpy(pvd+1u,"CD001",5u);pvd[6]=1u;pvd[881]=1u;
    dual32(pvd+80u,SECTORS);dual16(pvd+120u,1u);dual16(pvd+124u,1u);dual16(pvd+128u,2048u);
    const uint8_t dot=0,parent=1u;
    record(pvd+156u,FIRST-150u+ROOT,2048u,2u,&dot,1u);
    unsigned at=record(fixture.data[ROOT],FIRST-150u+ROOT,2048u,2u,&dot,1u);
    at+=record(fixture.data[ROOT]+at,FIRST-150u+ROOT,2048u,2u,&parent,1u);
    record(fixture.data[ROOT]+at,FIRST-150u+BOOT,BOOT_BYTES,0u,(const uint8_t *)"1ST_READ.BIN;1",14u);
    for(unsigned i=0;i<4096u;i++)fixture.data[BOOT+i/2048u][i%2048u]=(uint8_t)(i*29u+11u);
}
static bool cancelled(void) {return fixture.stop;}
static void log_line(const char *format,...) {(void)format;++fixture.logs;}
static void progress(const struct kui_app_status *s) {fixture.last=*s;++fixture.progress;}
bool kui_sd_connect(void) {
    CHECK(!fixture.connected && !fixture.mounted);++fixture.connections;
    fixture.connected=!fixture.connect_fail;return fixture.connected;
}
void kui_sd_disconnect(void) {
    CHECK(fixture.connected);++fixture.disconnects;fixture.connected=fixture.mounted=false;
}
bool kui_mount(FATFS *fs,kui_log_fn log) {
    (void)fs;(void)log;CHECK(fixture.connected && !fixture.mounted);++fixture.mounts;
    fixture.mounted=!fixture.mount_fail;return fixture.mounted;
}
FRESULT f_mount(FATFS *fs,const TCHAR *path,BYTE immediate) {
    CHECK(!fs && !strcmp(path,"0:") && !immediate && fixture.connected);
    ++fixture.unmounts;if(fixture.unmount_fail)return FR_DISK_ERR;
    fixture.mounted=false;return FR_OK;
}
enum kui_runtime_result kui_runtime_read(const char *path,struct kui_runtime_image *out,
    kui_log_fn log,kui_cancel_fn cancel) {
    (void)log;(void)cancel;
    CHECK(fixture.connected && fixture.mounted);
    /* This is the ONLY permitted filesystem pathname in this app. Any VMU
     * image, assignment, candidate, journal or preference lookup fails here. */
    CHECK(!strcmp(path,KUI_DISC_LAUNCH_PACKAGE));++fixture.runtime_reads;
    memset(out,0,sizeof(*out));if(fixture.runtime_fail)return KUI_RUNTIME_IO;
    uint32_t bytes=KUI_DISC_LAUNCH_STAGE_OFFSET+4u;
    out->data=calloc(1u,bytes);CHECK(out->data);uint8_t *h=(uint8_t *)out->data+KUI_DISC_LAUNCH_HEADER_OFFSET;
    memcpy(h,"KUIDISC1",8u);put(h+8u,1u);put(h+12u,KUI_DISC_LAUNCH_HEADER_BYTES);
    put(h+16u,KUI_DISC_LAUNCH_CONFIG_OFFSET);put(h+20u,KUI_DISC_LAUNCH_CONFIG_BYTES);
    put(h+24u,KUI_DISC_LAUNCH_STAGE_ADDRESS);put(h+28u,4u);
    put(h+32u,KUI_DISC_LAUNCH_STAGE_ADDRESS);put(h+36u,KUI_DISC_LAUNCH_EXEC_ADDRESS);
    put(h+40u,KUI_DISC_LAUNCH_EXEC_MAX_BYTES);put(h+44u,KUI_DISC_LAUNCH_STACK);
    put(h+48u,KUI_DISC_LAUNCH_STAGE_OFFSET);put(h+52u,KUI_DISC_LAUNCH_FLAGS);
    if(fixture.template_invalid)h[16u]^=1u;
    out->info.payload_bytes=out->info.memory_bytes=bytes;
    memcpy(out->info.build,"abcdef012345",13u);return KUI_RUNTIME_OK;
}
void kui_runtime_free(struct kui_runtime_image *image) {
    if(image->data){++fixture.runtime_frees;free(image->data);}
    memset(image,0,sizeof(*image));
}
void *__real_realloc(void *,size_t);
void *__wrap_realloc(void *p,size_t bytes) {
    ++fixture.reallocations;
    if(fixture.realloc_fail)return NULL;
    return __real_realloc(p,bytes);
}
bool kui_disc_prepare(struct kui_toc sessions[2]) {
    ++fixture.preparations;CHECK(!fixture.connected && !fixture.mounted);
    if(fixture.prepare_fail)return false;
    memcpy(sessions,fixture.toc,sizeof(fixture.toc));return true;
}
bool kui_disc_prepare_capture(struct kui_toc sessions[2],bool *is_cd) {
    bool result=kui_disc_prepare(sessions);*is_cd=fixture.is_cd;return result;
}
bool kui_disc_native_handoff(uint32_t fad,const uint8_t expected[2048]) {
    CHECK(fad==FIRST && expected && !fixture.connected && !fixture.mounted);
    CHECK(!memcmp(expected,fixture.data[0],2048u));
    return !fixture.handoff_fail;
}
static uint8_t bcd(unsigned n) {return (uint8_t)((n/10u)*16u+n%10u);}
enum kui_read_result kui_disc_read_raw(void *context,uint32_t fad,unsigned count,uint8_t *out) {
    (void)context;CHECK(count==1u && !fixture.connected && !fixture.mounted);
    CHECK(fad>=FIRST && fad-FIRST<SECTORS);++fixture.raw_reads;
    if(fixture.cancel_at==fixture.raw_reads)fixture.stop=true;
    if(fixture.read_fail==fixture.raw_reads)return KUI_READ_RETRY;
    memset(out,0,2352u);memset(out+1u,255,10u);
    out[12]=bcd(fad/4500u);out[13]=bcd((fad/75u)%60u);out[14]=bcd(fad%75u);out[15]=1u;
    memcpy(out+16u,fixture.data[fad-FIRST],2048u);
    if(fixture.change_at==fixture.raw_reads)out[16u+100u]^=1u;
    if(fixture.address_fail==fixture.raw_reads)out[14]=bcd((fad+1u)%75u);
    put(out+2064u,kui_cd_edc(out,2064u));
    if(fixture.edc_fail==fixture.raw_reads)out[16u+101u]^=1u;
    return KUI_READ_OK;
}
static void released(void) {
    CHECK(!fixture.connected && !fixture.mounted);
    CHECK(fixture.connections==1u && fixture.disconnects==1u);
    CHECK(fixture.mounts==1u && fixture.unmounts==1u);
}
static void refusal(bool stop) {
    struct kui_runtime_image image={0};struct kui_app_status status;
    CHECK(!kui_disc_launcher_prepare(&image,&status,log_line,cancelled,progress));
    CHECK(!image.data && !image.info.payload_bytes && !image.info.memory_bytes);
    CHECK(status.complete && !status.passed && status.stopped==stop && status.errors==(stop?0u:1u));
    CHECK(status.message[0] && fixture.progress && fixture.last.complete);
    CHECK(!fixture.connected && !fixture.mounted);
}
static void successful_paths(void) {
    setup();struct kui_app_status status;
    kui_disc_launcher_check(&status,log_line,cancelled,progress);
    CHECK(status.complete && status.passed && !status.errors && !status.stopped);
    CHECK(status.line_count==4u && status.done==21u*2048u);
    CHECK(fixture.raw_reads==21u && fixture.preparations==1u);
    CHECK(!fixture.connections && !fixture.runtime_reads && !fixture.runtime_frees);
    setup();struct kui_runtime_image image={0};
    CHECK(kui_disc_launcher_prepare(&image,&status,log_line,cancelled,progress));
    CHECK(status.complete && status.passed && !status.stopped && !status.errors);
    CHECK(fixture.runtime_reads==1u && fixture.reallocations==1u && !fixture.runtime_frees);
    CHECK(fixture.raw_reads==39u && status.done==39u*2048u);released();
    struct kui_disc_launch_plan plan;CHECK(kui_disc_launch_validate(image.data,image.info.payload_bytes,&plan));
    CHECK(plan.exec_bytes==BOOT_BYTES);
    CHECK(!memcmp((uint8_t *)image.data+plan.ip_offset,fixture.data,32768u));
    CHECK(!memcmp((uint8_t *)image.data+plan.exec_offset,fixture.data[BOOT],BOOT_BYTES));
    CHECK(image.info.crc32==kui_crc32(0u,image.data,image.info.payload_bytes));
    CHECK(image.info.payload_bytes==image.info.memory_bytes && fixture.last.complete);
    kui_runtime_free(&image);CHECK(fixture.runtime_frees==1u && !image.data);
}
static void preparation_failures(void) {
    setup();fixture.is_cd=true;refusal(false);released();CHECK(!fixture.raw_reads);
    setup();fixture.toc[1].count=100u;refusal(false);released();CHECK(!fixture.raw_reads);
    setup();fixture.handoff_fail=true;refusal(false);released();CHECK(fixture.raw_reads==39u);
    setup();fixture.stop=true;refusal(true);CHECK(!fixture.connections && !fixture.preparations);
    setup();fixture.connect_fail=true;refusal(false);CHECK(!fixture.mounts && !fixture.disconnects);
    setup();fixture.mount_fail=true;refusal(false);released();CHECK(!fixture.runtime_reads && !fixture.preparations);
    setup();fixture.runtime_fail=true;refusal(false);released();CHECK(fixture.runtime_reads==1u && !fixture.preparations);
    setup();fixture.unmount_fail=true;refusal(false);released();CHECK(fixture.runtime_frees==1u && !fixture.preparations);
    setup();fixture.template_invalid=true;refusal(false);released();CHECK(fixture.runtime_frees==1u && !fixture.preparations);
    setup();fixture.prepare_fail=true;refusal(false);released();CHECK(!fixture.raw_reads && fixture.runtime_frees==1u);
    setup();fixture.toc[1].count=0u;refusal(false);released();CHECK(!fixture.raw_reads);
    setup();fixture.toc[1].tracks[0].control=0u;refusal(false);released();CHECK(!fixture.raw_reads);
    setup();fixture.toc[1].tracks[0].end=FIRST;refusal(false);released();CHECK(!fixture.raw_reads);
    setup();fixture.toc[1].tracks[0].start=45149u;refusal(false);released();CHECK(!fixture.raw_reads);
    setup();fixture.data[0][62]='1';refusal(false);released();CHECK(fixture.raw_reads==3u);
    setup();fixture.realloc_fail=true;refusal(false);released();CHECK(fixture.reallocations==1u && fixture.runtime_frees==1u);
}
static void check_only_failures(void) {
    for(unsigned at=1u;at<=21u;at++) {
        setup();fixture.read_fail=at;struct kui_app_status status;
        kui_disc_launcher_check(&status,log_line,cancelled,progress);
        CHECK(status.complete && !status.passed && status.errors==1u && !status.stopped);
        CHECK(fixture.raw_reads==at && !fixture.connections && !fixture.runtime_reads);
    }
    setup();fixture.stop=true;struct kui_app_status status;
    kui_disc_launcher_check(&status,log_line,cancelled,progress);
    CHECK(status.complete && !status.passed && status.stopped && !status.errors && !fixture.preparations);
    setup();kui_disc_launcher_check(&status,log_line,cancelled,NULL);
    CHECK(status.complete && status.passed && !fixture.progress && !fixture.connections);
}
static void every_optical_cut(void) {
    for(unsigned at=1u;at<=39u;at++) {
        setup();fixture.read_fail=at;refusal(false);released();
        CHECK(fixture.raw_reads==at && fixture.runtime_frees==1u);
        setup();fixture.edc_fail=at;refusal(false);released();
        CHECK(fixture.raw_reads==at && fixture.runtime_frees==1u);
        setup();fixture.address_fail=at;refusal(false);released();
        CHECK(fixture.raw_reads==at && fixture.runtime_frees==1u);
        setup();fixture.cancel_at=at;refusal(true);released();
        CHECK(fixture.raw_reads==at && fixture.runtime_frees==1u);
    }
    /* Valid EDC cannot authorize changed bytes between preflight and load. */
    for(unsigned at=22u;at<=39u;at++) {
        setup();fixture.change_at=at;refusal(false);released();
        CHECK(fixture.raw_reads==39u && fixture.runtime_frees==1u);
    }
}
int main(void) {
    successful_paths();preparation_failures();check_only_failures();every_optical_cut();
    printf("Physical GD launcher app: %u checks; actual optical planner/packet hashes, read cuts, changed bytes, bounded SD lifecycle and no VMU file access\n",checks);
    return 0;
}
