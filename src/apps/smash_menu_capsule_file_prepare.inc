/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_menu_capsule.h"
#include "kui/smash_launch_timing.h"
#include <stdlib.h>

static bool smash_menu_capsule_file_map(FIL *file,FATFS *fs,const struct kui_volume *volume,
    uint32_t bytes,struct kui_smash_menu_capsule_runs *runs) {
    if(!bytes || (bytes&511u) || file->obj.fs!=fs || f_size(file)!=bytes)return false;
    link_map[0]=LINK_MAP_WORDS;file->cltbl=link_map;
    bool ok=f_lseek(file,CREATE_LINKMAP)==FR_OK && link_map[0]>=4u && link_map[0]<=LINK_MAP_WORDS &&
        kui_smash_menu_capsule_map(link_map,link_map[0],fs->database,fs->csize,fs->n_fatent,
            volume->start,volume->count,bytes/512u,runs);
    file->cltbl=NULL;return ok;
}
/* CREATE_LINKMAP retains whole allocated clusters. Guard the complete chain
 * before truncation/freeing, not just the capsule sectors later read raw. */
static bool smash_menu_capsule_allocation_safe(const FATFS *fs,const struct kui_volume *volume,
    const struct kui_retail_manifest *map,const struct kui_vmu_persist_profile *profile,
    const struct kui_smash_menu_capsule_runs *forbidden_capsules,size_t forbidden_count,
    struct kui_smash_menu_capsule_runs *allocated) {
    uint64_t blocks=0u;
    for(size_t at=1u;at+1u<link_map[0];at+=2u) {
        blocks+=(uint64_t)link_map[at]*fs->csize;
        if(blocks>UINT32_MAX)return false;
    }
    if(forbidden_count && !forbidden_capsules)return false;
    if(!kui_smash_menu_capsule_map(link_map,link_map[0],fs->database,fs->csize,fs->n_fatent,
        volume->start,volume->count,(uint32_t)blocks,allocated) ||
       !kui_smash_menu_capsule_disjoint(allocated,map,profile))return false;
    for(size_t i=0u;i<forbidden_count;i++)
        if(forbidden_capsules[i].blocks &&
           !kui_smash_menu_capsule_runs_disjoint(allocated,&forbidden_capsules[i]))return false;
    return true;
}
/* Inspect the other capsule before preparing either file. Its old contents
 * are replaceable, but its complete allocation must be safe before a writer
 * can rely on it as the other file's forbidden range. */
static int smash_menu_capsule_file_inspect(const char *path,FATFS *fs,const struct kui_volume *volume,
    const struct kui_retail_manifest *map,const struct kui_vmu_persist_profile *profile,
    struct kui_smash_menu_capsule_runs *allocated,const char **problem,kui_cancel_fn cancel) {
    if(allocated)memset(allocated,0,sizeof(*allocated));
    if(!path || !fs || !volume || !map || !profile || !allocated || !problem || !cancel)return -1;
    *problem="existing Smash Pack capsule allocation cannot be inspected safely";
    FIL file;FRESULT opened=f_open(&file,path,FA_READ);
    if(opened==FR_NO_FILE || opened==FR_NO_PATH)return 0;
    if(opened!=FR_OK)return -1;
    uint64_t bytes=f_size(&file);struct kui_smash_menu_capsule_runs useful={0},whole={0};
    int result=-1;
    if(file.obj.fs==fs && bytes<=KUI_RUNTIME_MAX_BYTES && !(bytes&511u) && !cancel()) {
        if(!bytes && !file.obj.sclust)result=0;
        else if(bytes && smash_menu_capsule_file_map(&file,fs,volume,(uint32_t)bytes,&useful) &&
            smash_menu_capsule_allocation_safe(fs,volume,map,profile,NULL,0u,&whole))result=1;
    }
    if(f_close(&file)!=FR_OK || cancel())result=-1;
    if(result==1)*allocated=whole;
    return result;
}
/* Every existing allocation is checked before any truncate, and every new
 * allocation is checked before its first payload write. Failed preparation
 * clears the result and never publishes a partially prepared capsule. */
