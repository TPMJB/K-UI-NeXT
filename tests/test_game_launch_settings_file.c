/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/game_launch_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* A small FatFs model injects faults at the actual file API boundaries. It
 * keeps both slots independently, so every attempted write can be checked
 * against the previously authoritative on-card bytes. */
struct image {bool exists;size_t size;uint8_t data[KUI_GAME_LAUNCH_SETTINGS_RECORD_MAX];};
static struct image disk[2], snapshot[2];
static unsigned read_opens,write_opens,last_written,opened,closed;
static bool parents[3],file_in_parent;
enum fault {NONE,READ_OPEN,READ_ERROR,READ_SHORT,READ_CLOSE,WRITE_OPEN,WRITE_ERROR,
    WRITE_SHORT,SYNC_ERROR,WRITE_CLOSE,VERIFY_OPEN,VERIFY_CORRUPT,VERIFY_SHORT,
    VERIFY_CLOSE,CHANGED_DURING_READ,PARENT_ERROR};
static enum fault failure;
static const struct kui_game_launch_settings old_choice={.reader=KUI_GAME_READER_BACKGROUND,.encoding=KUI_GAME_ENCODING_PLAIN};
static const struct kui_game_launch_settings new_choice={.reader=KUI_GAME_READER_EAGER,.cdda=true,.encoding=KUI_GAME_ENCODING_SCRAMBLED,
    .patches={.entries={{.address=UINT32_C(0x8c200000),.value=UINT32_C(0xdeadbeef),.enabled=true},
        {.address=UINT32_MAX,.value=UINT32_C(0x12345678)}}}};
