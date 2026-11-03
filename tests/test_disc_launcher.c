/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual app, optical boot planner and packet/hash admission. Only physical
 * drive, bootstrap file loading and mount lifecycle are supplied by this bus. */
#include "kui/disc_launcher.h"
#include "kui/disc_launch.h"
#include "kui/hash.h"
#include "platform.h"
#include <assert.h>
#include <stdarg.h>
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
    unsigned preparations,raw_reads,raw_calls,max_run,progress,logs,reallocations,callbacks,handoffs;
    unsigned read_fail,read_fatal,edc_fail,address_fail,cancel_at,change_at;
    bool fatal_sets_stop;
    unsigned cooked_reads,cooked_retry_at,cooked_fatal_at,cooked_cancel_at,cooked_change_at;
    unsigned unexpected_at;enum kui_read_result unexpected_result;
    uint32_t persistent_retry_fad,persistent_edc_fad,persistent_address_fad,late_payload_fad,late_boot_fad;
    uint32_t last_cooked_fad;
    bool cooked_retry_always,bulk_retry,bulk_refused,staging_exec;
    bool saw_single_reduction,saw_cooked_fallback,saw_boot_phase;
    struct {uint32_t fad,count;bool cooked;} io_trace[512];unsigned io_count;
    bool connected,mounted,stop,connect_fail,mount_fail,unmount_fail,prepare_fail,is_cd,handoff_fail;
    bool runtime_fail,template_invalid,realloc_fail,ce,vmu,callback_fail,callback_cancel,callback_mutate;
    bool callback_fill_refusal;
    bool saw_wait,saw_check,cancel_on_wait,cancel_after_ready_error;
    bool multitrack,saw_extent_refusal;
    enum kui_disc_launch_prepare_result launch_ready;
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
    fixture.toc[1].count=1u;fixture.toc[1].tracks[0]=(struct kui_track){3u,4u,FIRST,FIRST+512u};
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
static void log_line(const char *format,...) {
    char line[256];va_list args;va_start(args,format);vsnprintf(line,sizeof(line),format,args);va_end(args);
    if(strstr(line,"ISO extent refused LBA"))fixture.saw_extent_refusal=true;
    if(strstr(line,"reducing to single-sector PIO"))fixture.saw_single_reduction=true;
    if(strstr(line,"using repeated cooked sectors"))fixture.saw_cooked_fallback=true;
    if(strstr(line,"boot executable"))fixture.saw_boot_phase=true;
    ++fixture.logs;
}
static void progress(const struct kui_app_status *s) {
    fixture.last=*s;++fixture.progress;
    if(strstr(s->message,"boot executable staging"))fixture.staging_exec=true;
    if(strstr(s->message,"Waiting for the inserted disc")) {
        fixture.saw_wait=true;if(fixture.cancel_on_wait)fixture.stop=true;
    }
    if(strstr(s->message,"Checking the physical disc"))fixture.saw_check=true;
}
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
    /* Only the selected trusted bootstrap pathname is read by this layer.
     * Card image, assignment, candidate and journal lookups fail here. */
    CHECK(!strcmp(path,fixture.vmu?KUI_DISC_LAUNCH_VMU_PACKAGE:KUI_DISC_LAUNCH_PACKAGE));++fixture.runtime_reads;
    memset(out,0,sizeof(*out));if(fixture.runtime_fail)return KUI_RUNTIME_IO;
    uint32_t bytes=KUI_DISC_LAUNCH_STAGE_OFFSET+4u;
    out->data=calloc(1u,bytes);CHECK(out->data);uint8_t *h=(uint8_t *)out->data+KUI_DISC_LAUNCH_HEADER_OFFSET;
    memcpy(h,"KUIDISC1",8u);put(h+8u,1u);put(h+12u,KUI_DISC_LAUNCH_HEADER_BYTES);
    put(h+16u,KUI_DISC_LAUNCH_CONFIG_OFFSET);put(h+20u,KUI_DISC_LAUNCH_CONFIG_BYTES);
    put(h+24u,KUI_DISC_LAUNCH_STAGE_ADDRESS);put(h+28u,4u);
    put(h+32u,KUI_DISC_LAUNCH_STAGE_ADDRESS);put(h+36u,KUI_DISC_LAUNCH_EXEC_ADDRESS);
    put(h+40u,KUI_DISC_LAUNCH_EXEC_MAX_BYTES);put(h+44u,KUI_DISC_LAUNCH_STACK);
    put(h+48u,KUI_DISC_LAUNCH_STAGE_OFFSET);
    put(h+52u,fixture.vmu?KUI_DISC_LAUNCH_VMU_FLAGS:KUI_DISC_LAUNCH_FLAGS);
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
enum kui_disc_launch_prepare_result kui_disc_prepare_launch(struct kui_toc sessions[2],bool *is_cd,kui_cancel_fn cancel) {
    CHECK(cancel==cancelled && !fixture.saw_check);
    if(fixture.launch_ready!=KUI_DISC_LAUNCH_PREPARED) {
        ++fixture.preparations;
        if(fixture.launch_ready==KUI_DISC_LAUNCH_CANCELLED || fixture.cancel_after_ready_error)fixture.stop=true;
        return fixture.launch_ready;
    }
    bool result=kui_disc_prepare(sessions);*is_cd=fixture.is_cd;
    return result?KUI_DISC_LAUNCH_PREPARED:KUI_DISC_LAUNCH_IO_FAILED;
}
bool kui_disc_native_handoff(uint32_t fad,const uint8_t expected[2048]) {
    ++fixture.handoffs;
    CHECK(fad==FIRST && expected && !fixture.connected && !fixture.mounted);
    CHECK(!memcmp(expected,fixture.data[0],2048u));
    return !fixture.handoff_fail;
}
static bool staged_card(const char *product,const void *executable,uint32_t bytes,
    uint8_t profile[4096],char refusal[KUI_DISC_LAUNCHER_REFUSAL_BYTES],void *context,
    kui_log_fn log,kui_cancel_fn cancel) {
    CHECK(!fixture.connected && !fixture.mounted && fixture.raw_reads==39u && fixture.reallocations==1u);
    CHECK(!strcmp(product,"T-12345") && bytes==BOOT_BYTES && context==&fixture &&
        log==log_line && cancel==cancelled && !fixture.handoffs);
    CHECK(executable && !memcmp(executable,fixture.data[BOOT],BOOT_BYTES));
    for(unsigned i=0;i<4096u;i++)CHECK(!profile[i]);
    for(unsigned i=0;i<KUI_DISC_LAUNCHER_REFUSAL_BYTES;i++)CHECK(!refusal[i]);
    ++fixture.callbacks;
    if(fixture.callback_fill_refusal)memset(refusal,'R',KUI_DISC_LAUNCHER_REFUSAL_BYTES);
    else if(fixture.callback_fail)strcpy(refusal,"Virtual VMU unsupported: Maple driver. R: Save Log.");
    if(fixture.callback_cancel)fixture.stop=true;
    if(fixture.callback_mutate)((uint8_t *)(uintptr_t)executable)[100u]^=1u;
    memset(profile,0x5a,4096u);return !fixture.callback_fail;
}
static uint8_t bcd(unsigned n) {return (uint8_t)((n/10u)*16u+n%10u);}
static uint32_t sector_index(uint32_t fad,unsigned count) {
    uint32_t index=fad-FIRST;
    if(fixture.multitrack) {
        /* Sparse authored payloads in a real-shaped GD high-density TOC. */
        uint32_t payload=fixture.late_payload_fad?fixture.late_payload_fad:103462u;
        if(fixture.late_boot_fad && fad>=fixture.late_boot_fad && fad<fixture.late_boot_fad+2u)index=BOOT+fad-fixture.late_boot_fad;
        else if(fad>=payload && fad<549300u)index=ROOT+fad-payload;
        else CHECK(fad>=FIRST && fad<101871u && count<=101871u-fad);
    }
    CHECK(index<SECTORS && count<=SECTORS-index);return index;
}
static void trace_io(uint32_t fad,uint32_t count,bool cooked) {
    CHECK(fixture.io_count<sizeof(fixture.io_trace)/sizeof(fixture.io_trace[0]));
    fixture.io_trace[fixture.io_count].fad=fad;
    fixture.io_trace[fixture.io_count].count=count;
    fixture.io_trace[fixture.io_count++].cooked=cooked;
}
enum kui_read_result kui_disc_read_raw(void *context,uint32_t fad,unsigned count,uint8_t *out) {
    (void)context;CHECK(count && count<=8u && !fixture.connected && !fixture.mounted);
    uint32_t index=sector_index(fad,count);trace_io(fad,count,false);
    ++fixture.raw_calls;if(count>fixture.max_run)fixture.max_run=count;
    if(fixture.bulk_retry && count>1u) {
        fixture.bulk_refused=true;return KUI_READ_RETRY;
    }
    for(unsigned i=0;i<count;i++,fad++,out+=2352u) {
        ++fixture.raw_reads;
        if(fixture.cancel_at==fixture.raw_reads)fixture.stop=true;
        if(fixture.unexpected_at==fixture.raw_reads)return fixture.unexpected_result;
        if(fixture.read_fatal==fixture.raw_reads) {if(fixture.fatal_sets_stop)fixture.stop=true;return KUI_READ_FATAL;}
        if(fixture.read_fail==fixture.raw_reads || fixture.persistent_retry_fad==fad)return KUI_READ_RETRY;
        memset(out,0,2352u);memset(out+1u,255,10u);
        out[12]=bcd(fad/4500u);out[13]=bcd((fad/75u)%60u);out[14]=bcd(fad%75u);out[15]=1u;
        memcpy(out+16u,fixture.data[index+i],2048u);
        if(fixture.change_at==fixture.raw_reads)out[16u+100u]^=1u;
        if(fixture.address_fail==fixture.raw_reads || fixture.persistent_address_fad==fad)
            out[14]=bcd((fad+1u)%75u);
        put(out+2064u,kui_cd_edc(out,2064u));
        if(fixture.edc_fail==fixture.raw_reads || fixture.persistent_edc_fad==fad)out[16u+101u]^=1u;
    }
    return KUI_READ_OK;
}
enum kui_disc_boot_read_result kui_disc_read_boot_raw(void *context,uint32_t fad,unsigned count,uint8_t *out) {
    enum kui_read_result result=kui_disc_read_raw(context,fad,count,out);
    if(result==KUI_READ_OK)return fixture.stop?KUI_BOOT_READ_CANCELLED:KUI_BOOT_READ_OK;
    if(result==KUI_READ_RETRY)return KUI_BOOT_READ_RETRY;
    if(result==KUI_READ_FATAL)return KUI_BOOT_READ_FATAL;
    return (enum kui_disc_boot_read_result)99;
}
enum kui_disc_boot_read_result kui_disc_read_boot_cooked(void *context,uint32_t fad,uint8_t out[2048]) {
    (void)context;CHECK(!fixture.connected && !fixture.mounted);
    uint32_t index=sector_index(fad,1u);trace_io(fad,1u,true);
    ++fixture.cooked_reads;fixture.last_cooked_fad=fad;
    if(fixture.cooked_cancel_at==fixture.cooked_reads) {fixture.stop=true;return KUI_BOOT_READ_CANCELLED;}
    if(fixture.cooked_fatal_at==fixture.cooked_reads) {if(fixture.fatal_sets_stop)fixture.stop=true;return KUI_BOOT_READ_FATAL;}
    if(fixture.cooked_retry_always || fixture.cooked_retry_at==fixture.cooked_reads)return KUI_BOOT_READ_RETRY;
    memcpy(out,fixture.data[index],2048u);
    if(fixture.cooked_change_at==fixture.cooked_reads || (fixture.staging_exec && fixture.cooked_change_at==UINT32_MAX))out[100u]^=1u;
    return KUI_BOOT_READ_OK;
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
    CHECK(fixture.raw_calls==6u && fixture.max_run==8u);
    CHECK(fixture.saw_wait && fixture.saw_check);
    CHECK(!fixture.connections && !fixture.runtime_reads && !fixture.runtime_frees);
    setup();struct kui_runtime_image image={0};
    CHECK(kui_disc_launcher_prepare(&image,&status,log_line,cancelled,progress));
    CHECK(status.complete && status.passed && !status.stopped && !status.errors);
    CHECK(fixture.runtime_reads==1u && fixture.reallocations==1u && !fixture.runtime_frees);
    CHECK(fixture.raw_reads==39u && status.done==39u*2048u);released();
    CHECK(fixture.raw_calls==9u && fixture.max_run==8u);
    struct kui_disc_launch_plan plan;CHECK(kui_disc_launch_validate(image.data,image.info.payload_bytes,&plan));
    CHECK(plan.exec_bytes==BOOT_BYTES);
    CHECK(!memcmp((uint8_t *)image.data+plan.ip_offset,fixture.data,32768u));
    CHECK(!memcmp((uint8_t *)image.data+plan.exec_offset,fixture.data[BOOT],BOOT_BYTES));
    CHECK(image.info.crc32==kui_crc32(0u,image.data,image.info.payload_bytes));
    CHECK(image.info.payload_bytes==image.info.memory_bytes && fixture.last.complete);
    kui_runtime_free(&image);CHECK(fixture.runtime_frees==1u && !image.data);
}
static void region_paths(void) {
    for(unsigned gate=0;gate<2u;gate++) for(unsigned console=0;console<4u;console++) {
        setup();memset(fixture.data[0]+48u,' ',8u);fixture.data[0][48u]='E';
        struct kui_runtime_image image={0};struct kui_app_status status;
        struct kui_disc_launcher_region region={.region_free=gate!=0u,.console="JUE?"[console]};
        bool expected=region.region_free || region.console=='E';
        bool result=kui_disc_launcher_prepare_module(KUI_DISC_LAUNCH_PACKAGE,NULL,NULL,&region,
            &image,&status,log_line,cancelled,progress);
        CHECK(result==expected);released();
        CHECK(status.complete && status.passed==expected && !status.stopped);
        if(expected) {CHECK(image.data && fixture.reallocations==1u);kui_runtime_free(&image);}
        else {CHECK(!image.data && !fixture.reallocations && fixture.raw_reads==21u);
            CHECK(strstr(status.message,"Region-free") && status.errors==1u);}
    }
}
static void multitrack_setup(void) {
    setup();fixture.multitrack=true;fixture.toc[1].count=4u;
    fixture.toc[1].tracks[0]=(struct kui_track){3u,4u,45150u,102021u};
    fixture.toc[1].tracks[1]=(struct kui_track){4u,0u,102021u,102621u};
    fixture.toc[1].tracks[2]=(struct kui_track){5u,0u,102621u,103462u};
    fixture.toc[1].tracks[3]=(struct kui_track){6u,4u,103462u,549300u};
    dual32(fixture.data[16]+156u+2u,103462u-150u);
    dual32(fixture.data[ROOT]+2u,103462u-150u);
    dual32(fixture.data[ROOT]+34u+2u,103462u-150u);
    dual32(fixture.data[ROOT]+68u+2u,103470u-150u);
    memset(fixture.data[0]+48u,' ',8u);fixture.data[0][48u]='E';
}
static void separated_disc_paths(void) {
    multitrack_setup();struct kui_disc_boot_plan identity;struct kui_app_status status;
    CHECK(kui_disc_launcher_identity(&identity,&status,log_line,cancelled,progress));
    CHECK(identity.first_fad==45150u && identity.end_fad==549300u);
    CHECK(identity.metadata.root_lba==103312u && identity.boot_fad==103470u);
    CHECK(fixture.raw_reads==21u && !fixture.saw_extent_refusal);
    /* PAL metadata is admitted on NTSC with Region-free selected, and the
     * checked late-track executable survives the actual staging/handoff. */
    multitrack_setup();struct kui_runtime_image image={0};
    const struct kui_disc_launcher_region region={.region_free=true,.console='U'};
    CHECK(kui_disc_launcher_prepare_module(KUI_DISC_LAUNCH_PACKAGE,NULL,NULL,&region,
        &image,&status,log_line,cancelled,progress));released();
    struct kui_disc_launch_plan plan;CHECK(kui_disc_launch_validate(image.data,image.info.payload_bytes,&plan));
    CHECK(!memcmp((uint8_t *)image.data+plan.exec_offset,fixture.data[BOOT],BOOT_BYTES));
    CHECK(fixture.handoffs==1u && fixture.raw_reads==39u);kui_runtime_free(&image);
    /* Gap start/end, both audio tracks, and spans crossing DATA->gap are
     * rejected before the requested boot bytes can reach the optical bus. */
    const uint32_t bad[]={101870u,101871u,102021u,102621u,103461u,549299u,549300u};
    for(unsigned i=0u;i<sizeof(bad)/sizeof(bad[0]);i++) {
        multitrack_setup();dual32(fixture.data[ROOT]+68u+2u,bad[i]-150u);
        refusal(false);released();
        CHECK(fixture.raw_reads==3u && fixture.saw_extent_refusal && !fixture.handoffs);
    }
    /* TOC ordering/control corruption is rejected before the first read. */
    for(unsigned bad_toc=0u;bad_toc<4u;bad_toc++) {
        multitrack_setup();
        if(bad_toc==0u)fixture.toc[1].tracks[2].start--;
        if(bad_toc==1u)fixture.toc[1].tracks[2].control=7u;
        if(bad_toc==2u)fixture.toc[1].tracks[3].end=UINT32_MAX;
        if(bad_toc==3u)fixture.toc[1].tracks[3].number++;
        refusal(false);released();CHECK(!fixture.raw_reads);
    }
}
static void staged_callback_paths(void) {
    setup();fixture.vmu=true;struct kui_runtime_image image={0};struct kui_app_status status;
    CHECK(kui_disc_launcher_prepare_module(KUI_DISC_LAUNCH_VMU_PACKAGE,staged_card,&fixture,NULL,
        &image,&status,log_line,cancelled,progress));released();
    CHECK(fixture.callbacks==1u && fixture.handoffs==1u && status.passed);
    struct kui_disc_launch_plan plan;CHECK(kui_disc_launch_validate(image.data,image.info.payload_bytes,&plan));
    CHECK(plan.flags==KUI_DISC_LAUNCH_VMU_FLAGS && plan.vmu_profile_bytes==4096u);
    for(unsigned i=0;i<4096u;i++)CHECK(((uint8_t *)image.data)[plan.vmu_profile_offset+i]==0x5au);
    kui_runtime_free(&image);
    for(unsigned mode=0;mode<4u;mode++) {
        setup();fixture.vmu=true;fixture.callback_fail=mode<2u;
        fixture.callback_fill_refusal=mode==1u;fixture.callback_cancel=mode==2u;fixture.callback_mutate=mode==3u;
        CHECK(!kui_disc_launcher_prepare_module(KUI_DISC_LAUNCH_VMU_PACKAGE,staged_card,&fixture,NULL,
            &image,&status,log_line,cancelled,progress));released();
        CHECK(fixture.callbacks==1u && !fixture.handoffs && !image.data && fixture.runtime_frees==1u);
        CHECK(status.complete && !status.passed && status.stopped==(mode==2u) && status.errors==(mode==2u?0u:1u));
        if(mode==0u)CHECK(!strcmp(status.message,"Virtual VMU unsupported: Maple driver. R: Save Log."));
        if(mode==1u)CHECK(strlen(status.message)==KUI_DISC_LAUNCHER_REFUSAL_BYTES-1u && status.message[0]=='R');
        if(mode>=2u)CHECK(strstr(status.message,"boot bytes changed or handoff was stopped"));
    }
    /* No compatibility/card operation runs when staging itself fails. */
    setup();fixture.vmu=true;fixture.read_fatal=39u;
    CHECK(!kui_disc_launcher_prepare_module(KUI_DISC_LAUNCH_VMU_PACKAGE,staged_card,&fixture,NULL,
        &image,&status,log_line,cancelled,progress));released();
    CHECK(!fixture.callbacks && !fixture.handoffs && !image.data);
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
        setup();fixture.read_fatal=at;struct kui_app_status status;
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
static void readiness_failures(void) {
    static const struct {enum kui_disc_launch_prepare_result result;const char *message;} cases[]={
        {KUI_DISC_LAUNCH_OPEN,"lid is open"},{KUI_DISC_LAUNCH_NO_DISC,"No disc"},
        {KUI_DISC_LAUNCH_WAIT_TIMEOUT,"did not become ready"},{KUI_DISC_LAUNCH_CANCELLED,"stopped"},
        {KUI_DISC_LAUNCH_UNAVAILABLE,"Drive unavailable"},{KUI_DISC_LAUNCH_IO_FAILED,"initialization failed"}};
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        setup();fixture.launch_ready=cases[i].result;struct kui_app_status status;
        bool stopped=cases[i].result==KUI_DISC_LAUNCH_CANCELLED;
        refusal(stopped);released();
        CHECK(fixture.preparations==1u && !fixture.raw_calls && !fixture.callbacks && !fixture.handoffs);
        CHECK(fixture.saw_wait && !fixture.saw_check && strstr(fixture.last.message,cases[i].message));
        CHECK(strstr(fixture.last.lines[0],"spin-up"));
        setup();fixture.launch_ready=cases[i].result;struct kui_disc_boot_plan plan;
        memset(&plan,0xa5,sizeof(plan));
        CHECK(!kui_disc_launcher_identity(&plan,&status,log_line,cancelled,progress));
        CHECK(!fixture.connections && fixture.preparations==1u && !fixture.raw_reads);
        CHECK(status.stopped==stopped && status.errors==(stopped?0u:1u));
        CHECK(strstr(status.message,cases[i].message));
        for(size_t byte=0;byte<sizeof(plan);byte++) CHECK(!((const uint8_t *)&plan)[byte]);
    }
    setup();fixture.cancel_on_wait=true;refusal(true);released();
    CHECK(fixture.saw_wait && !fixture.preparations && !fixture.raw_reads);
    /* The adapter's terminal error wins over a later Stop sample. The
     * frontend must preserve its typed failure rather than showing success
     * at cancelling an operation which had already failed. */
    const enum kui_disc_launch_prepare_result errors[]={KUI_DISC_LAUNCH_IO_FAILED,
        KUI_DISC_LAUNCH_WAIT_TIMEOUT,KUI_DISC_LAUNCH_UNAVAILABLE};
    for(unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);i++) {
        setup();fixture.launch_ready=errors[i];fixture.cancel_after_ready_error=true;
        refusal(false);released();
        CHECK(fixture.stop && fixture.preparations==1u && !fixture.raw_reads && !fixture.handoffs);
    }
}
static void every_optical_cut(void) {
    static const unsigned ends[]={1u,2u,3u,11u,19u,21u,29u,37u,39u};
    for(unsigned at=1u;at<=39u;at++) {
        unsigned end=0u;
        for(unsigned i=0;i<sizeof(ends)/sizeof(ends[0]);i++)if(at<=ends[i]){end=ends[i];break;}
        CHECK(end);
        setup();fixture.read_fatal=at;refusal(false);released();
        CHECK(fixture.raw_reads==at && fixture.runtime_frees==1u && !fixture.cooked_reads);
        CHECK(strstr(fixture.last.lines[0],"Stage:") && strstr(fixture.last.lines[1],"Last read: FAD"));
        setup();fixture.cancel_at=at;refusal(true);released();
        CHECK(fixture.raw_reads==end && fixture.runtime_frees==1u);
        for(unsigned fault=0u;fault<3u;fault++) {
            setup();
            if(fault==0u)fixture.read_fail=at;
            if(fault==1u)fixture.edc_fail=at;
            if(fault==2u)fixture.address_fail=at;
            struct kui_runtime_image image={0};struct kui_app_status status;
            CHECK(kui_disc_launcher_prepare(&image,&status,log_line,cancelled,progress));released();
            CHECK(status.passed && fixture.handoffs==1u && fixture.saw_single_reduction && !fixture.cooked_reads);
            struct kui_disc_launch_plan plan;CHECK(kui_disc_launch_validate(image.data,image.info.payload_bytes,&plan));
            CHECK(!memcmp((uint8_t *)image.data+plan.exec_offset,fixture.data[BOOT],BOOT_BYTES));
            kui_runtime_free(&image);
        }
    }
    /* Valid EDC cannot authorize changed bytes between preflight and load. */
    for(unsigned at=22u;at<=39u;at++) {
        setup();fixture.change_at=at;refusal(false);released();
        CHECK(fixture.raw_reads==39u && fixture.runtime_frees==1u);
    }
}
static void read_recovery_paths(void) {
    struct kui_runtime_image image={0};struct kui_app_status status;
    setup();fixture.bulk_retry=true;
    CHECK(kui_disc_launcher_prepare(&image,&status,log_line,cancelled,progress));released();
    CHECK(fixture.bulk_refused && fixture.saw_single_reduction && !fixture.cooked_reads);
    bool reduced=false;
    for(unsigned i=0;i<fixture.io_count;i++) {
        if(fixture.io_trace[i].count>1u) {CHECK(!reduced);reduced=true;}
        else if(reduced)CHECK(fixture.io_trace[i].count==1u);
    }
    CHECK(reduced);kui_runtime_free(&image);
    for(unsigned fault=0u;fault<2u;fault++) {
        setup();
        if(fault)fixture.persistent_retry_fad=FIRST+1u;else fixture.persistent_edc_fad=FIRST+1u;
        CHECK(kui_disc_launcher_prepare(&image,&status,log_line,cancelled,progress));released();
        CHECK(fixture.cooked_reads && fixture.saw_cooked_fallback && fixture.handoffs==1u);
        bool cooked=false;
        for(unsigned i=0;i<fixture.io_count;i++) {
            if(fixture.io_trace[i].cooked)cooked=true;
            else CHECK(!cooked);
        }
        struct kui_disc_launch_plan plan;CHECK(kui_disc_launch_validate(image.data,image.info.payload_bytes,&plan));
        CHECK(!memcmp((uint8_t *)image.data+plan.exec_offset,fixture.data[BOOT],BOOT_BYTES));
        kui_runtime_free(&image);
    }
    /* A second bad address in the same raw run remains a hard refusal even
     * if an earlier EDC error selected the cooked fallback for its own bytes. */
    setup();fixture.persistent_edc_fad=FIRST+1u;fixture.persistent_address_fad=FIRST+2u;
    refusal(false);released();
    CHECK(fixture.cooked_reads==1u && fixture.last_cooked_fad==FIRST+1u && !fixture.handoffs);
    CHECK(strstr(fixture.last.message,"address mismatch") && strstr(fixture.last.lines[0],"IP.BIN verification"));
    CHECK(strstr(fixture.last.lines[1],"45152"));
    for(unsigned mode=0u;mode<4u;mode++) {
        setup();fixture.unexpected_at=1u;
        fixture.unexpected_result=mode==0u?KUI_READ_FATAL:mode==1u?KUI_READ_PARTIAL:(enum kui_read_result)(99u+mode);
        refusal(false);released();CHECK(fixture.raw_calls==1u && !fixture.cooked_reads && !fixture.handoffs);
    }
    setup();fixture.persistent_edc_fad=FIRST;fixture.cooked_retry_at=1u;
    CHECK(kui_disc_launcher_prepare(&image,&status,log_line,cancelled,progress));released();
    CHECK(fixture.cooked_reads && fixture.saw_cooked_fallback);kui_runtime_free(&image);
    setup();fixture.persistent_edc_fad=FIRST;fixture.cooked_retry_always=true;
    refusal(false);released();CHECK(fixture.cooked_reads==2u && !fixture.handoffs);
    CHECK(strstr(fixture.last.message,"retries exhausted") && strstr(fixture.last.lines[0],"IP header"));
    setup();fixture.persistent_edc_fad=FIRST;fixture.cooked_fatal_at=1u;
    refusal(false);released();CHECK(fixture.cooked_reads==1u && !fixture.handoffs);
    setup();fixture.persistent_edc_fad=FIRST;fixture.cooked_cancel_at=1u;
    refusal(true);released();CHECK(fixture.cooked_reads==1u && !fixture.handoffs);
    setup();fixture.read_fatal=1u;fixture.fatal_sets_stop=true;
    refusal(false);released();CHECK(fixture.stop && fixture.raw_calls==1u && !fixture.cooked_reads);
    setup();fixture.persistent_edc_fad=FIRST;fixture.cooked_fatal_at=1u;fixture.fatal_sets_stop=true;
    refusal(false);released();CHECK(fixture.stop && fixture.cooked_reads==1u && !fixture.handoffs);
    setup();fixture.persistent_edc_fad=FIRST;fixture.cooked_change_at=UINT32_MAX;
    refusal(false);released();CHECK(fixture.staging_exec && !fixture.handoffs);
    CHECK(strstr(fixture.last.message,"boot bytes changed"));
}
static void outer_rim_paths(void) {
    const uint32_t boots[]={549158u,549298u};
    for(unsigned test=0u;test<sizeof(boots)/sizeof(boots[0]);test++) {
        multitrack_setup();fixture.late_payload_fad=549150u;fixture.late_boot_fad=boots[test];
        dual32(fixture.data[16]+156u+2u,549150u-150u);
        dual32(fixture.data[ROOT]+2u,549150u-150u);
        dual32(fixture.data[ROOT]+34u+2u,549150u-150u);
        dual32(fixture.data[ROOT]+68u+2u,boots[test]-150u);
        struct kui_runtime_image image={0};struct kui_app_status status;
        const struct kui_disc_launcher_region region={.region_free=true,.console='U'};
        CHECK(kui_disc_launcher_prepare_module(KUI_DISC_LAUNCH_PACKAGE,NULL,NULL,&region,
            &image,&status,log_line,cancelled,progress));released();
        CHECK(fixture.handoffs==1u && fixture.saw_boot_phase);
        bool high=false,last=false;
        for(unsigned i=0;i<fixture.io_count;i++)if(fixture.io_trace[i].fad>=549150u) {
            high=true;CHECK(fixture.io_trace[i].count==1u);
            if(fixture.io_trace[i].fad==boots[test]+1u)last=true;
        }
        CHECK(high && last);struct kui_disc_launch_plan plan;
        CHECK(kui_disc_launch_validate(image.data,image.info.payload_bytes,&plan));
        CHECK(!memcmp((uint8_t *)image.data+plan.exec_offset,fixture.data[BOOT],BOOT_BYTES));
        kui_runtime_free(&image);
    }
}
int main(void) {
    successful_paths();region_paths();separated_disc_paths();staged_callback_paths();preparation_failures();check_only_failures();readiness_failures();every_optical_cut();read_recovery_paths();outer_rim_paths();
    printf("Physical GD launcher app: %u checks; actual planner/packet hashes, bounded raw/cooked recovery, high-FAD reads, read cuts, changed bytes and released storage\n",checks);
    return 0;
}