static bool smash_menu_capsule_file_prepare_many(FATFS *fs,const struct kui_volume *volume,
    const struct kui_retail_manifest *map,const struct kui_vmu_persist_profile *profile,
    const char *path,const uint8_t *capsule,uint32_t bytes,uint8_t page[4096],
    uint32_t immutable_bytes,const struct kui_smash_menu_capsule_runs *forbidden_capsules,size_t forbidden_count,
    struct kui_smash_menu_capsule_prepared *out,const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    if(out)memset(out,0,sizeof(*out));
    if(!fs || !volume || !map || !profile || !path || !capsule || !bytes || (bytes&511u) ||
       bytes>KUI_RUNTIME_MAX_BYTES || !immutable_bytes || immutable_bytes>bytes ||
       (forbidden_count && !forbidden_capsules) || !page || !out || !problem || !log || !cancel)return false;
    /* Share the GUI's bounded verification window across capsule compare,
     * writes and complete readback. Low memory preserves the original4KiB
     * caller buffer; no proof or payload survives this operation. */
    void *allocation=malloc(32768u+31u);
    uint8_t *buffer=allocation?(uint8_t *)(((uintptr_t)allocation+31u)&~(uintptr_t)31u):page;
    const UINT window=allocation?32768u:4096u;
    bool result=false;
    FIL file;struct kui_smash_menu_capsule_prepared prepared={0};
    FRESULT opened=f_open(&file,path,FA_READ);
    if(opened!=FR_OK && opened!=FR_NO_FILE && opened!=FR_NO_PATH) {
        *problem="cannot inspect the prepared Smash Pack capsule";goto finished;
    }
    if(opened==FR_OK) {
        *problem="existing Smash Pack capsule allocation is unsafe";
        uint64_t old_bytes=f_size(&file);
        bool safe=file.obj.fs==fs && old_bytes<=KUI_RUNTIME_MAX_BYTES && !(old_bytes&511u) &&
            ((!old_bytes && !file.obj.sclust) || (old_bytes &&
             smash_menu_capsule_file_map(&file,fs,volume,(uint32_t)old_bytes,&prepared.runs) &&
             smash_menu_capsule_allocation_safe(fs,volume,map,profile,forbidden_capsules,forbidden_count,&prepared.allocated_runs)));
        bool readable=true,same=safe && old_bytes==bytes;
        if(same && (f_lseek(&file,0u)!=FR_OK || f_tell(&file)))readable=same=false;
        for(uint32_t at=0u;same && at<immutable_bytes;) {
            UINT take=immutable_bytes-at>window?window:immutable_bytes-at,got=0u;
            readable=!cancel() && f_read(&file,buffer,take,&got)==FR_OK && got==take;
            same=readable && !memcmp(buffer,capsule+at,take);at+=take;
        }
        bool closed=f_close(&file)==FR_OK;
        if(!safe || !readable || !closed || cancel())goto finished;
        if(same) {
            kui_smash_launch_timing_metric(!strcmp(path,KUI_SMASH_MENU_CAPSULE_PATH)?
                KUI_SMASH_UI_CHILD_CAPSULE_WRITTEN:KUI_SMASH_UI_OUTER_CAPSULE_WRITTEN,0u);
            *out=prepared;log("Smash Pack: reusing a verified capsule");result=true;goto finished;
        }
    }
    *problem="cannot allocate and sync the Smash Pack capsule";
    if(cancel() || f_open(&file,path,FA_READ|FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)goto finished;
    bool ok=file.obj.fs==fs && f_expand(&file,bytes,1u)==FR_OK &&
        smash_menu_capsule_file_map(&file,fs,volume,bytes,&prepared.runs) &&
        smash_menu_capsule_allocation_safe(fs,volume,map,profile,forbidden_capsules,forbidden_count,&prepared.allocated_runs) &&
        f_lseek(&file,0u)==FR_OK && !f_tell(&file);
    for(uint32_t at=0u;ok && at<bytes;) {
        UINT take=bytes-at>window?window:bytes-at,put=0u;
        ok=!cancel() && f_write(&file,capsule+at,take,&put)==FR_OK && put==take;at+=take;
    }
    if(ok)ok=f_size(&file)==bytes && f_sync(&file)==FR_OK;
    if(f_close(&file)!=FR_OK)ok=false;
    if(!ok || cancel())goto finished;
    *problem="Smash Pack capsule readback or physical allocation changed";
    if(f_open(&file,path,FA_READ)!=FR_OK)goto finished;
    struct kui_smash_menu_capsule_prepared checked={0};
    ok=smash_menu_capsule_file_map(&file,fs,volume,bytes,&checked.runs) &&
        smash_menu_capsule_allocation_safe(fs,volume,map,profile,forbidden_capsules,forbidden_count,&checked.allocated_runs) &&
        !memcmp(&checked,&prepared,sizeof(checked)) && f_lseek(&file,0u)==FR_OK && !f_tell(&file);
    for(uint32_t at=0u;ok && at<bytes;) {
        UINT take=bytes-at>window?window:bytes-at,got=0u;
        ok=!cancel() && f_read(&file,buffer,take,&got)==FR_OK && got==take && !memcmp(buffer,capsule+at,take);at+=take;
    }
    if(f_close(&file)!=FR_OK)ok=false;
    if(ok && !cancel()) {
        kui_smash_launch_timing_metric(!strcmp(path,KUI_SMASH_MENU_CAPSULE_PATH)?
            KUI_SMASH_UI_CHILD_CAPSULE_WRITTEN:KUI_SMASH_UI_OUTER_CAPSULE_WRITTEN,1u);
        *out=prepared;result=true;
    }
finished:
    free(allocation);return result;
}
static bool smash_menu_capsule_file_prepare(FATFS *fs,const struct kui_volume *volume,
    const struct kui_retail_manifest *map,const struct kui_vmu_persist_profile *profile,
    const char *path,const uint8_t *capsule,uint32_t bytes,uint8_t page[4096],
    const struct kui_smash_menu_capsule_runs *forbidden_capsule,
    struct kui_smash_menu_capsule_prepared *out,const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    return smash_menu_capsule_file_prepare_many(fs,volume,map,profile,path,capsule,bytes,page,bytes,
        forbidden_capsule,forbidden_capsule?1u:0u,out,problem,log,cancel);
}
