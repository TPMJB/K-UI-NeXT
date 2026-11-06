/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_menu_collection.h"
#include "kui/smash_ce_child.h"
#include "kui/smash_ce_profile.h"

#include "smash_menu_collection_files.inc"
#include "smash_card_session_prepare.inc"
#include "smash_genesis_save_report.inc"
#include "smash_phase_text_prepare.inc"

static bool smash_collection_card_prepare(const struct kui_retail_manifest *map,FATFS *fs,
    const struct kui_volume *volume,uint8_t wire[KUI_VMU_PERSIST_PROFILE_WIRE_BYTES],
    struct kui_vmu_game_card_choice *choice,struct kui_vmu_persist_profile *profile,
    const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    static char refusal[128];bool ok=false;
    struct kui_vmu_game_card_work *work=malloc(sizeof(*work));
    struct kui_vmu_fs_range *forbidden=map->extent_count?malloc(map->extent_count*sizeof(*forbidden)):NULL;
    if(!work || (map->extent_count && !forbidden)) {
        *problem="not enough memory for the shared Smash Pack card";goto done;
    }
    for(uint32_t i=0u;i<map->extent_count;i++) {
        const struct kui_retail_extent *e=&map->slots[map->track_count+i].extent;
        forbidden[i]=(struct kui_vmu_fs_range){e->card_lba,e->blocks};
    }
    struct games_card_platform context={.fs=fs,.volume=*volume};
    const struct kui_vmu_game_card_platform platform={&context,games_card_identity,games_card_raw_begin,
        games_card_raw_end,games_card_salt};
    int result=kui_vmu_game_card_select_boundary(KUI_VC2_PERSIST_PRODUCT,NULL,NULL,
        map->partition_start,map->partition_end,forbidden,map->extent_count,&platform,work,wire,choice,
        games_card_history,&context,log,cancel);
    ok=result==KUI_VMU_GAME_CARD_READY &&
        !kui_vmu_persist_profile_decode(profile,wire,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES) &&
        !kui_vmu_game_card_template(profile->cid) && vc2_persist_binding(fs,choice,profile);
    if(!ok) {
        snprintf(refusal,sizeof(refusal),"shared Smash Pack card refused: %.90s",
            context.history_problem[0]?context.history_problem:choice->problem);*problem=refusal;
    }
done:
    free(forbidden);free(work);return ok;
}
static bool smash_collection_ce_module_read(const char *build,unsigned kind,
    struct kui_runtime_image *module,const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    const char *path=NULL;
    struct kui_smash_ce_profile owner;
    *problem="matching Smash Pack CE child module is missing or changed";
    bool loaded=kui_smash_ce_profile_paths(kind,&path,NULL,NULL) &&
        kui_runtime_read(path,module,log,cancel)==KUI_RUNTIME_OK && layout(module,false) &&
        !memcmp(module->info.build,build,12u) &&
        kui_smash_ce_profile_find(module->data,module->info.payload_bytes,kind,&owner)==1 &&
        kui_smash_ce_child_find(module->data,module->info.payload_bytes,kind)!=SIZE_MAX &&
        kui_smash_menu_return_seed_find(module->data,module->info.payload_bytes)!=SIZE_MAX;
    if(!loaded)return false;
    struct kui_smash_ce_helper helper;
    int embedded=kui_smash_ce_helper_find(module->data,module->info.payload_bytes,kind,&helper);
    return embedded>=0 && (owner.version!=KUI_SMASH_CE_PROFILE_EMBEDDED_VERSION || embedded==1);
}
static bool smash_collection_ce_map(const struct kui_game_metadata_ops *ops,
    const struct kui_game_metadata *metadata,const struct kui_retail_manifest *map,unsigned kind,
    struct kui_retail_manifest *child,const char **problem) {
    struct kui_game_metadata directory=*metadata;uint32_t lba=0u,bytes=0u;
    *problem="original Smash Pack CE child directory or executable is missing";
    if(kui_game_metadata_find_directory(ops,&directory,kui_smash_ce_child_folder(kind),&lba,&bytes)!=KUI_GAME_METADATA_OK)
        return false;
    directory.root_lba=lba;directory.root_bytes=bytes;
    if(kui_game_metadata_find(ops,&directory,"SAMPLE.BIN",KUI_SMASH_CE_SAMPLE_BYTES,&lba,&bytes)!=KUI_GAME_METADATA_OK ||
       bytes!=KUI_SMASH_CE_SAMPLE_BYTES)return false;
    *child=*map;child->boot_lba=lba;child->boot_bytes=bytes;child->boot_crc32=KUI_SMASH_CE_SAMPLE_CRC32;
    child->flags|=KUI_RETAIL_IMAGE_BOOT_CRC;
    memset(child->bootfile,0,sizeof(child->bootfile));memcpy(child->bootfile,"SAMPLE.BIN",sizeof("SAMPLE.BIN"));
    return true;
}
static bool smash_menu_collection_prepare(struct files *files,const struct kui_game_image *image,
    const struct kui_game_metadata_ops *ops,const struct kui_game_metadata *metadata,
    const struct kui_retail_manifest *map,FATFS *fs,const struct kui_volume *volume,
    const char *build,struct kui_runtime_image *outer,
    const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    struct kui_runtime_image children[KUI_SMASH_MENU_CHILD_COUNT]={{0}};bool ok=false;
    struct kui_retail_manifest *child_map=malloc(sizeof(*child_map));
    struct kui_vmu_persist_profile *profile=malloc(sizeof(*profile));
    uint8_t *wire=malloc(KUI_VMU_PERSIST_PROFILE_WIRE_BYTES),*page=malloc(4096u),*payload=NULL;
    struct kui_vmu_game_card_choice choice={0};
    size_t seed_at[KUI_SMASH_MENU_CHILD_COUNT],collection_at=kui_smash_menu_collection_find(outer->data,outer->info.payload_bytes);
    size_t return_at=kui_smash_menu_return_seed_find(outer->data,outer->info.payload_bytes);
    *problem="Smash Pack collection needs matching blank child and return seeds";
    if(!child_map || !profile || !wire || !page || collection_at==SIZE_MAX || return_at==SIZE_MAX ||
       !smash_menu_capsule_seeds_disjoint(collection_at,KUI_SMASH_MENU_COLLECTION_BYTES,return_at,KUI_SMASH_MENU_RETURN_SEED_BYTES))
        goto done;
    kui_smash_launch_timing_phase(KUI_SMASH_UI_CHILD_MODULE);
    if(!smash_menu_capsule_module_read(build,&children[0],problem,log,cancel))goto done;
    seed_at[0]=kui_smash_native_menu_seed_find(children[0].data,children[0].info.payload_bytes);
    for(unsigned i=1u;i<KUI_SMASH_MENU_CHILD_COUNT;i++) {
        if(!smash_collection_ce_module_read(build,i,&children[i],problem,log,cancel))goto done;
        seed_at[i]=kui_smash_ce_child_find(children[i].data,children[i].info.payload_bytes,i);
    }
    struct smash_card_session_gui card_session;
    if(!smash_card_session_modules(&card_session,outer,children,problem))goto done;
#if KUI_SMASH_LAUNCH_TIMING
    kui_smash_launch_timing_component(KUI_SMASH_UI_COMPONENT_MENU,outer->info.build,
        outer->info.payload_bytes,outer->info.crc32,0u);
    for(unsigned i=0u;i<KUI_SMASH_MENU_CHILD_COUNT;i++) {
        uint32_t helper_crc=0u;
        if(i) {
            struct kui_smash_ce_profile owner;
            if(kui_smash_ce_profile_find(children[i].data,children[i].info.payload_bytes,i,&owner)!=1)goto done;
            helper_crc=owner.helper_crc32;
        }
        kui_smash_launch_timing_component((enum kui_smash_ui_component)(i+1u),children[i].info.build,
            children[i].info.payload_bytes,children[i].info.crc32,helper_crc);
    }
#endif
#if KUI_SMASH_LAUNCH_TIMING
    *problem="Smash Pack normal/timing module profiles differ; copy the complete matching collection";
    uint32_t native_profile=0u;
    if(!kui_smash_phase_pair_profile(outer->data,outer->info.payload_bytes,
        children[0].data,children[0].info.payload_bytes,seed_at[0],&native_profile))goto done;
    kui_smash_launch_timing_metric(KUI_SMASH_UI_NATIVE_PROFILE_FLAGS,native_profile);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_OUTER_MODULE_CRC32,outer->info.crc32);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_CHILD_MODULE_CRC32,children[0].info.crc32);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_OUTER_PAYLOAD_BYTES,outer->info.payload_bytes);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_CHILD_PAYLOAD_BYTES,children[0].info.payload_bytes);
