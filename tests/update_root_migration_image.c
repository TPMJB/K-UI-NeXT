/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the exact manual root repair using actual release bytes and
 * production FatFs/update/boot paths. SH payloads are never executed. */
#include "kui/update.h"
#include "kui/update_boot.h"
#include "kui/update_handoff.h"
#include "kui/update_zip.h"
#include "kui/boot_image.h"
#include "kui/ext4_boot.h"
#include "kui/storage_policy.h"
#include "kui/media.h"
#include "ff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define PACKAGE "0:/KUI/updates/release.kup"
#define BACKUP "0:/KUI/system-before-updater-repair"
#define OLD_PACKAGE "0:/KUI/updates/old26.kup"
static uint8_t *disk;static size_t disk_bytes;static unsigned checks,writes;static uint64_t read_bytes;
static FATFS fs;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Root migration failed %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint64_t blocks(void *unused) {(void)unused;return disk_bytes/512u;}
static int read_blocks(void *unused,uint32_t block,size_t count,uint8_t *out) {
    (void)unused;if((uint64_t)block+count>blocks(NULL))return -1;
    read_bytes+=(uint64_t)count*512u;memcpy(out,disk+(size_t)block*512u,count*512u);return 0;
}
static int write_blocks(void *unused,uint32_t block,size_t count,const uint8_t *in) {
    (void)unused;if((uint64_t)block+count>blocks(NULL))return -1;
    ++writes;memcpy(disk+(size_t)block*512u,in,count*512u);return 0;
}
static int sync_blocks(void *unused) {(void)unused;return 0;}
static const struct kui_media_ops media={NULL,blocks,read_blocks,write_blocks,sync_blocks};
uint32_t kui_clock_fattime(void) {return ((2026u-1980u)<<25)|(11u<<21)|(4u<<16);}
static void logger(const char *format,...) {(void)format;}
static bool cancel(void) {return false;}
static void parents(const char *path) {
    char copy[256];CHECK(strlen(path)<sizeof(copy));strcpy(copy,path);
    for(char *p=copy+3u;*p;p++)if(*p=='/') {
        *p=0;FRESULT r=f_mkdir(copy);CHECK(r==FR_OK || r==FR_EXIST);*p='/';
    }
}
struct digest {uint64_t bytes;uint8_t sha256[32];};
struct root_file {bool present;struct digest digest;};
static bool host_present(const char *path) {
    FILE *file=fopen(path,"rb");
    if(file) {CHECK(!fclose(file));return true;}
    CHECK(errno==ENOENT);return false;
}
static void absent(const char *path) {
    FILINFO info;FRESULT result=f_stat(path,&info);
    CHECK(result==FR_NO_FILE || result==FR_NO_PATH);
}
static struct digest copy_host(const char *source,const char *destination) {
    FILE *input=fopen(source,"rb");CHECK(input);parents(destination);
    FIL output;CHECK(f_open(&output,destination,FA_WRITE|FA_CREATE_NEW)==FR_OK);
    uint8_t buffer[32768];struct digest value={0};struct kui_sha256 hash;kui_sha256_init(&hash);
    size_t n;while((n=fread(buffer,1,sizeof(buffer),input))!=0u) {
        UINT done=0;CHECK(f_write(&output,buffer,(UINT)n,&done)==FR_OK && done==n);
        kui_sha256_update(&hash,buffer,n);value.bytes+=n;
    }
    CHECK(!ferror(input) && !fclose(input));CHECK(f_sync(&output)==FR_OK && f_close(&output)==FR_OK);
    kui_sha256_digest(&hash,value.sha256);return value;
}
static struct digest file_digest(const char *path) {
    FIL input;CHECK(f_open(&input,path,FA_READ)==FR_OK);
    struct digest value={0};struct kui_sha256 hash;kui_sha256_init(&hash);uint8_t buffer[32768];
    for(;;) {
        UINT got=0;CHECK(f_read(&input,buffer,sizeof(buffer),&got)==FR_OK);if(!got)break;
        kui_sha256_update(&hash,buffer,got);value.bytes+=got;
    }
    CHECK(value.bytes==f_size(&input) && f_close(&input)==FR_OK);kui_sha256_digest(&hash,value.sha256);return value;
}
static void same_digest(const char *path,const struct digest *expected) {
    struct digest actual=file_digest(path);
    CHECK(actual.bytes==expected->bytes && !memcmp(actual.sha256,expected->sha256,32u));
}
static const char *const user_paths[]={"0:/KUI/vmu/cards/v1234/card.vmd",
    "0:/KUI/vmu-test/p0012/card.journal","0:/KUI/prefs/vmu.bin","0:/Games/untouched/disc.gdi"};
