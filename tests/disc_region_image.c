/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_region.h"
#include "kui/media.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,writes,sessions;
static uint8_t *disk;static size_t disk_bytes;static FATFS fs;
static int write_fail_after=-1;
static bool connected;
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
bool kui_sd_connect(void) {CHECK(!connected);connected=true;++sessions;kui_media_set(&media);return true;}
void kui_sd_disconnect(void) {CHECK(connected);connected=false;}
bool kui_mount(FATFS *mounted,kui_log_fn log) {
    CHECK(connected && log);return f_mount(mounted,"0:",1)==FR_OK;
}
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
static bool load(bool *enabled) {
    bool result=kui_disc_region_load(enabled,NULL,NULL);CHECK(!connected);return result;
}
static bool save(bool enabled) {
    bool result=kui_disc_region_save(enabled,NULL,NULL);CHECK(!connected);return result;
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *input=fopen(argv[1],"rb");CHECK(input && !fseek(input,0,SEEK_END));
    long length=ftell(input);CHECK(length>0 && !fseek(input,0,SEEK_SET));disk_bytes=(size_t)length;
    disk=malloc(disk_bytes);CHECK(disk && fread(disk,1,disk_bytes,input)==disk_bytes && !fclose(input));
    bool enabled=false;CHECK(load(&enabled) && enabled);
    CHECK(save(true));CHECK(save(false));CHECK(save(true));
    mount();bool exfat=fs.fs_type==FS_EXFAT;
    uint8_t active[64],current[64];file_read(KUI_DISC_REGION_PATH_A,active,sizeof(active));
    CHECK(f_mkdir("0:/KUI/vmu-test")==FR_OK);
    CHECK(f_mkdir("0:/KUI/vmu-test/p0005")==FR_OK);
    uint8_t sentinel[1024];for(unsigned i=0;i<sizeof(sentinel);i++)sentinel[i]=(uint8_t)(i*53u);
    file_write("0:/KUI/vmu-test/p0005/card.journal",sentinel,sizeof(sentinel));
    unmount();uint8_t *seed=malloc(disk_bytes);CHECK(seed);memcpy(seed,disk,disk_bytes);
    writes=0;CHECK(save(false));unsigned complete_writes=writes;
    CHECK(complete_writes>0 && complete_writes<100u);
    for(unsigned cut=0;cut<=complete_writes;cut++) {
        memcpy(disk,seed,disk_bytes);writes=0;write_fail_after=(int)cut;
        bool saved=save(false);
        write_fail_after=-1;enabled=true;CHECK(load(&enabled));
        if(saved)CHECK(!enabled);
        mount();file_read(KUI_DISC_REGION_PATH_A,current,sizeof(current));
        CHECK(!memcmp(active,current,sizeof(active)));
        uint8_t journal[1024];file_read("0:/KUI/vmu-test/p0005/card.journal",journal,sizeof(journal));
        CHECK(!memcmp(journal,sentinel,sizeof(journal)));unmount();
    }
    memcpy(disk,seed,disk_bytes);mount();
    uint8_t damaged[64];file_read(KUI_DISC_REGION_PATH_B,damaged,sizeof(damaged));damaged[32]^=1u;
    file_write(KUI_DISC_REGION_PATH_B,damaged,sizeof(damaged));unmount();
    enabled=false;CHECK(load(&enabled) && enabled);
    mount();active[32]^=1u;file_write(KUI_DISC_REGION_PATH_A,active,sizeof(active));unmount();
    enabled=false;CHECK(!load(&enabled) && !enabled);CHECK(!save(false));
    printf("Disc region real filesystem (%s): %u checks, %u write boundaries, %u isolated storage sessions passed\n",
        exfat?"exFAT":"FAT32",checks,complete_writes+1u,sessions);
    free(seed);free(disk);return 0;
}
