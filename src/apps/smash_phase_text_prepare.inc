/* SPDX-License-Identifier: GPL-3.0-only */
/* The diagnostic native stage only overwrites an already prepared data extent.
 * Allocation, old-file safety and complete readback remain GUI operations. */
#include "kui/smash_phase_file.h"
#define SMASH_PHASE_TEXT_PATH KUI_SMASH_PHASE_FILE_PATH
#define SMASH_PHASE_TEXT_BYTES KUI_SMASH_PHASE_FILE_BYTES
static int smash_phase_text_capability(const struct kui_runtime_image *native) {
    static const uint8_t marker[16]={
        'K','U','I','S','T','X','T','1',1u,0u,0u,0u,16u,0u,0u,0u};
    if(!native || !native->data)return -1;
    const uint8_t *p=native->data;size_t bytes=native->info.payload_bytes,found=SIZE_MAX;
    if(bytes<sizeof(marker))return 0;
    for(size_t at=0u;at<=bytes-sizeof(marker);at++) {
        if(memcmp(p+at,marker,sizeof(marker)))continue;
        if((at&3u) || found!=SIZE_MAX)return -1;
        found=at;
    }
    return found==SIZE_MAX?0:1;
}
/* No typed record means the normal build performs no phase-file operations.
 * An opted-in diagnostic gets a fixed, fully compared reset record per GUI
 * launch, after all six capsule allocations have been established. */
static bool smash_phase_text_prepare(struct files *source,
    struct smash_collection_files *capsules,const struct kui_runtime_image *native,
    const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    if(!source || !capsules || !native || !problem || !log || !cancel)return false;
    int capability=smash_phase_text_capability(native);
    if(!capability)return true;
    *problem="native phase-text capability is ambiguous or unaligned";
    if(capability<0)return false;
    *problem="native phase-text source directory cannot be normalized safely";
    char root[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];
    if(strcmp(source->root,"/") && (!kui_game_launch_settings_normalize(root,source->root) ||
       !strcmp(root,"/kui/apps/games")))return false;
    *problem="native phase-text module build is not a twelve-digit identity";
    for(unsigned i=0u;i<12u;i++) {
        unsigned c=(unsigned char)native->info.build[i];
        if(!((c>='0' && c<='9') || (c>='a' && c<='f')))return false;
    }
    *problem="native phase-text requires the complete prepared collection allocation set";
    for(unsigned i=0u;i<smash_collection_file_count(capsules);i++)if(!capsules->allocated[i].blocks)return false;
    *problem="native phase-text source reader could not be closed";
    if(cancel() || !close_reader(source))return false;
    uint8_t *text=malloc(SMASH_PHASE_TEXT_BYTES);
    *problem="not enough memory for native phase-text reset record";
    if(!text)return false;
    memset(text,' ',SMASH_PHASE_TEXT_BYTES);
    int n=snprintf((char *)text,SMASH_PHASE_TEXT_BYTES,
        KUI_SMASH_PHASE_FILE_PREFIX "%.12s\nPending: native measurement has not completed.\n",
        native->info.build);
    bool ok=n>=0 && (unsigned)n<SMASH_PHASE_TEXT_BYTES;
    if(ok) {
        text[n]=' ';text[SMASH_PHASE_TEXT_BYTES-1u]='\n';
        struct kui_smash_menu_capsule_prepared prepared={0};
        ok=smash_menu_capsule_file_prepare_many(capsules->fs,capsules->volume,
            capsules->map,capsules->profile,SMASH_PHASE_TEXT_PATH,text,SMASH_PHASE_TEXT_BYTES,
            capsules->page,SMASH_PHASE_TEXT_BYTES,capsules->allocated,smash_collection_file_count(capsules),
            &prepared,problem,log,cancel);
    } else *problem="native phase-text reset record formatting failed";
    free(text);
    if(ok)log("Smash Pack: diagnostic native phase TXT prepared at %s",SMASH_PHASE_TEXT_PATH);
    return ok;
}
