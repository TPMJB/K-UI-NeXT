/* SPDX-License-Identifier: GPL-3.0-only */
/* Temporary high-stage preparation using the established live ownership and
 * recovery pipeline. Only the compact sealed handoff is copied to the worker. */
#include "kui/vmu_persist_stage.h"
#include "kui/vmu_backing.h"
#include "../dreamcast/sci_sd_storage.h"
#include "sci_sd_bus.h"
#if KUI_NATIVE_VMU_ONLY
#include "vmu_native_transport.h"
#endif
#ifndef KUI_VMU_SD_PROFILE_BIN
#error Native persistence requires an explicitly prepared per-card profile
#endif
__asm__(".section .rodata.vmu_sd_profile,\"a\"\n.balign 4\n"
    ".global __vmu_sd_profile_blob_start\n__vmu_sd_profile_blob_start:\n"
    ".incbin \"" KUI_VMU_SD_PROFILE_BIN "\"\n"
    ".global __vmu_sd_profile_blob_end\n__vmu_sd_profile_blob_end:\n.previous\n");
extern const uint8_t native_persist_wire[] __asm__("__vmu_sd_profile_blob_start");
extern const uint8_t native_persist_wire_end[] __asm__("__vmu_sd_profile_blob_end");
static struct kui_vmu_persist_profile native_persist_profile;
static struct kui_vmu_persist_stage_workspace native_persist_work;
static struct kui_vmu_durable_handoff native_persist_handoff;
static bool native_persist_ready;
#include "kui/smash_fat_read_cache.h"
#include "kui/vc2_ce_vmu_stage_cache.h"
/* This fresh two-window scanner is shared by every persistent image stage.
 * Its historical API names do not select a title or a native SDK profile. */
