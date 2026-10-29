/* SPDX-License-Identifier: GPL-3.0-only */
/* Runs the game-card service on host FatFs over one partitioned FAT32 or
 * exFAT image made by mkfs (tests/test_vmu_game_card_service.py). The image
 * stays in memory; the stage's admission is replayed on every chosen card. */
#include "kui/vmu_game_card_service.h"
#include "kui/vmu_persist_profile.h"
#include "kui/media.h"
#include "diskio.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"%s:%d: %s (%s)\n",__FILE__,__LINE__,#x,last_log);exit(1);}} while(0)

static char last_log[256];
static uint8_t *disk;
static uint64_t sectors;
static FATFS fs;
static struct kui_volume volume;
static bool raw_open,cancel_flag;
static unsigned raw_begins,salt_counter;
static int write_fail_after=-1;
static unsigned writes_attempted;
static unsigned cancel_after,cancel_checks;
static uint8_t test_cid[16]={0x03,'S','D','K','U','I','-','T','E','S','T',0x10,1,2,3,0x41};
static struct kui_vmu_game_card_work work;

static void log_line(const char *format,...) {
    va_list args;va_start(args,format);vsnprintf(last_log,sizeof(last_log),format,args);va_end(args);
    if(getenv("KUI_TEST_VERBOSE")) fprintf(stderr,"%s\n",last_log);
}
static bool cancelled(void) {return cancel_flag || (cancel_after && ++cancel_checks>=cancel_after);}
static uint64_t media_blocks(void *context) {(void)context;return sectors;}
static int media_read(void *context,uint32_t block,size_t count,uint8_t *data) {
    (void)context;if((uint64_t)block+count>sectors) return -1;
    memcpy(data,disk+(size_t)block*512u,count*512u);return 0;
}
static int media_write(void *context,uint32_t block,size_t count,const uint8_t *data) {
    (void)context;CHECK(!raw_open);
    unsigned attempt=writes_attempted++;
    if(write_fail_after>=0 && attempt>=(unsigned)write_fail_after) return -1;
    if((uint64_t)block+count>sectors) return -1;
    memcpy(disk+(size_t)block*512u,data,count*512u);return 0;
}
static int media_sync(void *context) {(void)context;return 0;}
static const struct kui_media_ops media={NULL,media_blocks,media_read,media_write,media_sync};
static void mount(void) {
    kui_media_set(&media);
    CHECK(f_mount(&fs,"0:",1)==FR_OK);
    volume=*kui_media_volume();
    CHECK(volume.partitioned && volume.start==2048u);
}
static void unmount(void) {CHECK(f_mount(NULL,"0:",0)==FR_OK);}
static int raw_read(void *context,uint32_t lba,uint8_t out[512]) {
    CHECK(context==&disk && raw_open);
    if(lba>=sectors) return -1;
    memcpy(out,disk+(size_t)lba*512u,512u);return 0;
}
static bool identity(void *context,struct kui_vmu_game_card_raw *out) {
    CHECK(context==&volume && !raw_open);
    memcpy(out->cid,test_cid,16u);out->card_sectors=sectors;out->high_capacity=true;
    return true;
}
static bool raw_begin(void *context,struct kui_vmu_game_card_raw *out) {
    CHECK(context==&volume && !raw_open);
    if(f_mount(NULL,"0:",0)!=FR_OK) return false;
    raw_open=true;++raw_begins;
    out->read_sector=raw_read;out->context=&disk;
    return true;
}
static bool raw_end(void *context) {
    CHECK(context==&volume && raw_open);raw_open=false;
    return f_mount(&fs,"0:",1)==FR_OK && kui_media_volume()->start==volume.start &&
        kui_media_volume()->count==volume.count;
}
static void salt(void *context,uint8_t out[16]) {
    CHECK(context==&volume);memset(out,0x3c,16u);++salt_counter;
    memcpy(out,&salt_counter,sizeof(salt_counter));
}
static const struct kui_vmu_game_card_platform platform={&volume,identity,raw_begin,raw_end,salt};
static int select_card(const char *product,const struct kui_vmu_fs_range *forbidden,size_t count,
    struct kui_vmu_game_card_choice *choice,uint8_t wire[4096]) {
    return kui_vmu_game_card_select(product,volume.start,(uint64_t)volume.start+volume.count,
        forbidden,count,&platform,&work,wire,choice,log_line,cancelled);
}
static size_t file_read(const char *path,uint8_t *out,size_t capacity) {
    FIL file;UINT got=0;
    if(f_open(&file,path,FA_READ)!=FR_OK) return SIZE_MAX;
    size_t size=(size_t)f_size(&file);
    CHECK(size<=capacity && f_read(&file,out,(UINT)size,&got)==FR_OK && got==size && f_close(&file)==FR_OK);
    return size;
}
static void file_write(const char *path,const void *data,size_t bytes) {
    FIL file;UINT done=0;
    CHECK(f_open(&file,path,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
    CHECK(f_write(&file,data,(UINT)bytes,&done)==FR_OK && done==bytes);
    CHECK(f_close(&file)==FR_OK);
}
static bool exists(const char *path) {FILINFO info;return f_stat(path,&info)==FR_OK;}
static unsigned pair_dirs(void) {
    DIR dir;FILINFO info;unsigned count=0;
    if(f_opendir(&dir,KUI_VMU_GAME_CARD_ROOT)!=FR_OK) return 0;
    while(f_readdir(&dir,&info)==FR_OK && info.fname[0])
        if((info.fattrib&AM_DIR) && kui_vmu_game_card_pair_number(info.fname)) ++count;
    CHECK(f_closedir(&dir)==FR_OK);
    return count;
}
/* What the stage does first at boot: decode, then admit the live volume. */
static void admitted(const uint8_t wire[4096],unsigned pair) {
    static struct kui_vmu_persist_profile p;static struct kui_vmu_fs_workspace scratch;
    char expected[96]={0};snprintf(expected,sizeof(expected),"/KUI/vmu-test/p%04u",pair);
    CHECK(!kui_vmu_persist_profile_decode(&p,wire,4096u));
    CHECK(!memcmp(p.pair_path,expected,sizeof(expected)) && !memcmp(p.cid,test_cid,16u));
    CHECK(p.card_sectors==sectors && p.partition_first==volume.start &&
        p.partition_end==(uint64_t)volume.start+volume.count && p.high_capacity==1u);
    unmount();raw_open=true;
    struct kui_vmu_fs_input input={.read_sector=raw_read,.context=&disk,.card_sectors=p.card_sectors,
        .partition_first=p.partition_first,.partition_end=p.partition_end,
        .high_capacity=p.high_capacity,.pair_path=p.pair_path};
    memcpy(input.cid,p.cid,16u);memcpy(input.image_id,p.image_id,16u);
    int result=kui_vmu_fs_admit(&input,&p,&scratch);
    raw_open=false;CHECK(f_mount(&fs,"0:",1)==FR_OK);
    CHECK(!result);
}
static void pair_files(unsigned pair,const char *key,const uint8_t wire[4096]) {
    static uint8_t data[131072];char path[96];
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/card.vmd",pair);
    CHECK(file_read(path,data,sizeof(data))==131072u);
    for(unsigned block=0;block<256u;block++) {
        uint8_t blank[512];CHECK(!kui_vmu_image_blank_block((uint16_t)block,blank));
        CHECK(!memcmp(data+block*512u,blank,512u));
    }
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/card.journal",pair);
    CHECK(file_read(path,data,sizeof(data))==1024u);
    for(unsigned i=0;i<1024u;i++) CHECK(!data[i]);
    uint8_t identity[16];
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/identity.bin",pair);
    CHECK(file_read(path,identity,sizeof(identity))==16u);
    static struct kui_vmu_persist_profile p;
    CHECK(!kui_vmu_persist_profile_decode(&p,wire,4096u) && !memcmp(p.image_id,identity,16u));
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/game.bin",pair);
    char bound[KUI_VMU_GAME_CARD_KEY_BYTES];
    CHECK(file_read(path,data,sizeof(data))==64u && kui_vmu_game_card_binding_decode(data,64u,bound));
    CHECK(!strcmp(bound,key));
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/retail-persistent-profile-%04u.bin",pair,pair);
    CHECK(file_read(path,data,sizeof(data))==4096u && !memcmp(data,wire,4096u));
    static const char *const parts[]={"identity.part","game.part","profile.part"};
    for(unsigned i=0;i<3u;i++) {
        snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/%s",pair,parts[i]);
        CHECK(!exists(path));
    }
}
/* Recreate a file with the same bytes after a filler, as a PC copy would:
 * same contents, different clusters. */
static void move_file(unsigned pair,const char *leaf,unsigned filler) {
    static uint8_t data[131072],pad[65536];char path[96];
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/%s",pair,leaf);
    size_t size=file_read(path,data,sizeof(data));CHECK(size!=SIZE_MAX);
    char name[32];snprintf(name,sizeof(name),"0:/filler%u.bin",filler);
    memset(pad,(int)filler,sizeof(pad));file_write(name,pad,sizeof(pad));
    CHECK(f_unlink(path)==FR_OK);file_write(path,data,size);
}
static void first_run(uint32_t *first,uint32_t *count,const char *path) {
    FIL file;static DWORD map[66];
    CHECK(f_open(&file,path,FA_READ)==FR_OK);
    map[0]=66u;file.cltbl=map;
    CHECK(f_lseek(&file,CREATE_LINKMAP)==FR_OK && map[0]>=4u);
    *count=map[1];*first=map[2];
    unsigned runs=(map[0]-2u)/2u;
    if(runs>1u) {*count=map[2u*runs-1u];*first=map[2u*runs];}
    file.cltbl=NULL;CHECK(f_close(&file)==FR_OK);
}
/* Mark the last cluster of a pair's card.vmd free, as a damaged allocation
 * table would: a cleared exFAT bitmap bit or a zero FAT32 entry. */
static void clear_allocation(unsigned pair) {
    char path[96];uint32_t first,count;
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/card.vmd",pair);
    first_run(&first,&count,path);
    uint32_t last=first+count-1u;
    bool exfat=fs.fs_type==FS_EXFAT;
    uint64_t bitmap=(uint64_t)volume.start+fs.bitbase,fat=(uint64_t)volume.start+fs.fatbase;
    uint32_t fat_sectors=fs.fsize;unsigned copies=fs.n_fats;
    unmount();
    if(exfat) {
        uint32_t bit=last-2u;uint8_t *byte=disk+(size_t)(bitmap+bit/4096u)*512u+(bit%4096u)/8u;
        CHECK(*byte&(1u<<(bit%8u)));*byte&=(uint8_t)~(1u<<(bit%8u));
    } else for(unsigned copy=0;copy<copies;copy++) {
        uint8_t *entry=disk+(size_t)(fat+copy*(uint64_t)fat_sectors+last/128u)*512u+(last%128u)*4u;
        CHECK(entry[0] || entry[1] || entry[2] || entry[3]);memset(entry,0,4u);
    }
    mount();
}
static uint8_t *snapshot(void) {
    uint8_t *copy=malloc((size_t)sectors*512u);CHECK(copy);
    memcpy(copy,disk,(size_t)sectors*512u);return copy;
}
static bool unchanged(const uint8_t *copy) {return !memcmp(copy,disk,(size_t)sectors*512u);}

static void restore_disk(const uint8_t *saved) {
    unmount();memcpy(disk,saved,(size_t)sectors*512u);mount();
}
static void imported_assignments(void) {
    struct kui_vmu_game_card_choice choice;static uint8_t image[131072],verify[131072],wire[4096];
    uint8_t imported_id[16],second_id[16],fresh_salt[16]={0x41};
    for(unsigned block=0;block<256u;block++) CHECK(!kui_vmu_image_blank_block((uint16_t)block,image+block*512u));
    /* Existing free-block bytes are opaque and preserved, not synthesized. */
    image[10u*512u]=0x82;
    unsigned dirs=pair_dirs();uint8_t *before=snapshot();
    CHECK(kui_vmu_game_card_import_publish(image,sizeof(image)-1u,fresh_salt,&work,&choice,imported_id,log_line,cancelled)==KUI_VMU_GAME_CARD_FAILED);
    CHECK(unchanged(before) && pair_dirs()==dirs);free(before);
    /* A cancelled new folder is reserved forever; no partial card is assigned. */
    cancel_after=2u;cancel_checks=0;
    CHECK(kui_vmu_game_card_import_publish(image,sizeof(image),fresh_salt,&work,&choice,imported_id,log_line,cancelled)==KUI_VMU_GAME_CARD_CANCELLED);
    unsigned cancelled_pair=choice.pair;cancel_after=0;
    CHECK(cancelled_pair && pair_dirs()==dirs+1u);
    CHECK(kui_vmu_game_card_import_publish(image,sizeof(image),fresh_salt,&work,&choice,imported_id,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    unsigned imported=choice.pair;CHECK(imported!=cancelled_pair && choice.created);
    char path[96];snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/card.vmd",imported);
    CHECK(file_read(path,verify,sizeof(verify))==sizeof(image) && !memcmp(image,verify,sizeof(image)));
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/game.bin",imported);CHECK(!exists(path));
    ++fresh_salt[0];
    CHECK(kui_vmu_game_card_import_publish(image,sizeof(image),fresh_salt,&work,&choice,second_id,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    unsigned second=choice.pair;CHECK(second!=imported && memcmp(imported_id,second_id,16u));
    struct kui_vmu_game_assignment entries[3]={0};
    CHECK(kui_vmu_game_card_key("MK-51136",entries[0].key));
    CHECK(kui_vmu_game_card_key("MK-51058",entries[1].key));
    CHECK(kui_vmu_game_card_key("IMPORT-TWO",entries[2].key));
    for(unsigned i=0;i<3u;i++) {
        entries[i].pair=i==2u?second:imported;memcpy(entries[i].identity,i==2u?second_id:imported_id,16u);
    }
    CHECK(kui_vmu_game_card_assignments_apply(entries,3u,&work,&choice,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    /* The route guard can inspect an assignment without allocating,
     * collecting a profile, or changing any mounted file. */
    before=snapshot();
    CHECK(kui_vmu_game_card_assignment("mk-51136",&work,&choice,cancelled)==KUI_VMU_GAME_CARD_READY && choice.pair==imported);
    CHECK(kui_vmu_game_card_assignment("OTHER-GAME",&work,&choice,cancelled)==KUI_VMU_GAME_CARD_UNBOUND);
    CHECK(kui_vmu_game_card_assignment("",&work,&choice,cancelled)==KUI_VMU_GAME_CARD_UNBOUND && unchanged(before));free(before);
    /* Explicit assignment overrides old default game.bin, shares one card,
     * and prepares its admitted profile automatically on first launch. */
    CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_READY && choice.pair==imported && choice.collected);
    admitted(wire,imported);unsigned begins=raw_begins;
    CHECK(select_card("MK-51058",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_READY && choice.pair==imported && !choice.collected);
    CHECK(raw_begins==begins);admitted(wire,imported);
    CHECK(select_card("IMPORT-TWO",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_READY && choice.pair==second);
    admitted(wire,second);
    /* Give both slots a complete valid generation. */
    CHECK(kui_vmu_game_card_assignments_apply(entries,3u,&work,&choice,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    uint8_t *stable=snapshot();
    for(unsigned i=0;i<2u;i++) {entries[i].pair=second;memcpy(entries[i].identity,second_id,16u);}
    writes_attempted=0;
    CHECK(kui_vmu_game_card_assignments_apply(entries,2u,&work,&choice,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    unsigned writes=writes_attempted;CHECK(writes>0 && writes<100u);
    for(unsigned cut=0;cut<=writes;cut++) {
        restore_disk(stable);writes_attempted=0;write_fail_after=(int)cut;
        int result=kui_vmu_game_card_assignments_apply(entries,2u,&work,&choice,log_line,cancelled);
        CHECK(result==KUI_VMU_GAME_CARD_READY || result==KUI_VMU_GAME_CARD_FAILED);
        write_fail_after=-1;unmount();mount();
        CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_READY);
        unsigned selected=choice.pair;CHECK(selected==imported || selected==second);
        CHECK(select_card("MK-51058",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_READY && choice.pair==selected);
        if(result==KUI_VMU_GAME_CARD_READY) CHECK(selected==second);
    }
    restore_disk(stable);
    /* A torn newer slot chooses the previous complete generation. Both bad
     * slots fail closed without creating any replacement blank card. */
    uint8_t torn[64]={0};file_write(KUI_VMU_GAME_CARD_ROOT "/assign-b.bin",torn,sizeof(torn));
    CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_READY && choice.pair==imported);
    file_write(KUI_VMU_GAME_CARD_ROOT "/assign-a.bin",torn,sizeof(torn));
    before=snapshot();
    CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_FAILED);
    CHECK(strstr(choice.problem,"assignments are damaged") && unchanged(before));free(before);
    restore_disk(stable);
    /* Two separately valid records at the same sequence may not disagree. */
    CHECK(file_read(KUI_VMU_GAME_CARD_ROOT "/assign-b.bin",work.assignment_wire,sizeof(work.assignment_wire))==sizeof(work.assignment_wire));
    CHECK(kui_vmu_game_assign_decode(work.assignment_wire,sizeof(work.assignment_wire),&work.assignments));
    work.assignments.entries[0].pair=second;memcpy(work.assignments.entries[0].identity,second_id,16u);
    CHECK(kui_vmu_game_assign_encode(&work.assignments,work.assignment_wire));
    file_write(KUI_VMU_GAME_CARD_ROOT "/assign-a.bin",work.assignment_wire,sizeof(work.assignment_wire));
    before=snapshot();
    CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_FAILED);
    CHECK(strstr(choice.problem,"slots disagree") && unchanged(before));free(before);
    restore_disk(stable);
    /* Identity pins prevent an unrelated replacement at the same p number
     * from inheriting an old game's assignment, even with the same clusters. */
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/identity.bin",imported);
    uint8_t changed[16];memcpy(changed,imported_id,16u);changed[0]^=0x5a;file_write(path,changed,sizeof(changed));
    before=snapshot();
    CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_FAILED);
    CHECK(strstr(choice.problem,"identity changed") && unchanged(before));free(before);
    restore_disk(stable);
    /* Completed marker and journal are also required; damaged targets
     * refuse both launch and publication instead of silently changing cards. */
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/import.bin",imported);CHECK(f_unlink(path)==FR_OK);
    before=snapshot();
    CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_FAILED && unchanged(before));free(before);
    restore_disk(stable);
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/card.journal",imported);file_write(path,torn,sizeof(torn));
    before=snapshot();
    CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_FAILED && unchanged(before));free(before);
    restore_disk(stable);
    /* Reset/dead RTC + reset counter may repeat the exact creation salt.
     * Deleting/recreating a p number must never satisfy its retained pin. */
    static const char *const imported_leaves[]={"card.vmd","card.journal","identity.bin","import.bin"};
    for(unsigned i=0;i<4u;i++) {
        snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/%s",imported,imported_leaves[i]);CHECK(f_unlink(path)==FR_OK);
    }
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/retail-persistent-profile-%04u.bin",imported,imported);CHECK(f_unlink(path)==FR_OK);
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u",imported);CHECK(f_unlink(path)==FR_OK);
    --fresh_salt[0];before=snapshot();
    CHECK(kui_vmu_game_card_import_publish(image,sizeof(image),fresh_salt,&work,&choice,changed,log_line,cancelled)==KUI_VMU_GAME_CARD_FAILED);
    CHECK(strstr(choice.problem,"identity would repeat") && unchanged(before));free(before);
    CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_FAILED);
    restore_disk(stable);
    /* Keep the old pin only in the older slot; it is still a possible
     * recovery generation, so its identity must remain reserved too. */
    CHECK(kui_vmu_game_card_assignments_apply(entries,2u,&work,&choice,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    for(unsigned i=0;i<4u;i++) {
        snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/%s",imported,imported_leaves[i]);CHECK(f_unlink(path)==FR_OK);
    }
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/retail-persistent-profile-%04u.bin",imported,imported);CHECK(f_unlink(path)==FR_OK);
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u",imported);CHECK(f_unlink(path)==FR_OK);
    before=snapshot();
    CHECK(kui_vmu_game_card_import_publish(image,sizeof(image),fresh_salt,&work,&choice,changed,log_line,cancelled)==KUI_VMU_GAME_CARD_FAILED);
    CHECK(strstr(choice.problem,"identity would repeat") && unchanged(before));free(before);
    restore_disk(stable);
    /* A journal pending replay survives assignment changes. Remapping a
     * moved imported image refuses until that journal can be recovered. */
    static uint8_t pending[1024],saved_profile[4096],profile_check[4096];
    memset(pending,0,sizeof(pending));memset(pending+512u,0x11,512u);
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/card.journal",imported);file_write(path,pending,sizeof(pending));
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/retail-persistent-profile-%04u.bin",imported,imported);
    CHECK(file_read(path,saved_profile,sizeof(saved_profile))==sizeof(saved_profile));
    CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_READY && choice.pair==imported && !choice.collected);
    move_file(imported,"card.vmd",88u);
    CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_FAILED);
    CHECK(strstr(choice.problem,"journal holds a save"));
    CHECK(file_read(path,profile_check,sizeof(profile_check))==sizeof(profile_check) && !memcmp(saved_profile,profile_check,sizeof(saved_profile)));
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/card.journal",imported);
    CHECK(file_read(path,verify,sizeof(verify))==sizeof(pending) && !memcmp(verify,pending,sizeof(pending)));
    restore_disk(stable);free(stable);
    /* Import preserves existing live card bytes too. */
    snprintf(path,sizeof(path),KUI_VMU_GAME_CARD_ROOT "/p%04u/card.vmd",imported);
    CHECK(file_read(path,verify,sizeof(verify))==sizeof(image) && !memcmp(image,verify,sizeof(image)));
}

int main(int argc,char **argv) {
    if(argc!=2) {fprintf(stderr,"Usage: %s IMAGE\n",argv[0]);return 2;}
    FILE *file=fopen(argv[1],"rb");CHECK(file);
    CHECK(!fseek(file,0,SEEK_END));long length=ftell(file);CHECK(length>0 && !(length%512));
    CHECK(!fseek(file,0,SEEK_SET));sectors=(uint64_t)length/512u;
    disk=malloc((size_t)length);CHECK(disk && fread(disk,1,(size_t)length,file)==(size_t)length && !fclose(file));
    mount();
    struct kui_vmu_game_card_choice choice;
    static uint8_t wire[4096],first[4096],again[4096];
    uint8_t *before=snapshot();

    /* Nothing is read or written for a cancelled launch or an unbindable product. */
    cancel_flag=true;
    CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_CANCELLED);
    cancel_flag=false;
    CHECK(select_card("",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_UNBOUND);
    CHECK(select_card("MK 51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_UNBOUND);
    CHECK(select_card("0000000",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_UNBOUND);
    for(unsigned i=0;i<4096u;i++) CHECK(!wire[i]);
    CHECK(unchanged(before) && !raw_begins);free(before);

    /* First launch: parents, p0002 and its profile are created on a fresh volume. */
    CHECK(select_card("MK-51136",NULL,0,&choice,first)==KUI_VMU_GAME_CARD_READY);
    CHECK(choice.pair==2u && choice.created && choice.collected && !strcmp(choice.key,"MK-51136"));
    CHECK(raw_begins==1u);
    pair_files(2u,"MK-51136",first);admitted(first,2u);

    /* An installed pair without a binding is never chosen or changed. */
    static uint8_t shared_card[131072],shared_check[131072];
    for(unsigned block=0;block<256u;block++)
        CHECK(!kui_vmu_image_blank_block((uint16_t)block,shared_card+block*512u));
    shared_card[512u*10u]=0x5a;
    CHECK(f_mkdir(KUI_VMU_GAME_CARD_ROOT "/p0005")==FR_OK);
    file_write(KUI_VMU_GAME_CARD_ROOT "/p0005/card.vmd",shared_card,sizeof(shared_card));
    memset(shared_check,0,1024u);file_write(KUI_VMU_GAME_CARD_ROOT "/p0005/card.journal",shared_check,1024u);
    memset(shared_check,0x55,16u);file_write(KUI_VMU_GAME_CARD_ROOT "/p0005/identity.bin",shared_check,16u);

    /* Every later launch reuses the saved profile without writing anything. */
    before=snapshot();
    CHECK(select_card("mk-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_READY);
    CHECK(choice.pair==2u && !choice.created && !choice.collected && !memcmp(wire,first,4096u));
    CHECK(unchanged(before) && raw_begins==1u);free(before);

    /* Other games get the next free numbers; p0005 is skipped. */
    static const char *const products[]={"MK-51058","T-8101N","HDR-0076"};
    static const unsigned numbers[]={3u,4u,6u};
    for(unsigned i=0;i<3u;i++) {
        CHECK(select_card(products[i],NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_READY);
        CHECK(choice.pair==numbers[i] && choice.created && choice.collected);
        pair_files(numbers[i],products[i],wire);admitted(wire,numbers[i]);
        CHECK(memcmp(wire,first,4096u));
    }
    CHECK(pair_dirs()==5u);

    /* Two folders claiming one game refuse without writing. */
    static uint8_t binding[64];
    CHECK(file_read(KUI_VMU_GAME_CARD_ROOT "/p0002/game.bin",binding,sizeof(binding))==64u);
    CHECK(f_mkdir(KUI_VMU_GAME_CARD_ROOT "/p0010")==FR_OK);
    file_write(KUI_VMU_GAME_CARD_ROOT "/p0010/game.bin",binding,sizeof(binding));
    before=snapshot();
    CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_FAILED);
    CHECK(strstr(choice.problem,"p0002 and p0010 both claim MK-51136"));
    for(unsigned i=0;i<4096u;i++) CHECK(!wire[i]);
    CHECK(unchanged(before));free(before);
    CHECK(f_unlink(KUI_VMU_GAME_CARD_ROOT "/p0010/game.bin")==FR_OK && f_unlink(KUI_VMU_GAME_CARD_ROOT "/p0010")==FR_OK);

    /* A card copied back by a PC lands on new clusters: re-mapped once. */
    move_file(2u,"card.vmd",1u);
    CHECK(select_card("MK-51136",NULL,0,&choice,again)==KUI_VMU_GAME_CARD_READY);
    CHECK(choice.pair==2u && !choice.created && choice.collected && memcmp(again,first,4096u));
    pair_files(2u,"MK-51136",again);admitted(again,2u);
    unsigned begins=raw_begins;
    CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_READY);
    CHECK(!choice.collected && !memcmp(wire,again,4096u) && raw_begins==begins);

    /* identity.bin rewritten in place keeps its clusters but not its profile. */
    {
        FIL id;UINT done=0;static const uint8_t replacement[16]={0x99,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
        CHECK(f_open(&id,KUI_VMU_GAME_CARD_ROOT "/p0002/identity.bin",FA_WRITE|FA_OPEN_EXISTING)==FR_OK);
        CHECK(f_write(&id,replacement,16u,&done)==FR_OK && done==16u && f_close(&id)==FR_OK);
        CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_READY && choice.collected);
        static struct kui_vmu_persist_profile p;
        CHECK(!kui_vmu_persist_profile_decode(&p,wire,4096u) && !memcmp(p.image_id,replacement,16u));
        admitted(wire,2u);
    }

    /* A saved profile from another partition or geometry is re-mapped. */
    for(unsigned stale=0;stale<2u;stale++) {
        static struct kui_vmu_persist_profile p;static uint8_t edited[4096];
        static const char *const path=KUI_VMU_GAME_CARD_ROOT "/p0002/retail-persistent-profile-0002.bin";
        CHECK(file_read(path,edited,sizeof(edited))==4096u && !kui_vmu_persist_profile_decode(&p,edited,4096u));
        if(stale) --p.geometry.cluster_count;else ++p.partition_end;
        CHECK(!kui_vmu_persist_profile_encode(&p,edited));file_write(path,edited,sizeof(edited));
        CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_READY && choice.collected);
        CHECK(memcmp(wire,edited,4096u));admitted(wire,2u);
    }

    /* A moved pair whose journal still holds a save keeps its old profile. */
    static uint8_t journal[1024],old_profile[4096],profile_now[4096];
    memset(journal,0,sizeof(journal));memset(journal+512,0x11,512u);
    file_write(KUI_VMU_GAME_CARD_ROOT "/p0003/card.journal",journal,sizeof(journal));
    CHECK(file_read(KUI_VMU_GAME_CARD_ROOT "/p0003/retail-persistent-profile-0003.bin",old_profile,4096u)==4096u);
    move_file(3u,"card.vmd",2u);
    CHECK(select_card("MK-51058",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_FAILED);
    CHECK(strstr(choice.problem,"journal holds a save"));
    CHECK(file_read(KUI_VMU_GAME_CARD_ROOT "/p0003/retail-persistent-profile-0003.bin",profile_now,4096u)==4096u);
    CHECK(!memcmp(old_profile,profile_now,4096u));
    memset(journal,0,sizeof(journal));
    file_write(KUI_VMU_GAME_CARD_ROOT "/p0003/card.journal",journal,sizeof(journal));
    CHECK(select_card("MK-51058",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_READY && choice.collected);
    admitted(wire,3u);

    /* The game's own mapped track extents may not hold a card. */
    move_file(4u,"card.vmd",3u);
    uint32_t cluster,count;first_run(&cluster,&count,KUI_VMU_GAME_CARD_ROOT "/p0004/card.vmd");
    struct kui_vmu_fs_range track={(uint64_t)volume.start+fs.database+(uint64_t)(cluster-2u)*fs.csize,fs.csize};
    CHECK(select_card("T-8101N",&track,1u,&choice,wire)==KUI_VMU_GAME_CARD_FAILED);
    CHECK(strstr(choice.problem,"ownership scan refused"));
    CHECK(select_card("T-8101N",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_READY && choice.collected);
    admitted(wire,4u);

    /* A damaged allocation table stops new cards before any allocation. */
    uint8_t *good=snapshot();
    clear_allocation(6u);
    unsigned dirs=pair_dirs();
    before=snapshot();
    CHECK(select_card("NEW-0001",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_FAILED);
    CHECK(strstr(choice.problem,"p0006 card.vmd reads as unallocated"));
    CHECK(unchanged(before) && pair_dirs()==dirs);free(before);
    unmount();memcpy(disk,good,(size_t)sectors*512u);free(good);mount();
    CHECK(select_card("NEW-0001",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_READY && choice.pair==7u);
    admitted(wire,7u);

    /* A clone of the SD card has another CID: re-mapped, not handed over. */
    test_cid[15]^=0x5au;begins=raw_begins;
    CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_READY && choice.collected);
    CHECK(raw_begins==begins+1u);admitted(wire,2u);
    CHECK(select_card("MK-51136",NULL,0,&choice,wire)==KUI_VMU_GAME_CARD_READY && !choice.collected);

    /* The unbound pair is byte-identical after every launch above. */
    CHECK(file_read(KUI_VMU_GAME_CARD_ROOT "/p0005/card.vmd",shared_check,sizeof(shared_check))==131072u);
    CHECK(!memcmp(shared_check,shared_card,sizeof(shared_card)));
    CHECK(!exists(KUI_VMU_GAME_CARD_ROOT "/p0005/game.bin"));
    imported_assignments();
    printf("vmu game card service (%s): %u checks\n",fs.fs_type==FS_EXFAT?"exFAT":"FAT32",checks);
    unmount();free(disk);
    return 0;
}
