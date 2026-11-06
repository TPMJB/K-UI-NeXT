/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the production unified preparation and all six byte-backed file
 * publishers. Module loading, original-source proof, metadata lookup and the
 * already-tested card service are explicit seams; no target runtime is run. */
#include "ff.h"
#include "kui/core.h"
#include "kui/runtime.h"
#include "kui/media.h"
#include "kui/game_image.h"
#include "kui/game_metadata.h"
#include "kui/game_launch_settings.h"
#include "kui/retail_loader_layout.h"
#include "kui/smash_native_child.h"
#include "kui/smash_native_menu.h"
#include "kui/smash_menu_return.h"
#include "kui/smash_menu_collection.h"
#include "kui/smash_ce_child.h"
#include "kui/smash_ce_profile.h"
#include "kui/vc2_ce_vmu_persist.h"
#include "kui/vmu_game_card_service.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINK_MAP_WORDS 16u
static DWORD link_map[LINK_MAP_WORDS];
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
struct files {char root[128];bool reader_open;};
struct games_card_platform {FATFS *fs;struct kui_volume volume;char history_problem[128];};
enum native_cache_result {NATIVE_CACHE_SKIPPED,NATIVE_CACHE_READY,NATIVE_CACHE_CANCELLED};
struct native_cache_source_proof {bool complete;uint32_t crc32;uint8_t sha256[32];};
static bool close_reader(struct files *files) {assert(!files->reader_open);return true;}
static bool layout(const struct kui_runtime_image *image,bool ce) {
    assert(!ce);return image->data && image->info.payload_bytes==16384u &&
        image->info.memory_bytes==image->info.payload_bytes;
}
static bool games_card_identity(void *,struct kui_vmu_game_card_raw *);
static bool games_card_raw_begin(void *context,struct kui_vmu_game_card_raw *raw) {
    (void)context;(void)raw;assert(!"fixture must not enter raw storage");return false;
}
static bool games_card_raw_end(void *context) {(void)context;return false;}
static void games_card_salt(void *context,uint8_t out[16]) {(void)context;memset(out,0x41,16u);}
static int games_card_history(void *,enum kui_vmu_game_card_boundary_phase,const char *,unsigned,
    const uint8_t[16],const struct kui_vmu_game_candidates_storage *,kui_log_fn,kui_cancel_fn);
static enum native_cache_result native_cache_prepare(const struct kui_game_image *,struct kui_retail_manifest *,
    bool,uint32_t,bool,struct kui_runtime_image *,const char *,bool,kui_log_fn,kui_cancel_fn,
    struct native_cache_source_proof *,void *);
/* This existing integration fixture supplies its fresh original-source
 * proof at an explicit seam. The production owner-hint path has separate
 * canonical-record/stale-SHA tests; synthetic old modules stay on fallback. */
static enum native_cache_result smash_native_mapping_prepare(const struct kui_game_image *image,
    struct kui_retail_manifest *map,struct kui_runtime_image *module,const char *root,
    kui_log_fn log,kui_cancel_fn cancel,struct native_cache_source_proof *proof,bool *deferred) {
    *deferred=false;
    return native_cache_prepare(image,map,true,0u,false,module,root,true,log,cancel,proof,NULL);
}
static void smash_native_boot_target(struct kui_game_metadata *metadata,
    struct kui_retail_manifest *map,uint32_t lba,uint32_t bytes) {
    metadata->boot_lba=map->boot_lba=lba;metadata->boot_bytes=map->boot_bytes=bytes;
    memset(metadata->bootfile,0,sizeof(metadata->bootfile));memset(map->bootfile,0,sizeof(map->bootfile));
    memcpy(metadata->bootfile,"MDE_US.BIN",sizeof("MDE_US.BIN"));memcpy(map->bootfile,"MDE_US.BIN",sizeof("MDE_US.BIN"));
}
#include "../src/apps/vc2_ce_vmu_persist_prepare.inc"
#include "../src/apps/smash_menu_capsule_file_prepare.inc"
#include "../src/apps/smash_menu_capsule_pair_prepare.inc"
static bool smash_menu_capsule_module_read(const char *,struct kui_runtime_image *,const char **,kui_log_fn,kui_cancel_fn);
#include "../src/apps/smash_menu_collection_prepare.inc"