__asm__(".global __kui_native_fat_read_cache\n.set __kui_native_fat_read_cache,1\n");
#if KUI_SMASH_NATIVE_MENU
__asm__(".global __kui_smash_native_fat_read_cache\n.set __kui_smash_native_fat_read_cache,1\n");
#endif
static struct kui_smash_fat_read_cache smash_fat_read_cache;
/* Temporary cold-stage scratch. It does not enlarge the resident worker. */
static struct kui_vc2_ce_vmu_stage_cache native_persist_read_cache;
static int native_persist_identity(void *context,struct kui_vmu_persist_session_identity *out) {
    struct kui_retail_storage *storage=context;struct kui_vmu_sd_identity id;
    kui_smash_fat_cache_begin_epoch(&smash_fat_read_cache);
    kui_vc2_ce_vmu_stage_cache_invalidate(&native_persist_read_cache);
    if(kui_vmu_sd_identity_read(&storage->device.sd,&id)!=KUI_VMU_BACKING_OK) return -1;
    memcpy(out->cid,id.cid,16u);out->card_sectors=id.sectors;out->high_capacity=id.high_capacity;return 0;
}
static bool native_persist_healthy(void *context) {
    struct kui_retail_storage *storage=context;
    return
#if KUI_NATIVE_VMU_ONLY
        kui_native_vmu_transport_supported(storage->transport) && storage->device.sd.ready &&
        !storage->stream.active && kui_native_vmu_transport_healthy(storage->transport) &&
        !kui_native_vmu_transport_faulted(storage->transport);
#else
        storage->transport==KUI_STORAGE_SCI && storage->device.sd.ready &&
        !storage->stream.active && kui_sci_sd_healthy() && !kui_sci_sd_faulted();
#endif
}
static int native_persist_read_run(void *context,uint32_t lba,uint32_t count,void *out) {
    struct kui_retail_storage *storage=context;
    if(!count || count>KUI_VC2_STAGE_READ_BLOCKS || !native_persist_healthy(storage))return -1;
    if(count==1u) {
#if KUI_SMASH_PHASE_DIAGNOSTIC
        ++smash_phase_io[1];++smash_phase_io[3];
#endif
        last_card_result=kui_loader_sd_read(&storage->device.sd,lba,1u,out);
        return last_card_result==KUI_LOADER_SD_OK && native_persist_healthy(storage)?0:-1;
    }
#if KUI_SMASH_PHASE_DIAGNOSTIC
    ++smash_phase_io[2];
#endif
    enum kui_loader_sd_result result=KUI_LOADER_SD_OK;
    for(uint32_t i=0u;i<count;i++) {
        result=kui_retail_storage_read_run(storage,lba+i,count-i,(uint8_t *)out+i*512u);
        if(result!=KUI_LOADER_SD_OK)break;
#if KUI_SMASH_PHASE_DIAGNOSTIC
        ++smash_phase_io[3];
#endif
    }
    enum kui_loader_sd_result stopped=kui_retail_storage_stop(storage);
    last_card_result=stopped!=KUI_LOADER_SD_OK?stopped:result;
    return last_card_result==KUI_LOADER_SD_OK && native_persist_healthy(storage)?0:-1;
}
static int native_persist_fat_read_one(void *context,uint32_t lba,uint8_t out[512]) {
    return kui_vc2_ce_vmu_stage_cache_metadata_read(&native_persist_read_cache,&native_persist_profile,
        native_persist_work.session.phase==KUI_VMU_PERSIST_SESSION_ADMISSION,
        lba,out,native_persist_read_run,context);
}
static int native_persist_fat_read_window(void *context,uint32_t lba,uint32_t count,
    uint8_t *out,uint32_t *verified) {
    struct kui_retail_storage *storage=context;*verified=0u;
    if(!count || count>KUI_SMASH_FAT_CACHE_WINDOW_BLOCKS || !native_persist_healthy(storage))return -1;
#if KUI_SMASH_PHASE_DIAGNOSTIC
    ++smash_phase_io[2];
#endif
    enum kui_loader_sd_result result=KUI_LOADER_SD_OK;
    for(uint32_t i=0;i<count;i++) {
        result=kui_retail_storage_read_run(storage,lba+i,count-i,out+i*512u);
        if(result!=KUI_LOADER_SD_OK)break;
        ++*verified;
#if KUI_SMASH_PHASE_DIAGNOSTIC
        ++smash_phase_io[3];
#endif
    }
    enum kui_loader_sd_result stopped=kui_retail_storage_stop(storage);
    if(stopped!=KUI_LOADER_SD_OK || !native_persist_healthy(storage)) {
        *verified=0u;last_card_result=stopped!=KUI_LOADER_SD_OK?stopped:KUI_LOADER_SD_TIMEOUT;
        return -1;
    }
    if(*verified && native_persist_work.session.phase==KUI_VMU_PERSIST_SESSION_ADMISSION &&
       kui_vc2_ce_vmu_stage_cache_admission_store(&native_persist_read_cache,
           &native_persist_profile,lba,*verified,out)) {
        *verified=0u;return -1;
    }
    /* A CRC-verified prefix remains useful if a later, unrequested block
     * failed. Never serve that suffix, retry the requested first block, or
     * hide an uncertain stop. This retains the one-sector caller's answer. */
    last_card_result=*verified?KUI_LOADER_SD_OK:result;
    return *verified?0:-1;
}
static int native_persist_read(void *context,uint64_t lba,void *out) {
#if KUI_SMASH_PHASE_DIAGNOSTIC
    ++smash_phase_io[0];
#endif
    struct kui_retail_storage *storage=context;if(lba>UINT32_MAX) return -1;
    if(native_persist_work.session.phase==KUI_VMU_PERSIST_SESSION_ADMISSION) {
        int hit=kui_vc2_ce_vmu_stage_cache_admission_peek(&native_persist_read_cache,
            &native_persist_profile,lba,out);
        if(hit)return hit>0?0:-1;
    }
    if(native_persist_work.session.phase==KUI_VMU_PERSIST_SESSION_VALIDATION)
        return kui_vc2_ce_vmu_stage_cache_read(&native_persist_read_cache,&native_persist_profile,
            false,true,lba,out,native_persist_read_run,context);
    const struct kui_smash_fat_read_ops ops={storage,native_persist_fat_read_one,native_persist_fat_read_window};
    return kui_smash_fat_cache_read(&smash_fat_read_cache,&ops,(uint32_t)lba,out);
}
static enum kui_loader_sd_result __attribute__((noipa)) native_persist_pio_write(
    struct kui_loader_sd *sd,uint32_t lba,const void *in) {
    bool (*block)(void *,const uint8_t *,uint8_t *,size_t,bool,uint16_t *)=sd->bus.transfer_block;
    sd->bus.transfer_block=NULL;
    enum kui_loader_sd_result result=kui_sci_sd_write(sd,lba,1u,in);
    sd->bus.transfer_block=block;return result;
}
static int native_persist_write(void *context,uint64_t lba,const void *in) {
#if KUI_SMASH_PHASE_DIAGNOSTIC
    ++smash_phase_io[4];
#endif
    kui_smash_fat_cache_begin_epoch(&smash_fat_read_cache);
    kui_vc2_ce_vmu_stage_cache_invalidate(&native_persist_read_cache);
    struct kui_retail_storage *storage=context;if(lba>UINT32_MAX) return -1;
    last_card_result=native_persist_pio_write(&storage->device.sd,(uint32_t)lba,in);
    return last_card_result==KUI_LOADER_SD_OK?0:-1;
}
static int native_persist_sync(void *context) {
#if KUI_SMASH_PHASE_DIAGNOSTIC
    ++smash_phase_io[5];
#endif
    kui_smash_fat_cache_begin_epoch(&smash_fat_read_cache);
    kui_vc2_ce_vmu_stage_cache_invalidate(&native_persist_read_cache);
    struct kui_retail_storage *storage=context;last_card_result=kui_sci_sd_sync(&storage->device.sd);
    return last_card_result==KUI_LOADER_SD_OK?0:-1;
}
static enum kui_loader_sd_result __attribute__((noipa)) native_persist_storage_init(struct kui_retail_storage *storage,uint32_t selected) {
#if KUI_NATIVE_VMU_ONLY && defined(KUI_NATIVE_VMU_ONLY_TRANSPORT)
    /* The high stage can reach both buses; its selected low resident cannot.
     * Reject a mismatched test variant before decoding or recovering a card. */
    if(selected!=KUI_NATIVE_VMU_ONLY_TRANSPORT)
        stopped("VMU ONLY TRANSPORT DOES NOT MATCH",selected);
#endif
    /* Control never decodes or acquires the prepared pair, recovers a journal,
     * writes a checkpoint or publishes a durable handoff. Only ordinary game
     * storage is initialized. A malformed mode stops before this decision. */
    if(native_runtime_mode()==KUI_VMU_NATIVE_MODE_CONTROL ||
       native_runtime_mode()==KUI_VMU_NATIVE_MODE_READER_ONLY)
        return kui_retail_storage_init(storage,selected);
    size_t bytes=(size_t)(native_persist_wire_end-native_persist_wire);
    if(native_persist_ready || bytes!=KUI_VMU_PERSIST_PROFILE_WIRE_BYTES ||
       kui_vmu_persist_profile_decode(&native_persist_profile,native_persist_wire,bytes) ||
       kui_vmu_persist_stage_admit(&native_persist_profile,&manifest))
        stopped("NATIVE VMU PREPARED PROFILE / GAME MAP REFUSED",(uint32_t)bytes);
    enum kui_loader_sd_result result=kui_retail_storage_init(storage,selected);
    if(result!=KUI_LOADER_SD_OK) return result;
#if KUI_SMASH_PHASE_DIAGNOSTIC
    kui_smash_phase_stage_mark(KUI_SMASH_PHASE_CARD_READY);
#endif
    if(
#if KUI_NATIVE_VMU_ONLY
       !kui_native_vmu_transport_supported(selected) || storage->transport!=selected || storage->stream.active)
        stopped("NATIVE VMU REQUIRES STANDARD SD STORAGE",selected);
#else
       selected!=KUI_STORAGE_SCI || storage->transport!=KUI_STORAGE_SCI || storage->stream.active)
        stopped("NATIVE VMU REQUIRES STANDARD SCI STORAGE",selected);
