/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_card_session.h"
struct smash_card_session_gui {
    bool enabled;
    size_t offset[4];
    uint32_t generation;
    struct kui_smash_menu_capsule_prepared witness;
};
static struct kui_runtime_image *smash_card_session_module(unsigned i,
    struct kui_runtime_image *outer,struct kui_runtime_image children[3]) {
    return i?&children[i-1u]:outer;
}
/* This weaker tag check supplies only downgrade detection, never admission.
 * The codec independently requires one complete, aligned blank config. */
static bool smash_card_session_tag(const struct kui_runtime_image *module) {
    static const uint8_t tag[8]={'K','U','I','S','C','F','0','1'};
    const uint8_t *p=module->data;size_t bytes=module->info.payload_bytes;
    if(!p || bytes<sizeof(tag))return false;
    for(size_t i=0u;i<=bytes-sizeof(tag);i++)if(!memcmp(p+i,tag,sizeof(tag)))return true;
    return false;
}
static bool smash_card_session_modules(struct smash_card_session_gui *session,
    struct kui_runtime_image *outer,struct kui_runtime_image children[3],const char **problem) {
    if(!session || !outer || !children || !problem)return false;
    memset(session,0,sizeof(*session));unsigned tagged=0u;
    for(unsigned i=0u;i<4u;i++) {
        struct kui_runtime_image *module=smash_card_session_module(i,outer,children);
        bool tag=smash_card_session_tag(module);tagged+=(unsigned)tag;
        session->offset[i]=kui_smash_card_config_find(module->data,module->info.payload_bytes);
        if(tag && session->offset[i]==SIZE_MAX) {
            *problem="Smash Pack shared-card config is missing, changed, duplicated or unaligned";return false;
        }
    }
    if(!tagged)return true;
    *problem="Smash Pack shared-card session needs all four matching capabilities";
    if(tagged!=4u)return false;
    for(unsigned i=0u;i<4u;i++) {
        struct kui_runtime_image *module=smash_card_session_module(i,outer,children);
        if(module->info.payload_bytes<KUI_SMASH_CARD_RECEIPT_OFFSET+KUI_SMASH_CARD_RECEIPT_BYTES ||
           memcmp(module->info.build,outer->info.build,12u))return false;
        struct kui_smash_card_receipt receipt;
        memcpy(&receipt,(const uint8_t *)module->data+KUI_SMASH_CARD_RECEIPT_OFFSET,sizeof(receipt));
        if(!kui_smash_card_receipt_blank(&receipt)) {
            *problem="Smash Pack shared-card live receipt is not blank at the entry-prefix ABI";return false;
        }
    }
    session->enabled=true;return true;
}
static bool smash_card_session_prepare(struct files *source,struct smash_collection_files *files,
    struct kui_runtime_image *outer,struct kui_runtime_image children[3],
    struct smash_card_session_gui *session,const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    if(!source || !files || !outer || !children || !session || !problem || !log || !cancel)return false;
    if(!session->enabled)return true;
    *problem="Smash Pack shared-card witness source directory is unsafe";
    char root[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];
    if(strcmp(source->root,"/") && (!kui_game_launch_settings_normalize(root,source->root) ||
       !strcmp(root,"/kui/apps/games")))return false;
    if(!files->card_session || cancel() || !close_reader(source))return false;
    uint8_t *payload=calloc(1u,KUI_VMU_FS_WITNESS_FILE_BYTES);
    *problem="not enough memory for the shared-card witness allocation";
    if(!payload)return false;
    memcpy(payload,"KUIFSW01",8u);payload[8u]=1u;payload[12u]=0u;payload[13u]=2u;
    payload[16u]=KUI_VMU_FS_WITNESS_RECORD_BYTES;
    /* Only the first16 bytes are immutable. A same-size existing allocation
     * retains all runtime manifest bytes; persisted bytes grant no authority. */
    bool ok=smash_collection_file_publish(files,SMASH_CAP_CARD_SESSION,payload,
        KUI_VMU_FS_WITNESS_FILE_BYTES,16u,&session->witness,problem,log,cancel);
    free(payload);if(!ok)return false;
    struct games_card_platform salt_context={.fs=files->fs,.volume=*files->volume};uint8_t salt[16];
    games_card_salt(&salt_context,salt);
    session->generation=kui_retail_crc32(0u,salt,sizeof(salt));if(!session->generation)session->generation=1u;
    struct kui_vmu_fs_witness_binding common={0};
    for(unsigned i=0u;i<4u;i++) {
        struct kui_runtime_image *module=smash_card_session_module(i,outer,children);
        struct kui_smash_card_config config;
        memcpy(&config,(const uint8_t *)module->data+session->offset[i],sizeof(config));
        *problem="Smash Pack shared-card config binding failed before capsule publication";
        if(!kui_smash_card_configure(&config,i+1u,session->generation,module->info.build,
            &session->witness.runs,&session->witness.allocated_runs,files->profile))return false;
        struct kui_vmu_fs_witness_binding binding;
        if(!kui_smash_card_binding(&config,files->profile,&binding) ||
           (i && memcmp(&common,&binding,sizeof(binding))))return false;
        if(!i)common=binding;
        memcpy((uint8_t *)module->data+session->offset[i],&config,sizeof(config));
    }
    log("Smash Pack: shared-card witness prepared; generation %lu, all four configs bound",
        (unsigned long)session->generation);
    return !cancel();
}
