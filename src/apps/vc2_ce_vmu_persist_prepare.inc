/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_ce_vmu_persist.h"
#include "kui/smash_ce_profile.h"
#include "kui/smash_ce_helper.h"
#include "kui/smash_ce_transport.h"

typedef bool (*vc2_persist_capsule_publish_fn)(void *context,const char *path,
    const uint8_t *capsule,uint32_t bytes,uint32_t immutable_bytes,
    struct kui_vc2_vmu_late_seed *seed,const char **problem,kui_log_fn log,kui_cancel_fn cancel);

static bool vc2_persist_published_runs(const struct kui_vc2_vmu_late_seed *s,
    const struct kui_volume *v) {
    if(!s->first_lba || !s->first_blocks || s->first_blocks>KUI_VC2_PERSIST_CAPSULE_BLOCKS ||
       s->first_lba<v->start || (uint64_t)s->first_lba+s->first_blocks>(uint64_t)v->start+v->count)return false;
    uint32_t second=KUI_VC2_PERSIST_CAPSULE_BLOCKS-s->first_blocks;
    if(!second)return s->second_lba==0u;
    return s->second_lba>=v->start && s->second_lba &&
        (uint64_t)s->second_lba+second<=(uint64_t)v->start+v->count &&
        !((uint64_t)s->first_lba+s->first_blocks>s->second_lba &&
          (uint64_t)s->second_lba+second>s->first_lba);
}

/* The generated capsule is separate from the game and distributed helper.
 * Its final three sectors are reserved for the stage's sealed handoff. */
static bool vc2_persist_runs(const DWORD *table,size_t words,const FATFS *fs,
    const struct kui_volume *volume,struct kui_vc2_vmu_late_seed *seed) {
    if(!table || !fs || !volume || !seed || words<4u || (words&1u) ||
       !volume->count || (uint64_t)volume->start+volume->count>UINT64_C(0x100000000) ||
       !fs->database || fs->database>=volume->count || !fs->csize ||
       (fs->csize&(fs->csize-1u)) || fs->n_fatent<=2u ||
       (uint64_t)fs->database+(uint64_t)(fs->n_fatent-2u)*fs->csize>volume->count ||
       table[0]!=words || table[words-1u])return false;
    uint32_t lba[2]={0u,0u},blocks[2]={0u,0u},done=0u;unsigned runs=0u;
    for(size_t at=1u;at+1u<words;at+=2u) {
        uint32_t clusters=table[at],first=table[at+1u];
        if(!clusters || first<2u || first>=fs->n_fatent ||
           clusters>fs->n_fatent-first)return false;
        uint64_t sector=(uint64_t)fs->database+(uint64_t)(first-2u)*fs->csize;
        uint64_t count=(uint64_t)clusters*fs->csize;
        if(sector<fs->database || sector>=volume->count || count>volume->count-sector)return false;
        if(done==KUI_VC2_PERSIST_CAPSULE_BLOCKS)continue;
        if(count>KUI_VC2_PERSIST_CAPSULE_BLOCKS-done)count=KUI_VC2_PERSIST_CAPSULE_BLOCKS-done;
        uint64_t card_lba=(uint64_t)volume->start+sector;
        if(card_lba>UINT32_MAX || card_lba+count>(uint64_t)volume->start+volume->count)return false;
        if(runs && card_lba==(uint64_t)lba[runs-1u]+blocks[runs-1u])blocks[runs-1u]+=(uint32_t)count;
        else {
            if(runs==2u)return false;
            lba[runs]=(uint32_t)card_lba;blocks[runs]=(uint32_t)count;++runs;
        }
        done+=(uint32_t)count;
    }
    if(done!=KUI_VC2_PERSIST_CAPSULE_BLOCKS || !runs ||
       (runs==2u && (uint64_t)lba[1]<(uint64_t)lba[0]+blocks[0] &&
        (uint64_t)lba[1]+blocks[1]>lba[0]))return false;
    seed->first_lba=lba[0];seed->first_blocks=blocks[0];seed->second_lba=runs==2u?lba[1]:0u;
    return true;
}
static bool vc2_persist_binding(FATFS *fs,const struct kui_vmu_game_card_choice *choice,
    const struct kui_vmu_persist_profile *profile) {
    char key[12],path[128],pair[96];uint8_t binding[KUI_VMU_GAME_CARD_BINDING_BYTES];
    if(!choice->pair || !kui_vmu_game_card_key(KUI_VC2_PERSIST_PRODUCT,key) ||
       memcmp(choice->key,key,sizeof(key)))return false;
    snprintf(pair,sizeof(pair),"/KUI/vmu-test/p%04u",choice->pair);
    if(strcmp(profile->pair_path,pair))return false;
    snprintf(path,sizeof(path),"0:%s/%s",pair,KUI_VMU_GAME_CARD_BINDING_NAME);
    FIL file;if(f_open(&file,path,FA_READ)!=FR_OK)return false;
    UINT got=0u;char bound[12];
    bool ok=file.obj.fs==fs && f_size(&file)==sizeof(binding) &&
        f_read(&file,binding,sizeof(binding),&got)==FR_OK && got==sizeof(binding) &&
        kui_vmu_game_card_binding_decode(binding,sizeof(binding),bound) && !memcmp(bound,key,sizeof(key));
    if(f_close(&file)!=FR_OK)ok=false;
    return ok;
}
/* Reuse only the verified code and current physical map. The old config tail
 * is deliberately ignored: the stage always replaces all three sectors and
 * verifies them before publishing this launch's config CRC. No stale handoff
 * can become live by reusing the immutable helper bytes. */
