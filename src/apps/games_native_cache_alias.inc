/* SPDX-License-Identifier: GPL-3.0-only */
/* A path index selects advisory configuration, never executable identity.
 * The detached stage retains its fresh full SHA, replay and ownership proof. */
#include "kui/game_launch_settings.h"
static bool native_cache_alias_path(char out[128],const char *selected,
    const struct kui_retail_manifest *map,bool dual,const struct kui_runtime_image *package) {
    char normalized[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];
    if(!map || !package || !package->data || !map->boot_bytes ||
       !kui_game_launch_settings_normalize(normalized,selected))return false;
    uint32_t lease=dual?native_cache_dual_lease(package):0u;
    if(dual && !lease)return false;
    const uint32_t words[]={KUI_VMU_NATIVE_RESOLUTION_DETECTOR_EPOCH,map->boot_lba,map->boot_bytes,
        KUI_RETAIL_EXEC_ADDRESS,map->storage_transport,map->reader,dual?1u:0u,lease,map->gdi_crc32};
    struct kui_sha256 hash;kui_sha256_init(&hash);
    static const char domain[]="K-UI native mapping advisory alias v1";
    kui_sha256_update(&hash,domain,sizeof(domain));
    kui_sha256_update(&hash,normalized,strlen(normalized)+1u);
    for(unsigned i=0u;i<sizeof(words)/sizeof(words[0]);i++) {
        uint8_t word[4];for(unsigned j=0u;j<4u;j++)word[j]=(uint8_t)(words[i]>>(8u*j));
        kui_sha256_update(&hash,word,sizeof(word));
    }
    uint8_t digest[32];char hex[65];kui_sha256_digest(&hash,digest);
    for(unsigned i=0u;i<32u;i++)snprintf(hex+2u*i,3u,"%02x",digest[i]);
    int n=snprintf(out,128,"0:/KUI/VMU/native-cache/v%u/hints/%s.nvr",
        KUI_VMU_NATIVE_RESOLUTION_DETECTOR_EPOCH,hex);
    return n>0 && n<128;
}
static bool native_cache_alias_publish(const char *selected,const struct kui_retail_manifest *map,
    bool dual,const struct kui_runtime_image *package,const char *source_root,kui_cancel_fn cancel) {
    if(native_cache_cancelled(cancel) || native_cache_source_conflict(source_root))return false;
    char path[128];
    if(!native_cache_alias_path(path,selected,map,dual,package))return false;
    size_t slot=kui_vmu_native_cache_seed_find(package->data,package->info.payload_bytes);
    const uint8_t *wire,*digest;
    if(slot==SIZE_MAX || !kui_vmu_native_cache_seed_read((const uint8_t *)package->data+slot,
        KUI_VMU_NATIVE_CACHE_SEED_BYTES,&wire,&digest))return false;
    struct native_cache_hint_workspace *work=malloc(sizeof(*work));
    if(!work)return false;
    bool valid=kui_vmu_native_resolution_cache_inspect_config(wire,
        KUI_VMU_NATIVE_RESOLUTION_CACHE_BYTES,&work->resolution) &&
        native_cache_hint_admitted(map,dual,package,&work->resolution);
    /* Existing aliases and every direct hit are read-only. Publishing a
     * changed alias must precede card authority and invalidate source reuse. */
    if(valid && native_cache_file_read(path,work->wire) && !memcmp(wire,work->wire,sizeof(work->wire))) {
        free(work);return !native_cache_cancelled(cancel);
    }
    if(valid && !native_cache_cancelled(cancel) && native_cache_directories()) {
        char directory[64];FILINFO info;
        int n=snprintf(directory,sizeof(directory),"0:/KUI/VMU/native-cache/v%u/hints",
            KUI_VMU_NATIVE_RESOLUTION_DETECTOR_EPOCH);
        FRESULT made=n>0 && (size_t)n<sizeof(directory)?f_mkdir(directory):FR_INVALID_NAME;
        valid=(made==FR_OK || made==FR_EXIST) && f_stat(directory,&info)==FR_OK && (info.fattrib&AM_DIR) &&
            native_cache_file_publish(path,wire,work->wire,cancel);
    } else valid=false;
    free(work);return valid;
}
