/* SPDX-License-Identifier: GPL-3.0-only */
/* Saved mapping hints are ordinary local configuration, never source identity.
 * This helper does no source read or checksum reuse. Its caller must restrict
 * admission to a path whose detached stage can reject a hint and rediscover
 * from its own freshly loaded image. Exact-owner adapters keep fresh proof. */
struct native_cache_hint_workspace {
    DIR directory;
    FILINFO entry;
    uint8_t wire[KUI_VMU_NATIVE_RESOLUTION_CACHE_BYTES];
    uint8_t selected[KUI_VMU_NATIVE_RESOLUTION_CACHE_BYTES];
    struct kui_vmu_native_resolution resolution;
};
static bool native_cache_hint_name(const char *name) {
    if(!name)return false;
    for(unsigned i=0u;i<64u;i++)
        if(!((name[i]>='0' && name[i]<='9') || (name[i]>='a' && name[i]<='f')))return false;
    return !strcmp(name+64u,".nvr");
}
static bool native_cache_hint_admitted(const struct kui_retail_manifest *map,bool dual,
    const struct kui_runtime_image *package,const struct kui_vmu_native_resolution *r) {
    const bool arena=r->heap.family==KUI_VMU_NATIVE_HEAP_ARENA;
    return r->driver.image_bytes==map->boot_bytes &&
        r->driver.load_address==KUI_RETAIL_EXEC_ADDRESS &&
        (dual || (!arena && r->heap.initializer_slot)) &&
        (!arena || native_cache_dual_lease(package)==r->call.entry_bytes);
}
#include "games_native_cache_alias.inc"
static enum native_cache_result native_cache_hint_prepare(const struct kui_retail_manifest *map,
    bool dual,struct kui_runtime_image *package,const char *source_root,const char *selected,
    bool *indexed,kui_log_fn log,kui_cancel_fn cancel) {
    if(indexed)*indexed=false;
    if(native_cache_cancelled(cancel))return NATIVE_CACHE_CANCELLED;
    if(!map || !package || !package->data || native_cache_source_conflict(source_root) ||
       map->storage_transport!=KUI_STORAGE_SCI ||
       (dual?(!kui_native_dual_reader_valid(map->reader,map->storage_transport) ||
            kui_native_dual_descriptor_find(package->data,package->info.payload_bytes,NULL)!=1):
            map->reader!=KUI_RETAIL_READER_STANDARD) ||
       (map->flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED)))return NATIVE_CACHE_SKIPPED;
    size_t slot=kui_vmu_native_cache_seed_find(package->data,package->info.payload_bytes);
    if(slot==SIZE_MAX)return NATIVE_CACHE_SKIPPED;
    struct native_cache_hint_workspace *work=malloc(sizeof(*work));
    if(!work)return NATIVE_CACHE_SKIPPED;
    char alias[128];
    bool direct=native_cache_alias_path(alias,selected,map,dual,package) &&
        native_cache_file_read(alias,work->wire) &&
        kui_vmu_native_resolution_cache_inspect_config(work->wire,sizeof(work->wire),&work->resolution) &&
        native_cache_hint_admitted(map,dual,package,&work->resolution);
    if(native_cache_cancelled(cancel)) {free(work);return NATIVE_CACHE_CANCELLED;}
    if(direct &&
       kui_vmu_native_cache_seed_fill((uint8_t *)package->data+slot,
           KUI_VMU_NATIVE_CACHE_SEED_BYTES,work->wire,sizeof(work->wire))) {
        if(indexed)*indexed=true;
        if(log)log("Native VMU mapping: indexed advisory hint selected; detached image SHA and live ownership remain checked");
        free(work);return NATIVE_CACHE_READY;
    }
    char directory[64],path[136];
    int length=snprintf(directory,sizeof(directory),"0:/KUI/VMU/native-cache/v%u",
        KUI_VMU_NATIVE_RESOLUTION_DETECTOR_EPOCH);
    bool found=false,valid=length>0 && (size_t)length<sizeof(directory),opened=false;
    unsigned entries=0u;
    enum native_cache_result result=NATIVE_CACHE_SKIPPED;
    if(valid) {opened=f_opendir(&work->directory,directory)==FR_OK;valid=opened;}
    while(valid) {
        if(native_cache_cancelled(cancel)) {result=NATIVE_CACHE_CANCELLED;break;}
        if(f_readdir(&work->directory,&work->entry)!=FR_OK) {valid=false;break;}
        if(!work->entry.fname[0])break;
        if(entries++==256u) {valid=false;break;}
        if(!native_cache_hint_name(work->entry.fname))continue;
        length=snprintf(path,sizeof(path),"%s/%s",directory,work->entry.fname);
        if((work->entry.fattrib&AM_DIR) || work->entry.fsize!=sizeof(work->wire) ||
           length<=0 || (size_t)length>=sizeof(path) || !native_cache_file_read(path,work->wire) ||
           !kui_vmu_native_resolution_cache_inspect_config(work->wire,sizeof(work->wire),&work->resolution)) {
            valid=false;break;
        }
        if(!native_cache_hint_admitted(map,dual,package,&work->resolution))continue;
        if(found) {valid=false;break;}
        memcpy(work->selected,work->wire,sizeof(work->selected));found=true;
    }
    if(opened && f_closedir(&work->directory)!=FR_OK)valid=false;
    if(native_cache_cancelled(cancel))result=NATIVE_CACHE_CANCELLED;
    if(result!=NATIVE_CACHE_CANCELLED && valid && found &&
       kui_vmu_native_cache_seed_fill((uint8_t *)package->data+slot,
           KUI_VMU_NATIVE_CACHE_SEED_BYTES,work->selected,sizeof(work->selected))) {
        result=NATIVE_CACHE_READY;
        if(log)log("Native VMU mapping: saved hint selected; detached image SHA and live ownership remain checked");
    }
    free(work);return result;
}
