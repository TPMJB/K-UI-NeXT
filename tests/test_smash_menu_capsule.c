/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_menu_capsule.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct kui_smash_menu_capsule_runs runs;
static bool map(const uint32_t *table,size_t words,uint32_t blocks) {
    memset(&runs,0xa5,sizeof(runs));
    bool ok=kui_smash_menu_capsule_map(table,words,100u,8u,1002u,2048u,10000u,blocks,&runs);
    if(!ok)assert(!memcmp(&runs,&(struct kui_smash_menu_capsule_runs){0},sizeof(runs)));
    return ok;
}
static void capsule_pair_ranges(void) {
    const struct kui_smash_menu_capsule_runs a={100u,10u,1000u,20u};
    struct kui_smash_menu_capsule_runs b={110u,10u,1010u,20u};
    assert(kui_smash_menu_capsule_runs_disjoint(&a,&b)); /* exact boundaries */
    b.first_lba=109u;assert(!kui_smash_menu_capsule_runs_disjoint(&a,&b));
    b=(struct kui_smash_menu_capsule_runs){110u,10u,1009u,20u};
    assert(!kui_smash_menu_capsule_runs_disjoint(&a,&b));
    b=(struct kui_smash_menu_capsule_runs){1009u,10u,110u,20u};
    assert(!kui_smash_menu_capsule_runs_disjoint(&a,&b)); /* cross run order */
    b=(struct kui_smash_menu_capsule_runs){1010u,10u,110u,20u};
    assert(kui_smash_menu_capsule_runs_disjoint(&a,&b));
    struct kui_smash_menu_capsule_runs useful={2000u,17u,0u,17u};
    struct kui_smash_menu_capsule_runs allocated={2000u,24u,0u,24u};
    b=(struct kui_smash_menu_capsule_runs){2017u,1u,0u,1u};
    assert(kui_smash_menu_capsule_runs_disjoint(&useful,&b));
    assert(!kui_smash_menu_capsule_runs_disjoint(&allocated,&b));
    assert(!kui_smash_menu_capsule_runs_disjoint(NULL,&b));
    assert(!kui_smash_menu_capsule_runs_disjoint(&b,NULL));
    b=(struct kui_smash_menu_capsule_runs){0};
    assert(!kui_smash_menu_capsule_runs_disjoint(&a,&b));
    b=(struct kui_smash_menu_capsule_runs){2000u,21u,0u,20u};
    assert(!kui_smash_menu_capsule_runs_disjoint(&a,&b));
    b=(struct kui_smash_menu_capsule_runs){2000u,10u,0u,20u};
    assert(!kui_smash_menu_capsule_runs_disjoint(&a,&b));
    b=(struct kui_smash_menu_capsule_runs){UINT32_MAX,2u,0u,2u};
    assert(!kui_smash_menu_capsule_runs_disjoint(&a,&b));
    b=(struct kui_smash_menu_capsule_runs){UINT32_MAX,1u,0u,1u};
    assert(kui_smash_menu_capsule_runs_disjoint(&a,&b)); /* final card sector */
    assert(!kui_smash_menu_capsule_runs_disjoint(&b,&b));
}
int main(void) {
    const uint32_t one[]={4u,4u,20u,0u};
    assert(map(one,4u,29u));
    assert(runs.first_lba==2292u && runs.first_blocks==29u && !runs.second_lba && runs.blocks==29u);
    const uint32_t adjacent[]={6u,2u,20u,2u,22u,0u};
    assert(map(adjacent,6u,32u) && runs.first_blocks==32u && !runs.second_lba);
    const uint32_t two[]={6u,2u,20u,3u,30u,0u};
    assert(map(two,6u,33u) && runs.first_blocks==16u && runs.second_lba==2372u && runs.blocks==33u);
    const uint32_t reversed[]={6u,2u,30u,3u,20u,0u};
    assert(map(reversed,6u,33u) && runs.first_lba==2372u && runs.second_lba==2292u);
    const uint32_t three[]={8u,1u,20u,1u,30u,1u,40u,0u};
    assert(!map(three,8u,17u));
    assert(map(three,8u,16u)); /* trailing allocation is not exposed */
    const uint32_t self[]={6u,2u,20u,2u,21u,0u};
    assert(!map(self,6u,32u));
    assert(!map(one,4u,33u));
    assert(!map(one,4u,0u));
    uint32_t bad[6];memcpy(bad,two,sizeof(bad));bad[5]=1u;assert(!map(bad,6u,33u));
    memcpy(bad,two,sizeof(bad));bad[0]=4u;assert(!map(bad,6u,33u));
    memcpy(bad,two,sizeof(bad));bad[3]=0u;assert(!map(bad,6u,33u));
    memcpy(bad,two,sizeof(bad));bad[4]=1001u;assert(!map(bad,6u,33u));
    assert(!kui_smash_menu_capsule_map(one,4u,100u,3u,1002u,2048u,10000u,1u,&runs));
    assert(!kui_smash_menu_capsule_map(one,4u,100u,8u,1002u,UINT32_MAX,10000u,1u,&runs));
    assert(!kui_smash_menu_capsule_map(one,4u,100u,8u,2002u,2048u,10000u,1u,&runs));
    assert(!kui_smash_menu_capsule_map(one,3u,100u,8u,1002u,2048u,10000u,1u,&runs));
    assert(!kui_smash_menu_capsule_map(one,4u,100u,8u,1002u,2048u,10000u,1u,NULL));
    assert(map(two,6u,33u));
    struct kui_retail_manifest game={.partition_start=2048u,.partition_end=12048u,.track_count=1u,.extent_count=1u};
    game.slots[1].extent=(struct kui_retail_extent){.card_lba=4000u,.blocks=50u};
    struct kui_vmu_persist_profile profile={0};
    profile.files[0].allocated_count=1u;
    profile.files[0].allocated[0]=(struct kui_vmu_persist_extent){.lba=5000u,.blocks=256u};
    assert(kui_smash_menu_capsule_disjoint(&runs,&game,&profile));
    game.slots[1].extent.card_lba=runs.second_lba+16u;
    assert(!kui_smash_menu_capsule_disjoint(&runs,&game,&profile));
    game.slots[1].extent.card_lba=runs.second_lba+17u;
    assert(kui_smash_menu_capsule_disjoint(&runs,&game,&profile));
    profile.files[0].allocated[0].lba=runs.first_lba-1u;
    profile.files[0].allocated[0].blocks=1u;
    assert(kui_smash_menu_capsule_disjoint(&runs,&game,&profile));
    profile.files[0].allocated[0].blocks=2u;
    assert(!kui_smash_menu_capsule_disjoint(&runs,&game,&profile));
    profile.files[0].allocated_count=0u;
    profile.files[2].allocated_count=1u;
    profile.files[2].allocated[0]=(struct kui_vmu_persist_extent){.lba=runs.second_lba+16u,.blocks=1u};
    assert(!kui_smash_menu_capsule_disjoint(&runs,&game,&profile));
    profile.files[2].allocated_count=17u;
    assert(!kui_smash_menu_capsule_disjoint(&runs,&game,&profile));
    profile.files[2].allocated_count=0u;
    game.partition_end=runs.second_lba+16u;
    assert(!kui_smash_menu_capsule_disjoint(&runs,&game,&profile));
    capsule_pair_ranges();
    puts("Smash menu capsule map: OK");return 0;
}
