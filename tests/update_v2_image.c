/* SPDX-License-Identifier: GPL-3.0-only */
/* Self-contained mixed-build KUPs through production FatFs installation. */
#include "update_fixture.h"
#include "kui/media.h"
#include "ff.h"
#include <stdio.h>

#define FULL "0:/KUI/updates/full.kup"
#define READER "0:/KUI/updates/reader.kup"
#define CONFIG "0:/KUI/updates/config.kup"
#define BAD "0:/KUI/updates/bad.kup"
#define BODY 40064u
static uint8_t *disk;static size_t disk_bytes;static FATFS fs;static bool mounted,active,low_space;
static unsigned checks,writes,syncs;static int fail_write=-1,fail_sync=-1,cancel_after=-1;
static int effective_mode;static char effective_bank[13];static uint8_t effective_hash[32];
int kui_update_effective_snapshot(char bank[13],uint8_t hash[32]) {
    if(effective_mode>0) {memcpy(bank,effective_bank,13u);memcpy(hash,effective_hash,32u);}
    return effective_mode;
}
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Updater v2 failed %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint64_t blocks(void *unused) {(void)unused;return disk_bytes/512u;}
static int read_blocks(void *unused,uint32_t block,size_t count,uint8_t *out) {
    (void)unused;if((uint64_t)block+count>blocks(NULL))return -1;
    memcpy(out,disk+(size_t)block*512u,count*512u);return 0;
}
static int write_blocks(void *unused,uint32_t block,size_t count,const uint8_t *in) {
    (void)unused;if((uint64_t)block+count>blocks(NULL))return -1;
    if(active) {unsigned at=writes++;if(fail_write>=0 && at>=(unsigned)fail_write)return -1;}
    memcpy(disk+(size_t)block*512u,in,count*512u);return 0;
}
static int sync_blocks(void *unused) {
    (void)unused;if(active) {unsigned at=syncs++;if(fail_sync>=0 && at>=(unsigned)fail_sync)return -1;}return 0;
}
static const struct kui_media_ops media={NULL,blocks,read_blocks,write_blocks,sync_blocks};
uint32_t kui_clock_fattime(void) {return ((2026u-1980u)<<25)|(11u<<21)|(5u<<16);}
static void logger(const char *format,...) {(void)format;}
static bool cancel(void) {return cancel_after>=0 && writes>=(unsigned)cancel_after;}
FRESULT __real_f_open(FIL *,const TCHAR *,BYTE);
FRESULT __wrap_f_open(FIL *file,const TCHAR *path,BYTE flags) {
    if(active && (flags&FA_WRITE))CHECK(!strncmp(path,KUI_UPDATE_SYSTEM "/",sizeof(KUI_UPDATE_SYSTEM)));
    return __real_f_open(file,path,flags);
}
FRESULT __real_f_getfree(const TCHAR *,DWORD *,FATFS **);
FRESULT __wrap_f_getfree(const TCHAR *path,DWORD *clusters,FATFS **volume) {
    FRESULT r=__real_f_getfree(path,clusters,volume);if(active && low_space && r==FR_OK)*clusters=0u;return r;
}
static void clear_faults(void) {
    active=low_space=false;fail_write=fail_sync=cancel_after=-1;writes=syncs=0u;effective_mode=0;
    memset(effective_bank,0,sizeof(effective_bank));memset(effective_hash,0,sizeof(effective_hash));
}
static void mount_volume(void) {kui_media_set(&media);CHECK(f_mount(&fs,"0:",1)==FR_OK);mounted=true;}
static void unmount_volume(void) {CHECK(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);mounted=false;}
static void reset(const uint8_t *saved) {
    clear_faults();if(mounted)unmount_volume();memcpy(disk,saved,disk_bytes);mount_volume();
}
static void parents(const char *path) {
    char copy[256];CHECK(strlen(path)<sizeof(copy));strcpy(copy,path);
    for(char *p=copy+3u;*p;p++)if(*p=='/') {
        *p=0;FRESULT r=f_mkdir(copy);CHECK(r==FR_OK || r==FR_EXIST);*p='/';
    }
}
static void file_write(const char *path,const void *data,size_t bytes) {
    parents(path);FIL file;UINT put=0u;CHECK(f_open(&file,path,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
    CHECK(f_write(&file,data,(UINT)bytes,&put)==FR_OK && put==bytes);CHECK(f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
}
static void equal(const char *path,const void *expected,size_t bytes) {
    FIL file;CHECK(f_open(&file,path,FA_READ)==FR_OK && f_size(&file)==bytes);
    uint8_t *actual=malloc(bytes?bytes:1u);CHECK(actual);UINT got=0;
    CHECK(f_read(&file,actual,(UINT)bytes,&got)==FR_OK && got==bytes && !memcmp(actual,expected,bytes));
    CHECK(f_close(&file)==FR_OK);free(actual);
}
static void absent(const char *path) {FILINFO info;FRESULT r=f_stat(path,&info);CHECK(r==FR_NO_FILE || r==FR_NO_PATH);}
static uint8_t *runtime(const char *build,unsigned variant,bool gui,bool engine) {
    uint8_t *file=calloc(1u,64u+BODY);CHECK(file);uint8_t *body=file+64u;
    for(unsigned i=0;i<BODY;i++)body[i]=(uint8_t)(i*37u+variant*19u+11u);
    if(gui) {
        static const uint8_t bank[16]=KUI_UPDATE_CAPABILITY_INITIALIZER;
        static const uint8_t handoff[KUI_UPDATE_HANDOFF_BYTES]=KUI_UPDATE_HANDOFF_INITIALIZER;
        static const uint8_t engine2[16]={'K','U','I','E','N','G','2','!',2,0,0,0,253,255,255,255};
        memcpy(body+32u,bank,sizeof(bank));memcpy(body+128u,handoff,sizeof(handoff));
        /* Cross both the installer's32 KiB and baseline's4 KiB boundaries. */
        if(engine)memcpy(body+32760u,engine2,sizeof(engine2));
    }
    memcpy(file,"KUIRUN1\0",8u);uf_put32(file+8u,1u);uf_put32(file+12u,64u);
    uf_put32(file+16u,BODY);uf_put32(file+20u,KUI_RUNTIME_ADDRESS);uf_put32(file+24u,KUI_RUNTIME_ADDRESS);
    uf_put32(file+28u,BODY);uf_put32(file+32u,kui_crc32(0u,body,BODY));memcpy(file+40u,build,12u);
    uf_put32(file+60u,kui_crc32(0u,file,60u));return file;
}
struct fixture {struct kui_update_package package;const uint8_t *files[KUI_UPDATE_MAX_ENTRIES];uint8_t *wire;size_t bytes;};
static void add(struct fixture *fixture,const char *path,uint8_t policy,const char *build,const void *data,size_t bytes) {
    uint32_t i=fixture->package.count++;CHECK(i<KUI_UPDATE_MAX_ENTRIES);struct kui_update_entry *entry=fixture->package.entries+i;
    CHECK(strlen(path)<sizeof(entry->path));strcpy(entry->path,path);entry->policy=policy;entry->bytes=bytes;
    if(build)memcpy(entry->build,build,13u);
    fixture->files[i]=data;
    if(policy!=KUI_UPDATE_KEEP && policy!=KUI_UPDATE_REMOVE)uf_hash(data,bytes,entry->sha256);
}
static void seal(struct fixture *fixture,const char *id,uint32_t version) {
    memcpy(fixture->package.build,id,13u);fixture->package.version=version;
    CHECK(kui_update_package_seal(&fixture->package)==KUI_UPDATE_OK);
    fixture->bytes=(size_t)fixture->package.file_bytes;fixture->wire=calloc(1u,fixture->bytes);CHECK(fixture->wire);
    size_t manifest=0u;CHECK(kui_update_receipt_encode(&fixture->package,fixture->wire,fixture->bytes,&manifest)==KUI_UPDATE_OK);
    for(uint32_t i=0;i<fixture->package.count;i++)if(fixture->package.entries[i].bytes)
        memcpy(fixture->wire+fixture->package.entries[i].payload_offset,fixture->files[i],(size_t)fixture->package.entries[i].bytes);
    CHECK(manifest==128u+fixture->package.count*kui_update_entry_bytes(&fixture->package));
}
static struct kui_update_activation latest(void) {
    struct kui_update_activation a;unsigned slot=0u;CHECK(kui_update_activation_latest(&a,&slot,cancel)==KUI_UPDATE_OK);return a;
}
static enum kui_update_result inspect(const char *path,struct kui_update_package *package) {
    struct kui_app_status status;active=true;enum kui_update_result r=kui_update_inspect(path,package,&status,logger,cancel,NULL);active=false;
    CHECK(status.complete && status.passed==(r==KUI_UPDATE_OK));return r;
}
static enum kui_update_result install(const char *path,const struct kui_update_pin *pin) {
    struct kui_app_status status;active=true;enum kui_update_result r=kui_update_install(path,pin,&status,logger,cancel,NULL);active=false;
    CHECK(status.complete && status.passed==(r==KUI_UPDATE_OK));return r;
}
static void bank_equal(const char *bank,const char *relative,const void *expected,size_t bytes) {
    char path[160];CHECK(kui_update_bank_path(bank,relative,path,sizeof(path)));equal(path,expected,bytes);
}
static void bank_absent(const char *bank,const char *relative) {
    char path[160];CHECK(kui_update_bank_path(bank,relative,path,sizeof(path)));absent(path);
}
static void verify(const char *bank) {
    struct kui_update_package package;struct kui_app_status status;
    CHECK(kui_update_bank_verify(bank,&package,&status,logger,cancel,NULL)==KUI_UPDATE_OK);
    CHECK(package.version==2u && !strcmp(package.build,bank));
    CHECK(package.entries[0].policy==KUI_UPDATE_SELF_KUI && !strcmp(package.entries[0].path,"runtime.kui"));
    for(uint32_t i=0;i<package.count;i++)CHECK(package.entries[i].policy!=KUI_UPDATE_KEEP);
}
static void fallback_mode(const char *bank,bool full) {
    struct kui_update_package package;struct kui_update_entry member;
    CHECK(kui_update_bank_receipt_read(bank,&package,cancel)==KUI_UPDATE_OK);
    CHECK(package.flags==(full?KUI_UPDATE_FULL:0u));
    CHECK(kui_update_package_member(&package,"apps/games/root.kui",&member));
    CHECK(member.policy==(full?KUI_UPDATE_REMOVE:KUI_UPDATE_KEEP));
}
static void unchanged(const uint8_t *root,const uint8_t *root_module,const uint8_t *user,size_t user_bytes) {
    equal(KUI_RUNTIME_PATH,root,64u+BODY);equal("0:/KUI/apps/games/root.kui",root_module,64u+BODY);
    equal("0:/KUI/apps/disc/retired.kui",root_module,64u+BODY);equal("0:/KUI/prefs/test.bin",user,user_bytes);
}
static void old_or_new(const char *old,const char *new_id) {
    struct kui_update_activation a=latest();CHECK(!strcmp(a.selected,old) || !strcmp(a.selected,new_id));verify(a.selected);
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *input=fopen(argv[1],"rb");CHECK(input && !fseek(input,0,SEEK_END));long length=ftell(input);
    CHECK(length>0 && !fseek(input,0,SEEK_SET));disk_bytes=(size_t)length;disk=malloc(disk_bytes);
    CHECK(disk && fread(disk,1,disk_bytes,input)==disk_bytes && !fclose(input));mount_volume();
    uint8_t *root=runtime("111111111111",1u,true,true),*root_old=runtime("111111111111",1u,true,false);
    uint8_t *root_module=runtime("aaaaaaaaaaaa",2u,false,false),*gui=runtime("222222222222",3u,true,true);
    uint8_t *gui_config=runtime("222222222222",4u,true,true),*game=runtime("333333333333",5u,false,false);
    uint8_t *music=runtime("666666666666",6u,false,false),*reader=runtime("444444444444",7u,false,false);
    uint8_t raw[1027],asset[2017],user[1024];for(unsigned i=0;i<sizeof(raw);i++)raw[i]=(uint8_t)(i*3u+1u);
    for(unsigned i=0;i<sizeof(asset);i++)asset[i]=(uint8_t)(i*5u+2u);
    memset(user,0xb7,sizeof(user));
    file_write(KUI_RUNTIME_PATH,root,64u+BODY);file_write("0:/KUI/apps/games/root.kui",root_module,64u+BODY);
    file_write("0:/KUI/apps/disc/retired.kui",root_module,64u+BODY);file_write("0:/KUI/prefs/test.bin",user,sizeof(user));
    struct fixture full={0},delta={0},config={0},bad_gui={0},legacy={0},bad_component={0},legacy_keep={0},large={0},repair={0};
    add(&full,"runtime.kui",KUI_UPDATE_SELF_KUI,"222222222222",gui,64u+BODY);
    add(&full,"apps/games/cdda.kui",KUI_UPDATE_SELF_KUI,"333333333333",game,64u+BODY);
    add(&full,"apps/music/player.kui",KUI_UPDATE_SELF_KUI,"666666666666",music,64u+BODY);
    add(&full,"apps/games/helper.bin",KUI_UPDATE_RAW,NULL,raw,sizeof(raw));
    add(&full,"assets/theme/icons/controller.dat",KUI_UPDATE_RAW,NULL,asset,sizeof(asset));
    add(&full,"apps/disc/retired.kui",KUI_UPDATE_REMOVE,NULL,NULL,0u);full.package.flags=KUI_UPDATE_FULL;
    seal(&full,"bbbbbbbbbbbb",2u);
    struct fixture root_mode=full;root_mode.wire=NULL;root_mode.package.flags=0u;seal(&root_mode,"121212121212",2u);
    add(&delta,"apps/games/cdda.kui",KUI_UPDATE_SELF_KUI,"444444444444",reader,64u+BODY);
    add(&delta,"apps/games/helper.bin",KUI_UPDATE_KEEP,NULL,NULL,0u);seal(&delta,"cccccccccccc",2u);
    add(&config,"runtime.kui",KUI_UPDATE_SELF_KUI,"222222222222",gui_config,64u+BODY);
    add(&config,"apps/games/helper.bin",KUI_UPDATE_REMOVE,NULL,NULL,0u);seal(&config,"dddddddddddd",2u);
    add(&bad_gui,"runtime.kui",KUI_UPDATE_SELF_KUI,"111111111111",root_old,64u+BODY);seal(&bad_gui,"eeeeeeeeeeee",2u);
    add(&bad_component,"apps/games/cdda.kui",KUI_UPDATE_SELF_KUI,"555555555555",reader,64u+BODY);
    seal(&bad_component,"ffffffffffff",2u);
    add(&legacy,"runtime.kui",KUI_UPDATE_REPLACE,NULL,root_old,64u+BODY);seal(&legacy,"111111111111",1u);
    add(&legacy_keep,"runtime.kui",KUI_UPDATE_REPLACE,NULL,gui_config,64u+BODY);
    add(&legacy_keep,"apps/games/cdda.kui",KUI_UPDATE_INHERIT_ROOT,NULL,NULL,0u);seal(&legacy_keep,"222222222222",1u);
    for(unsigned i=0;i<KUI_UPDATE_MAX_ENTRIES;i++) {
        char path[80];snprintf(path,sizeof(path),"assets/capacity/file_%03u.bin",i);
        add(&large,path,KUI_UPDATE_RAW,NULL,asset,0u);
    }
    seal(&large,"999999999999",2u);
    add(&repair,"runtime.kui",KUI_UPDATE_SELF_KUI,"222222222222",gui_config,64u+BODY);
    repair.package.flags=KUI_UPDATE_FULL;seal(&repair,"888888888888",2u);
    file_write(FULL,full.wire,full.bytes);file_write(READER,delta.wire,delta.bytes);file_write(CONFIG,config.wire,config.bytes);
    struct kui_update_package package;struct kui_update_pin full_pin,delta_pin,config_pin;
    CHECK(inspect(FULL,&package)==KUI_UPDATE_OK);kui_update_package_pin(&package,&full_pin);
    CHECK(inspect(READER,&package)==KUI_UPDATE_OK && package.count==2u);kui_update_package_pin(&package,&delta_pin);
    CHECK(inspect(CONFIG,&package)==KUI_UPDATE_OK);kui_update_package_pin(&package,&config_pin);
    /* Incoming optional-GUI metadata is not a completed installed snapshot.
     * Selection must refuse it rather than promise a boot that will fall back. */
    char invalid_receipt[160];snprintf(invalid_receipt,sizeof(invalid_receipt),KUI_UPDATE_SYSTEM "/%s/package.bin",delta.package.build);
    file_write(invalid_receipt,delta.wire,128u+delta.package.count*kui_update_entry_bytes(&delta.package));
    clear_faults();struct kui_app_status rejected_status;active=true;
    CHECK(kui_update_bank_verify(delta.package.build,&package,&rejected_status,logger,cancel,NULL)==KUI_UPDATE_FORMAT);
    CHECK(kui_update_select(delta.package.build,delta.package.manifest_sha256,&rejected_status,logger,cancel,NULL)==KUI_UPDATE_FORMAT);
    active=false;CHECK(!writes);
    file_write(BAD,bad_gui.wire,bad_gui.bytes);clear_faults();CHECK(inspect(BAD,&package)==KUI_UPDATE_ENGINE && !writes);
    file_write(BAD,bad_component.wire,bad_component.bytes);CHECK(inspect(BAD,&package)==KUI_UPDATE_RUNTIME && !writes);
    size_t raw_at=(size_t)full.package.entries[3].payload_offset+500u;full.wire[raw_at]^=1u;
    file_write(BAD,full.wire,full.bytes);CHECK(inspect(BAD,&package)==KUI_UPDATE_CHECKSUM && !writes);full.wire[raw_at]^=1u;
    file_write(BAD,legacy.wire,legacy.bytes);CHECK(inspect(BAD,&package)==KUI_UPDATE_OK);struct kui_update_pin legacy_pin;
    kui_update_package_pin(&package,&legacy_pin);clear_faults();CHECK(install(BAD,&legacy_pin)==KUI_UPDATE_ENGINE && !writes);
    file_write(KUI_RUNTIME_PATH,root_old,64u+BODY);clear_faults();CHECK(install(FULL,&full_pin)==KUI_UPDATE_ENGINE && !writes);
    file_write(KUI_RUNTIME_PATH,root,64u+BODY);
    uint8_t *saved=malloc(disk_bytes);CHECK(saved);memcpy(saved,disk,disk_bytes);
    /* The first reader-only delivery also works with no prior bank: materialize
     * the capable root GUI, retain omitted root roles, and preserve absence. */
    clear_faults();CHECK(install(READER,&delta_pin)==KUI_UPDATE_OK);struct kui_update_activation first=latest();verify(first.selected);
    fallback_mode(first.selected,false);
    bank_equal(first.selected,"runtime.kui",root,64u+BODY);bank_equal(first.selected,"apps/games/cdda.kui",reader,64u+BODY);
    bank_absent(first.selected,"apps/games/helper.bin");unchanged(root,root_module,user,sizeof(user));reset(saved);
    file_write(BAD,root_mode.wire,root_mode.bytes);CHECK(inspect(BAD,&package)==KUI_UPDATE_OK);struct kui_update_pin root_mode_pin;
    kui_update_package_pin(&package,&root_mode_pin);CHECK(install(BAD,&root_mode_pin)==KUI_UPDATE_OK);
    char root_mode_bank[13];memcpy(root_mode_bank,latest().selected,13u);fallback_mode(root_mode_bank,false);reset(saved);
    clear_faults();CHECK(install(FULL,&full_pin)==KUI_UPDATE_OK);
    struct kui_update_activation a=latest();char full_bank[13];memcpy(full_bank,a.selected,13u);
    CHECK(strcmp(full_bank,full.package.build) && strcmp(full_bank,"222222222222") && strcmp(full_bank,root_mode_bank));verify(full_bank);
    fallback_mode(full_bank,true);
    bank_equal(full_bank,"apps/games/cdda.kui",game,64u+BODY);bank_absent(full_bank,"apps/disc/retired.kui");
    memcpy(saved,disk,disk_bytes);
    file_write(BAD,large.wire,large.bytes);clear_faults();CHECK(inspect(BAD,&package)==KUI_UPDATE_OK);
    struct kui_update_pin large_pin;kui_update_package_pin(&package,&large_pin);CHECK(install(BAD,&large_pin)==KUI_UPDATE_LIMIT && !writes);
    file_write(BAD,legacy_keep.wire,legacy_keep.bytes);CHECK(inspect(BAD,&package)==KUI_UPDATE_OK);
    struct kui_update_pin legacy_keep_pin;kui_update_package_pin(&package,&legacy_keep_pin);clear_faults();
    CHECK(install(BAD,&legacy_keep_pin)==KUI_UPDATE_OK);struct kui_update_activation legacy_applied=latest();verify(legacy_applied.selected);
    fallback_mode(legacy_applied.selected,true);
    CHECK(strcmp(legacy_applied.selected,legacy_keep.package.build));bank_equal(legacy_applied.selected,"runtime.kui",gui_config,64u+BODY);
    bank_equal(legacy_applied.selected,"apps/games/cdda.kui",game,64u+BODY);bank_equal(legacy_applied.selected,"apps/games/helper.bin",raw,sizeof(raw));
    reset(saved);
    uint64_t needed=0u,available=0u;clear_faults();active=true;
    CHECK(kui_update_plan_space(&delta.package,&needed,&available,cancel)==KUI_UPDATE_OK && needed>0u && available>=needed);active=false;
    clear_faults();low_space=true;CHECK(install(READER,&delta_pin)==KUI_UPDATE_SPACE && !writes);clear_faults();
    CHECK(install(READER,&delta_pin)==KUI_UPDATE_OK);unsigned full_writes=writes,full_syncs=syncs;
    a=latest();char delta_bank[13];memcpy(delta_bank,a.selected,13u);CHECK(strcmp(delta_bank,full_bank));verify(delta_bank);
    fallback_mode(delta_bank,true);
    CHECK(!strcmp(a.previous,full_bank));bank_equal(delta_bank,"runtime.kui",gui,64u+BODY);
    bank_equal(delta_bank,"apps/games/cdda.kui",reader,64u+BODY);bank_equal(delta_bank,"apps/music/player.kui",music,64u+BODY);
    bank_equal(delta_bank,"apps/games/helper.bin",raw,sizeof(raw));bank_equal(delta_bank,"assets/theme/icons/controller.dat",asset,sizeof(asset));
    bank_absent(delta_bank,"apps/disc/retired.kui");unchanged(root,root_module,user,sizeof(user));
    clear_faults();low_space=true;CHECK(install(READER,&delta_pin)==KUI_UPDATE_OK && !writes);clear_faults();
    CHECK(latest().sequence==a.sequence);
    /* A root engine1 cannot be hidden by the selected engine2 GUI. */
    file_write(KUI_RUNTIME_PATH,root_old,64u+BODY);clear_faults();CHECK(install(CONFIG,&config_pin)==KUI_UPDATE_ENGINE && !writes);
    file_write(KUI_RUNTIME_PATH,root,64u+BODY);clear_faults();CHECK(install(CONFIG,&config_pin)==KUI_UPDATE_OK);
    a=latest();char config_bank[13];memcpy(config_bank,a.selected,13u);CHECK(strcmp(config_bank,delta_bank));verify(config_bank);
    fallback_mode(config_bank,true);
    bank_equal(config_bank,"runtime.kui",gui_config,64u+BODY);bank_equal(config_bank,"apps/games/cdda.kui",reader,64u+BODY);
    bank_absent(config_bank,"apps/games/helper.bin");bank_absent(config_bank,"apps/disc/retired.kui");
    struct kui_app_status status;CHECK(kui_update_rollback(&status,logger,cancel,NULL)==KUI_UPDATE_OK);
    CHECK(!strcmp(latest().selected,delta_bank));bank_equal(delta_bank,"apps/games/helper.bin",raw,sizeof(raw));
    /* Every physical write/sync cut and every write-boundary cancellation is
     * recoverable, retaining the prior complete bank or the complete new one. */
    for(unsigned kind=0;kind<3u;kind++) {
        unsigned limit=kind==1u?full_syncs:full_writes;
        for(unsigned cut=0;cut<=limit;cut++) {
            reset(saved);if(kind==0u)fail_write=(int)cut;else if(kind==1u)fail_sync=(int)cut;else cancel_after=(int)cut;
            enum kui_update_result r=install(READER,&delta_pin);CHECK(r!=KUI_UPDATE_FORMAT && r!=KUI_UPDATE_COHORT);
            clear_faults();unmount_volume();mount_volume();old_or_new(full_bank,delta_bank);
            unchanged(root,root_module,user,sizeof(user));bank_equal(full_bank,"apps/games/cdda.kui",game,64u+BODY);
            CHECK(install(READER,&delta_pin)==KUI_UPDATE_OK);CHECK(!strcmp(latest().selected,delta_bank));verify(delta_bank);
        }
    }
    reset(saved);char corrupt[160];CHECK(kui_update_bank_path(full_bank,"apps/games/helper.bin",corrupt,sizeof(corrupt)));
    raw[500]^=1u;file_write(corrupt,raw,sizeof(raw));clear_faults();CHECK(install(READER,&delta_pin)==KUI_UPDATE_CHECKSUM && !writes);raw[500]^=1u;
    /* Corruption in a preserved source blocks before writes. An explicit
     * replacement/removal can still repair it through a separate new bank. */
    clear_faults();CHECK(install(CONFIG,&config_pin)==KUI_UPDATE_OK);bank_absent(latest().selected,"apps/games/helper.bin");verify(latest().selected);
    reset(saved);CHECK(kui_update_bank_path(full_bank,"apps/games/cdda.kui",corrupt,sizeof(corrupt)));
    game[500]^=1u;file_write(corrupt,game,64u+BODY);game[500]^=1u;clear_faults();CHECK(install(READER,&delta_pin)==KUI_UPDATE_OK);
    bank_equal(latest().selected,"apps/games/cdda.kui",reader,64u+BODY);verify(latest().selected);
    reset(saved);file_write(BAD,repair.wire,repair.bytes);CHECK(inspect(BAD,&package)==KUI_UPDATE_OK);
    struct kui_update_pin repair_pin;kui_update_package_pin(&package,&repair_pin);
    snprintf(corrupt,sizeof(corrupt),KUI_UPDATE_SYSTEM "/%s/package.bin",full_bank);file_write(corrupt,"broken",6u);
    clear_faults();effective_mode=-1;CHECK(install(BAD,&repair_pin)==KUI_UPDATE_OK);verify(latest().selected);fallback_mode(latest().selected,true);
    bank_equal(latest().selected,"runtime.kui",gui_config,64u+BODY);bank_absent(latest().selected,"apps/games/cdda.kui");
    unchanged(root,root_module,user,sizeof(user));
    /* A saved selection can still name a rejected bank after cold fallback.
     * Partial updates must clone actual root, not load that broken receipt. */
    reset(saved);snprintf(corrupt,sizeof(corrupt),KUI_UPDATE_SYSTEM "/%s/package.bin",full_bank);file_write(corrupt,"broken",6u);
    clear_faults();effective_mode=-1;active=true;
    CHECK(kui_update_plan_space(&delta.package,&needed,&available,cancel)==KUI_UPDATE_IO);active=false;
    CHECK(install(READER,&delta_pin)==KUI_UPDATE_IO && !writes);
    effective_mode=1;active=true;
    CHECK(kui_update_plan_space(&delta.package,&needed,&available,cancel)==KUI_UPDATE_OK && needed>0u);active=false;
    CHECK(install(READER,&delta_pin)==KUI_UPDATE_OK);verify(latest().selected);fallback_mode(latest().selected,false);
    bank_equal(latest().selected,"runtime.kui",root,64u+BODY);bank_equal(latest().selected,"apps/games/cdda.kui",reader,64u+BODY);
    bank_absent(latest().selected,"apps/music/player.kui");bank_absent(latest().selected,"apps/games/helper.bin");
    unchanged(root,root_module,user,sizeof(user));
    /* A pending root selection likewise cannot discard the running bank's
     * software before restart; publish still guards the saved selection. */
    reset(saved);CHECK(kui_update_bank_receipt_read(full_bank,&package,cancel)==KUI_UPDATE_OK);
    CHECK(kui_update_select("",NULL,&status,logger,cancel,NULL)==KUI_UPDATE_OK);clear_faults();
    effective_mode=1;memcpy(effective_bank,full_bank,13u);memcpy(effective_hash,package.manifest_sha256,32u);
    CHECK(install(READER,&delta_pin)==KUI_UPDATE_OK);CHECK(!strcmp(latest().selected,delta_bank) && !latest().previous[0]);
    bank_equal(delta_bank,"runtime.kui",gui,64u+BODY);bank_equal(delta_bank,"apps/music/player.kui",music,64u+BODY);fallback_mode(delta_bank,true);
    reset(saved);clear_faults();CHECK(install(READER,&delta_pin)==KUI_UPDATE_OK);
    printf("PASS updater v2: %u checks, FULL/partial identity and absence, independent component builds, reader-only retention, raw/nested assets, tombstones, same-build GUI configs, v1 translation, full receipt repair, effective root/bank selection, blocked-state guard, capacity guard, rollback, engine floor, %u write cuts, %u sync cuts, %u cancellation cuts, verified retries and unchanged root/user data\n",checks,full_writes+1u,full_syncs+1u,full_writes+1u);
    unmount_volume();free(saved);free(root);free(root_old);free(root_module);free(gui);free(gui_config);free(game);free(music);free(reader);
    free(full.wire);free(delta.wire);free(config.wire);free(bad_gui.wire);free(legacy.wire);free(bad_component.wire);
    free(legacy_keep.wire);free(large.wire);free(root_mode.wire);free(repair.wire);free(disk);return 0;
}
