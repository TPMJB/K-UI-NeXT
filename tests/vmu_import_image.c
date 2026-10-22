/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/vmu_import.h"
#include "kui/files.h"
#include "kui/hash.h"
#include "kui/media.h"
#include "kui/storage.h"
#include "kui/vmu_read_bounded.h"
#include <dc/maple.h>
#include <dc/vmufs.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static FILE *image;static uint64_t image_blocks;static FATFS fs;static const char *mode;static unsigned writes,connects,reads,vmucalls;static bool preview_only,restore_phase,inject_reads,cancel_during;static uint8_t card[256][512];static maple_device_t device={1,0,1,{MAPLE_FUNC_MEMCARD}};
static char selected[KUI_FILES_PATH_CAP]="/imports/SAVE.vms",resource[KUI_FILES_PATH_CAP]="/imports/RESOURCE.vms";
static uint8_t expected[2048],entry[32];static size_t source_bytes,padded;static bool game;
static bool is(const char *text){return !strcmp(mode,text);}
static void put16(uint8_t *p,unsigned n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static unsigned crc(const uint8_t *p,size_t n){unsigned state=0;for(size_t i=0;i<n;i++){state^=(unsigned)p[i]<<8;for(unsigned j=0;j<8;j++)state=((state<<1)^((state&32768)?0x1021:0))&65535;}return state;}
static uint64_t blocks(void *ctx){(void)ctx;return image_blocks;}
static int rd(void *ctx,uint32_t block,size_t n,uint8_t *out){(void)ctx;++reads;if(inject_reads&&is("media-fail"))return -1;return fseeko(image,(off_t)block*512,SEEK_SET)||fread(out,512,n,image)!=n?-1:0;}
static int wr(void *ctx,uint32_t block,size_t n,const uint8_t *data){(void)ctx;assert(!preview_only);++writes;return fseeko(image,(off_t)block*512,SEEK_SET)||fwrite(data,512,n,image)!=n?-1:0;}
static int sync_image(void *ctx){(void)ctx;return fflush(image)||fsync(fileno(image))?-1:0;}
static const struct kui_media_ops media={NULL,blocks,rd,wr,sync_image};
void kui_sd_set_params(unsigned transport,bool check){assert(transport==KUI_STORAGE_AUTO&&check);}
bool kui_sd_connect(void){++connects;kui_media_set(&media);return true;}
void kui_sd_disconnect(void){assert(connects);--connects;}
maple_device_t *maple_enum_dev(int port,int unit){++vmucalls;assert(restore_phase&&port==0&&unit==1);return &device;}
int vmufs_mutex_lock(void){assert(restore_phase);return 0;}
int vmufs_mutex_unlock(void){assert(restore_phase);return 0;}
int kui_vmu_read_bounded(maple_device_t *dev,uint16_t block,uint8_t data[512]){assert(restore_phase&&dev==&device&&block<256);++vmucalls;memcpy(data,card[block],512);return 0;}
int vmu_block_write(maple_device_t *dev,uint16_t block,const uint8_t *data){(void)dev;(void)block;(void)data;assert(!"Import and restore preview must never write VMUs");return -1;}
int vmufs_dir_write(maple_device_t *dev,vmu_root_t *root,vmu_dir_t *dir){(void)dev;(void)root;(void)dir;assert(!"Unexpected VMU directory write");return -1;}
int vmufs_fat_write(maple_device_t *dev,vmu_root_t *root,uint16_t *fat){(void)dev;(void)root;(void)fat;assert(!"Unexpected VMU FAT write");return -1;}
int vmufs_write(maple_device_t *dev,const char *name,void *data,int len,int flags){(void)dev;(void)name;(void)data;(void)len;(void)flags;assert(!"Unexpected VMU write");return -1;}
static bool cancel(void){return is("cancel-before")||(cancel_during&&reads>4);}
static void logline(const char *format,...){(void)format;}
static void create_file(const char *path,const uint8_t *bytes,size_t size){char local[KUI_FILES_PATH_CAP+2];snprintf(local,sizeof(local),"0:%s",path);FIL file;assert(f_open(&file,local,FA_WRITE|FA_CREATE_NEW)==FR_OK);UINT written;assert(f_write(&file,bytes,(UINT)size,&written)==FR_OK&&written==size);assert(f_close(&file)==FR_OK);}
static void payload(void){memset(expected,0,sizeof(expected));game=is("vmi-game")||is("dci-game");unsigned base=game?512:0;memcpy(expected+base,"Constructed save",16);memcpy(expected+base+16,"Independent filesystem fixture",30);memcpy(expected+base+48,"K-UI TEST",9);put16(expected+base+64,1);put32(expected+base+72,37);source_bytes=base+128+512+37;padded=(source_bytes+511)/512*512;for(size_t i=base+128;i<source_bytes;i++)expected[i]=(uint8_t)(i*13+7);if(game){put16(expected+base+70,0xabcd);put32(expected+base+72,UINT32_MAX);}else put16(expected+70,crc(expected,source_bytes));memset(expected+source_bytes,0xa7,padded-source_bytes);}
static void paths(bool create){
    strcpy(selected,"/imports/SAVE.vms");strcpy(resource,"/imports/RESOURCE.vms");
    if(is("deep")){
        strcpy(selected,"/imports");const unsigned lengths[]={127,127,109};
        for(unsigned i=0;i<3;i++){size_t at=strlen(selected);selected[at++]='/';memset(selected+at,'a'+(int)i,lengths[i]);selected[at+lengths[i]]=0;if(create){char path[KUI_FILES_PATH_CAP+2];snprintf(path,sizeof(path),"0:%s",selected);assert(f_mkdir(path)==FR_OK);}}
        strcat(selected,"/SAVE.vms");assert(strlen(selected)==KUI_FILES_PATH_CAP-1);
    }else if(is("vmi")||is("vmi-game")||is("missing-companion")||is("vmi-change")||is("metadata-highbit"))strcpy(selected,"/imports/record.vmi");
    else if(is("dci")||is("dci-game")||is("dci-ignored-first"))strcpy(selected,"/imports/record.dci");
    else if(is("uppercase"))strcpy(selected,"/imports/SAVE.VMS");
    else if(is("long-name"))strcpy(selected,"/imports/THIRTEENCHARS.vms");
}
static void fixture(void){
    payload();assert(f_mkdir("0:/imports")==FR_OK);paths(true);
    if(strstr(selected,".vmi")){
        uint8_t vmi[108]={0};memcpy(vmi+80,"RESOURCE",8);memcpy(vmi+88,"ORIGINAL",8);if(is("metadata-highbit")){vmi[88]=0x82;vmi[89]=0xa0;}
        static const uint8_t sega[]={83,69,71,65};for(unsigned i=0;i<4;i++)vmi[i]=vmi[80+i]&sega[i];put16(vmi+68,2024);vmi[70]=2;vmi[71]=29;vmi[72]=23;vmi[73]=59;vmi[74]=58;vmi[75]=4;put16(vmi+78,1);put16(vmi+100,game?3:0);put32(vmi+104,(uint32_t)(game?padded:source_bytes));create_file(selected,vmi,sizeof(vmi));if(!is("missing-companion"))create_file(resource,expected,game?padded:source_bytes);
    }else if(strstr(selected,".dci")){
        uint8_t data[2080]={0};data[0]=game?0xcc:0x33;data[1]=0xff;put16(data+2,is("dci-ignored-first")?0xffff:47);memcpy(data+4,"ORIGINAL",8);uint8_t time[]={0x20,0x24,0x02,0x29,0x23,0x59,0x58,3};memcpy(data+16,time,8);put16(data+24,(unsigned)(padded/512));put16(data+26,game?1:0);memcpy(data+32,expected,padded);
        for(size_t i=32;i<padded+32;i+=4){uint8_t a=data[i],b=data[i+1];data[i]=data[i+3];data[i+1]=data[i+2];data[i+2]=b;data[i+3]=a;}create_file(selected,data,padded+32);
    }else{if(is("bad-crc"))expected[source_bytes-1]^=1;create_file(selected,expected,is("padded")?padded:source_bytes);}
    memcpy(entry+4,"ORIGINAL",8);
}
static void source_digest(uint8_t digest[32]){char local[KUI_FILES_PATH_CAP+2];struct kui_sha256 hash;kui_sha256_init(&hash);const char *paths_to_hash[]={selected,resource};for(unsigned i=0;i<(strstr(selected,".vmi")&&!is("missing-companion")?2u:1u);i++){snprintf(local,sizeof(local),"0:%s",paths_to_hash[i]);FIL file;assert(f_open(&file,local,FA_READ)==FR_OK);uint8_t bytes[4096];for(;;){UINT got;assert(f_read(&file,bytes,sizeof(bytes),&got)==FR_OK);kui_sha256_update(&hash,bytes,got);if(got<sizeof(bytes))break;}assert(f_close(&file)==FR_OK);}kui_sha256_digest(&hash,digest);}
static void change(const char *path){char local[KUI_FILES_PATH_CAP+2];snprintf(local,sizeof(local),"0:%s",path);FIL file;assert(f_open(&file,local,FA_WRITE)==FR_OK);assert(f_lseek(&file,is("vmi-change")?4:640)==FR_OK);UINT written;uint8_t b=0xea;assert(f_write(&file,&b,1,&written)==FR_OK&&written==1);assert(f_close(&file)==FR_OK);}
static void verify_bundle(const char *path){char record[KUI_VMU_BACKUP_PATH_CAP];strcpy(record,path);char *last=strrchr(record,'/');assert(last);strcpy(last+1,"import.txt");FIL note;assert(f_open(&note,record,FA_READ)==FR_OK);uint8_t report[2048];UINT report_got=0;assert(f_read(&note,report,sizeof(report)-1,&report_got)==FR_OK);report[report_got]=0;assert(strstr((char *)report,"Input snapshot SHA256:")&&strstr((char *)report,selected));assert(f_close(&note)==FR_OK);char peer[KUI_VMU_BACKUP_PATH_CAP];strcpy(peer,path);char *dot=strrchr(peer,'.');assert(dot);strcpy(dot,".dir");FIL file;assert(f_open(&file,peer,FA_READ)==FR_OK);UINT got;assert(f_read(&file,entry,32,&got)==FR_OK&&got==32);assert(f_close(&file)==FR_OK);assert(entry[0]==(game?0xcc:0x33));if(is("dci-ignored-first"))assert(entry[2]==0xff&&entry[3]==0xff);if(is("metadata-highbit"))assert(entry[4]==0x82&&entry[5]==0xa0);else assert(!memcmp(entry+4,strstr(selected,".vmi")||strstr(selected,".dci")?"ORIGINAL":"SAVE",strstr(selected,".vmi")||strstr(selected,".dci")?8:4));assert(f_open(&file,path,FA_READ)==FR_OK);assert(f_size(&file)==padded);uint8_t bytes[2048];assert(f_read(&file,bytes,(UINT)padded,&got)==FR_OK&&got==padded);assert(f_close(&file)==FR_OK);assert(!memcmp(bytes,expected,source_bytes));if(!is("padded")&&!strstr(selected,".dci")&&!game)for(size_t i=source_bytes;i<padded;i++)assert(!bytes[i]);else assert(!memcmp(bytes,expected,padded));
    restore_phase=true;memset(card,0,sizeof(card));memset(card[255],0x55,16);put16(card[255]+70,254);put16(card[255]+72,1);put16(card[255]+74,253);put16(card[255]+76,13);put16(card[255]+80,200);for(unsigned i=0;i<256;i++)put16(card[254]+i*2,0xfffc);
    struct kui_vmu_view restored;kui_vmu_restore_run(path,0,false,&restored,logline,NULL,NULL);assert(restored.status.passed&&restored.restore_ready);restore_phase=false;
}
int main(int argc,char **argv){
    if(argc!=4)return 2;
    mode=argv[2];bool prepare=!strcmp(argv[3],"prepare");preview_only=!prepare&&is("preview-only");struct stat st;assert(stat(argv[1],&st)==0);image_blocks=(uint64_t)st.st_size/512;image=fopen(argv[1],preview_only?"rb":"r+b");assert(image);kui_media_set(&media);assert(kui_mount(&fs,logline));
    if(prepare){fixture();goto done;}
    payload();paths(false);uint8_t before[32],after[32];source_digest(before);assert(f_mount(NULL,"0:",0)==FR_OK);writes=reads=0;
    struct kui_vmu_view out;const char *path=selected;if(is("qualified")){static char qualified[KUI_FILES_PATH_CAP+2];snprintf(qualified,sizeof(qualified),"0:%s",selected);path=qualified;}else if(is("unsafe"))path="/imports/../SAVE.vms";else if(is("other-drive"))path="1:/imports/SAVE.vms";
    inject_reads=is("media-fail");cancel_during=is("cancel-during");
    kui_vmu_import_run(path,is("no-preview"),&out,is("qualified")?NULL:logline,cancel,NULL);assert(!vmucalls&&!writes&&!connects);
    bool bad=is("bad-crc")||is("long-name")||is("missing-companion")||is("no-preview")||is("unsafe")||is("other-drive")||is("media-fail")||is("cancel-before")||is("cancel-during");
    assert(out.status.passed==!bad);assert(out.import_ready==!bad);
    if(!bad&&!preview_only){
        if(is("source-change")||is("vmi-change")){assert(kui_mount(&fs,logline));change(is("vmi-change")?selected:selected);source_digest(before);assert(f_mount(NULL,"0:",0)==FR_OK);}
        const char *commit_path=is("path-change")?"/imports/OTHER.vms":path;
        writes=0;kui_vmu_import_run(commit_path,true,&out,logline,cancel,NULL);assert(!vmucalls&&!connects&&!out.import_ready);
        bool changed=is("source-change")||is("vmi-change")||is("path-change");assert(out.status.passed==!changed);
        if(changed)assert(!writes);else{assert(writes&&out.backup_path[0]);char first[KUI_VMU_BACKUP_PATH_CAP];strcpy(first,out.backup_path);assert(kui_mount(&fs,logline));verify_bundle(first);assert(f_mount(NULL,"0:",0)==FR_OK);vmucalls=0;kui_vmu_import_run(path,true,&out,logline,cancel,NULL);assert(!out.status.passed);}
    }
    inject_reads=false;kui_media_set(&media);assert(kui_mount(&fs,logline));source_digest(after);assert(!memcmp(before,after,32));
done:
    assert(f_mount(NULL,"0:",0)==FR_OK);assert(fclose(image)==0);printf("PASS VMU import %s\n",mode);return 0;
}
