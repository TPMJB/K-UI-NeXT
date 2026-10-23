/* SPDX-License-Identifier: GPL-3.0-only */
/* Temporary high-stage preparation using the established live ownership and
 * recovery pipeline. Only the compact sealed handoff is copied to the worker. */
#include "kui/vmu_persist_stage.h"
#include "kui/vmu_backing.h"
#include "../dreamcast/sci_sd_storage.h"
#include "sci_sd_bus.h"
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
static int native_persist_identity(void *context,struct kui_vmu_persist_session_identity *out) {
    struct kui_retail_storage *storage=context;struct kui_vmu_sd_identity id;
    if(kui_vmu_sd_identity_read(&storage->device.sd,&id)!=KUI_VMU_BACKING_OK) return -1;
    memcpy(out->cid,id.cid,16u);out->card_sectors=id.sectors;out->high_capacity=id.high_capacity;return 0;
}
static bool native_persist_healthy(void *context) {
    struct kui_retail_storage *storage=context;
    return storage->transport==KUI_STORAGE_SCI && storage->device.sd.ready &&
        !storage->stream.active && kui_sci_sd_healthy() && !kui_sci_sd_faulted();
}
static int native_persist_read(void *context,uint64_t lba,void *out) {
    struct kui_retail_storage *storage=context;if(lba>UINT32_MAX) return -1;
    last_card_result=kui_loader_sd_read(&storage->device.sd,(uint32_t)lba,1u,out);
    return last_card_result==KUI_LOADER_SD_OK?0:-1;
}
static enum kui_loader_sd_result __attribute__((noipa)) native_persist_pio_write(
    struct kui_loader_sd *sd,uint32_t lba,const void *in) {
    bool (*block)(void *,const uint8_t *,uint8_t *,size_t,bool,uint16_t *)=sd->bus.transfer_block;
    sd->bus.transfer_block=NULL;
    enum kui_loader_sd_result result=kui_sci_sd_write(sd,lba,1u,in);
    sd->bus.transfer_block=block;return result;
}
static int native_persist_write(void *context,uint64_t lba,const void *in) {
    struct kui_retail_storage *storage=context;if(lba>UINT32_MAX) return -1;
    last_card_result=native_persist_pio_write(&storage->device.sd,(uint32_t)lba,in);
    return last_card_result==KUI_LOADER_SD_OK?0:-1;
}
static int native_persist_sync(void *context) {
    struct kui_retail_storage *storage=context;last_card_result=kui_sci_sd_sync(&storage->device.sd);
    return last_card_result==KUI_LOADER_SD_OK?0:-1;
}
static enum kui_loader_sd_result __attribute__((noipa)) native_persist_storage_init(struct kui_retail_storage *storage,uint32_t selected) {
    /* Control never decodes or acquires the prepared pair, recovers a journal,
     * writes a checkpoint or publishes a durable handoff. Only ordinary game
     * storage is initialized. A malformed mode stops before this decision. */
    if(native_runtime_mode()==KUI_VMU_NATIVE_MODE_CONTROL)
        return kui_retail_storage_init(storage,selected);
    size_t bytes=(size_t)(native_persist_wire_end-native_persist_wire);
    if(native_persist_ready || bytes!=KUI_VMU_PERSIST_PROFILE_WIRE_BYTES ||
       kui_vmu_persist_profile_decode(&native_persist_profile,native_persist_wire,bytes) ||
       kui_vmu_persist_stage_admit(&native_persist_profile,&manifest))
        stopped("NATIVE VMU PREPARED PROFILE / GAME MAP REFUSED",(uint32_t)bytes);
    enum kui_loader_sd_result result=kui_retail_storage_init(storage,selected);
    if(result!=KUI_LOADER_SD_OK) return result;
    if(selected!=KUI_STORAGE_SCI || storage->transport!=KUI_STORAGE_SCI || storage->stream.active)
        stopped("NATIVE VMU REQUIRES STANDARD SCI STORAGE",selected);
    result=kui_retail_storage_acquire(storage);if(result!=KUI_LOADER_SD_OK) return result;
    retail_display_line("NATIVE VMU: LIVE OWNERSHIP / RECOVERY - KEEP POWER ON");
    const struct kui_vmu_persist_session_ops ops={storage,native_persist_identity,
        native_persist_read,native_persist_write,native_persist_sync,native_persist_healthy};
    int prepared=kui_vmu_persist_stage_prepare(&native_persist_profile,&manifest,&ops,
        &native_persist_work,&native_persist_handoff);
    enum kui_loader_sd_result stopped_stream=kui_retail_storage_stop(storage);
    bool healthy=native_persist_healthy(storage);kui_retail_storage_release(storage);
    healthy=healthy && storage->device.sd.ready && !storage->stream.active && !kui_sci_sd_faulted();
    if(prepared || stopped_stream!=KUI_LOADER_SD_OK || !healthy) {
        memset(&native_persist_handoff,0,sizeof(native_persist_handoff));
        kui_vmu_persist_session_poison(&native_persist_work.session);
        retail_display_hex("DURABLE STAGE PHASE",native_persist_work.phase);
        retail_display_hex("SESSION RESULT",(uint32_t)native_persist_work.session.last_result);
        retail_display_hex("OWNERSHIP RESULT",(uint32_t)native_persist_work.filesystem.stats.result);
        retail_display_hex("OWNERSHIP DETAIL",native_persist_work.filesystem.stats.detail);
        stopped("NATIVE VMU OWNERSHIP / RECOVERY / RELEASE FAILED",(uint32_t)prepared);
    }
    native_persist_ready=true;return KUI_LOADER_SD_OK;
}
