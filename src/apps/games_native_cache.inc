/* SPDX-License-Identifier: GPL-3.0-only */
/* A cache-bearing native module explicitly opts into this launch preflight.
 * Cache files have the same trust policy as ordinary local launch settings:
 * their hashes detect corruption, not a hostile rewrite by the card owner.
 * The stage rehashes the loaded executable and always rebinds live ownership. */
#include "kui/vmu_native_cache_seed.h"
#include "kui/vmu_native_resolution_cache.h"
#include "kui/vmu_native_program.h"
#include "kui/hash.h"

enum native_cache_result {NATIVE_CACHE_SKIPPED,NATIVE_CACHE_READY,NATIVE_CACHE_CANCELLED};
struct native_cache_workspace {
    uint8_t wire[KUI_VMU_NATIVE_RESOLUTION_CACHE_BYTES];
    uint8_t compare[KUI_VMU_NATIVE_RESOLUTION_CACHE_BYTES];
    uint8_t digest[32];
    struct kui_vmu_native_resolution resolution;
};
static bool native_cache_cancelled(kui_cancel_fn cancel) {return cancel && cancel();}
static bool native_cache_source_conflict(const char *root) {
    /* The whole reserved VMU subtree is excluded, including the ancestor of
     * possible FAT short aliases for native-cache. Track names are siblings. */
    const char *reserved="/kui/vmu";
    if(!root) return true;
    if(root[0]=='0' && root[1]==':') root+=2;
    while(*reserved && *root) {
        unsigned char a=(unsigned char)*root++,b=(unsigned char)*reserved++;
        if(a>='A' && a<='Z') a=(unsigned char)(a+'a'-'A');
        if(a!=b) return false;
    }
    return !*reserved && (!*root || *root=='/');
}
static bool native_cache_file_read(const char *path,uint8_t out[1024]) {
    FILINFO info;FIL file;UINT got=0;
    if(f_stat(path,&info)!=FR_OK || (info.fattrib&AM_DIR) || info.fsize!=1024u ||
       f_open(&file,path,FA_READ)!=FR_OK) return false;
    bool read=f_read(&file,out,1024u,&got)==FR_OK && got==1024u;
    bool closed=f_close(&file)==FR_OK;
    return read && closed;
}
static bool native_cache_directories(void) {
    char epoch[64];
    int n=snprintf(epoch,sizeof(epoch),"0:/KUI/VMU/native-cache/v%u",KUI_VMU_NATIVE_RESOLUTION_DETECTOR_EPOCH);
    if(n<0 || (size_t)n>=sizeof(epoch)) return false;
    const char *const paths[]={"0:/KUI","0:/KUI/VMU","0:/KUI/VMU/native-cache",epoch};
    for(unsigned i=0;i<sizeof(paths)/sizeof(paths[0]);i++) {
        FILINFO info;FRESULT result=f_mkdir(paths[i]);
        if((result!=FR_OK && result!=FR_EXIST) || f_stat(paths[i],&info)!=FR_OK || !(info.fattrib&AM_DIR)) return false;
    }
    return true;
}
static bool native_cache_file_publish(const char *path,const uint8_t wire[1024],
    uint8_t compare[1024],kui_cancel_fn cancel) {
    if(native_cache_cancelled(cancel) || !native_cache_directories()) return false;
    char temporary[128];FIL file;FRESULT opened=FR_EXIST;
    for(unsigned attempt=0;attempt<8u && opened==FR_EXIST;attempt++) {
        int n=snprintf(temporary,sizeof(temporary),"%s.tmp%u",path,attempt);
        if(n<0 || (size_t)n>=sizeof(temporary)) return false;
        opened=f_open(&file,temporary,FA_WRITE|FA_CREATE_NEW);
    }
    if(opened!=FR_OK) return false;
    UINT written=0;
    bool valid=!native_cache_cancelled(cancel) &&
        f_write(&file,wire,1024u,&written)==FR_OK && written==1024u;
    if(valid) valid=f_sync(&file)==FR_OK;
    bool closed=f_close(&file)==FR_OK;
    valid=valid && closed && !native_cache_cancelled(cancel) &&
        native_cache_file_read(temporary,compare) && !memcmp(wire,compare,1024u);
    if(valid && !native_cache_cancelled(cancel)) {
        FILINFO old;FRESULT present=f_stat(path,&old);
        /* A bad old record is replaceable configuration, never a directory.
         * Power loss between unlink and rename leaves a cache miss. The new
         * name is published only after a complete synced/readback record. */
        valid=present==FR_NO_FILE || present==FR_NO_PATH ||
            (present==FR_OK && !(old.fattrib&AM_DIR) && f_unlink(path)==FR_OK);
        if(valid && !native_cache_cancelled(cancel)) valid=f_rename(temporary,path)==FR_OK;
        else valid=false;
    } else valid=false;
    if(!valid) (void)f_unlink(temporary);
    return valid;
}
static enum native_cache_result native_cache_prepare(const struct kui_game_image *image,
    const struct kui_retail_manifest *map,bool native,uint32_t options,bool patched,
    struct kui_runtime_image *package,const char *source_root,kui_log_fn log,kui_cancel_fn cancel) {
    const bool dual=map && package && package->data &&
        kui_native_dual_reader_valid(map->reader,map->storage_transport) &&
        options==map->reader &&
        kui_native_dual_descriptor_find(package->data,package->info.payload_bytes,NULL)==1;
    if(!native || (options && !dual) || patched || !image || !map || !package || !package->data ||
       image->format!=KUI_GAME_IMAGE_GDI || image->compressed || image->scrambled || image->cd_image ||
       map->storage_transport!=KUI_STORAGE_SCI || (map->reader!=KUI_RETAIL_READER_STANDARD && !dual) ||
       (map->flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED)) ||
       map->boot_bytes<128u || map->boot_bytes>KUI_RETAIL_IMAGE_BOOT_MAX ||
       map->boot_lba>=KUI_GAME_LBA_LIMIT ||
       (map->boot_bytes+2047u)/2048u>KUI_GAME_LBA_LIMIT-map->boot_lba)
        return NATIVE_CACHE_SKIPPED;
    size_t slot=kui_vmu_native_cache_seed_find(package->data,package->info.payload_bytes);
    if(slot==SIZE_MAX) return NATIVE_CACHE_SKIPPED;
    if(native_cache_cancelled(cancel)) return NATIVE_CACHE_CANCELLED;
    uint8_t *boot=malloc(map->boot_bytes);
    struct native_cache_workspace *work=malloc(sizeof(*work));
    if(!boot || !work) {free(work);free(boot);return NATIVE_CACHE_SKIPPED;}
    enum native_cache_result result=NATIVE_CACHE_SKIPPED;
    struct kui_sha256 sha;uint8_t image_hash[32],sector[2048];kui_sha256_init(&sha);
    for(uint32_t done=0;done<map->boot_bytes;) {
        if(native_cache_cancelled(cancel)) {result=NATIVE_CACHE_CANCELLED;goto finished;}
        enum kui_game_result read=kui_game_image_read(image,map->boot_lba+done/2048u,1u,
            KUI_GAME_SECTOR_MODE1,sector,sizeof(sector));
        if(read!=KUI_GAME_OK) {
            if(read==KUI_GAME_CANCELLED || native_cache_cancelled(cancel)) result=NATIVE_CACHE_CANCELLED;
            goto finished;
        }
        uint32_t take=map->boot_bytes-done;if(take>sizeof(sector)) take=sizeof(sector);
        memcpy(boot+done,sector,take);kui_sha256_update(&sha,sector,take);done+=take;
    }
    kui_sha256_digest(&sha,image_hash);
    /* The cache filename binds the complete source read above. Its record
     * describes only the caller-proven live program, matching stage replay.
     * Do not shorten a source based on a cached record or an arbitrary tail. */
    size_t program_bytes;
    if(!kui_vmu_native_program_extent(boot,map->boot_bytes,KUI_RETAIL_EXEC_ADDRESS,
       &program_bytes)) goto finished;
    char path[120],hex[65];
    for(unsigned i=0;i<32u;i++) snprintf(hex+2u*i,3u,"%02x",image_hash[i]);
    int length=snprintf(path,sizeof(path),"0:/KUI/VMU/native-cache/v%u/%s.nvr",
        KUI_VMU_NATIVE_RESOLUTION_DETECTOR_EPOCH,hex);
    if(length<0 || (size_t)length>=sizeof(path)) goto finished;
    bool disk=!native_cache_source_conflict(source_root);
    /* This copied digest is explicitly trusted local configuration. It is
     * kept separate from the record, never passed as an aliased self-hash. */
    bool hit=disk && native_cache_file_read(path,work->wire) &&
        kui_vmu_native_resolution_cache_record_digest(work->wire,sizeof(work->wire),work->digest) &&
        kui_vmu_native_resolution_cache_replay(boot,program_bytes,KUI_RETAIL_EXEC_ADDRESS,
            work->wire,sizeof(work->wire),work->digest,&work->resolution);
    if(!hit) {
        if(native_cache_cancelled(cancel)) {result=NATIVE_CACHE_CANCELLED;goto finished;}
        if(!kui_vmu_native_resolution_cache_build(boot,program_bytes,KUI_RETAIL_EXEC_ADDRESS,
            work->wire,sizeof(work->wire),&work->resolution)) goto finished;
        if(native_cache_cancelled(cancel)) {result=NATIVE_CACHE_CANCELLED;goto finished;}
        if(disk && !native_cache_file_publish(path,work->wire,work->compare,cancel)) {
            if(native_cache_cancelled(cancel)) {result=NATIVE_CACHE_CANCELLED;goto finished;}
            if(log) log("Native VMU cache persistence unavailable; using the complete in-memory proof");
        }
    }
    if(native_cache_cancelled(cancel)) {result=NATIVE_CACHE_CANCELLED;goto finished;}
    if(kui_vmu_native_cache_seed_fill((uint8_t *)package->data+slot,
        KUI_VMU_NATIVE_CACHE_SEED_BYTES,work->wire,sizeof(work->wire))) {
        result=NATIVE_CACHE_READY;
        if(log) log("Native VMU mapping: %s complete executable proof; live ownership remains checked",
            hit?"cached":"resolved");
    }
finished:
    free(work);free(boot);return result;
}
