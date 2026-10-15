/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_selection.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* A small FatFs model injects faults at the actual file API boundaries. It
 * keeps both slots independently, so every attempted write can be checked
 * against the previously authoritative on-card bytes. */
struct image {bool exists;size_t size;uint8_t data[KUI_GAMES_SELECTION_BYTES];};
static struct image disk[2], snapshot[2];
static unsigned read_opens,write_opens,last_written,opened,closed;
static bool parents[3],file_in_parent;
enum fault {NONE,READ_OPEN,READ_ERROR,READ_SHORT,READ_CLOSE,WRITE_OPEN,WRITE_ERROR,
    WRITE_SHORT,SYNC_ERROR,WRITE_CLOSE,VERIFY_OPEN,VERIFY_CORRUPT,VERIFY_SHORT,
    VERIFY_CLOSE,CHANGED_DURING_READ,PARENT_ERROR};
static enum fault failure;
static const struct kui_games_selection old_choice={"/Games/Arcade","Sonic.gdi"};
static const struct kui_games_selection new_choice={"/Games/Other","Crazy Taxi.gdi"};
static unsigned index_of(const char *path) {
    if(!strcmp(path,KUI_GAMES_SELECTION_PATH_A)) return 0;
    assert(!strcmp(path,KUI_GAMES_SELECTION_PATH_B));return 1;
}
static int parent_of(const char *path) {
    static const char *const names[]={"0:/KUI","0:/KUI/apps","0:/KUI/apps/games"};
    for(unsigned i=0;i<3;i++) if(!strcmp(path,names[i])) return (int)i;
    assert(!"unexpected filesystem path");return -1;
}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    unsigned index=index_of(path);bool writing=(mode&FA_WRITE)!=0;
    if(writing) {
        ++write_opens;
        if(failure==WRITE_OPEN) return FR_DISK_ERR;
        last_written=index;disk[index].exists=true;disk[index].size=0;
    } else {
        ++read_opens;
        if(failure==READ_OPEN || (failure==VERIFY_OPEN && write_opens)) return FR_DISK_ERR;
        if(!disk[index].exists) return FR_NO_FILE;
        if(failure==CHANGED_DURING_READ && read_opens==3) {
            disk[index].data[16]++;
            uint32_t crc=kui_crc32(0,disk[index].data,disk[index].size-4);
            for(unsigned i=0;i<4;i++) disk[index].data[disk[index].size-4+i]=(uint8_t)(crc>>(8*i));
        }
    }
    memset(file,0,sizeof(*file));file->obj.id=(WORD)(index+1);file->obj.objsize=disk[index].size;
    file->flag=mode;++opened;return FR_OK;
}
FRESULT f_read(FIL *file,void *buffer,UINT wanted,UINT *got) {
    unsigned index=(unsigned)file->obj.id-1;assert(index<2 && !(file->flag&FA_WRITE));
    size_t at=(size_t)file->fptr,available=disk[index].size-at;
    UINT count=available<wanted?(UINT)available:wanted;
    if((failure==READ_SHORT || (failure==VERIFY_SHORT && write_opens)) && count) --count;
    memcpy(buffer,disk[index].data+at,count);file->fptr+=count;*got=count;
    return failure==READ_ERROR?FR_DISK_ERR:FR_OK;
}
FRESULT f_write(FIL *file,const void *buffer,UINT wanted,UINT *written) {
    unsigned index=(unsigned)file->obj.id-1;assert(index<2 && (file->flag&FA_WRITE));
    UINT count=(failure==WRITE_SHORT || failure==WRITE_ERROR)?wanted/2:wanted;
    assert((size_t)file->fptr+count<=sizeof(disk[index].data));
    memcpy(disk[index].data+(size_t)file->fptr,buffer,count);file->fptr+=count;
    disk[index].size=(size_t)file->fptr;file->obj.objsize=disk[index].size;*written=count;
    return failure==WRITE_ERROR?FR_DISK_ERR:FR_OK;
}
FRESULT f_sync(FIL *file) {
    assert(file->flag&FA_WRITE);return failure==SYNC_ERROR?FR_DISK_ERR:FR_OK;
}
FRESULT f_close(FIL *file) {
    unsigned index=(unsigned)file->obj.id-1;assert(index<2);++closed;
    if((file->flag&FA_WRITE) && failure==VERIFY_CORRUPT) disk[index].data[0]^=1;
    if((file->flag&FA_WRITE) && failure==WRITE_CLOSE) return FR_DISK_ERR;
    if(!(file->flag&FA_WRITE) && (failure==READ_CLOSE || (failure==VERIFY_CLOSE && write_opens))) return FR_DISK_ERR;
    return FR_OK;
}
FRESULT f_stat(const TCHAR *path,FILINFO *info) {
    int index=parent_of(path);memset(info,0,sizeof(*info));
    if(failure==PARENT_ERROR) return FR_DISK_ERR;
    if(!parents[index]) return FR_NO_PATH;
    info->fattrib=file_in_parent?0:AM_DIR;return FR_OK;
}
FRESULT f_mkdir(const TCHAR *path) {parents[parent_of(path)]=true;return FR_OK;}
static void reset(void) {
    memset(disk,0,sizeof(disk));memset(parents,0,sizeof(parents));file_in_parent=false;
    failure=NONE;read_opens=write_opens=opened=closed=0;last_written=99;
}
static void clear_faults(void) {failure=NONE;read_opens=write_opens=opened=closed=0;last_written=99;}
static void seed(unsigned slot,uint64_t seq,const struct kui_games_selection *s) {
    assert(kui_games_selection_encode(disk[slot].data,s,seq));disk[slot].size=KUI_GAMES_SELECTION_BYTES;disk[slot].exists=true;
}
static void expect(const struct kui_games_selection *s) {
    struct kui_games_selection out;assert(kui_games_selection_load(&out,NULL));assert(kui_games_selection_equal(&out,s));assert(opened==closed);
}
static void codec(void) {
    uint8_t record[KUI_GAMES_SELECTION_BYTES]={0};memcpy(record,"KUIGSEL1",8);record[8]=1;record[16]=9;
    memcpy(record+32,"/Games/Arcade",13);memcpy(record+160,"Sonic.gdi",9);
    uint32_t crc=kui_crc32(0,record,508);for(unsigned i=0;i<4;i++)record[508+i]=(uint8_t)(crc>>(8*i));
    struct kui_games_selection out;uint64_t seq=0;assert(kui_games_selection_decode(&out,&seq,record));assert(seq==9 && kui_games_selection_equal(&out,&old_choice));
    for(unsigned i=0;i<508;i++) {
        uint8_t bad[KUI_GAMES_SELECTION_BYTES];memcpy(bad,record,sizeof(bad));bad[i]^=128;
        struct kui_games_selection saved=out;uint64_t before=seq;
        assert(!kui_games_selection_decode(&out,&seq,bad));assert(!memcmp(&out,&saved,sizeof(out)) && seq==before);
    }
    record[300]=1;crc=kui_crc32(0,record,508);for(unsigned i=0;i<4;i++)record[508+i]=(uint8_t)(crc>>(8*i));
    assert(!kui_games_selection_decode(&out,&seq,record));
    struct kui_games_selection invalid=old_choice;strcpy(invalid.root,"/Games/../Other");
    assert(!kui_games_selection_valid(&invalid));strcpy(invalid.root,"/Games//Arcade");assert(!kui_games_selection_valid(&invalid));
    invalid=old_choice;strcpy(invalid.name,"../escape");assert(!kui_games_selection_valid(&invalid));
    invalid=old_choice;memset(invalid.name,'x',sizeof(invalid.name));assert(!kui_games_selection_valid(&invalid));
    memset(record,55,sizeof(record));uint8_t saved[KUI_GAMES_SELECTION_BYTES];memcpy(saved,record,sizeof(saved));
    assert(!kui_games_selection_encode(record,&invalid,1) && !memcmp(record,saved,sizeof(record)));
    assert(!kui_games_selection_encode(record,&old_choice,0));
    puts("PASS Games selection codec: independent fixture, corruption/reserved fields, canonical paths and preserved outputs");
}
static void recovery(void) {
    reset();struct kui_games_selection defaults;kui_games_selection_default(&defaults);expect(&defaults);assert(!write_opens);
    assert(kui_games_selection_save(&old_choice,NULL));assert(last_written==0);
    clear_faults();expect(&old_choice);assert(kui_games_selection_save(&old_choice,NULL) && !write_opens);
    assert(kui_games_selection_save(&new_choice,NULL) && last_written==1);clear_faults();expect(&new_choice);
    disk[1].data[0]^=1;clear_faults();expect(&old_choice);
    assert(kui_games_selection_save(&new_choice,NULL) && last_written==1);
    disk[1].size=200;clear_faults();expect(&old_choice);
    reset();seed(0,7,&old_choice);seed(1,7,&new_choice);snapshot[0]=disk[0];snapshot[1]=disk[1];
    struct kui_games_selection out;assert(!kui_games_selection_load(&out,NULL));assert(!kui_games_selection_save(&new_choice,NULL));
    assert(!write_opens && !memcmp(snapshot,disk,sizeof(disk)));
    reset();seed(0,7,&old_choice);seed(1,7,&old_choice);expect(&old_choice);
    reset();seed(0,UINT64_MAX,&old_choice);assert(!kui_games_selection_save(&new_choice,NULL) && !write_opens);
    puts("PASS Games selection files: cold defaults, alternating/no-op saves, corrupt/torn recovery and conflict/exhaustion refusal");
}
static void faults(void) {
    const enum fault cases[]={READ_OPEN,READ_ERROR,READ_SHORT,READ_CLOSE,WRITE_OPEN,WRITE_ERROR,WRITE_SHORT,SYNC_ERROR,WRITE_CLOSE,VERIFY_OPEN,VERIFY_CORRUPT,VERIFY_SHORT,VERIFY_CLOSE,PARENT_ERROR};
    for(unsigned slot=0;slot<2;slot++)for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        reset();seed(slot,7,&old_choice);seed(1-slot,6,&new_choice);snapshot[slot]=disk[slot];failure=cases[i];
        assert(!kui_games_selection_save(&new_choice,NULL));assert(!memcmp(&snapshot[slot],&disk[slot],sizeof(disk[slot])) && opened==closed);
        if(cases[i]==WRITE_ERROR || cases[i]==WRITE_SHORT || cases[i]==VERIFY_CORRUPT) {clear_faults();expect(&old_choice);}
    }
    reset();seed(0,7,&old_choice);parents[0]=true;file_in_parent=true;
    assert(!kui_games_selection_save(&new_choice,NULL) && !write_opens);
    puts("PASS Games selection files: read/write/sync/close/reread faults preserve authoritative slot");
}
int main(void) {codec();recovery();faults();return 0;}