enum {ROLE_BINDING=SMASH_CAP_COUNT,ROLE_VC2_HELPER,ROLE_SWIRL_HELPER,ROLE_COUNT};
struct disk_file {uint8_t data[65536];uint32_t bytes,cluster;bool present;unsigned creates,writes,reads,syncs;};
static struct disk_file disk[ROLE_COUNT];
static int active=-1,fail_sync=-1,corrupt_read=-1,module_fault=-1;
static FIL *handle;
static FATFS fs;
static struct kui_volume volume;
static struct kui_retail_manifest map;
static struct kui_game_metadata metadata;
static struct kui_game_image image;
static uint8_t template_wire[4096],selected_wire[4096],modules[4][16384],outer_template[16384];
static struct kui_runtime_image outer;
static unsigned selects,histories,identity_reads,source_proofs,module_reads;
static unsigned phase_text_opens,best_effort_report_refusals;
static unsigned resolver_calls,resolved_helper_opens;
static bool resolver_mapped,resolver_failed;
bool kui_runtime_resolve_path(const char *path,char *out,size_t capacity) {
    ++resolver_calls;if(resolver_failed)return false;
    int bytes=snprintf(out,capacity,"%s%s",resolver_mapped?"resolved:":"",path);
    return bytes>=0 && (size_t)bytes<capacity;
}
static const char build_id[]="123456789abc";
static const size_t profile_at=0x2200u,owner_at=0x3300u,late_at=0x3400u,child_at=0x3500u,
    return_at=0x3600u,collection_at=0x3300u;
static size_t child_return_offset(unsigned kind) {
    return kind?child_at+KUI_SMASH_CE_CHILD_BYTES:owner_at+KUI_SMASH_NATIVE_MENU_SEED_BYTES;
}
static bool cancel(void) {return false;}
static void logger(const char *format,...) {
    if(strstr(format,"prepared launch retained"))++best_effort_report_refusals;
}

