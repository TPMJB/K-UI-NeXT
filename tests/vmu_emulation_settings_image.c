/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_emulation_settings.h"
#include "kui/media.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,writes;
static uint8_t *disk;static size_t disk_bytes;static FATFS fs;
static int write_fail_after=-1;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
static uint64_t blocks(void *context){(void)context;return disk_bytes/512u;}
static int read_blocks(void *context,uint32_t lba,size_t count,uint8_t *out) {
    (void)context;if((uint64_t)lba+count>blocks(NULL))return -1;
    memcpy(out,disk+(size_t)lba*512u,count*512u);return 0;
}
static int write_blocks(void *context,uint32_t lba,size_t count,const uint8_t *data) {
    (void)context;unsigned call=writes++;
    if(write_fail_after>=0 && call>=(unsigned)write_fail_after)return -1;
    if((uint64_t)lba+count>blocks(NULL))return -1;
    memcpy(disk+(size_t)lba*512u,data,count*512u);return 0;
}
static int sync_blocks(void *context){(void)context;return 0;}
static const struct kui_media_ops media={NULL,blocks,read_blocks,write_blocks,sync_blocks};
static void mount(void){kui_media_set(&media);CHECK(f_mount(&fs,"0:",1)==FR_OK);}
static void unmount(void){CHECK(f_mount(NULL,"0:",0)==FR_OK);}
static void file_read(const char *path,uint8_t *out,size_t bytes) {
    FIL f;UINT got=0;CHECK(f_open(&f,path,FA_READ)==FR_OK);
    CHECK(f_size(&f)==bytes && f_read(&f,out,(UINT)bytes,&got)==FR_OK && got==bytes);
    CHECK(f_close(&f)==FR_OK);
}
static void file_write(const char *path,const void *data,size_t bytes) {
    FIL f;UINT done=0;CHECK(f_open(&f,path,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
    CHECK(f_write(&f,data,(UINT)bytes,&done)==FR_OK && done==bytes && f_close(&f)==FR_OK);
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *input=fopen(argv[1],"rb");CHECK(input && !fseek(input,0,SEEK_END));
    long length=ftell(input);CHECK(length>0 && !fseek(input,0,SEEK_SET));disk_bytes=(size_t)length;
    disk=malloc(disk_bytes);CHECK(disk && fread(disk,1,disk_bytes,input)==disk_bytes && !fclose(input));
    mount();bool exfat=fs.fs_type==FS_EXFAT,enabled=false;
    CHECK(kui_vmu_emulation_settings_load(&enabled,NULL) && enabled);
    CHECK(kui_vmu_emulation_settings_save(true,NULL));
    CHECK(kui_vmu_emulation_settings_save(false,NULL));
    CHECK(kui_vmu_emulation_settings_save(true,NULL));
    uint8_t active[64],current[64];file_read(KUI_VMU_EMULATION_SETTINGS_PATH_A,active,sizeof(active));
    CHECK(f_mkdir("0:/KUI/vmu-test")==FR_OK);
    CHECK(f_mkdir("0:/KUI/vmu-test/p0005")==FR_OK);
    uint8_t sentinel[1024];for(unsigned i=0;i<sizeof(sentinel);i++)sentinel[i]=(uint8_t)(i*53u);
    file_write("0:/KUI/vmu-test/p0005/card.journal",sentinel,sizeof(sentinel));
    unmount();uint8_t *seed=malloc(disk_bytes);CHECK(seed);memcpy(seed,disk,disk_bytes);
    mount();writes=0;CHECK(kui_vmu_emulation_settings_save(false,NULL));unsigned complete_writes=writes;
    unmount();CHECK(complete_writes>0 && complete_writes<100u);
    for(unsigned cut=0;cut<=complete_writes;cut++) {
        memcpy(disk,seed,disk_bytes);write_fail_after=-1;mount();writes=0;write_fail_after=(int)cut;
        bool saved=kui_vmu_emulation_settings_save(false,NULL);
        write_fail_after=-1;unmount();mount();enabled=true;
        CHECK(kui_vmu_emulation_settings_load(&enabled,NULL));
        if(saved)CHECK(!enabled);
        file_read(KUI_VMU_EMULATION_SETTINGS_PATH_A,current,sizeof(current));
        CHECK(!memcmp(active,current,sizeof(active)));
        uint8_t journal[1024];file_read("0:/KUI/vmu-test/p0005/card.journal",journal,sizeof(journal));
        CHECK(!memcmp(journal,sentinel,sizeof(journal)));unmount();
    }
    memcpy(disk,seed,disk_bytes);mount();
    uint8_t damaged[64];file_read(KUI_VMU_EMULATION_SETTINGS_PATH_B,damaged,sizeof(damaged));damaged[32]^=1;
    file_write(KUI_VMU_EMULATION_SETTINGS_PATH_B,damaged,sizeof(damaged));
    enabled=false;CHECK(kui_vmu_emulation_settings_load(&enabled,NULL) && enabled);
    active[32]^=1;file_write(KUI_VMU_EMULATION_SETTINGS_PATH_A,active,sizeof(active));
    enabled=false;CHECK(!kui_vmu_emulation_settings_load(&enabled,NULL) && !enabled);
    CHECK(!kui_vmu_emulation_settings_save(false,NULL));unmount();
    printf("VMU emulation preference real filesystem (%s): %u checks, %u write boundaries passed\n",
        exfat?"exFAT":"FAT32",checks,complete_writes+1u);
    free(seed);free(disk);return 0;
}
