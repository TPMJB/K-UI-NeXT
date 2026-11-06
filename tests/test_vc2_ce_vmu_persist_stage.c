/* SPDX-License-Identifier: GPL-3.0-only */
/* Production capsule admission/config transport; durable recovery remains the
 * separately tested core's contract and is represented by a sealed handoff. */
#include "kui/retail_image.h"
#include "kui/vmu_persist_stage.h"
#include "retail_storage.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

static struct kui_retail_manifest manifest;
static struct kui_retail_storage card;
static enum kui_loader_sd_result last_card_result;
static jmp_buf stopped_jump;
static const char *stopped_reason;
static uint32_t stopped_detail;
static uint32_t ownership_status[17];
static unsigned ownership_status_calls,ownership_begins,ownership_results;
static bool ownership_page;
static uint32_t ownership_result_clock;
static void __attribute__((noreturn)) stopped(const char *reason,uint32_t detail) {
    stopped_reason=reason;stopped_detail=detail;longjmp(stopped_jump,1);
}
#if KUI_VC2_PERSIST_CE_RETURN_TEST
#include "kui/smash_ce_return_loader.h"
#include "kui/smash_ce_transport.h"
#define KUI_SMASH_CE_RETURN_CODE_CRC32 UINT32_C(0x87310bce)
static struct kui_smash_card_receipt smash_card_receipt;
static struct kui_smash_ce_return_runtime smash_ce_return_prepared;
static uint32_t smash_ce_return_transport_crc;
static uint8_t static_transport[KUI_SMASH_CE_TRANSPORT_FILE_BYTES];
#define __vc2_vmu_blob_start static_transport
enum {CARRIER_BEGIN=41184u+KUI_SMASH_CE_RETURN_RUNTIME_OFFSET,
    CARRIER_END=CARRIER_BEGIN+632u,
    CARRIER_FIRST=CARRIER_BEGIN/512u,
    CARRIER_BLOCKS=(CARRIER_END+511u)/512u-CARRIER_FIRST};
#else
enum {CARRIER_BLOCKS=0};
#endif
void retail_display_line(const char *line) {assert(line && *line);}
void retail_display_values(const char *line,const uint32_t *values,unsigned count) {
    assert(line && *line && values && (count==3u || count==4u));
    if(!strncmp(line,"OWNERSHIP MS",12u))assert(!ownership_page && ownership_results);
}
void retail_display_vc2_ownership_begin(void) {
    assert(!ownership_page);ownership_page=true;++ownership_begins;
}
void retail_display_vc2_ownership_status(const uint32_t values[17]) {
    assert(values && ownership_page);memcpy(ownership_status,values,sizeof(ownership_status));++ownership_status_calls;
}
void retail_display_vc2_ownership_results(uint32_t clock_available) {
    assert(ownership_page);ownership_page=false;++ownership_results;
    ownership_result_clock=clock_available;
}
#include "../src/loader/vc2_ce_vmu_persist_stage.inc"
static uint32_t timer_start,timer_control,timer_constant,timer_count;
static struct kui_sci_sd_stats live_io;
uint32_t kui_vc2_stage_clock_sr(void) {return UINT32_C(0x500000f0);}
uint32_t kui_vc2_stage_clock_read(uint32_t address,unsigned width) {
    if(address==UINT32_C(0xffd80004)) {assert(width==1u);return timer_start;}
    if(address==UINT32_C(0xffd80028)) {assert(width==2u);return timer_control;}
    assert(width==4u);
    if(address==UINT32_C(0xffd80020))return timer_constant;
    assert(address==UINT32_C(0xffd80024));return timer_count;
}
void kui_vc2_stage_clock_write(uint32_t address,uint32_t value,unsigned width) {
    if(address==UINT32_C(0xffd80004)) {assert(width==1u);timer_start=value;}
    else if(address==UINT32_C(0xffd80028)) {assert(width==2u);timer_control=value;}
    else if(address==UINT32_C(0xffd80020)) {assert(width==4u);timer_constant=value;}
    else {assert(address==UINT32_C(0xffd80024) && width==4u);timer_count=value;}
}
void kui_sci_sd_stats_get(struct kui_sci_sd_stats *out) {*out=live_io;}