#endif
    if(!kui_smash_fat_cache_configure(&smash_fat_read_cache,storage->device.sd.blocks,
       native_persist_profile.partition_first,native_persist_profile.partition_end))
        stopped("NATIVE VMU FRESH FAT CACHE BOUNDS REFUSED",0u);
    result=kui_retail_storage_acquire(storage);if(result!=KUI_LOADER_SD_OK) return result;
    retail_display_line("NATIVE VMU: LIVE OWNERSHIP / RECOVERY - KEEP POWER ON");
    const struct kui_vmu_persist_session_ops ops={storage,native_persist_identity,
        native_persist_read,native_persist_write,native_persist_sync,native_persist_healthy};
    int prepared=
#if KUI_SMASH_CARD_SESSION
        smash_card_session_prepare(&native_persist_profile,&manifest,&ops,
#else
        kui_vmu_persist_stage_prepare(&native_persist_profile,&manifest,&ops,
#endif
        &native_persist_work,&native_persist_handoff);
    enum kui_loader_sd_result stopped_stream=kui_retail_storage_stop(storage);
    bool healthy=native_persist_healthy(storage);kui_retail_storage_release(storage);
    healthy=healthy && storage->device.sd.ready && !storage->stream.active &&
#if KUI_NATIVE_VMU_ONLY
        !kui_native_vmu_transport_faulted(storage->transport);
#else
        !kui_sci_sd_faulted();
#endif
    if(prepared || stopped_stream!=KUI_LOADER_SD_OK || !healthy) {
        /* Capture the original failure before poisoning a previously healthy
         * session. Start a fresh bounded page: the preceding launch messages
         * must not wrap over the ownership and boot-sector evidence. */
        const struct kui_vmu_fs_stats *stats=&native_persist_work.filesystem.stats;
        const struct kui_vmu_fs_boot_diagnostics *boot=&stats->boot;
        uint32_t values[5]={(uint32_t)prepared,(uint32_t)native_persist_work.session.last_result,
            (uint32_t)stats->result,stats->detail,native_persist_work.phase};
        memset(&native_persist_handoff,0,sizeof(native_persist_handoff));
        kui_vmu_persist_session_poison(&native_persist_work.session);
        retail_display_restore(&display);
        retail_display_line("NATIVE VMU PREPARATION FAILED");
        retail_display_values("PREPARE  SESSION  FS       DETAIL   PHASE",values,5u);
        values[0]=(uint32_t)stopped_stream;values[1]=(uint32_t)healthy;
        values[2]=boot->reject_mask;values[3]=boot->flags;values[4]=boot->valid;
        retail_display_values("STOP     HEALTH   REJECT   FLAGS    VALID",values,5u);
        values[0]=boot->zero_or;values[1]=boot->revision;values[2]=boot->sector_shift;
        values[3]=boot->cluster_shift;values[4]=boot->fat_count;
        retail_display_values("ZERO     REV      SECTOR   CLUSTER  FATS",values,5u);
        values[0]=(uint32_t)boot->partition_offset;values[1]=(uint32_t)(boot->partition_offset>>32);
        values[2]=(uint32_t)native_persist_profile.partition_first;
        values[3]=(uint32_t)(native_persist_profile.partition_first>>32);
        retail_display_values("PARTLO   PARTHI   EXPLO    EXPHI",values,4u);
        uint64_t expected_sectors=native_persist_profile.partition_end-native_persist_profile.partition_first;
        values[0]=(uint32_t)boot->volume_sectors;values[1]=(uint32_t)(boot->volume_sectors>>32);
        values[2]=(uint32_t)expected_sectors;values[3]=(uint32_t)(expected_sectors>>32);
        retail_display_values("VOLUMELO VOLUMEHI EXPLO    EXPHI",values,4u);
        retail_display_line("PHOTO THIS SCREEN THEN POWER OFF");
        retail_display_line("PREPARED VMU MAY REQUIRE JOURNAL RECOVERY");
        for(;;) __asm__ volatile("nop");
    }
    native_persist_ready=true;return KUI_LOADER_SD_OK;
}