#endif
    uint32_t capacity=(outer->info.payload_bytes+511u)&~511u;
    for(unsigned i=0u;i<KUI_SMASH_MENU_CHILD_COUNT;i++) {
        uint32_t bytes=children[i].info.payload_bytes;
        size_t ret=kui_smash_menu_return_seed_find(children[i].data,bytes);
        if(!bytes || bytes>UINT32_C(0x1f0000) || (bytes&3u) || children[i].info.memory_bytes!=bytes ||
           seed_at[i]==SIZE_MAX || seed_at[i]>UINT32_MAX || ret==SIZE_MAX ||
           ret!=seed_at[i]+(i?KUI_SMASH_CE_CHILD_BYTES:KUI_SMASH_NATIVE_MENU_SEED_BYTES) ||
           !smash_menu_capsule_seeds_disjoint(seed_at[i],i?KUI_SMASH_CE_CHILD_BYTES:KUI_SMASH_NATIVE_MENU_SEED_BYTES,
                ret,KUI_SMASH_MENU_RETURN_SEED_BYTES))goto done;
        if(((bytes+511u)&~511u)>capacity)capacity=(bytes+511u)&~511u;
    }
    if(!outer->info.payload_bytes || outer->info.payload_bytes>UINT32_C(0x1f0000) ||
       (outer->info.payload_bytes&3u) || outer->info.memory_bytes!=outer->info.payload_bytes)goto done;
    payload=calloc(1u,capacity);if(!payload) {*problem="not enough memory for Smash Pack collection capsules";goto done;}
    kui_smash_launch_timing_phase(KUI_SMASH_UI_CARD);
    if(!smash_collection_card_prepare(map,fs,volume,wire,&choice,profile,problem,log,cancel))goto done;
    struct smash_collection_files capsule_files={.fs=fs,.volume=volume,.map=map,.profile=profile,.page=page,
        .card_session=card_session.enabled};
    kui_smash_launch_timing_phase(KUI_SMASH_UI_CAPSULES);
    if(!smash_collection_files_inspect(&capsule_files,problem,cancel))goto done;
    if(!smash_card_session_prepare(files,&capsule_files,outer,children,&card_session,problem,log,cancel))goto done;
    struct kui_smash_menu_collection collection;
    memcpy(&collection,(const uint8_t *)outer->data+collection_at,sizeof(collection));
    for(unsigned i=0u;i<KUI_SMASH_MENU_CHILD_COUNT;i++) {
        struct kui_runtime_image *child=&children[i];
        if(!i) {
            struct kui_game_metadata engine=*metadata;uint32_t lba=0u,bytes=0u;
            *problem="original Smash Pack native engine is missing or changed";
            if(kui_game_metadata_find(ops,&engine,"MDE_US.BIN",KUI_SMASH_NATIVE_ENGINE_BYTES,&lba,&bytes)!=KUI_GAME_METADATA_OK ||
               bytes!=KUI_SMASH_NATIVE_ENGINE_BYTES || !close_reader(files))goto done;
            *child_map=*map;smash_native_boot_target(&engine,child_map,lba,bytes);child_map->flags|=KUI_RETAIL_IMAGE_BOOT_CRC;
            struct native_cache_source_proof proof={0};bool deferred=false;
            enum native_cache_result result=smash_native_mapping_prepare(image,child_map,child,files->root,
                log,cancel,&proof,&deferred);
            if(result!=NATIVE_CACHE_READY || (!deferred && (!proof.complete ||
               proof.crc32!=KUI_SMASH_NATIVE_ENGINE_CRC32 || !kui_smash_native_child_engine_owner(bytes,proof.sha256))) ||
               !close_reader(files))goto done;
            child_map->boot_crc32=KUI_SMASH_NATIVE_ENGINE_CRC32;
            size_t slot=kui_vmu_game_card_profile_slot(child->data,child->info.payload_bytes);
            if(slot==SIZE_MAX)goto done;
            memcpy((uint8_t *)child->data+slot,wire,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES);
        } else {
            if(!smash_collection_ce_map(ops,metadata,map,i,child_map,problem) || !close_reader(files) ||
               !vc2_ce_vmu_persist_prepare_prebound_launch(fs,volume,child_map,child,i,wire,&choice,
                   smash_collection_session_publish,&capsule_files,problem,log,cancel))goto done;
        }
        if(kui_retail_manifest_encode(child_map,(uint8_t *)child->data+KUI_RETAIL_MAP_OFFSET)!=KUI_GAME_OK)goto done;
        uint32_t padded=(child->info.payload_bytes+511u)&~511u;
        memset(payload,0,padded);memcpy(payload,child->data,child->info.payload_bytes);
        struct kui_smash_menu_capsule_prepared prepared={0};
        if(!smash_collection_file_publish(&capsule_files,i,payload,padded,padded,&prepared,problem,log,cancel))goto done;
        prepared.payload_bytes=child->info.payload_bytes;prepared.bytes=padded;
        prepared.crc32=kui_retail_crc32(0u,payload,padded);prepared.seed_offset=(uint32_t)seed_at[i];
        if(!smash_menu_capsule_plan(&prepared,&collection.plans[i]))goto done;
    }
    if(cancel() || !kui_smash_menu_collection_seal(&collection) ||
       kui_retail_manifest_encode(map,(uint8_t *)outer->data+KUI_RETAIL_MAP_OFFSET)!=KUI_GAME_OK)goto done;
    memcpy((uint8_t *)outer->data+collection_at,&collection,sizeof(collection));
    uint32_t padded=(outer->info.payload_bytes+511u)&~511u;
    memset(payload,0,padded);memcpy(payload,outer->data,outer->info.payload_bytes);
    struct kui_smash_menu_capsule_prepared prepared={0};
    if(!smash_collection_file_publish(&capsule_files,SMASH_CAP_OUTER,payload,padded,padded,&prepared,problem,log,cancel))goto done;
    prepared.payload_bytes=outer->info.payload_bytes;prepared.bytes=padded;
    prepared.crc32=kui_retail_crc32(0u,payload,padded);prepared.seed_offset=(uint32_t)return_at;
    struct kui_smash_menu_loader_seed plan;struct kui_smash_menu_return_seed returning;
    if(cancel() || !smash_menu_capsule_plan(&prepared,&plan) || !kui_smash_menu_return_seed_configure(&returning,&plan))goto done;
    memcpy((uint8_t *)outer->data+return_at,&returning,sizeof(returning));
    log("Smash Pack: native, VC2 and Swirl cold entries prepared with one shared card selection");ok=true;
    const char *phase_problem="diagnostic native phase TXT preparation unavailable";
    if(!smash_phase_text_prepare(files,&capsule_files,&children[0],&phase_problem,log,cancel))
        log("Smash Pack: native phase TXT unavailable: %s; prepared launch retained",phase_problem);
    if(!smash_genesis_save_report(files,ops,metadata,build,log,cancel))
        log("Smash Pack: save-capability report unavailable; prepared launch retained");
done:
    free(payload);free(page);free(wire);free(profile);free(child_map);
    for(unsigned i=0u;i<KUI_SMASH_MENU_CHILD_COUNT;i++)kui_runtime_free(&children[i]);
    return ok;
}
