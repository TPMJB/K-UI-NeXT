/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_ce_profile.h"
#include "kui/smash_card_session.h"
enum smash_capsule_file {SMASH_CAP_NATIVE,SMASH_CAP_VC2,SMASH_CAP_SWIRL,
    SMASH_CAP_OUTER,SMASH_CAP_VC2_SESSION,SMASH_CAP_SWIRL_SESSION,SMASH_CAP_CARD_SESSION,SMASH_CAP_COUNT};
static const char *const smash_capsule_paths[SMASH_CAP_COUNT]={
    KUI_SMASH_MENU_CAPSULE_PATH,KUI_SMASH_MENU_VC2_CAPSULE_PATH,KUI_SMASH_MENU_SWIRL_CAPSULE_PATH,
    KUI_SMASH_MENU_OUTER_CAPSULE_PATH,KUI_SMASH_CE_VC2_SESSION,KUI_SMASH_CE_SWIRL_SESSION,
    KUI_SMASH_CARD_WITNESS_PATH};
struct smash_collection_files {
    FATFS *fs;const struct kui_volume *volume;const struct kui_retail_manifest *map;
    const struct kui_vmu_persist_profile *profile;uint8_t *page;
    bool card_session;
    struct kui_smash_menu_capsule_runs allocated[SMASH_CAP_COUNT];
};
static unsigned smash_collection_file_count(const struct smash_collection_files *files) {
    return files->card_session?SMASH_CAP_COUNT:SMASH_CAP_CARD_SESSION;
}
/* Establish the complete old allocation set before any of the files can
 * be truncated. Each successful write replaces just its own entry. */
static bool smash_collection_files_inspect(struct smash_collection_files *files,
    const char **problem,kui_cancel_fn cancel) {
    for(unsigned i=0u;i<smash_collection_file_count(files);i++) {
        if(smash_menu_capsule_file_inspect(smash_capsule_paths[i],files->fs,files->volume,
            files->map,files->profile,&files->allocated[i],problem,cancel)<0)return false;
        for(unsigned j=0u;j<i;j++)if(files->allocated[i].blocks && files->allocated[j].blocks &&
           !kui_smash_menu_capsule_runs_disjoint(&files->allocated[i],&files->allocated[j])) {
            *problem="Smash Pack capsule allocations overlap; no capsule was changed";return false;
        }
    }
    return !cancel();
}
static bool smash_collection_file_publish(struct smash_collection_files *files,unsigned which,
    const uint8_t *payload,uint32_t bytes,uint32_t immutable_bytes,
    struct kui_smash_menu_capsule_prepared *out,const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    if(!files || which>=smash_collection_file_count(files))return false;
    struct kui_smash_menu_capsule_runs forbidden[SMASH_CAP_COUNT-1u];size_t count=0u;
    for(unsigned i=0u;i<smash_collection_file_count(files);i++)if(i!=which && files->allocated[i].blocks)
        forbidden[count++]=files->allocated[i];
    if(!smash_menu_capsule_file_prepare_many(files->fs,files->volume,files->map,files->profile,
        smash_capsule_paths[which],payload,bytes,files->page,immutable_bytes,forbidden,count,out,
        problem,log,cancel))return false;
    files->allocated[which]=out->allocated_runs;return true;
}
/* Session reuse compares all immutable helper bytes. Its live three-sector
 * tail is refreshed and read back by the selected cold stage on every entry. */
static bool smash_collection_session_publish(void *context,const char *path,
    const uint8_t *capsule,uint32_t bytes,uint32_t immutable_bytes,
    struct kui_vc2_vmu_late_seed *seed,const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    struct smash_collection_files *files=context;unsigned which=SMASH_CAP_COUNT;
    for(unsigned i=SMASH_CAP_VC2_SESSION;i<=SMASH_CAP_SWIRL_SESSION;i++)
        if(!strcmp(path,smash_capsule_paths[i]))which=i;
    struct kui_smash_menu_capsule_prepared prepared={0};
    if(!seed || bytes!=KUI_VC2_PERSIST_CAPSULE_BYTES || immutable_bytes!=KUI_VC2_PERSIST_HELPER_BYTES ||
       !smash_collection_file_publish(files,which,capsule,bytes,immutable_bytes,&prepared,problem,log,cancel) ||
       prepared.runs.blocks!=KUI_VC2_PERSIST_CAPSULE_BLOCKS)return false;
    seed->first_lba=prepared.runs.first_lba;seed->first_blocks=prepared.runs.first_blocks;
    seed->second_lba=prepared.runs.second_lba;return true;
}
