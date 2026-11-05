/* SPDX-License-Identifier: GPL-3.0-only */
/* This harness is compiled twice: against the frozen delivered engine2 C
 * implementation and against current C. It never treats old SH payload bytes
 * as though they execute the new implementation. SH instructions are not run. */
#include "kui/update.h"
#include "kui/update_boot.h"
#include "kui/update_software_file.h"
#include "kui/storage_policy.h"
#include "kui/game_launch_settings.h"
#include "kui/media.h"
#include "ff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FROZEN_ENGINE2
#define FROZEN_ENGINE2 0
#endif
#ifndef KUI_UPDATE_ENGINE_CURRENT
#define KUI_UPDATE_ENGINE_CURRENT 0u
#endif
#define ROOT_BUILD "111111111111"
#define GUI_BUILD "222222222222"
#define BANK_BUILD "444444444444"
#define GUI_BYTES 576u
#define HELPER_PATH "0:/KUI/apps/games/revision-helper.bin"
static unsigned checks,writes;
static uint8_t *disk;static size_t disk_bytes;static FATFS fs;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Engine revision failed %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint64_t blocks(void *unused) {(void)unused;return disk_bytes/512u;}
static int read_blocks(void *unused,uint32_t lba,size_t count,uint8_t *out) {
    (void)unused;if((uint64_t)lba+count>blocks(NULL))return -1;
    memcpy(out,disk+(size_t)lba*512u,count*512u);return 0;
}
static int write_blocks(void *unused,uint32_t lba,size_t count,const uint8_t *in) {
    (void)unused;if((uint64_t)lba+count>blocks(NULL))return -1;
    ++writes;memcpy(disk+(size_t)lba*512u,in,count*512u);return 0;
}
static int sync_blocks(void *unused) {(void)unused;return 0;}
static const struct kui_media_ops media={NULL,blocks,read_blocks,write_blocks,sync_blocks};
uint32_t kui_clock_fattime(void) {return ((2026u-1980u)<<25)|(11u<<21)|(5u<<16);}
static void logger(const char *format,...) {(void)format;}
static bool cancel(void) {return false;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(n>>(8u*i));}
static void digest(const void *data,size_t bytes,uint8_t out[32]) {
    struct kui_sha256 h;kui_sha256_init(&h);kui_sha256_update(&h,data,bytes);kui_sha256_digest(&h,out);
}
static void parents(const char *path) {
    char copy[192];CHECK(strlen(path)<sizeof(copy));strcpy(copy,path);
    for(char *p=copy+3u;*p;p++)if(*p=='/') {
        *p=0;FRESULT r=f_mkdir(copy);CHECK(r==FR_OK || r==FR_EXIST);*p='/';
    }
}
static void write_file(const char *path,const void *data,size_t bytes) {
    parents(path);FIL file;UINT done=0;
    CHECK(f_open(&file,path,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    CHECK(f_write(&file,data,(UINT)bytes,&done)==FR_OK && done==bytes);
    CHECK(f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}
static void gui_file(uint8_t file[GUI_BYTES],const char *build,unsigned engine) {
    memset(file,0,GUI_BYTES);uint8_t *body=file+64u;
    put32(body+16u,KUI_STORAGE_BOOT_MAGIC1);put32(body+20u,KUI_STORAGE_BOOT_MAGIC2);
    put32(body+24u,1u);put32(body+28u,KUI_STORAGE_AUTO);put32(body+32u,~(uint32_t)KUI_STORAGE_AUTO);
    static const uint8_t bank[16]=KUI_UPDATE_CAPABILITY_INITIALIZER;
    static const uint8_t handoff[KUI_UPDATE_HANDOFF_BYTES]=KUI_UPDATE_HANDOFF_INITIALIZER;
    memcpy(body+64u,bank,sizeof(bank));memcpy(body+80u,"KUIENG0!",8u);body[86u]=(uint8_t)('0'+engine);
    put32(body+88u,engine);put32(body+92u,~(uint32_t)engine);
    memcpy(body+128u,handoff,sizeof(handoff));
    memcpy(file,"KUIRUN1\0",8u);put32(file+8u,1u);put32(file+12u,64u);put32(file+16u,512u);
    put32(file+20u,KUI_RUNTIME_ADDRESS);put32(file+24u,KUI_RUNTIME_ADDRESS);put32(file+28u,512u);
    put32(file+32u,kui_crc32(0u,body,512u));memcpy(file+40u,build,12u);put32(file+60u,kui_crc32(0u,file,60u));
}
static void seed_bank(unsigned engine,unsigned flags) {
    uint8_t gui[GUI_BYTES];gui_file(gui,GUI_BUILD,engine);
    struct kui_update_package *p=calloc(1,sizeof(*p));CHECK(p);
    strcpy(p->build,BANK_BUILD);p->version=2u;p->flags=(uint8_t)flags;p->count=2u;
    struct kui_update_entry *entry=p->entries;
    strcpy(entry[0].path,"runtime.kui");strcpy(entry[0].build,GUI_BUILD);
    entry[0].policy=KUI_UPDATE_SELF_KUI;entry[0].bytes=sizeof(gui);digest(gui,sizeof(gui),entry[0].sha256);
    static const char helper[]="fixed helper";
    strcpy(entry[1].path,"apps/games/revision-helper.bin");entry[1].policy=KUI_UPDATE_RAW;
    entry[1].bytes=sizeof(helper);digest(helper,sizeof(helper),entry[1].sha256);
    CHECK(kui_update_package_seal(p)==KUI_UPDATE_OK);
    char path[192];CHECK(kui_update_bank_path(p->build,"runtime.kui",path,sizeof(path)));write_file(path,gui,sizeof(gui));
    CHECK(kui_update_bank_path(p->build,entry[1].path,path,sizeof(path)));write_file(path,helper,sizeof(helper));
    uint8_t receipt[128u+2u*160u];size_t used=0;
    CHECK(kui_update_receipt_encode(p,receipt,sizeof(receipt),&used)==KUI_UPDATE_OK && used==sizeof(receipt));
    snprintf(path,sizeof(path),KUI_UPDATE_SYSTEM "/%s/package.bin",p->build);write_file(path,receipt,used);
    struct kui_update_activation activation={.sequence=1u};strcpy(activation.selected,p->build);
    memcpy(activation.manifest_sha256,p->manifest_sha256,32u);
    uint8_t wire[128];CHECK(kui_update_activation_encode(&activation,wire));write_file(KUI_UPDATE_ACTIVE0,wire,sizeof(wire));
    free(p);
}
static enum kui_update_boot_result enter_bank(unsigned floor) {
    kui_update_boot_reset();kui_update_boot_require_engine(floor);
    struct kui_runtime_image image={0};struct kui_update_boot_view view;
    enum kui_update_boot_result result=kui_update_boot_read(ROOT_BUILD,KUI_STORAGE_SCI,&image,&view,logger,cancel);
    if(result==KUI_UPDATE_BOOT_CHAIN) {
        CHECK(image.data && !strcmp(image.info.build,GUI_BUILD));
        struct kui_update_handoff_context context={.payload_offset=128u};
        memcpy(context.wire,(uint8_t *)image.data+128u,sizeof(context.wire));kui_runtime_free(&image);
        kui_update_boot_reset();kui_update_boot_require_engine(floor);
        result=kui_update_boot_read_handoff(GUI_BUILD,KUI_STORAGE_SCI,&context,&image,&view,logger,cancel);
        CHECK(result==KUI_UPDATE_BOOT_PINNED && !image.data && !strcmp(view.bank,BANK_BUILD));
    } else CHECK(result==KUI_UPDATE_BOOT_BASELINE && !image.data && !view.bank[0]);
    return result;
}
static void saved_settings(bool expect_found) {
    struct kui_game_launch_settings settings;bool found=false;
    CHECK(kui_game_launch_settings_lookup("0:/Games/test/disc.gdi",&settings,&found,logger));
    CHECK(found==expect_found);
    if(found)CHECK(settings.reader==KUI_GAME_READER_BACKGROUND && settings.vmu_slot==KUI_GAME_VMU_SLOT_A1);
}
static void revision_contract(void) {
#if !FROZEN_ENGINE2
    struct kui_update_package package={0};
    CHECK(kui_update_engine_minimum(&package)==0u);
    package.version=2u;CHECK(kui_update_engine_minimum(&package)==2u);
    package.flags=KUI_UPDATE_FULL;CHECK(kui_update_engine_minimum(&package)==2u);
    package.flags=KUI_UPDATE_REQUIRES_ENGINE3;CHECK(kui_update_engine_minimum(&package)==3u);
    package.flags|=KUI_UPDATE_FULL;CHECK(kui_update_engine_minimum(&package)==3u);
    uint8_t old[GUI_BYTES],current[GUI_BYTES],mixed[64]={0};gui_file(old,GUI_BUILD,2u);gui_file(current,GUI_BUILD,3u);
    CHECK(kui_update_engine_level(old+64u,512u)==2u && kui_update_engine_level(current+64u,512u)==3u);
    memcpy(mixed,old+64u+80u,16u);memcpy(mixed+16u,current+64u+80u,16u);
    CHECK(kui_update_engine_level(mixed,sizeof(mixed))==0u);
    memcpy(mixed,current+64u+80u,16u);CHECK(kui_update_engine_level(mixed,sizeof(mixed))==0u);
    memset(mixed+16u,0,16u);CHECK(kui_update_engine_level(mixed,sizeof(mixed))==3u);
    mixed[15u]^=1u;CHECK(kui_update_engine_level(mixed,sizeof(mixed))==0u);
    CHECK(kui_update_engine_level(current+64u+80u,15u)==0u);
#endif
}
static void frozen_settings_failure(void) {
    FILINFO info;CHECK(kui_update_physical_stat(KUI_GAME_LAUNCH_SETTINGS_PATH_A,&info)==FR_OK);
    CHECK(f_stat(KUI_GAME_LAUNCH_SETTINGS_PATH_A,&info)==FR_NO_FILE);saved_settings(false);
    struct kui_game_launch_settings settings;kui_game_launch_settings_default(&settings);settings.reader=KUI_GAME_READER_EAGER;
    CHECK(!kui_game_launch_settings_save("0:/Games/test/disc.gdi",&settings,logger));
}
static void transfer_context(const char *path,struct kui_update_handoff_context *context,bool output) {
    FILE *file=fopen(path,output?"wb":"rb");CHECK(file);
    CHECK((output?fwrite(context,1u,sizeof(*context),file):fread(context,1u,sizeof(*context),file))==sizeof(*context));
    CHECK(!fclose(file));
}
int main(int argc,char **argv) {
    CHECK(argc==3 || argc==4);FILE *input=fopen(argv[1],"rb");CHECK(input && !fseek(input,0,SEEK_END));long length=ftell(input);
    CHECK(length>0 && !fseek(input,0,SEEK_SET));disk_bytes=(size_t)length;disk=malloc(disk_bytes);
    CHECK(disk && fread(disk,1u,disk_bytes,input)==disk_bytes && !fclose(input));
    kui_media_set(&media);CHECK(f_mount(&fs,"0:",1)==FR_OK);
    revision_contract();
    if(!strcmp(argv[2],"bridge-recipient")) {
        CHECK(FROZEN_ENGINE2 && argc==4);struct kui_update_handoff_context context;transfer_context(argv[3],&context,false);
        kui_update_boot_require_engine(2u);struct kui_runtime_image image={0};struct kui_update_boot_view view;
        CHECK(kui_update_boot_read_handoff(GUI_BUILD,KUI_STORAGE_SCI,&context,&image,&view,logger,cancel)==KUI_UPDATE_BOOT_PINNED);
        CHECK(!image.data && !strcmp(view.bank,BANK_BUILD));frozen_settings_failure();
        puts("PASS separate frozen GUI2 recipient adopts current dispatcher's serialized handoff and executes its old broken settings wrapper");
        goto done;
    }
    unsigned level=FROZEN_ENGINE2?2u:3u;
    uint8_t root[GUI_BYTES];gui_file(root,ROOT_BUILD,level);write_file(KUI_RUNTIME_PATH,root,sizeof(root));
    struct kui_game_launch_settings settings;kui_game_launch_settings_default(&settings);
    settings.reader=KUI_GAME_READER_BACKGROUND;settings.vmu_slot=KUI_GAME_VMU_SLOT_A1;
    CHECK(kui_game_launch_settings_save("0:/Games/test/disc.gdi",&settings,logger));saved_settings(true);
    if(!strcmp(argv[2],"bridge-dispatch")) {
        /* Explicitly reproduce the old floor2 dispatch policy using today's
         * dispatcher. The next host process is the actual frozen recipient. */
        CHECK(!FROZEN_ENGINE2 && argc==4);seed_bank(2u,1u);kui_update_boot_require_engine(2u);
        struct kui_runtime_image image={0};struct kui_update_boot_view view;
        CHECK(kui_update_boot_read(ROOT_BUILD,KUI_STORAGE_SCI,&image,&view,logger,cancel)==KUI_UPDATE_BOOT_CHAIN);
        CHECK(image.data && !strcmp(image.info.build,GUI_BUILD));
        struct kui_update_handoff_context context={.payload_offset=128u};
        memcpy(context.wire,(uint8_t *)image.data+128u,sizeof(context.wire));transfer_context(argv[3],&context,true);
        kui_runtime_free(&image);puts("PASS current C under old floor2 policy dispatches checked GUI2 to a separate frozen recipient process");
        goto done;
    }
    if(!strcmp(argv[2],"frozen-defect")) {
        CHECK(FROZEN_ENGINE2);seed_bank(2u,1u);CHECK(enter_bank(2u)==KUI_UPDATE_BOOT_PINNED);
        /* These are the frozen delivered engine2 wrapper and real settings
         * caller. The existing file vanishes, and verification of a new save
         * fails after its physical write. */
        frozen_settings_failure();
        /* The old decoder refuses bit1 before any installer can write. */
        struct kui_update_package p={0};uint8_t header[128]={0};
        memcpy(header,"KUIUPD2\0",8u);put32(header+8u,2u);put32(header+12u,128u);
        put32(header+16u,1u);put32(header+20u,160u);memcpy(header+32u,BANK_BUILD,12u);header[45u]=2u;
        put32(header+124u,kui_crc32(0u,header,124u));unsigned before=writes;
        CHECK(kui_update_header_decode(header,288u,&p)==KUI_UPDATE_FORMAT && writes==before);
        puts("PASS frozen engine2 actual C: FULL bank hides real saved launch settings, save verification fails; new engine3 flag refused before writes");
    } else {
        CHECK(!FROZEN_ENGINE2 && !strcmp(argv[2],"current-contract"));
        CHECK(KUI_UPDATE_ENGINE_CURRENT==3u);seed_bank(2u,1u);
        CHECK(enter_bank(3u)==KUI_UPDATE_BOOT_BASELINE);saved_settings(true);
        /* A new receipt bit cannot turn old GUI code into engine3. */
        kui_update_boot_reset();seed_bank(2u,3u);CHECK(enter_bank(3u)==KUI_UPDATE_BOOT_BASELINE);saved_settings(true);
        /* A GUI3 with an old receipt must also fall back, not chain forever. */
        kui_update_boot_reset();seed_bank(3u,1u);CHECK(enter_bank(3u)==KUI_UPDATE_BOOT_BASELINE);saved_settings(true);
        kui_update_boot_reset();seed_bank(3u,3u);CHECK(enter_bank(3u)==KUI_UPDATE_BOOT_PINNED);saved_settings(true);
        settings.cdda=true;CHECK(kui_game_launch_settings_save("0:/Games/test/disc.gdi",&settings,logger));
        struct kui_game_launch_settings loaded;bool found=false;
        CHECK(kui_game_launch_settings_lookup("0:/Games/test/disc.gdi",&loaded,&found,logger) && found && loaded.cdda);
        FILINFO info;CHECK(f_stat(KUI_GAME_LAUNCH_SETTINGS_PATH_A,&info)==FR_OK);
        FIL helper;CHECK(f_open(&helper,HELPER_PATH,FA_READ)==FR_OK && f_size(&helper)==13u);CHECK(f_close(&helper)==FR_OK);
        puts("PASS engine3 actual C: old GUI2 and old-receipt GUI3 cannot chain; GUI3 pair reads/writes real settings and checked helper after handoff");
    }
done:
    kui_update_boot_reset();CHECK(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);
    if(!strcmp(argv[2],"bridge-dispatch")) {
        FILE *output=fopen(argv[1],"wb");CHECK(output && fwrite(disk,1u,disk_bytes,output)==disk_bytes && !fclose(output));
    }
    free(disk);
    printf("PASS engine revision: %u checks\n",checks);return 0;
}
