/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_sd_stage_admission.h"
#include <assert.h>
#include <stdio.h>

static struct kui_vmu_sd_profile profile;
static struct kui_retail_manifest game;

static void fixture(void) {
    memset(&profile,0,sizeof(profile));memset(&game,0,sizeof(game));
    profile.card_sectors=100000u;profile.partition_first=100u;profile.partition_end=90000u;
    profile.transport=1u;profile.image_bytes=131072u;profile.high_capacity=1u;profile.extent_count=1u;
    profile.cid[0]=1u;profile.image_id[0]=2u;profile.sha256[0]=3u;
    profile.extents[0]=(struct kui_vmu_sd_profile_extent){0u,3000u,256u};
    game.card_sectors=100000u;game.partition_start=100u;game.partition_end=90000u;
    game.storage_transport=KUI_STORAGE_SCI;game.reader=KUI_RETAIL_READER_STANDARD;
    game.track_count=1u;game.extent_count=1u;game.session_lba=45000u;
    game.boot_lba=45000u;game.boot_bytes=2048u;
    memcpy(game.title,"Stage fixture",14u);memcpy(game.bootfile,"1ST_READ.BIN",13u);
    game.slots[0].track=(struct kui_retail_track){45000u,45004u,1u,1u,4u};
    game.slots[1].extent=(struct kui_retail_extent){0u,1000u,19u};
    assert(kui_vmu_sd_profile_validate(&profile)==0);
    assert(kui_retail_manifest_validate(&game)==KUI_GAME_OK);
}
int main(void) {
    fixture();assert(kui_vmu_sd_stage_admit(&profile,&game));
    assert(!kui_vmu_sd_stage_admit(NULL,&game));
    assert(!kui_vmu_sd_stage_admit(&profile,NULL));
    /* Game preparation may know only the partition-end capacity lower bound. */
    game.card_sectors=90000u;assert(kui_vmu_sd_stage_admit(&profile,&game));
    fixture();profile.extents[0].lba=1019u;assert(kui_vmu_sd_stage_admit(&profile,&game));
    profile.extents[0].lba=1018u;assert(!kui_vmu_sd_stage_admit(&profile,&game));
    fixture();game.slots[1].extent.card_lba=3100u;assert(!kui_vmu_sd_stage_admit(&profile,&game));
    fixture();game.extent_count=2u;game.slots[0].track.extent_count=2u;
    game.slots[1].extent.blocks=9u;game.slots[2].extent=(struct kui_retail_extent){9u,3100u,10u};
    assert(kui_retail_manifest_validate(&game)==KUI_GAME_OK);
    assert(!kui_vmu_sd_stage_admit(&profile,&game));
    fixture();profile.extent_count=2u;profile.extents[0].blocks=128u;
    profile.extents[1]=(struct kui_vmu_sd_profile_extent){128u,1000u,128u};
    assert(!kui_vmu_sd_stage_admit(&profile,&game));
    /* Even an audio allocation that the standard reader never plays is not
     * an admissible place to borrow the VMU image's sectors. */
    fixture();game.track_count=2u;game.extent_count=2u;
    game.slots[0].track.first_extent=2u;
    game.slots[1].track=(struct kui_retail_track){45004u,45008u,3u,1u,0u};
    game.slots[2].extent=(struct kui_retail_extent){0u,1000u,19u};
    game.slots[3].extent=(struct kui_retail_extent){0u,3100u,19u};
    assert(kui_retail_manifest_validate(&game)==KUI_GAME_OK);
    assert(!kui_vmu_sd_stage_admit(&profile,&game));
    game.slots[3].extent.card_lba=4000u;assert(kui_vmu_sd_stage_admit(&profile,&game));
    fixture();game.card_sectors=100001u;assert(!kui_vmu_sd_stage_admit(&profile,&game));
    fixture();++game.partition_start;assert(!kui_vmu_sd_stage_admit(&profile,&game));
    fixture();--game.partition_end;assert(!kui_vmu_sd_stage_admit(&profile,&game));
    fixture();game.storage_transport=KUI_STORAGE_SCIF;assert(!kui_vmu_sd_stage_admit(&profile,&game));
    fixture();game.reader=KUI_RETAIL_READER_ASYNC;assert(!kui_vmu_sd_stage_admit(&profile,&game));
    fixture();game.flags=KUI_RETAIL_IMAGE_CD;assert(!kui_vmu_sd_stage_admit(&profile,&game));
    fixture();game.extent_count=UINT32_MAX;assert(!kui_vmu_sd_stage_admit(&profile,&game));
    fixture();profile.extent_count=17u;assert(!kui_vmu_sd_stage_admit(&profile,&game));
    fixture();struct kui_vmu_sd_identity actual={.sectors=100000u,.high_capacity=true};
    memcpy(actual.cid,profile.cid,16u);assert(kui_vmu_sd_stage_identity(&profile,&actual));
    for(unsigned i=0;i<16u;i++) {actual.cid[i]^=1u;assert(!kui_vmu_sd_stage_identity(&profile,&actual));actual.cid[i]^=1u;}
    actual.high_capacity=false;assert(!kui_vmu_sd_stage_identity(&profile,&actual));
    actual.high_capacity=true;--actual.sectors;assert(!kui_vmu_sd_stage_identity(&profile,&actual));
    assert(!kui_vmu_sd_stage_identity(NULL,&actual));
    assert(!kui_vmu_sd_stage_identity(&profile,NULL));
    puts("VMU SD high-stage admission: all game extents, partition/capacity, modes and CID passed");
    return 0;
}