static const char *file_path(unsigned role) {
    if(role<SMASH_CAP_COUNT)return smash_capsule_paths[role];
    if(role==ROLE_BINDING)return "0:/KUI/vmu-test/p0001/game.bin";
    return role==ROLE_VC2_HELPER?KUI_SMASH_CE_VC2_HELPER:KUI_SMASH_CE_SWIRL_HELPER;
}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    assert(active<0);bool mapped=!strncmp(path,"resolved:",9u);if(mapped)path+=9u;
    /* This fixture supplies no diagnostic-file storage. The separate real
     * FatFs suite exercises its guarded creation; unified preparation must
     * remain successful when that optional report cannot be opened. */
    if(!strcmp(path,KUI_SMASH_PHASE_FILE_PATH)) {
        assert(!mapped && mode==FA_READ);++phase_text_opens;return FR_DENIED;
    }
    unsigned role=0u;while(role<ROLE_COUNT && strcmp(path,file_path(role)))++role;
    assert(role<ROLE_COUNT);struct disk_file *d=&disk[role];
    if(mapped) {assert(role==ROLE_VC2_HELPER || role==ROLE_SWIRL_HELPER);++resolved_helper_opens;}
    if(mode==FA_READ && !d->present)return FR_NO_FILE;
    if(mode&FA_CREATE_ALWAYS) {
        assert(role<SMASH_CAP_COUNT);d->present=true;d->bytes=0u;++d->creates;
    } else assert(mode==FA_READ);
    memset(file,0,sizeof(*file));file->obj.fs=&fs;file->obj.objsize=d->bytes;
    file->obj.sclust=d->bytes?d->cluster:0u;active=(int)role;handle=file;return FR_OK;
}
FRESULT f_lseek(FIL *file,FSIZE_t offset) {
    assert(file==handle && active>=0);struct disk_file *d=&disk[active];
    if(offset==CREATE_LINKMAP) {
        assert(active<SMASH_CAP_COUNT && file->cltbl && file->cltbl[0]>=4u);
        file->cltbl[0]=4u;file->cltbl[1]=(d->bytes+511u)/512u;
        file->cltbl[2]=d->cluster;file->cltbl[3]=0u;return FR_OK;
    }
    assert(offset<=d->bytes);file->fptr=offset;return FR_OK;
}
FRESULT f_expand(FIL *file,FSIZE_t bytes,BYTE contiguous) {
    assert(file==handle && active>=0 && active<SMASH_CAP_COUNT && contiguous==1u);
    struct disk_file *d=&disk[active];assert(!d->bytes && bytes<=sizeof(d->data));
    d->bytes=(uint32_t)bytes;file->obj.objsize=bytes;file->obj.sclust=d->cluster;return FR_OK;
}
FRESULT f_read(FIL *file,void *out,UINT bytes,UINT *got) {
    assert(file==handle && active>=0);struct disk_file *d=&disk[active];
    assert(file->fptr<=d->bytes && bytes<=d->bytes-file->fptr);
    memcpy(out,d->data+file->fptr,bytes);file->fptr+=bytes;*got=bytes;++d->reads;
    if(active==corrupt_read && d->writes && bytes)((uint8_t *)out)[0]^=1u;
    return FR_OK;
}
FRESULT f_write(FIL *file,const void *in,UINT bytes,UINT *put) {
    assert(file==handle && active>=0 && active<SMASH_CAP_COUNT);struct disk_file *d=&disk[active];
    assert(file->fptr<=d->bytes && bytes<=d->bytes-file->fptr);
    memcpy(d->data+file->fptr,in,bytes);file->fptr+=bytes;*put=bytes;++d->writes;return FR_OK;
}
FRESULT f_sync(FIL *file) {
    assert(file==handle && active>=0);++disk[active].syncs;return active==fail_sync?FR_DISK_ERR:FR_OK;
}
FRESULT f_close(FIL *file) {
    assert(file==handle && active>=0 && !file->cltbl);active=-1;handle=NULL;return FR_OK;
}
static bool games_card_identity(void *context,struct kui_vmu_game_card_raw *out) {
    assert(context && active<0);memset(out,0,sizeof(*out));memset(out->cid,0x27,16u);
    out->card_sectors=1048576u;out->high_capacity=true;++identity_reads;return true;
}
static int games_card_history(void *context,enum kui_vmu_game_card_boundary_phase phase,
    const char *product,unsigned pair,const uint8_t identity[16],
    const struct kui_vmu_game_candidates_storage *storage,kui_log_fn log,kui_cancel_fn cancelled) {
    assert(context && phase==KUI_VMU_GAME_CARD_RESOLVED && !strcmp(product,KUI_VC2_PERSIST_PRODUCT) &&
        pair==1u && identity && storage && log && cancelled && active<0);++histories;return KUI_VMU_GAME_CARD_READY;
}
int kui_vmu_game_card_select_boundary(const char *product,const struct kui_vmu_game_candidate_target *target,
    const struct kui_vmu_game_candidates_storage *storage,uint64_t first,uint64_t end,
    const struct kui_vmu_fs_range *forbidden,size_t count,const struct kui_vmu_game_card_platform *platform,
    struct kui_vmu_game_card_work *work,uint8_t wire[4096],struct kui_vmu_game_card_choice *choice,
    kui_vmu_game_card_boundary boundary,void *context,kui_log_fn log,kui_cancel_fn cancelled) {
    (void)work;assert(!strcmp(product,KUI_VC2_PERSIST_PRODUCT) && !target && !storage &&
        first==volume.start && end==(uint64_t)volume.start+volume.count && count==1u &&
        forbidden[0].lba==100000u && forbidden[0].blocks==20000u && platform->context==context &&
        boundary==games_card_history && active<0);++selects;
    memset(choice,0,sizeof(*choice));choice->pair=1u;assert(kui_vmu_game_card_key(product,choice->key));
    struct kui_vmu_persist_profile profile;assert(!kui_vmu_persist_profile_decode(&profile,selected_wire,4096u));
    struct kui_vmu_game_candidates_storage fresh={0};
    assert(boundary(context,KUI_VMU_GAME_CARD_RESOLVED,product,1u,profile.image_id,&fresh,log,cancelled)==KUI_VMU_GAME_CARD_READY);
    memcpy(wire,selected_wire,4096u);return KUI_VMU_GAME_CARD_READY;
}
int kui_vmu_game_card_select(const char *product,uint64_t first,uint64_t end,
    const struct kui_vmu_fs_range *forbidden,size_t count,const struct kui_vmu_game_card_platform *platform,
    struct kui_vmu_game_card_work *work,uint8_t wire[4096],struct kui_vmu_game_card_choice *choice,
    kui_log_fn log,kui_cancel_fn cancelled) {
    (void)product;(void)first;(void)end;(void)forbidden;(void)count;(void)platform;(void)work;
    (void)wire;(void)choice;(void)log;(void)cancelled;assert(!"CE must reuse the one collection selection");return -1;
}
enum kui_runtime_result kui_runtime_read(const char *path,struct kui_runtime_image *out,kui_log_fn log,kui_cancel_fn cancelled) {
    unsigned kind;
    if(!strcmp(path,KUI_SMASH_NATIVE_MENU_PACKAGE))kind=0u;
    else if(!strcmp(path,KUI_SMASH_CE_VC2_PACKAGE))kind=1u;
    else {assert(!strcmp(path,KUI_SMASH_CE_SWIRL_PACKAGE));kind=2u;}
    assert(log && cancelled && !out->data && active<0);++module_reads;
    if((int)kind==module_fault)return KUI_RUNTIME_IO;
    out->data=malloc(sizeof(modules[kind]));assert(out->data);memcpy(out->data,modules[kind],sizeof(modules[kind]));
    out->info=(struct kui_runtime_info){.payload_bytes=16384u,.memory_bytes=16384u};memcpy(out->info.build,build_id,13u);
    return KUI_RUNTIME_OK;
}
void kui_runtime_free(struct kui_runtime_image *module) {free(module->data);memset(module,0,sizeof(*module));}
static bool smash_menu_capsule_module_read(const char *build,struct kui_runtime_image *module,
    const char **problem,kui_log_fn log,kui_cancel_fn cancelled) {
    (void)problem;return kui_runtime_read(KUI_SMASH_NATIVE_MENU_PACKAGE,module,log,cancelled)==KUI_RUNTIME_OK &&
        layout(module,false) && !memcmp(module->info.build,build,12u);
}
enum kui_game_metadata_status kui_game_metadata_find_directory(const struct kui_game_metadata_ops *ops,
    struct kui_game_metadata *m,const char *name,uint32_t *lba,uint32_t *bytes) {
    assert(ops && m->root_lba==100u);*lba=!strcmp(name,"VIRTCOP2")?200u:300u;
    if(*lba==300u)assert(!strcmp(name,"SWIRL"));
    *bytes=2048u;return KUI_GAME_METADATA_OK;
}
enum kui_game_metadata_status kui_game_metadata_find(const struct kui_game_metadata_ops *ops,
    struct kui_game_metadata *m,const char *name,uint32_t max,uint32_t *lba,uint32_t *bytes) {
    assert(ops);
    if(!strcmp(name,"MDE_US.BIN")) {assert(m->root_lba==100u && max==KUI_SMASH_NATIVE_ENGINE_BYTES);*lba=45100u;*bytes=max;}
    else {assert(!strcmp(name,"SAMPLE.BIN") && max==KUI_SMASH_CE_SAMPLE_BYTES);
        assert(m->root_lba==200u || m->root_lba==300u);*lba=m->root_lba==200u?46000u:47000u;*bytes=max;}
    return KUI_GAME_METADATA_OK;
}
static enum native_cache_result native_cache_prepare(const struct kui_game_image *source,
    struct kui_retail_manifest *child,bool enabled,uint32_t reader,bool dual,struct kui_runtime_image *module,
    const char *root,bool disk_cache,kui_log_fn log,kui_cancel_fn cancelled,struct native_cache_source_proof *proof,void *unused) {
    assert(source==&image && enabled && !reader && !dual && module && !strcmp(root,"/Smash") &&
        disk_cache && log && cancelled && !unused && !strcmp(child->bootfile,"MDE_US.BIN"));
    static const uint8_t sha[32]={0x04,0xf4,0x9d,0x58,0x3b,0x39,0xbd,0xc7,
        0x10,0xce,0xc6,0xd0,0xa4,0x34,0xb5,0xd7,0x10,0xb9,0xbc,0xce,0xdd,0x53,0x9b,0xed,
        0xda,0x29,0x75,0x51,0x35,0xf0,0xfe,0x61};
    proof->complete=true;proof->crc32=KUI_SMASH_NATIVE_ENGINE_CRC32;memcpy(proof->sha256,sha,32u);
    ++source_proofs;return NATIVE_CACHE_READY;
}
static void module_templates(void) {
    struct kui_smash_menu_return_seed returning={.magic0=KUI_SMASH_MENU_RETURN_SEED_MAGIC0,
        .magic1=KUI_SMASH_MENU_RETURN_SEED_MAGIC1,.version=1u,.bytes=368u};
    memset(modules,0x55,sizeof(modules));
    for(unsigned kind=0u;kind<3u;++kind) {
        memcpy(modules[kind]+profile_at,template_wire,4096u);
        memcpy(modules[kind]+child_return_offset(kind),&returning,sizeof(returning));
        if(!kind) {
            struct kui_smash_native_menu_seed seed={.magic0=KUI_SMASH_NATIVE_MENU_SEED_MAGIC0,
                .magic1=KUI_SMASH_NATIVE_MENU_SEED_MAGIC1,.version=1u,.bytes=304u};
            memcpy(modules[kind]+owner_at,&seed,sizeof(seed));continue;
        }
        bool swirl=kind==2u;struct disk_file *helper=&disk[swirl?ROLE_SWIRL_HELPER:ROLE_VC2_HELPER];
        struct kui_smash_ce_profile owner={.magic0=KUI_SMASH_CE_PROFILE_MAGIC0,.magic1=KUI_SMASH_CE_PROFILE_MAGIC1,
            .version=1u,.bytes=80u,.kind=kind,.virtual_slot=swirl?1u:2u,.flags=swirl?7u:3u,
            .kernel_fad=swirl?493127u:529805u,.kernel_sectors=swirl?893u:899u,
            .kernel_bytes=swirl?0x1be800u:0x1c1800u,.body_bytes=swirl?0x1be000u:0x1c1000u,
            .body_crc32=swirl?0x14a27ac2u:0x6d9c21a3u,.prefix_crc32=swirl?0x7e29bc00u:0xa253bfefu,
            .ram_end=swirl?0x8cfb8000u:0x8cfc0000u,.helper_bytes=49152u,
            .helper_crc32=kui_retail_crc32(0u,helper->data,49152u),.reserve_begin=0x8cfc4000u,.reserve_bytes=49152u};
        owner.crc32=kui_retail_crc32(0u,&owner,sizeof(owner));memcpy(modules[kind]+owner_at,&owner,sizeof(owner));
        struct kui_vc2_vmu_late_seed late={.magic0=KUI_VC2_LATE_SEED_MAGIC0,.magic1=KUI_VC2_LATE_SEED_MAGIC1,
            .version=2u,.bytes=44u,.helper_bytes=49152u,.helper_crc32=owner.helper_crc32};
        memcpy(modules[kind]+late_at,&late,sizeof(late));
        struct kui_smash_ce_child_seed seed={.magic0=KUI_SMASH_CE_CHILD_MAGIC0,.magic1=KUI_SMASH_CE_CHILD_MAGIC1,
            .version=1u,.bytes=64u,.profile=kind};memcpy(modules[kind]+child_at,&seed,sizeof(seed));
    }
    struct kui_smash_menu_collection collection={.magic0=KUI_SMASH_MENU_COLLECTION_MAGIC0,
        .magic1=KUI_SMASH_MENU_COLLECTION_MAGIC1,.version=1u,.bytes=224u,.count=3u,.flags=7u};
    memcpy(modules[3]+collection_at,&collection,sizeof(collection));memcpy(modules[3]+return_at,&returning,sizeof(returning));
    memcpy(outer_template,modules[3],sizeof(outer_template));
}
static void fixture(bool reset_disk) {
    assert(active<0);fs=(FATFS){.fs_type=FS_FAT32,.database=6144u,.csize=1u,.n_fatent=65538u};
    volume=(struct kui_volume){.start=2048u,.count=1048576u-2048u};
    if(reset_disk) {
        memset(disk,0,sizeof(disk));for(unsigned i=0u;i<SMASH_CAP_COUNT;++i)disk[i].cluster=50000u+i*1000u;
        for(unsigned role=ROLE_VC2_HELPER;role<=ROLE_SWIRL_HELPER;++role) {
            disk[role].present=true;disk[role].bytes=49152u;
            for(unsigned i=0u;i<49152u;++i)disk[role].data[i]=(uint8_t)((i*37u+role*19u)^(i>>6));
        }
        disk[ROLE_BINDING].present=true;disk[ROLE_BINDING].bytes=64u;char key[12];
        assert(kui_vmu_game_card_key(KUI_VC2_PERSIST_PRODUCT,key) && kui_vmu_game_card_binding_encode(key,disk[ROLE_BINDING].data));
    }
    map=(struct kui_retail_manifest){.card_sectors=1048576u,.partition_start=2048u,.partition_end=1048576u,
        .track_count=1u,.extent_count=1u,.session_lba=45000u,.boot_lba=45020u,
        .boot_bytes=KUI_SMASH_NATIVE_MENU_BYTES,.boot_crc32=KUI_SMASH_NATIVE_MENU_CRC32,
        .ip_crc32=KUI_SMASH_NATIVE_IP_CRC32,.flags=KUI_RETAIL_IMAGE_BOOT_CRC,
        .storage_transport=KUI_STORAGE_SCI,.reader=KUI_RETAIL_READER_STANDARD};
    strcpy(map.product,"MK-51146");strcpy(map.title,"Sega Smash Pack");strcpy(map.bootfile,"1ST_READ.BIN");strcpy(map.region,"JUE");
    map.slots[0].track=(struct kui_retail_track){.start_lba=45000u,.end_lba=50000u,
        .first_extent=1u,.extent_count=1u,.control=4u|KUI_RETAIL_TRACK_COOKED};
    map.slots[1].extent=(struct kui_retail_extent){.card_lba=100000u,.blocks=20000u};
    assert(kui_retail_manifest_validate(&map)==KUI_GAME_OK);
    metadata=(struct kui_game_metadata){.root_lba=100u,.root_bytes=2048u};
    memset(&image,0,sizeof(image));module_templates();
    outer=(struct kui_runtime_image){.data=modules[3],.info={.payload_bytes=16384u,.memory_bytes=16384u}};
    memcpy(outer.info.build,build_id,13u);selects=histories=identity_reads=source_proofs=module_reads=0u;
    phase_text_opens=best_effort_report_refusals=0u;
    fail_sync=corrupt_read=module_fault=-1;
    resolver_calls=resolved_helper_opens=0u;resolver_mapped=resolver_failed=false;
}
static bool prepare(void) {
    struct files files={.root="/Smash"};const struct kui_game_metadata_ops ops={0};const char *problem=NULL;
    bool ok=smash_menu_collection_prepare(&files,&image,&ops,&metadata,&map,&fs,&volume,build_id,&outer,&problem,logger,cancel);
    assert(active<0 && problem);return ok;
}
static bool return_blank_at(const uint8_t *payload,size_t at) {
    struct kui_smash_menu_return_seed returning;memcpy(&returning,payload+at,sizeof(returning));
    return kui_smash_menu_return_seed_blank(&returning);
}
static void verify(void) {
    assert(selects==1u && histories==1u && identity_reads==2u && source_proofs==1u && module_reads==3u);
    struct kui_smash_menu_collection collection;memcpy(&collection,modules[3]+collection_at,sizeof(collection));
    assert(kui_smash_menu_collection_valid(&collection));
    assert(!memcmp(&collection,disk[SMASH_CAP_OUTER].data+collection_at,sizeof(collection)));
    for(unsigned i=0u;i<3u;++i) {
        struct disk_file *d=&disk[i];assert(d->present && d->bytes==16384u && d->syncs && d->reads);
        assert(!memcmp(d->data+profile_at,selected_wire,4096u) && return_blank_at(d->data,child_return_offset(i)));
        assert(collection.plans[i].payload_crc32==kui_retail_crc32(0u,d->data,d->bytes));
        assert(collection.plans[i].first_lba==8192u+d->cluster-2u && collection.plans[i].block_count==32u);
        struct kui_retail_manifest decoded;assert(kui_retail_manifest_decode(d->data+KUI_RETAIL_MAP_OFFSET,&decoded)==KUI_GAME_OK);
        assert(decoded.boot_bytes==(i?KUI_SMASH_CE_SAMPLE_BYTES:KUI_SMASH_NATIVE_ENGINE_BYTES));
        assert(!strcmp(decoded.bootfile,i?"SAMPLE.BIN":"MDE_US.BIN"));
        assert(decoded.storage_transport==KUI_STORAGE_SCI && decoded.reader==KUI_RETAIL_READER_STANDARD &&
            decoded.flags==KUI_RETAIL_IMAGE_BOOT_CRC);
        if(i) {
            struct kui_smash_ce_profile owner;assert(kui_smash_ce_profile_find(d->data,d->bytes,i,&owner)==1);
            struct kui_smash_ce_child_seed seed;memcpy(&seed,d->data+child_at,sizeof(seed));
            assert(kui_smash_ce_child_blank(&seed) && seed.profile==i && owner.virtual_slot==(i==1u?2u:1u));
            struct kui_vc2_vmu_late_seed late;memcpy(&late,d->data+late_at,sizeof(late));
            assert(late.helper_crc32==owner.helper_crc32 && late.first_lba==8192u+disk[i==1u?SMASH_CAP_VC2_SESSION:SMASH_CAP_SWIRL_SESSION].cluster-2u && late.first_blocks==99u);
            uint32_t seal=late.seed_crc32;late.seed_crc32=0u;assert(seal==kui_retail_crc32(0u,&late,sizeof(late)));
        }
    }
    assert(return_blank_at(disk[SMASH_CAP_OUTER].data,return_at));
    struct kui_smash_menu_return_seed returning;memcpy(&returning,modules[3]+return_at,sizeof(returning));
    assert(kui_smash_menu_return_seed_valid(&returning) && returning.flags==KUI_SMASH_MENU_RETURN_CONFIGURED);
    assert(returning.outer.payload_crc32==kui_retail_crc32(0u,disk[SMASH_CAP_OUTER].data,disk[SMASH_CAP_OUTER].bytes));
    assert(returning.outer.first_lba==8192u+disk[SMASH_CAP_OUTER].cluster-2u);
    for(unsigned i=SMASH_CAP_VC2_SESSION;i<=SMASH_CAP_SWIRL_SESSION;++i) {
        assert(disk[i].bytes==50688u && !memcmp(disk[i].data,disk[i==SMASH_CAP_VC2_SESSION?ROLE_VC2_HELPER:ROLE_SWIRL_HELPER].data,49152u));
    }
    assert(memcmp(disk[SMASH_CAP_VC2_SESSION].data,disk[SMASH_CAP_SWIRL_SESSION].data,49152u));
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *input=fopen(argv[1],"rb");assert(input);
    assert(fread(template_wire,1u,4096u,input)==4096u && fgetc(input)==EOF && !fclose(input));
    struct kui_vmu_persist_profile profile;assert(!kui_vmu_persist_profile_decode(&profile,template_wire,4096u));
    memset(profile.cid,0x27,16u);assert(!kui_vmu_persist_profile_encode(&profile,selected_wire));
    fixture(true);assert(prepare());verify();
    /* Opt into the new typed diagnostic capability. Its unavailable TXT and
     * this fixture's absent metadata-reader callbacks must not invalidate
     * the already prepared collection or any of its six capsule plans. */
    fixture(true);
    static const uint8_t text_marker[16]={
        'K','U','I','S','T','X','T','1',1u,0u,0u,0u,16u,0u,0u,0u};
    memcpy(modules[0]+0x2080u,text_marker,sizeof(text_marker));
    assert(prepare());verify();assert(phase_text_opens==1u && best_effort_report_refusals==2u);
    fixture(true);resolver_mapped=true;assert(prepare());verify();
    assert(resolver_calls==2u && resolved_helper_opens==2u);
    unsigned writes[SMASH_CAP_COUNT];for(unsigned i=0u;i<SMASH_CAP_COUNT;++i)writes[i]=disk[i].writes;
    memset(disk[SMASH_CAP_VC2_SESSION].data+49152u,0x6e,1536u);memset(disk[SMASH_CAP_SWIRL_SESSION].data+49152u,0x6e,1536u);
    fixture(false);assert(prepare());verify();
    for(unsigned i=0u;i<SMASH_CAP_COUNT;++i)assert(writes[i]==disk[i].writes);
    for(unsigned i=SMASH_CAP_VC2_SESSION;i<=SMASH_CAP_SWIRL_SESSION;++i)
        for(unsigned j=49152u;j<50688u;++j)assert(disk[i].data[j]==0x6eu);
    for(unsigned role=0u;role<SMASH_CAP_CARD_SESSION;++role) {
        fixture(true);fail_sync=(int)role;assert(!prepare() && return_blank_at(modules[3],return_at));
        fixture(true);corrupt_read=(int)role;assert(!prepare() && return_blank_at(modules[3],return_at));
    }
    fixture(true);resolver_failed=true;assert(!prepare() && return_blank_at(modules[3],return_at));
    assert(resolver_calls==1u && !resolved_helper_opens && !disk[ROLE_VC2_HELPER].reads &&
        !disk[SMASH_CAP_VC2_SESSION].writes && !disk[SMASH_CAP_OUTER].writes);
    for(unsigned kind=0u;kind<3u;++kind) {
        fixture(true);module_fault=(int)kind;assert(!prepare() && !selects && !histories && return_blank_at(modules[3],return_at));
        for(unsigned role=0u;role<SMASH_CAP_COUNT;++role)assert(!disk[role].writes);
        /* The surviving chooser computes this offset without a scan. A
         * unique valid return seed at any other offset cannot be admitted. */
        fixture(true);size_t old=child_return_offset(kind);
        memcpy(modules[kind]+0x3800u,modules[kind]+old,KUI_SMASH_MENU_RETURN_SEED_BYTES);
        memset(modules[kind]+old,0x55,KUI_SMASH_MENU_RETURN_SEED_BYTES);
        assert(!prepare() && !selects && !histories && return_blank_at(modules[3],return_at));
        for(unsigned role=0u;role<SMASH_CAP_COUNT;++role)assert(!disk[role].writes);
    }
    puts("PASS production unified preparation: one shared card/history, identical wire in3 backends, distinct CE helpers/seeds,6 readback-checked capsules, sealed3plans, disk-blank/launch-only return, reuse and unpublished failures");
    return 0;
}