static struct kui_vc2_vmu_late_seed source;
static uint8_t sectors[3+CARRIER_BLOCKS][512];
static uint32_t written_lba[3+CARRIER_BLOCKS];
static unsigned acquires,releases,stops,prepares,writes,reads,syncs,identities,poisons,multi_reads;
static unsigned fail_write,fail_read,corrupt_read,fail_health,health_calls;
static bool fail_acquire,fail_stop,fail_sync,fail_identity,changed_identity,release_fault,fault;
static bool metadata_read;
static int prepare_result,admit_result;
static bool sentinel_block(void *context,const uint8_t *tx,uint8_t *rx,
    size_t bytes,bool paced,uint16_t *crc) {
    (void)context;(void)tx;(void)rx;(void)bytes;(void)paced;(void)crc;return false;
}
enum kui_loader_sd_result kui_retail_storage_acquire(struct kui_retail_storage *storage) {
    assert(storage==&card);++acquires;return fail_acquire?KUI_LOADER_SD_TIMEOUT:KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_retail_storage_stop(struct kui_retail_storage *storage) {
    assert(storage==&card);++stops;return fail_stop?KUI_LOADER_SD_TIMEOUT:KUI_LOADER_SD_OK;
}
void kui_retail_storage_release(struct kui_retail_storage *storage) {
    assert(storage==&card);++releases;if(release_fault)fault=true;
}
bool kui_sci_sd_healthy(void) {return ++health_calls!=fail_health;}
bool kui_sci_sd_faulted(void) {return fault;}
int kui_vmu_sd_identity_read(struct kui_loader_sd *sd,struct kui_vmu_sd_identity *out) {
    assert(sd==&card.device.sd);++identities;
    if(fail_identity)return -1;
    memcpy(out->cid,vc2_persist_profile.cid,16u);out->sectors=vc2_persist_profile.card_sectors;
    out->high_capacity=vc2_persist_profile.high_capacity!=0u;
    if(changed_identity)out->cid[15]^=1u;
    return KUI_VMU_BACKING_OK;
}
enum kui_loader_sd_result kui_sci_sd_write(struct kui_loader_sd *sd,uint32_t lba,
    uint32_t count,const uint8_t *in) {
    assert(sd==&card.device.sd && count==1u && !sd->bus.transfer_block && writes<3u+CARRIER_BLOCKS);
#if KUI_VC2_PERSIST_CE_RETURN_TEST
    assert((writes<CARRIER_BLOCKS)==!smash_ce_return_transport_crc);
    assert(reads==(writes<CARRIER_BLOCKS?0u:CARRIER_BLOCKS));
#endif
    written_lba[writes]=lba;memcpy(sectors[writes],in,512u);++writes;
    return writes==fail_write?KUI_LOADER_SD_TIMEOUT:KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_sci_sd_sync(struct kui_loader_sd *sd) {
    assert(sd==&card.device.sd && sd->bus.transfer_block==sentinel_block);
    ++syncs;return fail_sync?KUI_LOADER_SD_TIMEOUT:KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_loader_sd_read(struct kui_loader_sd *sd,uint32_t lba,
    uint32_t count,void *out) {
    if(metadata_read) {
        assert(sd==&card.device.sd && count==1u && lba==vc2_persist_profile.card_sectors-1u);
        assert(!((uintptr_t)out&31u));
        memset(out,0x35,512u);timer_count-=1000u;++live_io.rx_blocks;return KUI_LOADER_SD_OK;
    }
    assert(sd==&card.device.sd && count==1u && reads<3u+CARRIER_BLOCKS);
#if KUI_VC2_PERSIST_CE_RETURN_TEST
    assert(syncs==(reads<CARRIER_BLOCKS?1u:2u));
    assert((reads<CARRIER_BLOCKS)==!smash_ce_return_transport_crc);
#else
    assert(syncs==1u);
#endif
    assert(lba==written_lba[reads]);memcpy(out,sectors[reads],512u);++reads;
    if(reads==corrupt_read)((uint8_t *)out)[511]^=1u;
    return reads==fail_read?KUI_LOADER_SD_TIMEOUT:KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_loader_sd_read_multi(struct kui_loader_sd *sd,uint32_t lba,
    uint32_t count,void *out) {
    assert(sd==&card.device.sd && count>1u && count<=KUI_VC2_STAGE_READ_BLOCKS && lba==123u);
    ++multi_reads;memset(out,0x71,(size_t)count*512u);return KUI_LOADER_SD_OK;
}
int kui_vmu_persist_stage_admit(const struct kui_vmu_persist_profile *profile,
    const struct kui_retail_manifest *map) {
    assert(profile==&vc2_persist_profile && map==&manifest);return admit_result;
}
const struct kui_vmu_persist_stage_policy kui_vmu_persist_stage_exclusive_checkpoint={1u,16u,1u,0u};
int kui_vmu_persist_stage_prepare_policy(const struct kui_vmu_persist_profile *profile,
    const struct kui_retail_manifest *map,const struct kui_vmu_persist_session_ops *ops,
    const struct kui_vmu_persist_stage_policy *policy,
    struct kui_vmu_persist_stage_workspace *work,struct kui_vmu_durable_handoff *handoff) {
    assert(policy==&kui_vmu_persist_stage_exclusive_checkpoint);
    assert(profile==&vc2_persist_profile && map==&manifest && ops->context==&card);
    assert(acquires==1u && !releases && work==&vc2_persist_work);++prepares;
    work->phase=KUI_VMU_PERSIST_STAGE_SEAL;
    if(prepare_result)return prepare_result;
    memset(handoff,0,sizeof(*handoff));handoff->card_sectors=profile->card_sectors;
    handoff->high_capacity=profile->high_capacity;
    memcpy(handoff->cid,profile->cid,16u);memcpy(handoff->image_id,profile->image_id,16u);
    memcpy(handoff->base_sha256,profile->base_sha256,32u);
    memcpy(handoff->block_crc,profile->block_crc,sizeof(handoff->block_crc));
    handoff->base_count=profile->files[0].useful_count;
    handoff->journal_count=profile->files[1].useful_count;
    for(unsigned role=0u;role<2u;role++)for(unsigned i=0u;i<profile->files[role].useful_count;i++) {
        struct kui_vmu_durable_extent *target=role?handoff->journal:handoff->base;
        target[i]=(struct kui_vmu_durable_extent){profile->files[role].useful[i].lba,
            profile->files[role].useful[i].blocks};
    }
    assert(!kui_vmu_durable_handoff_seal(handoff));return 0;
}
void kui_vmu_persist_session_poison(struct kui_vmu_persist_session *session) {
    assert(session==&vc2_persist_work.session);++poisons;
}
static void fixture(void) {
    memset(&vc2_persist_profile,0,sizeof(vc2_persist_profile));
    assert(!kui_vmu_persist_profile_decode(&vc2_persist_profile,vc2_persist_wire,4096u));
    memset(&vc2_persist_work,0,sizeof(vc2_persist_work));
    memset(&vc2_persist_config,0,sizeof(vc2_persist_config));
    vc2_persist_started=0u;
    memset(&vc2_persist_clock,0,sizeof(vc2_persist_clock));
    timer_start=3u;timer_control=0x20u;timer_constant=123u;timer_count=45u;
    memset(&live_io,0,sizeof(live_io));memset(ownership_status,0,sizeof(ownership_status));
    ownership_status_calls=ownership_begins=ownership_results=0u;ownership_page=false;
    ownership_result_clock=0u;metadata_read=false;
    memset(&vc2_persist_cache,0,sizeof(vc2_persist_cache));vc2_persist_read_phase=0u;
    manifest=(struct kui_retail_manifest){.partition_start=2048u,.partition_end=1048576u,
        .track_count=1u,.extent_count=1u};
    manifest.slots[1].extent=(struct kui_retail_extent){.card_lba=25000u,.blocks=100u};
    memset(&card,0,sizeof(card));card.transport=KUI_STORAGE_SCI;card.device.sd.ready=true;
    card.device.sd.bus.transfer_block=sentinel_block;
    source=(struct kui_vc2_vmu_late_seed){.first_lba=20000u,.first_blocks=99u};
    memset(sectors,0xaa,sizeof(sectors));memset(written_lba,0,sizeof(written_lba));
    acquires=releases=stops=prepares=writes=reads=syncs=identities=poisons=multi_reads=0u;
    fail_write=fail_read=corrupt_read=fail_health=health_calls=0u;
    fail_acquire=fail_stop=fail_sync=fail_identity=changed_identity=release_fault=fault=false;
    prepare_result=admit_result=0;stopped_reason=NULL;stopped_detail=0u;
#if KUI_VC2_PERSIST_CE_RETURN_TEST
    smash_ce_return_transport_crc=0u;
    for(unsigned i=0u;i<sizeof(static_transport);i++)static_transport[i]=(uint8_t)(i*17u+0x73u);
    memset(static_transport+41152u,0,sizeof(static_transport)-41152u);
    struct kui_smash_ce_transport t={KUI_SMASH_CE_TRANSPORT_MAGIC0,KUI_SMASH_CE_TRANSPORT_MAGIC1,
        1u,32u,41152u,1024u,KUI_SMASH_CE_RETURN_RUNTIME_OFFSET,632u};
    memcpy(static_transport+41152u,&t,sizeof(t));
    assert(kui_smash_ce_transport_static(static_transport,sizeof(static_transport),NULL));
    memset(&smash_ce_return_prepared,0x45,sizeof(smash_ce_return_prepared));
    struct kui_vmu_fs_witness_binding b={.card_sectors=vc2_persist_profile.card_sectors,
        .partition_first=vc2_persist_profile.partition_first,.partition_end=vc2_persist_profile.partition_end,
        .high_capacity=vc2_persist_profile.high_capacity,.generation=7u};
    memcpy(b.cid,vc2_persist_profile.cid,16u);memcpy(b.image_id,vc2_persist_profile.image_id,16u);
    memset(b.binding_sha256,0x51,32u);uint8_t admission[32];memset(admission,0x87,32u);
    assert(kui_smash_card_receipt_mint(&smash_card_receipt,&b,admission));
    assert(kui_smash_card_receipt_transfer(&smash_card_receipt,KUI_SMASH_CARD_ROLE_VC2+KUI_SMASH_SWIRL_VMU));
#endif
}
static void success(void) {
    assert(!setjmp(stopped_jump));uint32_t crc=vc2_ce_vmu_persist_prepare(&source);
    assert(acquires==1u && prepares==1u && writes==3u && syncs==1u && reads==3u);
    assert(identities==1u && stops==1u && releases==1u && !poisons && !stopped_reason);
    assert(card.device.sd.bus.transfer_block==sentinel_block);
    assert(!vc2_persist_clock.active && timer_start==3u && timer_control==0x20u &&
        timer_constant==123u && timer_count==45u);
    assert(kui_vc2_ce_vmu_persist_config_valid(&vc2_persist_config));
    assert(crc==kui_retail_crc32(0u,&vc2_persist_config,sizeof(vc2_persist_config)));
    for(unsigned i=0u;i<3u;i++) {
        assert(written_lba[i]==kui_vc2_ce_vmu_capsule_lba(&source,96u+i));
        assert(!memcmp(sectors[i],(const uint8_t *)&vc2_persist_config+512u*i,512u));
    }
}
static void refusal(bool acquired) {
    if(!setjmp(stopped_jump)) {
        (void)vc2_ce_vmu_persist_prepare(&source);assert(!"expected stage refusal");
    }
    assert(stopped_reason);
    if(acquired) {
        assert(stops==1u && releases==1u && poisons==1u);
        const uint8_t *bytes=(const uint8_t *)&vc2_persist_config;
        for(size_t i=0u;i<sizeof(vc2_persist_config);i++)assert(!bytes[i]);
    } else assert(!writes && !syncs && !reads && !releases);
    assert(card.device.sd.bus.transfer_block==sentinel_block);
}
static void live_ownership_progress(void) {
    fixture();metadata_read=true;uint8_t out[512];
    assert(kui_vc2_ce_vmu_stage_clock_start(&vc2_persist_clock));
    vc2_persist_work.session.phase=KUI_VMU_PERSIST_SESSION_ADMISSION;
    assert(!vc2_persist_read(&card,vc2_persist_profile.card_sectors-1u,out));
    assert(ownership_status_calls==1u && ownership_begins==1u && ownership_page);
    assert(!ownership_status[4] && !ownership_status[5] && !ownership_status[6]);
    vc2_persist_work.filesystem.stats.fat_entries=19u;
    vc2_persist_work.filesystem.stats.chain_steps=31u;
    timer_count-=779296u;
    assert(!vc2_persist_read(&card,vc2_persist_profile.card_sectors-1u,out));
    assert(ownership_status_calls==2u && ownership_begins==1u && ownership_status[3]==1u);
    assert(ownership_status[0]==vc2_persist_profile.geometry.fs_type &&
        ownership_status[1]==vc2_persist_profile.geometry.sectors_per_cluster*512u &&
        ownership_status[2]==vc2_persist_profile.geometry.cluster_count);
    assert(ownership_status[4]==1001u && ownership_status[5]==1u);
    assert(ownership_status[6]==1u && ownership_status[7]==1u &&
        ownership_status[8]==1u && !ownership_status[9]);
    assert(ownership_status[12]==19u && ownership_status[13]==31u);
    assert(!ownership_status[16]); /* host has no live CPU cache register */
    for(unsigned i=0u;i<sizeof(out);i++)assert(out[i]==0x35);
    assert(kui_vc2_ce_vmu_stage_clock_stop(&vc2_persist_clock));
    vc2_persist_clock.valid=0u;vc2_persist_status(0u);
    assert(ownership_status_calls==3u && !ownership_status[3] &&
        ownership_status[4]==UINT32_MAX && ownership_status[5]==UINT32_MAX);
    vc2_persist_work.session.phase=KUI_VMU_PERSIST_SESSION_RECOVERY;
    assert(!vc2_persist_read(&card,vc2_persist_profile.card_sectors-1u,out));
    assert(ownership_results==1u && !ownership_page && !ownership_result_clock);
}
#if KUI_VC2_PERSIST_CE_RETURN_TEST
static void carrier_success(void) {
    assert(!setjmp(stopped_jump));(void)vc2_ce_vmu_persist_prepare(&source);
    assert(writes==CARRIER_BLOCKS+3u && reads==writes && syncs==2u && identities==1u);
    assert(acquires==1u && releases==1u && stops==1u && !poisons);
    assert(!memcmp(&smash_ce_return_prepared.card_receipt,&smash_card_receipt,sizeof(smash_card_receipt)));
    uint8_t expected[49152];memcpy(expected,static_transport,sizeof(expected));
    memcpy(expected+CARRIER_BEGIN,&smash_ce_return_prepared,sizeof(smash_ce_return_prepared));
    assert(smash_ce_return_transport_crc==kui_retail_crc32(0u,expected,sizeof(expected)));
    for(unsigned i=0u;i<CARRIER_BLOCKS;i++) {
        assert(written_lba[i]==kui_vc2_ce_vmu_capsule_lba(&source,CARRIER_FIRST+i));
        assert(!memcmp(sectors[i],expected+(CARRIER_FIRST+i)*512u,512u));
    }
    for(unsigned i=0u;i<3u;i++) {
        assert(written_lba[CARRIER_BLOCKS+i]==kui_vc2_ce_vmu_capsule_lba(&source,96u+i));
        assert(!memcmp(sectors[CARRIER_BLOCKS+i],(const uint8_t *)&vc2_persist_config+512u*i,512u));
    }
}
int main(void) {
    fixture();carrier_success();
    for(unsigned split=CARRIER_FIRST;split<CARRIER_FIRST+CARRIER_BLOCKS;split++) {
        fixture();source.first_blocks=split;source.second_lba=30000u;carrier_success();
    }
    for(unsigned i=1u;i<=CARRIER_BLOCKS;i++) {
        fixture();fail_write=i;refusal(true);assert(writes==i && !syncs && !reads && !smash_ce_return_transport_crc);
        fixture();fail_read=i;refusal(true);assert(reads==i && !smash_ce_return_transport_crc);
        fixture();corrupt_read=i;refusal(true);assert(reads==i && !smash_ce_return_transport_crc);
    }
    fixture();fail_sync=true;refusal(true);assert(writes==CARRIER_BLOCKS && !reads && !smash_ce_return_transport_crc);
    fixture();prepare_result=-5;refusal(true);assert(!writes && !smash_ce_return_transport_crc);
    for(unsigned owner=1u;owner<=4u;owner++) {
        if(owner==KUI_SMASH_CARD_ROLE_VC2+KUI_SMASH_SWIRL_VMU)continue;
        fixture();smash_card_receipt.flags=1u|(owner<<8u);smash_card_receipt.crc32=0u;
        smash_card_receipt.crc32=kui_retail_crc32(0u,&smash_card_receipt,sizeof(smash_card_receipt));
        refusal(true);assert(!writes && !smash_ce_return_transport_crc);
    }
    fixture();fail_identity=true;refusal(true);
    fixture();changed_identity=true;refusal(true);
    fixture();release_fault=true;refusal(true);
    printf("PASS CE%u current carrier: exact632B window in%u sectors, fresh sync/readback before CRC publication, static-neighbour preservation, wrong-owner/I/O/identity/release refusals\n",
        KUI_SMASH_SWIRL_VMU+1u,CARRIER_BLOCKS);
    return 0;
}
#else
int main(void) {
    live_ownership_progress();
    fixture();uint8_t batch[1024];
    assert(!vc2_persist_read_run(&card,123u,2u,batch) && multi_reads==1u && !reads);
    for(unsigned i=0u;i<sizeof(batch);i++)assert(batch[i]==0x71u);
    fixture();success();
    assert(written_lba[0]==20096u && written_lba[1]==20097u && written_lba[2]==20098u);
    for(uint32_t boundary=96u;boundary<=98u;boundary++) {
        fixture();source.first_blocks=boundary;source.second_lba=30000u;success();
        for(uint32_t i=0u;i<3u;i++)assert(written_lba[i]==
            (96u+i<boundary?20096u+i:30000u+96u+i-boundary));
    }
    fixture();source.first_lba=30000u;source.first_blocks=50u;source.second_lba=20000u;success();
    fixture();source.first_lba=0u;assert(!vc2_persist_capsule_admit(&source));refusal(false);
    fixture();source.first_blocks=0u;assert(!vc2_persist_capsule_admit(&source));
    fixture();source.first_blocks=100u;assert(!vc2_persist_capsule_admit(&source));
    fixture();source.first_blocks=98u;assert(!vc2_persist_capsule_admit(&source));
    fixture();source.second_lba=30000u;assert(!vc2_persist_capsule_admit(&source));
    fixture();source.first_lba=2000u;assert(!vc2_persist_capsule_admit(&source));
    fixture();source.first_lba=1048500u;assert(!vc2_persist_capsule_admit(&source));
    fixture();source.first_lba=UINT32_MAX-10u;assert(!vc2_persist_capsule_admit(&source));
    fixture();source.first_blocks=50u;source.second_lba=20040u;assert(!vc2_persist_capsule_admit(&source));
    fixture();source.first_lba=24901u;assert(vc2_persist_capsule_admit(&source));
    source.first_lba++;assert(!vc2_persist_capsule_admit(&source));
    for(unsigned role=0u;role<3u;role++) {
        fixture();source.first_lba=vc2_persist_profile.files[role].allocated[0].lba;
        assert(!vc2_persist_capsule_admit(&source));
        fixture();vc2_persist_profile.files[role].allocated[0].blocks+=100u;
        source.first_lba=vc2_persist_profile.files[role].allocated[0].lba+
            vc2_persist_profile.files[role].useful[0].blocks;
        assert(!vc2_persist_capsule_admit(&source)); /* whole-cluster slack */
    }
    fixture();admit_result=-1;refusal(false);
    fixture();fail_acquire=true;refusal(false);
    fixture();card.stream.active=true;refusal(false);
    fixture();card.transport=KUI_STORAGE_SCIF;refusal(false);
    fixture();prepare_result=-5;refusal(true);
    for(unsigned i=1u;i<=3u;i++) {
        fixture();fail_write=i;refusal(true);assert(writes==i && !syncs && !reads);
        fixture();fail_read=i;refusal(true);assert(reads==i);
        fixture();corrupt_read=i;refusal(true);assert(reads==i);
    }
    fixture();fail_sync=true;refusal(true);assert(writes==3u && !reads);
    fixture();fail_health=1u;refusal(true);assert(!writes);
    fixture();fail_health=5u;refusal(true);assert(writes==3u && reads==3u);
    fixture();fail_identity=true;refusal(true);
    fixture();changed_identity=true;refusal(true);
    fixture();fail_stop=true;refusal(true);
    fixture();release_fault=true;refusal(true);
    puts("PASS production VC2 stage: whole-allocation and image exclusion, capsule map boundaries, three-sector config write/sync/readback, identity and release refusal");
    return 0;
}
#endif
