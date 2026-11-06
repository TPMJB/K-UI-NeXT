/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/runtime.h"
#include "kui/smash_menu_capsule.h"
#include "kui/smash_menu_loader.h"
#include "kui/smash_menu_return.h"
#include "kui/smash_native_menu.h"
#include "kui/smash_launch_timing.h"
#include <stdlib.h>
#include <string.h>

static bool smash_menu_capsule_plan(const struct kui_smash_menu_capsule_prepared *capsule,
    struct kui_smash_menu_loader_seed *seed) {
    *seed=(struct kui_smash_menu_loader_seed){.payload_bytes=capsule->payload_bytes,
        .padded_bytes=capsule->bytes,.payload_crc32=capsule->crc32,.menu_seed_offset=capsule->seed_offset,
        .first_lba=capsule->runs.first_lba,.first_blocks=capsule->runs.first_blocks,
        .second_lba=capsule->runs.second_lba,.block_count=capsule->runs.blocks};
    return kui_smash_menu_loader_seed_seal(seed);
}
static bool smash_native_menu_seed_prepare(struct kui_runtime_image *package,
    const struct kui_smash_menu_capsule_prepared *capsule,const char **problem) {
    size_t offset=kui_smash_menu_loader_seed_find(package->data,package->info.payload_bytes);
    struct kui_smash_menu_loader_seed seed;
    if(offset==SIZE_MAX || !smash_menu_capsule_plan(capsule,&seed)) {
        *problem="Smash Pack native menu capsule seed is missing or invalid";return false;
    }
    memcpy((uint8_t *)package->data+offset,&seed,sizeof(seed));return true;
}
static bool smash_menu_capsule_seeds_disjoint(size_t a,size_t an,size_t b,size_t bn) {
    return a<=SIZE_MAX-an && b<=SIZE_MAX-bn && (a+an<=b || b+bn<=a);
}
/* Publish child first, then an outer snapshot containing its complete child
 * plan and a blank return seed. The outer capsule's own plan is installed
 * only in the initial launch allocation after its full CRC/readback exists.
 * Neither disk image contains a checksum dependency on its own return plan. */
static bool smash_menu_capsule_pair_publish(FATFS *fs,const struct kui_volume *volume,
    const struct kui_retail_manifest *map,const struct kui_vmu_persist_profile *profile,
    const struct kui_runtime_image *child,struct kui_runtime_image *outer,uint8_t page[4096],
    const char **problem,kui_log_fn log,kui_cancel_fn cancel) {
    if(!fs || !volume || !map || !profile || !child || !outer || !child->data || !outer->data ||
       !page || !problem || !log || !cancel)return false;
    *problem="Smash Pack capsule pair is missing matching blank native and return seeds";
    uint32_t child_bytes=child->info.payload_bytes,outer_bytes=outer->info.payload_bytes;
    if(child_bytes>UINT32_C(0x1f0000) || outer_bytes>UINT32_C(0x1f0000) ||
       (child_bytes&3u) || (outer_bytes&3u) ||
       child->info.memory_bytes!=child_bytes || outer->info.memory_bytes!=outer_bytes ||
       memcmp(child->info.build,outer->info.build,12u))return false;
    size_t native_at=kui_smash_native_menu_seed_find(child->data,child_bytes);
    size_t child_return_at=kui_smash_menu_return_seed_find(child->data,child_bytes);
    size_t loader_at=kui_smash_menu_loader_seed_find(outer->data,outer_bytes);
    size_t outer_return_at=kui_smash_menu_return_seed_find(outer->data,outer_bytes);
    if(native_at==SIZE_MAX || child_return_at==SIZE_MAX || loader_at==SIZE_MAX || outer_return_at==SIZE_MAX ||
       !smash_menu_capsule_seeds_disjoint(native_at,KUI_SMASH_NATIVE_MENU_SEED_BYTES,
            child_return_at,KUI_SMASH_MENU_RETURN_SEED_BYTES) ||
       !smash_menu_capsule_seeds_disjoint(loader_at,KUI_SMASH_MENU_LOADER_SEED_BYTES,
            outer_return_at,KUI_SMASH_MENU_RETURN_SEED_BYTES))return false;
    struct kui_smash_menu_capsule_runs old_outer={0};
    KUI_SMASH_TIME_SPAN(inspect_time,KUI_SMASH_UI_CAPSULE_INSPECT);
    int existing=smash_menu_capsule_file_inspect(KUI_SMASH_MENU_OUTER_CAPSULE_PATH,fs,volume,
        map,profile,&old_outer,problem,cancel);
    KUI_SMASH_TIME_END(inspect_time);
    if(existing<0 || cancel())return false;
    uint32_t child_padded=(child_bytes+511u)&~511u,outer_padded=(outer_bytes+511u)&~511u;
    uint32_t capacity=child_padded>outer_padded?child_padded:outer_padded;
    uint8_t *wire=calloc(1u,capacity);bool ok=false;
    if(!wire) {*problem="not enough memory for the Smash Pack capsule pair";return false;}
    memcpy(wire,child->data,child_bytes);
    uint32_t child_crc=kui_retail_crc32(0u,wire,child_padded);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_CHILD_CAPSULE_BYTES,child_padded);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_CHILD_CAPSULE_CRC32,child_crc);
    struct kui_smash_menu_capsule_prepared native={0},menu={0};
    KUI_SMASH_TIME_SPAN(child_time,KUI_SMASH_UI_CAPSULE_CHILD);
    bool child_ready=smash_menu_capsule_file_prepare(fs,volume,map,profile,KUI_SMASH_MENU_CAPSULE_PATH,
        wire,child_padded,page,existing?&old_outer:NULL,&native,problem,log,cancel);
    KUI_SMASH_TIME_END(child_time);
    if(!child_ready)goto done;
    native.payload_bytes=child_bytes;native.bytes=child_padded;native.crc32=child_crc;
    native.seed_offset=(uint32_t)native_at;
    if(!smash_native_menu_seed_prepare(outer,&native,problem))goto done;
    memset(wire,0u,outer_padded);memcpy(wire,outer->data,outer_bytes);
    uint32_t outer_crc=kui_retail_crc32(0u,wire,outer_padded);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_OUTER_CAPSULE_BYTES,outer_padded);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_OUTER_CAPSULE_CRC32,outer_crc);
    KUI_SMASH_TIME_SPAN(outer_time,KUI_SMASH_UI_CAPSULE_OUTER);
    bool outer_ready=smash_menu_capsule_file_prepare(fs,volume,map,profile,KUI_SMASH_MENU_OUTER_CAPSULE_PATH,
        wire,outer_padded,page,&native.allocated_runs,&menu,problem,log,cancel);
    KUI_SMASH_TIME_END(outer_time);
    if(!outer_ready)goto done;
    menu.payload_bytes=outer_bytes;menu.bytes=outer_padded;menu.crc32=outer_crc;
    menu.seed_offset=(uint32_t)outer_return_at;
    struct kui_smash_menu_loader_seed plan;
    struct kui_smash_menu_return_seed configured;
    *problem="Smash Pack outer capsule return plan could not be sealed";
    if(cancel() || !smash_menu_capsule_plan(&menu,&plan) ||
       !kui_smash_menu_return_seed_configure(&configured,&plan))goto done;
    memcpy((uint8_t *)outer->data+outer_return_at,&configured,sizeof(configured));
    log("Smash Pack: checked native and outer capsules (%u/%u bytes); shared A1 card and fresh menu return prepared",
        child_padded,outer_padded);
    ok=true;
done:
    free(wire);return ok;
}
