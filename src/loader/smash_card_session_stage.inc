/* SPDX-License-Identifier: GPL-3.0-only */
/* Allocation authority belongs to a collection epoch, not to an SD bus
 * acquisition or a started backend. All transitions are authenticated raw
 * readers; only BASE/JOURNAL or bounded preallocated data may be written.
 * Old owners must drain/retire before transferring the v2 RAM-only receipt.
 * Each new backend independently recovers current saves under fresh CID fences.
 * No on-card witness, backend pointer or old content CRC snapshot grants this
 * authority. GUI/reset/unknown writers/faults end the epoch before reinit. */
#include "kui/smash_card_session.h"
#include "kui/vmu_persist_stage.h"
#include "kui/vmu_backing.h"
#include "kui/hash.h"
#include "../dreamcast/sci_sd_storage.h"
#include "sci_sd_bus.h"
#if KUI_SMASH_MENU_LOADER
#include "kui/smash_menu_return.h"
#endif
__asm__(".section .rodata.smash_card_session_config,\"a\"\n.balign 4\n"
    ".global __smash_card_session_config\n__smash_card_session_config:\n"
    ".ascii \"KUISCF01\"\n.long 1,4224\n.zero 4208\n.previous\n");
extern const struct kui_smash_card_config smash_card_config_seed __asm__("__smash_card_session_config");
static struct kui_smash_card_config smash_card_config;
static struct kui_smash_card_receipt smash_card_receipt;
static struct kui_vmu_fs_witness_binding smash_card_binding;
static uint32_t smash_card_imported;
static uint32_t smash_card_full_admissions __attribute__((unused));
static uint32_t smash_card_attaches __attribute__((unused));
static uint32_t smash_card_boot2_calls __attribute__((unused));
static unsigned smash_card_role(void) {
#if KUI_SMASH_MENU_LOADER
    return KUI_SMASH_CARD_ROLE_OUTER;
#elif KUI_SMASH_NATIVE_MENU
    return KUI_SMASH_CARD_ROLE_NATIVE;
#else
    return KUI_SMASH_CARD_ROLE_VC2+KUI_SMASH_SWIRL_VMU;
#endif
}
#if KUI_SMASH_PHASE_TEXT && KUI_SMASH_NATIVE_MENU
#include "kui/smash_phase_text.h"
static void smash_card_session_phase_stats(struct kui_smash_phase_session_stats *out) {
    memset(out,0,sizeof(*out));
    out->full_admissions=smash_card_full_admissions;out->attaches=smash_card_attaches;
    out->receipt_generation=smash_card_receipt.generation;out->receipt_sequence=smash_card_receipt.sequence;
    out->receipt_valid=kui_smash_card_receipt_valid(&smash_card_receipt);
    out->receipt_blank=kui_smash_card_receipt_blank(&smash_card_receipt);
    out->allocation_epoch=smash_card_attaches!=0u;
#if KUI_SMASH_NATIVE_GIN_ADOPT
    out->native_gin_route=1u;
#endif
    out->boot2_calls=smash_card_boot2_calls;
    /* There is no child metadata replay/file read in the allocation epoch.
     * Unobserved SD timing stays NA, never a fabricated zero measurement. */
}
#endif
static void smash_card_session_import(const uint8_t *wire) {
    uintptr_t base=(uintptr_t)wire-KUI_RETAIL_MAP_OFFSET;
    if(smash_card_imported || (base!=UINT32_C(0x8c010000) && base!=UINT32_C(0x8cc00000)))
        stopped("SMASH CARD SESSION INPUT REFUSED",(uint32_t)base);
    memcpy(&smash_card_config,&smash_card_config_seed,sizeof(smash_card_config));
    memcpy(&smash_card_receipt,(const void *)(base+KUI_SMASH_CARD_RECEIPT_OFFSET),sizeof(smash_card_receipt));
    unsigned role=smash_card_role();
    bool transition=kui_smash_card_receipt_transition_valid(&smash_card_receipt,role);
#if KUI_SMASH_MENU_LOADER
    transition=transition || kui_smash_card_receipt_ce_return_valid(&smash_card_receipt);
#endif
    if(!kui_smash_card_config_valid(&smash_card_config) || smash_card_config.role!=role ||
       memcmp(smash_card_config.build,KUI_BUILD_ID,12u) ||
       (!kui_smash_card_receipt_blank(&smash_card_receipt) &&
        !transition))
        stopped("SMASH ALLOCATION EPOCH CONFIG REFUSED",role);
    if(kui_smash_card_receipt_blank(&smash_card_receipt) && role!=KUI_SMASH_CARD_ROLE_OUTER)
        stopped("SMASH CHILD NEEDS LIVE ALLOCATION EPOCH",role);
#if KUI_SMASH_MENU_LOADER
    /* An epoch-bearing outer is reachable only through a checked terminal
     * return capsule. Initial GUI launch supplies a configured, uncaptured
     * return seed and a blank receipt. No old/on-disk receipt is imported. */
    if(!kui_smash_card_receipt_blank(&smash_card_receipt)) {
        extern const struct kui_smash_menu_return_seed smash_epoch_return_seed
            __asm__("__smash_menu_return_seed");
        if(!kui_smash_menu_return_seed_valid(&smash_epoch_return_seed) ||
           smash_epoch_return_seed.flags!=KUI_SMASH_MENU_RETURN_CONTEXT)
            stopped("SMASH EPOCH NEEDS TERMINAL NATIVE RETURN",0u);
    }
#endif
    /* This validation precedes storage initialization/acquisition, which may
     * clear its NEW local fault latch. Prior health was certified by the old
     * raw gate/worker retirement BEFORE the authenticated receipt moved. */
    smash_card_imported=1u;
}
#if !KUI_SMASH_MENU_LOADER
static struct kui_vmu_persist_allocation_transfer smash_card_transfer;
static int smash_card_session_prepare(const struct kui_vmu_persist_profile *p,
    const struct kui_retail_manifest *m,const struct kui_vmu_persist_session_ops *ops,
    struct kui_vmu_persist_stage_workspace *work,struct kui_vmu_durable_handoff *out) {
    unsigned role=smash_card_role();
    if(!smash_card_imported || !kui_smash_card_binding(&smash_card_config,p,&smash_card_binding) ||
       !kui_smash_card_receipt_matches(&smash_card_receipt,&smash_card_binding) ||
       !kui_smash_card_receipt_transition_valid(&smash_card_receipt,role))
        return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    memset(&smash_card_transfer,0,sizeof(smash_card_transfer));
    smash_card_transfer.magic=KUI_VMU_ALLOCATION_TRANSFER_MAGIC;
    smash_card_transfer.version=KUI_VMU_ALLOCATION_TRANSFER_VERSION;
    smash_card_transfer.bytes=sizeof(smash_card_transfer);
    smash_card_transfer.state=KUI_VMU_ALLOCATION_TRANSFER_READY;
    smash_card_transfer.card_sectors=p->card_sectors;
    smash_card_transfer.partition_first=p->partition_first;smash_card_transfer.partition_end=p->partition_end;
    smash_card_transfer.high_capacity=p->high_capacity;
    smash_card_transfer.generation=smash_card_receipt.generation;
    smash_card_transfer.sequence=smash_card_receipt.sequence+1u;
    smash_card_transfer.previous_owner=kui_smash_card_receipt_owner(&smash_card_receipt);
    smash_card_transfer.next_owner=role;
    memcpy(smash_card_transfer.cid,p->cid,16u);memcpy(smash_card_transfer.image_id,p->image_id,16u);
    memcpy(smash_card_transfer.ownership_sha256,smash_card_config.ownership_sha256,32u);
    memcpy(smash_card_transfer.domain_binding_sha256,smash_card_receipt.binding_sha256,32u);
    /* Every owner supplies its serialized bounded raw transport. A new
     * rebind still recovers/validates/hashes CURRENT data; only its immediate
     * marker-only checkpoint may reuse that same live snapshot. */
    int result=kui_vmu_persist_stage_rebind_policy(p,m,&smash_card_transfer,ops,
        &kui_vmu_persist_stage_exclusive_checkpoint,work,out);
    if(result)return result;
    if(smash_card_transfer.state!=KUI_VMU_ALLOCATION_TRANSFER_CONSUMED ||
       !kui_smash_card_receipt_transfer(&smash_card_receipt,role))
        return KUI_VMU_PERSIST_STAGE_HANDOFF;
    ++smash_card_attaches;return 0;
}
#else
#include "kui/smash_menu_return.h"
#include "kui/smash_fat_read_cache.h"
#include "kui/vc2_ce_vmu_stage_cache.h"
static struct kui_vmu_persist_profile smash_card_outer_profile;
static struct kui_vmu_fs_workspace smash_card_fs_work;
static struct kui_vmu_fs_range smash_card_protected[KUI_RETAIL_MANIFEST_SLOTS];
static struct kui_smash_fat_read_cache smash_card_fat_cache;
static struct kui_vc2_ce_vmu_stage_cache smash_card_read_cache;
static bool smash_card_healthy(struct kui_retail_storage *s) {
    return s && s->transport==KUI_STORAGE_SCI && s->device.sd.ready &&
        !s->stream.active && kui_sci_sd_healthy() && !kui_sci_sd_faulted();
}
static bool smash_card_identity_matches(struct kui_retail_storage *s,
    const struct kui_vmu_persist_profile *p) {
    struct kui_vmu_sd_identity id;
    return smash_card_healthy(s) && !kui_vmu_sd_identity_read(&s->device.sd,&id) &&
        smash_card_healthy(s) && id.sectors==p->card_sectors && id.high_capacity==p->high_capacity &&
        !memcmp(id.cid,p->cid,16u);
}
static int smash_card_outer_run(void *context,uint32_t lba,uint32_t count,void *out) {
    struct kui_retail_storage *s=context;
    if(!out || !count || count>32u || !smash_card_healthy(s) ||
       lba>=s->device.sd.blocks || count>s->device.sd.blocks-lba)return -1;
    enum kui_loader_sd_result result=KUI_LOADER_SD_OK;
    if(count==1u)result=kui_loader_sd_read(&s->device.sd,lba,1u,out);
    else for(uint32_t i=0u;i<count;i++) {
        result=kui_retail_storage_read_run(s,lba+i,count-i,(uint8_t *)out+i*512u);
        if(result!=KUI_LOADER_SD_OK)break;
    }
    enum kui_loader_sd_result stop=kui_retail_storage_stop(s);
    return result==KUI_LOADER_SD_OK && stop==KUI_LOADER_SD_OK && smash_card_healthy(s)?0:-1;
}
static int smash_card_outer_one(void *context,uint32_t lba,uint8_t out[512]) {
    return kui_vc2_ce_vmu_stage_cache_metadata_read(&smash_card_read_cache,&smash_card_outer_profile,
        true,lba,out,smash_card_outer_run,context);
}
static int smash_card_outer_window(void *context,uint32_t lba,uint32_t count,uint8_t *out,uint32_t *verified) {
    *verified=0u;if(smash_card_outer_run(context,lba,count,out))return -1;
    *verified=count;return kui_vc2_ce_vmu_stage_cache_admission_store(&smash_card_read_cache,
        &smash_card_outer_profile,lba,count,out);
}
static int smash_card_outer_sector(void *context,uint32_t lba,uint8_t out[512]) {
    int hit=kui_vc2_ce_vmu_stage_cache_admission_peek(&smash_card_read_cache,&smash_card_outer_profile,lba,out);
    if(hit)return hit>0?0:-1;
    const struct kui_smash_fat_read_ops ops={context,smash_card_outer_one,smash_card_outer_window};
    return kui_smash_fat_cache_read(&smash_card_fat_cache,&ops,lba,out);
}
static void smash_card_session_outer_prepare(void) {
    if(!smash_card_imported ||
       kui_vmu_persist_profile_decode(&smash_card_outer_profile,smash_card_config.profile_wire,4096u) ||
       kui_vmu_persist_stage_admit(&smash_card_outer_profile,&manifest) ||
       !kui_smash_card_binding(&smash_card_config,&smash_card_outer_profile,&smash_card_binding))
        stopped("SMASH SHARED CARD PROFILE REFUSED",0u);
    if(kui_retail_storage_acquire(&card)!=KUI_LOADER_SD_OK)
        stopped("SMASH SHARED CARD BUS REFUSED",0u);
    if(!smash_card_identity_matches(&card,&smash_card_outer_profile))
        stopped("SMASH SHARED CARD IDENTITY CHANGED",0u);
    if(!kui_smash_card_receipt_blank(&smash_card_receipt)) {
        if(!kui_smash_card_receipt_matches(&smash_card_receipt,&smash_card_binding) ||
           !(kui_smash_card_receipt_ce_return_valid(&smash_card_receipt)?
               kui_smash_card_receipt_ce_return(&smash_card_receipt):
               kui_smash_card_receipt_transfer(&smash_card_receipt,KUI_SMASH_CARD_ROLE_OUTER)))
            stopped("SMASH NATIVE ALLOCATION TRANSFER REFUSED",0u);
    } else {
        if(!kui_smash_fat_cache_configure(&smash_card_fat_cache,card.device.sd.blocks,
            smash_card_outer_profile.partition_first,smash_card_outer_profile.partition_end))
            stopped("SMASH SHARED CARD ADMISSION REFUSED",0u);
        for(uint32_t i=0u;i<manifest.extent_count;i++) {
            const struct kui_retail_extent *e=&manifest.slots[manifest.track_count+i].extent;
            smash_card_protected[i]=(struct kui_vmu_fs_range){e->card_lba,e->blocks};
        }
        struct kui_vmu_fs_input input={.read_sector=smash_card_outer_sector,.context=&card,
            .card_sectors=smash_card_outer_profile.card_sectors,
            .partition_first=smash_card_outer_profile.partition_first,
            .partition_end=smash_card_outer_profile.partition_end,
            .high_capacity=smash_card_outer_profile.high_capacity,
            .pair_path=smash_card_outer_profile.pair_path,
            .forbidden=smash_card_protected,.forbidden_count=manifest.extent_count};
        memcpy(input.cid,smash_card_outer_profile.cid,16u);memcpy(input.image_id,smash_card_outer_profile.image_id,16u);
        ++smash_card_full_admissions;
        int admitted=kui_vmu_fs_admit(&input,&smash_card_outer_profile,&smash_card_fs_work);
        if(admitted)stopped("SMASH SHARED CARD OWNERSHIP REFUSED",(uint32_t)admitted);
        if(!smash_card_identity_matches(&card,&smash_card_outer_profile))
            stopped("SMASH SHARED CARD CHANGED BEFORE SEAL",0u);
        /* An attestation of this successful live admission, not a digest
         * imported from a prior on-card witness. Replaying host metadata is
         * unnecessary while this exact allocation epoch has no FS writer. */
        static const char domain[]="KUI/SMASH/LIVE-ALLOCATION-ADMISSION/V2";
        struct kui_sha256 hash;uint8_t admission[32];kui_sha256_init(&hash);
        kui_sha256_update(&hash,domain,sizeof(domain));
        kui_sha256_update(&hash,smash_card_binding.binding_sha256,32u);
        kui_sha256_update(&hash,smash_card_config.ownership_sha256,32u);
        kui_sha256_digest(&hash,admission);
        if(!kui_smash_card_receipt_mint(&smash_card_receipt,&smash_card_binding,admission))
            stopped("SMASH LIVE ALLOCATION SEAL REFUSED",0u);
    }
    if(!smash_card_identity_matches(&card,&smash_card_outer_profile) ||
       kui_retail_storage_stop(&card)!=KUI_LOADER_SD_OK || !smash_card_healthy(&card))
        stopped("SMASH SHARED CARD FINAL IDENTITY FAILED",0u);
    kui_retail_storage_release(&card);
    if(!card.device.sd.ready || kui_sci_sd_faulted())stopped("SMASH SHARED CARD RELEASE FAILED",0u);
}
#endif
