/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_source_owner.h"
#include "kui/smash_native_child.h"
/* Only a new native stage that independently checks the complete named
 * source SHA may defer this exact-owner adapter's duplicate GUI source read.
 * A saved NVR is configuration, never a fresh executable/source proof. */
static enum native_cache_result smash_native_mapping_prepare(const struct kui_game_image *image,
    const struct kui_retail_manifest *map,struct kui_runtime_image *package,const char *source_root,
    kui_log_fn log,kui_cancel_fn cancel,struct native_cache_source_proof *proof,bool *deferred) {
    if(proof)*proof=(struct native_cache_source_proof){0};
    if(deferred)*deferred=false;
    if(native_cache_cancelled(cancel))return NATIVE_CACHE_CANCELLED;
    struct kui_native_source_owner owner;
    if(deferred && native_cache_source_eligible(image,map,true,0u,false,package,false) &&
       !native_cache_source_conflict(source_root) &&
       kui_native_source_owner_find(package->data,package->info.payload_bytes,&owner)==1 &&
       owner.image_bytes==map->boot_bytes && owner.load==KUI_RETAIL_EXEC_ADDRESS &&
       kui_smash_native_child_engine_owner(owner.image_bytes,owner.sha256)) {
        size_t slot=kui_vmu_native_cache_seed_find(package->data,package->info.payload_bytes);
        struct native_cache_workspace *work=slot!=SIZE_MAX?malloc(sizeof(*work)):NULL;
        if(work) {
            char path[120],hex[65];
            for(unsigned i=0u;i<32u;i++)snprintf(hex+2u*i,3u,"%02x",owner.sha256[i]);
            int length=snprintf(path,sizeof(path),"0:/KUI/VMU/native-cache/v%u/%s.nvr",
                KUI_VMU_NATIVE_RESOLUTION_DETECTOR_EPOCH,hex);
            kui_smash_launch_timing_phase(KUI_SMASH_UI_MDE_PROOF);
            bool valid=length>0 && (size_t)length<sizeof(path) &&
                native_cache_file_read(path,work->wire) &&
                kui_vmu_native_resolution_cache_inspect_config(work->wire,sizeof(work->wire),&work->resolution) &&
                work->resolution.driver.image_bytes==owner.image_bytes &&
                work->resolution.driver.load_address==owner.load &&
                !memcmp(work->resolution.driver.image_sha256,owner.sha256,32u) &&
                work->resolution.heap.family!=KUI_VMU_NATIVE_HEAP_ARENA &&
                work->resolution.heap.initializer_slot &&
                !native_cache_cancelled(cancel) &&
                kui_vmu_native_cache_seed_fill((uint8_t *)package->data+slot,
                    KUI_VMU_NATIVE_CACHE_SEED_BYTES,work->wire,sizeof(work->wire));
            free(work);
            if(native_cache_cancelled(cancel))return NATIVE_CACHE_CANCELLED;
            if(valid) {
                *deferred=true;
                kui_smash_launch_timing_metric(KUI_SMASH_UI_MDE_BYTES,map->boot_bytes);
                kui_smash_launch_timing_metric(KUI_SMASH_UI_CACHE_HIT,1u);
                if(log)log("Smash native mapping: saved configuration selected; the new stage freshly checks the complete engine SHA");
                return NATIVE_CACHE_READY;
            }
        }
    }
    /* Old stages, unknown/prefix mappings, cache corruption/miss and low
     * memory all retain the original full fresh source read and discovery. */
    return native_cache_prepare(image,map,true,0u,false,package,source_root,true,
        log,cancel,proof,NULL);
}