static unsigned index_of(const char *path) {
    if(!strcmp(path,KUI_GAME_LAUNCH_SETTINGS_PATH_A)) return 0;
    assert(!strcmp(path,KUI_GAME_LAUNCH_SETTINGS_PATH_B));return 1;
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
static void same(struct kui_game_launch_settings a,struct kui_game_launch_settings b) {
    assert(a.reader==b.reader && a.cdda==b.cdda && a.encoding==b.encoding);
    for(unsigned i=0;i<2;i++) {
        assert(a.patches.entries[i].address==b.patches.entries[i].address);
        assert(a.patches.entries[i].value==b.patches.entries[i].value);
        assert(a.patches.entries[i].enabled==b.patches.entries[i].enabled);
    }
}
static void reset(void) {
    memset(disk,0,sizeof(disk));memset(parents,0,sizeof(parents));file_in_parent=false;
    failure=NONE;read_opens=write_opens=opened=closed=0;last_written=99;
}
static void clear_faults(void) {failure=NONE;read_opens=write_opens=opened=closed=0;last_written=99;}
static void seed(unsigned slot,uint64_t sequence,const char *path,const struct kui_game_launch_settings *choice) {
    static struct kui_game_launch_table table;
    kui_game_launch_table_clear(&table);assert(kui_game_launch_table_upsert(&table,path,choice));
    assert(kui_game_launch_settings_encode(disk[slot].data,sizeof(disk[slot].data),&disk[slot].size,&table,sequence));
    disk[slot].exists=true;
}
static void seed_legacy(unsigned slot,uint64_t sequence) {
    /* Independent v1 layout, deliberately not produced by the current codec. */
    static const uint8_t fixture[]={
        'K','U','I','L','C','H','0','1',1,0,0,0,56,0,0,0,
        0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,
        12,0,1,0,1,0,0,0,'/','g','a','m','e','s','/','a','.','g','d','i',0,0,0,0};
    memcpy(disk[slot].data,fixture,sizeof(fixture));
    for(unsigned i=0;i<8;i++) disk[slot].data[16+i]=(uint8_t)(sequence>>(i*8));
    uint32_t crc=kui_crc32(0,disk[slot].data,sizeof(fixture)-4u);
    for(unsigned i=0;i<4;i++) disk[slot].data[sizeof(fixture)-4u+i]=(uint8_t)(crc>>(i*8));
    disk[slot].size=sizeof(fixture);disk[slot].exists=true;
}
static void expect_load(const char *path,const struct kui_game_launch_settings *choice) {
    struct kui_game_launch_settings got=new_choice;
    assert(kui_game_launch_settings_load(path,&got,NULL));same(got,*choice);
    assert(opened==closed);
}
static void recovery_and_merging(void) {
    reset();struct kui_game_launch_settings defaults;kui_game_launch_settings_default(&defaults);
    expect_load("/games/a.gdi",&defaults);assert(!write_opens);
    assert(kui_game_launch_settings_save("/games/a.gdi",&old_choice,NULL));
    assert(last_written==0 && parents[0] && parents[1] && parents[2]);
    clear_faults();expect_load("0:\\GAMES\\A.GDI",&old_choice);
    assert(kui_game_launch_settings_save("/games/b.gdi",&new_choice,NULL));assert(last_written==1);
    clear_faults();expect_load("/games/a.gdi",&old_choice);expect_load("/games/b.gdi",&new_choice);
    snapshot[1]=disk[1];
    assert(kui_game_launch_settings_save("/games/a.gdi",&new_choice,NULL));assert(last_written==0);
    assert(!memcmp(&snapshot[1],&disk[1],sizeof(snapshot[1])));
    clear_faults();expect_load("/games/a.gdi",&new_choice);expect_load("/games/b.gdi",&new_choice);
    assert(kui_game_launch_settings_save("/GAMES/A.GDI",&new_choice,NULL));assert(!write_opens);
    disk[0].data[0]^=1;clear_faults();expect_load("/games/a.gdi",&old_choice);
    assert(kui_game_launch_settings_save("/games/a.gdi",&new_choice,NULL));assert(last_written==0);
    /* A torn newest slot falls back to the complete earlier generation. */
    disk[0].size/=2;clear_faults();expect_load("/games/b.gdi",&new_choice);
    reset();disk[0].exists=disk[1].exists=true;disk[0].size=1;disk[1].size=35;
    expect_load("/games/a.gdi",&defaults);
    puts("PASS launch files: cold-boot defaults, aliases, per-key merges, alternating slots and corrupt/torn newest recovery");
}
static void faults_preserve_authority(void) {
    const enum fault cases[]={READ_OPEN,READ_ERROR,READ_SHORT,READ_CLOSE,WRITE_OPEN,WRITE_ERROR,
        WRITE_SHORT,SYNC_ERROR,WRITE_CLOSE,VERIFY_OPEN,VERIFY_CORRUPT,VERIFY_SHORT,VERIFY_CLOSE,PARENT_ERROR};
    for(unsigned current=0;current<2;current++) for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        reset();seed_legacy(current,7);seed(1-current,6,"/games/a.gdi",&new_choice);
        snapshot[current]=disk[current];failure=cases[i];
        assert(!kui_game_launch_settings_save("/games/a.gdi",&new_choice,NULL));
        assert(!memcmp(&snapshot[current],&disk[current],sizeof(snapshot[current])));
        assert(opened==closed);
        /* Failed sync/close/verify may leave a complete newer slot: never
         * assume failure undoes bytes the device already accepted. */
        if(cases[i]==WRITE_ERROR || cases[i]==WRITE_SHORT || cases[i]==VERIFY_CORRUPT) {
            clear_faults();expect_load("/games/a.gdi",&old_choice);
        }
    }
    reset();seed(0,7,"/games/a.gdi",&old_choice);failure=READ_OPEN;
    struct kui_game_launch_settings out=new_choice,defaults;kui_game_launch_settings_default(&defaults);
    assert(!kui_game_launch_settings_load("/games/a.gdi",&out,NULL));same(out,defaults);
    assert(!write_opens);
    reset();seed(0,7,"/games/a.gdi",&old_choice);failure=CHANGED_DURING_READ;
    assert(!kui_game_launch_settings_save("/games/a.gdi",&new_choice,NULL));assert(!write_opens);
    reset();seed(0,UINT64_MAX,"/games/a.gdi",&old_choice);snapshot[0]=disk[0];
    assert(!kui_game_launch_settings_save("/games/a.gdi",&new_choice,NULL));
    assert(!write_opens && !memcmp(&snapshot[0],&disk[0],sizeof(snapshot[0])));
    reset();seed(0,7,"/games/a.gdi",&old_choice);parents[0]=true;file_in_parent=true;
    assert(!kui_game_launch_settings_save("/games/a.gdi",&new_choice,NULL));assert(!write_opens);
    struct kui_game_launch_settings invalid=new_choice;invalid.encoding=KUI_GAME_ENCODING_COUNT;
    clear_faults();assert(!kui_game_launch_settings_save("/games/a.gdi",&invalid,NULL));
    assert(!kui_game_launch_settings_save("/games/../a.gdi",&new_choice,NULL));
    assert(!read_opens && !write_opens);
    puts("PASS launch files: v1-to-v2 read/write/flush/close/verify fault refusal, untouched authoritative slot, read races and sequence exhaustion");
}
static void mixed_version_migration(void) {
    for(unsigned legacy=0;legacy<2;legacy++) {
        reset();seed_legacy(legacy,7);seed(1-legacy,6,"/games/a.gdi",&new_choice);
        expect_load("/games/a.gdi",&old_choice);snapshot[legacy]=disk[legacy];
        assert(kui_game_launch_settings_save("/games/b.gdi",&new_choice,NULL));
        assert(last_written==1-legacy && disk[last_written].data[8]==2);
        assert(!memcmp(&snapshot[legacy],&disk[legacy],sizeof(snapshot[legacy])));
        clear_faults();expect_load("/games/a.gdi",&old_choice);expect_load("/games/b.gdi",&new_choice);
        struct kui_game_launch_settings changed=old_choice;changed.patches=new_choice.patches;
        assert(kui_game_launch_settings_save("/games/a.gdi",&changed,NULL));
        assert(last_written==legacy && disk[legacy].data[8]==2);
        clear_faults();expect_load("/games/a.gdi",&changed);expect_load("/games/b.gdi",&new_choice);
        /* A newer v1 remains authoritative over v2; version is never used
         * as generation priority, and a torn v2 falls back to that v1. */
        reset();seed_legacy(legacy,9);seed(1-legacy,10,"/games/a.gdi",&new_choice);
        expect_load("/games/a.gdi",&new_choice);disk[1-legacy].size/=2;
        clear_faults();expect_load("/games/a.gdi",&old_choice);
        reset();seed_legacy(legacy,10);seed(1-legacy,9,"/games/a.gdi",&new_choice);
        expect_load("/games/a.gdi",&old_choice);
    }
    puts("PASS launch files: highest-sequence v1/v2 selection, non-destructive migration, preserved entries and torn migration recovery");
}
static void patch_tuple_updates(void) {
    reset();struct kui_game_launch_settings choice=old_choice;
    assert(kui_game_launch_settings_save("/games/a.gdi",&choice,NULL));
    for(unsigned change=0;change<12;change++) {
        clear_faults();assert(kui_game_launch_settings_save("/games/a.gdi",&choice,NULL));assert(!write_opens);
        switch(change) {
        case 0: choice.patches.entries[0].address=1;break;
        case 1: choice.patches.entries[0].value=UINT32_C(0xdeadbeef);break;
        case 2: choice.patches.entries[0].address=UINT32_C(0x8c200000);break;
        case 3: choice.patches.entries[0].enabled=true;break;
        case 4: choice.patches.entries[0].address=UINT32_C(0x8c200004);break;
        case 5: choice.patches.entries[0].value=UINT32_C(0x89abcdef);break;
        case 6: choice.patches.entries[1].address=UINT32_C(0xac200008);break;
        case 7: choice.patches.entries[1].value=UINT32_MAX;break;
        case 8: choice.patches.entries[1].enabled=true;break;
        case 9: choice.patches.entries[1].address=UINT32_C(0xac20000c);break;
        case 10: choice.patches.entries[1].value=UINT32_C(0x87654321);break;
        case 11: choice.patches.entries[0].enabled=false;break;
        }
        assert(kui_game_launch_settings_save("/games/a.gdi",&choice,NULL));assert(write_opens==1);
        clear_faults();expect_load("/games/a.gdi",&choice);
    }
    clear_faults();choice.patches.entries[0]=choice.patches.entries[1];
    assert(!kui_game_launch_settings_save("/games/a.gdi",&choice,NULL));assert(!read_opens && !write_opens);
    puts("PASS launch files: patch-only address, value and enable changes save; disabled tuples survive and enabled duplicate targets fail before I/O");
}
static void full_collection(void) {
    static struct kui_game_launch_table table;
    reset();kui_game_launch_table_clear(&table);
    for(unsigned i=0;i<KUI_GAME_LAUNCH_SETTINGS_ENTRIES;i++) {
        char path[64];snprintf(path,sizeof(path),"/games/title-%u/disc.gdi",i);
        assert(kui_game_launch_table_upsert(&table,path,&old_choice));
    }
    assert(kui_game_launch_settings_encode(disk[1].data,sizeof(disk[1].data),&disk[1].size,&table,20));
    disk[1].exists=true;snapshot[1]=disk[1];
    /* Updating one of 128 entries exercises chunked readback of the whole
     * collection; adding another image must never evict an unrelated entry. */
    assert(kui_game_launch_settings_save("/games/title-127/disc.gdi",&new_choice,NULL));
    assert(last_written==0 && !memcmp(&snapshot[1],&disk[1],sizeof(snapshot[1])));
    clear_faults();expect_load("/games/title-0/disc.gdi",&old_choice);
    expect_load("/games/title-127/disc.gdi",&new_choice);
    snapshot[0]=disk[0];snapshot[1]=disk[1];
    assert(!kui_game_launch_settings_save("/games/one-too-many.gdi",&new_choice,NULL));
    assert(!write_opens && !memcmp(snapshot,disk,sizeof(disk)));
    puts("PASS launch files: full collection updates, bounded chunked readback and overflow without eviction");
}
int main(void) {recovery_and_merging();faults_preserve_authority();mixed_version_migration();patch_tuple_updates();full_collection();return 0;}
