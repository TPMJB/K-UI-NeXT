/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/maintenance.h"
#include "kui/files.h"
#include "kui/media.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static FILE *image;static uint64_t blocks_count;static const char *fault;static unsigned reads,writes;static bool injected,payload,comparison,readonly,bios_image;static FATFS fs;
static uint8_t source_storage[KUI_MAINTENANCE_BIOS_BYTES];
static const char *selected="0:/KUI/backups/system/selected image.bin";
static char deep_path[KUI_FILES_PATH_CAP+2u];
static bool is(const char *name){return fault&&!strcmp(fault,name);}
static uint64_t blocks(void *ctx){(void)ctx;return blocks_count;}
static uint8_t byte(uint32_t offset){return (uint8_t)(((offset*97u)^(offset>>8))&255);}
static bool content(const uint8_t *data,size_t n){if(n<32)return false;for(unsigned i=0;i<32;i++)if(data[i]!=byte(i))return false;return true;}
static int rd(void *ctx,uint32_t block,size_t n,uint8_t *data){(void)ctx;if(fseeko(image,(off_t)block*512,SEEK_SET)||fread(data,512,n,image)!=n)return -1;if(payload&&content(data,n*512)&&(is("readback-fail")||is("readback-corrupt"))){injected=true;if(is("readback-fail"))return -1;data[20]^=1;}
    if(comparison&&content(data,n*512)){
        if(is("media-fail")){injected=true;return -1;}
        unsigned first_pass_reads=2*(bios_image?KUI_MAINTENANCE_BIOS_BYTES:KUI_MAINTENANCE_FLASH_BYTES)/4096;
        if(is("file-change")&&reads>first_pass_reads){injected=true;data[20]^=1;}
    }return 0;}
static int wr(void *ctx,uint32_t block,size_t n,const uint8_t *data){(void)ctx;assert(!readonly);++writes;if(content(data,n*512)){payload=true;if(is("write-fail")){injected=true;return -1;}}return fseeko(image,(off_t)block*512,SEEK_SET)||fwrite(data,512,n,image)!=n?-1:0;}
static int sync_image(void *ctx){(void)ctx;if(payload&&is("sync-fail")){injected=true;return -1;}return fflush(image)||fsync(fileno(image))?-1:0;}
static const struct kui_media_ops media={NULL,blocks,rd,wr,sync_image};
static bool ident(void *ctx,struct kui_maintenance_identity *out){(void)ctx;memset(out,0,sizeof(*out));out->region=2;out->language=1;out->audio=1;out->settings_valid=true;
    unsigned sizes[]={8192,8192,16384,32768,65536};uint32_t at=0;for(unsigned i=0;i<5;i++){out->start[i]=at;out->size[i]=sizes[i];at+=sizes[i];}
    if(is("bad-layout")){out->start[4]=0;injected=true;}if(is("identity-fail")||(is("identity-after-fail")&&reads)){injected=true;return false;}if(is("identity-change")&&reads){out->region=3;injected=true;}
    if(is("unknown-identity")){out->region=-1;out->settings_valid=false;}return true;}
static int source_read(void *ctx,bool bios,uint32_t offset,uint8_t *data,size_t n){(void)ctx;assert(n<=4096&&offset+n<=(bios?KUI_MAINTENANCE_BIOS_BYTES:KUI_MAINTENANCE_FLASH_BYTES));++reads;
    if(is("source-fail")||(is("source-second-fail")&&reads==2)){injected=true;return -1;}memcpy(data,source_storage+offset,n);
    unsigned first_pass_reads=2*(bios_image?KUI_MAINTENANCE_BIOS_BYTES:KUI_MAINTENANCE_FLASH_BYTES)/4096;
    if((is("source-change")&&reads==2)||(is("verify-source-change")&&reads>64)||
       (comparison&&is("source-late-change")&&reads>first_pass_reads)){data[0]^=1;injected=true;}return 0;}
