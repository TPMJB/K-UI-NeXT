/* SPDX-License-Identifier: GPL-3.0-only */
/* High-stage scratch and device callbacks end before native execution. Only
 * the sealed pointer-free config is late-loaded into the reserved helper. */
#include "kui/vc2_ce_vmu_persist.h"
#include "kui/vmu_persist_stage.h"
#include "kui/vmu_backing.h"
#include "kui/vc2_ce_vmu_stage_cache.h"
#include "kui/vc2_ce_vmu_stage_clock.h"
#include "../dreamcast/sci_sd_storage.h"
#include "sci_sd_bus.h"
#include "retail_display.h"
#if !KUI_VC2_STAGE_OWNERSHIP_PROFILE
#error "VC2 ownership profiling belongs only in the cold standalone stage"
#endif
#ifndef KUI_VC2_PERSIST_PROFILE_BIN
#error "VC2 persistence requires its launch-local prepared profile slot"
#endif
__asm__(".section .rodata.vc2_persist_profile,\"a\"\n.balign 4\n"
        ".global __vc2_persist_profile_blob_start\n__vc2_persist_profile_blob_start:\n"
        ".incbin \"" KUI_VC2_PERSIST_PROFILE_BIN "\"\n"
        ".global __vc2_persist_profile_blob_end\n__vc2_persist_profile_blob_end:\n.previous\n");
extern const uint8_t vc2_persist_wire[] __asm__("__vc2_persist_profile_blob_start");
extern const uint8_t vc2_persist_wire_end[] __asm__("__vc2_persist_profile_blob_end");
static struct kui_vmu_persist_profile vc2_persist_profile;
static struct kui_vmu_persist_stage_workspace vc2_persist_work;
static struct kui_vc2_ce_vmu_persist_config vc2_persist_config;
static uint8_t vc2_persist_check[512];
static uint32_t vc2_persist_started;
static struct kui_vc2_ce_vmu_stage_cache vc2_persist_cache;
static uint32_t vc2_persist_read_phase;
static struct kui_vc2_ce_vmu_stage_clock vc2_persist_clock;
static uint32_t vc2_persist_phase_ticks,vc2_persist_io_ticks;
static uint32_t vc2_persist_status_ticks,vc2_persist_status_runs;
static struct kui_sci_sd_stats vc2_persist_phase_io;
static void vc2_persist_status(uint32_t tick) {
    struct kui_sci_sd_stats now;kui_sci_sd_stats_get(&now);
    const struct kui_vmu_persist_geometry *g=&vc2_persist_profile.geometry;
    uint32_t dmaor=0u,chcr=0u,ccr=0u;
#ifdef KUI_ON_CONSOLE
    dmaor=*(volatile const uint32_t *)(uintptr_t)UINT32_C(0xffa00040);
    chcr=*(volatile const uint32_t *)(uintptr_t)UINT32_C(0xffa0001c);
    ccr=*(volatile const uint32_t *)(uintptr_t)UINT32_C(0xff00001c);
#endif
    uint32_t valid=vc2_persist_clock.valid;
    retail_display_vc2_ownership_status((uint32_t[17]){
        g->fs_type,g->sectors_per_cluster*512u,g->cluster_count,valid,
        valid?kui_vc2_ce_vmu_stage_clock_ms(tick-vc2_persist_phase_ticks):UINT32_MAX,
        valid?kui_vc2_ce_vmu_stage_clock_ms(vc2_persist_io_ticks):UINT32_MAX,
        vc2_persist_cache.commands,vc2_persist_cache.sectors,
        now.rx_blocks-vc2_persist_phase_io.rx_blocks,now.polled_blocks-vc2_persist_phase_io.polled_blocks,
        dmaor,chcr,vc2_persist_work.filesystem.stats.fat_entries,
        vc2_persist_work.filesystem.stats.chain_steps,vc2_persist_work.filesystem.stats.reads,
        vc2_persist_work.filesystem.stats.directory_sectors,ccr});
    vc2_persist_status_ticks=tick;vc2_persist_status_runs=vc2_persist_cache.commands;
}

