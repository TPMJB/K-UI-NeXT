/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_menu_capsule.h"
#include "kui/smash_native_menu.h"
#if KUI_SMASH_LAUNCH_TIMING
#include "kui/smash_phase_video.h"
#endif

#include "smash_menu_capsule_file_prepare.inc"
#include "smash_menu_capsule_pair_prepare.inc"

static bool smash_menu_capsule_module_read(const char *expected_build,struct kui_runtime_image *child,
    const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    *problem="matching Smash Pack native menu module is missing or changed";
    if(!expected_build || kui_runtime_read(KUI_SMASH_NATIVE_MENU_PACKAGE,child,log,cancel)!=KUI_RUNTIME_OK ||
       !layout(child,false))return false;
    if(memcmp(child->info.build,expected_build,12u)) {
        *problem="Smash Pack menu loader and native child builds differ; copy the matching pair";return false;
    }
    return true;
}
#include "smash_menu_collection_prepare.inc"
static bool smash_menu_capsule_prepare(struct files *files,const struct kui_game_image *image,
    const struct kui_game_metadata_ops *ops,const struct kui_game_metadata *metadata,
    const struct kui_retail_manifest *map,FATFS *fs,const struct kui_volume *volume,
    const char *expected_build,struct kui_runtime_image *outer,
    const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    if(!files || !image || !ops || !metadata || !map || !fs || !volume || !expected_build ||
       !outer || !outer->data || !problem || !log || !cancel)return false;
    *problem="Smash Pack native menu needs a plain SCI Standard map with at most48 slots";
    if((fs->fs_type!=FS_FAT32 && fs->fs_type!=FS_EXFAT) ||
       map->storage_transport!=KUI_STORAGE_SCI || map->reader!=KUI_RETAIL_READER_STANDARD ||
       map->track_count>48u || map->extent_count>48u-map->track_count ||
       map->partition_start!=volume->start || map->partition_end!=(uint64_t)volume->start+volume->count ||
       image->format!=KUI_GAME_IMAGE_GDI || image->compressed || image->scrambled || image->cd_image ||
       !metadata->native_gd || !kui_smash_native_child_candidate(metadata) ||
       map->ip_crc32!=KUI_SMASH_NATIVE_IP_CRC32 || map->boot_bytes!=KUI_SMASH_NATIVE_MENU_BYTES ||
       map->boot_crc32!=KUI_SMASH_NATIVE_MENU_CRC32)return false;
    if(kui_smash_menu_collection_find(outer->data,outer->info.payload_bytes)!=SIZE_MAX)
        return smash_menu_collection_prepare(files,image,ops,metadata,map,fs,volume,expected_build,outer,
            problem,log,cancel);
    struct kui_runtime_image child={0};
    struct kui_retail_manifest *child_map=malloc(sizeof(*child_map));
    struct kui_vmu_persist_profile *profile=malloc(sizeof(*profile));
    uint8_t *page=malloc(4096u);bool ok=false;
    if(!child_map || !profile || !page) {*problem="not enough memory for the Smash Pack native capsule";goto done;}
    struct kui_game_metadata engine=*metadata;uint32_t engine_lba=0u,engine_bytes=0u;
    kui_smash_launch_timing_phase(KUI_SMASH_UI_CHILD_MODULE);
    *problem="original Smash Pack native engine is missing or has changed size";
    if(kui_game_metadata_find(ops,&engine,"MDE_US.BIN",KUI_SMASH_NATIVE_ENGINE_BYTES,&engine_lba,&engine_bytes)!=KUI_GAME_METADATA_OK ||
       engine_bytes!=KUI_SMASH_NATIVE_ENGINE_BYTES || !close_reader(files))goto done;
    if(!smash_menu_capsule_module_read(expected_build,&child,problem,log,cancel))goto done;
    size_t seed_offset=kui_smash_native_menu_seed_find(child.data,child.info.payload_bytes);
    if(seed_offset==SIZE_MAX || seed_offset>UINT32_MAX ||
       kui_smash_menu_return_seed_find(child.data,child.info.payload_bytes)==SIZE_MAX ||
       kui_vmu_game_card_profile_slot(child.data,child.info.payload_bytes)==SIZE_MAX ||
       kui_vmu_native_cache_seed_find(child.data,child.info.payload_bytes)==SIZE_MAX)goto done;
#if KUI_SMASH_LAUNCH_TIMING
    *problem="Smash Pack normal/timing module profiles differ; copy the complete matching pair";
    uint32_t native_profile=0u;
    if(!kui_smash_phase_pair_profile(outer->data,outer->info.payload_bytes,
       child.data,child.info.payload_bytes,seed_offset,&native_profile))goto done;
    kui_smash_launch_timing_metric(KUI_SMASH_UI_NATIVE_PROFILE_FLAGS,native_profile);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_OUTER_MODULE_CRC32,outer->info.crc32);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_CHILD_MODULE_CRC32,child.info.crc32);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_OUTER_PAYLOAD_BYTES,outer->info.payload_bytes);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_CHILD_PAYLOAD_BYTES,child.info.payload_bytes);
#endif
    *child_map=*map;smash_native_boot_target(&engine,child_map,engine_lba,engine_bytes);
    child_map->flags|=KUI_RETAIL_IMAGE_BOOT_CRC;
    struct native_cache_source_proof proof={0};bool deferred=false;
    enum native_cache_result result=smash_native_mapping_prepare(image,child_map,&child,files->root,
        log,cancel,&proof,&deferred);
    *problem="Smash Pack native engine identity or guarded SDK mapping refused";
    if(result!=NATIVE_CACHE_READY || (!deferred && (!proof.complete ||
       proof.crc32!=KUI_SMASH_NATIVE_ENGINE_CRC32 || !kui_smash_native_child_engine_owner(engine_bytes,proof.sha256))) ||
       !close_reader(files))goto done;
    child_map->boot_crc32=KUI_SMASH_NATIVE_ENGINE_CRC32;
    kui_smash_launch_timing_phase(KUI_SMASH_UI_CARD);
    if(!smash_native_card_prepare(child_map,&child,fs,problem,log,cancel))goto done;
    size_t profile_offset=kui_vmu_game_card_profile_slot(child.data,child.info.payload_bytes);
    if(profile_offset==SIZE_MAX || kui_vmu_persist_profile_decode(profile,(const uint8_t *)child.data+profile_offset,
       KUI_VMU_PERSIST_PROFILE_WIRE_BYTES) ||
       kui_retail_manifest_encode(child_map,(uint8_t *)child.data+KUI_RETAIL_MAP_OFFSET)!=KUI_GAME_OK)goto done;
    if(kui_retail_manifest_encode(map,(uint8_t *)outer->data+KUI_RETAIL_MAP_OFFSET)!=KUI_GAME_OK)goto done;
    kui_smash_launch_timing_phase(KUI_SMASH_UI_CAPSULES);
    ok=smash_menu_capsule_pair_publish(fs,volume,map,profile,&child,outer,page,problem,log,cancel);
done:
    free(page);free(profile);free(child_map);kui_runtime_free(&child);return ok;
}
