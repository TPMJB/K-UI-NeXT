/* SPDX-License-Identifier: GPL-3.0-only */
/* Install actual release bytes through production FatFs/update/boot/resolver
 * paths. Runtime images are inspected and freed, never executed on the host. */
#include "kui/update.h"
#include "kui/update_boot.h"
#include "kui/update_handoff.h"
#include "kui/update_zip.h"
#include "kui/native_vmu_standard.h"
#include "kui/vmu_game_card.h"
#include "kui/storage_policy.h"
#include "kui/media.h"
#include "ff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define PACKAGE "0:/KUI/updates/release.kup"
#define ARCHIVE "0:/KUI/updates/test/variant.zip"
static uint8_t *disk;static size_t disk_bytes;static unsigned checks,writes;static uint64_t read_bytes;
static FATFS fs;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Release updater failed %u: %s\n",__LINE__,#x);exit(1);}}while(0)
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
static void modules(const struct kui_update_package *package,const struct root_file *root) {
    for(unsigned i=1u;i<kui_update_catalog_count();i++) {
        char logical[192],resolved[192];snprintf(logical,sizeof(logical),"0:/KUI/%s",kui_update_catalog_path(i));
        struct kui_update_runtime_stream stream;
        CHECK(kui_update_runtime_resolve(logical,resolved,sizeof(resolved),&stream));
        struct kui_update_entry member={0};
        if(package)CHECK(kui_update_package_member(package,kui_update_catalog_path(i),&member));
        bool inherited=package && member.policy==KUI_UPDATE_INHERIT_ROOT;
        if(package && !inherited) {
            char expected[192];CHECK(kui_update_bank_path(package->build,kui_update_catalog_path(i),expected,sizeof(expected)));
            CHECK(stream.pinned && !strcmp(resolved,expected));
        } else {
            CHECK(!stream.pinned && !stream.bytes && !stream.build[0] && !strcmp(resolved,logical));
            if(inherited) {
                char bank[192];CHECK(!member.bytes);
                CHECK(kui_update_bank_path(package->build,kui_update_catalog_path(i),bank,sizeof(bank)));absent(bank);
            }
        }
        bool present=(package && !inherited) || root[i].present;
        const char *relative=kui_update_catalog_path(i);size_t length=strlen(relative);
        if(length>=4u && !strcmp(relative+length-4u,".bin")) {
            /* CE helpers are raw, separately qualified files, not .kui
             * envelopes. GUI-only banks inherit their exact bytes/absence. */
            CHECK(!package || inherited);
            if(present)same_digest(logical,&root[i].digest);else absent(logical);
            continue;
        }
        struct kui_runtime_image image={0};
        enum kui_runtime_result loaded=kui_runtime_read(logical,&image,logger,cancel);
        if(!present) {CHECK(loaded==KUI_RUNTIME_IO && !image.data);absent(logical);continue;}
        CHECK(loaded==KUI_RUNTIME_OK && image.data);
        if(package && !inherited)CHECK(!strcmp(image.info.build,kui_update_entry_build(package->build,&member)));
        if((!package || inherited) && !strcmp(kui_update_catalog_path(i),"apps/games/retail-boot.kui")) {
            size_t profile_slot=0u;CHECK(kui_native_vmu_standard_recognize(&image,&profile_slot));
            CHECK(profile_slot==kui_vmu_game_card_profile_slot(image.data,image.info.payload_bytes));
        }
        kui_runtime_free(&image);
        if(!package || inherited)same_digest(logical,&root[i].digest);
    }
}
static void context_from_chain(const struct kui_runtime_image *image,struct kui_update_handoff_context *context) {
    unsigned found=0u;const uint8_t *bytes=image->data;
    for(uint32_t i=0;i+KUI_UPDATE_HANDOFF_BYTES<=image->info.payload_bytes;i++) {
        if(bytes[i]!='K')continue;
        struct kui_update_handoff record;
        if(!kui_update_handoff_decode(bytes+i,&record))continue;
        CHECK(record.payload_offset==i);memcpy(context->wire,bytes+i,sizeof(context->wire));context->payload_offset=i;++found;
    }
    CHECK(found==1u);
}
static unsigned legacy_build_guards(const struct kui_update_package *package) {
    unsigned historical=0u,expected=0u;
    for(unsigned i=1u;i<package->count;i++)
        if(package->entries[i].policy==KUI_UPDATE_APPROVED_LEGACY)++expected;
    for(unsigned i=1u;i<package->count;i++)if(package->entries[i].policy==KUI_UPDATE_APPROVED_LEGACY) {
        ++historical;char path[192],logical[192];const struct kui_update_entry *entry=package->entries+i;
        CHECK(kui_update_bank_path(package->build,entry->path,path,sizeof(path)));
        snprintf(logical,sizeof(logical),"0:/KUI/%s",entry->path);
        FIL file;UINT got=0u;uint8_t original[64],changed[64];CHECK(f_open(&file,path,FA_READ)==FR_OK);
        CHECK(f_read(&file,original,sizeof(original),&got)==FR_OK && got==sizeof(original));CHECK(f_close(&file)==FR_OK);
        memcpy(changed,original,sizeof(changed));memcpy(changed+40u,package->build,12u);
        uint32_t crc=kui_crc32(0,changed,60u);for(unsigned j=0;j<4u;j++)changed[60u+j]=(uint8_t)(crc>>(8u*j));
        /* Deliberate media corruption for the guard test: a freshly CRC-sealed
         * current-build header cannot replace the approved historical build. */
        CHECK(f_open(&file,path,FA_WRITE|FA_OPEN_EXISTING)==FR_OK);
        CHECK(f_write(&file,changed,sizeof(changed),&got)==FR_OK && got==sizeof(changed));CHECK(f_close(&file)==FR_OK);
        struct kui_runtime_image image={0};CHECK(kui_runtime_read(logical,&image,logger,cancel)==KUI_RUNTIME_CHECKSUM && !image.data);
        CHECK(f_open(&file,path,FA_WRITE|FA_OPEN_EXISTING)==FR_OK);
        CHECK(f_write(&file,original,sizeof(original),&got)==FR_OK && got==sizeof(original));CHECK(f_close(&file)==FR_OK);
        struct digest expected={.bytes=entry->bytes};memcpy(expected.sha256,entry->sha256,32u);same_digest(path,&expected);
        CHECK(kui_runtime_read(logical,&image,logger,cancel)==KUI_RUNTIME_OK);
        CHECK(!strcmp(image.info.build,kui_update_entry_build(package->build,entry)));kui_runtime_free(&image);
    }
    CHECK(historical==expected);return historical;
}
int main(int argc,char **argv) {
    CHECK(argc>=4 && argc<=6);bool archive=argc>=5,changed_archive=argc==6;
    FILE *input=fopen(argv[1],"rb");CHECK(input && !fseek(input,0,SEEK_END));long length=ftell(input);
    CHECK(length>0 && !fseek(input,0,SEEK_SET));disk_bytes=(size_t)length;disk=malloc(disk_bytes);
    CHECK(disk && fread(disk,1,disk_bytes,input)==disk_bytes && !fclose(input));
    kui_media_set(&media);CHECK(f_mount(&fs,"0:",1)==FR_OK);
    size_t count=kui_update_catalog_count();struct root_file *root=calloc(count,sizeof(*root));CHECK(root);
    unsigned root_present=0u;
    char host[1024],path[192];for(unsigned i=0;i<count;i++) {
        CHECK(snprintf(host,sizeof(host),"%s/%s",argv[2],kui_update_catalog_path(i))<(int)sizeof(host));
        snprintf(path,sizeof(path),"0:/KUI/%s",kui_update_catalog_path(i));
        root[i].present=host_present(host);if(!i)CHECK(root[i].present);
        if(root[i].present) {root[i].digest=copy_host(host,path);++root_present;}else absent(path);
    }
    seed_users();struct digest expected_package=copy_host(argv[3],PACKAGE),archive_digest={0};
    if(archive)archive_digest=copy_host(argv[4],ARCHIVE);
    kui_update_boot_reset();struct kui_runtime_image image={0};CHECK(kui_runtime_read(KUI_RUNTIME_PATH,&image,logger,cancel)==KUI_RUNTIME_OK);
    char baseline[13];memcpy(baseline,image.info.build,sizeof(baseline));kui_runtime_free(&image);
    modules(NULL,root);preserved(root,(unsigned)count);
    struct kui_update_package *package=malloc(sizeof(*package));CHECK(package);struct kui_app_status status;
    char prepared[192];const char *package_path=PACKAGE;
    if(archive) {
        CHECK(kui_update_zip_prepare(ARCHIVE,prepared,&status,logger,cancel,NULL)==KUI_UPDATE_OK && status.passed);
        package_path=prepared;same_digest(package_path,&expected_package);same_digest(ARCHIVE,&archive_digest);
    }
    CHECK(kui_update_inspect(package_path,package,&status,logger,cancel,NULL)==KUI_UPDATE_OK && status.passed);
    CHECK(package->count<=count && strcmp(baseline,package->build));
    struct kui_update_pin pin;kui_update_package_pin(package,&pin);
    if(changed_archive) {
        /* Simulate the user replacing the reviewed source ZIP. INSTALL must
         * re-extract it and reject its different valid package against D's pin. */
        CHECK(f_unlink(ARCHIVE)==FR_OK);(void)copy_host(argv[5],ARCHIVE);
        CHECK(kui_update_zip_prepare(ARCHIVE,prepared,&status,logger,cancel,NULL)==KUI_UPDATE_OK && status.passed);
        CHECK(kui_update_install(prepared,&pin,&status,logger,cancel,NULL)==KUI_UPDATE_STALE && !status.passed);
        struct kui_update_activation untouched;unsigned slot=0u;
        CHECK(kui_update_activation_latest(&untouched,&slot,cancel)==KUI_UPDATE_NOT_FOUND);
        absent(KUI_UPDATE_ACTIVE0);absent(KUI_UPDATE_ACTIVE1);preserved(root,(unsigned)count);
        CHECK(f_unlink(ARCHIVE)==FR_OK);struct digest restored=copy_host(argv[4],ARCHIVE);
        CHECK(restored.bytes==archive_digest.bytes && !memcmp(restored.sha256,archive_digest.sha256,32u));
    }
    if(archive) {
        CHECK(kui_update_zip_prepare(ARCHIVE,prepared,&status,logger,cancel,NULL)==KUI_UPDATE_OK && status.passed);
        same_digest(package_path,&expected_package);same_digest(ARCHIVE,&archive_digest);
    }
    CHECK(kui_update_install(package_path,&pin,&status,logger,cancel,NULL)==KUI_UPDATE_OK && status.passed);
    preserved(root,(unsigned)count);
    CHECK(!strcmp(latest().selected,package->build) && !latest().previous[0]);
    struct kui_update_package *verified=malloc(sizeof(*verified));CHECK(verified);
    CHECK(kui_update_bank_verify(package->build,verified,&status,logger,cancel,NULL)==KUI_UPDATE_OK);
    for(unsigned i=0;i<package->count;i++) {
        CHECK(kui_update_bank_path(package->build,package->entries[i].path,path,sizeof(path)));
        if(package->entries[i].policy==KUI_UPDATE_INHERIT_ROOT) {absent(path);continue;}
        struct digest expected={.bytes=package->entries[i].bytes};memcpy(expected.sha256,package->entries[i].sha256,32u);same_digest(path,&expected);
    }
    unsigned before=writes;struct kui_update_boot_view view;
    CHECK(kui_update_boot_read(baseline,KUI_STORAGE_SCI,&image,&view,logger,cancel)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(!strcmp(view.bank,package->build) && !strcmp(image.info.build,package->build));
    struct kui_update_handoff_context context;context_from_chain(&image,&context);kui_runtime_free(&image);
    uint64_t adoption_start=read_bytes;
    CHECK(kui_update_boot_read_handoff(package->build,KUI_STORAGE_SCI,&context,&image,&view,logger,cancel)==KUI_UPDATE_BOOT_PINNED);
    /* PINNED alone also follows a failed-adoption full reload. Actual release
     * runtimes must demonstrate bounded receipt/header/slot-only adoption. */
    CHECK(package->entries[0].bytes>262144u && read_bytes-adoption_start<package->entries[0].bytes/2u);
    CHECK(!image.data && !strcmp(view.bank,package->build));modules(package,root);CHECK(writes==before);preserved(root,(unsigned)count);
    /* Restart the currently selected GUI must reread its full checked image,
     * even when its build equals the running GUI. A cancelled handoff restores
     * the running bank pin before normal use resumes. No SH code is executed. */
    uint64_t reload_start=read_bytes;
    CHECK(kui_update_boot_reload(package->build,KUI_STORAGE_SCI,&image,&view,logger,cancel)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(image.data && !strcmp(view.bank,package->build) && !strcmp(image.info.build,package->build));
    CHECK(read_bytes-reload_start>=package->entries[0].bytes);
    kui_runtime_free(&image);kui_update_boot_reload_abort();modules(package,root);
    CHECK(writes==before);preserved(root,(unsigned)count);
    unsigned historical=legacy_build_guards(package);
    /* Held B is a one-boot bypass, leaving persistent selection unchanged. */
    before=writes;CHECK(kui_update_boot_baseline(package->build,KUI_STORAGE_SCI,&image,&view,logger,cancel)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(!view.bank[0] && !strcmp(image.info.build,baseline));kui_runtime_free(&image);
    CHECK(writes==before && !strcmp(latest().selected,package->build));
    preserved(root,(unsigned)count);
    CHECK(kui_update_rollback(&status,logger,cancel,NULL)==KUI_UPDATE_OK && status.passed);
    before=writes;
    CHECK(kui_update_boot_read(package->build,KUI_STORAGE_SCI,&image,&view,logger,cancel)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(writes==before && image.data && !strcmp(image.info.build,baseline));
    kui_runtime_free(&image);
    CHECK(!latest().selected[0] && !strcmp(latest().previous,package->build));
    before=writes;CHECK(kui_update_boot_read(baseline,KUI_STORAGE_SCI,&image,&view,logger,cancel)==KUI_UPDATE_BOOT_BASELINE);
    CHECK(!view.bank[0] && !image.data);modules(NULL,root);CHECK(writes==before);preserved(root,(unsigned)count);
    if(archive)same_digest(ARCHIVE,&archive_digest);
    printf("PASS actual release updater: %u checks; %s -> %s, %u manifest entries/%zu known roles, %u present/%zu absent retained root files, %u legacy guards, %s%sfull same-build reload/abort, chained handoff/adoption, inherited module recognition/absence, held-B bypass, rollback and protected root/user data\n",checks,baseline,package->build,package->count,count,root_present,count-root_present,historical,archive?"ZIP check/install restaging, ":"",changed_archive?"changed-ZIP pin refusal, ":"");
    kui_update_boot_reset();CHECK(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);
    free(verified);free(package);free(root);free(disk);return 0;
}
