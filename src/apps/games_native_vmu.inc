/* SPDX-License-Identifier: GPL-3.0-only */
/* Experimental frontend mapping only. No module selection, game RAM patch,
 * allocator call or virtual-card/save enablement is performed here. */
#include "kui/vmu_native_cache.h"
#include "kui/vmu_native_heap.h"
#include "kui/hash.h"

static void native_vmu_mapping(const struct kui_game_image *image,
    const struct kui_retail_manifest *map,const char *source_root,kui_log_fn log) {
    if(map->boot_bytes<2u || map->boot_bytes>KUI_RETAIL_EXEC_MAX_BYTES) return;
    uint8_t *boot=malloc(map->boot_bytes);
    if(!boot) {log("Native VMU mapping: insufficient preboot workspace; continuing ordinary launch");return;}
    uint8_t sector[2048];enum kui_game_result result=KUI_GAME_OK;
    for(uint32_t done=0;done<map->boot_bytes;) {
        result=kui_game_image_read(image,map->boot_lba+done/2048u,1u,
            KUI_GAME_SECTOR_MODE1,sector,sizeof(sector));
        if(result!=KUI_GAME_OK) break;
        uint32_t take=map->boot_bytes-done;if(take>sizeof(sector)) take=sizeof(sector);
        memcpy(boot+done,sector,take);done+=take;
    }
    if(result!=KUI_GAME_OK) {log("Native VMU mapping: executable read failed; continuing ordinary launch");free(boot);return;}
    struct kui_sha256 hash;uint8_t digest[32];kui_sha256_init(&hash);
    kui_sha256_update(&hash,boot,map->boot_bytes);kui_sha256_digest(&hash,digest);
    char key[65],path[112];
    for(unsigned i=0;i<32u;i++) snprintf(key+2u*i,3u,"%02x",digest[i]);
    snprintf(path,sizeof(path),"0:/KUI/VMU/native-cache/%s.nvm",key);
    struct kui_vmu_native_discovery driver;uint8_t wire[KUI_VMU_NATIVE_CACHE_BYTES];
    FIL file;FILINFO info;FRESULT present=f_stat(path,&info);bool cached=false;
    if(present==FR_OK && !(info.fattrib&AM_DIR) && info.fsize==sizeof(wire) &&
       f_open(&file,path,FA_READ)==FR_OK) {
        UINT got=0;bool read=f_read(&file,wire,sizeof(wire),&got)==FR_OK && got==sizeof(wire);
        bool closed=f_close(&file)==FR_OK;
        cached=read && closed && kui_vmu_native_cache_load(&driver,wire,sizeof(wire),
            boot,map->boot_bytes,KUI_RETAIL_EXEC_ADDRESS);
    }
    int found=cached?KUI_VMU_NATIVE_RESOLVED:kui_vmu_native_discover(
        boot,map->boot_bytes,KUI_RETAIL_EXEC_ADDRESS,&driver);
    if(found==KUI_VMU_NATIVE_RESOLVED) {
        struct kui_vmu_native_heap heap;
        int allocator=kui_vmu_native_heap_discover(boot,map->boot_bytes,KUI_RETAIL_EXEC_ADDRESS,&heap);
        log("Native VMU mapping: %s family=%u submit=%08x completion=%08x; installation requirements=%u",
            cached?"verified cache":"resolved",driver.family,driver.submit,driver.completion_call,driver.requirements);
        log("Native VMU allocator mapping: status=%d initializer-slot=%08x; live ownership still required",
            allocator,allocator==KUI_VMU_NATIVE_HEAP_FOUND?heap.initializer_slot:0u);
        /* Never overwrite an existing file or a game track in this folder.
         * A partial cache after interruption is simply ignored next launch. */
        const char *cache_root="/KUI/VMU/native-cache";bool source_conflict=true;
        size_t root_bytes=strlen(source_root),cache_bytes=strlen(cache_root);
        if(root_bytes!=cache_bytes) source_conflict=false;
        else for(size_t i=0;i<cache_bytes;i++) {
            unsigned char a=(unsigned char)source_root[i],b=(unsigned char)cache_root[i];
            if(a>='A' && a<='Z') a=(unsigned char)(a+'a'-'A');
            if(b>='A' && b<='Z') b=(unsigned char)(b+'a'-'A');
            if(a!=b) {source_conflict=false;break;}
        }
        if(!cached && !source_conflict && (present==FR_NO_FILE || present==FR_NO_PATH) &&
           kui_vmu_native_cache_encode(&driver,wire,sizeof(wire))) {
            FRESULT directory=f_mkdir("0:/KUI/VMU");
            if(directory==FR_OK || directory==FR_EXIST) {
                directory=f_mkdir("0:/KUI/VMU/native-cache");
                if((directory==FR_OK || directory==FR_EXIST) &&
                   f_open(&file,path,FA_WRITE|FA_CREATE_NEW)==FR_OK) {
                    UINT written=0;
                    bool saved=f_write(&file,wire,sizeof(wire),&written)==FR_OK && written==sizeof(wire);
                    if(saved) saved=f_sync(&file)==FR_OK;
                    bool closed=f_close(&file)==FR_OK;
                    log(saved && closed?"Native VMU static mapping cached":"Native VMU cache unavailable; mapping retained for this preflight only");
                }
            }
        }
    } else log("Native VMU mapping: unsupported or ambiguous family (%d); continuing ordinary launch",found);
    free(boot);
}
