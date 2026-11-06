/* SPDX-License-Identifier: GPL-3.0-only */
/* Production launcher preparation with a byte-backed FatFs model.  The card
 * service is an explicit seam: its own ownership and recovery tests cover it. */
#include "../src/apps/games_retail.c"
#include "kui/vc2_ce_vmu_persist.h"
#include <assert.h>
#include <stdarg.h>

enum fixture_file { FIX_NONE, FIX_HELPER, FIX_CAPSULE, FIX_BINDING, FIX_FILES };
enum fixture_operation { FIX_OPEN, FIX_READ, FIX_WRITE, FIX_SEEK, FIX_SYNC, FIX_CLOSE };
struct fixture_handle { FIL *file;enum fixture_file kind; };
static struct fixture_handle handles[8];
static FATFS fixture_fs,other_fs;
static struct kui_volume fixture_volume;
static uint8_t payload[12288],before_payload[sizeof(payload)];
static uint8_t helper[KUI_VC2_PERSIST_HELPER_BYTES];
static uint8_t capsule[KUI_VC2_PERSIST_CAPSULE_BYTES];
static uint8_t binding[KUI_VMU_GAME_CARD_BINDING_BYTES];
static uint8_t template_wire[KUI_VMU_PERSIST_PROFILE_WIRE_BYTES];
static uint8_t selected_wire[KUI_VMU_PERSIST_PROFILE_WIRE_BYTES];
static struct kui_runtime_image module;
static struct kui_retail_manifest game_map;
static DWORD fixture_table[16];
static size_t table_words;
static unsigned operations[FIX_FILES][6],active_handles,selects,logs;
static enum fixture_file failed_file,wrong_fs_file,short_file;
static enum fixture_operation failed_operation,short_operation;
static unsigned failed_ordinal,short_ordinal,cancel_after_reads;
static bool cancel_now,bad_service,mutate_after_verify,corrupt_readback,capsule_missing;
static uint64_t helper_bytes;
static int service_result;
static size_t source_bytes_read;
static unsigned unified_kind,publish_calls,publish_fault,identity_calls;
static bool bad_identity;
static bool resolver_mapped,resolver_failed;
static unsigned resolver_calls,resolved_helper_opens;
bool kui_runtime_resolve_path(const char *path,char *out,size_t capacity) {
    ++resolver_calls;if(resolver_failed)return false;
    int bytes=snprintf(out,capacity,"%s%s",resolver_mapped?"resolved:":"",path);
    return bytes>=0 && (size_t)bytes<capacity;
}
static const size_t seed_at=128u,profile_at=1024u;
static FRESULT probe_stat_result;
static BYTE probe_attribute;
static enum kui_runtime_result probe_runtime_result;
static enum kui_game_result probe_proof_result;
static const struct kui_runtime_image *probe_source;
static unsigned probe_stats,probe_loads,probe_frees,probe_proofs;
static bool probe_bad_ip,probe_bad_sha,probe_bad_crc;
FRESULT f_stat(const TCHAR *path,FILINFO *info) {
    assert(!strcmp(path,KUI_GAMES_RETAIL_PHYSICAL_PACKAGE));++probe_stats;
    memset(info,0,sizeof(*info));info->fattrib=probe_attribute;return probe_stat_result;
}
enum kui_runtime_result kui_runtime_read(const char *path,struct kui_runtime_image *out,
    kui_log_fn log,kui_cancel_fn cancel) {
    assert(!strcmp(path,KUI_GAMES_RETAIL_PHYSICAL_PACKAGE) && !out->data && log && cancel && !active_handles);
    ++probe_loads;if(probe_runtime_result!=KUI_RUNTIME_OK)return probe_runtime_result;
    out->info=probe_source->info;out->data=malloc(out->info.payload_bytes);assert(out->data);
    memcpy(out->data,probe_source->data,out->info.payload_bytes);return KUI_RUNTIME_OK;
}
void kui_runtime_free(struct kui_runtime_image *out) {
    ++probe_frees;free(out->data);memset(out,0,sizeof(*out));
}
const char *kui_game_result_name(enum kui_game_result result) {
    (void)result;return "fixture source read failure";
}