static bool cancel(void){unsigned first_pass_reads=2*(bios_image?KUI_MAINTENANCE_BIOS_BYTES:KUI_MAINTENANCE_FLASH_BYTES)/4096;
    if(is("cancel-before")||(is("cancel-writing")&&reads>=4)||(is("cancel-verifying")&&reads>=64)||
       (comparison&&is("cancel-during")&&reads>=4)||
       (comparison&&is("cancel-recheck")&&reads>=first_pass_reads)){injected=true;return true;}return false;}
static void logline(const char *format,...){(void)format;}
static void check_file(const char *path,unsigned bytes){FIL file;assert(f_open(&file,path,FA_READ)==FR_OK);assert(f_size(&file)==bytes);uint8_t data[4096];for(unsigned i=0;i<bytes;i+=sizeof(data)){UINT got;assert(f_read(&file,data,sizeof(data),&got)==FR_OK&&got==sizeof(data));for(unsigned j=0;j<sizeof(data);j++)assert(data[j]==byte(i+j));}assert(f_close(&file)==FR_OK);}
static void deep_selected(bool create){
    strcpy(deep_path,"0:/KUI/backups/system");
    const unsigned lengths[]={127,127,88};
    for(unsigned i=0;i<3;i++){
        size_t at=strlen(deep_path);deep_path[at++]='/';
        memset(deep_path+at,(int)('a'+i),lengths[i]);deep_path[at+lengths[i]]=0;
        if(create)assert(f_mkdir(deep_path)==FR_OK);
    }
    strcat(deep_path,"/selected image.bin");
    assert(strlen(deep_path+2)==KUI_FILES_PATH_CAP-1);
    selected=deep_path;
}
static void prepare_comparison(void){
    assert(f_mkdir("0:/KUI")==FR_OK);assert(f_mkdir("0:/KUI/backups")==FR_OK);assert(f_mkdir("0:/KUI/backups/system")==FR_OK);
    if(is("deep")||is("deep-qualified"))deep_selected(true);
    if(is("missing"))return;
    uint32_t bytes=bios_image?KUI_MAINTENANCE_BIOS_BYTES:KUI_MAINTENANCE_FLASH_BYTES;
    if(is("truncated"))--bytes;else if(is("oversize"))++bytes;
    FIL file;assert(f_open(&file,selected,FA_WRITE|FA_CREATE_NEW)==FR_OK);
    for(uint32_t off=0;off<bytes;off+=4096){
        uint8_t data[4096];UINT amount=bytes-off>sizeof(data)?sizeof(data):bytes-off,written=0;
        for(UINT i=0;i<amount;i++){
            uint32_t at=off+i;data[i]=byte(at);
            if(is("different")&&(at==123||at==4095||at==4096||at==bytes-1))data[i]^=1;
        }
        assert(f_write(&file,data,amount,&written)==FR_OK&&written==amount);
    }
    assert(f_close(&file)==FR_OK);
}
static void check_comparison(const struct kui_maintenance_source *src){
    if(is("deep")||is("deep-qualified")||is("too-long"))deep_selected(false);
    struct kui_app_status out;const char *path=selected+2;
    char too_long[KUI_FILES_PATH_CAP+1u];
    if(is("unsafe-parent"))path="0:/KUI/../selected image.bin";
    else if(is("other-drive"))path="1:/KUI/backups/system/selected image.bin";
    else if(is("root"))path="0:/";
    else if(is("qualified")||is("deep-qualified"))path=selected;
    else if(is("directory"))path="/KUI/backups/system";
    else if(is("too-long")){memcpy(too_long,selected+2,KUI_FILES_PATH_CAP-1);too_long[KUI_FILES_PATH_CAP-1]='x';too_long[KUI_FILES_PATH_CAP]=0;path=too_long;}
    readonly=true;comparison=true;writes=0;
    kui_maintenance_compare_execute(path,bios_image,src,&out,logline,cancel,NULL);
    assert(!writes);for(uint32_t i=0;i<sizeof(source_storage);i++)assert(source_storage[i]==byte(i));
    bool good=is("exact")||is("qualified")||is("unknown-identity")||is("deep")||is("deep-qualified"),different=is("different");
    assert(out.passed==good);assert(out.complete==(good||different));
    if(good||different){
        assert(reads==4*(bios_image?KUI_MAINTENANCE_BIOS_BYTES:KUI_MAINTENANCE_FLASH_BYTES)/4096);
        assert(!out.errors&&!out.stopped&&out.done==out.total&&out.line_count==12);
        if(good){assert(strstr(out.message,"Exact match:")&&strstr(out.lines[10],"differing bytes=0"));if(is("unknown-identity"))assert(strstr(out.lines[0],"Unknown")&&strstr(out.lines[1],"unavailable"));}
        else assert(strstr(out.message,"Different: 4 bytes; first at 0x0000007b")&&strstr(out.lines[10],"Different bytes=4"));
    }else{
        assert(out.stopped==(!strncmp(fault,"cancel-",7)));
        if(!out.stopped)assert(out.errors);
        if(is("truncated")||is("oversize")||is("missing")||is("directory")||is("unsafe-parent")||is("other-drive")||is("root")||is("too-long"))assert(!reads);
        else assert(injected);
    }
}
int main(int argc,char **argv){if(argc!=3&&argc!=4)return 2;bool prepare=argc==4&&!strcmp(argv[3],"prepare");
    comparison=!strncmp(argv[2],"compare-",8);bios_image=!strncmp(argv[2],"compare-bios-",13);readonly=comparison&&!prepare;
    const char *separator=comparison?strchr(argv[2]+8,'-'):NULL;
    if(comparison&&!separator)return 2;
    fault=comparison?separator+1:argv[2];
    for(uint32_t i=0;i<sizeof(source_storage);i++)source_storage[i]=byte(i);
    struct stat st;assert(stat(argv[1],&st)==0);blocks_count=(uint64_t)st.st_size/512;image=fopen(argv[1],readonly?"rb":"r+b");assert(image);kui_media_set(&media);assert(kui_mount(&fs,logline));const struct kui_maintenance_source src={NULL,ident,source_read};struct kui_app_status out;unsigned action=is("inspect")?0:is("bios")?2:1;
    if(comparison){if(prepare){comparison=false;prepare_comparison();}else check_comparison(&src);goto done;}
    kui_maintenance_execute(action,&src,&out,logline,cancel,NULL);
    bool good=is("good")||is("inspect")||is("bios");assert(out.passed==good);
    if(good){assert(out.complete&&!out.errors);if(is("inspect"))assert(!writes&&!reads);else{char path[128];snprintf(path,sizeof(path),"0:/KUI/backups/system/%s-0001/image.bin",action==2?"bios":"flash");check_file(path,action==2?KUI_MAINTENANCE_BIOS_BYTES:KUI_MAINTENANCE_FLASH_BYTES);
        kui_maintenance_execute(action,&src,&out,logline,cancel,NULL);assert(out.passed);check_file(path,action==2?KUI_MAINTENANCE_BIOS_BYTES:KUI_MAINTENANCE_FLASH_BYTES);snprintf(path,sizeof(path),"0:/KUI/backups/system/%s-0002/image.bin",action==2?"bios":"flash");check_file(path,action==2?KUI_MAINTENANCE_BIOS_BYTES:KUI_MAINTENANCE_FLASH_BYTES);}}
    else{assert(injected);if(is("cancel-before")||is("identity-fail")||is("bad-layout"))assert(!writes);fault=NULL;assert(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(&media);assert(kui_mount(&fs,logline));FILINFO info;FRESULT r=f_stat("0:/KUI/backups/system/flash-0001/image.bin",&info);assert(r==FR_NO_FILE||r==FR_NO_PATH);}
done:
    assert(f_mount(NULL,"0:",0)==FR_OK);assert(fclose(image)==0);printf("PASS maintenance %s\n",argv[2]);return 0;}
