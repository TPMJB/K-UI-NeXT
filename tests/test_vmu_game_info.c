/* SPDX-License-Identifier: GPL-3.0-only */
/* Real preference/assignment/binding codecs and summary worker. The fake
 * filesystem permits metadata reads only and rejects every image open/write. */
#include "kui/vmu_game_info.h"
#include "kui/vmu_game_assign.h"
#include "kui/vmu_game_card_service.h"
#include "kui/vmu_emulation_settings.h"
#include "kui/vmu_library.h"
#include "kui/vmu_backing.h"
#include "kui/games_storage.h"
#include "kui/media.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks,opens,releases,metadata_reads,labels,writes;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"%s:%u: %s\n",__FILE__,__LINE__,#x);exit(1);}} while(0)
struct entry {char path[192];uint8_t *bytes;size_t length;bool read_error;};
static struct entry files[40];static unsigned file_count;
static char directories[20][6];static unsigned directory_count,directory_index;
static bool mounted,directory_open,release_error,open_error,identity_error,swap_after_label;
static unsigned card_cid=1,cancel_after,cancel_calls;
static int name_error;static char nickname[49];
static struct kui_volume volume={2048,65536,true};
static bool cancel(void) {return cancel_after && ++cancel_calls>=cancel_after;}
static int find(const char *path) {for(unsigned i=0;i<file_count;i++) if(!strcmp(files[i].path,path)) return (int)i;return -1;}
static void add(const char *path,const void *bytes,size_t length) {
    CHECK(file_count<40 && find(path)<0);struct entry *f=files+file_count++;
    snprintf(f->path,sizeof(f->path),"%s",path);f->length=length;
    if(bytes) {f->bytes=malloc(length);CHECK(f->bytes);memcpy(f->bytes,bytes,length);}
}
static void clear(void) {
    CHECK(!mounted && !directory_open);
    for(unsigned i=0;i<file_count;i++) free(files[i].bytes);
    memset(files,0,sizeof(files));file_count=directory_count=0;
    release_error=open_error=identity_error=swap_after_label=false;card_cid=1;
    cancel_after=cancel_calls=0;name_error=0;nickname[0]=0;
}
static void put32(uint8_t *out,uint32_t value) {for(unsigned i=0;i<4;i++) out[i]=(uint8_t)(value>>(8u*i));}
static void preference(bool enabled) {
    uint8_t wire[64]={0};memcpy(wire,"KUIVMUE1",8);put32(wire+8,1);put32(wire+12,64);
    put32(wire+16,1);put32(wire+24,enabled?1:0);put32(wire+60,kui_crc32(0,wire,60));
    add(KUI_VMU_EMULATION_SETTINGS_PATH_A,wire,sizeof(wire));
}
static void assignment(unsigned slot,uint64_t sequence,unsigned pair,unsigned identity,bool corrupt) {
    struct kui_vmu_game_assign_table table={.sequence=sequence,.count=1};
    strcpy(table.entries[0].key,"ALPHA-1");table.entries[0].pair=pair;
    memset(table.entries[0].identity,(int)identity,16);
    uint8_t wire[KUI_VMU_GAME_ASSIGN_WIRE_BYTES];CHECK(kui_vmu_game_assign_encode(&table,wire));
    if(corrupt) wire[101]^=1;
    add(slot?KUI_VMU_GAME_CARD_ROOT "/assign-b.bin":KUI_VMU_GAME_CARD_ROOT "/assign-a.bin",wire,sizeof(wire));
}
static void card(unsigned pair,const char *key,bool imported) {
    CHECK(directory_count<20);snprintf(directories[directory_count++],6,"p%04u",pair);
    char path[192];uint8_t identity[16];memset(identity,(int)pair,16);
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/identity.bin",pair);add(path,identity,16);
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/card.vmd",pair);add(path,NULL,KUI_VMU_IMAGE_BYTES);
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/card.journal",pair);add(path,NULL,1024);
    if(key) {
        char canonical[12];uint8_t wire[64];CHECK(kui_vmu_game_card_key(key,canonical));
        CHECK(kui_vmu_game_card_binding_encode(canonical,wire));
        snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/game.bin",pair);add(path,wire,sizeof(wire));
    }
    if(imported) {
        uint8_t wire[64]={0};memcpy(wire,"KUIVMUI1",8);memcpy(wire+16,identity,16);
        put32(wire+60,kui_crc32(0,wire,60));
        snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/import.bin",pair);add(path,wire,sizeof(wire));
    }
}
enum kui_games_storage_result kui_games_storage_open(kui_log_fn log) {
    (void)log;CHECK(!mounted);++opens;if(open_error) return KUI_GAMES_STORAGE_NO_CARD;
    mounted=true;return KUI_GAMES_STORAGE_OK;
}
bool kui_games_storage_release(void) {CHECK(mounted && !directory_open);mounted=false;++releases;return !release_error;}
const struct kui_volume *kui_media_volume(void) {return &volume;}
bool kui_sd_vmu_profile_identity(struct kui_vmu_sd_identity *out) {
    CHECK(mounted);memset(out,0,sizeof(*out));if(identity_error) return false;
    out->cid[0]=(uint8_t)card_cid;out->sectors=131072;out->high_capacity=true;return true;
}
FRESULT f_stat(const TCHAR *path,FILINFO *info) {
    CHECK(mounted);int at=find(path);memset(info,0,sizeof(*info));if(at<0) return FR_NO_FILE;
    info->fsize=files[at].length;return FR_OK;
}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    CHECK(mounted && !directory_open && mode==FA_READ);
    CHECK(!strstr(path,"card.vmd") && !strstr(path,"card.journal"));
    int at=find(path);if(at<0) return FR_NO_FILE;CHECK(files[at].bytes);
    memset(file,0,sizeof(*file));file->obj.id=(WORD)(at+1);file->obj.objsize=files[at].length;return FR_OK;
}
FRESULT f_read(FIL *file,void *data,UINT requested,UINT *got) {
    CHECK(mounted && file->obj.id && file->obj.id<=file_count);struct entry *f=files+file->obj.id-1;
    ++metadata_reads;if(f->read_error) return FR_DISK_ERR;
    size_t count=requested;if(count>f->length-file->fptr) count=f->length-file->fptr;
    memcpy(data,f->bytes+file->fptr,count);file->fptr+=count;*got=(UINT)count;return FR_OK;
}
FRESULT f_close(FIL *file) {(void)file;CHECK(mounted && !directory_open);return FR_OK;}
FRESULT f_opendir(DIR *dir,const TCHAR *path) {
    (void)dir;CHECK(mounted && !directory_open && !strcmp(path,KUI_VMU_GAME_CARD_ROOT));
    if(!directory_count) return FR_NO_PATH;
    directory_open=true;directory_index=0;return FR_OK;
}
FRESULT f_readdir(DIR *dir,FILINFO *info) {
    (void)dir;CHECK(mounted && directory_open);memset(info,0,sizeof(*info));
    if(directory_index<directory_count) {strcpy(info->fname,directories[directory_index++]);info->fattrib=AM_DIR;}
    return FR_OK;
}
FRESULT f_closedir(DIR *dir) {(void)dir;CHECK(mounted && directory_open);directory_open=false;return FR_OK;}
FRESULT f_write(FIL *file,const void *data,UINT size,UINT *written) {(void)file;(void)data;(void)size;(void)written;++writes;CHECK(false);return FR_DISK_ERR;}
FRESULT f_sync(FIL *file) {(void)file;++writes;CHECK(false);return FR_DISK_ERR;}
FRESULT f_mkdir(const TCHAR *path) {(void)path;++writes;CHECK(false);return FR_DISK_ERR;}
int kui_vmu_library_label(const char *path,const uint8_t expected[16],char out[49],kui_cancel_fn stop) {
    (void)stop;CHECK(mounted && !directory_open && strstr(path,"/card.vmd"));++labels;
    char identity_path[192];size_t prefix=(size_t)(strrchr(path,'/')-path);
    CHECK(prefix+14<sizeof(identity_path));memcpy(identity_path,path,prefix);strcpy(identity_path+prefix,"/identity.bin");
    int at=find(identity_path);CHECK(at>=0 && !memcmp(expected,files[at].bytes,16));
    if(name_error) return name_error;
    strcpy(out,nickname);if(swap_after_label) ++card_cid;return KUI_VMU_LIBRARY_OK;
}
static struct kui_vmu_game_info result;
static void run(const char *key,bool native) {
    kui_vmu_game_info_run(key,native,&result,NULL,cancel);CHECK(!mounted && !directory_open && !writes);
}
int main(void) {
    clear();run(" alpha-1 ",true);CHECK(result.known && result.enabled && !strcmp(result.product,"ALPHA-1"));
    CHECK(!strcmp(result.label,"Automatic game card") && strstr(result.notice,"checked at launch"));
    clear();preference(false);unsigned before=metadata_reads;run("ALPHA-1",true);
    CHECK(result.known && !result.enabled && !strcmp(result.label,"Physical VMUs"));CHECK(metadata_reads==before+1);
    clear();preference(false);files[0].bytes[30]^=1;run("ALPHA-1",true);
    CHECK(!result.known && !strcmp(result.label,"Unavailable"));
    before=opens;run("ALPHA-1",false);CHECK(result.known && !strcmp(result.label,"Physical / loader specific") && opens==before);
    clear();run("",true);CHECK(result.known && !strcmp(result.label,"Selected at launch"));
    clear();card(5,NULL,true);assignment(0,10,5,5,false);strcpy(nickname,"Shared imports");run("ALPHA-1",true);
    CHECK(result.known && !strcmp(result.label,"Shared imports"));
    clear();card(5,NULL,true);assignment(0,10,5,5,false);assignment(1,11,6,6,true);run("ALPHA-1",true);
    CHECK(result.known && !strcmp(result.label,"Shared virtual VMU"));
    clear();card(5,NULL,true);card(6,NULL,true);assignment(0,10,5,5,false);assignment(1,11,6,6,false);
    run("ALPHA-1",true);CHECK(result.known && !strcmp(result.label,"Shared virtual VMU"));
    clear();card(5,NULL,true);assignment(0,10,5,6,false);run("ALPHA-1",true);
    CHECK(!result.known && strstr(result.notice,"identity changed"));
    clear();card(5,NULL,true);assignment(0,10,5,5,false);assignment(1,10,6,6,false);run("ALPHA-1",true);
    CHECK(!result.known && strstr(result.notice,"disagree"));
    clear();assignment(0,10,5,5,true);run("ALPHA-1",true);CHECK(!result.known && strstr(result.notice,"damaged"));
    clear();card(5,NULL,true);assignment(0,10,5,5,false);
    files[find(KUI_VMU_GAME_CARD_ROOT "/p0005/import.bin")].bytes[20]^=1;
    run("ALPHA-1",true);CHECK(!result.known);
    clear();card(5,"ALPHA-1",false);strcpy(nickname,"Code Veronica saves");run("ALPHA-1",true);
    CHECK(result.known && !strcmp(result.label,"Code Veronica saves"));
    clear();card(5,"ALPHA-1",false);run("ALPHA-1",true);CHECK(result.known && !strcmp(result.label,"Game card ALPHA-1"));
    clear();card(5,"ALPHA-1",false);card(6,"ALPHA-1",false);run("ALPHA-1",true);
    CHECK(!result.known && strstr(result.notice,"Several"));
    clear();card(5,"BETA-1",false);run("ALPHA-1",true);CHECK(result.known && !strcmp(result.label,"Automatic game card"));
    clear();card(5,"ALPHA-1",false);name_error=KUI_VMU_LIBRARY_INVALID;run("ALPHA-1",true);CHECK(!result.known);
    clear();card(5,"ALPHA-1",false);swap_after_label=true;run("ALPHA-1",true);CHECK(!result.known && strstr(result.notice,"changed"));
    clear();card(5,NULL,true);assignment(0,10,5,5,false);files[find(KUI_VMU_GAME_CARD_ROOT "/assign-a.bin")].read_error=true;
    run("ALPHA-1",true);CHECK(!result.known && strstr(result.notice,"Cannot read"));
    clear();open_error=true;run("ALPHA-1",true);CHECK(!result.known);
    clear();release_error=true;run("ALPHA-1",true);CHECK(!result.known && strstr(result.notice,"release"));
    clear();identity_error=true;run("ALPHA-1",true);CHECK(!result.known);
    clear();cancel_after=2;run("ALPHA-1",true);CHECK(!result.known && strstr(result.notice,"cancelled"));
    CHECK(opens==releases+1);clear();printf("VMU game information: %u checks passed; metadata reads %u, names %u, writes %u\n",checks,metadata_reads,labels,writes);return 0;
}