static struct kui_vc2_vmu_late_seed *seed(void) {return (void *)(payload+seed_at);}
static enum fixture_file kind(FIL *file) {
    for(unsigned i=0u;i<sizeof(handles)/sizeof(handles[0]);i++)
        if(handles[i].file==file)return handles[i].kind;
    assert(!"unknown FatFs handle");return FIX_NONE;
}
static bool operation(enum fixture_file which,enum fixture_operation op) {
    unsigned n=++operations[which][op];
    return which==failed_file && op==failed_operation && n==failed_ordinal;
}
static bool shortened(enum fixture_file which,enum fixture_operation op) {
    return which==short_file && op==short_operation &&
        operations[which][op]==short_ordinal;
}
static void seed_unpublished(void) {
    assert(!memcmp(payload+seed_at,before_payload+seed_at,sizeof(*seed())));
}
FRESULT f_open(FIL *file,const TCHAR *name,BYTE mode) {
    enum fixture_file which=FIX_NONE;
    bool mapped=!strncmp(name,"resolved:",9u);if(mapped)name+=9u;
    const char *helper_path=KUI_VC2_LATE_FILE_PATH,*session_path=KUI_VC2_PERSIST_CAPSULE_PATH;
    if(unified_kind)assert(kui_smash_ce_profile_paths(unified_kind,NULL,&helper_path,&session_path));
    if(!strcmp(name,helper_path)) {
        if(mapped)++resolved_helper_opens;
        which=FIX_HELPER;assert(mode==FA_READ);
    } else if(!strcmp(name,session_path)) which=FIX_CAPSULE;
    else if(!strcmp(name,"0:/KUI/vmu-test/p0001/game.bin")) {
        which=FIX_BINDING;assert(mode==FA_READ);
    }
    /* No other user card or journal is writable through this UI path. */
    assert(which!=FIX_NONE);seed_unpublished();
    if(which==FIX_CAPSULE && (mode&FA_WRITE)) {
        assert(source_bytes_read>=sizeof(helper));
        assert(operations[FIX_HELPER][FIX_CLOSE]);
    }
    if(operation(which,FIX_OPEN))return FR_DISK_ERR;
    if(which==FIX_CAPSULE && mode==FA_READ && capsule_missing)return FR_NO_FILE;
    memset(file,0,sizeof(*file));
    file->obj.fs=wrong_fs_file==which?&other_fs:&fixture_fs;
    file->obj.objsize=which==FIX_HELPER?helper_bytes:
        which==FIX_CAPSULE?sizeof(capsule):sizeof(binding);
    if(which==FIX_CAPSULE && (mode&FA_CREATE_ALWAYS)) {
        capsule_missing=false;
        memset(capsule,0xcc,sizeof(capsule));file->obj.objsize=0u;
    }
    if(which==FIX_CAPSULE && mode==FA_READ && corrupt_readback)
        capsule[sizeof(capsule)-1u]^=1u;
    bool added=false;
    for(unsigned i=0u;i<sizeof(handles)/sizeof(handles[0]);i++)if(!handles[i].file) {
        handles[i]=(struct fixture_handle){file,which};added=true;break;
    }
    assert(added);++active_handles;return FR_OK;
}
FRESULT f_lseek(FIL *file,FSIZE_t offset) {
    enum fixture_file which=kind(file);seed_unpublished();
    if(operation(which,FIX_SEEK))return FR_DISK_ERR;
    if(offset==CREATE_LINKMAP) {
        assert(which==FIX_CAPSULE && file->cltbl);
        if(file->cltbl[0]<table_words) {file->cltbl[0]=(DWORD)table_words;return FR_NOT_ENOUGH_CORE;}
        memcpy(file->cltbl,fixture_table,table_words*sizeof(*fixture_table));return FR_OK;
    }
    assert(offset<=file->obj.objsize);file->fptr=offset;return FR_OK;
}
FRESULT f_read(FIL *file,void *buffer,UINT want,UINT *got) {
    enum fixture_file which=kind(file);seed_unpublished();*got=0u;
    if(operation(which,FIX_READ))return FR_DISK_ERR;
    const uint8_t *data=which==FIX_HELPER?helper:which==FIX_CAPSULE?capsule:binding;
    size_t bytes=which==FIX_HELPER?sizeof(helper):which==FIX_CAPSULE?sizeof(capsule):sizeof(binding);
    assert(file->fptr<=bytes);
    *got=want>bytes-file->fptr?(UINT)(bytes-file->fptr):want;
    if(shortened(which,FIX_READ) && *got)--*got;
    memcpy(buffer,data+file->fptr,*got);file->fptr+=*got;
    if(which==FIX_HELPER)source_bytes_read+=*got;
    return FR_OK;
}
FRESULT f_write(FIL *file,const void *buffer,UINT want,UINT *got) {
    enum fixture_file which=kind(file);seed_unpublished();*got=0u;
    assert(which==FIX_CAPSULE && source_bytes_read>=sizeof(helper));
    if(operation(which,FIX_WRITE))return FR_DISK_ERR;
    assert(file->fptr<=sizeof(capsule) && want<=sizeof(capsule)-file->fptr);
    *got=shortened(which,FIX_WRITE)?want-1u:want;
    memcpy(capsule+file->fptr,buffer,*got);file->fptr+=*got;
    if(file->fptr>file->obj.objsize)file->obj.objsize=file->fptr;
    return FR_OK;
}
FRESULT f_sync(FIL *file) {
    enum fixture_file which=kind(file);seed_unpublished();assert(which==FIX_CAPSULE);
    return operation(which,FIX_SYNC)?FR_DISK_ERR:FR_OK;
}
FRESULT f_close(FIL *file) {
    enum fixture_file which=kind(file);seed_unpublished();assert(!file->cltbl);
    bool failed=operation(which,FIX_CLOSE),removed=false;
    if(which==FIX_HELPER && mutate_after_verify && operations[which][FIX_CLOSE]==1u)
        helper[sizeof(helper)-1u]^=1u;
    for(unsigned i=0u;i<sizeof(handles)/sizeof(handles[0]);i++)if(handles[i].file==file) {
        handles[i]=(struct fixture_handle){0};removed=true;break;
    }
    assert(removed && active_handles);--active_handles;return failed?FR_DISK_ERR:FR_OK;
}
FRESULT f_mount(FATFS *fs,const TCHAR *path,BYTE immediate) {
    (void)fs;(void)immediate;assert(!strcmp(path,"0:"));return FR_OK;
}
const struct kui_volume *kui_media_volume(void) {return &fixture_volume;}
bool kui_sd_vmu_profile_identity(struct kui_vmu_sd_identity *out) {
    ++identity_calls;
    memset(out,0,sizeof(*out));memset(out->cid,0x27,sizeof(out->cid));
    if(bad_identity)out->cid[0]^=1u;
    out->sectors=1048576u;out->high_capacity=true;return true;
}
bool kui_sd_raw_read_ops(struct kui_media_ops *out) {(void)out;return false;}
bool kui_clock_now(struct kui_datetime *out) {(void)out;return false;}
bool kui_clock_to_seconds(const struct kui_datetime *value,int64_t *out) {(void)value;(void)out;return false;}
int kui_vmu_game_card_select(const char *product,uint64_t first,uint64_t end,
    const struct kui_vmu_fs_range *forbidden,size_t forbidden_count,
    const struct kui_vmu_game_card_platform *platform,struct kui_vmu_game_card_work *work,
    uint8_t wire[KUI_VMU_PERSIST_PROFILE_WIRE_BYTES],struct kui_vmu_game_card_choice *choice,
    kui_log_fn log,kui_cancel_fn cancel) {
    (void)platform;(void)work;(void)log;(void)cancel;
    seed_unpublished();assert(!active_handles && !strcmp(product,KUI_VC2_PERSIST_PRODUCT));
    assert(first==fixture_volume.start && end==(uint64_t)first+fixture_volume.count);
    assert(forbidden_count==1u && forbidden && forbidden[0].lba==20000u && forbidden[0].blocks==96u);
    ++selects;memset(choice,0,sizeof(*choice));choice->pair=1u;
    assert(kui_vmu_game_card_key(bad_service?"OLDUSER":KUI_VC2_PERSIST_PRODUCT,choice->key));
    memcpy(wire,selected_wire,sizeof(selected_wire));return service_result;
}
static unsigned history_calls,creation_attempts;
int kui_vmu_history_allocation_mounted(kui_cancel_fn cancel) {(void)cancel;assert(!"native child must not create cards");return -1;}
int kui_vmu_history_launch_mounted(const struct kui_vmu_game_selection *card,
    struct kui_vmu_history_view *view,kui_log_fn log,kui_cancel_fn cancel) {
    (void)view;(void)log;(void)cancel;char key[12];
    assert(kui_vmu_game_card_key(KUI_VC2_PERSIST_PRODUCT,key));
    assert(card->selected && card->pair==1u && !memcmp(card->product,key,sizeof(key)));
    ++history_calls;return 0;
}
int kui_vmu_game_card_select_boundary(const char *product,
    const struct kui_vmu_game_candidate_target *target,const struct kui_vmu_game_candidates_storage *storage,
    uint64_t first,uint64_t end,const struct kui_vmu_fs_range *forbidden,size_t forbidden_count,
    const struct kui_vmu_game_card_platform *platform,struct kui_vmu_game_card_work *work,
    uint8_t wire[KUI_VMU_PERSIST_PROFILE_WIRE_BYTES],struct kui_vmu_game_card_choice *choice,
    kui_vmu_game_card_boundary boundary,void *boundary_context,kui_log_fn log,kui_cancel_fn cancel) {
    assert(!target && !storage && boundary && boundary_context==platform->context);
    if(service_result==KUI_VMU_GAME_CARD_UNBOUND) {
        ++creation_attempts;int refused=boundary(boundary_context,KUI_VMU_GAME_CARD_BEFORE_CREATE,
            product,0u,NULL,NULL,log,cancel);assert(refused!=KUI_VMU_GAME_CARD_READY);return refused;
    }
    int result=kui_vmu_game_card_select(product,first,end,forbidden,forbidden_count,platform,work,wire,choice,log,cancel);
    struct kui_vmu_game_candidates_storage fresh={0};char canonical[12];
    assert(kui_vmu_game_card_key(product,canonical));
    return result==KUI_VMU_GAME_CARD_READY?boundary(boundary_context,KUI_VMU_GAME_CARD_RESOLVED,
        canonical,choice->pair,(const uint8_t[16]){0x27},&fresh,log,cancel):result;
}
static bool cancelled(void) {
    return cancel_now || (cancel_after_reads && operations[FIX_HELPER][FIX_READ]>=cancel_after_reads);
}
static void logger(const char *format,...) {(void)format;++logs;}
static void fixture(void) {
    memset(&fixture_fs,0,sizeof(fixture_fs));fixture_fs.fs_type=FS_FAT32;
    fixture_fs.database=6144u;fixture_fs.csize=1u;fixture_fs.n_fatent=65538u;
    fixture_volume=(struct kui_volume){.start=2048u,.count=1048576u-2048u};
    for(unsigned i=0u;i<sizeof(helper);i++)helper[i]=(uint8_t)((i*37u+17u)^(i>>6));
    memset(capsule,0xa7,sizeof(capsule));memset(payload,0xa5,sizeof(payload));
    memcpy(payload+profile_at,template_wire,sizeof(template_wire));
    module=(struct kui_runtime_image){.data=payload,.info={.payload_bytes=sizeof(payload)}};
    game_map=(struct kui_retail_manifest){.partition_start=fixture_volume.start,
        .partition_end=(uint64_t)fixture_volume.start+fixture_volume.count,.track_count=1u,.extent_count=1u};
    game_map.slots[1].extent=(struct kui_retail_extent){.card_lba=20000u,.blocks=96u};
    *seed()=(struct kui_vc2_vmu_late_seed){
        .magic0=KUI_VC2_LATE_SEED_MAGIC0,.magic1=KUI_VC2_LATE_SEED_MAGIC1,
        .version=KUI_VC2_PERSIST_SEED_VERSION,.bytes=KUI_VC2_LATE_SEED_BYTES,
        .helper_bytes=KUI_VC2_PERSIST_HELPER_BYTES,
        .helper_crc32=kui_retail_crc32(0u,helper,sizeof(helper))};
    memcpy(before_payload,payload,sizeof(payload));
    memset(fixture_table,0,sizeof(fixture_table));
    fixture_table[0]=table_words=4u;fixture_table[1]=99u;fixture_table[2]=5000u;
    memset(handles,0,sizeof(handles));memset(operations,0,sizeof(operations));
    active_handles=selects=logs=source_bytes_read=failed_ordinal=short_ordinal=cancel_after_reads=0u;
    failed_file=wrong_fs_file=short_file=FIX_NONE;failed_operation=short_operation=FIX_OPEN;
    cancel_now=bad_service=mutate_after_verify=corrupt_readback=false;
    capsule_missing=true;
    unified_kind=publish_calls=publish_fault=identity_calls=0u;bad_identity=false;
    resolver_mapped=resolver_failed=false;resolver_calls=resolved_helper_opens=0u;
    helper_bytes=sizeof(helper);service_result=KUI_VMU_GAME_CARD_READY;
    char key[KUI_VMU_GAME_CARD_KEY_BYTES];assert(kui_vmu_game_card_key(KUI_VC2_PERSIST_PRODUCT,key));
    assert(kui_vmu_game_card_binding_encode(key,binding));
}
static bool prepare(void) {
    const char *problem=NULL;
    bool ok=vc2_ce_vmu_persist_prepare_launch(&fixture_fs,&fixture_volume,&game_map,
        &module,&problem,logger,cancelled);
    assert(problem && !active_handles);
    return ok;
}
static void refused(void) {
    memcpy(before_payload,payload,sizeof(payload));
    assert(!prepare());seed_unpublished();
}
static void accepted(uint32_t first,uint32_t count,uint32_t second) {
    memcpy(before_payload,payload,sizeof(payload));
    assert(prepare());
    assert(selects==1u && operations[FIX_HELPER][FIX_OPEN] &&
        operations[FIX_HELPER][FIX_READ]>=12u && operations[FIX_HELPER][FIX_CLOSE]);
    assert(operations[FIX_CAPSULE][FIX_CLOSE]);
    if(operations[FIX_CAPSULE][FIX_WRITE])assert(operations[FIX_CAPSULE][FIX_SYNC]);
    else assert(!operations[FIX_CAPSULE][FIX_SYNC]);
    assert(seed()->version==KUI_VC2_PERSIST_SEED_VERSION &&
        seed()->first_lba==first && seed()->first_blocks==count && seed()->second_lba==second &&
        !seed()->reserved);
    uint32_t crc=seed()->seed_crc32;seed()->seed_crc32=0u;
    assert(kui_retail_crc32(0u,seed(),sizeof(*seed()))==crc);seed()->seed_crc32=crc;
    assert(kui_retail_crc32(0u,capsule,sizeof(helper))==seed()->helper_crc32);
    if(!mutate_after_verify)assert(!memcmp(capsule,helper,sizeof(helper)));
    for(size_t i=sizeof(helper);i<sizeof(capsule);i++)
        assert(capsule[i]==(operations[FIX_CAPSULE][FIX_WRITE]?0u:0x6eu));
    assert(!memcmp(payload+profile_at,selected_wire,sizeof(selected_wire)));
    for(size_t i=0u;i<sizeof(payload);i++) {
        bool seed_byte=i>=seed_at && i<seed_at+sizeof(*seed());
        bool profile_byte=i>=profile_at && i<profile_at+sizeof(selected_wire);
        if(!seed_byte && !profile_byte)assert(payload[i]==before_payload[i]);
    }
}
static void split_at(uint32_t first) {
    table_words=6u;fixture_table[0]=(DWORD)table_words;
    fixture_table[1]=first;fixture_table[2]=5000u;
    fixture_table[3]=KUI_VC2_PERSIST_CAPSULE_BLOCKS-first;fixture_table[4]=8000u;
}
static void failure(enum fixture_file file,enum fixture_operation op,unsigned ordinal) {
    failed_file=file;failed_operation=op;failed_ordinal=ordinal;refused();
}
static void return_transport_reuse(void) {
    /* The original raw helper remains immutable. Only the exact typed632B
     * runtime window may differ after a preceding launch. */
    const uint32_t runtime=256u,begin=41184u+runtime,end=begin+632u;
    for(unsigned scenario=0u;scenario<6u;scenario++) {
        fixture();memset(helper+41152u,0,sizeof(helper)-41152u);
        struct kui_smash_ce_transport transport={KUI_SMASH_CE_TRANSPORT_MAGIC0,KUI_SMASH_CE_TRANSPORT_MAGIC1,
            1u,32u,41152u,1024u,runtime,632u};
        memcpy(helper+41152u,&transport,sizeof(transport));
        assert(kui_smash_ce_transport_static(helper,sizeof(helper),NULL));
        seed()->helper_crc32=kui_retail_crc32(0u,helper,sizeof(helper));
        memcpy(capsule,helper,sizeof(helper));memset(capsule+sizeof(helper),0x6e,sizeof(capsule)-sizeof(helper));
        memset(capsule+begin,0x9a,632u);capsule_missing=false;
        const uint32_t outside[]={0u,41152u+4u,begin-1u,end,41184u+1024u};
        if(scenario) capsule[outside[scenario-1u]]^=1u;
        uint8_t prior[sizeof(capsule)];memcpy(prior,capsule,sizeof(prior));
        memcpy(before_payload,payload,sizeof(before_payload));
        assert(prepare() && !active_handles);
        if(!scenario) {
            assert(!operations[FIX_CAPSULE][FIX_WRITE] && !operations[FIX_CAPSULE][FIX_SYNC]);
            assert(!memcmp(prior,capsule,sizeof(capsule)));
        } else {
            assert(operations[FIX_CAPSULE][FIX_WRITE] && operations[FIX_CAPSULE][FIX_SYNC]);
            assert(!memcmp(capsule,helper,sizeof(helper)));
            for(size_t i=sizeof(helper);i<sizeof(capsule);i++)assert(!capsule[i]);
        }
        assert(seed()->helper_crc32==kui_retail_crc32(0u,helper,sizeof(helper)) &&
            !memcmp(payload+profile_at,selected_wire,sizeof(selected_wire)));
    }
    puts("PASS CE return carrier reuse: old runtime only causes zero writes; every adjacent/header/raw/slack static mismatch recreates verified static code");
}
static void smash_native_launch_map(void) {
    /* Original supplied disc topology:40 tracks, onlyT1/T3/T40 mapped, each
     * split into two physical runs like the failing console preparation. */
    static const uint32_t starts[40]={0u,6483u,45000u,325501u,326339u,331043u,
        331643u,336738u,337338u,337938u,338538u,339138u,348454u,357460u,
        367354u,375706u,384189u,393058u,401435u,416382u,424567u,432492u,
        437767u,445258u,445969u,451108u,457128u,457728u,458328u,458928u,
        459528u,460128u,460728u,461328u,461928u,462528u,463128u,463765u,
        464365u,485566u};
    struct kui_retail_manifest original={.track_count=40u,.extent_count=6u,
        .storage_transport=KUI_STORAGE_SCI,.card_sectors=249997312u,
        .partition_start=2048u,.partition_end=249997312u,.session_lba=45000u,
        .boot_lba=548896u,.boot_bytes=KUI_SMASH_NATIVE_MENU_BYTES,
        .boot_crc32=KUI_SMASH_NATIVE_MENU_CRC32,.ip_crc32=KUI_SMASH_NATIVE_IP_CRC32,
        .flags=KUI_RETAIL_IMAGE_BOOT_CRC};
    strcpy(original.title,"SEGA SMASH PACK VOLUME 1");strcpy(original.product,"MK-51146");
    strcpy(original.region,"U");strcpy(original.bootfile,"1ST_READ.BIN");
    unsigned next=40u;uint32_t physical_lba=10000u;
    for(unsigned i=0u;i<40u;i++) {
        uint32_t end=i==0u?6333u:i==1u?7009u:i==2u?325351u:i==39u?549150u:starts[i+1u];
        bool data=i==0u || i==2u || i==39u;
        original.slots[i].track=(struct kui_retail_track){starts[i],end,(uint16_t)next,
            (uint8_t)(data?2u:0u),(uint8_t)(data?4u:0u)};
        if(!data)continue;
        uint32_t blocks=((end-starts[i])*2352u+511u)/512u;
        original.slots[next++].extent=(struct kui_retail_extent){0u,physical_lba,256u};
        original.slots[next++].extent=(struct kui_retail_extent){256u,physical_lba+512u,blocks-256u};
        physical_lba+=blocks+2048u;
    }
    assert(next==46u && kui_retail_manifest_validate(&original)==KUI_GAME_OK);
    uint8_t wire[KUI_RETAIL_IMAGE_WIRE_BYTES],untouched[sizeof(wire)];
    struct kui_retail_manifest map=original,decoded;
    struct kui_game_metadata metadata={.boot_lba=original.boot_lba,.boot_bytes=original.boot_bytes};
    strcpy(metadata.bootfile,original.bootfile);
    /* Reproduce the shipped short-string replacement through the independent
     * canonical wire validator. It fails before any loader handoff. */
    map.boot_lba=546117u;map.boot_bytes=KUI_SMASH_NATIVE_ENGINE_BYTES;
    snprintf(map.bootfile,sizeof(map.bootfile),"MDE_US.BIN");
    assert(!strcmp(map.bootfile,"MDE_US.BIN") && map.bootfile[11]=='N');
    memset(wire,0xa7,sizeof(wire));memcpy(untouched,wire,sizeof(wire));
    assert(kui_retail_manifest_encode(&map,wire)==KUI_GAME_INVALID);
    assert(!memcmp(wire,untouched,sizeof(wire)));
    for(unsigned child=KUI_SMASH_NATIVE_CHILD_PHANTASY_STAR_II;
        child<KUI_SMASH_NATIVE_CHILD_COUNT;child++) {
        map=original;map.boot_crc32=KUI_SMASH_NATIVE_ENGINE_CRC32;
        /* Execute the production launcher replacement, not a test copy. */
        smash_native_boot_target(&metadata,&map,546117u,KUI_SMASH_NATIVE_ENGINE_BYTES);
        assert(map.track_count==40u && map.extent_count==6u &&
            map.boot_lba==546117u && map.boot_bytes==KUI_SMASH_NATIVE_ENGINE_BYTES);
        assert(!memcmp(map.bootfile,metadata.bootfile,sizeof(map.bootfile)));
        assert(!memcmp(map.bootfile,"MDE_US.BIN",sizeof("MDE_US.BIN")));
        for(unsigned i=sizeof("MDE_US.BIN");i<sizeof(map.bootfile);i++)assert(!map.bootfile[i]);
        assert(kui_retail_manifest_encode(&map,wire)==KUI_GAME_OK);
        assert(kui_retail_manifest_decode(wire,&decoded)==KUI_GAME_OK);
        assert(!memcmp(&map,&decoded,sizeof(map)));
        /* The original CRC/source-owner guards are still required for each
         * selected child; changing filename padding cannot admit another MDE. */
        struct kui_smash_native_child_seed s={.child=child};
        assert(kui_smash_native_child_seed_seal(&s) && kui_smash_native_child_seed_valid(&s));
    }
    puts("PASS production Smash native target: stale short-name tail reproduces refusal; both children encode/decode40tracks and6extents with canonical metadata");
}
static void smash_collection_selection(const struct kui_runtime_image *actual,bool expected) {
    struct kui_game_metadata metadata={.ip_valid=true,.boot_valid=true,.native_gd=true,
        .boot_bytes=KUI_SMASH_NATIVE_MENU_BYTES};
    strcpy(metadata.product,"MK-51146");strcpy(metadata.version,"V1.002");
    strcpy(metadata.region,"U");strcpy(metadata.bootfile,"1ST_READ.BIN");
    struct kui_game_image image={.format=KUI_GAME_IMAGE_GDI};
    uint8_t sha[32]={0x3a,0x31,0x1b,0x18,0x9a,0x48,0x70,0xcc,
        0xbe,0xfc,0x4f,0xcd,0x30,0x6e,0x2d,0x38,0xe5,0xae,0x81,0x0a,0xe5,0xed,0xd2,0x25,
        0xaf,0x6b,0x9a,0xa6,0xd5,0xdf,0x5f,0x10};
#define ROUTE(transport,options,patched,enabled,child,ip) \
    smash_collection_route(&metadata,&image,transport,options,patched,enabled,child,ip,sha,actual)
    assert(ROUTE(KUI_STORAGE_SCI,0u,false,true,0u,KUI_SMASH_NATIVE_IP_CRC32)==expected);
    assert(!ROUTE(KUI_STORAGE_SCI,0u,false,false,0u,KUI_SMASH_NATIVE_IP_CRC32));
    for(unsigned child=1u;child<KUI_SMASH_NATIVE_CHILD_COUNT;child++)
        assert(!ROUTE(KUI_STORAGE_SCI,0u,false,true,child,KUI_SMASH_NATIVE_IP_CRC32));
    assert(!ROUTE(KUI_STORAGE_SCIF,0u,false,true,0u,KUI_SMASH_NATIVE_IP_CRC32));
    assert(!ROUTE(KUI_STORAGE_IDE,0u,false,true,0u,KUI_SMASH_NATIVE_IP_CRC32));
    assert(!ROUTE(KUI_STORAGE_SCI,KUI_RETAIL_READER_ASYNC,false,true,0u,KUI_SMASH_NATIVE_IP_CRC32));
    assert(!ROUTE(KUI_STORAGE_SCI,KUI_GAMES_RETAIL_CDDA,false,true,0u,KUI_SMASH_NATIVE_IP_CRC32));
    assert(!ROUTE(KUI_STORAGE_SCI,0u,true,true,0u,KUI_SMASH_NATIVE_IP_CRC32));
    assert(!ROUTE(KUI_STORAGE_SCI,0u,false,true,0u,KUI_SMASH_NATIVE_IP_CRC32^1u));
    for(unsigned i=0u;i<32u;i++) {sha[i]^=1u;
        assert(!ROUTE(KUI_STORAGE_SCI,0u,false,true,0u,KUI_SMASH_NATIVE_IP_CRC32));sha[i]^=1u;}
    metadata.version[5]='3';assert(!ROUTE(KUI_STORAGE_SCI,0u,false,true,0u,KUI_SMASH_NATIVE_IP_CRC32));metadata.version[5]='2';
    metadata.product[0]='X';assert(!ROUTE(KUI_STORAGE_SCI,0u,false,true,0u,KUI_SMASH_NATIVE_IP_CRC32));metadata.product[0]='M';
    metadata.boot_bytes--;assert(!ROUTE(KUI_STORAGE_SCI,0u,false,true,0u,KUI_SMASH_NATIVE_IP_CRC32));metadata.boot_bytes++;
    image.scrambled=true;assert(!ROUTE(KUI_STORAGE_SCI,0u,false,true,0u,KUI_SMASH_NATIVE_IP_CRC32));image.scrambled=false;
    image.compressed=true;assert(!ROUTE(KUI_STORAGE_SCI,0u,false,true,0u,KUI_SMASH_NATIVE_IP_CRC32));image.compressed=false;
    image.cd_image=true;assert(!ROUTE(KUI_STORAGE_SCI,0u,false,true,0u,KUI_SMASH_NATIVE_IP_CRC32));image.cd_image=false;
    image.format=KUI_GAME_IMAGE_CUE;assert(!ROUTE(KUI_STORAGE_SCI,0u,false,true,0u,KUI_SMASH_NATIVE_IP_CRC32));
#undef ROUTE
}
static void smash_collection_fixtures(void) {
    struct kui_native_foreground_capability cap={KUI_NATIVE_FOREGROUND_CAP_MAGIC0,
        KUI_NATIVE_FOREGROUND_CAP_MAGIC1,1u,32u,0u,48u,0u,KUI_NATIVE_FOREGROUND_CAP_VC2_PERSIST};
    struct kui_runtime_image candidate={.data=&cap,.info={.payload_bytes=sizeof(cap)}};
    smash_collection_selection(&candidate,true);
    cap.flags=KUI_NATIVE_FOREGROUND_CAP_VC2_LATE;smash_collection_selection(&candidate,false);
    cap.flags=0u;cap.slots=64u;smash_collection_selection(&candidate,false);
    cap.flags=3u;cap.slots=48u;cap.version=2u;smash_collection_selection(&candidate,false);
    struct {
        struct kui_native_foreground_capability cap;
        struct kui_smash_reader_control tag;
        struct kui_smash_reader_control duplicate;
    } tagged={{KUI_NATIVE_FOREGROUND_CAP_MAGIC0,KUI_NATIVE_FOREGROUND_CAP_MAGIC1,
        1u,32u,0u,64u,0u,0u},{KUI_SMASH_READER_CONTROL_WORDS},{0}};
    candidate.data=&tagged;candidate.info.payload_bytes=sizeof(tagged);
    smash_collection_selection(&candidate,true);
    assert(smash_collection_capability(&candidate)==3);
    tagged.tag.magic0^=1u;assert(smash_collection_capability(&candidate)==-1);tagged.tag.magic0^=1u;
    tagged.tag.magic1^=1u;assert(smash_collection_capability(&candidate)==-1);tagged.tag.magic1^=1u;
    tagged.tag.variant=2u;assert(smash_collection_capability(&candidate)==-1);tagged.tag.variant=3u;
    tagged.tag.reserved=1u;assert(smash_collection_capability(&candidate)==-1);tagged.tag.reserved=0u;
    tagged.duplicate=tagged.tag;assert(smash_collection_capability(&candidate)==-1);
    memset(&tagged.duplicate,0,sizeof(tagged.duplicate));
    tagged.cap.flags=3u;tagged.cap.slots=48u;assert(smash_collection_capability(&candidate)==-1);
    const uint32_t partial0=KUI_SMASH_READER_CONTROL_MAGIC0,partial1=KUI_SMASH_READER_CONTROL_MAGIC1;
    assert(kui_smash_reader_control_find(&partial0,sizeof(partial0),NULL)==-1);
    assert(kui_smash_reader_control_find(&partial1,sizeof(partial1),NULL)==-1);
    const uint8_t *record=(const uint8_t *)(const void *)&tagged.tag;
    for(unsigned length=4u;length<sizeof(tagged.tag);length++)
        assert(kui_smash_reader_control_find(record,length,NULL)==-1);
    assert(!strcmp(kui_native_standard_package(true),KUI_GAMES_RETAIL_PACKAGE));
    assert(!strcmp(kui_native_standard_package(false),KUI_GAMES_RETAIL_PHYSICAL_PACKAGE));
    puts("PASS production Smash collection selector: exact menu/IP/capability admission, unrelated/changed games and options refused, generic control and both cold RPG routes preserved");
}
static enum kui_game_result probe_proof(const struct smash_collection_request *request,
    uint32_t *ip,uint8_t sha[32],uint32_t *crc) {
    assert(request->metadata->boot_bytes==KUI_SMASH_NATIVE_MENU_BYTES && !active_handles);++probe_proofs;
    static const uint8_t owner[32]={0x3a,0x31,0x1b,0x18,0x9a,0x48,0x70,0xcc,
        0xbe,0xfc,0x4f,0xcd,0x30,0x6e,0x2d,0x38,0xe5,0xae,0x81,0x0a,0xe5,0xed,0xd2,0x25,
        0xaf,0x6b,0x9a,0xa6,0xd5,0xdf,0x5f,0x10};
    *ip=KUI_SMASH_NATIVE_IP_CRC32^(uint32_t)probe_bad_ip;
    *crc=KUI_SMASH_NATIVE_MENU_CRC32^(uint32_t)probe_bad_crc;
    memcpy(sha,owner,32u);sha[0]^=(uint8_t)probe_bad_sha;return probe_proof_result;
}
static void probe_reset(const struct kui_runtime_image *source) {
    probe_source=source;probe_stat_result=FR_OK;probe_attribute=0u;
    probe_runtime_result=KUI_RUNTIME_OK;probe_proof_result=KUI_GAME_OK;
    probe_stats=probe_loads=probe_frees=probe_proofs=0u;
    probe_bad_ip=probe_bad_sha=probe_bad_crc=false;
}
static bool checked_probe(const struct smash_collection_request *request,
    struct kui_runtime_image *loaded,bool *selected,uint32_t *crc,const char **path,const char **problem) {
    uint32_t prior_ip=request->map->ip_crc32,prior_crc=*crc;const char *prior_path=*path;
    bool ok=smash_collection_probe(request,loaded,selected,crc,path,problem,logger,cancelled,probe_proof);
    if(!*selected)assert(request->map->ip_crc32==prior_ip && *crc==prior_crc && *path==prior_path);
    return ok;
}
static void smash_collection_probe_cases(const struct kui_runtime_image *source,bool persistent) {
    struct kui_game_metadata metadata={.ip_valid=true,.boot_valid=true,.native_gd=true,
        .boot_bytes=KUI_SMASH_NATIVE_MENU_BYTES};
    strcpy(metadata.product,"MK-51146");strcpy(metadata.version,"V1.002");
    strcpy(metadata.region,"U");strcpy(metadata.bootfile,"1ST_READ.BIN");
    struct files files={0};struct kui_game_image image={.format=KUI_GAME_IMAGE_GDI};
    struct kui_retail_manifest map={.storage_transport=KUI_STORAGE_SCI,.session_lba=45000u};
    struct smash_collection_request request={&files,&metadata,&image,&map,true,false,true,0u,0u};
    struct kui_runtime_image loaded={0};bool selected=false;uint32_t crc=0u;
    const char *path=KUI_GAMES_RETAIL_PACKAGE,*problem=NULL;
#define PROBE() checked_probe(&request,&loaded,&selected,&crc,&path,&problem)
    probe_reset(source);assert(PROBE());
    assert(selected==persistent && probe_stats==1u && probe_loads==1u && probe_proofs==(unsigned)persistent);
    assert(request.emulation_enabled); /* no global preference mutation */
    if(persistent) {
        assert(loaded.data && !probe_frees && !strcmp(path,KUI_GAMES_RETAIL_PHYSICAL_PACKAGE));
        assert(crc==KUI_SMASH_NATIVE_MENU_CRC32 && map.ip_crc32==KUI_SMASH_NATIVE_IP_CRC32);
        kui_runtime_free(&loaded);
    } else assert(!loaded.data && probe_frees==1u && !strcmp(path,KUI_GAMES_RETAIL_PACKAGE));
    for(unsigned skip=0u;skip<5u;skip++) {
        probe_reset(source);path=KUI_GAMES_RETAIL_PACKAGE;
        request.emulation_enabled=skip!=0u;request.child=skip==1u?KUI_SMASH_NATIVE_CHILD_PHANTASY_STAR_II:
            skip==2u?KUI_SMASH_NATIVE_CHILD_SHINING_FORCE:0u;
        request.native_standard=skip!=3u;metadata.product[0]=skip==4u?'X':'M';
        assert(PROBE() && !selected && !loaded.data && !probe_stats && !probe_loads && !probe_proofs);
    }
    metadata.product[0]='M';request.native_standard=request.emulation_enabled=true;request.child=0u;
    probe_reset(source);probe_stat_result=FR_NO_FILE;assert(PROBE() && !selected && !loaded.data && !probe_loads);
    probe_reset(source);probe_stat_result=FR_DISK_ERR;assert(!PROBE() && !loaded.data && !probe_loads);
    probe_reset(source);probe_attribute=AM_DIR;assert(!PROBE() && !loaded.data && !probe_loads);
    const enum kui_runtime_result errors[]={KUI_RUNTIME_IO,KUI_RUNTIME_CHECKSUM,KUI_RUNTIME_CANCELLED};
    for(unsigned i=0u;i<sizeof(errors)/sizeof(errors[0]);i++) {
        probe_reset(source);probe_runtime_result=errors[i];assert(!PROBE() && !loaded.data && probe_loads==1u && !probe_proofs);
    }
    if(persistent) {
        for(unsigned fault=0u;fault<4u;fault++) {
            probe_reset(source);probe_bad_ip=fault==0u;probe_bad_sha=fault==1u;probe_bad_crc=fault==2u;
            if(fault==3u)probe_proof_result=KUI_GAME_CANCELLED;
            bool ok=PROBE();assert(ok==(fault!=3u) && !selected && probe_proofs==1u);
            if(fault==3u) {assert(loaded.data);kui_runtime_free(&loaded);}else assert(!loaded.data && probe_frees==1u);
        }
    }
    uint8_t *broken=malloc(source->info.payload_bytes);assert(broken);
    memcpy(broken,source->data,source->info.payload_bytes);
    struct kui_runtime_image invalid=*source;invalid.data=broken;
    broken[KUI_RETAIL_HEADER_OFFSET]^=1u;
    probe_reset(&invalid);assert(!PROBE() && !selected && loaded.data);kui_runtime_free(&loaded);
    memcpy(broken,source->data,source->info.payload_bytes);
    size_t cap_at=0u;assert(kui_native_foreground_descriptor_find(broken,source->info.payload_bytes,&cap_at)==1);
    assert(source->info.payload_bytes-32u!=cap_at);
    memcpy(broken+source->info.payload_bytes-32u,broken+cap_at,32u);
    probe_reset(&invalid);assert(!PROBE() && !selected && loaded.data);kui_runtime_free(&loaded);free(broken);
    fixture();assert(f_open(&files.reader,KUI_VC2_LATE_FILE_PATH,FA_READ)==FR_OK);files.reader_open=true;
    probe_reset(source);assert(PROBE() && selected==persistent && !files.reader_open && !active_handles);
    assert(operations[FIX_HELPER][FIX_CLOSE]==1u);if(loaded.data)kui_runtime_free(&loaded);
    fixture();assert(f_open(&files.reader,KUI_VC2_LATE_FILE_PATH,FA_READ)==FR_OK);files.reader_open=true;
    failed_file=FIX_HELPER;failed_operation=FIX_CLOSE;failed_ordinal=1u;
    probe_reset(source);assert(!PROBE() && !selected && !loaded.data && !probe_loads && !files.reader_open && !active_handles);
#undef PROBE
    puts("PASS actual production collection probe: real module retained, generic/control/missing fallback, exact-proof and cold-RPG separation, corrupt/I/O/cancel/close failures");
}
static void smash_invalid_control_probe(const struct kui_runtime_image *candidate) {
    probe_reset(candidate);
    /* The actual probe must stop rather than load the generic VMU
     * fallback when an explicit comparison identity is damaged. */
    struct kui_game_metadata m={.ip_valid=true,.boot_valid=true,.native_gd=true,
        .boot_bytes=KUI_SMASH_NATIVE_MENU_BYTES};
    strcpy(m.product,"MK-51146");strcpy(m.version,"V1.002");strcpy(m.region,"U");strcpy(m.bootfile,"1ST_READ.BIN");
    struct files f={0};struct kui_game_image image={.format=KUI_GAME_IMAGE_GDI};
    struct kui_retail_manifest map={.storage_transport=KUI_STORAGE_SCI};
    struct smash_collection_request q={&f,&m,&image,&map,true,false,true,0u,0u};
    struct kui_runtime_image loaded={0};bool selected=false;uint32_t crc=0u;
    const char *launch=KUI_GAMES_RETAIL_PACKAGE,*problem=NULL;
    assert(!checked_probe(&q,&loaded,&selected,&crc,&launch,&problem));
    assert(!selected && loaded.data && !probe_proofs && strstr(problem,"comparison03"));
    kui_runtime_free(&loaded);
}
static void smash_collection_actual(const char *path,bool expected) {
    FILE *input=fopen(path,"rb");assert(input && !fseek(input,0,SEEK_END));
    long length=ftell(input);assert(length>64 && !fseek(input,64,SEEK_SET));
    size_t bytes=(size_t)length-64u;uint8_t *data=malloc(bytes);assert(data);
    assert(fread(data,1u,bytes,input)==bytes && !fclose(input));
    struct kui_runtime_image candidate={.data=data,.info={.payload_bytes=bytes,.memory_bytes=bytes}};
    smash_collection_selection(&candidate,expected);smash_collection_probe_cases(&candidate,expected);
    size_t tag_at=0u;
    if(kui_smash_reader_control_find(candidate.data,candidate.info.payload_bytes,&tag_at)==1) {
        struct kui_smash_reader_control saved;memcpy(&saved,data+tag_at,sizeof(saved));
        assert(smash_collection_capability(&candidate)==3);
        const unsigned changed[]={0u,4u,8u,12u,16u,20u};
        for(unsigned i=0u;i<sizeof(changed)/sizeof(changed[0]);i++) {
            data[tag_at+changed[i]]^=1u;assert(smash_collection_capability(&candidate)==-1);
            smash_invalid_control_probe(&candidate);memcpy(data+tag_at,&saved,sizeof(saved));
        }
        assert(candidate.info.payload_bytes-sizeof(saved)!=tag_at);
        uint8_t tail[sizeof(saved)];memcpy(tail,data+candidate.info.payload_bytes-sizeof(saved),sizeof(saved));
        memcpy(data+candidate.info.payload_bytes-sizeof(saved),&saved,sizeof(saved));
        assert(smash_collection_capability(&candidate)==-1);smash_invalid_control_probe(&candidate);
        memcpy(data+candidate.info.payload_bytes-sizeof(saved),tail,sizeof(tail));
        size_t cap_at=0u;assert(kui_native_foreground_descriptor_find(data,bytes,&cap_at)==1);
        uint8_t cap[32];memcpy(cap,data+cap_at,sizeof(cap));
        ((uint32_t *)(void *)(data+cap_at))[5]=48u;((uint32_t *)(void *)(data+cap_at))[7]=3u;
        assert(smash_collection_capability(&candidate)==-1);smash_invalid_control_probe(&candidate);
        memcpy(data+cap_at,cap,sizeof(cap));
        puts("PASS tagged comparison03: actual retained module, damaged/duplicate tag refused, unchanged setting and cold-RPG routes");
    }
    free(data);
}
static void prebound_owner(unsigned kind) {
    unified_kind=kind;bool swirl=kind==KUI_SMASH_MENU_CHILD_SWIRL;
    struct kui_smash_ce_profile owner={
        .magic0=KUI_SMASH_CE_PROFILE_MAGIC0,.magic1=KUI_SMASH_CE_PROFILE_MAGIC1,
        .version=1u,.bytes=80u,.kind=kind,.virtual_slot=swirl?1u:2u,.flags=swirl?7u:3u,
        .kernel_fad=swirl?493127u:529805u,.kernel_sectors=swirl?893u:899u,
        .kernel_bytes=swirl?0x1be800u:0x1c1800u,.body_bytes=swirl?0x1be000u:0x1c1000u,
        .body_crc32=swirl?0x14a27ac2u:0x6d9c21a3u,.prefix_crc32=swirl?0x7e29bc00u:0xa253bfefu,
        .ram_end=swirl?0x8cfb8000u:0x8cfc0000u,.helper_bytes=sizeof(helper),
        .helper_crc32=seed()->helper_crc32,.reserve_begin=0x8cfc4000u,.reserve_bytes=49152u};
    owner.crc32=kui_retail_crc32(0u,&owner,sizeof(owner));
    assert(kui_smash_ce_profile_valid(&owner));memcpy(payload+6000u,&owner,sizeof(owner));
}
static bool guarded_publish(void *context,const char *path,const uint8_t *bytes,
    uint32_t count,uint32_t immutable,struct kui_vc2_vmu_late_seed *out,
    const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    assert(context==&publish_calls && problem && log && cancel && !active_handles);
    const char *expected=NULL;assert(kui_smash_ce_profile_paths(unified_kind,NULL,NULL,&expected));
    assert(!strcmp(path,expected) && count==sizeof(capsule) && immutable==sizeof(helper));
    assert(!memcmp(bytes,helper,immutable));
    for(uint32_t i=immutable;i<count;++i)assert(!bytes[i]);
    assert(!selects && !operations[FIX_CAPSULE][FIX_OPEN]);seed_unpublished();++publish_calls;
    if(publish_fault==1u)return false;
    out->first_lba=13190u;out->first_blocks=99u;out->second_lba=0u;
    if(publish_fault==2u)out->helper_crc32^=1u;
    if(publish_fault==3u)out->first_lba=0u;
    if(publish_fault==4u) {out->first_blocks=50u;out->second_lba=13200u;}
    if(publish_fault==5u)out->first_lba=UINT32_MAX-20u;
    return true;
}
static bool prebound_prepare(unsigned expected,const uint8_t *wire,
    const struct kui_vmu_game_card_choice *choice,vc2_persist_capsule_publish_fn publish) {
    memcpy(before_payload,payload,sizeof(payload));const char *problem=NULL;
    bool ok=vc2_ce_vmu_persist_prepare_prebound_launch(&fixture_fs,&fixture_volume,&game_map,&module,
        expected,wire,choice,publish,&publish_calls,&problem,logger,cancelled);
    assert(!active_handles && !selects && !operations[FIX_CAPSULE][FIX_OPEN]);
    if(!ok)assert(!memcmp(payload,before_payload,sizeof(payload)));
    return ok;
}
static void prebound_cases(void) {
    struct kui_vmu_game_card_choice choice={.pair=1u};
    assert(kui_vmu_game_card_key(KUI_VC2_PERSIST_PRODUCT,choice.key));
    for(unsigned kind=KUI_SMASH_MENU_CHILD_VC2;kind<=KUI_SMASH_MENU_CHILD_SWIRL;++kind) {
        fixture();prebound_owner(kind);resolver_mapped=true;
        assert(prebound_prepare(kind,selected_wire,&choice,guarded_publish));
        assert(publish_calls==1u && identity_calls==1u && seed()->first_lba==13190u &&
            seed()->first_blocks==99u && !memcmp(payload+profile_at,selected_wire,sizeof(selected_wire)));
        assert(resolver_calls==1u && resolved_helper_opens==1u);
        assert(operations[FIX_HELPER][FIX_OPEN]==1u && operations[FIX_BINDING][FIX_OPEN]==1u);
        fixture();prebound_owner(kind);
        assert(!prebound_prepare(3u-kind,selected_wire,&choice,guarded_publish) && !publish_calls && !identity_calls);
        fixture();prebound_owner(kind);seed()->helper_crc32^=1u;
        assert(!prebound_prepare(kind,selected_wire,&choice,guarded_publish) && !publish_calls);
        fixture();prebound_owner(kind);
        assert(!prebound_prepare(kind,selected_wire,&choice,NULL) && !publish_calls && !identity_calls);
        fixture();prebound_owner(kind);bad_identity=true;
        assert(!prebound_prepare(kind,selected_wire,&choice,guarded_publish) && !publish_calls && identity_calls==1u);
        fixture();prebound_owner(kind);binding[16]^=1u;
        assert(!prebound_prepare(kind,selected_wire,&choice,guarded_publish) && !publish_calls);
        for(unsigned fault=1u;fault<=5u;++fault) {
            fixture();prebound_owner(kind);publish_fault=fault;
            assert(!prebound_prepare(kind,selected_wire,&choice,guarded_publish) && publish_calls==1u);
        }
        fixture();prebound_owner(kind);
        assert(!prebound_prepare(kind,template_wire,&choice,guarded_publish) && !publish_calls);
        fixture();prebound_owner(kind);struct kui_vmu_game_card_choice wrong=choice;wrong.key[0]^=1u;
        assert(!prebound_prepare(kind,selected_wire,&wrong,guarded_publish) && !publish_calls);
    }
    puts("PASS unified CE preparation: distinct helper/session owners, one prebound card, fresh identity/binding, guarded callback only and unpublished refusals");
}
static void embedded_owner(unsigned kind) {
    memset(helper+2048u,0,sizeof(helper)-2048u);
    seed()->helper_crc32=kui_retail_crc32(0,helper,sizeof(helper));prebound_owner(kind);
    struct kui_smash_ce_profile *owner=(void *)(payload+6000u);
    owner->version=KUI_SMASH_CE_PROFILE_EMBEDDED_VERSION;owner->crc32=0;
    owner->crc32=kui_retail_crc32(0,owner,sizeof(*owner));
    const uint32_t words[]={KUI_SMASH_CE_HELPER_MAGIC0,KUI_SMASH_CE_HELPER_MAGIC1,1,32,kind,2048,49152,0};
    memcpy(payload+8192u,words,sizeof(words));memcpy(payload+8224u,helper,2048u);
}
static void embedded_cases(void) {
    struct kui_vmu_game_card_choice choice={.pair=1u};
    assert(kui_vmu_game_card_key(KUI_VC2_PERSIST_PRODUCT,choice.key));
    for(unsigned kind=1u;kind<=2u;kind++) {
        fixture();embedded_owner(kind);resolver_failed=true;
        assert(prebound_prepare(kind,selected_wire,&choice,guarded_publish));
        assert(publish_calls==1u && !resolver_calls && !operations[FIX_HELPER][FIX_OPEN] && !source_bytes_read);
        const unsigned changed[]={0u,4u,8u,12u,16u,20u,24u,28u,32u};
        for(unsigned i=0u;i<sizeof(changed)/sizeof(changed[0]);i++) {
            fixture();embedded_owner(kind);payload[8192u+changed[i]]^=1u;
            assert(!prebound_prepare(kind,selected_wire,&choice,guarded_publish));
            assert(!publish_calls && !identity_calls && !resolver_calls && !operations[FIX_HELPER][FIX_OPEN]);
        }
        fixture();embedded_owner(kind);memset(payload+8192u,0,32u);
        assert(!prebound_prepare(kind,selected_wire,&choice,guarded_publish) && !resolver_calls);
    }
    puts("PASS embedded CE preparation: no external helper lookup, exact padded CRC, version2 mandatory descriptor and unpublished corruptions");
}
int main(int argc,char **argv) {
    smash_native_launch_map();
    smash_collection_fixtures();
    assert(argc==2 || argc==3 || argc==5 || argc==6);FILE *profile=fopen(argv[1],"rb");assert(profile);
    assert(fread(template_wire,1u,sizeof(template_wire),profile)==sizeof(template_wire));
    assert(fgetc(profile)==EOF && !fclose(profile));
    struct kui_vmu_persist_profile decoded;
    assert(!kui_vmu_persist_profile_decode(&decoded,template_wire,sizeof(template_wire)));
    memset(decoded.cid,0x27,sizeof(decoded.cid));
    assert(!kui_vmu_persist_profile_encode(&decoded,selected_wire));
    prebound_cases();
    embedded_cases();
    return_transport_reuse();

    fixture();accepted(13190u,99u,0u);
    fixture();resolver_mapped=true;accepted(13190u,99u,0u);
    assert(resolver_calls==1u && resolved_helper_opens==1u);
    fixture();resolver_failed=true;refused();
    assert(resolver_calls==1u && !operations[FIX_HELPER][FIX_OPEN] && !selects && !operations[FIX_CAPSULE][FIX_WRITE]);
    fixture();capsule_missing=false;memcpy(capsule,helper,sizeof(helper));
    memset(capsule+sizeof(helper),0x6e,sizeof(capsule)-sizeof(helper));
    accepted(13190u,99u,0u);
    assert(!operations[FIX_CAPSULE][FIX_WRITE] && operations[FIX_CAPSULE][FIX_READ]==12u);
    fixture();capsule_missing=false;accepted(13190u,99u,0u); /* stale code rebuilds */
    for(uint32_t boundary=96u;boundary<=98u;boundary++) {
        fixture();split_at(boundary);accepted(13190u,boundary,16190u);
        for(uint32_t block=96u;block<99u;block++)
            assert(kui_vc2_ce_vmu_capsule_lba(seed(),block)==
                (block<boundary?13190u+block:16190u+block-boundary));
    }
    fixture();split_at(48u);fixture_table[2]=8000u;fixture_table[4]=5000u;
    accepted(16190u,48u,13190u);
    fixture();split_at(48u);fixture_table[4]=5048u;accepted(13190u,99u,0u);
    fixture();fixture_fs.csize=64u;fixture_fs.n_fatent=6000u;
    table_words=6u;fixture_table[0]=6u;fixture_table[1]=1u;fixture_table[2]=100u;
    fixture_table[3]=1u;fixture_table[4]=300u;accepted(14464u,64u,27264u);

    fixture();table_words=8u;fixture_table[0]=8u;
    fixture_table[1]=33u;fixture_table[2]=5000u;fixture_table[3]=33u;fixture_table[4]=6000u;
    fixture_table[5]=33u;fixture_table[6]=7000u;refused();
    fixture();split_at(64u);fixture_table[4]=5010u;refused();
    fixture();fixture_table[1]=98u;refused();
    fixture();fixture_table[2]=1u;refused();
    fixture();fixture_table[2]=fixture_fs.n_fatent;refused();
    fixture();fixture_table[1]=fixture_fs.n_fatent;refused();
    fixture();fixture_table[3]=1u;refused();
    fixture();fixture_fs.database=fixture_volume.count;refused();
    fixture();fixture_fs.csize=3u;refused();
    fixture();fixture_volume.count=1000u;refused();
    fixture();fixture_volume.start=UINT32_MAX-100u;refused();
    fixture();wrong_fs_file=FIX_HELPER;refused();assert(!operations[FIX_CAPSULE][FIX_WRITE]);
    fixture();wrong_fs_file=FIX_CAPSULE;refused();
    fixture();helper_bytes--;refused();assert(!operations[FIX_CAPSULE][FIX_WRITE]);
    fixture();failure(FIX_HELPER,FIX_OPEN,1u);
    fixture();failure(FIX_HELPER,FIX_READ,2u);assert(!operations[FIX_CAPSULE][FIX_WRITE]);
    fixture();failure(FIX_HELPER,FIX_CLOSE,1u);assert(!operations[FIX_CAPSULE][FIX_WRITE]);
    fixture();failure(FIX_CAPSULE,FIX_OPEN,1u);
    fixture();failure(FIX_CAPSULE,FIX_OPEN,2u);
    fixture();failure(FIX_CAPSULE,FIX_OPEN,3u);
    fixture();failure(FIX_CAPSULE,FIX_WRITE,2u);
    fixture();failure(FIX_CAPSULE,FIX_SYNC,1u);
    fixture();failure(FIX_CAPSULE,FIX_SEEK,1u);
    fixture();failure(FIX_CAPSULE,FIX_CLOSE,1u);
    fixture();failure(FIX_CAPSULE,FIX_CLOSE,2u);
    fixture();failure(FIX_CAPSULE,FIX_READ,13u);
    fixture();corrupt_readback=true;refused();
    fixture();short_file=FIX_HELPER;short_operation=FIX_READ;short_ordinal=3u;refused();
    fixture();short_file=FIX_CAPSULE;short_operation=FIX_WRITE;short_ordinal=3u;refused();
    fixture();short_file=FIX_CAPSULE;short_operation=FIX_READ;short_ordinal=13u;refused();
    fixture();capsule_missing=false;memcpy(capsule,helper,sizeof(helper));
    failure(FIX_CAPSULE,FIX_READ,12u);assert(!operations[FIX_CAPSULE][FIX_WRITE]);
    fixture();capsule_missing=false;memcpy(capsule,helper,sizeof(helper));
    failure(FIX_CAPSULE,FIX_CLOSE,1u);assert(!operations[FIX_CAPSULE][FIX_WRITE]);
    fixture();capsule_missing=false;memcpy(capsule,helper,sizeof(helper));
    fixture_table[1]=98u;refused();assert(!operations[FIX_CAPSULE][FIX_WRITE]);
    fixture();cancel_now=true;refused();assert(!operations[FIX_HELPER][FIX_OPEN]);
    fixture();cancel_after_reads=2u;refused();assert(!operations[FIX_CAPSULE][FIX_WRITE]);
    fixture();seed()->helper_crc32^=1u;refused();assert(!operations[FIX_CAPSULE][FIX_WRITE]);
    fixture();helper[sizeof(helper)-1u]^=1u;refused();assert(!operations[FIX_CAPSULE][FIX_WRITE]);
    fixture();mutate_after_verify=true;accepted(13190u,99u,0u);
    fixture();seed()->helper_bytes--;refused();assert(!operations[FIX_HELPER][FIX_OPEN]);
    fixture();seed()->version=KUI_VC2_LATE_SEED_VERSION;refused();
    fixture();seed()->first_lba=1u;refused();
    fixture();seed()->reserved=1u;refused();
    fixture();seed()->seed_crc32=1u;refused();
    fixture();memcpy(payload+6000u,seed(),sizeof(*seed()));refused();
    fixture();memmove(payload+seed_at+1u,seed(),sizeof(*seed()));payload[seed_at]=0u;refused();
    fixture();payload[profile_at]^=1u;refused();
    fixture();memcpy(payload+7000u,template_wire,sizeof(template_wire));refused();
    fixture();service_result=KUI_VMU_GAME_CARD_FAILED;refused();
    fixture();service_result=KUI_VMU_GAME_CARD_UNBOUND;refused();
    fixture();service_result=KUI_VMU_GAME_CARD_CANCELLED;refused();
    fixture();service_result=KUI_VMU_GAME_CARD_CHOICE_REQUIRED;refused();
    fixture();bad_service=true;refused();
    fixture();memcpy(selected_wire,template_wire,sizeof(selected_wire));refused();
    assert(!kui_vmu_persist_profile_encode(&decoded,selected_wire));
    fixture();binding[16]^=1u;refused();
    fixture();wrong_fs_file=FIX_BINDING;refused();
    fixture();failure(FIX_BINDING,FIX_OPEN,1u);
    fixture();failure(FIX_BINDING,FIX_READ,1u);
    fixture();failure(FIX_BINDING,FIX_CLOSE,1u);
    /* The production native-child route binds only the existing VC2 pair.
     * Both children call this same route; no ROM/product-specific key exists. */
    fixture();history_calls=creation_attempts=0u;const char *native_problem=NULL;
    assert(smash_native_card_prepare(&game_map,&module,&fixture_fs,&native_problem,logger,cancelled));
    assert(selects==1u && history_calls==1u && !creation_attempts &&
        !memcmp(payload+profile_at,selected_wire,sizeof(selected_wire)));
    assert(!operations[FIX_HELPER][FIX_OPEN] && !operations[FIX_CAPSULE][FIX_OPEN]);
    fixture();service_result=KUI_VMU_GAME_CARD_UNBOUND;memcpy(before_payload,payload,sizeof(payload));
    assert(!smash_native_card_prepare(&game_map,&module,&fixture_fs,&native_problem,logger,cancelled));
    assert(creation_attempts==1u && !selects && !memcmp(payload,before_payload,sizeof(payload)));
    fixture();bad_service=true;memcpy(before_payload,payload,sizeof(payload));
    assert(!smash_native_card_prepare(&game_map,&module,&fixture_fs,&native_problem,logger,cancelled));
    assert(!memcmp(payload,before_payload,sizeof(payload)));
    fixture();binding[16]^=1u;memcpy(before_payload,payload,sizeof(payload));
    assert(!smash_native_card_prepare(&game_map,&module,&fixture_fs,&native_problem,logger,cancelled));
    assert(!memcmp(payload,before_payload,sizeof(payload)));
    if(argc>=3) {
        FILE *actual=fopen(argv[2],"rb");assert(actual && !fseek(actual,0,SEEK_END));
        long file_bytes=ftell(actual);assert(file_bytes>(long)KUI_RUNTIME_HEADER_BYTES && !fseek(actual,64,SEEK_SET));
        size_t bytes=(size_t)file_bytes-64u;uint8_t *data=malloc(bytes);assert(data);
        assert(fread(data,1u,bytes,actual)==bytes && !fclose(actual));
        struct kui_runtime_image candidate={.data=data,.info={.payload_bytes=bytes}};
        size_t at=smash_native_seed_slot(&candidate);assert(at!=SIZE_MAX);
        const char *seed_problem=NULL;
        assert(smash_native_seed_fill(&candidate,KUI_SMASH_NATIVE_CHILD_SHINING_FORCE,&seed_problem));
        struct kui_smash_native_child_seed ready;memcpy(&ready,data+at,sizeof(ready));
        assert(kui_smash_native_child_seed_valid(&ready) && ready.child==KUI_SMASH_NATIVE_CHILD_SHINING_FORCE);
        assert(smash_native_seed_slot(&candidate)==SIZE_MAX); /* no stale repatching */
        free(data);
    }
    if(argc>=5) {
        smash_collection_actual(argv[3],true);smash_collection_actual(argv[4],argc==6);
        if(argc==6)smash_collection_actual(argv[5],false);
        puts("PASS actual linked VC2 persistent/control module selection through the production collection selector");
    }
    puts("PASS production persistent VC2 capsule preparation: source CRC before writes, exact helper plus zero config, two-run capsule including every config boundary, sealed seed after sync and close, dedicated-card binding, cancellation and I/O refusals");
    return 0;
}