static int vc2_persist_existing_capsule(FATFS *fs,const struct kui_volume *volume,
    const char *path,const uint8_t *helper,uint8_t page[4096],
    struct kui_vc2_vmu_late_seed *seed,kui_cancel_fn cancel) {
    FIL file;FRESULT opened=f_open(&file,path,FA_READ);
    if(opened==FR_NO_FILE || opened==FR_NO_PATH)return 0;
    if(opened!=FR_OK)return -1;
    int result=0;
    if(file.obj.fs!=fs)result=-1;
    else if(f_size(&file)==KUI_VC2_PERSIST_CAPSULE_BYTES) {
        struct kui_smash_ce_transport transport;
        bool dynamic=kui_smash_ce_transport_static(helper,KUI_VC2_PERSIST_HELPER_BYTES,&transport);
        uint32_t mutable_begin=dynamic?KUI_SMASH_CE_TRANSPORT_CODE_OFFSET+transport.runtime_offset:0u;
        uint32_t mutable_end=dynamic?mutable_begin+transport.runtime_bytes:0u;
        result=1;
        for(uint32_t at=0u;result==1 && at<KUI_VC2_PERSIST_HELPER_BYTES;at+=4096u) {
            UINT got=0u;
            if(cancel() || f_read(&file,page,4096u,&got)!=FR_OK || got!=4096u)result=-1;
            else for(uint32_t i=0u;i<4096u;i++)
                if(!(dynamic && at+i>=mutable_begin && at+i<mutable_end) && page[i]!=helper[at+i]) {result=0;break;}
        }
        if(result==1) {
            link_map[0]=LINK_MAP_WORDS;file.cltbl=link_map;
            if(f_lseek(&file,CREATE_LINKMAP)!=FR_OK || link_map[0]<4u || link_map[0]>LINK_MAP_WORDS ||
               !vc2_persist_runs(link_map,link_map[0],fs,volume,seed))result=-1;
            file.cltbl=NULL;
        }
    }
    if(f_close(&file)!=FR_OK)result=-1;
    return result;
}
static bool vc2_ce_vmu_persist_prepare_paths(FATFS *fs,const struct kui_volume *volume,
    const struct kui_retail_manifest *map,struct kui_runtime_image *package,
    const char *helper_path,const char *capsule_path,unsigned expected_kind,
    const uint8_t *prebound_wire,const struct kui_vmu_game_card_choice *prebound_choice,
    vc2_persist_capsule_publish_fn publish_capsule,void *publish_context,
    const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    static char refusal[128];
    if(!fs || !volume || !map || !package || !package->data || !helper_path || !capsule_path ||
       !problem || !log || !cancel || (!!prebound_wire!=!!prebound_choice))return false;
    *problem="VC2 persistent module seed or card profile is missing, changed or ambiguous";
    if((fs->fs_type!=FS_FAT32 && fs->fs_type!=FS_EXFAT) ||
       map->track_count>KUI_RETAIL_MANIFEST_SLOTS ||
       map->extent_count>KUI_RETAIL_MANIFEST_SLOTS-map->track_count ||
       map->partition_start!=volume->start || map->partition_end!=(uint64_t)volume->start+volume->count)return false;
    uint8_t *data=package->data;size_t found=SIZE_MAX;
    struct kui_vc2_vmu_late_seed seed={0};
    if(package->info.payload_bytes<sizeof(seed))return false;
    for(size_t at=0u;at<=package->info.payload_bytes-sizeof(seed);++at) {
        if(le32(data+at)!=KUI_VC2_LATE_SEED_MAGIC0 || le32(data+at+4u)!=KUI_VC2_LATE_SEED_MAGIC1 ||
           le32(data+at+8u)!=KUI_VC2_PERSIST_SEED_VERSION || le32(data+at+12u)!=sizeof(seed))continue;
        if(found!=SIZE_MAX || (at&3u))return false;
        memcpy(&seed,data+at,sizeof(seed));
        if(seed.helper_bytes!=KUI_VC2_PERSIST_HELPER_BYTES || seed.first_lba || seed.first_blocks ||
           seed.second_lba || seed.reserved || seed.seed_crc32)return false;
        found=at;
    }
    size_t profile_slot=kui_vmu_game_card_profile_slot(data,package->info.payload_bytes);
    if(found==SIZE_MAX || profile_slot==SIZE_MAX ||
       (found<profile_slot+KUI_VMU_PERSIST_PROFILE_WIRE_BYTES && profile_slot<found+sizeof(seed)) || cancel())return false;
    struct kui_smash_ce_helper embedded={0};int embedded_result=0;
    if(expected_kind) {
        struct kui_smash_ce_profile owner;
        if(!prebound_wire || !publish_capsule ||
           kui_smash_ce_profile_find(data,package->info.payload_bytes,expected_kind,&owner)!=1 ||
           owner.helper_crc32!=seed.helper_crc32 || owner.helper_bytes!=seed.helper_bytes)return false;
        embedded_result=kui_smash_ce_helper_find(data,package->info.payload_bytes,expected_kind,&embedded);
        if(embedded_result<0 || (owner.version==KUI_SMASH_CE_PROFILE_EMBEDDED_VERSION && embedded_result!=1))return false;
    }
    struct kui_vmu_game_card_work *work=malloc(sizeof(*work));
    uint8_t *wire=malloc(KUI_VMU_PERSIST_PROFILE_WIRE_BYTES);
    uint8_t *capsule=calloc(1u,KUI_VC2_PERSIST_CAPSULE_BYTES);
    struct kui_vmu_fs_range *forbidden=map->extent_count?malloc(sizeof(*forbidden)*map->extent_count):NULL;
    bool ok=false;
    if(!work || !wire || !capsule || (map->extent_count && !forbidden)) {
        *problem="not enough memory for the shared Sega Smash Pack VMU";goto done;
    }
    *problem="matching VC2 helper is missing, changed or unreadable";
    log("Smash Pack VMU: checking matching helper");
    FIL file;char resolved_helper[192];
    if(embedded_result==1) {
        memcpy(capsule,embedded.raw,embedded.stored_bytes);ok=true;
        log("Smash Pack VMU: using helper embedded in the selected CE module");
    } else {
    if(!kui_runtime_resolve_path(helper_path,resolved_helper,sizeof(resolved_helper)) ||
       f_open(&file,resolved_helper,FA_READ)!=FR_OK)goto done;
    ok=file.obj.fs==fs && f_size(&file)==KUI_VC2_PERSIST_HELPER_BYTES;
    for(uint32_t at=0u;ok && at<KUI_VC2_PERSIST_HELPER_BYTES;at+=4096u) {
        UINT got=0u;
        ok=!cancel() && f_read(&file,capsule+at,4096u,&got)==FR_OK && got==4096u;
    }
    if(f_close(&file)!=FR_OK)ok=false;
    }
    if(!ok || cancel() || kui_retail_crc32(0u,capsule,KUI_VC2_PERSIST_HELPER_BYTES)!=seed.helper_crc32) {ok=false;goto done;}
    for(uint32_t i=0u;i<map->extent_count;++i) {
        const struct kui_retail_extent *e=&map->slots[map->track_count+i].extent;
        forbidden[i]=(struct kui_vmu_fs_range){e->card_lba,e->blocks};
    }
    struct games_card_platform context={.fs=fs,.volume=*volume};
    const struct kui_vmu_game_card_platform platform={&context,games_card_identity,games_card_raw_begin,
        games_card_raw_end,games_card_salt};
    struct kui_vmu_game_card_choice choice={0};
    int result;
    if(prebound_wire) {
        /* The collection launcher owns selection/history once. Recheck the
         * mounted physical card and binding, without selecting another pair. */
        log("Smash Pack VMU: checking the collection's shared card");
        memcpy(wire,prebound_wire,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES);choice=*prebound_choice;
        struct kui_vmu_game_card_raw identity={0};
        result=KUI_VMU_GAME_CARD_FAILED;
        if(!kui_vmu_persist_profile_decode(&work->profile,wire,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES) &&
           games_card_identity(&context,&identity) &&
           !memcmp(identity.cid,work->profile.cid,sizeof(identity.cid)) &&
           identity.card_sectors==work->profile.card_sectors &&
           identity.high_capacity==(work->profile.high_capacity!=0u) &&
           work->profile.partition_first==map->partition_start &&
           work->profile.partition_end==map->partition_end)result=KUI_VMU_GAME_CARD_READY;
    } else {
        log("Smash Pack VMU: selecting dedicated test card (cached maps when unchanged)");
        result=kui_vmu_game_card_select(KUI_VC2_PERSIST_PRODUCT,map->partition_start,map->partition_end,
            forbidden,map->extent_count,&platform,work,wire,&choice,log,cancel);
    }
    ok=result==KUI_VMU_GAME_CARD_READY &&
        !kui_vmu_persist_profile_decode(&work->profile,wire,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES) &&
        !kui_vmu_game_card_template(work->profile.cid) && vc2_persist_binding(fs,&choice,&work->profile);
    if(!ok) {
        snprintf(refusal,sizeof(refusal),"shared Sega Smash Pack VMU refused (%d): %.72s",result,choice.problem);
        *problem=refusal;goto done;
    }
    if(publish_capsule) {
        const struct kui_vc2_vmu_late_seed immutable=seed;
        ok=publish_capsule(publish_context,capsule_path,capsule,KUI_VC2_PERSIST_CAPSULE_BYTES,
            KUI_VC2_PERSIST_HELPER_BYTES,&seed,problem,log,cancel);
        struct kui_vc2_vmu_late_seed checked=seed;
        checked.first_lba=checked.first_blocks=checked.second_lba=0u;
        if(!ok || cancel() || memcmp(&checked,&immutable,sizeof(checked)) ||
           !vc2_persist_published_runs(&seed,volume)) {ok=false;goto done;}
        goto publish;
    }
    *problem="existing VC2 session capsule could not be verified";
    log("Smash Pack VMU: checking existing session capsule");
    int existing=vc2_persist_existing_capsule(fs,volume,capsule_path,capsule,work->check,&seed,cancel);
    if(existing<0 || cancel()) {ok=false;goto done;}
    if(existing>0) {
        log("Smash Pack VMU: reusing verified session capsule; no capsule rewrite");
        goto publish;
    }
    *problem="cannot create and sync the generated VC2 session capsule";
    log("Smash Pack VMU: creating matching session capsule");
    if(cancel() || f_open(&file,capsule_path,FA_READ|FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK) {ok=false;goto done;}
    ok=file.obj.fs==fs;
    for(uint32_t at=0u;ok && at<KUI_VC2_PERSIST_CAPSULE_BYTES;) {
        UINT take=KUI_VC2_PERSIST_CAPSULE_BYTES-at>4096u?4096u:KUI_VC2_PERSIST_CAPSULE_BYTES-at,put=0u;
        ok=!cancel() && f_write(&file,capsule+at,take,&put)==FR_OK && put==take;at+=take;
    }
    if(ok)ok=f_size(&file)==KUI_VC2_PERSIST_CAPSULE_BYTES && f_sync(&file)==FR_OK;
    if(f_close(&file)!=FR_OK)ok=false;
    if(!ok || cancel()) {ok=false;goto done;}
    *problem="VC2 session capsule must read back exactly in at most two physical runs";
    if(f_open(&file,capsule_path,FA_READ)!=FR_OK) {ok=false;goto done;}
    ok=file.obj.fs==fs && f_size(&file)==KUI_VC2_PERSIST_CAPSULE_BYTES;
    if(ok) {
        link_map[0]=LINK_MAP_WORDS;file.cltbl=link_map;
        ok=f_lseek(&file,CREATE_LINKMAP)==FR_OK && link_map[0]>=4u && link_map[0]<=LINK_MAP_WORDS &&
            vc2_persist_runs(link_map,link_map[0],fs,volume,&seed);
        file.cltbl=NULL;
    }
    if(ok)ok=f_lseek(&file,0u)==FR_OK && f_tell(&file)==0u;
    for(uint32_t at=0u;ok && at<KUI_VC2_PERSIST_CAPSULE_BYTES;) {
        UINT take=KUI_VC2_PERSIST_CAPSULE_BYTES-at>sizeof(work->check)?sizeof(work->check):KUI_VC2_PERSIST_CAPSULE_BYTES-at;
        UINT got=0u;ok=!cancel() && f_read(&file,work->check,take,&got)==FR_OK && got==take &&
            !memcmp(work->check,capsule+at,take);at+=take;
    }
    if(f_close(&file)!=FR_OK)ok=false;
    if(!ok || cancel()) {ok=false;goto done;}
publish:
    seed.seed_crc32=kui_retail_crc32(0u,&seed,sizeof(seed));
    memcpy(data+profile_slot,wire,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES);
    memcpy(data+found,&seed,sizeof(seed));
    log("Sega Smash Pack VMU p%04u: checked %u-byte session capsule in %u physical runs",choice.pair,
        KUI_VC2_PERSIST_CAPSULE_BYTES,seed.second_lba?2u:1u);
done:
    free(work);free(wire);free(capsule);free(forbidden);return ok;
}
static bool vc2_ce_vmu_persist_prepare_launch(FATFS *fs,const struct kui_volume *volume,
    const struct kui_retail_manifest *map,struct kui_runtime_image *package,
    const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    return vc2_ce_vmu_persist_prepare_paths(fs,volume,map,package,
        KUI_VC2_LATE_FILE_PATH,KUI_VC2_PERSIST_CAPSULE_PATH,0u,NULL,NULL,NULL,NULL,problem,log,cancel);
}
/* The unified collection must supply the exact profile selected once for all
 * children. Only the matching sealed CE module chooses its helper/session
 * filenames; the caller cannot redirect either path to another backend. */
static bool vc2_ce_vmu_persist_prepare_prebound_launch(FATFS *fs,const struct kui_volume *volume,
    const struct kui_retail_manifest *map,struct kui_runtime_image *package,unsigned kind,
    const uint8_t wire[KUI_VMU_PERSIST_PROFILE_WIRE_BYTES],const struct kui_vmu_game_card_choice *choice,
    vc2_persist_capsule_publish_fn publish_capsule,void *publish_context,
    const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    const char *helper=NULL,*session=NULL;
    if(!wire || !choice || !kui_smash_ce_profile_paths(kind,NULL,&helper,&session))return false;
    return vc2_ce_vmu_persist_prepare_paths(fs,volume,map,package,helper,session,kind,
        wire,choice,publish_capsule,publish_context,problem,log,cancel);
}
