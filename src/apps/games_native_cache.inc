/* SPDX-License-Identifier: GPL-3.0-only */
/* A cache-bearing native module explicitly opts into this launch preflight.
 * Cache files have the same trust policy as ordinary local launch settings:
 * their hashes detect corruption, not a hostile rewrite by the card owner.
 * The stage rehashes the loaded executable and always rebinds live ownership. */
#include "kui/vmu_native_cache_seed.h"
#include "kui/vmu_native_resolution_cache.h"
#include "kui/vmu_native_program.h"
#include "kui/native_sdk_sci_family.h"
#include "kui/hash.h"
#include "kui/core.h"
#include "kui/smash_launch_timing.h"

enum native_cache_result {NATIVE_CACHE_SKIPPED,NATIVE_CACHE_READY,NATIVE_CACHE_CANCELLED};
/* This reports only a completed source read, independently of whether an
 * optional SDK mapping can be built. It never grants cache/live ownership. */
struct native_cache_source_proof {bool complete;uint32_t crc32;uint8_t sha256[32];};
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
/* The combined module's stage refuses this executable. Only then may New
 * use the reader-only module, if its SDK family exists. */
static void native_cache_standalone(const uint8_t *boot,const struct kui_retail_manifest *map,
    bool *standalone) {
    struct kui_native_sdk_sci_family family;
    if(standalone && map->reader==KUI_RETAIL_READER_ASYNC &&
       kui_native_sdk_sci_family_find(boot,map->boot_bytes,KUI_RETAIL_EXEC_ADDRESS,&family)==1)
        *standalone=true;
}
/* The lease a combined module's stage allocates, from its own descriptor. */
static uint32_t native_cache_dual_lease(const struct kui_runtime_image *package) {
    size_t at=0u;
    if(kui_native_dual_descriptor_find(package->data,package->info.payload_bytes,&at)!=1) return 0u;
    return kui_native_dual_u32((const uint8_t *)package->data+at+24u);
}
/* A completed source read proves bytes/CRC only. Keep its eligibility shared
 * with checksum reuse; SDK resolution and live ownership stay independent. */
