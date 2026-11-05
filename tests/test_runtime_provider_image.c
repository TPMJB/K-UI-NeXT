/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the optional embedded-module boundary with the production loader,
 * bank resolver and real FatFs. The provider returns authored fixture bytes;
 * its compression codec remains the existing GUI provider's responsibility. */
#include "update_fixture.h"
#include "kui/update_boot.h"
#include "kui/storage_policy.h"
#include "kui/media.h"
#include <stdio.h>

#define MODULE "0:/KUI/apps/games/test-module.kui"
#define UNBUNDLED "0:/KUI/apps/disc/unbundled.kui"
#define MISSING "0:/KUI/apps/disc/missing.kui"
#define GUI_BUILD "111111111111"
#define EMBEDDED_BUILD "aaaaaaaaaaaa"
#define ROOT_BUILD "bbbbbbbbbbbb"
#define BANK_BUILD "cccccccccccc"
static uint8_t *disk;static size_t disk_bytes;static FATFS fs;static unsigned checks;
static uint8_t embedded[UF_FILE_BYTES],root_module[UF_FILE_BYTES],bank_module[UF_FILE_BYTES];
static unsigned opens,provider_calls,polls,cancel_after;
static char opened_path[192];static bool unhandled;static enum kui_runtime_result provider_result;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Runtime-provider check failed %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint64_t blocks(void *unused) {(void)unused;return disk_bytes/512u;}
static int read_blocks(void *unused,uint32_t lba,size_t count,uint8_t *out) {
    (void)unused;if((uint64_t)lba+count>blocks(NULL))return -1;
    memcpy(out,disk+(size_t)lba*512u,count*512u);return 0;
}
static int write_blocks(void *unused,uint32_t lba,size_t count,const uint8_t *in) {
    (void)unused;if((uint64_t)lba+count>blocks(NULL))return -1;
    memcpy(disk+(size_t)lba*512u,in,count*512u);return 0;
}
static int sync_blocks(void *unused) {(void)unused;return 0;}
static const struct kui_media_ops media={NULL,blocks,read_blocks,write_blocks,sync_blocks};
uint32_t kui_clock_fattime(void) {return ((2026u-1980u)<<25)|(11u<<21)|(5u<<16);}
static void logger(const char *format,...) {(void)format;}
static bool cancel(void) {return cancel_after && ++polls>=cancel_after;}
static void reset_observation(void) {
    opens=provider_calls=polls=cancel_after=0u;opened_path[0]=0;
    unhandled=false;provider_result=KUI_RUNTIME_OK;
}
FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __wrap_f_open(FIL *file,const TCHAR *path,BYTE mode) {
    ++opens;CHECK(strlen(path)<sizeof(opened_path));strcpy(opened_path,path);
    return __real_f_open(file,path,mode);
}
#if KUI_TEST_MODULE_PROVIDER
enum kui_runtime_result kui_runtime_module_bundle_read(const char *path,
    struct kui_runtime_image *out,kui_cancel_fn cancelled,bool *handled) {
    ++provider_calls;CHECK(out && cancelled && handled);
    *out=(struct kui_runtime_image){0};*handled=false;
    if(unhandled || strcmp(path,MODULE))return KUI_RUNTIME_CHECKSUM;
    *handled=true;
    if(provider_result!=KUI_RUNTIME_OK)return provider_result;
    if(cancelled())return KUI_RUNTIME_CANCELLED;
    CHECK(kui_runtime_header(embedded,sizeof(embedded),&out->info)==KUI_RUNTIME_OK);
    CHECK(kui_crc32(0,embedded+64u,UF_PAYLOAD_BYTES)==out->info.crc32);
    out->data=malloc(UF_PAYLOAD_BYTES);CHECK(out->data);
    memcpy(out->data,embedded+64u,UF_PAYLOAD_BYTES);
    if(cancelled()) {kui_runtime_free(out);return KUI_RUNTIME_CANCELLED;}
    return KUI_RUNTIME_OK;
}
#endif
static void parents(const char *path) {
    char copy[192];CHECK(strlen(path)<sizeof(copy));strcpy(copy,path);
    for(char *p=copy+3u;*p;p++)if(*p=='/') {
        *p=0;FRESULT result=f_mkdir(copy);CHECK(result==FR_OK || result==FR_EXIST);*p='/';
    }
}
static void write_file(const char *path,const void *bytes,size_t size) {
    parents(path);FIL file;UINT done=0u;
    CHECK(f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    CHECK(f_write(&file,bytes,(UINT)size,&done)==FR_OK && done==size && f_close(&file)==FR_OK);
}
static void module(uint8_t out[UF_FILE_BYTES],const char *build,unsigned value) {
    struct uf_package package=uf_build(build,1u);memcpy(out,uf_file(&package,1u),UF_FILE_BYTES);uf_free(&package);
    memset(out+64u,(int)value,UF_PAYLOAD_BYTES);
    uf_put32(out+32u,kui_crc32(0,out+64u,UF_PAYLOAD_BYTES));uf_put32(out+60u,kui_crc32(0,out,60u));
}
static void gui_runtime(uint8_t out[UF_FILE_BYTES]) {
    struct uf_package package=uf_build(GUI_BUILD,1u);memcpy(out,uf_file(&package,0u),UF_FILE_BYTES);uf_free(&package);
    static const uint8_t engine[16]=KUI_UPDATE_ENGINE_INITIALIZER;
    const uint32_t transport[]={KUI_STORAGE_BOOT_MAGIC1,KUI_STORAGE_BOOT_MAGIC2,1u,KUI_STORAGE_AUTO,~(uint32_t)KUI_STORAGE_AUTO};
    memcpy(out+64u+48u,engine,sizeof(engine));
    for(unsigned i=0u;i<5u;i++)uf_put32(out+64u+80u+4u*i,transport[i]);
    uf_put32(out+32u,kui_crc32(0,out+64u,UF_PAYLOAD_BYTES));uf_put32(out+60u,kui_crc32(0,out,60u));
}
static void activate(const char *bank,bool replace,bool full,const uint8_t gui[UF_FILE_BYTES]) {
    struct kui_update_package *package=calloc(1,sizeof(*package));CHECK(package);
    strcpy(package->build,bank);package->version=2u;package->channel=1u;
    package->flags=KUI_UPDATE_REQUIRES_ENGINE3|(full?KUI_UPDATE_FULL:0u);package->count=replace?2u:1u;
    strcpy(package->entries[0].path,"runtime.kui");strcpy(package->entries[0].build,GUI_BUILD);
    package->entries[0].policy=KUI_UPDATE_SELF_KUI;package->entries[0].bytes=UF_FILE_BYTES;
    uf_hash(gui,UF_FILE_BYTES,package->entries[0].sha256);
    if(replace) {
        strcpy(package->entries[1].path,"apps/games/test-module.kui");strcpy(package->entries[1].build,BANK_BUILD);
        package->entries[1].policy=KUI_UPDATE_SELF_KUI;package->entries[1].bytes=UF_FILE_BYTES;
        uf_hash(bank_module,UF_FILE_BYTES,package->entries[1].sha256);
    }
    CHECK(kui_update_package_seal(package)==KUI_UPDATE_OK);char path[192];
    CHECK(kui_update_bank_path(bank,"runtime.kui",path,sizeof(path)));write_file(path,gui,UF_FILE_BYTES);
    if(replace) {CHECK(kui_update_bank_path(bank,"apps/games/test-module.kui",path,sizeof(path)));write_file(path,bank_module,UF_FILE_BYTES);}
    uint8_t *receipt=malloc(KUI_UPDATE_RECEIPT_MAX);CHECK(receipt);size_t written=0u;
    CHECK(kui_update_receipt_encode(package,receipt,KUI_UPDATE_RECEIPT_MAX,&written)==KUI_UPDATE_OK);
    CHECK(snprintf(path,sizeof(path),KUI_UPDATE_SYSTEM "/%s/package.bin",bank)<(int)sizeof(path));write_file(path,receipt,written);free(receipt);
    static uint64_t sequence;struct kui_update_activation selection={.sequence=++sequence};strcpy(selection.selected,bank);
    memcpy(selection.manifest_sha256,package->manifest_sha256,32u);free(package);
    uint8_t wire[128];CHECK(kui_update_activation_encode(&selection,wire));write_file(KUI_UPDATE_ACTIVE0,wire,sizeof(wire));
    struct kui_runtime_image image;struct kui_update_boot_view view;reset_observation();
    CHECK(kui_update_boot_read(GUI_BUILD,KUI_STORAGE_SCI,&image,&view,logger,cancel)==KUI_UPDATE_BOOT_CHAIN);
    struct kui_update_handoff_context context={.payload_offset=128u};memcpy(context.wire,(uint8_t *)image.data+128u,sizeof(context.wire));
    kui_runtime_free(&image);
    CHECK(kui_update_boot_read_handoff(GUI_BUILD,KUI_STORAGE_SCI,&context,&image,&view,logger,cancel)==KUI_UPDATE_BOOT_PINNED);
    CHECK(!image.data && !strcmp(view.bank,bank));reset_observation();
}
static void expect_module(const char *path,const uint8_t expected[UF_FILE_BYTES],unsigned expected_opens,unsigned expected_calls) {
    struct kui_runtime_image image;
    CHECK(kui_runtime_read(path,&image,logger,cancel)==KUI_RUNTIME_OK);
    CHECK(image.data && !memcmp(image.data,expected+64u,UF_PAYLOAD_BYTES));
    CHECK(!memcmp(image.info.build,expected+40u,12u) && image.info.payload_bytes==UF_PAYLOAD_BYTES);
    CHECK(opens==expected_opens && provider_calls==expected_calls);kui_runtime_free(&image);
}
static void empty_image(const struct kui_runtime_image *image) {
    CHECK(!image->data && !image->info.payload_bytes && !image->info.memory_bytes && !image->info.build[0]);
}
static void root_reads(void) {
    reset_observation();
#if KUI_TEST_MODULE_PROVIDER
    expect_module(MODULE,embedded,0u,1u);
    const enum kui_runtime_result errors[]={KUI_RUNTIME_HEADER,KUI_RUNTIME_CHECKSUM,KUI_RUNTIME_IO,KUI_RUNTIME_MEMORY,KUI_RUNTIME_CANCELLED};
    struct kui_runtime_image image;
    for(unsigned i=0u;i<sizeof(errors)/sizeof(errors[0]);i++) {
        reset_observation();provider_result=errors[i];
        CHECK(kui_runtime_read(MODULE,&image,logger,cancel)==errors[i]);empty_image(&image);
        CHECK(provider_calls==1u && !opens);
    }
    for(unsigned after=2u;after<=3u;after++) {
        reset_observation();cancel_after=after;
        CHECK(kui_runtime_read(MODULE,&image,logger,cancel)==KUI_RUNTIME_CANCELLED);empty_image(&image);
        CHECK(provider_calls==1u && !opens);
    }
    reset_observation();unhandled=true;
    expect_module(MODULE,root_module,1u,1u);CHECK(!strcmp(opened_path,MODULE));
    reset_observation();expect_module(UNBUNDLED,root_module,1u,1u);
#else
    expect_module(MODULE,root_module,1u,0u);CHECK(!strcmp(opened_path,MODULE));
    reset_observation();expect_module(UNBUNDLED,root_module,1u,0u);
#endif
    reset_observation();struct kui_runtime_image cancelled_image;cancel_after=1u;
    CHECK(kui_runtime_read(MODULE,&cancelled_image,logger,cancel)==KUI_RUNTIME_CANCELLED);empty_image(&cancelled_image);
    CHECK(!provider_calls && !opens);
    reset_observation();struct kui_runtime_image missing;
    CHECK(kui_runtime_read(MISSING,&missing,logger,cancel)==KUI_RUNTIME_IO);empty_image(&missing);
    CHECK(opens==1u && provider_calls==KUI_TEST_MODULE_PROVIDER);
}
static void bank_reads(const uint8_t gui[UF_FILE_BYTES]) {
    activate("444444444444",false,false,gui);
#if KUI_TEST_MODULE_PROVIDER
    expect_module(MODULE,embedded,0u,1u);
#else
    expect_module(MODULE,root_module,1u,0u);
#endif
    activate("555555555555",true,false,gui);provider_result=KUI_RUNTIME_CHECKSUM;
    expect_module(MODULE,bank_module,1u,0u);CHECK(strstr(opened_path,"/555555555555/")!=NULL);
    char physical[192];CHECK(kui_update_bank_path("555555555555","apps/games/test-module.kui",physical,sizeof(physical)));
    uint8_t changed[UF_FILE_BYTES];memcpy(changed,bank_module,sizeof(changed));changed[UF_FILE_BYTES-1u]^=1u;write_file(physical,changed,sizeof(changed));
    reset_observation();provider_result=KUI_RUNTIME_OK;struct kui_runtime_image image;
    CHECK(kui_runtime_read(MODULE,&image,logger,cancel)==KUI_RUNTIME_CHECKSUM);empty_image(&image);
    CHECK(opens==1u && !provider_calls);
    CHECK(f_unlink(physical)==FR_OK);reset_observation();
    CHECK(kui_runtime_read(MODULE,&image,logger,cancel)==KUI_RUNTIME_IO);empty_image(&image);
    CHECK(opens==1u && !provider_calls);
    activate("666666666666",false,true,gui);
    CHECK(kui_update_runtime_absent(MODULE));
    CHECK(kui_runtime_read(MODULE,&image,logger,cancel)==KUI_RUNTIME_IO);empty_image(&image);
    CHECK(!opens && !provider_calls);
    kui_update_boot_block();reset_observation();
    CHECK(kui_runtime_read(MODULE,&image,logger,cancel)==KUI_RUNTIME_IO);empty_image(&image);
    CHECK(!opens && !provider_calls);
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *host=fopen(argv[1],"rb");CHECK(host && !fseek(host,0,SEEK_END));
    long length=ftell(host);CHECK(length>0 && !fseek(host,0,SEEK_SET));disk_bytes=(size_t)length;
    disk=malloc(disk_bytes);CHECK(disk && fread(disk,1,disk_bytes,host)==disk_bytes && !fclose(host));
    kui_media_set(&media);CHECK(f_mount(&fs,"0:",1)==FR_OK);
    uint8_t gui[UF_FILE_BYTES];gui_runtime(gui);module(embedded,EMBEDDED_BUILD,0x11u);
    module(root_module,ROOT_BUILD,0x22u);module(bank_module,BANK_BUILD,0x33u);
    write_file(KUI_RUNTIME_PATH,gui,sizeof(gui));write_file(MODULE,root_module,sizeof(root_module));write_file(UNBUNDLED,root_module,sizeof(root_module));
    root_reads();kui_update_boot_require_engine(KUI_UPDATE_ENGINE_CURRENT);bank_reads(gui);
    kui_update_boot_reset();root_reads();
    CHECK(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);free(disk);
    printf("PASS %u runtime provider checks (%s): ROOT/inheritance, authoritative errors/cancel, disk fallback, pinned-bank priority\n",
        checks,KUI_TEST_MODULE_PROVIDER?"present":"absent");return 0;
}
