/* SPDX-License-Identifier: GPL-3.0-only */
/* Explicit cold children use the already-bound Smash Pack card. They cannot
 * create or replace a binding; native A1 is merely another presentation. */
static size_t smash_native_seed_slot(const struct kui_runtime_image *package) {
    if(!package || !package->data || package->info.payload_bytes<KUI_SMASH_NATIVE_CHILD_SEED_BYTES)return SIZE_MAX;
    const uint8_t *data=package->data;size_t found=SIZE_MAX;
    for(size_t at=0u;at<=package->info.payload_bytes-KUI_SMASH_NATIVE_CHILD_SEED_BYTES;++at) {
        if(le32(data+at)!=KUI_SMASH_NATIVE_CHILD_SEED_MAGIC0 || le32(data+at+4u)!=KUI_SMASH_NATIVE_CHILD_SEED_MAGIC1 ||
           le32(data+at+8u)!=1u || le32(data+at+12u)!=KUI_SMASH_NATIVE_CHILD_SEED_BYTES)continue;
        struct kui_smash_native_child_seed seed;memcpy(&seed,data+at,sizeof(seed));
        if(found!=SIZE_MAX || (at&3u) || seed.version!=1u || seed.bytes!=sizeof(seed) ||
           seed.child || seed.reserved[0] || seed.reserved[1] || seed.crc32)return SIZE_MAX;
        found=at;
    }
    if(found==SIZE_MAX || kui_vmu_game_card_profile_slot(data,package->info.payload_bytes)==SIZE_MAX ||
       kui_vmu_native_cache_seed_find(data,package->info.payload_bytes)==SIZE_MAX)return SIZE_MAX;
    return found;
}
static bool smash_native_seed_fill(struct kui_runtime_image *package,unsigned child,const char **problem) {
    size_t at=smash_native_seed_slot(package);struct kui_smash_native_child_seed seed={.child=child};
    if(at==SIZE_MAX || !kui_smash_native_child_seed_seal(&seed)) {
        *problem="Smash Pack native child module seed is absent or changed";return false;
    }
    memcpy((uint8_t *)package->data+at,&seed,sizeof(seed));return true;
}
static int smash_native_card_boundary(void *context,enum kui_vmu_game_card_boundary_phase phase,
    const char *product,unsigned pair,const uint8_t identity[16],
    const struct kui_vmu_game_candidates_storage *storage,kui_log_fn log,kui_cancel_fn cancel) {
    struct games_card_platform *platform=context;
    if(phase==KUI_VMU_GAME_CARD_BEFORE_CREATE) {
        snprintf(platform->history_problem,sizeof(platform->history_problem),
            "existing shared Sega Smash Pack VMU is required; run the VC2 VMU test first");
        return KUI_VMU_GAME_CARD_FAILED;
    }
    return games_card_history(context,phase,product,pair,identity,storage,log,cancel);
}
static bool smash_native_card_prepare(const struct kui_retail_manifest *map,struct kui_runtime_image *package,
    FATFS *fs,const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    static char refusal[128];bool ok=false;
    size_t slot=kui_vmu_game_card_profile_slot(package->data,package->info.payload_bytes);
    struct kui_vmu_game_card_work *work=malloc(sizeof(*work));
    uint8_t *wire=malloc(KUI_VMU_PERSIST_PROFILE_WIRE_BYTES);
    struct kui_vmu_fs_range *forbidden=map->extent_count?malloc(map->extent_count*sizeof(*forbidden)):NULL;
    if(slot==SIZE_MAX || !work || !wire || (map->extent_count && !forbidden)) {
        *problem="not enough memory or no profile slot for the shared Smash Pack VMU";goto done;
    }
    for(uint32_t i=0;i<map->extent_count;i++) {
        const struct kui_retail_extent *e=&map->slots[map->track_count+i].extent;
        forbidden[i]=(struct kui_vmu_fs_range){e->card_lba,e->blocks};
    }
    struct games_card_platform context={.fs=fs,.volume=*kui_media_volume()};
    const struct kui_vmu_game_card_platform platform={&context,games_card_identity,games_card_raw_begin,
        games_card_raw_end,games_card_salt};
    struct kui_vmu_game_card_choice choice={0};
    int result=kui_vmu_game_card_select_boundary(KUI_VC2_PERSIST_PRODUCT,NULL,NULL,
        map->partition_start,map->partition_end,forbidden,map->extent_count,&platform,work,wire,&choice,
        smash_native_card_boundary,&context,log,cancel);
    ok=result==KUI_VMU_GAME_CARD_READY &&
        !kui_vmu_persist_profile_decode(&work->profile,wire,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES) &&
        !kui_vmu_game_card_template(work->profile.cid) && vc2_persist_binding(fs,&choice,&work->profile);
    if(ok) {
        memcpy((uint8_t *)package->data+slot,wire,KUI_VMU_PERSIST_PROFILE_WIRE_BYTES);
        log("Smash Pack native VMU: existing shared card pair %u presented in A1",choice.pair);
    } else {
        snprintf(refusal,sizeof(refusal),"shared Smash Pack card refused: %.90s",
            context.history_problem[0]?context.history_problem:choice.problem);*problem=refusal;
    }
done:
    free(forbidden);free(wire);free(work);return ok;
}