static bool native_cache_source_eligible(const struct kui_game_image *image,
    const struct kui_retail_manifest *map,bool native,uint32_t options,bool patched,
    const struct kui_runtime_image *package,bool dual) {
    return native && (!options || dual) && !patched && image && map && package && package->data &&
        image->format==KUI_GAME_IMAGE_GDI && !image->compressed && !image->scrambled && !image->cd_image &&
        map->storage_transport==KUI_STORAGE_SCI && (map->reader==KUI_RETAIL_READER_STANDARD || dual) &&
        !(map->flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED)) &&
        map->boot_bytes>=128u && map->boot_bytes<=KUI_RETAIL_IMAGE_BOOT_MAX &&
        map->boot_lba<KUI_GAME_LBA_LIMIT &&
        (map->boot_bytes+2047u)/2048u<=KUI_GAME_LBA_LIMIT-map->boot_lba;
}
static enum native_cache_result native_cache_prepare(const struct kui_game_image *image,
    const struct kui_retail_manifest *map,bool native,uint32_t options,bool patched,
    struct kui_runtime_image *package,const char *source_root,bool allow_persistent_cache,kui_log_fn log,kui_cancel_fn cancel,
    struct native_cache_source_proof *source_proof,bool *standalone) {
    if(source_proof)*source_proof=(struct native_cache_source_proof){0};
    if(standalone)*standalone=false;
    const bool dual=map && package && package->data &&
        kui_native_dual_reader_valid(map->reader,map->storage_transport) &&
        options==map->reader &&
        kui_native_dual_descriptor_find(package->data,package->info.payload_bytes,NULL)==1;
    if(!native_cache_source_eligible(image,map,native,options,patched,package,dual))
        return NATIVE_CACHE_SKIPPED;
    size_t slot=kui_vmu_native_cache_seed_find(package->data,package->info.payload_bytes);
    if(slot==SIZE_MAX) return NATIVE_CACHE_SKIPPED;
    kui_smash_launch_timing_phase(KUI_SMASH_UI_MDE_READ_HASH);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_MDE_BYTES,map->boot_bytes);
    if(native_cache_cancelled(cancel)) return NATIVE_CACHE_CANCELLED;
    const uint32_t boot_sectors=(map->boot_bytes+2047u)/2048u;
    const size_t boot_capacity=(size_t)boot_sectors*2048u;
    uint8_t *boot=malloc(boot_capacity);
    struct native_cache_workspace *work=malloc(sizeof(*work));
    if(!boot || !work) {free(work);free(boot);return NATIVE_CACHE_SKIPPED;}
    enum native_cache_result result=NATIVE_CACHE_SKIPPED;
    struct kui_sha256 sha;uint8_t image_hash[32];kui_sha256_init(&sha);
    uint32_t source_crc=0u;
    if(log)log("Native VMU mapping: reading %u executable bytes",map->boot_bytes);
    KUI_SMASH_TIME_SPAN(source_read,KUI_SMASH_UI_EXEC_READ);
    enum kui_game_result read=kui_game_image_read(image,map->boot_lba,boot_sectors,
        KUI_GAME_SECTOR_MODE1,boot,boot_capacity);
    KUI_SMASH_TIME_END(source_read);
    if(read!=KUI_GAME_OK) {
        if(read==KUI_GAME_CANCELLED || native_cache_cancelled(cancel)) result=NATIVE_CACHE_CANCELLED;
        goto finished;
    }
    if(native_cache_cancelled(cancel)) {result=NATIVE_CACHE_CANCELLED;goto finished;}
    kui_smash_launch_timing_metric(KUI_SMASH_UI_EXEC_SOURCE_BYTES,map->boot_bytes);
    KUI_SMASH_TIME_SPAN(source_sha,KUI_SMASH_UI_EXEC_SHA);
    kui_sha256_update(&sha,boot,map->boot_bytes);
    kui_sha256_digest(&sha,image_hash);
    KUI_SMASH_TIME_END(source_sha);
    kui_smash_launch_timing_add(KUI_SMASH_UI_EXEC_SHA_BYTES,map->boot_bytes);
    KUI_SMASH_TIME_SPAN(source_crc_time,KUI_SMASH_UI_EXEC_CRC);
    if(source_proof)source_crc=kui_crc32(source_crc,boot,map->boot_bytes);
    KUI_SMASH_TIME_END(source_crc_time);
    if(source_proof) {source_proof->complete=true;source_proof->crc32=source_crc;memcpy(source_proof->sha256,image_hash,32u);}
    kui_smash_launch_timing_phase(KUI_SMASH_UI_MDE_EXTENT);
    /* The cache filename binds the complete source read above. Its record
     * describes only the caller-proven live program, matching stage replay.
     * Do not shorten a source based on a cached record or an arbitrary tail. */
    size_t program_bytes;
    if(log)log("Native VMU mapping: validating executable lifetime extent");
    KUI_SMASH_TIME_SPAN(program_extent,KUI_SMASH_UI_PROGRAM_EXTENT);
    bool extent_valid=kui_vmu_native_program_extent(boot,map->boot_bytes,KUI_RETAIL_EXEC_ADDRESS,&program_bytes);
    KUI_SMASH_TIME_END(program_extent);
    if(!extent_valid)goto finished;
    kui_smash_launch_timing_metric(KUI_SMASH_UI_PROGRAM_BYTES,program_bytes);
    kui_smash_launch_timing_phase(KUI_SMASH_UI_MDE_PROOF);
    char path[120],hex[65];
    for(unsigned i=0;i<32u;i++) snprintf(hex+2u*i,3u,"%02x",image_hash[i]);
    int length=snprintf(path,sizeof(path),"0:/KUI/VMU/native-cache/v%u/%s.nvr",
        KUI_VMU_NATIVE_RESOLUTION_DETECTOR_EPOCH,hex);
    if(length<0 || (size_t)length>=sizeof(path)) goto finished;
    /* VMU Off never searches or creates the VMU tree. Background still
     * receives the complete proof built from the selected executable in RAM. */
    bool disk=allow_persistent_cache && !native_cache_source_conflict(source_root);
    /* This copied digest is explicitly trusted local configuration. It is
     * kept separate from the record, never passed as an aliased self-hash. */
    if(log)log("Native VMU mapping: checking saved proof");
    KUI_SMASH_TIME_SPAN(replay_time,KUI_SMASH_UI_SDK_REPLAY);
    bool hit=disk && native_cache_file_read(path,work->wire) &&
        kui_vmu_native_resolution_cache_record_digest(work->wire,sizeof(work->wire),work->digest);
    if(hit) {
        uint8_t prefix_hash[32];const uint8_t *program_hash=image_hash;
        if(program_bytes!=map->boot_bytes) {
            /* A reclaimed bootstrap suffix has a different identity. Hash
             * exactly the proved prefix once; never reuse the full-source
             * digest for that shorter program or copy a digest from wire. */
            KUI_SMASH_TIME_SPAN(prefix_sha,KUI_SMASH_UI_EXEC_SHA);
            struct kui_sha256 prefix;kui_sha256_init(&prefix);
            kui_sha256_update(&prefix,boot,program_bytes);kui_sha256_digest(&prefix,prefix_hash);
            KUI_SMASH_TIME_END(prefix_sha);
            kui_smash_launch_timing_add(KUI_SMASH_UI_EXEC_SHA_BYTES,program_bytes);
            program_hash=prefix_hash;
        }
        hit=kui_vmu_native_resolution_cache_replay_hashed(boot,program_bytes,KUI_RETAIL_EXEC_ADDRESS,
            work->wire,sizeof(work->wire),work->digest,program_hash,&work->resolution);
    }
    KUI_SMASH_TIME_END(replay_time);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_CACHE_HIT,hit?1u:0u);
    if(!hit) {
        if(native_cache_cancelled(cancel)) {result=NATIVE_CACHE_CANCELLED;goto finished;}
        if(log)log("Native VMU mapping: scanning SDK families");
        KUI_SMASH_TIME_SPAN(discover_time,KUI_SMASH_UI_SDK_DISCOVERY);
        bool resolved=kui_vmu_native_resolution_cache_build(boot,program_bytes,KUI_RETAIL_EXEC_ADDRESS,
            work->wire,sizeof(work->wire),&work->resolution);
        KUI_SMASH_TIME_END(discover_time);
        if(!resolved) {
            native_cache_standalone(boot,map,standalone);
            goto finished;
        }
        if(native_cache_cancelled(cancel)) {result=NATIVE_CACHE_CANCELLED;goto finished;}
        if(disk && !native_cache_file_publish(path,work->wire,work->compare,cancel)) {
            if(native_cache_cancelled(cancel)) {result=NATIVE_CACHE_CANCELLED;goto finished;}
            if(log) log("Native VMU cache persistence unavailable; using the complete in-memory proof");
        }
    }
    if(native_cache_cancelled(cancel)) {result=NATIVE_CACHE_CANCELLED;goto finished;}
    /* A reviewed fixed arena (no initializer slot) borrows a startup call
     * that only the combined module's stage redirects. Other modules are
     * never seeded with one; their stage refuses the game as before. An
     * arena lease (an executable without an SDK allocator) borrows the same
     * call and is the whole lease: only a combined module whose own lease is
     * exactly that arena installs it; otherwise New may use the reader-only
     * module, as for an executable without any proof. */
    const bool arena=work->resolution.heap.family==KUI_VMU_NATIVE_HEAP_ARENA;
    if(!dual && (arena || !work->resolution.heap.initializer_slot)) {
        if(log) log(arena?"Native VMU mapping: this executable's RAM arena needs the compact Background module":
            "Native VMU mapping: this executable's fixed arena needs the New virtual-A2 module");
        goto finished;
    }
    if(arena && native_cache_dual_lease(package)!=work->resolution.call.entry_bytes) {
        if(log) log("Native VMU mapping: this executable's RAM arena needs the compact Background module");
        native_cache_standalone(boot,map,standalone);
        goto finished;
    }
    if(kui_vmu_native_cache_seed_fill((uint8_t *)package->data+slot,
        KUI_VMU_NATIVE_CACHE_SEED_BYTES,work->wire,sizeof(work->wire))) {
        result=NATIVE_CACHE_READY;
        if(log) log("Native VMU mapping: %s complete executable proof; live ownership remains checked",
            hit?"cached":"resolved");
    }
finished:
    free(work);free(boot);return result;
}
