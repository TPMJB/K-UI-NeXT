/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual delivered GUI/KUP bytes through production FatFs and boot/update
 * functions. SH payloads are checked and handed off, never executed here. */
#include "kui/update.h"
#include "kui/update_boot.h"
#include "kui/update_handoff.h"
#include "kui/update_software_file.h"
#include "kui/boot_image.h"
#include "kui/ext4_boot.h"
#include "kui/storage_policy.h"
#include "kui/media.h"
#include "ff.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PACKAGE "0:/KUI/updates/current.kup"
#define OLD_PACKAGE "0:/KUI/updates/old26.kup"
#define SAVED_BOOT "0:/KUI/boot-before-engine2.kui"
#define SAVED_MAX 512u
static uint8_t *disk;static size_t disk_bytes;static FATFS fs;
static unsigned checks,writes;static uint64_t reads,logical_reads,adoption_bytes;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Engine setup failed %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint64_t blocks(void *unused) {(void)unused;return disk_bytes/512u;}
static int read_blocks(void *unused,uint32_t block,size_t count,uint8_t *out) {
    (void)unused;if((uint64_t)block+count>blocks(NULL))return -1;
    reads+=(uint64_t)count*512u;memcpy(out,disk+(size_t)block*512u,count*512u);return 0;
}
static int write_blocks(void *unused,uint32_t block,size_t count,const uint8_t *in) {
    (void)unused;if((uint64_t)block+count>blocks(NULL))return -1;
    ++writes;memcpy(disk+(size_t)block*512u,in,count*512u);return 0;
}
static int sync_blocks(void *unused) {(void)unused;return 0;}
static const struct kui_media_ops media={NULL,blocks,read_blocks,write_blocks,sync_blocks};
uint32_t kui_clock_fattime(void) {return ((2026u-1980u)<<25)|(11u<<21)|(5u<<16);}
static void logger(const char *format,...) {(void)format;}
static bool cancel(void) {return false;}
FRESULT __real_f_read(FIL *,void *,UINT,UINT *);
FRESULT __wrap_f_read(FIL *file,void *data,UINT bytes,UINT *got) {
    FRESULT result=__real_f_read(file,data,bytes,got);logical_reads+=*got;return result;
}
static void mount_volume(void) {kui_media_set(&media);CHECK(f_mount(&fs,"0:",1)==FR_OK);}
static void unmount_volume(void) {CHECK(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);}
static void parents(const char *path) {
    char copy[256];CHECK(strlen(path)<sizeof(copy));strcpy(copy,path);
    for(char *at=copy+3u;*at;at++)if(*at=='/') {
        *at=0;FRESULT r=f_mkdir(copy);CHECK(r==FR_OK || r==FR_EXIST);*at='/';
    }
}
struct digest {uint64_t bytes;uint8_t sha256[32];};
static struct digest file_digest(const char *path) {
    FIL file;CHECK(kui_update_physical_open(&file,path,FA_READ)==FR_OK);
    struct digest value={0};struct kui_sha256 hash;kui_sha256_init(&hash);uint8_t buffer[32768];
    for(;;) {
        UINT got=0u;CHECK(f_read(&file,buffer,sizeof(buffer),&got)==FR_OK);if(!got)break;
        kui_sha256_update(&hash,buffer,got);value.bytes+=got;
    }
    CHECK(value.bytes==f_size(&file) && f_close(&file)==FR_OK);kui_sha256_digest(&hash,value.sha256);return value;
}
static void same_digest(const char *path,const struct digest *expected) {
    struct digest actual=file_digest(path);
    CHECK(actual.bytes==expected->bytes && !memcmp(actual.sha256,expected->sha256,32u));
}
static struct digest copy_host(const char *source,const char *destination) {
    FILE *input=fopen(source,"rb");CHECK(input);parents(destination);
    FIL output;CHECK(f_open(&output,destination,FA_WRITE|FA_CREATE_NEW)==FR_OK);
    uint8_t buffer[32768];struct digest value={0};struct kui_sha256 hash;kui_sha256_init(&hash);size_t n;
    while((n=fread(buffer,1u,sizeof(buffer),input))!=0u) {
        UINT put=0u;CHECK(f_write(&output,buffer,(UINT)n,&put)==FR_OK && put==n);
        kui_sha256_update(&hash,buffer,n);value.bytes+=n;
    }
    CHECK(!ferror(input) && !fclose(input));CHECK(f_sync(&output)==FR_OK && f_close(&output)==FR_OK);
    kui_sha256_digest(&hash,value.sha256);return value;
}
struct saved_file {char path[192];struct digest digest;};
static struct saved_file apps[SAVED_MAX],old_files[SAVED_MAX];static unsigned app_count,old_count;
static void capture_tree(const char *directory,struct saved_file *files,unsigned *count) {
    DIR dir;FILINFO info;CHECK(f_opendir(&dir,directory)==FR_OK);
    for(;;) {
        CHECK(f_readdir(&dir,&info)==FR_OK);if(!info.fname[0])break;
        char path[192];CHECK(snprintf(path,sizeof(path),"%s/%s",directory,info.fname)<(int)sizeof(path));
        if(info.fattrib&AM_DIR)capture_tree(path,files,count);
        else {
            CHECK(*count<SAVED_MAX);strcpy(files[*count].path,path);files[(*count)++].digest=file_digest(path);
        }
    }
    CHECK(f_closedir(&dir)==FR_OK);
}
static void tree_preserved(const char *directory,const struct saved_file *files,unsigned count) {
    struct saved_file *actual=calloc(SAVED_MAX,sizeof(*actual));CHECK(actual);unsigned found=0u;
    capture_tree(directory,actual,&found);CHECK(found==count);
    for(unsigned i=0;i<count;i++)same_digest(files[i].path,&files[i].digest);
    free(actual);
}
static const char *const user_paths[]={"0:/KUI/vmu/cards/v1234/card.vmd",
    "0:/KUI/vmu-test/p0012/card.journal","0:/KUI/prefs/vmu.bin","0:/Games/untouched/disc.gdi"};
