/* SPDX-License-Identifier: GPL-3.0-only */
/* Included only by the explicitly enabled high stage after its ordinary
 * storage and stopped() definitions. It never survives as runtime state. */
#include "kui/armada_ce_vmu_persist_stage.h"
#include "kui/vmu_backing.h"
#include "../dreamcast/sci_sd_storage.h"
#include "sci_sd_bus.h"
_Static_assert(sizeof(struct kui_vmu_durable_handoff)==KUI_ARMADA_CE_VMU_HANDOFF_BYTES,
    "Retained CE durable handoff layout");
_Static_assert(KUI_ARMADA_CE_VMU_SERVICES_P2==KUI_ARMADA_CE_VMU_SERVICES_ADDRESS &&
    sizeof(struct kui_armada_ce_vmu_runtime_services)==KUI_ARMADA_CE_VMU_SERVICES_SIZE,
    "Retained CE resident service layout");
#ifndef KUI_VMU_SD_PROFILE_BIN
#error "Durable CE VMU requires an explicit prepared P2 card profile"
#endif
__asm__(".section .rodata.vmu_sd_profile,\"a\"\n.balign 4\n"
        ".global __vmu_sd_profile_blob_start\n__vmu_sd_profile_blob_start:\n"
        ".incbin \"" KUI_VMU_SD_PROFILE_BIN "\"\n"
        ".global __vmu_sd_profile_blob_end\n__vmu_sd_profile_blob_end:\n.previous\n");
extern const uint8_t armada_persist_wire[] __asm__("__vmu_sd_profile_blob_start");
extern const uint8_t armada_persist_wire_end[] __asm__("__vmu_sd_profile_blob_end");
static struct kui_vmu_persist_profile armada_persist_profile;
static struct kui_armada_ce_vmu_persist_stage_workspace armada_persist_work;
static struct kui_vmu_durable_handoff armada_persist_handoff;
static struct kui_armada_ce_vmu_runtime_services armada_persist_services;
static uint32_t armada_persist_ready;
static int armada_persist_identity(void *context,struct kui_vmu_persist_session_identity *out) {
    struct kui_retail_storage *storage=context;struct kui_vmu_sd_identity id;
    if(kui_vmu_sd_identity_read(&storage->device.sd,&id)!=KUI_VMU_BACKING_OK) return -1;
    memcpy(out->cid,id.cid,16u);out->card_sectors=id.sectors;out->high_capacity=id.high_capacity;
    return 0;
}
static bool armada_persist_healthy(void *context) {
    struct kui_retail_storage *storage=context;
    return storage->transport==KUI_STORAGE_SCI && storage->device.sd.ready &&
        !storage->stream.active && kui_sci_sd_healthy() && !kui_sci_sd_faulted();
}
static int armada_persist_read(void *context,uint64_t lba,void *out) {
    struct kui_retail_storage *storage=context;
    if(lba>UINT32_MAX) return -1;
    last_card_result=kui_loader_sd_read(&storage->device.sd,(uint32_t)lba,1u,out);
    return last_card_result==KUI_LOADER_SD_OK?0:-1;
}
static int armada_persist_write(void *context,uint64_t lba,const void *in) {
    struct kui_retail_storage *storage=context;
    if(lba>UINT32_MAX) return -1;
    bool (*block)(void *,const uint8_t *,uint8_t *,size_t,bool,uint16_t *)=storage->device.sd.bus.transfer_block;
    storage->device.sd.bus.transfer_block=NULL;
    last_card_result=kui_sci_sd_write(&storage->device.sd,(uint32_t)lba,1u,in);
    storage->device.sd.bus.transfer_block=block;
    return last_card_result==KUI_LOADER_SD_OK?0:-1;
}
static int armada_persist_sync(void *context) {
    struct kui_retail_storage *storage=context;
    last_card_result=kui_sci_sd_sync(&storage->device.sd);
    return last_card_result==KUI_LOADER_SD_OK?0:-1;
}
static void armada_persist_prepare(void) {
    size_t bytes=(size_t)(armada_persist_wire_end-armada_persist_wire);
    if(armada_persist_ready || bytes!=KUI_VMU_PERSIST_PROFILE_WIRE_BYTES ||
       kui_vmu_persist_profile_decode(&armada_persist_profile,armada_persist_wire,bytes))
        stopped("CE VMU PREPARED CARD PROFILE REFUSED",(uint32_t)bytes);
    const bool stream=
#if KUI_ARMADA_CE_STREAM
        true;
#else
        false;
#endif
    if(resident_blob!=__retail_resident_scia_blob_start ||
       kui_armada_ce_vmu_persist_services_prepare(resident_blob,resident_bytes,
           stream,&armada_persist_services))
        stopped("CE VMU RESIDENT SERVICE DESCRIPTOR REFUSED",(uint32_t)resident_bytes);
    if(card.transport!=KUI_STORAGE_SCI || card.stream.active ||
       kui_retail_storage_acquire(&card)!=KUI_LOADER_SD_OK)
        stopped("CE VMU PREPARATION BUS UNAVAILABLE",card.transport);
    const struct kui_vmu_persist_session_ops ops={&card,armada_persist_identity,
        armada_persist_read,armada_persist_write,armada_persist_sync,armada_persist_healthy};
    int prepared=kui_armada_ce_vmu_persist_stage_prepare(&armada_persist_profile,&manifest,
        &ops,&armada_persist_work,&armada_persist_handoff);
    enum kui_loader_sd_result stopped_stream=kui_retail_storage_stop(&card);
    bool healthy=armada_persist_healthy(&card);
    kui_retail_storage_release(&card);
    healthy=healthy && card.device.sd.ready && !card.stream.active && !kui_sci_sd_faulted();
    if(prepared || stopped_stream!=KUI_LOADER_SD_OK || !healthy) {
        memset(&armada_persist_handoff,0,sizeof(armada_persist_handoff));
        kui_vmu_persist_session_poison(&armada_persist_work.core.session);
        stopped("CE VMU OWNERSHIP / RECOVERY / RELEASE FAILED",(uint32_t)prepared);
    }
    armada_persist_ready=1u;
}