static void vc2_persist_phase_end(void) {
    if(!vc2_persist_read_phase)return;
    struct kui_sci_sd_stats now;kui_sci_sd_stats_get(&now);
    uint32_t elapsed=kui_vc2_ce_vmu_stage_clock_ticks(&vc2_persist_clock)-vc2_persist_phase_ticks;
    uint32_t valid=vc2_persist_clock.valid;
    if(vc2_persist_read_phase==KUI_VMU_PERSIST_SESSION_ADMISSION)
        retail_display_vc2_ownership_results(valid);
    retail_display_values(vc2_persist_read_phase==KUI_VMU_PERSIST_SESSION_ADMISSION?
        "OWNERSHIP MS READ MS DMA PIO":"VMU PHASE MS READ MS DMA PIO",(uint32_t[4]){
        valid?kui_vc2_ce_vmu_stage_clock_ms(elapsed):UINT32_MAX,
        valid?kui_vc2_ce_vmu_stage_clock_ms(vc2_persist_io_ticks):UINT32_MAX,
        now.rx_blocks-vc2_persist_phase_io.rx_blocks,now.polled_blocks-vc2_persist_phase_io.polled_blocks},4u);
}

static int vc2_persist_identity(void *context,struct kui_vmu_persist_session_identity *out) {
    struct kui_retail_storage *storage=context;struct kui_vmu_sd_identity id;
    if(kui_vmu_sd_identity_read(&storage->device.sd,&id)!=KUI_VMU_BACKING_OK)return -1;
    memcpy(out->cid,id.cid,16u);out->card_sectors=id.sectors;out->high_capacity=id.high_capacity;
    return 0;
}
static bool vc2_persist_healthy(void *context) {
    struct kui_retail_storage *storage=context;
    return storage->transport==KUI_STORAGE_SCI && storage->device.sd.ready &&
        !storage->stream.active && kui_sci_sd_healthy() && !kui_sci_sd_faulted();
}
static int vc2_persist_read_run(void *context,uint32_t lba,uint32_t blocks,void *out) {
    struct kui_retail_storage *storage=context;
    uint32_t start=kui_vc2_ce_vmu_stage_clock_ticks(&vc2_persist_clock);
    /* read() intentionally repeats CMD17 even for count>1. Only the
     * established CMD18/CMD12 helper actually batches a contiguous run. */
    last_card_result=blocks>1u?kui_loader_sd_read_multi(&storage->device.sd,lba,blocks,out):
        kui_loader_sd_read(&storage->device.sd,lba,blocks,out);
    vc2_persist_io_ticks+=kui_vc2_ce_vmu_stage_clock_ticks(&vc2_persist_clock)-start;
    return last_card_result==KUI_LOADER_SD_OK?0:-1;
}
static int vc2_persist_read(void *context,uint64_t lba,void *out) {
    uint32_t phase=vc2_persist_work.session.phase;
    if(phase!=vc2_persist_read_phase) {
        vc2_persist_phase_end();
        vc2_persist_read_phase=phase;
        vc2_persist_phase_ticks=kui_vc2_ce_vmu_stage_clock_ticks(&vc2_persist_clock);
        vc2_persist_status_ticks=vc2_persist_phase_ticks;vc2_persist_status_runs=vc2_persist_cache.commands;
        vc2_persist_io_ticks=0u;kui_sci_sd_stats_get(&vc2_persist_phase_io);
        if(phase==KUI_VMU_PERSIST_SESSION_ADMISSION) {
            retail_display_vc2_ownership_begin();
            vc2_persist_status(vc2_persist_phase_ticks);
        }
        if(phase==KUI_VMU_PERSIST_SESSION_RECOVERY)retail_display_line("VC2 VMU: RECOVERING SAVED CARD");
        if(phase==KUI_VMU_PERSIST_SESSION_VALIDATION)retail_display_line("VC2 VMU: VALIDATING CARD CONTENTS");
    }
    if(phase==KUI_VMU_PERSIST_SESSION_ADMISSION) {
        uint32_t tick=kui_vc2_ce_vmu_stage_clock_ticks(&vc2_persist_clock);
        if((vc2_persist_clock.valid && tick-vc2_persist_status_ticks>=UINT32_C(779296)) ||
           (!vc2_persist_clock.valid && vc2_persist_cache.commands-vc2_persist_status_runs>=128u))
            vc2_persist_status(tick);
    }
    return kui_vc2_ce_vmu_stage_cache_read(&vc2_persist_cache,&vc2_persist_profile,
        phase==KUI_VMU_PERSIST_SESSION_ADMISSION,
        phase==KUI_VMU_PERSIST_SESSION_VALIDATION,lba,out,vc2_persist_read_run,context);
}
static int vc2_persist_write(void *context,uint64_t lba,const void *in) {
    struct kui_retail_storage *storage=context;
    kui_vc2_ce_vmu_stage_cache_invalidate(&vc2_persist_cache);
    if(lba>UINT32_MAX)return -1;
    bool (*block)(void *,const uint8_t *,uint8_t *,size_t,bool,uint16_t *)=storage->device.sd.bus.transfer_block;
    storage->device.sd.bus.transfer_block=NULL;
    last_card_result=kui_sci_sd_write(&storage->device.sd,(uint32_t)lba,1u,in);
    storage->device.sd.bus.transfer_block=block;
    return last_card_result==KUI_LOADER_SD_OK?0:-1;
}
static int vc2_persist_sync(void *context) {
    struct kui_retail_storage *storage=context;
    kui_vc2_ce_vmu_stage_cache_invalidate(&vc2_persist_cache);
    last_card_result=kui_sci_sd_sync(&storage->device.sd);
    return last_card_result==KUI_LOADER_SD_OK?0:-1;
}
/* Protect every selected-card allocation, including journal/identity slack,
 * before recovery can write. The capsule is never inserted into the GDI. */