static struct digest user_digest[sizeof(user_paths)/sizeof(user_paths[0])];
static void seed_users(void) {
    uint8_t buffer[1024];for(unsigned i=0;i<sizeof(buffer);i++)buffer[i]=(uint8_t)(i*53u+17u);
    for(unsigned i=0;i<sizeof(user_paths)/sizeof(user_paths[0]);i++) {
        parents(user_paths[i]);FIL file;UINT done=0;
        CHECK(f_open(&file,user_paths[i],FA_WRITE|FA_CREATE_NEW)==FR_OK);
        CHECK(f_write(&file,buffer,sizeof(buffer),&done)==FR_OK && done==sizeof(buffer));CHECK(f_close(&file)==FR_OK);
        user_digest[i]=file_digest(user_paths[i]);
    }
}
static struct kui_update_activation latest(void) {
    struct kui_update_activation value;unsigned slot=0;
    CHECK(kui_update_activation_latest(&value,&slot,cancel)==KUI_UPDATE_OK);return value;
}
static void preserved(const struct root_file *root,unsigned count) {
    char path[192];for(unsigned i=0;i<count;i++) {
        snprintf(path,sizeof(path),"0:/KUI/%s",kui_update_catalog_path(i));
        if(root[i].present)same_digest(path,&root[i].digest);else absent(path);
    }
    for(unsigned i=0;i<sizeof(user_paths)/sizeof(user_paths[0]);i++)same_digest(user_paths[i],user_digest+i);
}

/* These cases must stay on FAT32/exFAT. Fail if the bootstrap ever tries the
 * unrelated ext4 fallback instead of the production FAT image reader. */