static struct digest users[sizeof(user_paths)/sizeof(user_paths[0])];
static void seed_users(void) {
    uint8_t data[1024];for(unsigned i=0;i<sizeof(data);i++)data[i]=(uint8_t)(i*53u+17u);
    for(unsigned i=0;i<sizeof(user_paths)/sizeof(user_paths[0]);i++) {
        parents(user_paths[i]);FIL file;UINT put=0u;CHECK(f_open(&file,user_paths[i],FA_WRITE|FA_CREATE_NEW)==FR_OK);
        CHECK(f_write(&file,data,sizeof(data),&put)==FR_OK && put==sizeof(data));CHECK(f_close(&file)==FR_OK);
        users[i]=file_digest(user_paths[i]);
    }
}
static void preserved(const struct digest *root,const struct digest *boot,const char *old_directory) {
    same_digest(KUI_RUNTIME_PATH,root);same_digest(SAVED_BOOT,boot);
    tree_preserved("0:/KUI/apps",apps,app_count);tree_preserved(old_directory,old_files,old_count);
    for(unsigned i=0;i<sizeof(user_paths)/sizeof(user_paths[0]);i++)same_digest(user_paths[i],users+i);
}
static struct kui_runtime_info runtime_info(const char *path,unsigned engine,struct kui_update_handoff_context *context) {
    FIL file;CHECK(kui_update_physical_open(&file,path,FA_READ)==FR_OK);uint8_t header[64];UINT got=0u;
    CHECK(f_read(&file,header,sizeof(header),&got)==FR_OK && got==sizeof(header));struct kui_runtime_info info;
    CHECK(kui_runtime_header(header,f_size(&file),&info)==KUI_RUNTIME_OK);
    uint8_t *body=malloc(info.payload_bytes);CHECK(body);
    CHECK(f_read(&file,body,info.payload_bytes,&got)==FR_OK && got==info.payload_bytes && f_close(&file)==FR_OK);
    CHECK(kui_crc32(0u,body,info.payload_bytes)==info.crc32);
    CHECK(kui_update_capability_valid(body,info.payload_bytes) && kui_update_engine_level(body,info.payload_bytes)==engine);
    uint32_t offset=0u;CHECK(kui_update_handoff_empty(body,info.payload_bytes,&offset));
    if(context) {context->payload_offset=offset;memcpy(context->wire,body+offset,sizeof(context->wire));}
    free(body);return info;
}
static struct kui_update_activation latest(unsigned *slot) {
    struct kui_update_activation value;unsigned actual=0u;
    CHECK(kui_update_activation_latest(&value,&actual,cancel)==KUI_UPDATE_OK);if(slot)*slot=actual;return value;
}
enum kui_ext4_boot_result kui_ext4_boot_read_path(const struct kui_media_ops *raw,
    const char *path,struct kui_runtime_image *out,kui_log_fn log,kui_cancel_fn stop) {
    (void)raw;(void)path;(void)out;(void)log;(void)stop;CHECK(false);return KUI_EXT4_BOOT_INVALID;
}
static void bootstrap(enum kui_boot_mode mode,const char *build) {
    unmount_volume();kui_update_boot_reset();unsigned before=writes;struct kui_runtime_image image={0};
    CHECK(kui_boot_image_read_mode(&media,KUI_STORAGE_SCI,mode,&image,logger,cancel)==KUI_RUNTIME_OK);
    CHECK(image.data && !strcmp(image.info.build,build) && writes==before);kui_runtime_free(&image);mount_volume();
}
static void context_from_chain(const struct kui_runtime_image *image,
    const struct kui_update_activation *activation,struct kui_update_handoff_context *context) {
    CHECK(image->data && image->info.payload_bytes>=KUI_UPDATE_HANDOFF_BYTES);
    const uint8_t *body=image->data;unsigned found=0u;
    /* The dispatcher and selected GUI can have different layouts. Discover
     * the stamped slot in the checked selected image, not in the root GUI. */
    for(uint32_t offset=0u;offset<=image->info.payload_bytes-KUI_UPDATE_HANDOFF_BYTES;offset++) {
        if(body[offset]!='K' || memcmp(body+offset,"KUIHOF1",8u))continue;
        struct kui_update_handoff candidate;
        if(!kui_update_handoff_decode(body+offset,&candidate))continue;
        CHECK(candidate.payload_offset==offset);++found;context->payload_offset=offset;
        memcpy(context->wire,body+offset,sizeof(context->wire));
    }
    CHECK(found==1u);struct kui_update_handoff handoff;
    CHECK(kui_update_handoff_decode(context->wire,&handoff));
    CHECK(!strcmp(handoff.build,image->info.build) && strcmp(handoff.build,activation->selected));
    CHECK(handoff.sequence==activation->sequence && !memcmp(handoff.manifest_sha256,activation->manifest_sha256,32u));
}
static uint64_t adopt(const char *build,const struct kui_update_activation *activation,
    const struct kui_update_handoff_context *context) {
    uint64_t before=reads,logical_before=logical_reads;unsigned writes_before=writes;struct kui_runtime_image image={0};struct kui_update_boot_view view;
    CHECK(kui_update_boot_read_handoff(build,KUI_STORAGE_SCI,context,&image,&view,logger,cancel)==KUI_UPDATE_BOOT_PINNED);
    CHECK(!image.data && !strcmp(view.bank,activation->selected) && !strcmp(view.build,build) && writes==writes_before);
    CHECK(logical_reads-logical_before==adoption_bytes);
    char bank[13];uint8_t sha[32];CHECK(kui_update_effective_snapshot(bank,sha)==1 && !strcmp(bank,activation->selected));
    CHECK(!memcmp(sha,activation->manifest_sha256,32u));return reads-before;
}
static void exact_members(const struct kui_update_package *incoming,const struct kui_update_package *receipt) {
    CHECK(incoming->count==receipt->count);FIL package;CHECK(f_open(&package,PACKAGE,FA_READ)==FR_OK);
    uint8_t wanted[32768],actual[32768];
    for(uint32_t i=0;i<incoming->count;i++) {
        const struct kui_update_entry *entry=&incoming->entries[i];
        const struct kui_update_entry *installed=kui_update_package_find(receipt,entry->path);CHECK(installed);
        CHECK(entry->policy==KUI_UPDATE_SELF_KUI || entry->policy==KUI_UPDATE_RAW);
        CHECK(installed->policy==entry->policy && installed->bytes==entry->bytes &&
            !memcmp(installed->sha256,entry->sha256,32u) && !memcmp(installed->build,entry->build,13u));
        char physical[192],logical[192];CHECK(kui_update_bank_path(receipt->build,entry->path,physical,sizeof(physical)));
        CHECK(snprintf(logical,sizeof(logical),"0:/KUI/%s",entry->path)<(int)sizeof(logical));
        char resolved[192];struct kui_update_runtime_stream stream;
        CHECK(kui_update_runtime_resolve(logical,resolved,sizeof(resolved),&stream) &&
            !strcmp(resolved,physical) && stream.pinned && stream.raw==(entry->policy==KUI_UPDATE_RAW));
        FIL bank;CHECK(kui_update_physical_open(&bank,physical,FA_READ)==FR_OK && f_size(&bank)==entry->bytes);
        CHECK(f_lseek(&package,(FSIZE_t)entry->payload_offset)==FR_OK);
        for(uint64_t at=0u;at<entry->bytes;) {
            UINT n=(UINT)(entry->bytes-at>sizeof(actual)?sizeof(actual):entry->bytes-at),got=0u;
            CHECK(f_read(&package,wanted,n,&got)==FR_OK && got==n);
            CHECK(f_read(&bank,actual,n,&got)==FR_OK && got==n && !memcmp(wanted,actual,n));at+=n;
        }
        CHECK(f_close(&bank)==FR_OK);FILINFO info;CHECK(f_stat(logical,&info)==FR_OK && info.fsize==entry->bytes);
        if(entry->policy==KUI_UPDATE_SELF_KUI) {
            struct kui_runtime_image image={0};CHECK(kui_runtime_read(logical,&image,logger,cancel)==KUI_RUNTIME_OK);
            CHECK(image.data && !strcmp(image.info.build,entry->build));kui_runtime_free(&image);
        } else {
            FIL helper;CHECK(f_open(&helper,logical,FA_READ)==FR_OK && f_size(&helper)==entry->bytes);
            CHECK(f_close(&helper)==FR_OK);
        }
    }
    CHECK(f_close(&package)==FR_OK);
}
int main(int argc,char **argv) {
    CHECK(argc==7);FILE *host=fopen(argv[1],"rb");CHECK(host && !fseek(host,0,SEEK_END));long length=ftell(host);
    CHECK(length>0 && !fseek(host,0,SEEK_SET));disk_bytes=(size_t)length;disk=malloc(disk_bytes);
    CHECK(disk && fread(disk,1u,disk_bytes,host)==disk_bytes && !fclose(host));mount_volume();
    FILE *inventory=fopen(argv[6],"r");CHECK(inventory);char relative[192],source[1024],destination[256];unsigned seeded=0u;
    while(fgets(relative,sizeof(relative),inventory)) {
        relative[strcspn(relative,"\r\n")]=0;CHECK(relative[0] && kui_update_module_path_valid(relative));
        CHECK(snprintf(source,sizeof(source),"%s/%s",argv[2],relative)<(int)sizeof(source));
        CHECK(snprintf(destination,sizeof(destination),"0:/KUI/%s",relative)<(int)sizeof(destination));
        (void)copy_host(source,destination);++seeded;
    }
    CHECK(!ferror(inventory) && !fclose(inventory) && seeded>1u);capture_tree("0:/KUI/apps",apps,&app_count);seed_users();
    struct kui_update_handoff_context old_empty;struct kui_runtime_info original=runtime_info(KUI_RUNTIME_PATH,0u,&old_empty);
    CHECK(snprintf(source,sizeof(source),"%s/runtime.kui",argv[2])<(int)sizeof(source));
    struct digest boot=copy_host(source,"0:/KUI/boot.kui");(void)copy_host(argv[3],OLD_PACKAGE);
    struct digest delivered=copy_host(argv[5],PACKAGE);
    struct kui_update_package *old=malloc(sizeof(*old)),*incoming=malloc(sizeof(*incoming)),*receipt=malloc(sizeof(*receipt));CHECK(old && incoming && receipt);
    struct kui_app_status status;struct kui_update_pin pin;
    CHECK(kui_update_inspect(OLD_PACKAGE,old,&status,logger,cancel,NULL)==KUI_UPDATE_OK && old->count==26u);
    kui_update_package_pin(old,&pin);CHECK(kui_update_install(OLD_PACKAGE,&pin,&status,logger,cancel,NULL)==KUI_UPDATE_OK);
    char old_gui_path[160];CHECK(kui_update_bank_path(old->build,"runtime.kui",old_gui_path,sizeof(old_gui_path)));
    struct kui_runtime_info old_gui=runtime_info(old_gui_path,0u,NULL);CHECK(!strcmp(old_gui.build,old->build));
    CHECK(kui_update_select("",NULL,&status,logger,cancel,NULL)==KUI_UPDATE_OK);
    CHECK(kui_update_select(old->build,old->manifest_sha256,&status,logger,cancel,NULL)==KUI_UPDATE_OK);
    char old_directory[160];CHECK(snprintf(old_directory,sizeof(old_directory),KUI_UPDATE_SYSTEM "/%s",old->build)<(int)sizeof(old_directory));
    capture_tree(old_directory,old_files,&old_count);CHECK(old_count>1u);
    unsigned old_slot=0u;struct kui_update_activation old_activation=latest(&old_slot);CHECK(!strcmp(old_activation.selected,old->build));
    /* Both retained records can name the same GUI1. Its failed engine check
     * must consume one complete GUI read across the two selection attempts. */
    struct kui_update_activation duplicate=old_activation;CHECK(duplicate.sequence>1u);--duplicate.sequence;
    uint8_t duplicate_wire[128];CHECK(kui_update_activation_encode(&duplicate,duplicate_wire));FIL older_slot;UINT put=0u;
    CHECK(f_open(&older_slot,old_slot?KUI_UPDATE_ACTIVE0:KUI_UPDATE_ACTIVE1,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    CHECK(f_write(&older_slot,duplicate_wire,sizeof(duplicate_wire),&put)==FR_OK && put==sizeof(duplicate_wire));
    CHECK(f_sync(&older_slot)==FR_OK && f_close(&older_slot)==FR_OK);
    struct digest old_record=file_digest(old_slot?KUI_UPDATE_ACTIVE1:KUI_UPDATE_ACTIVE0);
    /* Only the dispatcher is replaced. Existing selection and banks stay in
     * system/, and every physical reader/raw helper stays where it was. */
    kui_update_boot_reset();CHECK(f_unlink(KUI_RUNTIME_PATH)==FR_OK);struct digest root=copy_host(argv[4],KUI_RUNTIME_PATH);
    struct kui_update_handoff_context empty;struct kui_runtime_info current=runtime_info(KUI_RUNTIME_PATH,2u,&empty);
    CHECK(strcmp(original.build,current.build));bootstrap(KUI_BOOT_MODE_AUTOBOOT,original.build);
    CHECK(f_rename("0:/KUI/boot.kui",SAVED_BOOT)==FR_OK);same_digest(SAVED_BOOT,&boot);
    bootstrap(KUI_BOOT_MODE_AUTOBOOT,current.build);bootstrap(KUI_BOOT_MODE_NORMAL,current.build);
    kui_update_boot_require_engine(2u);unsigned before=writes;uint64_t logical_before=logical_reads;
    struct kui_runtime_image image={0};struct kui_update_boot_view view;
    CHECK(kui_update_boot_read_handoff(current.build,KUI_STORAGE_SCI,&empty,&image,&view,logger,cancel)==KUI_UPDATE_BOOT_BASELINE);
    CHECK(!image.data && view.fallback && !view.bank[0] && !strcmp(view.build,current.build) && writes==before);
    const struct kui_update_entry *old_gui_entry=kui_update_package_find(old,"runtime.kui");CHECK(old_gui_entry);
    CHECK(logical_reads-logical_before==2u*KUI_UPDATE_ACTIVATION_BYTES+
        2u*(KUI_UPDATE_HEADER_BYTES+old->count*kui_update_entry_bytes(old))+old_gui_entry->bytes+KUI_RUNTIME_HEADER_BYTES);
    char effective[13];uint8_t effective_hash[32];CHECK(kui_update_effective_snapshot(effective,effective_hash)==1 && !effective[0]);
    CHECK(!strcmp(latest(NULL).selected,old->build));preserved(&root,&boot,old_directory);
    CHECK(kui_update_inspect(PACKAGE,incoming,&status,logger,cancel,NULL)==KUI_UPDATE_OK && incoming->version==2u && incoming->flags==KUI_UPDATE_FULL);
    const struct kui_update_entry *gui=kui_update_package_find(incoming,"runtime.kui");CHECK(gui);
    bool same_gui=gui->bytes==root.bytes && !memcmp(gui->sha256,root.sha256,32u);
    kui_update_package_pin(incoming,&pin);CHECK(kui_update_install(PACKAGE,&pin,&status,logger,cancel,NULL)==KUI_UPDATE_OK);
    struct kui_update_activation selected=latest(NULL);CHECK(strcmp(selected.selected,gui->build) && strcmp(selected.selected,incoming->build));
    CHECK(selected.sequence==old_activation.sequence+1u && !strcmp(selected.previous,old->build));
    CHECK(kui_update_bank_verify(selected.selected,receipt,&status,logger,cancel,NULL)==KUI_UPDATE_OK);
    CHECK(receipt->flags==KUI_UPDATE_FULL && !memcmp(receipt->manifest_sha256,selected.manifest_sha256,32u));
    adoption_bytes=2u*KUI_UPDATE_ACTIVATION_BYTES+KUI_UPDATE_HEADER_BYTES+
        receipt->count*kui_update_entry_bytes(receipt)+KUI_RUNTIME_HEADER_BYTES+KUI_UPDATE_HANDOFF_BYTES;
    same_digest(old_slot?KUI_UPDATE_ACTIVE1:KUI_UPDATE_ACTIVE0,&old_record);preserved(&root,&boot,old_directory);
    CHECK(kui_update_boot_reload(current.build,KUI_STORAGE_SCI,&image,&view,logger,cancel)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(image.data && !strcmp(image.info.build,gui->build) && !strcmp(view.bank,selected.selected));
    struct kui_update_handoff_context handoff;context_from_chain(&image,&selected,&handoff);kui_runtime_free(&image);
    /* Model transfer into a fresh GUI process without executing SH bytes. */
    kui_update_boot_reload_abort();kui_update_boot_reset();uint64_t adopted=adopt(gui->build,&selected,&handoff);
    CHECK(adopted<gui->bytes-KUI_RUNTIME_HEADER_BYTES);exact_members(incoming,receipt);preserved(&root,&boot,old_directory);same_digest(PACKAGE,&delivered);
    /* Cold entry must execute the selected GUI, including when its build or
     * bytes match root. Only its checked RAM handoff permits adoption. */
    kui_update_boot_reset();before=writes;
    CHECK(kui_update_boot_read_handoff(current.build,KUI_STORAGE_SCI,&empty,&image,&view,logger,cancel)==KUI_UPDATE_BOOT_CHAIN);
    CHECK(image.data && !strcmp(image.info.build,gui->build) && !strcmp(view.bank,selected.selected) && writes==before);
    context_from_chain(&image,&selected,&handoff);kui_runtime_free(&image);(void)adopt(gui->build,&selected,&handoff);
    exact_members(incoming,receipt);preserved(&root,&boot,old_directory);
    printf("PASS actual engine2 setup: %u checks; legacy26 %s retained, root GUI %s, selected GUI %s, FULL %u-file package %s -> snapshot %s, duplicate old-GUI read once, %s-GUI restart/cold handoff, %llu logical/%llu physical bytes per checked adoption, exact bank KUI/raw bytes, %u physical app files and user data unchanged, optional boot override preserved, no system archive\n",checks,old->build,current.build,gui->build,incoming->count,incoming->build,selected.selected,same_gui?"same":"different",(unsigned long long)adoption_bytes,(unsigned long long)adopted,app_count);
    kui_update_boot_reset();unmount_volume();free(old);free(incoming);free(receipt);free(disk);return 0;
}