static bool vc2_persist_capsule_admit(const struct kui_vc2_vmu_late_seed *seed) {
    if(!seed || !seed->first_lba || !seed->first_blocks ||
       seed->first_blocks>KUI_VC2_PERSIST_CAPSULE_BLOCKS ||
       ((seed->first_blocks==KUI_VC2_PERSIST_CAPSULE_BLOCKS)!=(seed->second_lba==0u)))return false;
    const uint32_t lba[2]={seed->first_lba,seed->second_lba};
    const uint32_t blocks[2]={seed->first_blocks,KUI_VC2_PERSIST_CAPSULE_BLOCKS-seed->first_blocks};
    for(unsigned run=0u;run<2u;run++) {
        if(!blocks[run])continue;
        uint64_t end=(uint64_t)lba[run]+blocks[run];
        if(lba[run]<manifest.partition_start || end>manifest.partition_end ||
           end>vc2_persist_profile.card_sectors || end>UINT64_C(0x100000000))return false;
        if(run && (uint64_t)lba[0]<end && (uint64_t)lba[run]<(uint64_t)lba[0]+blocks[0])return false;
        for(unsigned role=0u;role<KUI_VMU_PERSIST_PROFILE_FILES;role++) {
            const struct kui_vmu_persist_file *file=&vc2_persist_profile.files[role];
            for(uint32_t i=0u;i<file->allocated_count;i++) {
                const struct kui_vmu_persist_extent *e=&file->allocated[i];
                if((uint64_t)lba[run]<(uint64_t)e->lba+e->blocks && (uint64_t)e->lba<end)return false;
            }
        }
        for(uint32_t i=0u;i<manifest.extent_count;i++) {
            const struct kui_retail_extent *e=&manifest.slots[manifest.track_count+i].extent;
            if((uint64_t)lba[run]<(uint64_t)e->card_lba+e->blocks && (uint64_t)e->card_lba<end)return false;
        }
    }
    return true;
}
#if KUI_SMASH_CE_RETURN
static int smash_ce_return_transport_seal(const struct kui_vc2_vmu_late_seed *seed) {
    /* Only current RAM ownership may seal this carrier. An old runtime on
     * disk is ignored and overwritten; it never supplies a receipt or CRC. */
    if(smash_ce_return_transport_crc ||
       kui_smash_card_receipt_owner(&smash_card_receipt)!=KUI_SMASH_CARD_ROLE_VC2+KUI_SMASH_SWIRL_VMU)
        return -110;
    smash_ce_return_prepared.card_receipt=smash_card_receipt;
    smash_ce_return_prepared.code_crc32=kui_smash_ce_return_runtime_crc(
        KUI_SMASH_CE_RETURN_CODE_CRC32,&smash_ce_return_prepared);
    const uint32_t begin=KUI_SMASH_CE_TRANSPORT_CODE_OFFSET+KUI_SMASH_CE_RETURN_RUNTIME_OFFSET;
    const uint32_t end=begin+sizeof(smash_ce_return_prepared);
    if(end>KUI_VC2_PERSIST_HELPER_BYTES)return -111;
    for(uint32_t block=begin/512u;block<(end+511u)/512u;block++) {
        uint32_t at=block*512u;
        memcpy(vc2_persist_check,__vc2_vmu_blob_start+at,512u);
        for(uint32_t i=0u;i<512u;i++)if(at+i>=begin && at+i<end)
            vc2_persist_check[i]=((const uint8_t *)&smash_ce_return_prepared)[at+i-begin];
        if(!vc2_persist_healthy(&card) || vc2_persist_write(&card,kui_vc2_ce_vmu_capsule_lba(seed,block),vc2_persist_check))return -112;
    }
    if(vc2_persist_sync(&card) || !vc2_persist_healthy(&card))return -113;
    for(uint32_t block=begin/512u;block<(end+511u)/512u;block++) {
        uint32_t at=block*512u;
        if(vc2_persist_read(&card,kui_vc2_ce_vmu_capsule_lba(seed,block),vc2_persist_check))return -114;
        for(uint32_t i=0u;i<512u;i++) {
            uint8_t expected=at+i>=begin && at+i<end?
                ((const uint8_t *)&smash_ce_return_prepared)[at+i-begin]:__vc2_vmu_blob_start[at+i];
            if(vc2_persist_check[i]!=expected)return -115;
        }
    }
    uint32_t crc=kui_retail_crc32(0u,__vc2_vmu_blob_start,begin);
    crc=kui_retail_crc32(crc,&smash_ce_return_prepared,sizeof(smash_ce_return_prepared));
    crc=kui_retail_crc32(crc,__vc2_vmu_blob_start+end,KUI_VC2_PERSIST_HELPER_BYTES-end);
    if(!crc)return -116;
    smash_ce_return_transport_crc=crc;
    return 0;
}
#endif
static uint32_t vc2_ce_vmu_persist_prepare(const struct kui_vc2_vmu_late_seed *seed) {
    size_t bytes=(size_t)(vc2_persist_wire_end-vc2_persist_wire);
    if(vc2_persist_started || bytes!=KUI_VMU_PERSIST_PROFILE_WIRE_BYTES ||
       kui_vmu_persist_profile_decode(&vc2_persist_profile,vc2_persist_wire,bytes))
        stopped("VC2 PERSISTENT CARD PROFILE REFUSED",(uint32_t)bytes);
    vc2_persist_started=1u;
    if(kui_vmu_persist_stage_admit(&vc2_persist_profile,&manifest) || !vc2_persist_capsule_admit(seed))
        stopped("VC2 CAPSULE / CARD ALLOCATION REFUSED",seed?seed->first_lba:0u);
    if(card.transport!=KUI_STORAGE_SCI || card.stream.active ||
       kui_retail_storage_acquire(&card)!=KUI_LOADER_SD_OK)
        stopped("VC2 PERSISTENT PREPARATION BUS UNAVAILABLE",card.transport);
    const struct kui_vmu_persist_session_ops ops={&card,vc2_persist_identity,
        vc2_persist_read,vc2_persist_write,vc2_persist_sync,vc2_persist_healthy};
    (void)kui_vc2_ce_vmu_stage_clock_start(&vc2_persist_clock);
    retail_display_line("VC2 VMU: VERIFYING OWNERSHIP AND SAVE");
    int result=
#if KUI_SMASH_CARD_SESSION
        smash_card_session_prepare(&vc2_persist_profile,&manifest,&ops,
#else
        kui_vmu_persist_stage_prepare(&vc2_persist_profile,&manifest,&ops,
#endif
        &vc2_persist_work,&vc2_persist_config.handoff);
    vc2_persist_phase_end();vc2_persist_read_phase=0u;
    retail_display_values("VMU FS READS FAT ENTRIES CHAIN STEPS",(uint32_t[3]){
        vc2_persist_work.filesystem.stats.reads,vc2_persist_work.filesystem.stats.fat_entries,
        vc2_persist_work.filesystem.stats.chain_steps},3u);
    retail_display_values("VMU CALLS SD READ RUNS BLOCKS HITS",(uint32_t[4]){
        vc2_persist_cache.requests,vc2_persist_cache.commands,vc2_persist_cache.sectors,
        vc2_persist_cache.hits},4u);
    if(!result) {
        vc2_persist_config.magic=KUI_VC2_PERSIST_CONFIG_MAGIC;
        vc2_persist_config.version=KUI_VC2_PERSIST_CONFIG_VERSION;
        vc2_persist_config.bytes=sizeof(vc2_persist_config);
        vc2_persist_config.handoff_bytes=sizeof(vc2_persist_config.handoff);
        vc2_persist_config.crc32=kui_vc2_ce_vmu_persist_config_crc(&vc2_persist_config);
        if(!kui_vc2_ce_vmu_persist_config_valid(&vc2_persist_config))result=-100;
    }
    /* The code bytes remain immutable. Each mutable tail sector comes only
     * from the launch-validated capsule map, followed by sync and exact readback. */
    if(!result)retail_display_line("VC2 VMU: SEALING PREPARED CARD");
#if KUI_SMASH_CE_RETURN
    if(!result)result=smash_ce_return_transport_seal(seed);
#endif
    for(uint32_t i=0u;!result && i<KUI_VC2_PERSIST_CONFIG_BLOCKS;i++) {
        if(!vc2_persist_healthy(&card) || vc2_persist_write(&card,
            kui_vc2_ce_vmu_capsule_lba(seed,KUI_VC2_PERSIST_HELPER_BLOCKS+i),
            (const uint8_t *)&vc2_persist_config+512u*i))result=-101;
    }
    if(!result && (vc2_persist_sync(&card) || !vc2_persist_healthy(&card)))result=-102;
    for(uint32_t i=0u;!result && i<KUI_VC2_PERSIST_CONFIG_BLOCKS;i++) {
        if(vc2_persist_read(&card,kui_vc2_ce_vmu_capsule_lba(seed,KUI_VC2_PERSIST_HELPER_BLOCKS+i),
            vc2_persist_check) || memcmp(vc2_persist_check,(const uint8_t *)&vc2_persist_config+512u*i,512u))result=-103;
    }
    struct kui_vmu_persist_session_identity actual={0};
    if(!result && (vc2_persist_identity(&card,&actual) ||
       actual.card_sectors!=vc2_persist_config.handoff.card_sectors ||
       actual.high_capacity!=vc2_persist_config.handoff.high_capacity ||
       memcmp(actual.cid,vc2_persist_config.handoff.cid,sizeof(actual.cid))))result=-104;
    enum kui_loader_sd_result stop_result=kui_retail_storage_stop(&card);
    bool healthy=vc2_persist_healthy(&card);
    kui_retail_storage_release(&card);
    if(vc2_persist_clock.active && !kui_vc2_ce_vmu_stage_clock_stop(&vc2_persist_clock))
        stopped("VC2 OWNERSHIP TIMER RESTORE REFUSED",0u);
    healthy=healthy && card.device.sd.ready && !card.stream.active && !kui_sci_sd_faulted();
    if(result || stop_result!=KUI_LOADER_SD_OK || !healthy) {
        memset(&vc2_persist_config,0,sizeof(vc2_persist_config));
        kui_vmu_persist_session_poison(&vc2_persist_work.session);
        stopped("VC2 CARD RECOVERY / CONFIG / RELEASE FAILED",
            (vc2_persist_work.phase<<16)|((uint32_t)result&UINT32_C(0xffff)));
    }
    return kui_retail_crc32(0u,&vc2_persist_config,sizeof(vc2_persist_config));
}