enum kui_ext4_boot_result kui_ext4_boot_read_path(const struct kui_media_ops *raw,
    const char *path,struct kui_runtime_image *out,kui_log_fn log,kui_cancel_fn stop) {
    (void)raw;(void)path;(void)out;(void)log;(void)stop;CHECK(false);return KUI_EXT4_BOOT_INVALID;
}
struct saved_file {char relative[192];struct digest digest;};
static struct saved_file backup[128];static unsigned backup_count;
static void capture_tree(const char *directory,const char *relative) {
    DIR dir;FILINFO info;CHECK(f_opendir(&dir,directory)==FR_OK);
    for(;;) {
        CHECK(f_readdir(&dir,&info)==FR_OK);if(!info.fname[0])break;
        char path[256],name[192];
        CHECK(snprintf(path,sizeof(path),"%s/%s",directory,info.fname)<(int)sizeof(path));
        CHECK(snprintf(name,sizeof(name),"%s%s%s",relative,*relative?"/":"",info.fname)<(int)sizeof(name));
        if(info.fattrib&AM_DIR)capture_tree(path,name);
        else {
            CHECK(backup_count<sizeof(backup)/sizeof(backup[0]));
            strcpy(backup[backup_count].relative,name);
            backup[backup_count++].digest=file_digest(path);
        }
    }
    CHECK(f_closedir(&dir)==FR_OK);
}
static void backup_preserved(void) {
    char path[256];for(unsigned i=0;i<backup_count;i++) {
        CHECK(snprintf(path,sizeof(path),BACKUP "/%s",backup[i].relative)<(int)sizeof(path));
        same_digest(path,&backup[i].digest);
    }
}
static void root_modules(const struct root_file *root,unsigned count) {
    for(unsigned i=1u;i<count;i++) {
        char logical[192],resolved[192];struct kui_update_runtime_stream stream;
        snprintf(logical,sizeof(logical),"0:/KUI/%s",kui_update_catalog_path(i));
        CHECK(kui_update_runtime_resolve(logical,resolved,sizeof(resolved),&stream));
        CHECK(!strcmp(logical,resolved) && !stream.pinned);
        if(root[i].present)same_digest(logical,&root[i].digest);else absent(logical);
    }
}
static void mount_volume(void) {kui_media_set(&media);CHECK(f_mount(&fs,"0:",1)==FR_OK);}
static void unmount_volume(void) {CHECK(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);}
static void bootstrap(enum kui_boot_mode mode,const char *build) {
    unmount_volume();kui_update_boot_reset();unsigned before=writes;
    struct kui_runtime_image image={0};
    CHECK(kui_boot_image_read_mode(&media,KUI_STORAGE_SCI,mode,&image,logger,cancel)==KUI_RUNTIME_OK);
    CHECK(image.data && !strcmp(image.info.build,build) && writes==before);
    kui_runtime_free(&image);mount_volume();
}
int main(int argc,char **argv) {
    CHECK(argc==6);
    FILE *input=fopen(argv[1],"rb");CHECK(input && !fseek(input,0,SEEK_END));long length=ftell(input);
    CHECK(length>0 && !fseek(input,0,SEEK_SET));disk_bytes=(size_t)length;disk=malloc(disk_bytes);
    CHECK(disk && fread(disk,1,disk_bytes,input)==disk_bytes && !fclose(input));mount_volume();
    unsigned count=(unsigned)kui_update_catalog_count();
    struct root_file *root=calloc(count,sizeof(*root));CHECK(root);
    unsigned present=0u;char host[1024],path[192];
    for(unsigned i=0;i<count;i++) {
        CHECK(snprintf(host,sizeof(host),"%s/%s",argv[2],kui_update_catalog_path(i))<(int)sizeof(host));
        snprintf(path,sizeof(path),"0:/KUI/%s",kui_update_catalog_path(i));
        root[i].present=host_present(host);if(!i)CHECK(root[i].present);
        if(root[i].present) {root[i].digest=copy_host(host,path);++present;}else absent(path);
    }
    seed_users();CHECK(snprintf(host,sizeof(host),"%s/runtime.kui",argv[2])<(int)sizeof(host));
    struct digest boot_digest=copy_host(host,"0:/KUI/boot.kui");
    kui_update_boot_reset();struct kui_runtime_image image={0};
    CHECK(kui_runtime_read(KUI_RUNTIME_PATH,&image,logger,cancel)==KUI_RUNTIME_OK);
    char original_build[13];memcpy(original_build,image.info.build,sizeof(original_build));kui_runtime_free(&image);
    (void)copy_host(argv[3],OLD_PACKAGE);(void)copy_host(argv[5],PACKAGE);
    struct kui_update_package *old=malloc(sizeof(*old)),*future=malloc(sizeof(*future));CHECK(old && future);
    struct kui_app_status status;struct kui_update_pin pin;
    CHECK(kui_update_inspect(OLD_PACKAGE,old,&status,logger,cancel,NULL)==KUI_UPDATE_OK && old->count==26u);
    kui_update_package_pin(old,&pin);
    CHECK(kui_update_install(OLD_PACKAGE,&pin,&status,logger,cancel,NULL)==KUI_UPDATE_OK);
    /* Populate both activation slots and retain the old26 bank as selected. */
    CHECK(kui_update_select("",NULL,&status,logger,cancel,NULL)==KUI_UPDATE_OK);
    CHECK(kui_update_select(old->build,old->manifest_sha256,&status,logger,cancel,NULL)==KUI_UPDATE_OK);
    CHECK(!strcmp(latest().selected,old->build));
    struct kui_update_boot_view view;unsigned before=writes;
    CHECK(kui_update_boot_read(original_build,KUI_STORAGE_SCI,&image,&view,logger,cancel)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(image.data && !strcmp(image.info.build,old->build) && writes==before);kui_runtime_free(&image);
    kui_update_boot_reset();(void)copy_host(argv[3],KUI_UPDATE_SYSTEM "/update-stage/current.kup");
    capture_tree(KUI_UPDATE_SYSTEM,"");CHECK(backup_count>=5u);absent(BACKUP);
    /* Exact documented repair: archive software selection/banks; replace only
     * the root GUI. No button sampling or old updater participates. */
    CHECK(f_rename(KUI_UPDATE_SYSTEM,BACKUP)==FR_OK);absent(KUI_UPDATE_SYSTEM);
    CHECK(f_unlink(KUI_RUNTIME_PATH)==FR_OK);root[0].digest=copy_host(argv[4],KUI_RUNTIME_PATH);
    CHECK(kui_runtime_read(KUI_RUNTIME_PATH,&image,logger,cancel)==KUI_RUNTIME_OK);
    char repaired[13];memcpy(repaired,image.info.build,sizeof(repaired));kui_runtime_free(&image);
    CHECK(strcmp(original_build,repaired) && strcmp(old->build,repaired));
    /* An optional old boot.kui really does win CD AUTOBOOT. Preserve it under
     * an unused name, then exercise the actual bootstrap reader again. */
    bootstrap(KUI_BOOT_MODE_AUTOBOOT,original_build);
    absent("0:/KUI/boot-before-updater-repair.kui");
    CHECK(f_rename("0:/KUI/boot.kui","0:/KUI/boot-before-updater-repair.kui")==FR_OK);
    same_digest("0:/KUI/boot-before-updater-repair.kui",&boot_digest);
    bootstrap(KUI_BOOT_MODE_AUTOBOOT,repaired);bootstrap(KUI_BOOT_MODE_NORMAL,repaired);
    before=writes;
    CHECK(kui_update_boot_read(repaired,KUI_STORAGE_SCI,&image,&view,logger,cancel)==KUI_UPDATE_BOOT_BASELINE);
    CHECK(!image.data && !view.bank[0] && !strcmp(view.build,repaired) && writes==before);
    absent(KUI_UPDATE_SYSTEM);root_modules(root,count);backup_preserved();preserved(root,count);
    CHECK(kui_update_boot_reload(repaired,KUI_STORAGE_SCI,&image,&view,logger,cancel)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(image.data && !view.bank[0] && !strcmp(image.info.build,repaired) && writes==before);
    kui_runtime_free(&image);kui_update_boot_reload_abort();root_modules(root,count);
    /* Future package recognition and installation recreate only the live
     * system directory; none of the archived selections/banks are modified. */
    CHECK(kui_update_inspect(OLD_PACKAGE,future,&status,logger,cancel,NULL)==KUI_UPDATE_OK && future->count==26u);
    CHECK(kui_update_inspect(PACKAGE,future,&status,logger,cancel,NULL)==KUI_UPDATE_OK && future->count==34u);
    CHECK(!strcmp(future->build,repaired));kui_update_package_pin(future,&pin);
    CHECK(kui_update_install(PACKAGE,&pin,&status,logger,cancel,NULL)==KUI_UPDATE_OK);
    CHECK(!strcmp(latest().selected,repaired));backup_preserved();preserved(root,count);
    same_digest("0:/KUI/boot-before-updater-repair.kui",&boot_digest);
    printf("PASS deterministic root migration: %u checks; selected old26 %s, exact root %s, %u archived bank/activation/cache files, %u present/%u absent root files, optional boot override rename, actual CD AUTOBOOT/Start K-UI load, no-B baseline startup, root restart, old26/new34 recognition, future system recreation, unchanged backups/apps/user data\n",checks,old->build,repaired,backup_count,present,count-present);
    kui_update_boot_reset();unmount_volume();free(old);free(future);free(root);free(disk);return 0;
}
